#include "light_modulator.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace
{
void Check(bool cond, const char* msg)
{
    if (!cond)
    {
        std::cerr << "FAIL: " << msg << std::endl;
        ExitProcess(1);
    }
}

void Hr(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        std::cerr << "FAIL HRESULT 0x" << std::hex << hr << std::dec << ": " << msg << std::endl;
        ExitProcess(1);
    }
}

struct ManyLightRecord
{
    float relPos[3];
    float pad0;
    float rgb[3];
    float colorW;
    uint32_t packedDir0[2];
    uint32_t packedDir1[2];
};
static_assert(sizeof(ManyLightRecord) == 48);
} // namespace

int main()
{
    std::cout << "Starting LightModulator DMA test on D3D12 WARP..." << std::endl;

    // 1. Create WARP Device
    ComPtr<IDXGIFactory4> factory;
    Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter> warpAdapter;
    Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "EnumWarpAdapter");

    ComPtr<ID3D12Device> device;
    Hr(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");

    // 2. Command Queue, Allocator, List
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Hr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

    ComPtr<ID3D12CommandAllocator> allocator;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");

    ComPtr<ID3D12GraphicsCommandList> list;
    Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList");

    // 3. Initialize LightModulator
    cdt::render::LightModulator modulator;
    Check(modulator.Initialize(device.Get()), "modulator.Initialize");

    // 4. Create buffers:
    const size_t numLights = 4;
    const size_t lightsByteSize = numLights * sizeof(ManyLightRecord);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = lightsByteSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> lightsBuffer;
    Hr(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&lightsBuffer)),
       "Create lightsBuffer");

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    bufferDesc.Width = lightsByteSize;

    ComPtr<ID3D12Resource> lightsUpload;
    Hr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lightsUpload)),
       "Create lightsUpload");

    ComPtr<ID3D12Resource> lightsReadback;
    Hr(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&lightsReadback)),
       "Create lightsReadback");

    // 5. Populate Initial Data:
    ManyLightRecord initialLights[numLights]{};
    initialLights[0].relPos[0] = 10.0f; initialLights[0].rgb[0] = 1.0f; initialLights[0].rgb[1] = 0.5f; initialLights[0].rgb[2] = 0.2f;
    initialLights[1].relPos[0] = 50.0f; initialLights[1].rgb[0] = 0.8f; initialLights[1].rgb[1] = 0.8f; initialLights[1].rgb[2] = 0.8f;
    initialLights[2].relPos[0] = 100.0f; initialLights[2].rgb[0] = 0.2f; initialLights[2].rgb[1] = 0.4f; initialLights[2].rgb[2] = 0.6f;
    initialLights[3].relPos[0] = 200.0f; initialLights[3].rgb[0] = 1.0f; initialLights[3].rgb[1] = 1.0f; initialLights[3].rgb[2] = 1.0f;

    void* pMapped = nullptr;
    Hr(lightsUpload->Map(0, nullptr, &pMapped), "Map lightsUpload");
    std::memcpy(pMapped, initialLights, sizeof(initialLights));
    lightsUpload->Unmap(0, nullptr);

    list->CopyBufferRegion(lightsBuffer.Get(), 0, lightsUpload.Get(), 0, lightsByteSize);

    D3D12_RESOURCE_BARRIER initBarrier{};
    initBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    initBarrier.Transition.pResource = lightsBuffer.Get();
    initBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    initBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1, &initBarrier);

    // 6. Apply Overrides:
    // Override Light 0 -> RGB = (0.0, 0.0, 0.0) (Turn OFF)
    // Override Light 2 -> RGB = (2.5, 0.0, 1.5) (Vibrant Magenta)
    // Light 1 and 3 are NOT overridden -> untouched
    cdt::modulator::LightOverride overrides[2]{};
    overrides[0].lightIndex = 0;
    overrides[0].rgb[0] = 0.0f; overrides[0].rgb[1] = 0.0f; overrides[0].rgb[2] = 0.0f;
    overrides[0].colorW = 0.0f;
    overrides[0].enabled = 1;

    overrides[1].lightIndex = 2;
    overrides[1].rgb[0] = 2.5f; overrides[1].rgb[1] = 0.0f; overrides[1].rgb[2] = 1.5f;
    overrides[1].colorW = 1.0f;
    overrides[1].enabled = 1;

    Check(modulator.ApplyOverrides(list.Get(), lightsBuffer.Get(), overrides, 2), "ApplyOverrides");
    // Four unsent batches own all upload slices. A fifth must skip this frame
    // rather than overwrite bytes which the GPU has not consumed yet.
    for (uint32_t i = 1; i < cdt::render::LightModulator::kRingSlices; ++i)
        Check(modulator.ApplyOverrides(list.Get(), lightsBuffer.Get(), overrides, 2), "pending slice allocation");
    Check(!modulator.ApplyOverrides(list.Get(), lightsBuffer.Get(), overrides, 2), "pending slice exhaustion");

    // Copy modified lights to readback
    D3D12_RESOURCE_BARRIER readbackBarrier{};
    readbackBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    readbackBarrier.Transition.pResource = lightsBuffer.Get();
    readbackBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    readbackBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &readbackBarrier);

    list->CopyBufferRegion(lightsReadback.Get(), 0, lightsBuffer.Get(), 0, lightsByteSize);
    Hr(list->Close(), "list->Close");

    // Execute & wait
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);
    Check(modulator.OnSubmitted(queue.Get(), 1, lists), "signal exact modulation submission");

    ComPtr<ID3D12Fence> fence;
    Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
    Hr(queue->Signal(fence.Get(), 1), "queue->Signal");

    HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Hr(fence->SetEventOnCompletion(1, hEvent), "SetEventOnCompletion");
    Check(WaitForSingleObject(hEvent, 5000) == WAIT_OBJECT_0, "first GPU completion");
    CloseHandle(hEvent);

    // 7. Verify Results!
    ManyLightRecord resultLights[numLights]{};
    Hr(lightsReadback->Map(0, nullptr, &pMapped), "Map lightsReadback");
    std::memcpy(resultLights, pMapped, sizeof(resultLights));
    lightsReadback->Unmap(0, nullptr);

    std::cout << "Light 0 (Overridden OFF): RGB = ("
              << resultLights[0].rgb[0] << ", " << resultLights[0].rgb[1] << ", " << resultLights[0].rgb[2] << ")\n";
    std::cout << "Light 1 (Untouched): RGB = ("
              << resultLights[1].rgb[0] << ", " << resultLights[1].rgb[1] << ", " << resultLights[1].rgb[2] << ")\n";
    std::cout << "Light 2 (Overridden Magenta): RGB = ("
              << resultLights[2].rgb[0] << ", " << resultLights[2].rgb[1] << ", " << resultLights[2].rgb[2] << ")\n";
    std::cout << "Light 3 (Untouched): RGB = ("
              << resultLights[3].rgb[0] << ", " << resultLights[3].rgb[1] << ", " << resultLights[3].rgb[2] << ")\n";

    // Verification:
    // Light 0 must be 0.0
    Check(std::abs(resultLights[0].rgb[0]) < 1e-5f, "Light 0 R must be 0");
    Check(std::abs(resultLights[0].rgb[1]) < 1e-5f, "Light 0 G must be 0");
    Check(std::abs(resultLights[0].rgb[2]) < 1e-5f, "Light 0 B must be 0");

    // Light 1 must be untouched original 0.8
    Check(std::abs(resultLights[1].rgb[0] - 0.8f) < 1e-5f, "Light 1 R untouched");
    Check(std::abs(resultLights[1].rgb[1] - 0.8f) < 1e-5f, "Light 1 G untouched");
    Check(std::abs(resultLights[1].rgb[2] - 0.8f) < 1e-5f, "Light 1 B untouched");

    // Light 2: 2.5, 0.0, 1.5
    Check(std::abs(resultLights[2].rgb[0] - 2.5f) < 1e-5f, "Light 2 R magenta");
    Check(std::abs(resultLights[2].rgb[1] - 0.0f) < 1e-5f, "Light 2 G magenta");
    Check(std::abs(resultLights[2].rgb[2] - 1.5f) < 1e-5f, "Light 2 B magenta");

    // Light 3: Must be untouched original (1.0, 1.0, 1.0)
    Check(std::abs(resultLights[3].rgb[0] - 1.0f) < 1e-5f, "Light 3 R untouched");
    Check(std::abs(resultLights[3].rgb[1] - 1.0f) < 1e-5f, "Light 3 G untouched");
    Check(std::abs(resultLights[3].rgb[2] - 1.0f) < 1e-5f, "Light 3 B untouched");

    // Fence completion allows reuse. Shutdown during a recorded but unsubmitted
    // batch must retain its upload allocation until the exact list is submitted.
    ComPtr<ID3D12CommandAllocator> nextAllocator;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&nextAllocator)), "next allocator");
    ComPtr<ID3D12GraphicsCommandList> nextList;
    Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, nextAllocator.Get(), nullptr,
                                 IID_PPV_ARGS(&nextList)), "next list");
    D3D12_RESOURCE_BARRIER resumeBarrier{};
    resumeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resumeBarrier.Transition.pResource = lightsBuffer.Get();
    resumeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resumeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    resumeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    nextList->ResourceBarrier(1, &resumeBarrier);
    Check(modulator.ApplyOverrides(nextList.Get(), lightsBuffer.Get(), overrides, 2), "reclaim completed slice");
    modulator.Shutdown();
    Check(!modulator.IsInitialized(), "shutdown rejects new modulation");
    Check(!modulator.Initialize(device.Get()), "pending upload allocation retained");
    Hr(nextList->Close(), "next list close");
    ID3D12CommandList* nextLists[] = { nextList.Get() };
    queue->ExecuteCommandLists(1, nextLists);
    Check(modulator.OnSubmitted(queue.Get(), 1, nextLists), "signal submission after shutdown");
    Hr(queue->Signal(fence.Get(), 2), "queue signal after shutdown");
    hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(hEvent != nullptr, "completion event after shutdown");
    Hr(fence->SetEventOnCompletion(2, hEvent), "completion after shutdown");
    Check(WaitForSingleObject(hEvent, 5000) == WAIT_OBJECT_0, "GPU completion after shutdown");
    CloseHandle(hEvent);
    modulator.Shutdown();
    Check(modulator.Initialize(device.Get()), "reinitialize after completed upload");
    modulator.Shutdown();

    std::cout << "SUCCESS: All LightModulator DMA assertions PASSED!" << std::endl;
    return 0;
}
