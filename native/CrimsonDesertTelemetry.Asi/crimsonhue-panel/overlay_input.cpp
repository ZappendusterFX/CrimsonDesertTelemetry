#include "overlay.h"
#include <imgui.h>
#include <atomic>
#include <array>
#include <algorithm>
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
// The game can reposition the system cursor even while its window messages are
// captured. Keep our own cursor after a real raw-mouse packet reaches this window.
// Registration alone is not proof: buffered consumers can bypass WM_INPUT dispatch.
// Never register a device here: that would replace the game's route.
bool rawCursor = false;
ImVec2 cursorClient{};
ImVec2 clientSize{1, 1};
void ClampCursor() {
    cursorClient.x = std::clamp(cursorClient.x, 0.0f, clientSize.x - 1.0f);
    cursorClient.y = std::clamp(cursorClient.y, 0.0f, clientSize.y - 1.0f);
}
void ReadMousePacket(HWND window, LPARAM lParam) {
    RAWINPUT packet{};
    UINT size = sizeof(packet);
    const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
        &packet, &size, sizeof(RAWINPUTHEADER));
    if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) ||
        packet.header.dwType != RIM_TYPEMOUSE) return;
    const auto& mouse = packet.data.mouse;
    const bool absolute = (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    POINT position{};
    if (absolute) {
        const bool desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const int width = GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const int height = GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        position.x = MulDiv(std::clamp(mouse.lLastX, 0L, 65535L), std::max(width - 1, 0), 65535) +
            (desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0);
        position.y = MulDiv(std::clamp(mouse.lLastY, 0L, 65535L), std::max(height - 1, 0), 65535) +
            (desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0);
        if (!ScreenToClient(window, &position)) return;
    }
    std::lock_guard lock(inputMutex);
    if (!capture || window != activeWindow) return;
    rawCursor = true; // Also handles a game registration changed after opening.
    if (absolute) cursorClient = {static_cast<float>(position.x), static_cast<float>(position.y)};
    else { cursorClient.x += static_cast<float>(mouse.lLastX); cursorClient.y += static_cast<float>(mouse.lLastY); }
    ClampCursor();
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool owns = capture && window == activeWindow && GetForegroundWindow() == GetAncestor(window, GA_ROOT);
    if (owns && message == WM_INPUT) ReadMousePacket(window, lParam);
    WNDPROC original{};
    {
        std::lock_guard lock(inputMutex);
        if (const auto found = originals.find(window); found != originals.end()) original = found->second;
        if (owns && capture && window == activeWindow) {
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
    RECT clip{}; bool restore = false; HWND window{};
    {
        std::lock_guard lock(inputMutex);
        restore = clipReleased; clip = previousClip; window = activeWindow;
        clipReleased = false;
        rawCursor = false;
        characters.clear(); wheel = horizontalWheel = 0;
    }
    // User32 calls can dispatch messages. Never hold our WndProc's mutex here.
    if (restore && GetForegroundWindow() == GetAncestor(window, GA_ROOT)) ClipCursor(&clip);
}
void SetInputWindow(HWND window) noexcept {
    // Called every rendered frame. Releasing capture here used to create a small
    // per-frame window in which game mouse/keyboard messages slipped through.
    if (activeWindow == window) return;
    if (activeWindow != window) ReleaseInput();
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
    auto& io = ImGui::GetIO();
    io.MouseDrawCursor = owns;
    io.AddFocusEvent(owns);
    if (owns) {
        const bool opening = !capture;
        RECT bounds{};
        const bool haveBounds = GetClientRect(window, &bounds) != FALSE;
        POINT mouse{};
        bool needsSystemPosition = opening;
        { std::lock_guard lock(inputMutex); needsSystemPosition |= !rawCursor; }
        const bool havePosition = needsSystemPosition && GetCursorPos(&mouse) && ScreenToClient(window, &mouse);
        RECT clip{};
        const bool haveClip = opening && GetClipCursor(&clip);
        ImVec2 position{};
        {
            std::lock_guard lock(inputMutex);
            if (haveBounds) clientSize = {static_cast<float>(std::max(bounds.right - bounds.left, 1L)),
                static_cast<float>(std::max(bounds.bottom - bounds.top, 1L))};
            if (opening) {
                rawCursor = false;
                cursorClient = havePosition ? ImVec2(static_cast<float>(mouse.x), static_cast<float>(mouse.y)) :
                    ImVec2(clientSize.x * 0.5f, clientSize.y * 0.5f);
                if (haveClip) { previousClip = clip; clipReleased = true; }
            } else if (!rawCursor && havePosition) cursorClient = {static_cast<float>(mouse.x), static_cast<float>(mouse.y)};
            ClampCursor();
            position = {cursorClient.x * io.DisplaySize.x / clientSize.x, cursorClient.y * io.DisplaySize.y / clientSize.y};
            capture = true;
        }
        ClipCursor(nullptr);
        io.AddMousePosEvent(position.x, position.y);
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
