#include "overlay.h"
#include <imgui.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
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
HCURSOR previousCursor{};
struct Presses {
    std::bitset<256> legacy, raw;
    bool Any() const { return legacy.any() || raw.any(); }
    void Clear() { legacy.reset(); raw.reset(); }
};
Presses gamePresses, panelPresses;
std::bitset<256> releasePhysical;
bool drainingPanel = false;
std::uint64_t inputEpoch = 0;
std::atomic<bool> waitingForRelease{false}, resetUiInput{false};

std::bitset<256> PhysicalPresses() {
    std::bitset<256> result;
    for (int key = 1; key < 255; ++key) {
        // Track the two physical modifiers, not their duplicate aggregate VK.
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) continue;
        if (GetAsyncKeyState(key) & 0x8000) result.set(key);
    }
    return result;
}
int KeyboardKey(int key, unsigned scan, bool extended) {
    if (key == VK_SHIFT) return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    if (key == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (key == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    return key;
}
void Press(Presses& state, bool raw, int key, bool down) {
    if (key <= 0 || key >= 255) return;
    (raw ? state.raw : state.legacy).set(key, down);
    if (&state == &panelPresses && !down) releasePhysical.reset(key);
}
void ObserveLegacy(Presses& state, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: Press(state, false, VK_LBUTTON, true); break;
    case WM_LBUTTONUP: Press(state, false, VK_LBUTTON, false); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: Press(state, false, VK_RBUTTON, true); break;
    case WM_RBUTTONUP: Press(state, false, VK_RBUTTON, false); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: Press(state, false, VK_MBUTTON, true); break;
    case WM_MBUTTONUP: Press(state, false, VK_MBUTTON, false); break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        Press(state, false, HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, message != WM_XBUTTONUP); break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
        Press(state, false, KeyboardKey(static_cast<int>(wParam), static_cast<unsigned>((static_cast<UINT_PTR>(lParam) >> 16) & 0xff),
            (lParam & (static_cast<LPARAM>(1) << 24)) != 0), message == WM_KEYDOWN || message == WM_SYSKEYDOWN); break;
    }
}
bool ReadPacket(LPARAM lParam, RAWINPUT& packet) {
    UINT size = sizeof(packet);
    const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
        &packet, &size, sizeof(RAWINPUTHEADER));
    if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER)) return false;
    if (packet.header.dwType == RIM_TYPEMOUSE) return read >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE);
    if (packet.header.dwType == RIM_TYPEKEYBOARD) return read >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD);
    return false;
}
void ObserveRaw(Presses& state, const RAWINPUT& packet) {
    if (packet.header.dwType == RIM_TYPEKEYBOARD) {
        const auto& key = packet.data.keyboard;
        Press(state, true, KeyboardKey(key.VKey, key.MakeCode, (key.Flags & RI_KEY_E0) != 0), (key.Flags & RI_KEY_BREAK) == 0);
    } else if (packet.header.dwType == RIM_TYPEMOUSE) {
        const std::array<int, 5> keys{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        for (unsigned index = 0; index < keys.size(); ++index) {
            if (packet.data.mouse.usButtonFlags & (1u << (index * 2))) Press(state, true, keys[index], true);
            if (packet.data.mouse.usButtonFlags & (2u << (index * 2))) Press(state, true, keys[index], false);
        }
    }
}
void FinishPanelDrain() {
    if (!panelPresses.Any() && releasePhysical.none()) drainingPanel = false;
}
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
void ReadMousePacket(HWND window, const RAWMOUSE& mouse) {
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
bool NativeCursorInClient(HWND window, CURSORINFO& cursor) {
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor)) return false;
    auto point = cursor.ptScreenPos;
    RECT client{};
    return ScreenToClient(window, &point) && GetClientRect(window, &client) &&
        point.x >= client.left && point.x < client.right && point.y >= client.top && point.y < client.bottom;
}
void RestoreNativeCursor(HWND window, HCURSOR previous) {
    CURSORINFO cursor{};
    if (previous && GetForegroundWindow() == GetAncestor(window, GA_ROOT) &&
        NativeCursorInClient(window, cursor) && !cursor.hCursor) SetCursor(previous);
}
bool HideNativeCursor(HWND window) {
    CURSORINFO cursor{};
    if (!capture || activeWindow != window || GetForegroundWindow() != GetAncestor(window, GA_ROOT) ||
        !NativeCursorInClient(window, cursor)) return false;
    // The raw HUD cursor can differ from the OS position. Show only the former.
    // Borrow the old shape; never change ShowCursor counters or cursor position.
    const auto previous = SetCursor(nullptr);
    bool owned = false;
    {
        std::lock_guard lock(inputMutex);
        owned = capture && activeWindow == window;
        if (owned && previous) previousCursor = previous;
    }
    // A close/focus transition may run while User32 is executing. Undo a hide
    // which no longer belongs to us, without replacing another visible cursor.
    if (!owned) RestoreNativeCursor(window, previous);
    return owned;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool foreground = window == activeWindow && GetForegroundWindow() == GetAncestor(window, GA_ROOT);
    const bool focusLost = message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wParam);
    if (focusLost && window == activeWindow) ReleaseInput();
    if (foreground && message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT &&
        reinterpret_cast<HWND>(wParam) == window && HideNativeCursor(window)) return TRUE;
    RAWINPUT packet{};
    const bool havePacket = foreground && message == WM_INPUT && ReadPacket(lParam, packet);
    if (foreground && capture && havePacket && packet.header.dwType == RIM_TYPEMOUSE)
        ReadMousePacket(window, packet.data.mouse);
    WNDPROC original{};
    bool block = false;
    {
        std::lock_guard lock(inputMutex);
        if (const auto found = originals.find(window); found != originals.end()) original = found->second;
        if (focusLost && window == activeWindow) {
            gamePresses.Clear(); panelPresses.Clear(); releasePhysical.reset(); drainingPanel = false;
            waitingForRelease = false;
        } else if (foreground && window == activeWindow) {
            // Freeze routing for this message. A render/shortcut transition must
            // not split a press/release between the two receivers mid-dispatch.
            block = capture || drainingPanel;
            auto& receiver = block ? panelPresses : gamePresses;
            ObserveLegacy(receiver, message, wParam, lParam);
            if (havePacket) ObserveRaw(receiver, packet);
            if (drainingPanel) FinishPanelDrain();
        }
        if (block && capture) {
            if (message == WM_CHAR && characters.size() < 256) characters.push_back(static_cast<unsigned short>(wParam));
            if (message == WM_MOUSEWHEEL) wheel += static_cast<short>(HIWORD(wParam)) / 120.0f;
            if (message == WM_MOUSEHWHEEL) horizontalWheel += static_cast<short>(HIWORD(wParam)) / 120.0f;
        }
    }
    if (block) {
        if (message == WM_INPUT) return DefWindowProcW(window, message, wParam, lParam);
        if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_CHAR || message == WM_KEYDOWN ||
            message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) return 0;
    }
    return original ? CallWindowProcW(original, window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}
}
bool HasInputFocus() noexcept { return capture.load(); }
bool InputHandoffPending() noexcept { return waitingForRelease.load(); }
void MaintainInputHandoff() noexcept {
    const auto window = activeWindow.load();
    if (!window) return;
    if (GetForegroundWindow() != GetAncestor(window, GA_ROOT)) {
        ReleaseInput();
        std::lock_guard lock(inputMutex);
        gamePresses.Clear(); panelPresses.Clear(); releasePhysical.reset(); drainingPanel = false;
        waitingForRelease = false;
        return;
    }
    { std::lock_guard lock(inputMutex); if (!drainingPanel) return; }
    const auto physical = PhysicalPresses();
    std::lock_guard lock(inputMutex);
    releasePhysical &= physical;
    FinishPanelDrain();
}
void ReleaseInput() noexcept {
    // Poll before the mutex while capture still protects in-flight panel input.
    const auto physical = capture ? PhysicalPresses() : std::bitset<256>{};
    RECT clip{}; bool restore = false; HWND window{}; HCURSOR cursor{};
    {
        std::lock_guard lock(inputMutex);
        if (capture) {
            releasePhysical = physical;
            drainingPanel = panelPresses.Any() || physical.any();
        }
        capture = false; ++inputEpoch; resetUiInput = true; waitingForRelease = false;
        restore = clipReleased; clip = previousClip; window = activeWindow;
        clipReleased = false;
        cursor = previousCursor; previousCursor = nullptr;
        rawCursor = false;
        characters.clear(); wheel = horizontalWheel = 0;
    }
    // User32 calls can dispatch messages. Never hold our WndProc's mutex here.
    if (restore && GetForegroundWindow() == GetAncestor(window, GA_ROOT)) ClipCursor(&clip);
    RestoreNativeCursor(window, cursor);
}
void SetInputWindow(HWND window) noexcept {
    // Called every rendered frame. Releasing capture here used to create a small
    // per-frame window in which game mouse/keyboard messages slipped through.
    if (activeWindow == window) return;
    if (activeWindow != window) ReleaseInput();
    std::lock_guard lock(inputMutex);
    capture = false; activeWindow = window;
    gamePresses.Clear(); panelPresses.Clear(); releasePhysical.reset(); drainingPanel = false;
    if (originals.contains(window)) return;
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc));
    if (previous) originals.emplace(window, reinterpret_cast<WNDPROC>(previous));
}
void FeedInput(HWND window, bool visible) {
    MaintainInputHandoff();
    const bool foreground = GetForegroundWindow() == GetAncestor(window, GA_ROOT);
    const bool requested = visible && foreground;
    if (!requested) ReleaseInput();
    auto& io = ImGui::GetIO();
    if (resetUiInput.exchange(false)) { io.ClearEventsQueue(); io.ClearInputKeys(); io.AddFocusEvent(false); }
    bool owns = false;
    ImVec2 position{};
    bool eligible = false;
    std::uint64_t epoch{};
    if (requested) {
        const auto physical = capture ? std::bitset<256>{} : PhysicalPresses();
        std::lock_guard lock(inputMutex);
        eligible = !drainingPanel && (capture || (!gamePresses.Any() && physical.none()));
        epoch = inputEpoch;
    }
    if (eligible) {
        const bool opening = !capture;
        RECT bounds{};
        const bool haveBounds = GetClientRect(window, &bounds) != FALSE;
        POINT mouse{};
        bool needsSystemPosition = opening;
        { std::lock_guard lock(inputMutex); needsSystemPosition |= !rawCursor; }
        const bool havePosition = needsSystemPosition && GetCursorPos(&mouse) && ScreenToClient(window, &mouse);
        RECT clip{};
        const bool haveClip = opening && GetClipCursor(&clip);
        {
            std::lock_guard lock(inputMutex);
            owns = epoch == inputEpoch && !drainingPanel && (capture || !gamePresses.Any());
            if (owns) {
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
        }
        if (owns) ClipCursor(nullptr);
    }
    waitingForRelease = requested && !owns;
    io.MouseDrawCursor = owns;
    if (owns) HideNativeCursor(window);
    io.AddFocusEvent(owns);
    if (owns) io.AddMousePosEvent(position.x, position.y);
    else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
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
