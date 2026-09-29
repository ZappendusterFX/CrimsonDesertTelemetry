#include "render_capture.h"
#include "manylights_pair.h"
#include "submission_observer.h"
#include "ambient_probe.h"
#include "render_bridge.h"
#include "sky_bridge.h"
#include "device_identity.h"
#include "presentation_queue.h"
#include "light_modulator.h"
#include "modulator_bridge.h"
#include "native_contract.generated.h"
#include "console/common.h"
#include "console/mem.h"
#include <d3d12.h>
#include <MinHook.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" void CdtFilterThunk();
extern "C" { void* CdtFilterTrampoline = nullptr; }
extern "C" void CdtAmbientThunkA();
extern "C" void CdtAmbientThunkB();
extern "C" { void* CdtAmbientTrampolineA = nullptr; void* CdtAmbientTrampolineB = nullptr; }

namespace cdt::render
{
namespace
{
namespace contract = native_contract;
enum class Phase { Discover, Found, Preparing, Ready, Recorded, Submitting, WaitingGpu, Failed, Stopped };
SRWLOCK lock = SRWLOCK_INIT;
Phase phase = Phase::Stopped;
uint64_t gameBase{}, hookAddress{}, lastAttempt{}, capturedAt{}, issuedAt{}, submittedAt{}, fenceValue{};
uint32_t intervalMs = 50, capturedFrame{}, lastFrame{}, error{};
uint64_t capturedOutputResource{}, capturedCounterResource{}, capturedOwner{};
uint32_t capturedBufferIndex = UINT32_MAX;
bool hasFrame{}, executeEnabled{};
std::atomic<bool> hookEnabled{};
ID3D12Resource* discoverySource{};
ID3D12Resource* pendingSource{};
ID3D12Resource* pendingCounter{};
ID3D12Resource* readback{};
ID3D12Fence* fence{};
IUnknown* preparedDeviceIdentity{};
// One probe queue per process. Releasing the last ReShade queue-wrapper reference
// at world entry has blocked inside the driver on a real hung startup. The ASI is
// process-lifetime, so keep this bounded queue until process exit.
ID3D12CommandQueue* retainedProbeQueue{};
bool executeFromPresentation{};
ID3D12GraphicsCommandList* pendingList{};
D3D12_COMMAND_LIST_TYPE queueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
std::array<uint8_t, SceneBytes> scene{};
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
ExecuteFn executeOriginal{};
void* executeTarget{};
LightModulator modulator;
bool ambientMode{};
std::atomic<bool> skyStreaming{};
bool pendingAmbient{};
std::atomic<bool> skyHookEnabled{};
uint64_t lastSkyAttempt{};
HANDLE ambientFile = INVALID_HANDLE_VALUE;
HANDLE ambientRequestEvent{};
HANDLE captureReadyEvent{};
bool captureReady{};
std::filesystem::path ambientDirectory;
uint32_t ambientRun{};
bool ambientAcceptRequests{};
AmbientRecordHeader ambientHeader{};
ExposureCacheContext ambientExposure{};
std::array<std::atomic<uint64_t>, 2> ambientHits{};
uint64_t ambientReportAt{};
uint32_t ambientSamples{};
constexpr uint32_t AmbientSampleLimit = 120;
uint32_t ambientLimit = AmbientSampleLimit;
constexpr uint64_t SubmissionTimeoutMs = 60000;
constexpr uint64_t GpuTimeoutMs = 5000;

HANDLE pairEvent{};
std::filesystem::path pairDirectory;
bool pairRequested{};
uint64_t pairRequestedAt{};
uint32_t pairRuns{};
ID3D12Resource* pendingInput{};
bool pendingPairSave{};
ManyLightsPairHeader pairHeader{};
constexpr uint32_t PairRunLimit = 8;
// Continuous input requires enhanced buffer barriers on the prepared device.
bool upstreamEnabled{}, upstreamRefused{}, enhancedBarriers{};
uint64_t capturedInputResource{};
InputState capturedInputState = InputState::Disabled;
uint64_t inputUnavailableSamples{};
size_t CopyBytes()
{
    return ambientMode ? AmbientBytes : pairEvent || upstreamEnabled ? PairCopyBytes : LightBytes + CounterBytes;
}
bool InitializePairControl(const wchar_t* directory)
{
    if (pairEvent || readback || ambientMode || phase != Phase::Stopped) return false;
    pairDirectory = directory;
    const auto name = L"Local\\CrimsonDesertTelemetry.ManyLightsPair." + std::to_wstring(GetCurrentProcessId());
    pairEvent = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    if (!pairEvent) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    { CloseHandle(pairEvent); pairEvent = nullptr; return false; }
    ch::Log("ManyLights INPUT/OUTPUT diagnostic enabled, IDLE until explicit request, max8 files; public feed unchanged.");
    return true;
}
void PollPairRequest()
{
    if (!pairEvent) return;
    if (pairRequested && GetTickCount64() - pairRequestedAt > 5000)
    {
        pairRequested = false;
        ch::Log("ManyLights pair request expired: no eligible source/frame; no file saved.");
    }
    if (WaitForSingleObject(pairEvent, 0) != WAIT_OBJECT_0) return;
    const bool streamRunning = phase == Phase::Ready || phase == Phase::Recorded ||
        phase == Phase::Submitting || phase == Phase::WaitingGpu;
    // A normal light/sky copy may already be in flight. Reserve the next free
    // transaction, but never queue another diagnostic behind a paired one.
    // With continuous input every transaction carries input; only a pending
    // SAVE makes the diagnostic busy.
    if (!captureReady || !streamRunning || pendingPairSave || pairRequested || pairRuns >= PairRunLimit || error)
    { ch::Log("ManyLights pair request refused: not ready, busy, faulted or limit reached; not queued."); return; }
    pairRequested = true; pairRequestedAt = GetTickCount64();
    ch::Log("ManyLights pair requested: next eligible frame, 5-second expiry.");
}
void SavePair(const void* mapped)
{
    pairHeader.completedTick = GetTickCount64(); pairHeader.fenceValue = fenceValue;
    pairHeader.flags = 15; // exact input context, paired scene, same-list copy, completed queue fence
    const auto path = pairDirectory / (L"manylights-pair-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(capturedAt) + L"-" + std::to_wstring(pairRuns) + L".bin");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = file != INVALID_HANDLE_VALUE;
    const auto write = [&](const void* bytes, DWORD count) {
        DWORD written{};
        return WriteFile(file, bytes, count, &written, nullptr) && written == count;
    };
    if (ok)
    {
        ok = write(&pairHeader, sizeof(pairHeader)) && write(scene.data(), SceneBytes) &&
            write(mapped, static_cast<DWORD>(PairCopyBytes));
        CloseHandle(file);
    }
    ch::Log("ManyLights pair %s: frame=%u input=0x%llX output=0x%llX fence=%llu file=%s; input validity NOT inferred.",
        ok ? "saved" : "WRITE FAILED (reject partial file)", capturedFrame, pairHeader.input,
        pairHeader.output, fenceValue, path.string().c_str());
}
void CloseAmbientFile()
{
    if (ambientFile != INVALID_HANDLE_VALUE) { CloseHandle(ambientFile); ambientFile = INVALID_HANDLE_VALUE; }
}
void CloseAmbientControl()
{
    ambientAcceptRequests = false;
    if (ambientRequestEvent) { CloseHandle(ambientRequestEvent); ambientRequestEvent = nullptr; }
}
void CloseCaptureReadyGate()
{
    if (captureReadyEvent) { CloseHandle(captureReadyEvent); captureReadyEvent = nullptr; }
    captureReady = false;
}
bool InitializeCaptureReadyGate()
{
    const auto name = L"Local\\CrimsonDesertTelemetry.CaptureReady." + std::to_wstring(GetCurrentProcessId());
    captureReadyEvent = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    captureReady = false;
    return captureReadyEvent != nullptr;
}
bool InitializeAmbientControl(const wchar_t* directory)
{
    ambientDirectory = directory;
    const auto name = L"Local\\CrimsonDesertTelemetry.AmbientProbe." + std::to_wstring(GetCurrentProcessId());
    ambientRequestEvent = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    if (!ambientRequestEvent) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    { CloseHandle(ambientRequestEvent); ambientRequestEvent = nullptr; return false; }
    return true;
}
// Worker-only, under the capture lock. A request is a one-bit start signal,
// never an arbitrary command/path. Busy requests are discarded, not queued.
void PollAmbientRequest()
{
    if (!ambientRequestEvent || WaitForSingleObject(ambientRequestEvent, 0) != WAIT_OBJECT_0) return;
    if (!ambientAcceptRequests || phase != Phase::Stopped || error || pendingSource || pendingList)
    {
        ch::Log("Ambient start request ignored: busy, stopped externally, or faulted; no queued restart.");
        return;
    }
    const auto path = ambientDirectory / (L"ambient-probe-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++ambientRun) + L".bin");
    ambientFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ambientFile == INVALID_HANDLE_VALUE)
    {
        error = GetLastError(); phase = Phase::Failed;
        return;
    }
    ambientSamples = 0; hasFrame = false; capturedAt = 0; lastAttempt = 0;
    // Completed runs reuse the same device/readback/fence and monotonically
    // increasing fence values. Never rearm after a failed or in-flight copy.
    phase = readback && fence && executeEnabled ? Phase::Ready : Phase::Discover;
    ch::Log("Ambient probe started by explicit request: max%u samples, output=%s", ambientLimit, path.string().c_str());
}

template<class T> bool Read(uint64_t address, T& result)
{
    return address && ch::mem::SafeRead(reinterpret_cast<void*>(address), &result, sizeof(result));
}
bool ReadScene(std::array<uint8_t, SceneBytes>& result)
{
    uint64_t root{}, data{};
    if (!Read(gameBase + contract::SceneGlobalRva, root) || !Read(root + contract::SceneRootPointerOffset, data)) return false;
    // +428 is a pointer to the SceneConstantBuffer, not an inline structure.
    return ch::mem::SafeRead(reinterpret_cast<void*>(data), result.data(), result.size()) && ValidateScene(result.data());
}
bool ReadExposure(uint64_t sky, ExposureCacheSample& result)
{
    return ReadExposureCache(sky, result, [](uint64_t address, void* data, size_t bytes) {
        return ch::mem::SafeRead(reinterpret_cast<void*>(address), data, bytes);
    });
}
bool Resolve(uint64_t outer, uint64_t command, ID3D12Resource*& source, ID3D12GraphicsCommandList*& list)
{
    uint64_t inner{}, holder{}, resource{}, nativeList{};
    uint32_t stride{}, count{};
    if (!Read(outer + contract::InnerOffset, inner) || !Read(inner + contract::StrideOffset, stride) ||
        !Read(inner + contract::CountOffset, count) || stride != RecordStride || count != RecordCount ||
        !Read(inner + contract::ResourceOffset, resource) || !Read(command + contract::CommandHolderOffset, holder) ||
        !Read(holder + contract::NativeListOffset, nativeList) || !resource || !nativeList) return false;
    source = reinterpret_cast<ID3D12Resource*>(resource);
    list = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeList);
    const auto desc = source->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width == LightBytes &&
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
        (list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);
}
bool ResolveCounter(uint64_t outer, ID3D12Resource*& counter)
{
    uint64_t inner{}, resource{};
    // Do not assume the CPU wrapper's stride/count encoding for this counter.
    // The exact binding identifies it; the native descriptor bounds the copy.
    if (!Read(outer + contract::InnerOffset, inner) || !Read(inner + contract::ResourceOffset, resource) || !resource) return false;
    counter = reinterpret_cast<ID3D12Resource*>(resource);
    const auto desc = counter->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width >= CounterBytes &&
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
}
bool ResolveAmbient(uint64_t outer, uint64_t command, ID3D12Resource*& source, ID3D12GraphicsCommandList*& list)
{
    uint64_t inner{}, resource{}, holder{}, nativeList{};
    uint32_t stride{}, count{};
    if (!Read(outer + contract::InnerOffset, inner) || !inner ||
        !Read(inner + contract::StrideOffset, stride) || stride != 16 ||
        !Read(inner + contract::CountOffset, count) || count != 64 ||
        !Read(inner + contract::ResourceOffset, resource) || !resource ||
        !Read(command + contract::CommandHolderOffset, holder) || !holder ||
        !Read(holder + contract::NativeListOffset, nativeList) || !nativeList) return false;
    source = reinterpret_cast<ID3D12Resource*>(resource);
    list = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeList);
    const auto desc = source->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width >= AmbientBytes &&
        desc.Width <= 65536 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
        (list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);
}
void Fail(uint32_t code)
{
    error = code;
    phase = Phase::Failed;
    // References stay alive after a partial command recording or failed submit;
    // releasing them without a completion fence would itself be unsafe.
}
// The input wrapper is optional per sample. A fault while resolving it must
// cost only this sample's input, never the proven filtered output stream.
bool ResolveInput(uint64_t owner, uint64_t command, ID3D12Resource* source, ID3D12Resource* counter,
    ID3D12GraphicsCommandList* list, ID3D12Resource*& input, ID3D12GraphicsCommandList7*& inputList)
{
    input = nullptr; inputList = nullptr;
    __try
    {
        uint64_t inputOuter{};
        ID3D12Resource* candidate{};
        ID3D12GraphicsCommandList* resolvedList{};
        IUnknown* inputDevice{};
        bool valid = enhancedBarriers && Read(owner + PairInputOwnerOffset, inputOuter) &&
            Resolve(inputOuter, command, candidate, resolvedList) && candidate != source && candidate != counter &&
            resolvedList == list && SUCCEEDED(device_identity::ChildDeviceIdentity(candidate, &inputDevice));
        if (inputDevice) { valid = valid && inputDevice == preparedDeviceIdentity; inputDevice->Release(); }
        ID3D12GraphicsCommandList7* candidateList{};
        if (!valid || FAILED(list->QueryInterface(IID_PPV_ARGS(&candidateList))) || !candidateList) return false;
        input = candidate; inputList = candidateList;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { input = nullptr; inputList = nullptr; return false; }
}

void Record(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner, bool ambientCopy = false)
{
    // Production capture is installed at process startup, but the managed host
    // only opens this one-shot gate after player and render-camera data prove
    // that a playable world exists. Explicit research probes remain manual.
    if (!ambientMode && !captureReady) return;
    if (phase != Phase::Discover && phase != Phase::Ready) return;
    const uint64_t now = GetTickCount64();
    auto& attempt = ambientCopy && skyStreaming ? lastSkyAttempt : lastAttempt;
    const auto interval = ambientCopy && skyStreaming ? 500u : intervalMs;
    if (now - attempt < interval) return;
    attempt = now;
    ID3D12Resource* source{};
    ID3D12Resource* counter{};
    ID3D12GraphicsCommandList* list{};
    if (ambientCopy)
    {
        if (!ResolveAmbient(outer, command, source, list)) return;
    }
    else if (!Resolve(outer, command, source, list) || !ResolveCounter(counterOuter, counter) || source == counter) return;
    if (phase == Phase::Discover)
    {
        source->AddRef();
        discoverySource = source;
        queueType = list->GetType();
        phase = Phase::Found;
        return;
    }
    IUnknown* sourceDevice{};
    IUnknown* counterDevice{};
    IUnknown* listDevice{};
    const bool sameDevice = SUCCEEDED(device_identity::ChildDeviceIdentity(source, &sourceDevice)) &&
        (ambientCopy || SUCCEEDED(device_identity::ChildDeviceIdentity(counter, &counterDevice))) &&
        SUCCEEDED(device_identity::ChildDeviceIdentity(list, &listDevice)) &&
        sourceDevice == preparedDeviceIdentity && (ambientCopy || counterDevice == preparedDeviceIdentity) &&
        listDevice == preparedDeviceIdentity && (skyStreaming || list->GetType() == queueType);
    if (sourceDevice) sourceDevice->Release();
    if (counterDevice) counterDevice->Release();
    if (listDevice) listDevice->Release();
    if (!sameDevice) { Fail(ERROR_INVALID_HANDLE); return; }
    // One bounded in-flight transaction for BOTH feeds. Its exact submitting
    // queue is checked below; no second ExecuteCommandLists detour is installed.
    queueType = list->GetType();
    std::array<uint8_t, SceneBytes> confirmation{};
    if (!ReadScene(scene) || !ReadScene(confirmation) || !SameScene(scene.data(), confirmation.data())) return;
    memcpy(&capturedFrame, scene.data() + contract::FrameOffset, sizeof(capturedFrame));
    if (hasFrame && capturedFrame == lastFrame) return;
    ID3D12Resource* input{};
    ID3D12GraphicsCommandList7* inputList{};
    const bool saveRequested = !ambientCopy && pairRequested;
    if (!ambientCopy && (upstreamEnabled || pairRequested))
    {
        if (!ResolveInput(owner, command, source, counter, list, input, inputList))
        {
            if (saveRequested) ch::Log("ManyLights pair refused: input resource/list validation failed; normal stream continues.");
            if (upstreamEnabled && inputUnavailableSamples++ % 600 == 0)
                ch::Log("ManyLights INPUT unavailable for this sample (%llu so far): wrapper/list validation failed; filtered output continues.",
                    inputUnavailableSamples);
        }
        pairRequested = false;
    }
    if (ambientMode)
    {
        ExposureCacheSample cache{};
        const auto begin = GetTickCount64();
        const bool valid = ReadExposure(owner, cache);
        BeginExposureCache(ambientExposure, cache, valid, begin);
    }
    source->AddRef();
    if (counter) counter->AddRef();
    list->AddRef();
    pendingSource = source;
    pendingCounter = counter;
    pendingList = list;
    pendingAmbient = ambientCopy;
    pendingInput = input;
    if (input) input->AddRef();
    pendingPairSave = saveRequested && input;
    capturedInputResource = reinterpret_cast<uint64_t>(input);
    capturedInputState = upstreamEnabled ? (input ? InputState::Paired : InputState::Unavailable)
        : upstreamRefused ? InputState::Refused : InputState::Disabled;
    capturedAt = now;
    // These identities belong to THIS recorded pair. Never resolve them again
    // when the worker publishes: the renderer may already have switched banks.
    capturedOutputResource = reinterpret_cast<uint64_t>(source);
    capturedCounterResource = reinterpret_cast<uint64_t>(counter);
    capturedOwner = owner;
    capturedBufferIndex = UINT32_MAX;
    uint32_t currentIndex{};
    if (!ambientCopy && owner && Read(owner + contract::OwnerBankIndexOffset, currentIndex)) capturedBufferIndex = currentIndex;
    if (pendingPairSave)
    {
        ++pairRuns;
        pairHeader = {};
        pairHeader.pid = GetCurrentProcessId(); pairHeader.frame = capturedFrame; pairHeader.bank = capturedBufferIndex;
        pairHeader.capturedTick = now; pairHeader.owner = owner;
        pairHeader.input = reinterpret_cast<uint64_t>(input);
        pairHeader.output = capturedOutputResource; pairHeader.counter = capturedCounterResource;
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
            memcpy(&pairHeader.processStart, &creation, sizeof(creation));
    }
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    for (auto& barrier : barriers)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    barriers[0].Transition.pResource = source;
    barriers[1].Transition.pResource = counter;
    const UINT barrierCount = ambientCopy ? 1u : static_cast<UINT>(barriers.size());
    list->ResourceBarrier(barrierCount, barriers.data());
    list->CopyBufferRegion(readback, 0, source, 0, ambientCopy ? AmbientBytes : LightBytes);
    // The counter is a GPU-written buffer, not a CPU count. Copy its bounded
    // prefix before subsequent engine passes reuse it, on the SAME list/fence.
    if (!ambientCopy) list->CopyBufferRegion(readback, LightBytes, counter, 0, CounterBytes);
    for (auto& barrier : barriers)
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    list->ResourceBarrier(barrierCount, barriers.data());
    if (input)
    {
        // The proven input SRV remains SHADER_RESOURCE at this post-dispatch
        // boundary. Use a buffer access/sync round-trip, not an assumed UAV state.
        auto transition = PairInputBarrier(input, false);
        D3D12_BARRIER_GROUP group{}; group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = 1; group.pBufferBarriers = &transition;
        inputList->Barrier(1, &group);
        list->CopyBufferRegion(readback, PairInputOffset, input, 0, LightBytes);
        transition = PairInputBarrier(input, true);
        inputList->Barrier(1, &group);
        inputList->Release();
    }
    // A camera update concurrent with recording invalidates this pair. Still
    // submit and fence the copy, but never publish the mismatched sample.
    if (!ReadScene(confirmation) || !SameScene(scene.data(), confirmation.data())) capturedAt = 0;
    if (ambientMode)
    {
        ExposureCacheSample cache{};
        const bool valid = ReadExposure(owner, cache);
        EndExposureCache(ambientExposure, cache, valid, GetTickCount64());
    }
    issuedAt = now;
    phase = Phase::Recorded;
}

void STDMETHODCALLTYPE ExecuteHook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    bool target = false;
    AcquireSRWLockExclusive(&lock);
    if (phase == Phase::Recorded)
    {
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == pendingList) { target = true; phase = Phase::Submitting; break; }
    }
    ReleaseSRWLockExclusive(&lock);
    const auto observer = submissionObserver.load(std::memory_order_relaxed);
    if (observer) observer(queue, count, lists, false);
    executeOriginal(queue, count, lists);
    if (observer) observer(queue, count, lists, true);
    // Modulation runs on every filter call, including frames without a periodic
    // telemetry copy. Fence each submitted upload slice on this exact queue.
    AcquireSRWLockExclusive(&lock);
    const bool modulatorSubmitted = modulator.OnSubmitted(queue, count, lists);
    ReleaseSRWLockExclusive(&lock);
    if (!modulatorSubmitted)
        ch::Log("ManyLights GPU Modulator disabled: queue identity or upload fence signal failed; upload retained.");
    if (!target) return;
    // Queue::Signal is ordered AFTER the exact submission containing our copy.
    // A delay, ID3D12Fence::Signal (CPU-side), or a different queue is not proof.
    IUnknown* queueDevice{};
    const bool compatibleQueue = SUCCEEDED(device_identity::ChildDeviceIdentity(queue, &queueDevice)) &&
        queueDevice == preparedDeviceIdentity && queue->GetDesc().Type == queueType;
    if (queueDevice) queueDevice->Release();
    const HRESULT hr = compatibleQueue ? queue->Signal(fence, ++fenceValue) : E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    if (FAILED(hr)) Fail(static_cast<uint32_t>(hr));
    else { submittedAt = GetTickCount64(); phase = Phase::WaitingGpu; }
    ReleaseSRWLockExclusive(&lock);
}

bool Prepare()
{
    ID3D12Device* device{};
    ch::Log("ManyLights capture preparation: acquiring source device and readback resources.");
    HRESULT hr = discoverySource->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(hr)) return false;
    hr = device_identity::CanonicalDeviceIdentity(device, &preparedDeviceIdentity);
    if (FAILED(hr)) { device->Release(); return false; }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    auto desc = discoverySource->GetDesc();
    desc.Width = CopyBytes();
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    if (SUCCEEDED(hr)) hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    // The input's SRV access round-trip uses enhanced buffer barriers. Query the
    // prepared device once; without support only the input is refused.
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
    enhancedBarriers = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) &&
        options.EnhancedBarriersSupported;
    if (upstreamEnabled && !enhancedBarriers)
    {
        upstreamEnabled = false; upstreamRefused = true;
        PublishInputState(InputState::Refused);
        ch::Log("ManyLights INPUT refused: device lacks enhanced barriers; filtered output continues.");
    }
    if (FAILED(hr)) { device->Release(); error = static_cast<uint32_t>(hr); return false; }
    ch::Log("ManyLights capture preparation: readback and fence ready; acquiring queue hook target.");
    // Prefer the game's own presentation queue on THIS source's device: no extra
    // queue at world entry. Whether its entry also sees the compute lists is
    // proven only by an observed submission; otherwise the submission timeout
    // stops capture. Without a matching queue (HUD off), keep the probe queue.
    const auto presentation = presentation_queue::Current();
    executeFromPresentation = presentation.execute && presentation.device == preparedDeviceIdentity;
    if (executeFromPresentation)
    {
        executeTarget = presentation.execute;
        ch::Log("ManyLights capture preparation: submission hook target from the game's presentation queue; no probe queue created.");
    }
    else
    {
        // Obtain the real ExecuteCommandLists implementation from THIS source's
        // device, avoiding a wrong WARP/adapter/system-D3D12 function address.
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = queueType;
        hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&retainedProbeQueue));
        if (FAILED(hr)) { device->Release(); error = static_cast<uint32_t>(hr); return false; }
        executeTarget = (*reinterpret_cast<void***>(retainedProbeQueue))[10];
        ch::Log("ManyLights capture preparation: queue created; installing submission hook.");
    }
    const auto create = MH_CreateHook(executeTarget, ExecuteHook, reinterpret_cast<void**>(&executeOriginal));
    const auto enable = create == MH_OK ? MH_EnableHook(executeTarget) : create;
    // Do not release the probe queue here. Its last Release can enter a graphics
    // wrapper/driver wait while the game is entering the world. One retained queue
    // is bounded by this ASI's process lifetime and cannot outlive the process.
    if (enable != MH_OK) { device->Release(); error = ERROR_INVALID_FUNCTION; return false; }
    executeEnabled = true;
    ch::Log("ManyLights capture preparation: submission hook ready; initializing modulator.");
    if (!ambientMode)
    {
        if (modulator.Initialize(device) && modulator::OpenModulatorBridge(GetCurrentProcessId()))
            ch::Log("ManyLights GPU Modulator initialized: shared memory bridge open.");
        else
        {
            modulator.Shutdown();
            ch::Log("ManyLights GPU Modulator initialization failed (upload buffer or shared bridge).");
        }
    }
    device->Release();
    if (ambientMode)
    {
        D3D12_HEAP_PROPERTIES props{}; D3D12_HEAP_FLAGS flags{};
        const HRESULT heapHr = discoverySource->GetHeapProperties(&props, &flags);
        ch::Log("Ambient probe prepared: source=0x%llX width=%llu heap=%u heapHr=0x%08X queue=%u; 1024-byte copy, submission fence, max120 samples; NOT API data.",
            reinterpret_cast<uint64_t>(discoverySource), discoverySource->GetDesc().Width,
            static_cast<unsigned>(props.Type), static_cast<unsigned>(heapHr), static_cast<unsigned>(queueType));
    }
    else ch::Log("ManyLights recurring capture ready: exact filter callsite, %u Hz, paired counter%s, submission fence; instrumented run.",
        intervalMs ? 1000 / intervalMs : 0u, upstreamEnabled ? " + full INPUT" : "");
    return true;
}
// Exact-build input anchors: owner+0x628 load and its SRV binder call. The outer
// exact-EXE gate is still required in instruments::Run. Do not silently carry
// this field/anchor to another profile/build; relocate and re-verify after updates.
bool InputAnchorsVerified(uint64_t moduleBase)
{
    if (contract::BuildId != "25477059" || contract::HookRva != 0x3DA97DA || !CheckCapturePreflight(moduleBase)) return false;
    constexpr std::array<uint8_t, 7> expected{0x49,0x8B,0xB5,0x28,0x06,0,0};
    std::array<uint8_t, 7> bytes{};
    if (!ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase + 0x3DA9406), bytes.data(), bytes.size()) || bytes != expected)
        return false;
    constexpr std::array<uint8_t, 12> bind{0x4C,0x8B,0xC6,0x48,0x8B,0xD0,0x48,0x8B,0xCB,0x41,0xFF,0xD2};
    std::array<uint8_t, 12> binding{};
    return ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase + 0x3DA94BC), binding.data(), binding.size()) &&
        binding == bind;
}
}

bool EnableManyLightsPair(uint64_t moduleBase, const wchar_t* directory)
{
    return InputAnchorsVerified(moduleBase) && InitializePairControl(directory);
}

bool EnableUpstreamInput(uint64_t moduleBase)
{
    if (upstreamEnabled || readback || ambientMode || phase != Phase::Stopped) return false;
    if (!InputAnchorsVerified(moduleBase))
    {
        upstreamRefused = true;
        PublishInputState(InputState::Refused);
        return false;
    }
    upstreamEnabled = true; upstreamRefused = false;
    ch::Log("ManyLights INPUT stream enabled: engine light records before view selection, copied with every filtered sample on the same list/fence.");
    return true;
}

void Modulate(uint64_t outer, uint64_t command)
{
    if (ambientMode || !modulator.IsInitialized()) return;

    std::vector<modulator::LightOverride> activeOverrides;
    bool masterEnable = false;
    if (!modulator::PollModulatorOverrides(activeOverrides, masterEnable) || !masterEnable || activeOverrides.empty())
    {
        return;
    }

    ID3D12Resource* source{};
    ID3D12GraphicsCommandList* list{};
    if (!Resolve(outer, command, source, list))
    {
        return;
    }

    modulator.ApplyOverrides(list, source, activeOverrides.data(), static_cast<uint32_t>(activeOverrides.size()));
}

void CaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner)
{
    // Never block the renderer behind worker-side Map/publication/initialization.
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    __try
    {
        Record(outer, command, counterOuter, owner);
        Modulate(outer, command);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fail(GetExceptionCode()); }
    ReleaseSRWLockExclusive(&lock);
}

void CaptureAmbient(uint64_t sky, uint64_t command, uint64_t path)
{
    if ((!ambientMode && !skyStreaming) || path >= ambientHits.size()) return;
    // Only A has a validated public decode profile. B remains research-only.
    if (skyStreaming && path != 0) return;
    ++ambientHits[path];
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    __try
    {
        uint64_t outer{};
        if ((phase == Phase::Discover || phase == Phase::Ready) && Read(sky + 0x98, outer) && outer)
        {
            const auto previous = phase;
            Record(outer, command, 0, sky, true);
            if (previous == Phase::Ready && phase == Phase::Recorded)
            {
                ambientHeader.pid = GetCurrentProcessId();
                ambientHeader.frame = capturedFrame;
                ambientHeader.producerRva = AmbientHookRvas[path];
                ambientHeader.flags = capturedAt ? 7u : 0u; // exact build, completed fence (at save), stable CPU scene
                ambientHeader.capturedTick = capturedAt;
                ambientHeader.resource = capturedOutputResource;
                ambientHeader.outer = outer;
                ambientHeader.sky = sky;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { Fail(GetExceptionCode()); }
    ReleaseSRWLockExclusive(&lock);
}

bool CheckAmbientPreflight(uint64_t moduleBase)
{
    if (!CheckCapturePreflight(moduleBase)) return false;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!Read(moduleBase, dos) || !Read(moduleBase + static_cast<uint32_t>(dos.e_lfanew), nt)) return false;
    const auto sectionBase = moduleBase + static_cast<uint32_t>(dos.e_lfanew) +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    std::array<uint8_t, 15> bytes{};
    const auto match = [&](uint32_t rva, const auto& expected) {
        bool executable = false;
        for (uint16_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        {
            IMAGE_SECTION_HEADER section{};
            if (!Read(sectionBase + i * sizeof(section), section)) return false;
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= section.VirtualAddress &&
                uint64_t{rva} + expected.size() <= uint64_t{section.VirtualAddress} + section.Misc.VirtualSize)
                executable = true;
        }
        if (!executable) return false;
        return ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase + rva), bytes.data(), expected.size()) &&
            memcmp(bytes.data(), expected.data(), expected.size()) == 0;
    };
    if (!match(AmbientHookRvas[0], AmbientSignatureA) || !match(AmbientHookRvas[1], AmbientSignatureB)) return false;
    // Verify dispatch immediately before both hooks and the source-field loads.
    const std::array<uint8_t, 7> sourceA{0x48,0x8B,0xAF,0x98,0x00,0x00,0x00};
    const std::array<uint8_t, 7> sourceB{0x48,0x8B,0x9D,0x98,0x00,0x00,0x00};
    return match(AmbientHookRvas[0] - 6, AmbientDispatchSignature) && match(AmbientHookRvas[1] - 6, AmbientDispatchSignature) &&
        match(AmbientSourceRvas[0], sourceA) && match(AmbientSourceRvas[1], sourceB);
}

bool StartAmbientProbe(uint64_t moduleBase, const wchar_t* outputDirectory)
{
    if (!CheckAmbientPreflight(moduleBase) || phase != Phase::Stopped || hookEnabled || executeEnabled) return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (!InitializeAmbientControl(outputDirectory)) return false;
    gameBase = moduleBase; ambientMode = true; intervalMs = 500;
    hookAddress = gameBase + AmbientHookRvas[0];
    void* targets[]{reinterpret_cast<void*>(hookAddress), reinterpret_cast<void*>(gameBase + AmbientHookRvas[1])};
    void* thunks[]{reinterpret_cast<void*>(CdtAmbientThunkA), reinterpret_cast<void*>(CdtAmbientThunkB)};
    void** trampolines[]{&CdtAmbientTrampolineA, &CdtAmbientTrampolineB};
    for (unsigned i = 0; i < 2; ++i)
    {
        if (MH_CreateHook(targets[i], thunks[i], trampolines[i]) != MH_OK)
        { CloseAmbientControl(); return false; }
    }
    phase = Phase::Stopped; // Loading/menu frames must not consume the experiment.
    if (MH_EnableHook(targets[0]) != MH_OK || MH_EnableHook(targets[1]) != MH_OK)
    {
        MH_DisableHook(targets[0]); MH_DisableHook(targets[1]);
        phase = Phase::Stopped; CloseAmbientControl(); return false;
    }
    hookEnabled = true;
    ambientAcceptRequests = true;
    PublishStatus(Status::Stopped); // Explicitly no ManyLights stream in this private research mode.
    ch::Log("Ambient probe v2 enabled, IDLE until explicit start request (2Hz, max120 samples/run). Includes GPU-derived CPU exposure cache (source-frame age UNKNOWN), not paired GPU exposure. ManyLights paused. Event=Local\\CrimsonDesertTelemetry.AmbientProbe.%u", GetCurrentProcessId());
    return true;
}

PreflightResult CheckCapturePreflight(uint64_t moduleBase)
{
    if (!moduleBase) return {PreflightFailure::MissingImage};
    // Bound address arithmetic before touching an untrusted/malformed image.
    if (moduleBase > UINT64_MAX - 0x80000000ULL) return {PreflightFailure::MalformedImage};
    IMAGE_DOS_HEADER dos{};
    if (!Read(moduleBase, dos)) return {PreflightFailure::UnreadableImage};
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        dos.e_lfanew > 0x100000) return {PreflightFailure::MalformedImage};
    IMAGE_NT_HEADERS64 nt{};
    if (!Read(moduleBase + static_cast<uint32_t>(dos.e_lfanew), nt)) return {PreflightFailure::UnreadableImage};
    const auto imageBytes = nt.OptionalHeader.SizeOfImage;
    const uint64_t sectionsRva = static_cast<uint32_t>(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
        nt.FileHeader.SizeOfOptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96 || imageBytes > 0x80000000ULL ||
        sectionsRva + nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > imageBytes)
        return {PreflightFailure::MalformedImage};
    const auto inside = [imageBytes](uint64_t rva, size_t length)
    { return rva < imageBytes && length <= imageBytes - rva; };
    if (!inside(contract::SceneGlobalRva, sizeof(uint64_t))) return {PreflightFailure::SceneGlobalOutsideImage};
    std::array<IMAGE_SECTION_HEADER, 96> sections{};
    for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        if (!Read(moduleBase + sectionsRva + i * sizeof(IMAGE_SECTION_HEADER), sections[i]))
            return {PreflightFailure::UnreadableImage};
    const auto executable = [&](uint64_t rva, size_t length)
    {
        if (!inside(rva, length)) return false;
        for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            const uint64_t start = section.VirtualAddress;
            const uint64_t size = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && start <= rva && rva - start <= size &&
                length <= size - (rva - start) && inside(start, static_cast<size_t>(size))) return true;
        }
        return false;
    };
    const auto matches = [&](uint64_t rva, std::span<const uint8_t> signature)
    {
        std::array<uint8_t, 128> bytes{};
        return !signature.empty() && signature.size() <= bytes.size() &&
            ch::mem::SafeRead(reinterpret_cast<const void*>(moduleBase + rva), bytes.data(), signature.size()) &&
            std::equal(signature.begin(), signature.end(), bytes.begin());
    };
    if (!executable(contract::HookRva, contract::HookSignature.size()))
        return {PreflightFailure::HookOutsideExecutableSection};
    if (!matches(contract::HookRva, contract::HookSignature)) return {PreflightFailure::HookSignatureMismatch};
    for (uint32_t i = 0; i < contract::ContextSignatures.size(); ++i)
    {
        const auto& context = contract::ContextSignatures[i];
        if (!executable(context.rva, context.bytes.size())) return {PreflightFailure::ContextOutsideExecutableSection, i};
        if (!matches(context.rva, context.bytes)) return {PreflightFailure::ContextSignatureMismatch, i};
    }
    return {};
}

const char* PreflightFailureName(PreflightFailure failure)
{
    switch (failure)
    {
        case PreflightFailure::None: return "ready";
        case PreflightFailure::MissingImage: return "missing-module";
        case PreflightFailure::UnreadableImage: return "unreadable-image";
        case PreflightFailure::MalformedImage: return "malformed-pe-image";
        case PreflightFailure::SceneGlobalOutsideImage: return "scene-root-outside-image";
        case PreflightFailure::HookOutsideExecutableSection: return "hook-not-in-executable-section";
        case PreflightFailure::HookSignatureMismatch: return "hook-signature-mismatch";
        case PreflightFailure::ContextOutsideExecutableSection: return "context-not-in-executable-section";
        case PreflightFailure::ContextSignatureMismatch: return "caller-context-signature-mismatch";
    }
    return "unknown-preflight-failure";
}

bool StartCapture(uint64_t moduleBase, unsigned sampleRateHz, bool skyEnabled)
{
    const auto preflight = CheckCapturePreflight(moduleBase);
    if (!preflight)
    {
        PublishStatus(Status::Incompatible, ERROR_INVALID_DATA, ExactBuild);
        ch::Log("ManyLights disabled before any hook: %s (context index %u).", PreflightFailureName(preflight.failure), preflight.contextIndex);
        return false;
    }
    // Check BEFORE the filter detour replaces bytes used by this preflight.
    if (skyEnabled && !CheckAmbientPreflight(moduleBase))
    {
        sky::PublishStatus(Status::Incompatible, ERROR_INVALID_DATA);
        ch::Log("Global sky disabled: ambient context preflight failed; local lights remain independent.");
        skyEnabled = false;
    }
    gameBase = moduleBase;
    hookAddress = gameBase + contract::HookRva;
    intervalMs = 1000 / std::clamp(sampleRateHz, 1u, 60u);
    if (!InitializeCaptureReadyGate())
    { PublishStatus(Status::Fault, GetLastError(), ExactBuild); return false; }
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
    { CloseCaptureReadyGate(); PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    if (MH_CreateHook(reinterpret_cast<void*>(hookAddress), CdtFilterThunk, &CdtFilterTrampoline) != MH_OK)
    { CloseCaptureReadyGate(); PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    phase = Phase::Discover;
    if (MH_EnableHook(reinterpret_cast<void*>(hookAddress)) != MH_OK)
    { phase = Phase::Stopped; CloseCaptureReadyGate(); PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    hookEnabled = true;
    if (skyEnabled)
    {
        auto* target = reinterpret_cast<void*>(gameBase + AmbientHookRvas[0]);
        // Publish configuration under the same lock read by both callbacks.
        AcquireSRWLockExclusive(&lock);
        skyStreaming = true;
        ReleaseSRWLockExclusive(&lock);
        if (MH_CreateHook(target, CdtAmbientThunkA, &CdtAmbientTrampolineA) == MH_OK && MH_EnableHook(target) == MH_OK)
        {
            skyHookEnabled = true;
            sky::PublishStatus(Status::Waiting);
            ch::Log("Global sky stream enabled: native A, 2Hz opportunistic, shared bounded copy/fence; no exposure correction.");
        }
        else
        {
            AcquireSRWLockExclusive(&lock); skyStreaming = false; ReleaseSRWLockExclusive(&lock);
            sky::PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION);
        }
    }
    ch::Log("ManyLights exact-build/context detour installed at RVA 0x%llX; waiting for the host's first playable-world signal.", contract::HookRva);
    return true;
}

void PollCapture()
{
    AcquireSRWLockExclusive(&lock);
    PollPairRequest();
    if (ambientMode) PollAmbientRequest();
    if (!ambientMode && !captureReady && captureReadyEvent &&
        WaitForSingleObject(captureReadyEvent, 0) == WAIT_OBJECT_0)
    {
        captureReady = true;
        ch::Log("Playable-world signal received; native light and sky capture armed.");
    }
    if (phase == Phase::Found)
    {
        phase = Phase::Preparing;
        ReleaseSRWLockExclusive(&lock);
        const bool ok = Prepare();
        AcquireSRWLockExclusive(&lock);
        phase = ok ? Phase::Ready : Phase::Failed;
    }
    if (phase == Phase::WaitingGpu)
    {
        const UINT64 completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) Fail(static_cast<uint32_t>(DXGI_ERROR_DEVICE_REMOVED));
        else if (completed >= fenceValue)
        {
            void* mapped{};
            D3D12_RANGE range{0, CopyBytes()};
            const HRESULT hr = readback->Map(0, &range, &mapped);
            if (FAILED(hr) || !mapped) Fail(static_cast<uint32_t>(hr));
            else
            {
                bool saveOk = true;
                if (capturedAt && ambientMode)
                {
                    ambientHeader.sequence = ambientSamples + 1;
                    std::array<uint8_t, sizeof(AmbientRecordHeader) + SceneBytes + AmbientBytes + sizeof(ExposureCacheContext)> record{};
                    memcpy(record.data(), &ambientHeader, sizeof(ambientHeader));
                    memcpy(record.data() + sizeof(ambientHeader), scene.data(), SceneBytes);
                    memcpy(record.data() + sizeof(ambientHeader) + SceneBytes, mapped, AmbientBytes);
                    memcpy(record.data() + sizeof(ambientHeader) + SceneBytes + AmbientBytes, &ambientExposure, sizeof(ambientExposure));
                    DWORD written{};
                    saveOk = WriteFile(ambientFile, record.data(), static_cast<DWORD>(record.size()), &written, nullptr) && written == record.size();
                    if (saveOk)
                    {
                        ++ambientSamples;
                        if (ambientSamples == 1 || ambientSamples % 20 == 0)
                            ch::Log("Ambient sample %u/120: frame=%u producerRva=0x%X source=0x%llX fence=%llu exposureCacheFlags=0x%X (cache GPU age unknown; NOT API).",
                                ambientSamples, capturedFrame, ambientHeader.producerRva, capturedOutputResource, fenceValue, ambientExposure.flags);
                    }
                }
                else if (capturedAt && pendingAmbient)
                    sky::PublishSample(scene.data(), mapped, capturedAt, capturedOutputResource, ambientHeader.producerRva);
                else if (capturedAt) PublishSample(scene.data(), mapped, static_cast<const uint8_t*>(mapped) + LightBytes,
                    capturedAt, capturedOutputResource, capturedCounterResource, capturedOwner, capturedBufferIndex,
                    capturedInputState == InputState::Paired ? static_cast<const uint8_t*>(mapped) + PairInputOffset : nullptr,
                    capturedInputResource, capturedInputState);
                if (pendingPairSave)
                {
                    if (capturedAt) SavePair(mapped);
                    else ch::Log("ManyLights pair discarded: camera changed during recording; no file saved.");
                }
                pendingPairSave = false;
                const D3D12_RANGE noWrites{0,0};
                readback->Unmap(0, &noWrites);
                pendingSource->Release(); pendingSource = nullptr;
                if (pendingCounter) pendingCounter->Release(); pendingCounter = nullptr;
                if (pendingInput) pendingInput->Release(); pendingInput = nullptr;
                pendingList->Release(); pendingList = nullptr;
                lastFrame = capturedFrame; hasFrame = true;
                phase = Phase::Ready;
                if (!saveOk) Fail(ERROR_WRITE_FAULT);
                else if (ambientMode && ambientSamples >= ambientLimit)
                {
                    phase = Phase::Stopped; CloseAmbientFile();
                    ch::Log("Ambient probe complete: %u fence-completed samples saved. IDLE until another explicit start; restart with Research/AmbientProbe=0 to restore ManyLights.", ambientSamples);
                }
            }
        }
    }
    const auto now = GetTickCount64();
    if (phase == Phase::Recorded && now - issuedAt > SubmissionTimeoutMs)
    {
        ch::Log("Capture submission timeout: recorded command list was not observed within %llu ms (hook target: %s).",
            SubmissionTimeoutMs, executeFromPresentation ? "presentation queue" : "probe queue");
        Fail(WAIT_TIMEOUT);
    }
    else if (phase == Phase::WaitingGpu && now - submittedAt > GpuTimeoutMs)
    {
        ch::Log("Capture GPU timeout: fence %llu remained incomplete for %llu ms (completed=%llu).",
            fenceValue, GpuTimeoutMs, fence ? fence->GetCompletedValue() : 0);
        Fail(WAIT_TIMEOUT);
    }
    if (phase == Phase::Failed)
    {
        if (!error) error = ERROR_INVALID_DATA;
        PublishStatus(Status::Fault, error, ExactBuild);
        if (skyStreaming) sky::PublishStatus(Status::Fault, error);
        ch::Log("%s disabled after capture failure 0x%08X; pending resources retained safely until process exit.", ambientMode ? "Ambient probe" : "ManyLights", error);
        CloseAmbientFile();
        ambientAcceptRequests = false;
        phase = Phase::Stopped;
    }
    if (ambientMode && phase != Phase::Stopped && GetTickCount64() - ambientReportAt > 10000)
    {
        ambientReportAt = GetTickCount64();
        ch::Log("Ambient probe progress: pathA=%llu pathB=%llu samples=%u phase=%u.",
            ambientHits[0].load(), ambientHits[1].load(), ambientSamples, static_cast<unsigned>(phase));
    }
    ReleaseSRWLockExclusive(&lock);
}

bool CaptureReady()
{
    AcquireSRWLockShared(&lock);
    const bool ready = captureReady;
    ReleaseSRWLockShared(&lock);
    return ready;
}

uint32_t CaptureFailureCode()
{
    AcquireSRWLockShared(&lock);
    const auto result = (phase == Phase::Failed || phase == Phase::Stopped) ? error : 0;
    ReleaseSRWLockShared(&lock);
    return result;
}

void StopCapture()
{
    if (hookEnabled) MH_DisableHook(reinterpret_cast<void*>(hookAddress));
    if (hookEnabled && ambientMode) MH_DisableHook(reinterpret_cast<void*>(gameBase + AmbientHookRvas[1]));
    if (skyHookEnabled) MH_DisableHook(reinterpret_cast<void*>(gameBase + AmbientHookRvas[0]));
    if (executeEnabled) MH_DisableHook(executeTarget);
    AcquireSRWLockExclusive(&lock);
    phase = Phase::Stopped;
    // An ExecuteHook that reached the original queue call may still be running.
    // Shutdown keeps any unsubmitted or incomplete upload slices until process exit.
    modulator.Shutdown();
    modulator::CloseModulatorBridge();
    CloseAmbientControl();
    CloseCaptureReadyGate();
    CloseAmbientFile();
    if (pairEvent) { CloseHandle(pairEvent); pairEvent = nullptr; }
    pairRequested = false;
    ReleaseSRWLockExclusive(&lock);
    PublishStatus(Status::Stopped);
    if (skyStreaming) sky::PublishStatus(Status::Stopped);
    // Hooks/trampolines and bounded resources live until process exit. The ASI
    // is pinned while instrumentation is enabled, so in-flight thunks cannot
    // jump into an unloaded DLL. Never MH_Uninitialize: overlay shares MinHook.
}
bool OwnsCodeAddress(uint64_t address)
{
    if (skyHookEnabled && address >= gameBase + AmbientHookRvas[0] &&
        address < gameBase + AmbientHookRvas[0] + AmbientSignatureA.size()) return true;
    if (ambientMode) return hookEnabled &&
        ((address >= gameBase + AmbientHookRvas[0] && address < gameBase + AmbientHookRvas[0] + AmbientSignatureA.size()) ||
         (address >= gameBase + AmbientHookRvas[1] && address < gameBase + AmbientHookRvas[1] + AmbientSignatureB.size()));
    return hookEnabled && address >= hookAddress && address < hookAddress + contract::HookSignature.size();
}
#ifdef CDT_RENDER_CAPTURE_TEST
bool InitializePairForTest(const wchar_t* directory) { return InitializePairControl(directory); }
// Host-test-only: the synthetic module has no game code to verify anchors against.
bool InitializeUpstreamForTest()
{
    if (upstreamEnabled || readback || ambientMode || phase != Phase::Stopped) return false;
    upstreamEnabled = true;
    return true;
}
// Host-test-only entry. Never compiled into the ASI; production always requires
// the executable hash plus the exact callsite signature before installing hooks.
void InitializeCaptureForTest(uint64_t moduleBase)
{
    gameBase = moduleBase;
    // Smoke steps are explicit, one-shot calls. Sleep(2) need not advance
    // GetTickCount64, so a wall-clock throttle can silently skip a test step.
    // Production StartCapture still derives its interval from the sample rate.
    intervalMs = 0;
    if (!InitializeCaptureReadyGate()) { phase = Phase::Failed; return; }
    const auto result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED) { phase = Phase::Failed; return; }
    phase = Phase::Discover;
    captureReady = false;
}
bool InitializeAmbientForTest(uint64_t moduleBase, const wchar_t* directory)
{
    InitializeCaptureForTest(moduleBase);
    ambientMode = true;
    captureReady = true;
    ambientLimit = 2;
    phase = Phase::Stopped;
    ambientAcceptRequests = InitializeAmbientControl(directory);
    return ambientAcceptRequests;
}
uint32_t AmbientSamplesForTest() { return ambientSamples; }
bool ProbeQueueCreatedForTest() { return retainedProbeQueue != nullptr; }
void EnableSkyForTest() { skyStreaming = true; }

const char* CapturePhaseForTest()
{
    AcquireSRWLockShared(&lock);
    const auto current = phase;
    ReleaseSRWLockShared(&lock);
    switch (current)
    {
        case Phase::Discover: return "discover (no source recorded)";
        case Phase::Found: return "found (not prepared)";
        case Phase::Preparing: return "preparing";
        case Phase::Ready: return "ready (no copy recorded)";
        case Phase::Recorded: return "recorded (not submitted)";
        case Phase::Submitting: return "submitting";
        case Phase::WaitingGpu: return "waiting-gpu (fence incomplete)";
        case Phase::Failed: return "failed";
        case Phase::Stopped: return "stopped";
    }
    return "unknown";
}
#endif
}

extern "C" void CdtCaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner)
{ cdt::render::CaptureFilter(outer, command, counterOuter, owner); }
extern "C" void CdtCaptureAmbient(uint64_t sky, uint64_t command, uint64_t path)
{ cdt::render::CaptureAmbient(sky, command, path); }
