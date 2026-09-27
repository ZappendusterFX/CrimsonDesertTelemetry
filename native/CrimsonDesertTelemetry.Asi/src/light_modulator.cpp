#include "light_modulator.h"
#include "device_identity.h"
#include <array>
#include <cmath>
#include <utility>

namespace cdt::render
{

bool LightModulator::Initialize(ID3D12Device* device)
{
    if (!device) return false;
    Shutdown();
    // An earlier command list may still reference this upload allocation. Never
    // replace it without a completed GPU fence or proof that it was not submitted.
    if (uploadBuffer_) return false;

    IUnknown* identity{};
    if (FAILED(device_identity::CanonicalDeviceIdentity(device, &identity))) return false;
    deviceIdentity_.Attach(identity);

    uploadBufferSize_ = kRingSlices * kSliceSize; // 4 * 24576 = 98304 bytes

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = uploadBufferSize_;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&uploadBuffer_));
    if (FAILED(hr)) { Shutdown(); return false; }

    hr = uploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedUpload_));
    if (FAILED(hr))
    {
        Shutdown();
        return false;
    }

    for (auto& slice : slices_)
    {
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&slice.fence));
        if (FAILED(hr)) { Shutdown(); return false; }
    }

    currentRingSlice_ = 0;
    accepting_ = true;
    return true;
}

void LightModulator::Shutdown()
{
    accepting_ = false;
    ReclaimCompleted();
    ReleaseIfIdle();
}

void LightModulator::ReclaimCompleted()
{
    for (auto& slice : slices_)
    {
        if (slice.state != SliceState::Submitted || !slice.fence) continue;
        const uint64_t completed = slice.fence->GetCompletedValue();
        // UINT64_MAX is D3D12's device-removed sentinel, not completion proof.
        if (completed != UINT64_MAX && completed >= slice.fenceValue)
        {
            slice.state = SliceState::Free;
        }
    }
}

void LightModulator::ReleaseIfIdle()
{
    if (accepting_) return;
    for (const auto& slice : slices_)
        if (slice.state != SliceState::Free) return;

    if (uploadBuffer_ && mappedUpload_)
    {
        uploadBuffer_->Unmap(0, nullptr);
        mappedUpload_ = nullptr;
    }
    uploadBuffer_.Reset();
    deviceIdentity_.Reset();
    for (auto& slice : slices_)
    {
        slice.fence.Reset();
        slice.pendingList.Reset();
        slice.fenceValue = 0;
    }
    uploadBufferSize_ = 0;
    currentRingSlice_ = 0;
}

bool LightModulator::ApplyOverrides(ID3D12GraphicsCommandList* list,
                                    ID3D12Resource* lightsBuffer,
                                    const modulator::LightOverride* overrides,
                                    uint32_t count)
{
    if (!IsInitialized() || !list || !lightsBuffer || !overrides || count == 0)
    {
        return false;
    }

    const auto desc = lightsBuffer->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0 ||
        (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT && list->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)) return false;

    IUnknown* sourceDevice{};
    IUnknown* listDevice{};
    const bool sameDevice = SUCCEEDED(device_identity::ChildDeviceIdentity(lightsBuffer, &sourceDevice)) &&
        SUCCEEDED(device_identity::ChildDeviceIdentity(list, &listDevice)) &&
        sourceDevice == deviceIdentity_.Get() && listDevice == deviceIdentity_.Get();
    if (sourceDevice) sourceDevice->Release();
    if (listDevice) listDevice->Release();
    if (!sameDevice) return false;

    struct LightRgbPayload
    {
        float rgb[3];
    };

    uint32_t validCount = 0;
    std::array<std::pair<uint32_t, LightRgbPayload>, modulator::MaxLightOverrides> toApply;

    for (uint32_t i = 0; i < count && validCount < toApply.size(); ++i)
    {
        if (overrides[i].enabled != 0 && overrides[i].lightIndex < 32768 &&
            static_cast<uint64_t>(overrides[i].lightIndex) < desc.Width / 48 &&
            std::isfinite(overrides[i].rgb[0]) && std::isfinite(overrides[i].rgb[1]) &&
            std::isfinite(overrides[i].rgb[2]))
        {
            LightRgbPayload payload{};
            payload.rgb[0] = overrides[i].rgb[0];
            payload.rgb[1] = overrides[i].rgb[1];
            payload.rgb[2] = overrides[i].rgb[2];
            toApply[validCount++] = { overrides[i].lightIndex, payload };
        }
    }

    if (validCount == 0) return true;

    // A slice is free only after its exact queue has signaled and completed its
    // fence. If the GPU is four submissions behind, skip this modulation frame.
    ReclaimCompleted();
    uint32_t slice = kRingSlices;
    for (uint32_t i = 0; i < kRingSlices; ++i)
    {
        const uint32_t candidate = (currentRingSlice_ + i) % kRingSlices;
        if (slices_[candidate].state == SliceState::Free) { slice = candidate; break; }
    }
    if (slice == kRingSlices) return false;
    currentRingSlice_ = (slice + 1) % kRingSlices;
    slices_[slice].pendingList = list;
    slices_[slice].state = SliceState::AwaitingSubmit;
    const uint64_t sliceOffset = static_cast<uint64_t>(slice) * kSliceSize;
    uint8_t* sliceDst = mappedUpload_ + sliceOffset;

    // Write payloads into upload buffer slice
    for (uint32_t i = 0; i < validCount; ++i)
    {
        auto* dst = reinterpret_cast<LightRgbPayload*>(sliceDst + i * sizeof(LightRgbPayload));
        *dst = toApply[i].second;
    }

    // Barrier: UNORDERED_ACCESS -> COPY_DEST
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.pResource = lightsBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &barrier);

    // Issue CopyBufferRegion for each override:
    // Offset +16 in each 48-byte record is the float3 rgb (12 bytes).
    // color[3] (offset +28) is attenuation/radius factor and remains UNTOUCHED!
    for (uint32_t i = 0; i < validCount; ++i)
    {
        const uint64_t dstOffset = static_cast<uint64_t>(toApply[i].first) * 48 + 16;
        const uint64_t srcOffset = sliceOffset + static_cast<uint64_t>(i) * sizeof(LightRgbPayload);
        list->CopyBufferRegion(lightsBuffer, dstOffset, uploadBuffer_.Get(), srcOffset, sizeof(LightRgbPayload));
    }

    // Barrier: COPY_DEST -> UNORDERED_ACCESS
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1, &barrier);

    return true;
}

bool LightModulator::OnSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    if (!uploadBuffer_ || !queue || !lists || count == 0) return true;

    bool matched = false;
    for (const auto& slice : slices_)
    {
        if (slice.state != SliceState::AwaitingSubmit) continue;
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == static_cast<ID3D12CommandList*>(slice.pendingList.Get())) { matched = true; break; }
        if (matched) break;
    }
    if (!matched) return true;

    IUnknown* queueDevice{};
    const bool sameDevice = SUCCEEDED(device_identity::ChildDeviceIdentity(queue, &queueDevice)) &&
        queueDevice == deviceIdentity_.Get();
    if (queueDevice) queueDevice->Release();

    bool success = true;
    for (auto& slice : slices_)
    {
        if (slice.state != SliceState::AwaitingSubmit) continue;
        bool containsList = false;
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == static_cast<ID3D12CommandList*>(slice.pendingList.Get())) { containsList = true; break; }
        if (!containsList) continue;

        const bool compatible = sameDevice && queue->GetDesc().Type == slice.pendingList->GetType();
        const HRESULT signal = compatible ? queue->Signal(slice.fence.Get(), slice.fenceValue + 1) : E_INVALIDARG;
        if (FAILED(signal))
        {
            // The commands may be in flight. Keep the upload allocation and stop
            // accepting new payloads when completion cannot be established.
            slice.state = SliceState::Uncertain;
            accepting_ = false;
            success = false;
        }
        else
        {
            ++slice.fenceValue;
            slice.state = SliceState::Submitted;
            slice.pendingList.Reset();
        }
    }
    ReclaimCompleted();
    ReleaseIfIdle();
    return success;
}

} // namespace cdt::render
