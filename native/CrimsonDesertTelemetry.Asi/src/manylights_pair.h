#pragma once
#include "render_bridge.h"
#include <d3d12.h>

namespace cdt::render
{
// Private diagnostic only. File = header, paired scene, output, output counter,
// INPUT full capacity. Input has NO inferred live count and NO public API decode.
inline uint64_t PairInputOwnerOffset = 0x628;
constexpr size_t PairInputOffset = LightBytes + CounterBytes;
constexpr size_t PairCopyBytes = PairInputOffset + LightBytes;
struct alignas(8) ManyLightsPairHeader
{
    uint32_t magic = 0x50445443, version = 1, headerBytes = 128, sceneBytes = SceneBytes;
    uint32_t count = RecordCount, stride = RecordStride, counterBytes = CounterBytes, flags = 0;
    uint32_t pid{}, frame{}, bank{}, reserved{};
    uint64_t capturedTick{}, completedTick{}, fenceValue{}, owner{};
    uint64_t input{}, output{}, counter{}, build = 25477059, processStart{}, reserved2{};
};
static_assert(sizeof(ManyLightsPairHeader) == 128);
inline D3D12_BUFFER_BARRIER PairInputBarrier(ID3D12Resource* resource, bool restore)
{
    D3D12_BUFFER_BARRIER b{};
    b.pResource = resource; b.Size = UINT64_MAX;
    b.SyncBefore = restore ? D3D12_BARRIER_SYNC_COPY : D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    b.SyncAfter = restore ? D3D12_BARRIER_SYNC_COMPUTE_SHADING : D3D12_BARRIER_SYNC_COPY;
    b.AccessBefore = restore ? D3D12_BARRIER_ACCESS_COPY_SOURCE : D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    b.AccessAfter = restore ? D3D12_BARRIER_ACCESS_SHADER_RESOURCE : D3D12_BARRIER_ACCESS_COPY_SOURCE;
    return b;
}
// Call before StartCapture. No new hook; exact existing hook/context + input load.
bool EnableManyLightsPair(uint64_t moduleBase, const wchar_t* directory);
// Continuous ManyLights INPUT: the engine's light records BEFORE view selection,
// copied with every filtered sample at the same hook, list and fence. Same exact
// anchors as the pair diagnostic. Call before StartCapture; the bridge must have
// been opened with its input block.
bool EnableUpstreamInput(uint64_t moduleBase);
}
