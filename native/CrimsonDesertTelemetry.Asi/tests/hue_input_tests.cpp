#include <windows.h>
#include <imgui.h>
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
LRESULT CALLBACK Original(HWND, UINT, WPARAM, LPARAM) { ++forwarded; return 701; }
HWND TestForeground() { return testForegroundWindow; }
HWND TestAncestor(HWND window, UINT) { return window; }
BOOL TestClip(const RECT* value) {
    ++clips; clipping = value ? *value : RECT{0, 0, 1920, 1080};
    if (reenterClip && installed) installed(reinterpret_cast<HWND>(1), WM_MOUSEMOVE, 0, 0);
    return TRUE;
}
BOOL TestGetClip(RECT* value) { *value = clipping; return TRUE; }
BOOL TestPosition(POINT* value) { *value = {150, 150}; return TRUE; }
BOOL TestScreenToClient(HWND, POINT*) { return TRUE; }
SHORT TestKey(int) { return 0; }
LONG_PTR TestSubclass(HWND, int, LONG_PTR next) {
    const auto previous = installed ? installed : Original;
    installed = reinterpret_cast<WNDPROC>(next); return reinterpret_cast<LONG_PTR>(previous);
}
LRESULT TestCall(WNDPROC previous, HWND window, UINT message, WPARAM w, LPARAM l) { return previous(window, message, w, l); }
LRESULT TestDefault(HWND, UINT, WPARAM, LPARAM) { return 902; }
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool SameRect(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
}
#define GetForegroundWindow TestForeground
#define GetAncestor TestAncestor
#define ClipCursor TestClip
#define GetClipCursor TestGetClip
#define GetCursorPos TestPosition
#define ScreenToClient TestScreenToClient
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
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        unsigned char* pixels{}; int width{}, height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        SetInputWindow(window); FeedInput(window, true);
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
        ImGui::DestroyContext();
        std::cout << "PASS foreground capture, Hide/Close passthrough for keyboard/mouse/raw input, clipping restore, cursor hide, pending-text cleanup, reopen and focus loss; mocked User32 only.\n";
        return 0;
    } catch (const std::exception& error) {
        ReleaseInput(); ImGui::DestroyContext(); std::cerr << error.what() << '\n'; return 1;
    }
}
