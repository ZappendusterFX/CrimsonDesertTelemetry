#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "native_contract.generated.h"

namespace cdt::render
{
constexpr uint32_t Magic = 0x52445443;
// 3 = scene/output/counter/visibility. 4 appends the paired ManyLights INPUT
// block after that unchanged v3 prefix; it exists only when requested.
constexpr uint32_t Version = 3;
constexpr uint32_t InputVersion = 4;
constexpr uint32_t SceneBytes = native_contract::SceneBytes;
constexpr uint32_t RecordCount = native_contract::RecordCount;
constexpr uint32_t RecordStride = native_contract::RecordStride;
constexpr uint32_t LightBytes = RecordCount * RecordStride;
constexpr uint32_t CounterBytes = native_contract::CounterBytes;
constexpr uint32_t LegacyMappingBytes = 256 + SceneBytes + LightBytes + CounterBytes;
struct VisibilityEntry { uint32_t code{}; float closest{}; };
static_assert(sizeof(VisibilityEntry) == 8);
constexpr uint32_t MappingBytes = LegacyMappingBytes + RecordCount * sizeof(VisibilityEntry);
// The ManyLights INPUT has the output's capacity/stride; only the consumer
// counter's DWORD0 (already in `counters`) says how many records are current.
constexpr uint32_t InputBytes = LightBytes;
constexpr uint32_t InputMappingBytes = MappingBytes + InputBytes;
enum class Status : uint32_t { Waiting, Active, Incompatible, Fault, LegacyConflict, Stopped };
// Bit1 denotes a fully guarded per-executable contract, either embedded exact or
// recovered then independently checked before hooks. Keep the old alias/ABI.
enum Flags : uint32_t { ValidatedContract = 1, ExactBuild = ValidatedContract, FenceCompleted = 2, PairedScene = 4, PairedCounter = 8 };
// Disabled: no input block. Paired: this sample's block was copied on the same
// list/fence as its output. Unavailable: requested, but not in this sample.
// Refused: the exact-build input anchors or device support did not verify.
enum class InputState : uint32_t { Disabled, Paired, Unavailable, Refused };

// Little-endian inter-process ABI. Readers acquire seqlock before/after copying
// and reject odd or changed values. Do not expose std::atomic across the ABI.
struct alignas(8) Header
{
    uint32_t magic, version, headerBytes, totalBytes;
    volatile LONG64 seqlock;
    uint32_t pid;
    Status state;
    uint64_t processStartFileTime, sampleSequence, capturedTickMs, publishedTickMs;
    uint32_t frameNumber, sceneBytes, rawCount, stride, error, flags;
    uint64_t outputResource, counterResource, owner;
    uint32_t bufferIndex, counterBytes;
    uint32_t visibilityVersion, visibilityEntryBytes;
    uint64_t visibilityVolumeSequence, visibilityVolumeTickMs;
    uint32_t visibilityContextFrame, visibilityMaximumAgeMs, visibilityTraceBudget, visibilityReserved;
    uint64_t inputResource;
    InputState inputState;
    uint32_t inputBytes;
    uint8_t reserved[80];
};
struct Mapping
{
    Header header;
    uint8_t scene[SceneBytes];
    uint8_t lights[LightBytes];
    uint8_t counters[CounterBytes];
    VisibilityEntry visibility[RecordCount];
};
static_assert(sizeof(Header) == 256 && sizeof(Mapping) == MappingBytes);
static_assert(offsetof(Header, seqlock) == 16 && offsetof(Header, flags) == 84);
static_assert(offsetof(Header, outputResource) == 88 && offsetof(Header, counterBytes) == 116);
static_assert(offsetof(Header, visibilityVersion) == 120 && offsetof(Header, visibilityTraceBudget) == 152);
static_assert(offsetof(Header, inputResource) == 160 && offsetof(Header, inputState) == 168 &&
    offsetof(Header, inputBytes) == 172);
static_assert(offsetof(Mapping, scene) == 256 && offsetof(Mapping, lights) == 3072);
static_assert(offsetof(Mapping, counters) == 3072 + LightBytes);
static_assert(offsetof(Mapping, visibility) == LegacyMappingBytes);
// Version 4 only: the input block starts directly after the complete v3 mapping.
inline const uint8_t* InputBlock(const Mapping* mapping)
{ return reinterpret_cast<const uint8_t*>(mapping) + MappingBytes; }

bool OpenBridge(bool includeInput = false);
void SetSourceVisibilityEnabled(bool enabled);
void PublishStatus(Status state, uint32_t error = 0, uint32_t flags = 0);
// Only meaningful for a version-4 mapping; permanent until the next paired sample.
void PublishInputState(InputState state);
// `input` must be the full-capacity input copied with THIS output/counter, or null.
void PublishSample(const void* scene, const void* lights, const void* counters, uint64_t capturedTickMs,
    uint64_t outputResource, uint64_t counterResource, uint64_t owner, uint32_t bufferIndex,
    const void* input = nullptr, uint64_t inputResource = 0, InputState inputState = InputState::Disabled);
#ifdef CDT_RENDER_BRIDGE_TEST
void SetBeforeVisibilityTraceForTest(void(*observer)());
#endif
// Validation independent of game pointers, also used by the smoke test.
bool ValidateScene(const void* scene);
bool SameScene(const void* first, const void* second);
}
