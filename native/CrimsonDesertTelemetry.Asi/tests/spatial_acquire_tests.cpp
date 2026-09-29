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

// Minimal COM stand-ins: a queue whose Signal fails or never reaches the fence,
// and a fence reporting device removal. Only the called slots are populated.
HRESULT failedSignal{E_FAIL};
unsigned signalCalls{};
HRESULT STDMETHODCALLTYPE FakeSignal(void*, ID3D12Fence*, UINT64) { ++signalCalls; return failedSignal; }
ULONG STDMETHODCALLTYPE FakeAddRef(void*) { return 2; }
ULONG STDMETHODCALLTYPE FakeRelease(void*) { return 1; }
UINT64 STDMETHODCALLTYPE RemovedValue(void*) { return UINT64_MAX; }
struct FakeObject { void** vtable; };
std::array<void*, 16> queueTable{}, fenceTable{};
FakeObject fakeQueue{queueTable.data()}, removedFence{fenceTable.data()};

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
        Put(command, 0x800, Address(holder)); List(list);
        Put(owner, 8, Address(sceneOwner)); Put(sceneOwner, 0x428, Address(sceneData));
        // GI constants whose reference decodes inside clipmap 1 (status Ok).
        for (unsigned axis = 0; axis < 3; ++axis) Put(owner, 0x20 + 0x10 + axis * 4, 1.0f);
        Put(owner, 0x20 + 0x150 + 12, 1.0f);
    }
    void List(ID3D12GraphicsCommandList* list) { Put(holder, 8, reinterpret_cast<uint64_t>(list)); }
    void Frame(uint32_t value) { Put(sceneData, 0x20, value); }
    uint64_t Dispatch() { return cdt::spatial::Dispatch(Address(command), 2, 1, 1, Address(owner), 0); }
};

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    // Index 2 is a setup list for volume uploads, never seen by the acquisition.
    std::array<ComPtr<ID3D12CommandAllocator>, 3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 3> lists;
    ComPtr<ID3D12Resource> volume, upload;
    ComPtr<ID3D12Fence> fence;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t fenceValue{}, uploadBytes{};
    void Wait()
    {
        Hr(queue->Signal(fence.Get(), ++fenceValue), "setup signal");
        for (int i = 0; i < 5000 && fence->GetCompletedValue() < fenceValue; ++i) Sleep(1);
        Check(fence->GetCompletedValue() >= fenceValue, "setup fence completed");
    }
    // Executes a list; the render ExecuteHook calls the observer with the
    // submitting queue after the original Execute. A null observer queue means
    // the submission was not observed.
    void Execute(size_t index, ID3D12CommandQueue* observerQueue)
    {
        Hr(lists[index]->Close(), "close list");
        ID3D12CommandList* submitted[]{lists[index].Get()};
        queue->ExecuteCommandLists(1, submitted);
        if (observerQueue) cdt::spatial::OnSubmission(observerQueue, 1, submitted, true);
        Wait();
        Hr(allocators[index]->Reset(), "reset allocator");
        Hr(lists[index]->Reset(allocators[index].Get(), nullptr), "reset list");
    }
    // An unsubmitted recording is discarded by the next Reset, never executed.
    void Discard(size_t index)
    {
        Hr(lists[index]->Close(), "close discarded list");
        Hr(lists[index]->Reset(allocators[index].Get(), nullptr), "reset discarded list");
    }
};

// Rewrites every texel of the GI volume; it is kept in COPY_SOURCE between uses.
void Fill(Gpu& gpu, uint8_t texel, bool initial)
{
    void* mapped{};
    Hr(gpu.upload->Map(0, nullptr, &mapped), "map upload");
    std::memset(mapped, texel, static_cast<size_t>(gpu.uploadBytes));
    gpu.upload->Unmap(0, nullptr);
    auto& list = gpu.lists[2];
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpu.volume.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    if (!initial) list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = gpu.volume.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = gpu.upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = gpu.footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    gpu.Execute(2, nullptr);
}

void Create(Gpu& gpu, uint8_t texel)
{
    ComPtr<IDXGIFactory4> factory;
    Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<IDXGIAdapter> warp;
    Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
    Hr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)), "WARP device");
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Hr(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)), "compute queue");
    for (size_t i = 0; i < gpu.lists.size(); ++i)
    {
        Hr(gpu.device->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&gpu.allocators[i])), "allocator");
        Hr(gpu.device->CreateCommandList(0, queueDesc.Type, gpu.allocators[i].Get(), nullptr,
            IID_PPV_ARGS(&gpu.lists[i])), "list");
    }
    Hr(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)), "setup fence");

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = 64; desc.Height = 32; desc.DepthOrArraySize = 264; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_TYPELESS; desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Hr(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&gpu.volume)), "3D GI volume");
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &gpu.footprint, nullptr, nullptr, &gpu.uploadBytes);
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bufferDesc.Width = gpu.uploadBytes; bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1; bufferDesc.MipLevels = 1; bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Hr(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&gpu.upload)), "upload buffer");
    Fill(gpu, texel, true);
}

// Polls like the worker loop until the copy's fence completed and was published.
void PollUntilPublished(unsigned expected)
{
    for (int i = 0; i < 5000 && cdt::sky::published < expected; ++i) { cdt::spatial::Poll(); Sleep(1); }
}
bool BufferHolds(ID3D12Resource* buffer, uint8_t texel)
{
    void* mapped{};
    if (FAILED(buffer->Map(0, nullptr, &mapped))) return false;
    const bool holds = static_cast<const uint8_t*>(mapped)[0] == texel;
    buffer->Unmap(0, nullptr);
    return holds;
}
}

int main()
{
    using namespace test;
    namespace spatial = cdt::spatial;
    namespace sky = cdt::sky;
    queueTable[14] = reinterpret_cast<void*>(&FakeSignal);           // ID3D12CommandQueue::Signal
    fenceTable[1] = reinterpret_cast<void*>(&FakeAddRef);
    fenceTable[2] = reinterpret_cast<void*>(&FakeRelease);
    fenceTable[8] = reinterpret_cast<void*>(&RemovedValue);          // ID3D12Fence::GetCompletedValue
    auto* failingQueue = reinterpret_cast<ID3D12CommandQueue*>(&fakeQueue);
    Gpu gpu;
    Create(gpu, 51); // 51/255 = 0.2 sampled, so sky visibility 0.8
    Game game(gpu.volume.Get(), gpu.lists[0].Get());
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
    Check(spatial::acquisitionInFlight && spatial::activeList == gpu.lists[0].Get(), "copy recorded after the hook");
    spatial::Poll();
    Check(sky::published == 0, "unsubmitted copy is not published");
    gpu.Execute(0, gpu.queue.Get());
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
    gpu.Discard(0);

    // 4. Re-armed copy uses a new buffer and the same monotonic fence.
    auto* fence = spatial::readbackFence.Get();
    game.Frame(13);
    game.Dispatch();
    Check(spatial::acquisitionInFlight && spatial::readbackBuffer && spatial::readbackBuffer.Get() != firstBuffer,
        "re-armed copy records into a fresh buffer");
    Check(spatial::readbackFence.Get() == fence, "fence is kept across abandonment");
    gpu.Execute(0, gpu.queue.Get());
    Check(spatial::fenceValue == 2, "fence value continues monotonically");
    PollUntilPublished(2);
    Check(sky::published == 2 && sky::lastState == sky::Visibility::Valid && sky::lastFrame == 13 &&
        std::fabs(sky::lastValue - 0.8) < 1e-6, "re-armed copy publishes a current Valid sample");

    // 5. An abandoned list executed late writes only its retained buffer, never
    // signals, and does not disturb the copy recorded after it.
    game.Frame(14);
    game.Dispatch();
    auto* lateBuffer = spatial::readbackBuffer.Get();
    spatial::recordedAt = GetTickCount64() - spatial::SubmissionTimeoutMs - 1;
    spatial::Poll();
    Check(spatial::abandonedCopies == 2 && spatial::abandonedBuffers[1] == lateBuffer, "late list abandoned");
    Check(BufferHolds(lateBuffer, 51), "retained buffer still holds the previous copy");
    game.List(gpu.lists[1].Get());
    game.Frame(15);
    game.Dispatch();
    Check(spatial::activeList == gpu.lists[1].Get() && spatial::readbackBuffer.Get() != lateBuffer,
        "new copy recorded on another list into a fresh buffer");
    Fill(gpu, 102, false); // 102/255 = 0.4 sampled, so sky visibility 0.6
    gpu.Execute(0, gpu.queue.Get());
    Check(spatial::fenceValue == 2 && spatial::activeList == gpu.lists[1].Get(),
        "late observed execution of the abandoned list signals nothing");
    Check(BufferHolds(lateBuffer, 102), "late execution wrote its retained buffer");
    gpu.Execute(1, gpu.queue.Get());
    PollUntilPublished(3);
    Check(sky::published == 3 && sky::lastFrame == 15 && spatial::fenceValue == 3 &&
        std::fabs(sky::lastValue - 0.6) < 1e-6, "copy after the late list publishes its own current sample");

    // 6. A failed Signal is abandoned at the next poll, never mapped.
    game.Frame(16);
    game.Dispatch();
    failedSignal = E_FAIL;
    gpu.Execute(1, failingQueue);
    Check(signalCalls == 1 && spatial::signalFailed && spatial::acquisitionInFlight, "failed Signal recorded");
    spatial::Poll();
    Check(spatial::abandonedCopies == 3 && !spatial::acquisitionInFlight && !spatial::signalFailed &&
        sky::published == 3, "failed Signal abandoned without publication");

    // 7. A fence still incomplete after the GPU timeout is abandoned.
    game.Frame(17);
    game.Dispatch();
    failedSignal = S_OK; // accepted, but this stand-in never signals the fence
    gpu.Execute(1, failingQueue);
    spatial::Poll();
    Check(spatial::acquisitionInFlight && spatial::abandonedCopies == 3, "no abandonment before the GPU timeout");
    spatial::submittedAt = GetTickCount64() - spatial::GpuTimeoutMs - 1;
    spatial::Poll();
    Check(spatial::abandonedCopies == 4 && !spatial::acquisitionInFlight && sky::published == 3,
        "GPU timeout abandons the copy without publication");

    // 8. Abandonment is bounded; at the limit the path stops and reports Unavailable.
    for (uint32_t i = 0; spatial::abandonedCopies < spatial::MaximumAbandonedCopies; ++i)
    {
        game.Frame(20 + i);
        game.Dispatch();
        Check(spatial::acquisitionInFlight, "copy recorded before bounded abandonment");
        spatial::recordedAt = GetTickCount64() - spatial::SubmissionTimeoutMs - 1;
        spatial::Poll();
        gpu.Discard(1);
    }
    Check(!spatial::sampleAmbient && sky::published == 4 && sky::lastState == sky::Visibility::Unavailable,
        "limit stops sampling and publishes Unavailable");
    game.Frame(40);
    game.Dispatch();
    Check(!spatial::acquisitionInFlight && !spatial::readbackBuffer, "no copy after the limit");

    // 9. Device removal (completed value UINT64_MAX) is never treated as a finished copy.
    spatial::sampleAmbient = true;
    game.Frame(41);
    game.Dispatch();
    Check(spatial::acquisitionInFlight && spatial::readbackBuffer, "copy recorded for the removal control");
    spatial::readbackFence.Reset();
    spatial::readbackFence.Attach(reinterpret_cast<ID3D12Fence*>(&removedFence));
    gpu.Execute(1, failingQueue);
    spatial::Poll();
    Check(!spatial::acquisitionInFlight && !spatial::sampleAmbient && sky::published == 5 &&
        sky::lastState == sky::Visibility::Unavailable && sky::lastFrame == 0,
        "device removal stops the path as Unavailable without mapping");
    game.Frame(42);
    game.Dispatch();
    Check(!spatial::acquisitionInFlight, "no copy after device removal");

    Hr(gpu.device->GetDeviceRemovedReason(), "WARP device healthy");
    for (auto* buffer : spatial::abandonedBuffers) if (buffer) buffer->Release();
    std::cout << "PASS " << checks << " spatial acquisition controls: hook-gated recording, fenced publication, "
        "submission/Signal/GPU timeouts with retained buffers, late abandoned execution, device removal\n";
    return 0;
}
