#include "overlay.h"
#include <imgui.h>
#include <atomic>
#include <array>
#include <map>
#include <mutex>
#include <vector>

namespace crimson::overlay {
namespace {
std::atomic<bool> capture{false};
std::mutex inputMutex;
std::map<HWND, WNDPROC> originals;
std::vector<unsigned short> characters;
float wheel = 0, horizontalWheel = 0;
std::atomic<HWND> activeWindow{nullptr};
RECT previousClip{};
bool clipReleased = false;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    WNDPROC original{};
    {
        std::lock_guard lock(inputMutex);
        if (const auto found = originals.find(window); found != originals.end()) original = found->second;
        if (capture && window == activeWindow) {
            if (message == WM_CHAR && characters.size() < 256) characters.push_back(static_cast<unsigned short>(wParam));
            if (message == WM_MOUSEWHEEL) wheel += static_cast<short>(HIWORD(wParam)) / 120.0f;
            if (message == WM_MOUSEHWHEEL) horizontalWheel += static_cast<short>(HIWORD(wParam)) / 120.0f;
        }
    }
    if (capture && window == activeWindow && GetForegroundWindow() == GetAncestor(window, GA_ROOT)) {
        if (message == WM_INPUT) return DefWindowProcW(window, message, wParam, lParam);
        if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_CHAR || message == WM_KEYDOWN ||
            message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) return 0;
    }
    return original ? CallWindowProcW(original, window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}
}
bool HasInputFocus() noexcept { return capture.load(); }
void ReleaseInput() noexcept {
    capture = false;
    std::lock_guard lock(inputMutex);
    if (clipReleased && GetForegroundWindow() == GetAncestor(activeWindow.load(), GA_ROOT)) ClipCursor(&previousClip);
    clipReleased = false;
}
void SetInputWindow(HWND window) noexcept {
    std::lock_guard lock(inputMutex);
    capture = false; activeWindow = window;
    if (originals.contains(window)) return;
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc));
    if (previous) originals.emplace(window, reinterpret_cast<WNDPROC>(previous));
}
void FeedInput(HWND window, bool visible) {
    const bool foreground = GetForegroundWindow() == GetAncestor(window, GA_ROOT);
    const bool owns = visible && foreground;
    if (!owns) ReleaseInput();
    capture = owns;
    auto& io = ImGui::GetIO();
    io.MouseDrawCursor = owns;
    io.AddFocusEvent(foreground);
    POINT mouse{};
    if (owns && GetCursorPos(&mouse) && ScreenToClient(window, &mouse)) {
        { std::lock_guard lock(inputMutex); if (!clipReleased) { GetClipCursor(&previousClip); clipReleased = true; } }
        ClipCursor(nullptr);
        io.AddMousePosEvent(static_cast<float>(mouse.x), static_cast<float>(mouse.y));
    } else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    const std::array<int, 5> mouseKeys{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (int index = 0; index < 5; ++index) io.AddMouseButtonEvent(index, owns && (GetAsyncKeyState(mouseKeys[index]) & 0x8000));
    const std::pair<int, ImGuiKey> keys[]{
        {VK_TAB, ImGuiKey_Tab}, {VK_LEFT, ImGuiKey_LeftArrow}, {VK_RIGHT, ImGuiKey_RightArrow},
        {VK_UP, ImGuiKey_UpArrow}, {VK_DOWN, ImGuiKey_DownArrow}, {VK_PRIOR, ImGuiKey_PageUp},
        {VK_NEXT, ImGuiKey_PageDown}, {VK_HOME, ImGuiKey_Home}, {VK_END, ImGuiKey_End},
        {VK_INSERT, ImGuiKey_Insert}, {VK_DELETE, ImGuiKey_Delete}, {VK_BACK, ImGuiKey_Backspace},
        {VK_SPACE, ImGuiKey_Space}, {VK_RETURN, ImGuiKey_Enter}, {VK_ESCAPE, ImGuiKey_Escape},
        {'A', ImGuiKey_A}, {'C', ImGuiKey_C}, {'V', ImGuiKey_V}, {'X', ImGuiKey_X}, {'Y', ImGuiKey_Y}, {'Z', ImGuiKey_Z},
        {VK_CONTROL, ImGuiMod_Ctrl}, {VK_SHIFT, ImGuiMod_Shift}, {VK_MENU, ImGuiMod_Alt}, {VK_LWIN, ImGuiMod_Super}};
    for (const auto& key : keys) io.AddKeyEvent(key.second, owns && (GetAsyncKeyState(key.first) & 0x8000));
    std::lock_guard lock(inputMutex);
    if (owns) { for (const auto value : characters) io.AddInputCharacterUTF16(value); io.AddMouseWheelEvent(horizontalWheel, wheel); }
    characters.clear(); wheel = horizontalWheel = 0;
}
}
