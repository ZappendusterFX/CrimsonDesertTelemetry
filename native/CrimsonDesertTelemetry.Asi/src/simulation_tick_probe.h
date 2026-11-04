#pragma once
#include <cstddef>
#include <cstdint>
#include "acquisition_pause.h"

namespace cdt::simulation_tick_probe
{
inline constexpr std::uint32_t Magic = 0x54445443, Version = 2;
enum class Status : std::uint32_t { Starting = 1, Observing = 2, Stopped = 3, Failed = 4 };
enum class Error : std::uint32_t
{
    None, ExactBuild, Image, Signature, Prefix, Unwind, Mapping, ProcessIdentity,
    ModulePin, HookInitialize, HookCreate, HookEnable, HookDisable, Exception
};
// Local\CrimsonDesertTelemetry.SimulationTicks.<PID>. Independent simulation
// liveness and acquisition pause; never a light-output command. Reader checks an unchanged even seqlock,
// PID/process birth, and observerTickMs freshness. A static counter is not a
// static observer: Poll publishes at most20Hz independently of rendering.
struct alignas(8) Snapshot
{
    std::uint32_t magic, version, headerBytes, totalBytes;
    std::int64_t seqlock;
    std::uint32_t pid;
    Status status;
    std::uint64_t processStartFileTime, entryCounter, observerTickMs;
    std::uint64_t lastProgressTickMs, ageMs; // 0 / UINT64_MAX until progress
    std::uint32_t state, hookRva; // state: unknown0/running1/paused-after500ms2
    Error error;
    std::uint32_t flags; // bit0 entry-only, bit1 hook, bit2 acquisition paused, bit3 pause control
    std::uint8_t reserved[8];
    std::uint64_t captureEpoch, resumedAtTickMs;
    std::uint8_t reservedTail[16];
};
static_assert(sizeof(Snapshot) == 128 && offsetof(Snapshot, seqlock) == 16);
static_assert(offsetof(Snapshot, processStartFileTime) == 32 && offsetof(Snapshot, entryCounter) == 40);
static_assert(offsetof(Snapshot, observerTickMs) == 48 && offsetof(Snapshot, lastProgressTickMs) == 56);
static_assert(offsetof(Snapshot, ageMs) == 64 && offsetof(Snapshot, state) == 72);
static_assert(offsetof(Snapshot, hookRva) == 76 && offsetof(Snapshot, error) == 80 && offsetof(Snapshot, flags) == 84);
static_assert(offsetof(Snapshot, captureEpoch) == 96 && offsetof(Snapshot, resumedAtTickMs) == 104);

// Worker-thread only, once per process. Caller must opt in and verify the loaded
// native runtime before Start, including any guarded recovery. This module checks
// a unique executable-section match, exact prefix and unwind entry. Diagnostic-
// only mode additionally retains its exact contract hash.
bool Start(std::uint64_t moduleBase, bool pauseControlEnabled = false);
void Poll();
void Stop();
bool OwnsCodeAddress(std::uint64_t address);
inline bool ShouldPauseAcquisition() noexcept { return acquisition_pause::gate.Paused(); }
inline std::uint64_t CaptureEpoch() noexcept { return acquisition_pause::gate.Epoch(); }
inline bool IsCaptureCurrent(std::uint64_t epoch) noexcept { return acquisition_pause::gate.Current(epoch); }
inline std::uint64_t ResumedAtTickMs() noexcept { return acquisition_pause::gate.ResumedAt(); }
}
