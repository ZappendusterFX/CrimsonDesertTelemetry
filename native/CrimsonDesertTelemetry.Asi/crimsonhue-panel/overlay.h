#pragma once
#include <windows.h>
#include <chrono>
#include <string>
#include <nlohmann/json.hpp>

// This library is compiled into CDT. No graphics hooks, device ownership,
// bridge credentials or telemetry acquisition live here.
namespace crimson::overlay {
using Json = nlohmann::json;
using read_fn = int(__stdcall*)(void*, int);
using send_fn = int(__stdcall*)(const void*, int);
void RefreshBridge() noexcept; // worker only; may call the managed controller
bool TryRead(Json& view);
void Command(Json value) noexcept; // render-safe bounded queue
bool PanelAvailable() noexcept;
bool PanelVisible() noexcept;
void PollPanelShortcut() noexcept;
void PreparePanelFrame(HWND window);
void DrawPanel(); // inside CDT's ImGui frame, never calls CLR or devices
void RequestPanelStop() noexcept;
void Toggle() noexcept;
void SetVisibleForTest(bool visible) noexcept;
const char* GraphicsStatus() noexcept;
void DrawUi(const Json& view, bool& visible);
void SetInputWindow(HWND window) noexcept;
void FeedInput(HWND window, bool visible);
void ReleaseInput() noexcept;
bool HasInputFocus() noexcept;
}
