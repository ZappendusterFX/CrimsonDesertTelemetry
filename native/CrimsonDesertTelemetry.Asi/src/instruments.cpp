#include "instruments.h"
#include "build_guard.h"
#include "render_capture.h"
#include "manylights_pair.h"
#include "ambient_probe.h"
#include "render_bridge.h"
#include "source_visibility_bridge.h"
#include "sky_bridge.h"
#include "spatial_probe.h"
#include "physics_probe.h"
#include "simulation_tick_probe.h"
#include "runtime_contract.generated.h"
#include "runtime_recovery.h"
#include "overlay.h"
#include "console/common.h"
#include "console/breakpoint.h"
#include "console/console.h"
#include "console/discovery.h"
#include "console/explorer.h"
#include "console/mem.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace cdt::instruments
{
namespace
{
HMODULE self{};
HANDLE singleton{};
std::wstring iniPath;
std::string moduleDirectory;
bool legacy{}, duplicate{}, verified{}, hashChecked{}, requested{}, earlyFailed{};
bool LegacyPresent()
{
    if (GetModuleHandleW(L"CrimsonHueConsole.asi") || GetModuleHandleW(L"CrimsonHueConsole.dll")) return true;
    // Also refuse a legacy ASI that the loader has not loaded yet. This makes
    // mixed installations safe independent of alphabetical loading order.
    const auto path = ch::GameDirectory() + "\\CrimsonHueConsole.asi";
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA((moduleDirectory + "\\CrimsonHueConsole.asi").c_str()) != INVALID_FILE_ATTRIBUTES;
}
void PinModule()
{
    HMODULE pinned{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(self), &pinned);
}
}

void EarlyAttach(HMODULE module)
{
    self = module;
    try
    {
        std::array<wchar_t, 32768> path{};
        if (!GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) { earlyFailed = true; return; }
        const auto directory = std::filesystem::path(path.data()).parent_path();
        moduleDirectory = directory.string();
        iniPath = (directory / L"CrimsonDesertTelemetry.ini").wstring();
#if CDT_RESEARCH
        ch::LoadConfig(moduleDirectory);
#endif
        const auto name = L"Local\\CrimsonDesertTelemetry.Native." + std::to_wstring(GetCurrentProcessId());
        singleton = CreateMutexW(nullptr, FALSE, name.c_str());
        duplicate = !singleton || GetLastError() == ERROR_ALREADY_EXISTS;
        legacy = LegacyPresent();
        if (duplicate || legacy) return;
#if CDT_RESEARCH
        requested = ch::g_cfg.enableConsole || ch::g_cfg.enableExplorer;
        if (requested)
        {
            // The optional research startup capture must run before the game
            // executes registration. Hashing on this same thread preserves that
            // ordering; moving the arm to a delayed worker loses debugThis.
            hashChecked = true;
            verified = VerifyExecutable();
            if (!verified) return;
            PinModule();
            ch::discovery::EarlyPatch();
        }
#endif
    }
    catch (...) { earlyFailed = true; }
}

void RunImpl(HANDLE stopEvent)
{
    if (duplicate) return;
    ch::OpenLog(moduleDirectory);
#if CDT_RESEARCH
    ch::LogConfig();
    ch::bp::ReportStartup();
#else
    ch::Log("Production build: Research, Console, Explorer and legacy OcclusionTest settings are inactive.");
#endif
    const bool skyBridge = sky::OpenBridge();
    // The input block is part of the mapping's fixed size, so decide before opening.
    const bool upstreamRequested = !earlyFailed &&
        GetPrivateProfileIntW(L"Lights", L"Enabled", 0, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"Lights", L"Upstream", 1, iniPath.c_str()) != 0;
    if (!render::OpenBridge(upstreamRequested))
    {
        ch::Log("Native bridge already owned or unavailable; instrumentation skipped.");
        overlay::SetLocalFault("native-capture", "Native telemetry bridge unavailable",
            "A duplicate telemetry producer or a shared-memory failure prevents capture. Check the installed ASIs and native log, then restart the game.");
        return;
    }
    if (legacy || LegacyPresent())
    {
        ch::Log("Native instruments refused: legacy CrimsonHueConsole is still installed/loaded. Archive the old ASI and restart.");
        render::PublishStatus(render::Status::LegacyConflict, ERROR_ALREADY_EXISTS);
        sky::PublishStatus(render::Status::LegacyConflict, ERROR_ALREADY_EXISTS);
        overlay::SetLocalFault("native-capture", "Conflicting legacy console plugin",
            "CrimsonHueConsole is still installed or loaded. Archive the old ASI and restart with only the integrated CrimsonDesertTelemetry plugin.");
        return;
    }
    if (earlyFailed)
    {
        render::PublishStatus(render::Status::Fault, ERROR_DLL_INIT_FAILED);
        sky::PublishStatus(render::Status::Fault, ERROR_DLL_INIT_FAILED);
        overlay::SetLocalFault("native-capture", "Native configuration or startup failed",
            "The plugin could not initialize its configuration or module context. Check CrimsonDesertTelemetry.ini and the native log, then restart.");
        return;
    }
    const bool captureEnabled = GetPrivateProfileIntW(L"Lights", L"Enabled", 0, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0;
    // Independent tick observer for production pause control or a monitor-only diagnostic.
#if CDT_RESEARCH
    constexpr bool simulationTickRequested = false;
    constexpr bool pauseControlEnabled = false;
#else
    const bool simulationTickRequested = GetPrivateProfileIntW(L"Diagnostics", L"SimulationTicks", 0, iniPath.c_str()) != 0;
    const bool pauseControlEnabled = GetPrivateProfileIntW(L"Pause", L"Enabled", 1, iniPath.c_str()) != 0;
#endif
    if (!requested && !captureEnabled && !simulationTickRequested && !pauseControlEnabled)
    {
        render::PublishStatus(render::Status::Stopped);
        overlay::ClearLocalFault("native-capture");
        return;
    }
    std::array<unsigned char,32> executableHash{};
    if (!ReadExecutableHash(executableHash))
    {
        render::PublishStatus(render::Status::Incompatible, ERROR_READ_FAULT);
        sky::PublishStatus(render::Status::Incompatible, ERROR_READ_FAULT);
        overlay::SetLocalFault("native-capture", "Game executable could not be verified",
            "The executable could not be read completely. Native game hooks remain disabled.");
        return;
    }
    hashChecked = true;
    verified = executableHash == native_contract::ExecutableSha256;
    const auto moduleBase = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    // Guard failures on a known executable also enter recovery. The host searches
    // once; it must never adopt a modified/ambiguous context merely to keep going.
    bool exactContexts = verified && static_cast<bool>(render::CheckCapturePreflight(moduleBase));
    for (const auto& context : native_contract::support::Contexts)
    {
        std::array<uint8_t,128> actual{};
        if (context.bytes.size()>actual.size() ||
            !ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase+context.rva),actual.data(),context.bytes.size()) ||
            !std::equal(context.bytes.begin(),context.bytes.end(),actual.begin())) exactContexts = false;
    }
    const bool ambientRequested = GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0;
    if (ambientRequested && exactContexts && !render::CheckAmbientPreflight(moduleBase)) exactContexts = false;
    const bool physicsRecoveryRequested = GetPrivateProfileIntW(L"SourceVisibility", L"Enabled", 0, iniPath.c_str()) != 0;
    const uint32_t requestedFeatures = (captureEnabled?1u:0u)|(upstreamRequested?2u:0u)|
        (ambientRequested?4u:0u)|(physicsRecoveryRequested?8u:0u);
    recovery::Handoff recovery;
#if CDT_RESEARCH
    // The retained research instruments have additional historical offsets and
    // remain exact-only. Automatic recovery belongs to the production pipeline.
    constexpr bool recoverySupported = false;
#else
    constexpr bool recoverySupported = native_contract::support::Available;
#endif
    recovery::DiagnosticRequest diagnostic;
    if constexpr (recoverySupported)
    {
        std::array<wchar_t,34> token{};
        const auto count=GetPrivateProfileStringW(L"Compatibility",L"RecoveryTestOnce",L"",
            token.data(),static_cast<DWORD>(token.size()),iniPath.c_str());
        std::string failure;
        if(!recovery::ReadDiagnosticRequest(std::wstring_view(token.data(),count),diagnostic,failure))
        {
            ch::Log("One-time recovery test refused: %s",failure.c_str());
            overlay::SetLocalFault("native-capture","Recovery test configuration is invalid",failure);
            return;
        }
        if(count)
            ch::Log(diagnostic.force
                ? "One-time recovery test armed: exact-build fast path is skipped; cold discovery and all live guards remain required."
                : "One-time recovery test was already consumed; using ordinary executable and context checks.");
    }
    const bool recoveryNeeded = !verified || !exactContexts || diagnostic.force;
    if (recoverySupported)
    {
        if (!recovery.Initialize(executableHash,recoveryNeeded,requestedFeatures,diagnostic))
        {
            overlay::SetLocalFault("native-capture", "Recovery communication unavailable",
                "The per-process compatibility channel could not initialize. Native game hooks remain disabled.");
            return;
        }
    }
    if (recoveryNeeded)
    {
        render::PublishStatus(render::Status::Incompatible, ERROR_REVISION_MISMATCH);
        sky::PublishStatus(render::Status::Incompatible, ERROR_REVISION_MISMATCH);
        if (!recoverySupported)
        {
            ch::Log("Native executable/context guard refused; this archived/research contract has no runtime recovery metadata.");
            overlay::SetLocalFault("native-capture", "This game build needs a current telemetry package",
                "Native hooks remain disabled. Install a package containing automatic update recovery for this game layout.");
            return;
        }
        ch::Log(diagnostic.force
            ? "Starting explicitly requested one-time recovery test; reference values and EXE identity remain intact."
            : "Executable or code context changed: requesting one conditional in-game address recovery.");
        if (!recovery.WaitAndApply(stopEvent)) return;
        overlay::ClearLocalFault("native-capture");
    }
    PinModule();
    if (!ch::mem::GetModuleRange(nullptr, &ch::g_game.moduleBase, &ch::g_game.moduleSize))
    {
        render::PublishStatus(render::Status::Fault, ERROR_MOD_NOT_FOUND);
        sky::PublishStatus(render::Status::Fault, ERROR_MOD_NOT_FOUND);
        overlay::SetLocalFault("native-capture", "Game module unavailable",
            "The plugin could not resolve the game image. Check the native log and restart the game; no capture hook was installed.");
        return;
    }
#if CDT_RESEARCH
    if (requested)
    {
        const DWORD delay = std::clamp(ch::g_cfg.initDelayMs, 1u, 30000u);
        if (WaitForSingleObject(stopEvent, delay) != WAIT_TIMEOUT) return;
        if (ch::g_cfg.enableConsole || (ch::g_cfg.enableExplorer && ch::g_cfg.allowDebugCommands))
        {
            for (unsigned i = 0; i < 300 && !ch::g_game.window; ++i)
            {
                ch::g_game.window = ch::console::FindGameWindow();
                if (!ch::g_game.window && WaitForSingleObject(stopEvent, 100) != WAIT_TIMEOUT) return;
            }
            const bool discovered = ch::discovery::Discover();
            ch::Log("Integrated research discovery completed: %s", discovered ? "ready" : "partial/unavailable");
            if (ch::g_cfg.enableConsole && ch::g_game.window)
            {
                ch::discovery::DumpRegisteredCommands();
                ch::console::HookWndProc(ch::g_game.window);
            }
        }
        else ch::discovery::DisarmEarlyBreakpoint();
        if (ch::g_cfg.enableExplorer) ch::explorer::Start();
    }
#endif
    bool capturing = false;
    bool upstreamStarted = !upstreamRequested;
    if (captureEnabled)
    {
        const unsigned rate = GetPrivateProfileIntW(L"Lights", L"ManyLightsSampleRateHz", 20, iniPath.c_str());
#if CDT_RESEARCH
        const bool ambientProbe = GetPrivateProfileIntW(L"Research", L"AmbientProbe", 0, iniPath.c_str()) != 0;
        if (!ambientProbe && GetPrivateProfileIntW(L"Research", L"ManyLightsPair", 0, iniPath.c_str()) != 0 &&
            !render::EnableManyLightsPair(ch::g_game.moduleBase, std::filesystem::path(moduleDirectory).c_str()))
            ch::Log("ManyLights INPUT/OUTPUT probe initialization refused; normal capture remains independent.");
#else
        // An old diagnostic INI must never replace the production light/sky feed.
        constexpr bool ambientProbe = false;
#endif
        const bool streamSky = skyBridge && GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0;
        if (upstreamRequested && !ambientProbe)
        {
            upstreamStarted = render::EnableUpstreamInput(ch::g_game.moduleBase);
            if (!upstreamStarted) ch::Log("ManyLights INPUT refused: guarded input anchors did not verify; filtered output remains independent.");
        }
        capturing = ambientProbe
            ? render::StartAmbientProbe(ch::g_game.moduleBase, std::filesystem::path(moduleDirectory).c_str())
            : render::StartCapture(ch::g_game.moduleBase, rate, streamSky);
        if (!capturing)
        {
            if (streamSky) sky::PublishStatus(render::Status::Fault, ERROR_INVALID_FUNCTION);
            ch::Log("Automatic ManyLights instrumentation failed closed; authored telemetry remains independent.");
            const auto preflight = render::CheckCapturePreflight(ch::g_game.moduleBase);
            overlay::SetLocalFault("native-capture", "Native light capture refused initialization",
                !preflight ? std::string("Build context check failed: ") + render::PreflightFailureName(preflight.failure) +
                    ". No new capture hook was installed. Check for conflicting mods or use a telemetry release validated for this game build." :
                    "The validated capture hook could not initialize. Check the native log for MinHook or graphics errors and restart the game.");
        }
        else overlay::ClearLocalFault("native-capture");
        if (GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0 &&
            (!skyBridge || (!ambientProbe && sky::FailureCode())))
            overlay::SetLocalFault("ambient-capture", "Global sky capture unavailable",
                "The sky bridge or validated capture hook could not initialize. Local lights may still work. Check the native log; use a plugin validated for this game build.");
        else overlay::ClearLocalFault("ambient-capture");
    }
    else overlay::ClearLocalFault("native-capture");
#if CDT_RESEARCH
    const bool physicsVisibility =
        GetPrivateProfileIntW(L"Experimental", L"PhysicsVisibility", 0, iniPath.c_str()) != 0;
    const bool sourceVisibility = !physicsVisibility && GetPrivateProfileIntW(L"SourceVisibility", L"Enabled", 0, iniPath.c_str()) != 0;
#else
    // 2.2: production source visibility IS the continuous physics ray fan. The
    // retired SDF camera-to-source classifier gave false clears through walls.
    const bool physicsVisibility = captureEnabled &&
        GetPrivateProfileIntW(L"SourceVisibility", L"Enabled", 0, iniPath.c_str()) != 0;
    constexpr bool sourceVisibility = false;
#endif
    // OFF uses the narrow render-capture path with its paired camera. ON keeps
    // that path disabled and uses the player/all-known-source bridge below.
    render::SetSourceVisibilityEnabled(!CDT_RESEARCH && sourceVisibility);
#if CDT_RESEARCH
    const bool sourceVisibilityBridge = sourceVisibility && source_visibility::Open();
    if (sourceVisibility && !sourceVisibilityBridge)
        ch::Log("Source visibility query bridge could not initialize; light records remain available without geometry metadata.");
#endif
#if CDT_RESEARCH
    const bool hudOcclusion = !physicsVisibility && GetPrivateProfileIntW(L"LightOverlay", L"Enabled", 0, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"LightOverlay", L"OcclusionTest", 0, iniPath.c_str()) != 0;
    const bool spatialProbe = captureEnabled && (sourceVisibility || hudOcclusion || GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0 ||
        GetPrivateProfileIntW(L"Research", L"SpatialProbe", 0, iniPath.c_str()) != 0);
    const bool spatialReadback = GetPrivateProfileIntW(L"Research", L"SpatialReadback", 0, iniPath.c_str()) != 0;
    const bool persistDistanceReadback = GetPrivateProfileIntW(L"Research", L"SignedDistanceReadback", 0, iniPath.c_str()) != 0;
    const bool distanceReadback = persistDistanceReadback || hudOcclusion || sourceVisibility;
    const bool readbackAllowed = !(spatialReadback||distanceReadback) || (capturing&&
        GetPrivateProfileIntW(L"Research", L"AmbientProbe", 0, iniPath.c_str()) == 0);
    const unsigned spatialTransactions = hudOcclusion&&!persistDistanceReadback ? 120u :
        GetPrivateProfileIntW(L"Research", distanceReadback?L"SignedDistanceReadbackCount":L"SpatialReadbackCount", 1, iniPath.c_str());
    const unsigned spatialInterval =
        GetPrivateProfileIntW(L"Research", distanceReadback?L"SignedDistanceReadbackIntervalMs":L"SpatialReadbackIntervalMs", 1000, iniPath.c_str());
    // Separate from the retained count above: this is how long the live camera
    // visibility keeps refreshing on the ambient endpoint, 0 to leave it off.
    const unsigned spatialVisibilitySeconds =
        GetPrivateProfileIntW(L"Research", L"SpatialVisibilitySeconds", 0, iniPath.c_str());
    const bool spatialStarted = spatialProbe && readbackAllowed && spatial::Start(ch::g_game.moduleBase,
        std::filesystem::path(moduleDirectory).c_str(),spatialReadback || GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0,spatialTransactions,spatialInterval,
        spatialVisibilitySeconds,distanceReadback,persistDistanceReadback,hudOcclusion || sourceVisibility);
#else
    const bool sampleAmbient = GetPrivateProfileIntW(L"Ambient", L"Enabled", 0, iniPath.c_str()) != 0;
    const bool spatialProbe = captureEnabled && (sampleAmbient || sourceVisibility);
    const bool spatialStarted = spatialProbe && capturing && spatial::Start(ch::g_game.moduleBase,
        std::filesystem::path(moduleDirectory).c_str(), sampleAmbient, 1, 1000, 0,
        sourceVisibility, false, false);
#endif
    if(spatialProbe && !spatialStarted)
        ch::Log("Spatial binding probe refused initialization; existing telemetry remains independent.");
    uint32_t reportedCaptureError = 0;
#if CDT_RESEARCH
    const bool physicsRequested = physicsVisibility || GetPrivateProfileIntW(L"Research", L"PhysicsProbe", 0, iniPath.c_str()) != 0;
#else
    const bool physicsRequested = physicsVisibility;
#endif
    const bool physicsStarted = physicsRequested && physics::Start(ch::g_game.moduleBase,
        std::filesystem::path(moduleDirectory).c_str(), physicsVisibility);
    if (physicsRequested && !physicsStarted) ch::Log("Physics observation refused initialization; no physics capture available.");
    if (physicsVisibility && !physicsStarted)
        overlay::SetLocalFault("physics-visibility", "Light visibility unavailable",
            "The validated physics hook or data bridge could not start. Raw light data is unaffected; check the native log.");
    recovery.Complete((!captureEnabled || capturing) && upstreamStarted &&
        (!ambientRequested || (spatialStarted && skyBridge && !sky::FailureCode())) &&
        (!physicsRecoveryRequested || physicsStarted));
    // Reaching this point requires either the ordinary native context checks or
    // a successful guarded recovery. Never introduce a second compatibility bypass.
    const bool simulationTickStarted = (pauseControlEnabled || (simulationTickRequested && verified && exactContexts)) &&
        simulation_tick_probe::Start(ch::g_game.moduleBase, pauseControlEnabled);
    if ((simulationTickRequested || pauseControlEnabled) && !simulationTickStarted)
        ch::Log("Simulation tick observer unavailable; validated native runtime and unique function guards are required. Acquisition continues normally.");
    while (WaitForSingleObject(stopEvent, 5) == WAIT_TIMEOUT)
    {
        if (simulationTickStarted) simulation_tick_probe::Poll();
        if(spatialStarted) spatial::Poll();
        if(physicsStarted) physics::Poll();
#if CDT_RESEARCH
        if(sourceVisibilityBridge) source_visibility::Poll();
#endif
        if (capturing)
        {
            render::PollCapture();
            const auto failure = render::CaptureFailureCode();
            if (failure && failure != reportedCaptureError)
            {
                reportedCaptureError = failure;
                overlay::SetLocalFault("native-capture", "Native light capture stopped safely",
                    "Capture error " + std::to_string(failure) +
                    ". Check the native log for device, queue or readback failure, then restart. Old samples are not current light data.");
            }
        }
    }
#if CDT_RESEARCH
    if(sourceVisibilityBridge) source_visibility::Close();
#endif
    if(simulationTickRequested) simulation_tick_probe::Stop();
    if(physicsStarted) physics::Stop();
    if(spatialStarted) spatial::Stop();
    if (capturing) render::StopCapture();
    else render::PublishStatus(render::Status::Stopped);
    // No blocking cleanup under loader lock. Stop() is on this worker.
#if CDT_RESEARCH
    if (ch::g_cfg.enableExplorer) ch::explorer::Stop();
    ch::console::UnhookWndProc();
    ch::discovery::DisarmEarlyBreakpoint();
#endif
}
void Run(HANDLE stopEvent)
{
    try { RunImpl(stopEvent); }
    catch (...)
    {
        simulation_tick_probe::Stop();
        ch::Log("Native instrumentation worker failed unexpectedly; capture cannot continue.");
        render::PublishStatus(render::Status::Fault, ERROR_UNHANDLED_EXCEPTION);
        sky::PublishStatus(render::Status::Fault, ERROR_UNHANDLED_EXCEPTION);
        overlay::SetLocalFault("native-capture", "Native telemetry worker failed",
            "Native telemetry could not continue. Check the native log and restart the game; stale samples must not be treated as current lights.");
    }
}
bool OwnsCodeAddress(uint64_t address)
{
    if (simulation_tick_probe::OwnsCodeAddress(address)) return true;
    if (physics::OwnsCodeAddress(address)) return true;
    return render::OwnsCodeAddress(address) || spatial::OwnsCodeAddress(address);
}
}
