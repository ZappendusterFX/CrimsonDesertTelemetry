#pragma once
#include <windows.h>
#include <filesystem>
#include "overlay_model.h"

namespace cdt::overlay
{
// HUD, world light markers and startup notices opt in independently.
// With all three off, only a compatible optional CrimsonHue panel enables hooks/client.
Config LoadConfig(const std::filesystem::path& ini);
// Called only from a bootstrap worker, never from DllMain itself.
void Start(HMODULE module, HANDLE stopEvent, const std::filesystem::path& directory) noexcept;
bool TryRead(View& view);
void Publish(View view);
// Independent of WebSocket/host readiness. A source owns and explicitly clears
// its bounded actionable fault; notifications still respect the user's opt-out.
void SetLocalFault(std::string_view source, std::string_view title, std::string_view detail) noexcept;
void ClearLocalFault(std::string_view source) noexcept;
// Blocking receive loop for a dedicated worker; the ASI keeps it for process lifetime.
void RunClient(Config config, HANDLE stopEvent);
void DrawHud(const View& view, const Config& config, bool details);
void DrawLightOverlay(const View& view, const Config& config);
void DrawNotice(const Notice& notice, const Config& config, bool hudVisible, bool details);

// The graphics runtime is process-lifetime. No hot-unload is supported.
bool InstallGraphics(const Config& config) noexcept;
void MaintainGraphics() noexcept;
const char* GraphicsStatus() noexcept;
const char* GraphicsOutputLabel() noexcept;
unsigned long long RenderedFrames() noexcept;
void SetVisibleForTest(bool visible) noexcept;
void SetLightVisibleForTest(bool visible) noexcept;
void SetHideOccludedForTest(bool hidden) noexcept;
}
