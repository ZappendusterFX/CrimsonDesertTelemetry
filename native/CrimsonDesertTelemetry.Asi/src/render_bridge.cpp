#include "render_bridge.h"
#include "ambient_probe.h"
#include "sky_bridge.h"
#include "sdf_visibility.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace cdt::render
{
namespace
{
HANDLE mappingHandle{};
Mapping* mapping{};
bool inputMapped{};
SRWLOCK publishLock = SRWLOCK_INIT;
std::atomic<bool> sourceVisibilityEnabled{};
struct PreparedVisibility
{
    uint64_t volumeSequence{}, volumeTickMs{};
    uint32_t contextFrame{};
    std::array<VisibilityEntry, RecordCount> entries{};
};
// One bounded working buffer, protected by publishLock like all other writers.
// Readers keep seeing the preceding complete capture while geometry is computed.
PreparedVisibility preparedVisibility;
#ifdef CDT_RENDER_BRIDGE_TEST
std::atomic<void(*)()> beforeVisibilityTrace{};
#endif
template<class T> T At(const void* bytes, size_t offset)
{
    T value{};
    memcpy(&value, static_cast<const uint8_t*>(bytes) + offset, sizeof(value));
    return value;
}
void BeginWrite() { InterlockedIncrement64(&mapping->header.seqlock); MemoryBarrier(); }
void EndWrite() { MemoryBarrier(); InterlockedIncrement64(&mapping->header.seqlock); }

void PrepareVisibility(const void* scene, const void* lights, const void* counters, uint64_t now)
{
    auto& prepared=preparedVisibility;
    prepared.volumeSequence=0;prepared.volumeTickMs=0;prepared.contextFrame=0;
    const bool enabled=sourceVisibilityEnabled.load(std::memory_order_relaxed);
    prepared.entries.fill(VisibilityEntry{enabled?0u:11u,0});
    if(!enabled)return;
    const auto camera=At<std::array<float,3>>(scene,native_contract::PositionOffset);
    struct Target{uint32_t index;std::array<float,3> world;double distanceSquared;};
    std::vector<Target> candidates;
    const auto count=At<uint32_t>(counters,4);
    if(count>RecordCount||!ValidateScene(scene))return;
    candidates.reserve(std::min(count,sdf::MaximumBatchTargets));
    for(uint32_t index=0;index<count;++index)
    {
        const auto* record=static_cast<const uint8_t*>(lights)+size_t{index}*RecordStride;
        const auto marker=At<float>(record,12);
        if(!std::isfinite(marker)||std::fabs(marker-3.14159265f)>.0001f)continue;
        const auto relative=At<std::array<float,3>>(record,0);
        std::array<float,3> world{};double distanceSquared{};bool valid=true;
        for(unsigned axis=0;axis<3;++axis)
        {
            world[axis]=camera[axis]+relative[axis];
            valid=valid&&std::isfinite(relative[axis])&&std::isfinite(world[axis]);
            distanceSquared+=double{relative[axis]}*relative[axis];
        }
        if(!valid){prepared.entries[index].code=6;continue;}
        if(distanceSquared>100.0*100.0){prepared.entries[index].code=8;continue;}
        prepared.entries[index].code=7;
        candidates.push_back({index,world,distanceSquared});
    }
    const auto retained=std::min(candidates.size(),size_t{sdf::MaximumBatchTargets});
    std::partial_sort(candidates.begin(),candidates.begin()+retained,candidates.end(),
        [](const Target& a,const Target& b){return a.distanceSquared<b.distanceSquared ||
            (a.distanceSquared==b.distanceSquared&&a.index<b.index);});
    std::vector<std::array<float,3>> targets;targets.reserve(retained);
    for(size_t i=0;i<retained;++i)targets.push_back(candidates[i].world);
#ifdef CDT_RENDER_BRIDGE_TEST
    if(const auto observer=beforeVisibilityTrace.load())observer();
#endif
    const auto batch=sdf::TraceBatch(camera,targets,now);
    prepared.volumeSequence=batch.status.sequence;
    prepared.volumeTickMs=batch.status.capturedTickMilliseconds;
    prepared.contextFrame=batch.status.contextFrame;
    for(size_t i=0;i<batch.traces.size();++i)
    {
        const auto& trace=batch.traces[i];auto& entry=prepared.entries[candidates[i].index];
        if(!trace.available)
            entry.code=trace.reason=="stale-sdf-volume"?9u:
                trace.reason=="unbracketed-sdf-context"||trace.reason=="invalid-sdf-clock"||
                    trace.reason=="invalid-sdf-context"?10u:
                trace.reason=="waiting-for-sdf-volume"?0u:6u;
        else entry.code=static_cast<uint32_t>(trace.verdict);
        entry.closest=trace.samples&&std::isfinite(trace.closest)?static_cast<float>(trace.closest):0;
    }
}
}

void SetSourceVisibilityEnabled(bool enabled)
{
    sourceVisibilityEnabled.store(enabled,std::memory_order_relaxed);
}
#ifdef CDT_RENDER_BRIDGE_TEST
void SetBeforeVisibilityTraceForTest(void(*observer)()){beforeVisibilityTrace.store(observer);}
#endif

bool OpenBridge(bool includeInput)
{
    const auto name = L"Local\\CrimsonDesertTelemetry.Render." + std::to_wstring(GetCurrentProcessId());
    const uint32_t bytes = includeInput ? InputMappingBytes : MappingBytes;
    mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, name.c_str());
    if (!mappingHandle) return false;
    // Never take over another producer's mapping, including duplicate ASI copies.
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mappingHandle); mappingHandle = nullptr; return false; }
    mapping = static_cast<Mapping*>(MapViewOfFile(mappingHandle, FILE_MAP_WRITE, 0, 0, bytes));
    if (!mapping) { CloseHandle(mappingHandle); mappingHandle = nullptr; return false; }
    inputMapped = includeInput;
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    BeginWrite();
    mapping->header.magic = Magic;
    mapping->header.version = includeInput ? InputVersion : Version;
    mapping->header.headerBytes = sizeof(Header);
    mapping->header.totalBytes = bytes;
    mapping->header.inputBytes = includeInput ? InputBytes : 0;
    mapping->header.inputState = includeInput ? InputState::Unavailable : InputState::Disabled;
    mapping->header.pid = GetCurrentProcessId();
    mapping->header.processStartFileTime = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    mapping->header.sceneBytes = SceneBytes;
    mapping->header.rawCount = RecordCount;
    mapping->header.stride = RecordStride;
    mapping->header.counterBytes = CounterBytes;
    mapping->header.visibilityVersion = 1;
    mapping->header.visibilityEntryBytes = sizeof(VisibilityEntry);
    mapping->header.visibilityMaximumAgeMs = sdf::ProductionMaximumAgeMilliseconds;
    mapping->header.visibilityTraceBudget = sdf::MaximumBatchTargets;
    mapping->header.state = Status::Waiting;
    EndWrite();
    return true;
}

void PublishStatus(Status state, uint32_t error, uint32_t flags)
{
    if (!mapping) return;
    AcquireSRWLockExclusive(&publishLock);
    BeginWrite();
    mapping->header.state = state;
    mapping->header.error = error;
    mapping->header.flags = flags;
    mapping->header.publishedTickMs = GetTickCount64();
    EndWrite();
    ReleaseSRWLockExclusive(&publishLock);
}

void PublishInputState(InputState state)
{
    if (!mapping || !inputMapped) return;
    AcquireSRWLockExclusive(&publishLock);
    BeginWrite();
    mapping->header.inputState = state;
    mapping->header.inputResource = 0;
    EndWrite();
    ReleaseSRWLockExclusive(&publishLock);
}

void PublishSample(const void* scene, const void* lights, const void* counters, uint64_t capturedTickMs,
    uint64_t outputResource, uint64_t counterResource, uint64_t owner, uint32_t bufferIndex,
    const void* input, uint64_t inputResource, InputState inputState)
{
    if (!mapping) return;
    // An unpaired or absent input block is never published as this sample's.
    if (!inputMapped) inputState = InputState::Disabled;
    else if (inputState == InputState::Paired && (!input || !inputResource)) inputState = InputState::Unavailable;
    else if (inputState == InputState::Disabled) inputState = InputState::Unavailable;
    AcquireSRWLockExclusive(&publishLock);
    const auto publishedTickMs = GetTickCount64();
    try
    {
        PrepareVisibility(scene, lights, counters, publishedTickMs);
    }
    catch (...)
    {
        // Optional geometry failure must not retain partial or previous metadata.
        preparedVisibility.volumeSequence = 0;
        preparedVisibility.volumeTickMs = 0;
        preparedVisibility.contextFrame = 0;
        preparedVisibility.entries.fill(VisibilityEntry{10, 0});
    }
    // Only fixed-size copies and header updates belong in the inter-process
    // seqlock. SDF tracing must not make healthy raw lights temporarily unreadable.
    BeginWrite();
    memcpy(mapping->scene, scene, SceneBytes);
    memcpy(mapping->lights, lights, LightBytes);
    memcpy(mapping->counters, counters, CounterBytes);
    memcpy(mapping->visibility, preparedVisibility.entries.data(), sizeof(mapping->visibility));
    if (inputMapped)
    {
        // Readers ignore stale block bytes unless this sample marks them paired.
        if (inputState == InputState::Paired)
            memcpy(reinterpret_cast<uint8_t*>(mapping) + MappingBytes, input, InputBytes);
        mapping->header.inputState = inputState;
        mapping->header.inputResource = inputState == InputState::Paired ? inputResource : 0;
    }
    ++mapping->header.sampleSequence;
    mapping->header.capturedTickMs = capturedTickMs;
    mapping->header.publishedTickMs = publishedTickMs;
    mapping->header.frameNumber = At<uint32_t>(scene, native_contract::FrameOffset);
    mapping->header.error = 0;
    mapping->header.flags = ExactBuild | FenceCompleted | PairedScene | PairedCounter;
    mapping->header.outputResource = outputResource;
    mapping->header.counterResource = counterResource;
    mapping->header.owner = owner;
    mapping->header.bufferIndex = bufferIndex;
    mapping->header.state = Status::Active;
    mapping->header.visibilityVersion = 1;
    mapping->header.visibilityEntryBytes = sizeof(VisibilityEntry);
    mapping->header.visibilityMaximumAgeMs = sdf::ProductionMaximumAgeMilliseconds;
    mapping->header.visibilityTraceBudget = sdf::MaximumBatchTargets;
    mapping->header.visibilityVolumeSequence = preparedVisibility.volumeSequence;
    mapping->header.visibilityVolumeTickMs = preparedVisibility.volumeTickMs;
    mapping->header.visibilityContextFrame = preparedVisibility.contextFrame;
    EndWrite();
    ReleaseSRWLockExclusive(&publishLock);
}

bool ValidateScene(const void* scene)
{
    if (!scene || At<float>(scene, native_contract::EarthRadiusOffset) != native_contract::EarthRadius) return false;
    const auto screen = At<std::array<float, 4>>(scene, native_contract::ScreenOffset);
    if (!std::isfinite(screen[0]) || !std::isfinite(screen[1]) || screen[0] < 64 || screen[1] < 64 ||
        screen[0] > 32768 || screen[1] > 32768 || !std::isfinite(screen[2]) || !std::isfinite(screen[3]) ||
        std::fabs(screen[0] * screen[2] - 1.0f) > 0.002f || std::fabs(screen[1] * screen[3] - 1.0f) > 0.002f) return false;
    const auto position = At<std::array<float, 4>>(scene, native_contract::PositionOffset);
    const auto direction = At<std::array<float, 4>>(scene, native_contract::DirectionOffset);
    float length = 0;
    for (size_t i = 0; i < 3; ++i)
    {
        if (!std::isfinite(position[i]) || std::fabs(position[i]) > 1000000 || !std::isfinite(direction[i])) return false;
        length += direction[i] * direction[i];
    }
    return length > 0.95f && length < 1.05f;
}

bool SameScene(const void* first, const void* second)
{
    return At<uint32_t>(first, native_contract::FrameOffset) == At<uint32_t>(second, native_contract::FrameOffset) &&
        memcmp(static_cast<const uint8_t*>(first) + native_contract::PositionOffset,
            static_cast<const uint8_t*>(second) + native_contract::PositionOffset, 32) == 0 &&
        memcmp(static_cast<const uint8_t*>(first) + native_contract::ViewRelativeOffset,
            static_cast<const uint8_t*>(second) + native_contract::ViewRelativeOffset, 64) == 0;
}
}

namespace cdt::sky
{
namespace { HANDLE handle{}; Mapping* mapping{}; SRWLOCK lock = SRWLOCK_INIT;
void Begin() { InterlockedIncrement64(&mapping->header.seqlock); MemoryBarrier(); }
void End() { MemoryBarrier(); InterlockedIncrement64(&mapping->header.seqlock); }
}
bool OpenBridge()
{
    if (mapping) return false;
    const auto name = L"Local\\CrimsonDesertTelemetry.Sky." + std::to_wstring(GetCurrentProcessId());
    handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Mapping), name.c_str());
    if (!handle) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(handle); handle = nullptr; return false; }
    mapping = static_cast<Mapping*>(MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, sizeof(Mapping)));
    if (!mapping) { CloseHandle(handle); handle = nullptr; return false; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    { UnmapViewOfFile(mapping); mapping = nullptr; CloseHandle(handle); handle = nullptr; return false; }
    Begin();
    auto& h = mapping->header;
    h.magic = 0x53445443; h.version = Version; h.headerBytes = sizeof(Header); h.totalBytes = sizeof(Mapping);
    h.pid = GetCurrentProcessId(); h.state = render::Status::Stopped;
    h.processStartFileTime = (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    h.sceneBytes = render::SceneBytes; h.payloadBytes = PayloadBytes;
    h.cameraSkyVisibilityWorking = 0.0; h.visibilityState = Visibility::Unavailable;
    h.visibilityFrameNumber = 0; h.visibilityTickMs = 0;
    End();
    return true;
}
void PublishStatus(render::Status state, uint32_t error)
{
    if (!mapping) return;
    AcquireSRWLockExclusive(&lock); Begin();
    mapping->header.state = state; mapping->header.error = error;
    mapping->header.flags = 0; mapping->header.publishedTickMs = GetTickCount64();
    End(); ReleaseSRWLockExclusive(&lock);
}
uint32_t FailureCode()
{
    if (!mapping) return ERROR_INVALID_HANDLE;
    AcquireSRWLockShared(&lock);
    const auto result = mapping->header.error;
    ReleaseSRWLockShared(&lock);
    return result;
}
void PublishSample(const void* scene, const void* data, uint64_t tick, uint64_t resource, uint32_t producerRva)
{
    if (!mapping || !render::ValidateScene(scene) || !data || !tick || !resource || producerRva != render::AmbientHookRvas()[0]) return;
    AcquireSRWLockExclusive(&lock); Begin();
    memcpy(mapping->scene, scene, render::SceneBytes); memcpy(mapping->data, data, PayloadBytes);
    auto& h = mapping->header;
    ++h.sampleSequence; h.capturedTickMs = tick; h.publishedTickMs = GetTickCount64();
    memcpy(&h.frameNumber, mapping->scene + native_contract::FrameOffset, sizeof(h.frameNumber));
    h.resource = resource; h.producerRva = producerRva; h.error = 0;
    h.flags = render::ExactBuild | render::FenceCompleted | render::PairedScene;
    h.state = render::Status::Active;
    End(); ReleaseSRWLockExclusive(&lock);
}
void PublishVisibility(double value, Visibility state, uint32_t frameNumber, uint64_t tickMs)
{
    // A different subsystem at a different rate, so this deliberately leaves
    // state, sampleSequence, flags and the sky frame alone. It only borrows the
    // lock and seqlock so a reader cannot observe a half-written block.
    if (!mapping) return;
    if (state != Visibility::Valid) { value = 0.0; frameNumber = 0; tickMs = 0; }
    AcquireSRWLockExclusive(&lock); Begin();
    auto& h = mapping->header;
    h.cameraSkyVisibilityWorking = value; h.visibilityState = state;
    h.visibilityFrameNumber = frameNumber; h.visibilityTickMs = tickMs;
    End(); ReleaseSRWLockExclusive(&lock);
}
}
