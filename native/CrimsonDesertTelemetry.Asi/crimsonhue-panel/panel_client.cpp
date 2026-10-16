#include "overlay.h"
#include "overlay_shortcut.h"
#include <imgui.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace crimson::overlay {
namespace {
std::mutex bridgeMutex;
Json latest = Json::object();
std::deque<std::string> commands;
std::atomic<bool> available{false}, visible{true};
read_fn readState{};
send_fn sendCommand{};
using Status = int(__stdcall*)();
Status controllerStatus{};
ShortcutGate shortcutGate;
bool Discover() {
    if (readState && sendCommand) return true;
    // Discover only an already loaded optional plugin. Never load or replace it.
    const auto module = GetModuleHandleW(L"CrimsonHue.asi");
    if (!module) return false;
    const auto protocol = reinterpret_cast<Status>(GetProcAddress(module, "CrimsonHueUiProtocol"));
    if (!protocol || protocol() != 1) return false; // old independent-renderer ASIs are incompatible
    const auto read = reinterpret_cast<read_fn>(GetProcAddress(module, "CrimsonHueReadState"));
    const auto send = reinterpret_cast<send_fn>(GetProcAddress(module, "CrimsonHueSendCommand"));
    const auto status = reinterpret_cast<Status>(GetProcAddress(module, "CrimsonHueStatus"));
    HMODULE pinned{};
    if (!read || !send || !status || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(read), &pinned)) return false;
    readState = read; sendCommand = send; controllerStatus = status;
    available = true; return true;
}
}
bool PanelAvailable() noexcept { return available.load(); }
bool PanelVisible() noexcept { return available && visible; }
const char* GraphicsStatus() noexcept { return "CrimsonHue panel is rendered by CDT. Requires .NET 8 Desktop Runtime x64 for the controller."; }
void Command(Json value) noexcept {
    try {
        auto text = value.dump(); if (text.size() > 16384) return;
        std::lock_guard lock(bridgeMutex);
        const auto action = value.value("action", std::string{});
        if (action == "stop" || action == "close") {
            if (commands.size() >= 64) commands.pop_back();
            commands.push_back(std::move(text));
        } else if (commands.size() < 64) commands.push_back(std::move(text));
    } catch (...) { }
}
bool TryRead(Json& view) {
    std::unique_lock lock(bridgeMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    view = latest; return true;
}
void RefreshBridge() noexcept {
    try {
        if (!Discover()) return;
        const int status = controllerStatus();
        if (status == 2 || status < 0) {
            ReleaseInput();
            std::lock_guard lock(bridgeMutex);
            latest = {{"ready", false}, {"telemetry", status == 2 ? "CrimsonHue controller stopped." : "CrimsonHue controller failed to start. Check .NET 8 Desktop Runtime x64."}};
            return;
        }
        std::vector<char> bytes(256 * 1024);
        const int size = readState(bytes.data(), static_cast<int>(bytes.size()));
        if (size > 0 && size <= static_cast<int>(bytes.size())) {
            auto data = Json::parse(bytes.data(), bytes.data() + size);
            if (!data.is_object()) return;
            if (data.contains("shortcut")) {
                const auto& key = data.at("shortcut");
                UpdateShortcut({key.at("key").get<int>(), key.at("control").get<bool>(), key.at("shift").get<bool>(), key.at("alt").get<bool>()});
            }
            std::lock_guard lock(bridgeMutex); latest = std::move(data);
        }
        std::deque<std::string> pending;
        { std::lock_guard lock(bridgeMutex); pending.swap(commands); }
        for (const auto& command : pending) if (!sendCommand(command.data(), static_cast<int>(command.size()))) {
            const auto value = Json::parse(command); const auto action = value.value("action", std::string{});
            if (action == "stop" || action == "close") Command(value);
        }
    } catch (...) { RequestPanelStop(); }
}
void PollPanelShortcut() noexcept {
    if (!PanelAvailable()) return;
    DWORD process{}; GetWindowThreadProcessId(GetForegroundWindow(), &process);
    const auto shortcut = CurrentShortcut();
    const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    if (shortcutGate.Update(shortcut, down(shortcut.key), down(VK_CONTROL), down(VK_SHIFT), down(VK_MENU), process == GetCurrentProcessId())) Toggle();
}
void Toggle() noexcept {
    bool expected = visible.load();
    while (!visible.compare_exchange_weak(expected, !expected)) { }
    if (expected) ReleaseInput();
}
void SetVisibleForTest(bool value) noexcept { visible = value; if (!value) ReleaseInput(); }
void RequestPanelStop() noexcept { visible = false; ReleaseInput(); Command({{"action", "stop"}}); }
void PreparePanelFrame(HWND window) {
    if (!PanelAvailable()) return;
    auto& io = ImGui::GetIO();
    if (!PanelVisible()) {
        ReleaseInput();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        FeedInput(window, false);
        return;
    }
    io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    ImGui::GetIO().BackendPlatformName = "cdt_win32_with_crimsonhue";
    SetInputWindow(window); FeedInput(window, PanelVisible());
}
void DrawPanel(ImFont* font, float scale) {
    if (!PanelVisible()) return;
    // A snapshot update must not erase the panel for one frame. Keep the last
    // complete view if the worker currently owns the snapshot mutex.
    static Json view = Json::object();
    TryRead(view);
    bool open = visible;
    // The current context/device/backend belongs entirely to CDT.
    const auto savedStyle = ImGui::GetStyle();
    auto& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.WindowRounding = 12 * scale;
    style.WindowPadding = {14 * scale, 12 * scale};
    style.Colors[ImGuiCol_Text] = {228.f/255, 239.f/255, 244.f/255, 1};
    style.Colors[ImGuiCol_TextDisabled] = {174.f/255, 194.f/255, 207.f/255, .7f};
    style.Colors[ImGuiCol_WindowBg] = {9.f/255, 20.f/255, 30.f/255, .94f};
    style.Colors[ImGuiCol_Border] = {47.f/255, 74.f/255, 90.f/255, .92f};
    style.Colors[ImGuiCol_TitleBgActive] = {20.f/255, 47.f/255, 59.f/255, 1};
    style.Colors[ImGuiCol_Button] = {31.f/255, 69.f/255, 82.f/255, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {40.f/255, 100.f/255, 111.f/255, 1};
    style.Colors[ImGuiCol_TabSelected] = {23.f/255, 75.f/255, 86.f/255, 1};
    style.Colors[ImGuiCol_SliderGrab] = {49.f/255, 221.f/255, 208.f/255, 1};
    const float savedFontScale = font ? font->Scale : 1;
    if (font) { font->Scale = 16.0f * scale / font->FontSize; ImGui::PushFont(font); }
    try { DrawUi(view, open, scale); }
    catch (...) { if (font) { ImGui::PopFont(); font->Scale = savedFontScale; } style = savedStyle; throw; }
    if (font) { ImGui::PopFont(); font->Scale = savedFontScale; }
    style = savedStyle;
    // A worker shortcut can hide the panel during this frame. Publishing an
    // unchanged local 'open' would resurrect it and lose the shortcut edge.
    if (!open) visible = false;
    if (!open) ReleaseInput();
}
}
