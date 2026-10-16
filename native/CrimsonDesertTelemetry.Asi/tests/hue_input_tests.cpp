#include <windows.h>
#include <imgui.h>
#include <cmath>
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
LRESULT CALLBACK Original(HWND, UINT, WPARAM, LPARAM) { ++forwarded; return 701; }
HWND TestForeground() { return testForegroundWindow; }
HWND TestAncestor(HWND window, UINT) { return window; }
BOOL TestClip(const RECT* value) {
    ++clips; clipping = value ? *value : RECT{0, 0, 1920, 1080};
    if (reenterClip && installed) installed(reinterpret_cast<HWND>(1), WM_MOUSEMOVE, 0, 0);
    return TRUE;
}
BOOL TestGetClip(RECT* value) { *value = clipping; return TRUE; }
BOOL TestPosition(POINT* value) { ++cursorReads; *value = osPosition; return TRUE; }
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
SHORT TestKey(int) { return 0; }
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
}
#define GetForegroundWindow TestForeground
#define GetAncestor TestAncestor
#define ClipCursor TestClip
#define GetClipCursor TestGetClip
#define GetCursorPos TestPosition
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
        SetInputWindow(window); FeedInput(window, true);
        FrameAt(150, 150, "Legacy cursor did not use OS position");
        osPosition = {175, 225}; FeedInput(window, true);
        FrameAt(175, 225, "Legacy cursor stopped following OS position without raw registration");
        Require(HasInputFocus() && io.MouseDrawCursor, "Visible foreground panel did not own input");
        Require(installed(window, WM_KEYDOWN, 'W', 0) == 0 && forwarded == 0, "Open panel leaked keyboard input");
        Require(installed(window, WM_MOUSEMOVE, 0, 0) == 0 && forwarded == 0, "Open panel leaked mouse input");
        Require(installed(window, WM_INPUT, 0, 0) == 902, "Captured raw input was not cleaned up");
        installed(window, WM_CHAR, 'x', 0);
        const int beforeRelease = clips;
        reenterClip = true; // Reentrant User32 dispatch must not deadlock our input mutex.
        ReleaseInput();
        reenterClip = false;
        Require(!HasInputFocus() && SameRect(clipping, originalClip), "Hide did not release capture and restore game clipping");
        Require(clips == beforeRelease + 1, "Hide restored clipping more than once");
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
        const int beforeBackground = clips;
        FeedInput(window, true);
        Require(!HasInputFocus() && !io.MouseDrawCursor, "Background panel retained capture");
        Require(clips == beforeBackground, "Background release clipped another app's cursor");
        Require(installed(window, WM_KEYDOWN, 'A', 0) == 701, "Background panel swallowed input");
        testForegroundWindow = window; FeedInput(window, true); ReleaseInput(); ReleaseInput();
        Require(!HasInputFocus(), "Repeated Close/Stop retained capture");

        // A delivered raw mouse packet enables the virtual cursor. Registration
        // alone would not prove WM_INPUT reaches this window (buffered input).
        // The panel must never change registration or write the system cursor.
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
        ReleaseInput(); Require(registrationWrites == 0, "Panel changed the game's raw input registration");
        ImGui::DestroyContext();
        std::cout << "PASS raw virtual cursor survives OS warps; relative/absolute movement, signed desktop, DPI scaling, clamp, cancellation, idempotent window, safe legacy fallback; input release/reopen/focus/wheel and zero registration writes; mocked User32 only.\n";
        return 0;
    } catch (const std::exception& error) {
        ReleaseInput(); ImGui::DestroyContext(); std::cerr << error.what() << '\n'; return 1;
    }
}
