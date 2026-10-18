#include <windows.h>
#include <imgui.h>
#include <cmath>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Only this test translation unit replaces User32 calls. It cannot steal the
// owner's foreground window, move/clip the real cursor or inject real input.
namespace {
HWND testForegroundWindow = reinterpret_cast<HWND>(1);
WNDPROC installed{};
RECT clipping{10, 20, 300, 400};
int forwarded{}, clips{};
bool reenterClip{};
POINT osPosition{150, 150}, clientOrigin{};
RECT clientRect{0, 0, 800, 600};
RAWINPUT rawPacket{};
UINT rawReadBytes = sizeof(RAWINPUT);
int cursorReads{}, rawReads{}, registrationWrites{};
HCURSOR nativeCursor = reinterpret_cast<HCURSOR>(0x44);
int cursorShapeWrites{};
bool reenterCursor{};
std::array<bool, 256> physicalKeys{};
bool gameAttackLegacy{}, gameAttackRaw{};
LRESULT CALLBACK Original(HWND, UINT message, WPARAM, LPARAM) {
    ++forwarded;
    if (message == WM_LBUTTONDOWN) gameAttackLegacy = true;
    if (message == WM_LBUTTONUP) gameAttackLegacy = false;
    if (message == WM_INPUT && rawPacket.header.dwType == RIM_TYPEMOUSE) {
        if (rawPacket.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) gameAttackRaw = true;
        if (rawPacket.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) gameAttackRaw = false;
    }
    if (message == WM_KILLFOCUS) gameAttackLegacy = gameAttackRaw = false;
    return 701;
}
HWND TestForeground() { return testForegroundWindow; }
HWND TestAncestor(HWND window, UINT) { return window; }
BOOL TestClip(const RECT* value) {
    ++clips; clipping = value ? *value : RECT{0, 0, 1920, 1080};
    if (reenterClip && installed) installed(reinterpret_cast<HWND>(1), WM_MOUSEMOVE, 0, 0);
    return TRUE;
}
BOOL TestGetClip(RECT* value) { *value = clipping; return TRUE; }
BOOL TestPosition(POINT* value) { ++cursorReads; *value = osPosition; return TRUE; }
BOOL TestCursorInfo(CURSORINFO* value) {
    value->hCursor = nativeCursor; value->ptScreenPos = osPosition;
    value->flags = nativeCursor ? CURSOR_SHOWING : 0; return TRUE;
}
HCURSOR TestSetCursor(HCURSOR value) {
    ++cursorShapeWrites; const auto previous = nativeCursor; nativeCursor = value;
    if (reenterCursor && installed) installed(reinterpret_cast<HWND>(1), WM_MOUSEMOVE, 0, 0);
    return previous;
}
BOOL TestScreenToClient(HWND, POINT* value) { value->x -= clientOrigin.x; value->y -= clientOrigin.y; return TRUE; }
BOOL TestClientRect(HWND, RECT* value) { *value = clientRect; return TRUE; }
int TestMetrics(int index) {
    switch (index) {
    case SM_CXSCREEN: return 1920;
    case SM_CYSCREEN: return 1080;
    case SM_XVIRTUALSCREEN: return -1920;
    case SM_YVIRTUALSCREEN: return -200;
    case SM_CXVIRTUALSCREEN: return 3840;
    case SM_CYVIRTUALSCREEN: return 1280;
    default: return 0;
    }
}
UINT TestRawData(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    ++rawReads;
    if (handle != reinterpret_cast<HRAWINPUT>(0x123) || command != RID_INPUT || !size || headerSize != sizeof(RAWINPUTHEADER))
        return static_cast<UINT>(-1);
    if (!data) { *size = sizeof(RAWINPUT); return 0; }
    if (*size < sizeof(RAWINPUT)) { *size = sizeof(RAWINPUT); return static_cast<UINT>(-1); }
    std::memcpy(data, &rawPacket, sizeof(rawPacket)); *size = sizeof(rawPacket); return rawReadBytes;
}
[[maybe_unused]] BOOL TestRegister(PCRAWINPUTDEVICE, UINT, UINT) { ++registrationWrites; return FALSE; }
SHORT TestKey(int key) {
    if (key == VK_CONTROL) return physicalKeys[VK_LCONTROL] || physicalKeys[VK_RCONTROL] ? static_cast<SHORT>(0x8000) : 0;
    if (key == VK_SHIFT) return physicalKeys[VK_LSHIFT] || physicalKeys[VK_RSHIFT] ? static_cast<SHORT>(0x8000) : 0;
    if (key == VK_MENU) return physicalKeys[VK_LMENU] || physicalKeys[VK_RMENU] ? static_cast<SHORT>(0x8000) : 0;
    return key > 0 && key < 256 && physicalKeys[key] ? static_cast<SHORT>(0x8000) : 0;
}
LONG_PTR TestSubclass(HWND, int, LONG_PTR next) {
    const auto previous = installed ? installed : Original;
    installed = reinterpret_cast<WNDPROC>(next); return reinterpret_cast<LONG_PTR>(previous);
}
LRESULT TestCall(WNDPROC previous, HWND window, UINT message, WPARAM w, LPARAM l) { return previous(window, message, w, l); }
LRESULT TestDefault(HWND, UINT, WPARAM, LPARAM) { return 902; }
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool SameRect(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
void FrameAt(float x, float y, const char* message) {
    ImGui::NewFrame();
    const auto actual = ImGui::GetIO().MousePos;
    ImGui::EndFrame();
    Require(std::abs(actual.x - x) < 0.01f && std::abs(actual.y - y) < 0.01f, message);
}
void SendRaw(LONG x, LONG y, USHORT flags = MOUSE_MOVE_RELATIVE) {
    rawPacket = {}; rawPacket.header.dwType = RIM_TYPEMOUSE; rawPacket.header.dwSize = sizeof(rawPacket);
    rawPacket.data.mouse.usFlags = flags; rawPacket.data.mouse.lLastX = x; rawPacket.data.mouse.lLastY = y;
    Require(installed(reinterpret_cast<HWND>(1), WM_INPUT, 0, reinterpret_cast<LPARAM>(reinterpret_cast<HRAWINPUT>(0x123))) == 902,
        "Captured raw packet was not cleaned up through DefWindowProc");
}
LRESULT RawButtons(USHORT flags, LONG x = 0, LONG y = 0) {
    rawPacket = {}; rawPacket.header.dwType = RIM_TYPEMOUSE; rawPacket.header.dwSize = sizeof(rawPacket);
    rawPacket.data.mouse.usButtonFlags = flags; rawPacket.data.mouse.lLastX = x; rawPacket.data.mouse.lLastY = y;
    return installed(reinterpret_cast<HWND>(1), WM_INPUT, 0, 0x123);
}
LRESULT RawKey(USHORT key, USHORT flags, USHORT scan = 0) {
    rawPacket = {}; rawPacket.header.dwType = RIM_TYPEKEYBOARD; rawPacket.header.dwSize = sizeof(rawPacket);
    rawPacket.data.keyboard.VKey = key; rawPacket.data.keyboard.Flags = flags; rawPacket.data.keyboard.MakeCode = scan;
    return installed(reinterpret_cast<HWND>(1), WM_INPUT, 0, 0x123);
}
}
#define GetForegroundWindow TestForeground
#define GetAncestor TestAncestor
#define ClipCursor TestClip
#define GetClipCursor TestGetClip
#define GetCursorPos TestPosition
#define GetCursorInfo TestCursorInfo
#define SetCursor TestSetCursor
#define ScreenToClient TestScreenToClient
#define GetClientRect TestClientRect
#define GetSystemMetrics TestMetrics
#define GetRawInputData TestRawData
#define RegisterRawInputDevices TestRegister
#define GetAsyncKeyState TestKey
#define SetWindowLongPtrW TestSubclass
#define CallWindowProcW TestCall
#define DefWindowProcW TestDefault
#include "../crimsonhue-panel/overlay_input.cpp"
#undef GetForegroundWindow
#undef GetAncestor
#undef ClipCursor
#undef GetClipCursor
#undef GetCursorPos
#undef GetCursorInfo
#undef SetCursor
#undef ScreenToClient
#undef GetClientRect
#undef GetSystemMetrics
#undef GetRawInputData
#undef RegisterRawInputDevices
#undef GetAsyncKeyState
#undef SetWindowLongPtrW
#undef CallWindowProcW
#undef DefWindowProcW

int main() {
    using namespace crimson::overlay;
    ImGui::CreateContext();
    try {
        const auto window = reinterpret_cast<HWND>(1);
        const RECT originalClip = clipping;
        auto& io = ImGui::GetIO(); io.DisplaySize = {800, 600};
        io.ConfigInputTrickleEventQueue = false;
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        unsigned char* pixels{}; int width{}, height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        SetInputWindow(window); reenterCursor = true; FeedInput(window, true); reenterCursor = false;
        Require(!nativeCursor && io.MouseDrawCursor, "Owned HUD left both the native and software cursors visible");
        nativeCursor = reinterpret_cast<HCURSOR>(0x55);
        Require(installed(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == TRUE &&
            !nativeCursor, "Client WM_SETCURSOR restored a duplicate native cursor");
        Require(installed(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window), MAKELPARAM(HTCAPTION, WM_MOUSEMOVE)) == 701,
            "HUD swallowed non-client cursor selection");
        forwarded = 0;
        FrameAt(150, 150, "Legacy cursor did not use OS position");
        osPosition = {175, 225}; FeedInput(window, true);
        FrameAt(175, 225, "Legacy cursor stopped following OS position without raw registration");
        Require(HasInputFocus() && io.MouseDrawCursor, "Visible foreground panel did not own input");
        Require(installed(window, WM_KEYDOWN, 'W', 0) == 0 && forwarded == 0, "Open panel leaked keyboard input");
        Require(installed(window, WM_KEYUP, 'W', 0) == 0 && forwarded == 0, "Open panel leaked its key release");
        Require(installed(window, WM_MOUSEMOVE, 0, 0) == 0 && forwarded == 0, "Open panel leaked mouse input");
        Require(installed(window, WM_INPUT, 0, 0) == 902, "Captured raw input was not cleaned up");
        installed(window, WM_CHAR, 'x', 0);
        const int beforeRelease = clips;
        reenterClip = reenterCursor = true; // Reentrant User32 dispatch must not deadlock our input mutex.
        ReleaseInput();
        reenterClip = reenterCursor = false;
        Require(!HasInputFocus() && SameRect(clipping, originalClip), "Hide did not release capture and restore game clipping");
        Require(clips == beforeRelease + 1, "Hide restored clipping more than once");
        Require(nativeCursor == reinterpret_cast<HCURSOR>(0x55), "Hide did not restore the last suppressed cursor shape");
        Require(installed(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == 701,
            "Hidden HUD swallowed native cursor selection");
        Require(installed(window, WM_KEYDOWN, 'W', 0) == 701, "Hidden panel swallowed keyboard input");
        Require(installed(window, WM_KEYUP, 'W', 0) == 701, "Hidden panel swallowed key release");
        Require(installed(window, WM_MOUSEMOVE, 0, 0) == 701, "Hidden panel swallowed mouse input");
        Require(installed(window, WM_INPUT, 0, 0) == 701, "Hidden panel swallowed raw game input");
        FeedInput(window, false);
        Require(!HasInputFocus() && !io.MouseDrawCursor && clips == beforeRelease + 1, "Hidden frame recaptured input or cursor");
        ImGui::NewFrame();
        Require(io.InputQueueCharacters.empty(), "Text captured before Hide leaked into the next frame");
        ImGui::EndFrame();
        FeedInput(window, true); Require(HasInputFocus(), "Reopen failed to acquire input");
        testForegroundWindow = reinterpret_cast<HWND>(2);
        nativeCursor = reinterpret_cast<HCURSOR>(0x99);
        const int beforeForeignShape = cursorShapeWrites;
        const int beforeBackground = clips;
        FeedInput(window, true);
        Require(!HasInputFocus() && !io.MouseDrawCursor, "Background panel retained capture");
        Require(clips == beforeBackground, "Background release clipped another app's cursor");
        Require(nativeCursor == reinterpret_cast<HCURSOR>(0x99) && cursorShapeWrites == beforeForeignShape,
            "Background release changed another app's cursor shape");
        Require(installed(window, WM_KEYDOWN, 'A', 0) == 701, "Background panel swallowed input");
        testForegroundWindow = window; FeedInput(window, true); ReleaseInput(); ReleaseInput();
        Require(!HasInputFocus(), "Repeated Close/Stop retained capture");

        FeedInput(window, true);
        nativeCursor = reinterpret_cast<HCURSOR>(0x66); // Game chose a newer shape before Hide.
        ReleaseInput();
        Require(nativeCursor == reinterpret_cast<HCURSOR>(0x66), "Hide overwrote a newer visible game cursor");
        osPosition = {900, 700};
        FeedInput(window, true);
        Require(nativeCursor == reinterpret_cast<HCURSOR>(0x66), "HUD hid a cursor outside its client area");
        ReleaseInput(); osPosition = {150, 150};

        // A delivered raw mouse packet enables the virtual cursor. Registration
        // alone would not prove WM_INPUT reaches this window (buffered input).
        // The panel must never change registration or write system cursor position.
        osPosition = {150, 150};
        const int readsBeforeOpen = cursorReads;
        FeedInput(window, true); FrameAt(150, 150, "Raw cursor was not seeded from OS position on open");
        Require(cursorReads == readsBeforeOpen + 1, "Raw cursor did not seed exactly once");
        SendRaw(0, 0);
        osPosition = {0, 0};
        for (int iteration = 0; iteration < 4; ++iteration) {
            SetInputWindow(window);
            Require(HasInputFocus(), "Same-window frame reset input ownership");
            FeedInput(window, true); FrameAt(150, 150, "Game cursor warp moved the raw virtual cursor");
        }
        Require(cursorReads == readsBeforeOpen + 1, "Raw capture repeatedly sampled a game-warped OS cursor");
        SendRaw(17, -9); FeedInput(window, true); FrameAt(167, 141, "Raw relative movement was not integrated");
        FeedInput(window, true); FrameAt(167, 141, "Raw relative movement was applied twice");
        SendRaw(11, -4); SendRaw(-11, 4); FeedInput(window, true);
        FrameAt(167, 141, "Opposing raw deltas did not cancel before the next frame");
        SendRaw(10000, -10000); FeedInput(window, true); FrameAt(799, 0, "Raw cursor was not clamped to the client extent");
        SendRaw(-10000, 10000); FeedInput(window, true); FrameAt(0, 599, "Raw cursor crossed the opposite client extent");

        // Client coordinates and ImGui render coordinates may differ under DPI
        // scaling. Scale once, after integrating in client pixels.
        ReleaseInput(); osPosition = {120, 80}; io.DisplaySize = {1600, 1200};
        FeedInput(window, true); FrameAt(240, 160, "Seeded client cursor did not map to ImGui display size");
        SendRaw(5, 7); FeedInput(window, true); FrameAt(250, 174, "Raw delta was not scaled once from client pixels");
        io.DisplaySize = {800, 600};
        ReleaseInput(); osPosition = {150, 150}; FeedInput(window, true); FrameAt(150, 150, "Raw reopen seed failed");

        // Absolute devices report normalized screen coordinates. A desktop to
        // the left/above the primary monitor must keep its signed origin.
        clientOrigin = {-1920, -200};
        SendRaw(0, 0, MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP); FeedInput(window, true);
        FrameAt(0, 0, "Absolute virtual-desktop input lost its negative screen origin");
        SendRaw(65535, 65535, MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP); FeedInput(window, true);
        FrameAt(799, 599, "Absolute virtual-desktop endpoint was not clamped in client coordinates");
        clientOrigin = {-100, -50};
        SendRaw(0, 0, MOUSE_MOVE_ABSOLUTE); FeedInput(window, true);
        FrameAt(100, 50, "Primary absolute input incorrectly used virtual-desktop coordinates");
        clientOrigin = {};
        installed(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        installed(window, WM_MOUSEHWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        FeedInput(window, true); ImGui::NewFrame();
        Require(io.MouseWheel == 1 && io.MouseWheelH == -1, "Raw cursor changed wheel delivery");
        ImGui::EndFrame();

        SendRaw(400, 300); ReleaseInput(); osPosition = {80, 90};
        FeedInput(window, true); FrameAt(80, 90, "Hide retained old raw movement on reopen");
        const int readsBeforeBackground = rawReads;
        testForegroundWindow = reinterpret_cast<HWND>(2);
        Require(installed(window, WM_INPUT, 0, reinterpret_cast<LPARAM>(reinterpret_cast<HRAWINPUT>(0x123))) == 701,
            "Background panel swallowed raw input before its next frame");
        Require(rawReads == readsBeforeBackground, "Background panel consumed raw input before focus release");
        FeedInput(window, true); Require(!HasInputFocus(), "Raw capture survived focus loss");
        testForegroundWindow = window; osPosition = {90, 100}; FeedInput(window, true);
        FrameAt(90, 100, "Focus return reused stale raw cursor coordinates");
        ReleaseInput();

        // No packets, malformed reads, keyboard packets and truncated mouse
        // packets must keep the working legacy fallback instead of freezing it.
        osPosition = {210, 220}; FeedInput(window, true);
        FrameAt(210, 220, "Reopen did not return to legacy fallback before receiving raw input");
        osPosition = {30, 40}; FeedInput(window, true); FrameAt(30, 40, "No-packet fallback stopped following OS cursor");
        Require(installed(window, WM_INPUT, 0, 0) == 902, "Invalid raw input was not cleaned up");
        osPosition = {40, 50}; FeedInput(window, true); FrameAt(40, 50, "Invalid raw read enabled virtual cursor");
        rawPacket = {}; rawPacket.header.dwType = RIM_TYPEKEYBOARD; rawPacket.header.dwSize = sizeof(rawPacket);
        Require(installed(window, WM_INPUT, 0, reinterpret_cast<LPARAM>(reinterpret_cast<HRAWINPUT>(0x123))) == 902,
            "Keyboard raw packet was not cleaned up");
        osPosition = {50, 60}; FeedInput(window, true); FrameAt(50, 60, "Keyboard raw packet enabled virtual cursor");
        rawReadBytes = sizeof(RAWINPUTHEADER);
        SendRaw(500, 500); osPosition = {70, 80}; FeedInput(window, true);
        FrameAt(70, 80, "Truncated mouse packet enabled virtual cursor or applied partial movement");
        rawReadBytes = sizeof(RAWINPUT);
        SendRaw(0, 0); osPosition = {0, 0}; FeedInput(window, true);
        FrameAt(70, 80, "First valid mouse packet failed to latch the last legacy position");
        ReleaseInput();

        // Reproduce a game attack already down when the HUD opens. Physical up
        // alone is insufficient: its queued game release still has to arrive.
        physicalKeys[VK_LBUTTON] = true;
        Require(installed(window, WM_LBUTTONDOWN, MK_LBUTTON, 0) == 701 && gameAttackLegacy,
            "Game did not receive its initial attack press");
        const auto heldCursor = nativeCursor;
        const int beforeHeldCursor = cursorShapeWrites;
        FeedInput(window, true);
        Require(!HasInputFocus() && InputHandoffPending(), "HUD took over a held game attack");
        Require(nativeCursor == heldCursor && cursorShapeWrites == beforeHeldCursor,
            "HUD hid the game cursor while still waiting for its held attack release");
        physicalKeys[VK_LBUTTON] = false; FeedInput(window, true);
        Require(!HasInputFocus(), "HUD trusted physical-up before the queued release reached the game");
        Require(installed(window, WM_LBUTTONUP, 0, 0) == 701 && !gameAttackLegacy,
            "HUD swallowed the game's attack release and left it attacking");
        FeedInput(window, true); Require(HasInputFocus(), "HUD did not acquire input after the game released it");
        ReleaseInput();

        // Legacy and raw are distinct routes; either release may arrive first.
        for (const bool rawFirst : {false, true}) {
            physicalKeys[VK_LBUTTON] = true;
            Require(installed(window, WM_LBUTTONDOWN, MK_LBUTTON, 0) == 701, "Legacy game press lost");
            Require(RawButtons(RI_MOUSE_LEFT_BUTTON_DOWN) == 701 && gameAttackRaw, "Raw game press lost");
            FeedInput(window, true); Require(!HasInputFocus(), "HUD captured a dual-route attack");
            physicalKeys[VK_LBUTTON] = false;
            if (rawFirst) Require(RawButtons(RI_MOUSE_LEFT_BUTTON_UP, 3, 2) == 701, "Raw game release lost");
            else Require(installed(window, WM_LBUTTONUP, 0, 0) == 701, "Legacy game release lost");
            FeedInput(window, true); Require(!HasInputFocus(), "One route released input on behalf of the other");
            if (rawFirst) Require(installed(window, WM_LBUTTONUP, 0, 0) == 701, "Late legacy release lost");
            else Require(RawButtons(RI_MOUSE_LEFT_BUTTON_UP, 3, 2) == 701, "Late raw release lost");
            Require(!gameAttackLegacy && !gameAttackRaw, "Game remained attacking after both releases");
            FeedInput(window, true); Require(HasInputFocus(), "Dual-route handoff never completed");
            ReleaseInput();
        }

        physicalKeys[VK_LBUTTON] = true;
        Require(RawButtons(RI_MOUSE_LEFT_BUTTON_DOWN) == 701, "Raw-only attack press lost");
        FeedInput(window, true); Require(!HasInputFocus(), "HUD captured a raw-only attack");
        physicalKeys[VK_LBUTTON] = false; physicalKeys[VK_RBUTTON] = true;
        Require(RawButtons(RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN, 4, -3) == 701 && !gameAttackRaw,
            "Mixed release/new-press packet was split or swallowed");
        FeedInput(window, true); Require(!HasInputFocus(), "Mixed raw packet lost the continuing right-button press");
        physicalKeys[VK_RBUTTON] = false;
        Require(RawButtons(RI_MOUSE_RIGHT_BUTTON_UP) == 701, "Raw right-button release lost");
        FeedInput(window, true); Require(HasInputFocus(), "Mixed raw handoff did not finish");
        ReleaseInput();

        // Closing while an HUD click is down must consume that same click's up,
        // then resume without any further visible frame or an extra HUD toggle.
        FeedInput(window, true);
        physicalKeys[VK_LBUTTON] = true;
        Require(installed(window, WM_LBUTTONDOWN, MK_LBUTTON, 0) == 0 &&
            RawButtons(RI_MOUSE_LEFT_BUTTON_DOWN) == 902, "HUD click reached the game");
        ReleaseInput(); ReleaseInput();
        Require(!HasInputFocus(), "HUD hide retained active input capture");
        physicalKeys[VK_LBUTTON] = false;
        Require(installed(window, WM_LBUTTONUP, 0, 0) == 0 && RawButtons(RI_MOUSE_LEFT_BUTTON_UP) == 902,
            "Hidden HUD leaked its unmatched click release");
        Require(!gameAttackLegacy && !gameAttackRaw, "Closing the HUD initiated a game attack");
        Require(installed(window, WM_LBUTTONDOWN, MK_LBUTTON, 0) == 701 && gameAttackLegacy,
            "Game input failed to resume without a new frame or HUD toggle");
        installed(window, WM_LBUTTONUP, 0, 0);

        // Raw keyboard and classic key-up must also remain paired, including
        // left/right modifier normalization used by the overlay shortcut.
        physicalKeys['W'] = true;
        Require(installed(window, WM_KEYDOWN, 'W', 0) == 701 && RawKey('W', 0) == 701, "Game movement press lost");
        FeedInput(window, true); Require(!HasInputFocus(), "HUD swallowed a held movement key");
        physicalKeys['W'] = false;
        Require(installed(window, WM_KEYUP, 'W', 0) == 701, "Movement key-up lost");
        FeedInput(window, true); Require(!HasInputFocus(), "Raw movement release was not awaited");
        Require(RawKey('W', RI_KEY_BREAK) == 701, "Raw movement release lost");
        FeedInput(window, true); Require(HasInputFocus(), "Keyboard handoff failed");
        physicalKeys[VK_RCONTROL] = true;
        Require(installed(window, WM_KEYDOWN, VK_CONTROL, static_cast<LPARAM>(1) << 24) == 0 &&
            RawKey(VK_CONTROL, RI_KEY_E0) == 902, "Panel modifier escaped capture");
        ReleaseInput(); physicalKeys[VK_RCONTROL] = false;
        Require(installed(window, WM_KEYUP, VK_CONTROL, static_cast<LPARAM>(1) << 24) == 0 &&
            RawKey(VK_CONTROL, RI_KEY_E0 | RI_KEY_BREAK) == 902, "Modifier release did not drain both routes");
        Require(installed(window, WM_KEYDOWN, 'A', 0) == 701, "Modifier drain blocked later gameplay");
        installed(window, WM_KEYUP, 'A', 0);

        // A polled-only UI press can finish on the worker while every HUD is
        // hidden. No raw registration, queue draining or fake key-up is needed.
        FeedInput(window, true);
        physicalKeys[VK_LBUTTON] = true; FeedInput(window, true); ImGui::NewFrame(); ImGui::EndFrame();
        Require(io.MouseDown[0], "Test did not seed a polled HUD button");
        ReleaseInput(); physicalKeys[VK_LBUTTON] = false; MaintainInputHandoff();
        Require(installed(window, WM_KEYDOWN, 'A', 0) == 701, "Hidden worker could not finish a polled-only release");
        installed(window, WM_KEYUP, 'A', 0);
        FeedInput(window, true); ImGui::NewFrame(); ImGui::EndFrame();
        Require(!io.MouseDown[0], "Reopen retained a stale ImGui mouse-down");

        physicalKeys[VK_LBUTTON] = true;
        installed(window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
        ReleaseInput(); testForegroundWindow = reinterpret_cast<HWND>(2);
        Require(installed(window, WM_KILLFOCUS, 0, 0) == 701, "Focus loss did not reach the game");
        physicalKeys.fill(false); MaintainInputHandoff(); testForegroundWindow = window;
        FeedInput(window, true); Require(HasInputFocus(), "Focus return retained a stale held-button barrier");
        ReleaseInput(); Require(registrationWrites == 0, "Panel changed the game's raw input registration");
        ImGui::DestroyContext();
        std::cout << "PASS single software cursor, native shape hide/restore/client/non-client/foreign ownership; game/HUD press-release ownership (legacy + raw mouse/keyboard), queued releases, hidden drain, modifiers, focus and ImGui reset; raw cursor/warp/fallback/DPI/wheel regressions; mocked User32 only.\n";
        return 0;
    } catch (const std::exception& error) {
        ReleaseInput(); ImGui::DestroyContext(); std::cerr << error.what() << '\n'; return 1;
    }
}
