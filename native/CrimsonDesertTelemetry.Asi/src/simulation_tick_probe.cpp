// Movement-pump signature informed by World Builder, MIT:
// Copyright (c)2026 Moon-yungg. See licenses/WorldBuilder-MIT.txt.
// Offline identity evidence: artifacts/worldbuilder-tick-20261104/.
#include "simulation_tick_probe.h"
#include "simulation_tick_scan.h"
#include "simulation_tick.h"
#include "menu_state.h"
#include "console/mem.h"
#include "runtime_contract.generated.h"
#include <MinHook.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
alignas(64) volatile LONG64 CdtSimulationTickCounter = 0;
void* CdtSimulationTickTrampoline = nullptr;
void CdtSimulationTickThunk();
}

namespace cdt::simulation_tick_probe
{
namespace
{
constexpr bool DiagnosticBuild = native_contract::BuildId == "26423880" || native_contract::BuildId == "26448710";
HANDLE mapping{};
Snapshot* view{};
Snapshot current{};
std::atomic<bool> enabled{};
std::atomic<std::uint64_t> target{};
bool attempted{};
bool pauseControl{};
std::uint64_t lastPoll{};
simulation_tick::Monitor monitor;

std::uint64_t Counter()
{
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&CdtSimulationTickCounter, 0, 0));
}
void Publish()
{
    if (!view) return;
    current.observerTickMs = GetTickCount64();
    current.flags = 1u | (enabled.load() ? 2u : 0u) | (ShouldPauseAcquisition() ? 4u : 0u) | (pauseControl ? 8u : 0u);
    current.captureEpoch = CaptureEpoch();
    current.resumedAtTickMs = ResumedAtTickMs();
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&view->seqlock));
    // Preserve the shared seqlock while replacing its surrounding payload.
    std::memcpy(view, &current, offsetof(Snapshot, seqlock));
    constexpr auto afterSequence = offsetof(Snapshot, pid);
    std::memcpy(reinterpret_cast<unsigned char*>(view) + afterSequence,
        reinterpret_cast<const unsigned char*>(&current) + afterSequence, sizeof(Snapshot) - afterSequence);
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&view->seqlock));
}
bool TickFailure(Error error)
{
    current.error = error;
    current.entryCounter = Counter();
    current.state = 0;
    current.lastProgressTickMs = 0;
    current.ageMs = UINT64_MAX;
    return false;
}
bool OpenMapping()
{
    FILETIME born{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &born, &exited, &kernel, &user)) return false;
    current = {};
    current.magic = Magic; current.version = pauseControl ? MenuVersion : DiagnosticVersion;
    current.headerBytes = current.totalBytes = sizeof(Snapshot);
    current.pid = GetCurrentProcessId(); current.status = Status::Starting;
    current.processStartFileTime = (std::uint64_t{born.dwHighDateTime} << 32) | born.dwLowDateTime;
    current.ageMs = UINT64_MAX;
    const auto name = L"Local\\CrimsonDesertTelemetry.SimulationTicks." + std::to_wstring(current.pid);
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Snapshot), name.c_str());
    if (!mapping) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping); mapping = nullptr; return false; }
    view = static_cast<Snapshot*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Snapshot)));
    if (!view) { CloseHandle(mapping); mapping = nullptr; return false; }
    Publish();
    return true;
}
bool ImageSections(std::uint64_t base, std::uint32_t& size, std::vector<scan::Section>& sections)
{
    if (base != reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr))) return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!ch::mem::SafeRead(reinterpret_cast<void*>(base), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !ch::mem::SafeRead(reinterpret_cast<void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96 ||
        nt.OptionalHeader.SizeOfImage < 4096 || nt.OptionalHeader.SizeOfImage > 0x40000000) return false;
    size = nt.OptionalHeader.SizeOfImage;
    const auto table = static_cast<std::uint32_t>(dos.e_lfanew) + sizeof(nt);
    const auto tableBytes = std::size_t{nt.FileHeader.NumberOfSections} * sizeof(IMAGE_SECTION_HEADER);
    if (table >= size || tableBytes > size - table) return false;
    std::vector<IMAGE_SECTION_HEADER> headers(nt.FileHeader.NumberOfSections);
    if (!ch::mem::SafeRead(reinterpret_cast<void*>(base + table), headers.data(), tableBytes)) return false;
    for (const auto& h : headers)
        sections.push_back({h.VirtualAddress, h.Misc.VirtualSize, (h.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
    return true;
}
bool StartTickHook(std::uint64_t moduleBase)
{
    try
    {
        // The caller already completed the normal native validation/recovery.
        // Diagnostic-only packages retain their original exact-build boundary.
        if (!pauseControl && !DiagnosticBuild) return TickFailure(Error::ExactBuild);
        std::uint32_t imageBytes{};
        std::vector<scan::Section> sections;
        if (!ImageSections(moduleBase, imageBytes, sections)) return TickFailure(Error::Image);
        const auto read = [=](std::uint32_t rva, std::span<std::uint8_t> bytes) {
            return rva < imageBytes && bytes.size() <= imageBytes - rva &&
                ch::mem::SafeRead(reinterpret_cast<const void*>(moduleBase + rva), bytes.data(), bytes.size());
        };
        const auto match = scan::Find(imageBytes, sections, read);
        if (match.result != scan::Result::Unique)
            return TickFailure(match.result == scan::Result::PrefixMismatch ? Error::Prefix : Error::Signature);
        current.hookRva = match.rva;
        const auto address = moduleBase + match.rva;
        DWORD64 unwindBase{};
        const auto function = RtlLookupFunctionEntry(address, &unwindBase, nullptr);
        if (!function || unwindBase != moduleBase || function->BeginAddress != match.rva ||
            function->EndAddress > imageBytes || function->EndAddress <= function->BeginAddress ||
            function->EndAddress - function->BeginAddress < scan::Prefix.size()) return TickFailure(Error::Unwind);
        std::array<std::uint8_t, scan::Prefix.size()> before{};
        if (!read(match.rva, before) || before != scan::Prefix) return TickFailure(Error::Prefix);
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&CdtSimulationTickThunk), &pinned)) return TickFailure(Error::ModulePin);
        const auto initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return TickFailure(Error::HookInitialize);
        target.store(address);
        if (MH_CreateHook(reinterpret_cast<void*>(address), &CdtSimulationTickThunk,
            &CdtSimulationTickTrampoline) != MH_OK) return TickFailure(Error::HookCreate);
        // CreateHook does not patch the target. Recheck immediately before enable.
        if (!read(match.rva, before) || before != scan::Prefix)
        {
            MH_RemoveHook(reinterpret_cast<void*>(address));
            CdtSimulationTickTrampoline = nullptr;
            return TickFailure(Error::Prefix);
        }
        monitor.Reset();
        current.entryCounter = Counter();
        monitor.Observe(current.entryCounter, simulation_tick::Monitor::Clock::now());
        if (MH_EnableHook(reinterpret_cast<void*>(address)) != MH_OK)
        {
            MH_RemoveHook(reinterpret_cast<void*>(address)); // Never enabled: no in-flight trampoline.
            CdtSimulationTickTrampoline = nullptr;
            return TickFailure(Error::HookEnable);
        }
        enabled = true;
        current.error = Error::None;
        return true;
    }
    catch (...) { return TickFailure(Error::Exception); }
}
}

bool Start(std::uint64_t moduleBase, std::uint32_t imageSize, bool pauseControlEnabled)
{
    if (attempted) return false;
    attempted = true;
    pauseControl = pauseControlEnabled;
    if (!OpenMapping()) return false;
    const bool menuReady = pauseControl && menu_state::Initialize(moduleBase, imageSize);
    if (pauseControl)
        ch::Log(menuReady ? "Menu activity: guarded UI registry resolved." :
            "Menu activity unavailable: UI registry did not verify. Acquisition continues normally.");
    const bool tickReady = StartTickHook(moduleBase);
    if (!tickReady) ch::Log("Simulation tick observation unavailable (error %u).", static_cast<unsigned>(current.error));
    const bool observing = menuReady || tickReady;
    current.status = observing ? Status::Observing : Status::Failed;
    lastPoll = GetTickCount64();
    Publish();
    return observing;
}
void Poll()
{
    const auto now = GetTickCount64();
    if (current.status != Status::Observing || now - lastPoll < 50) return;
    lastPoll = now;
    const auto observed = simulation_tick::Monitor::Clock::now();
    current.entryCounter = Counter();
    if (enabled.load()) current.state = static_cast<std::uint32_t>(monitor.Observe(current.entryCounter, observed));
    const auto progressed = monitor.LastProgress();
    current.ageMs = progressed ? static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(observed - *progressed).count()) : UINT64_MAX;
    current.lastProgressTickMs = progressed && current.ageMs <= now ? now - current.ageMs : 0;
    const auto menu = pauseControl ? menu_state::Sample() : menu_state::State::Unknown;
    current.menuState = static_cast<std::uint32_t>(menu);
    acquisition_pause::gate.Update(menu == menu_state::State::Open, now);
    Publish();
}
void Stop()
{
    acquisition_pause::gate.Update(false, GetTickCount64());
    current.menuState = 0;
    if (enabled.load())
    {
        const auto result = MH_DisableHook(reinterpret_cast<void*>(target.load()));
        if (result == MH_OK || result == MH_ERROR_DISABLED) enabled = false;
        else { TickFailure(Error::HookDisable); current.status = Status::Failed; }
    }
    if (current.status != Status::Failed) current.status = Status::Stopped;
    current.entryCounter = Counter();
    Publish();
    if (view) { UnmapViewOfFile(view); view = nullptr; }
    if (mapping) { CloseHandle(mapping); mapping = nullptr; }
    // No RemoveHook/Uninitialize after enable: an already-entered detour can
    // still reach the trampoline. Pinned code and global storage survive to exit.
}
bool OwnsCodeAddress(std::uint64_t address)
{
    const auto at = target.load();
    return enabled.load() && at && address >= at && address - at < scan::Prefix.size();
}
}
