#include "spatial_probe.h"
#include "spatial_sample.h"
#include "sky_bridge.h"
#include "submission_observer.h"
#include "sdf_acquire.h"
#include "render_bridge.h"
#include "render_capture.h"
#include "native_contract.generated.h"
#include "console/mem.h"
#include <windows.h>
#include <d3d12.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <wrl/client.h>
#include <mutex>

extern "C" void CdtSpatialThunk();
namespace cdt::spatial
{
namespace
{
// Exact-build spatial acquisition. The private all-on package exercises this
// candidate without promoting the build to a supported release.
constexpr bool UpdatedGameBuild = native_contract::BuildId == "25477059";
constexpr uint32_t DispatchRva = UpdatedGameBuild ? 0x389C000 : 0x37B6520;
constexpr uint32_t ExposureReturnRva = UpdatedGameBuild ? 0x362595A : 0x3547264;
constexpr std::array<uint8_t, 14> Signature{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56};
using DispatchFn = uint64_t(*)(uint64_t, uint32_t, uint32_t, uint32_t);
DispatchFn originalDispatch{};
void* dispatchTarget{};
uint64_t gameBase{};
bool sampleAmbient{}, requestedSources{};
std::atomic<bool> sampleSources{};

Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
Microsoft::WRL::ComPtr<ID3D12Fence> readbackFence;
uint64_t fenceValue = 0;
bool acquisitionInFlight = false;
uint32_t latestFrame = 0;
size_t activeRowPitch = 0;
ID3D12GraphicsCommandList* activeList = nullptr;
// The sampler consumes the complete CPU GI upload-source block, not scene data.
std::array<uint8_t, ConstantBytes> activeGi{};
std::mutex acquireMutex;
// OnSubmission runs only inside the render queue hook, installed after the
// playable-world gate. A copy recorded earlier cannot be observed: on 25477059
// one was recorded at frame 1 and cleared only because that list happened to be
// reused in-world (measured 2026-09-29). Record only once a call proves the hook.
std::atomic<bool> submissionObserved{};
// A list still unobserved after this, a failed Signal, or a fence still
// incomplete after the GPU timeout abandons the copy and re-arms it.
constexpr uint64_t SubmissionTimeoutMs = 5000, GpuTimeoutMs = 5000;
uint64_t recordedAt = 0, submittedAt = 0;
bool signalFailed = false;
// An abandoned list can still be submitted unobserved and then write its buffer.
// Such a buffer is never read or released again, not even at unload; bounded.
constexpr size_t MaximumAbandonedCopies = 8;
std::array<ID3D12Resource*, MaximumAbandonedCopies> abandonedBuffers{};
size_t abandonedCopies = 0;

template<class T> bool Read(uint64_t address, T& value)
{
    return address >= 0x10000 && ch::mem::SafeRead(reinterpret_cast<void*>(address), &value, sizeof(value));
}

bool Resolve(uint64_t owner, uint64_t command, ID3D12Resource*& resource, ID3D12GraphicsCommandList*& nativeList, uint32_t& frame, std::array<uint8_t, ConstantBytes>& giArray)
{
    uint64_t renderer{}, back{}, outer{}, storage{}, holder{}, sceneOwner{}, sceneData{};
    uint32_t width{}, height{}, depth{};
    if (!Read(owner + 0x10, renderer) || !Read(renderer + 0x660, back) || back != owner ||
        !Read(owner + 0x4B8, outer) || !Read(outer + 0x30, storage) ||
        !Read(storage + 0x10, back) || back != outer || !Read(storage + 0x100, reinterpret_cast<uint64_t&>(resource)) || !resource ||
        !Read(storage + 0xD0, width) || !Read(storage + 0xD4, height) || !Read(storage + 0xD8, depth) ||
        width != 64 || height != 32 || depth != 264 || !Read(command + 0x800, holder) ||
        !Read(holder + 8, reinterpret_cast<uint64_t&>(nativeList)) || !nativeList ||
        !Read(owner + 8, sceneOwner) || !Read(sceneOwner + 0x428, sceneData) ||
        !Read(sceneData + 0x20, frame) || !Read(owner + 0x20, giArray)) return false;
    
    auto desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D || desc.Width != 64 || desc.Format != DXGI_FORMAT_R8_TYPELESS) return false;
    return true;
}

void OnSubmission(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists, bool after)
{
    submissionObserved.store(true, std::memory_order_release);
    if(sampleSources)sdf::acquire::Submission(queue,count,lists,after);
    std::lock_guard<std::mutex> lock(acquireMutex);
    if (!acquisitionInFlight || !after || !readbackFence || !activeList) return;
    
    bool listFound = false;
    for (UINT i = 0; i < count; ++i) {
        if (lists[i] == activeList) { listFound = true; break; }
    }
    
    if (listFound) {
        fenceValue++;
        // A failed Signal leaves this value unreachable; Poll abandons the copy.
        signalFailed = FAILED(queue->Signal(readbackFence.Get(), fenceValue));
        submittedAt = GetTickCount64();
        activeList = nullptr;
    }
}

// Caller holds acquireMutex.
void Abandon()
{
    abandonedBuffers[abandonedCopies++] = readbackBuffer.Detach();
    activeList = nullptr;
    acquisitionInFlight = false;
    signalFailed = false;
    if (abandonedCopies == MaximumAbandonedCopies)
    {
        // Stop re-arming for this process instead of retaining more buffers.
        sampleAmbient = false;
        sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
    }
}

} // namespace

uint64_t Dispatch(uint64_t command, uint32_t x, uint32_t y, uint32_t z, uint64_t owner, uint64_t caller)
{
    std::array<uint8_t,ConstantBytes> giBefore{};
    const bool observeSources=sampleSources&&caller==gameBase+ExposureReturnRva&&
        x==2&&y==1&&z==1&&Read(owner+0x20,giBefore);
    auto result = originalDispatch(command, x, y, z);
    
    ID3D12Resource* resource{};
    ID3D12GraphicsCommandList* list{};
    uint32_t frame{};
    std::array<uint8_t, ConstantBytes> giArray{};
    
    if (x == 2 && y == 1 && z == 1 && Resolve(owner, command, resource, list, frame, giArray))
    {
        if(observeSources)
        {
            uint64_t sceneOwner{},sceneData{};std::array<float,3> camera{};
            if(Read(owner+8,sceneOwner)&&Read(sceneOwner+0x428,sceneData)&&Read(sceneData+0x80,camera))
                sdf::acquire::ObserveContext(list,giArray,camera,frame,GetTickCount64(),giBefore==giArray);
        }
        std::lock_guard<std::mutex> lock(acquireMutex);
        if(!sampleAmbient||acquisitionInFlight||!submissionObserved.load(std::memory_order_acquire))return result;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&device))))
        {
            if (!readbackBuffer)
            {
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                uint32_t rows{};
                uint64_t rowBytes{}, totalBytes{};
                auto desc = resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
                
                D3D12_RESOURCE_DESC bufferDesc{};
                bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bufferDesc.Alignment = 0;
                bufferDesc.Width = totalBytes;
                bufferDesc.Height = 1;
                bufferDesc.DepthOrArraySize = 1;
                bufferDesc.MipLevels = 1;
                bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
                bufferDesc.SampleDesc.Count = 1;
                bufferDesc.SampleDesc.Quality = 0;
                bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
                
                D3D12_HEAP_PROPERTIES heapProperties{};
                heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
                heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapProperties.CreationNodeMask = 1;
                heapProperties.VisibleNodeMask = 1;
                
                // After an abandoned copy only the buffer is replaced; the fence
                // and its monotonic value continue.
                if (SUCCEEDED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuffer))) &&
                    !readbackFence)
                {
                    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&readbackFence));
                }
            }
            
            if (readbackBuffer && readbackFence)
            {
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                uint32_t rows{};
                uint64_t rowBytes{}, totalBytes{};
                auto desc = resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
                
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                dst.pResource = readbackBuffer.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = footprint;
                
                src.pResource = resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                
                activeList = list;
                activeRowPitch = footprint.Footprint.RowPitch;
                activeGi = giArray;
                latestFrame = frame;
                recordedAt = GetTickCount64();
                acquisitionInFlight = true;
            }
        }
    }
    return result;
}

} // namespace cdt::spatial

extern "C" uint64_t CdtSpatialDispatch(uint64_t command, uint32_t x, uint32_t y, uint32_t z, uint64_t owner, uint64_t caller)
{
    return cdt::spatial::Dispatch(command, x, y, z, owner, caller);
}

namespace cdt::spatial
{
bool Start(uint64_t moduleBase, const wchar_t*, bool enableReadback, unsigned, unsigned, unsigned,
    bool enableDistanceReadback, bool, bool)
{
    if (!enableReadback&&!enableDistanceReadback) return false;
    std::array<uint8_t, Signature.size()> bytes{};
    if (!Read(moduleBase + DispatchRva, bytes) || bytes != Signature) return false;
    
    dispatchTarget = reinterpret_cast<void*>(moduleBase + DispatchRva);
    gameBase=moduleBase;sampleAmbient=enableReadback;requestedSources=enableDistanceReadback;
    sampleSources.store(false, std::memory_order_release);
    if (MH_CreateHook(dispatchTarget, reinterpret_cast<void*>(CdtSpatialThunk), reinterpret_cast<void**>(&originalDispatch)) != MH_OK) return false;
    if (MH_EnableHook(dispatchTarget) != MH_OK)
        return false;
    
    render::submissionObserver = OnSubmission;
    return true;
}

void Poll()
{
    // The previous package started SDF interception during the loading screens.
    // On 25477059 that caught frames 7/8 with a zero camera and permanently
    // failed when the loading command list reset before confirmed submission.
    // Arm only after the existing playable-world gate opens; never reuse those
    // early loading resources as visibility evidence.
    if (requestedSources && !sampleSources.load(std::memory_order_acquire) && render::CaptureReady())
    {
        sdf::acquire::Start();
        sampleSources.store(true, std::memory_order_release);
    }
    if(sampleSources)sdf::acquire::Poll();
    std::lock_guard<std::mutex> lock(acquireMutex);
    if (!acquisitionInFlight) return;
    const auto now = GetTickCount64();
    if (activeList)
    {
        if (now - recordedAt > SubmissionTimeoutMs) Abandon();
        return;
    }
    if (!readbackFence || !readbackBuffer) return;
    if (signalFailed) { Abandon(); return; }
    const auto completed = readbackFence->GetCompletedValue();
    if (completed == UINT64_MAX)
    {
        // Device removed: this is not a completed copy. Never map or re-arm.
        acquisitionInFlight = false;
        sampleAmbient = false;
        sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
        return;
    }
    if (completed < fenceValue)
    {
        if (now - submittedAt > GpuTimeoutMs) Abandon();
        return;
    }
    {
        void* mappedData = nullptr;
        if (SUCCEEDED(readbackBuffer->Map(0, nullptr, &mappedData)))
        {
            uint8_t* volume = static_cast<uint8_t*>(mappedData);
            
            auto reference = SampleAtReference(activeGi.data(), volume, activeRowPitch);
            auto state = reference.status == SampleStatus::Ok ? sky::Visibility::Valid 
                       : reference.status == SampleStatus::Fallback ? sky::Visibility::Fallback 
                       : sky::Visibility::Unavailable;
                       
            sky::PublishVisibility(reference.skyVisibility, state, latestFrame, GetTickCount64());
            
            readbackBuffer->Unmap(0, nullptr);
        }
        else
        {
            sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
        }
        
        acquisitionInFlight = false;
    }
}

void Stop()
{
    render::submissionObserver = nullptr;
    if (dispatchTarget) MH_DisableHook(dispatchTarget);
    if (sampleSources.exchange(false, std::memory_order_acq_rel)) sdf::acquire::Stop();
    requestedSources = false;
    readbackBuffer.Reset();
    readbackFence.Reset();
    acquisitionInFlight = false;
    sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
}

bool OwnsCodeAddress(uint64_t address)
{
    return (dispatchTarget && address >= reinterpret_cast<uint64_t>(dispatchTarget) && address < reinterpret_cast<uint64_t>(dispatchTarget) + 32)||
        (sampleSources&&sdf::acquire::OwnsCodeAddress(address));
}
}
