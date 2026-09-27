#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include "modulator_bridge.h"

namespace cdt::render
{

class LightModulator
{
public:
    LightModulator() = default;
    ~LightModulator() { Shutdown(); }

    bool Initialize(ID3D12Device* device);
    void Shutdown();

    bool IsInitialized() const { return accepting_ && uploadBuffer_ != nullptr && mappedUpload_ != nullptr; }

    // Copies overrides directly into lightsBuffer using CopyBufferRegion.
    // lightsBuffer must be a D3D12 buffer in UNORDERED_ACCESS state with stride 48 (ManyLightRecord).
    // Each record has RGB at offset +16 (12 bytes) which is updated without touching attenuation (colorW).
    bool ApplyOverrides(ID3D12GraphicsCommandList* list,
                        ID3D12Resource* lightsBuffer,
                        const modulator::LightOverride* overrides,
                        uint32_t count);

    // Call after ExecuteCommandLists for every submission, including frames where
    // the periodic telemetry readback did not record a copy. The exact submitting
    // queue signals each slice's fence before its upload bytes can be reused.
    bool OnSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

    static constexpr uint32_t kRingSlices = 4;
    static constexpr uint32_t kMaxOverridesPerSlice = modulator::MaxLightOverrides; // 2048
    static constexpr uint32_t kPayloadSize = 12; // float rgb[3]
    static constexpr uint32_t kSliceSize = (kMaxOverridesPerSlice * kPayloadSize + 255) & ~255; // 24576 bytes

private:
    enum class SliceState { Free, AwaitingSubmit, Submitted, Uncertain };
    struct Slice
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pendingList;
        uint64_t fenceValue = 0;
        SliceState state = SliceState::Free;
    };

    void ReclaimCompleted();
    void ReleaseIfIdle();

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer_;
    Microsoft::WRL::ComPtr<IUnknown> deviceIdentity_;
    std::array<Slice, kRingSlices> slices_{};
    uint8_t* mappedUpload_ = nullptr;
    size_t uploadBufferSize_ = 0;
    uint32_t currentRingSlice_ = 0;
    bool accepting_ = false;
};

} // namespace cdt::render
