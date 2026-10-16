# Hue cursor startup crash and dump hang — 2026-10-16, Codex

## Live evidence

Owner installed CDT `2.2.5-huepanel.2` and CrimsonHue `0.4.0-asi.10` and reported
the game hanging on start. PID 8240 started at 19:31:06; deployed ASI hashes
exactly matched both packages. Hue was ready at 1281 ms. CDT's log reached
`Overlay ready: D3D12 / SDR.` at 19:31:15. No ReShade proxy loaded.

Thread-state inspection found **113 suspended threads and one waiting thread**,
26528. No debugger was attached. The diagnostic stack reader temporarily
incremented only the sampled thread's suspend count and restored that increment;
it did not resume the 113 already suspended threads, patch code, inject hooks or
terminate the game. No raw memory dump or saved Hue credentials were collected.

Thread 26528 was inside Streamline's exception/dump path:

```
ntdll!LdrLoadDll -> Steam overlay forwarding -> LoadLibraryExW
dbgcore!MiniDumpWriteDump -> sl.interposer exception handler
ntdll!KiUserExceptionDispatcher
CrimsonDesertTelemetry + 0xbc306
CrimsonDesertTelemetry + 0xad34a
CrimsonDesertTelemetry + 0xac819
CrimsonDesertTelemetry + 0x4838c
CrimsonDesertTelemetry + 0x48022
CrimsonDesertTelemetry + 0x49cc8
```

Relinking the unchanged release objects into a diagnostic copy with a linker
map produced **byte-identical `.text`**, SHA-256
`FAB4CACF13AC3703F7D73AABA14C2126804E8FD467A0397719FEE3984D02E021`.
That maps the exception/callers unambiguously:

| RVA | Function |
|---|---|
| `0xbc306` | `ImFontAtlasGetMouseCursorTexData + 0x46` |
| `0xad34a` | `ImGui::RenderMouseCursor + 0x15a` |
| `0xac819` | `ImGui::Render + 0x279` |
| `0x4838c` | CDT `Renderer::Draw + 0x2ec` |
| `0x48022` | CDT outer `Draw + 0x312` |
| `0x49cc8` | CDT `OnPresent + 0x68` |

Local evidence under `C:\DEV\CrimsonHue\artifacts\hang-8240` includes
`focus-stacks.txt`, `cdt.map`, diagnostic relink and before/after tests.
Stack export names from system DLLs are approximate; the CDT function mapping
uses the matching linker map. The exact dump-handler lock owner was not identified.

## Cause and fix

CDT's passive HUD discarded `io.Fonts->ClearInputData()` after upload. In pinned
ImGui 1.91.9b that clears `CustomRects` and sets `PackIdMouseCursors = -1`.
Hue enables `io.MouseDrawCursor` for foreground interaction; its first cursor
render then reads the deleted rectangle. The original hidden-window smoke never
enabled the software cursor. Earlier successful synthetic tests therefore did
not cover the failing path; `.2` failed real-game startup acceptance.

The corrected renderer retains its font input bytes for the ImGui context's
lifetime and calls **ClearTexData**, which discards only CPU texture pixels.
The cursor rectangle and source metadata remain valid, including after resize.
No third-party source patch, disabled cursor or swallowed native fault is used.

The GPU harness now forces software-cursor rendering with synthetic ImGui focus,
without taking the owner's real foreground focus. **Before the fix** it exits
with `0xC0000005` (`-1073741819`). **After the fix** the same GPU/washer/mixer,
hide/reopen and resize checks pass. This directly reproduces the live exception.

## Input release

Owner also reported missing mouse/keyboard control after hiding the HUD.
The crash/dump suspension itself blocks normal input; a separate surviving-game
input bug was not established from that frozen process. Release is hardened:

- Hide/Close/Stop immediately clear capture and pending text/wheel events.
- Cursor clipping restores outside the input mutex, avoiding User32 callback
  reentrancy while that mutex is held. Switching windows releases the old owner.
- Hidden panel frames disable ImGui mouse/keyboard input, clear UI focus and
  hide its software cursor. The WndProc passes keyboard, mouse and raw input
  through to the game's original procedure.

`hue-input-ownership` uses the real input-adapter code with mocked User32 to
verify capture, hide/close passthrough for key-down/up, mouse and raw input,
clipping restore once, pending-text cleanup, reopen and focus loss. It never
clips/moves the real cursor or injects input. Actual foreground gameplay input
remains owner acceptance after the corrected package is installed.

## Replacement

Use **CDT 2.2.5-huepanel.3**, production profile/research OFF/build 25477059,
with the **already installed CrimsonHue 0.4.0-asi.10**. Only CDT needs updating;
the headless Hue binary and saved settings/credentials do not change. Old ZIPs
remain immutable. Close/terminate the already crashed game before replacement;
no installed file or live hook was modified during this diagnosis.

ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.3-ModManagers.zip`,
SHA-256 `A401EA8E0AE8C12136E2816AB774ED9AAF66B9B4A86C90CCD67AB3F18447D1F1`.
ASI SHA-256 `51EDA7502E5E2384112084C8C25A2A2A9A9564FF8E7027D15ADBD8C3DDD860B8`.
Maintained package builder/validator passed. Final cursor/Hue GPU test passed
(850841 visible pixels, screenshot `cursor-final/ingame.png`); focused CDT
input/overlay/notifications/HDR regression suite **13/13** passed. Final tested
objects and packaged ASI have identical `.text`, SHA-256
`161C215653EC7F2153E2608F7EB63B44938CB9E5981433F8DB5EEE92E757EDE0`.
The corrected package is not installed or game-tested yet. Earlier 39/39 native
and 85/79 managed checks are historical baseline evidence, not a substitute for
this regression or real foreground game-input acceptance.
