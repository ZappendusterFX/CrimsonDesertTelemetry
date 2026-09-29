// Synthetic controls for the production camera sky-visibility acquisition. WARP
// copies and direct observer calls; no hook installation, game memory or
// evidence of live game behavior.
#include "../src/spatial_acquire.cpp"
#include <dxgi1_4.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace cdt::sky
{
unsigned published{};
Visibility lastState{Visibility::Fallback};
double lastValue{-1};
uint32_t lastFrame{};
void PublishVisibility(double value, Visibility state, uint32_t frameNumber, uint64_t)
{
    ++published; lastState = state; lastValue = value; lastFrame = frameNumber;
}
}
namespace cdt::render { bool CaptureReady() { return false; } }
namespace cdt::sdf::acquire
{
void Start() {}
void ObserveContext(ID3D12GraphicsCommandList*, const std::array<std::uint8_t, 768>&,
    const std::array<float, 3>&, std::uint32_t, std::uint64_t, bool) {}
void Poll() {}
void Submission(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*, bool) {}
void Stop() {}
bool OwnsCodeAddress(std::uint64_t) { return false; }
}

namespace test
{
using Microsoft::WRL::ComPtr;
unsigned checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::cerr << "FAIL " << checks << ": " << message << '\n'; ExitProcess(1); }
}
void Hr(HRESULT result, const char* message) { Check(SUCCEEDED(result), message); }
uint64_t StubDispatch(uint64_t, uint32_t, uint32_t, uint32_t) { return 0x1234; }
template<class T> void Put(std::vector<uint8_t>& block, size_t offset, T value)
{ std::memcpy(block.data() + offset, &value, sizeof(value)); }
uint64_t Address(std::vector<uint8_t>& block) { return reinterpret_cast<uint64_t>(block.data()); }

struct Game
{
    std::vector<uint8_t> owner = std::vector<uint8_t>(0x4C0), renderer = std::vector<uint8_t>(0x668),
        outer = std::vector<uint8_t>(0x38), storage = std::vector<uint8_t>(0x108),
        command = std::vector<uint8_t>(0x808), holder = std::vector<uint8_t>(0x10),
        sceneOwner = std::vector<uint8_t>(0x430), sceneData = std::vector<uint8_t>(0x90);
    Game(ID3D12Resource* volume, ID3D12GraphicsCommandList* list)
    {
        Put(owner, 0x10, Address(renderer)); Put(renderer, 0x660, Address(owner));
        Put(owner, 0x4B8, Address(outer)); Put(outer, 0x30, Address(storage));
        Put(storage, 0x10, Address(outer)); Put(storage, 0x100, reinterpret_cast<uint64_t>(volume));
        Put(storage, 0xD0, 64u); Put(storage, 0xD4, 32u); Put(storage, 0xD8, 264u);
        Put(command, 0x800, Address(holder)); Put(holder, 8, reinterpret_cast<uint64_t>(list));
        Put(owner, 8, Address(sceneOwner)); Put(sceneOwner, 0x428, Address(sceneData));
        // GI constants whose reference decodes inside clipmap 1 (status Ok).
        for (unsigned axis = 0; axis < 3; ++axis) Put(owner, 0x20 + 0x10 + axis * 4, 1.0f);
        Put(owner, 0x20 + 0x150 + 12, 1.0f);
    }
    void Frame(uint32_t value) { Put(sceneData, 0x20, value); }
    uint64_t Dispatch() { return cdt::spatial::Dispatch(Address(command), 2, 1, 1, Address(owner), 0); }
};

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> volume, upload;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue{};
    void Wait()
    {
        Hr(queue->Signal(fence.Get(), ++fenceValue), "setup signal");
        for (int i = 0; i < 5000 && fence->GetCompletedValue() < fenceValue; ++i) Sleep(1);
        Check(fence->GetCompletedValue() >= fenceValue, "setup fence completed");
    }
    void Execute(bool observe)
    {
        Hr(list->Close(), "close list");
        ID3D12CommandList* lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        // The render ExecuteHook calls the observer after the original Execute.
        if (observe) cdt::spatial::OnSubmission(queue.Get(), 1, lists, true);
        Wait();
        Hr(allocator->Reset(), "reset allocator");
        Hr(list->Reset(allocator.Get(), nullptr), "reset list");
    }
    // An unsubmitted recording is discarded by the next Reset, never executed.
    void Discard()
    {
        Hr(list->Close(), "close discarded list");
        Hr(list->Reset(allocator.Get(), nullptr), "reset discarded list");
    }
};

void Create(Gpu& gpu, uint8_t texel)
{
    ComPtr<IDXGIFactory4> factory;
    Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<IDXGIAdapter> warp;
    Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
    Hr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)), "WARP device");
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Hr(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)), "compute queue");
    Hr(gpu.device->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&gpu.allocator)), "allocator");
    Hr(gpu.device->CreateCommandList(0, queueDesc.Type, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.list)), "list");
    Hr(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)), "setup fence");

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = 64; desc.Height = 32; desc.DepthOrArraySize = 264; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_TYPELESS; desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Hr(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&gpu.volume)), "3D GI volume");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total{};
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bufferDesc.Width = total; bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1; bufferDesc.MipLevels = 1; bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Hr(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&gpu.upload)), "upload buffer");
    void* mapped{};
    Hr(gpu.upload->Map(0, nullptr, &mapped), "map upload");
    std::memset(mapped, texel, static_cast<size_t>(total));
    gpu.upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = gpu.volume.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = gpu.upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
    gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpu.volume.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    gpu.list->ResourceBarrier(1, &barrier);
    gpu.Execute(false);
}

// Polls like the worker loop until the copy's fence completed and was published.
void PollUntilPublished(unsigned expected)
{
    for (int i = 0; i < 5000 && cdt::sky::published < expected; ++i) { cdt::spatial::Poll(); Sleep(1); }
}
}

int main()
{
    using namespace test;
    namespace spatial = cdt::spatial;
    namespace sky = cdt::sky;
    Gpu gpu;
    Create(gpu, 51); // 51/255 = 0.2 sampled, so sky visibility 0.8
    Game game(gpu.volume.Get(), gpu.list.Get());
    spatial::originalDispatch = StubDispatch;
    spatial::sampleAmbient = true;

    // 1. Before any observer call the render queue hook does not exist yet.
    game.Frame(1);
    Check(game.Dispatch() == 0x1234, "original dispatch result is returned");
    Check(!spatial::acquisitionInFlight && !spatial::activeList && !spatial::readbackBuffer,
        "no copy is recorded before the submission hook was observed");
    spatial::Poll();
    Check(sky::published == 0, "nothing published before the hook");

    // 2. The first observer call proves the hook; the next dispatch records.
    spatial::OnSubmission(gpu.queue.Get(), 0, nullptr, false);
    game.Frame(11);
    game.Dispatch();
    Check(spatial::acquisitionInFlight && spatial::activeList == gpu.list.Get(), "copy recorded after the hook");
    spatial::Poll();
    Check(sky::published == 0, "unsubmitted copy is not published");
    gpu.Execute(true);
    Check(!spatial::activeList && spatial::fenceValue == 1, "observed submission signals the fence");
    PollUntilPublished(1);
    Check(sky::published == 1 && sky::lastState == sky::Visibility::Valid, "fenced copy published as Valid");
    Check(std::fabs(sky::lastValue - 0.8) < 1e-6 && sky::lastFrame == 11, "published value and frame come from this copy");
    Check(!spatial::acquisitionInFlight, "acquisition re-armed after publication");

    // 3. A copy that is never submitted is abandoned after the timeout.
    game.Frame(12);
    game.Dispatch();
    auto* firstBuffer = spatial::readbackBuffer.Get();
    Check(spatial::acquisitionInFlight && spatial::activeList, "second copy recorded");
    spatial::Poll();
    Check(spatial::acquisitionInFlight && spatial::abandonedCopies == 0, "no abandonment before the timeout");
    spatial::recordedAt = GetTickCount64() - spatial::SubmissionTimeoutMs - 1;
    spatial::Poll();
    Check(!spatial::acquisitionInFlight && !spatial::activeList, "timed-out copy abandoned");
    Check(spatial::abandonedCopies == 1 && spatial::abandonedBuffers[0] == firstBuffer && !spatial::readbackBuffer,
        "abandoned buffer retained, never reused");
    Check(sky::published == 1, "abandonment publishes nothing by itself");
    gpu.Discard();

    // 4. Re-armed copy uses a new buffer and the same monotonic fence.
    auto* fence = spatial::readbackFence.Get();
    game.Frame(13);
    game.Dispatch();
    Check(spatial::acquisitionInFlight && spatial::readbackBuffer && spatial::readbackBuffer.Get() != firstBuffer,
        "re-armed copy records into a fresh buffer");
    Check(spatial::readbackFence.Get() == fence, "fence is kept across abandonment");
    gpu.Execute(true);
    Check(spatial::fenceValue == 2, "fence value continues monotonically");
    PollUntilPublished(2);
    Check(sky::published == 2 && sky::lastState == sky::Visibility::Valid && sky::lastFrame == 13 &&
        std::fabs(sky::lastValue - 0.8) < 1e-6, "re-armed copy publishes a current Valid sample");

    // 5. Abandonment is bounded; at the limit the path stops and reports Unavailable.
    for (uint32_t i = 0; i < spatial::MaximumAbandonedCopies - 1; ++i)
    {
        game.Frame(20 + i);
        game.Dispatch();
        Check(spatial::acquisitionInFlight, "copy recorded before bounded abandonment");
        spatial::recordedAt = GetTickCount64() - spatial::SubmissionTimeoutMs - 1;
        spatial::Poll();
        gpu.Discard();
    }
    Check(spatial::abandonedCopies == spatial::MaximumAbandonedCopies && !spatial::sampleAmbient,
        "sampling stops at the abandonment limit");
    Check(sky::published == 3 && sky::lastState == sky::Visibility::Unavailable, "limit publishes Unavailable");
    game.Frame(40);
    game.Dispatch();
    Check(!spatial::acquisitionInFlight && !spatial::readbackBuffer, "no copy after the limit");
    Hr(gpu.device->GetDeviceRemovedReason(), "WARP device healthy");
    for (auto* buffer : spatial::abandonedBuffers) if (buffer) buffer->Release();
    std::cout << "PASS " << checks << " spatial acquisition controls: hook-gated recording, fenced publication, "
        "bounded submission timeout with retained buffers\n";
    return 0;
}
