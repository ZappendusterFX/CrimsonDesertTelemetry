# Hue cursor drift correction — 2026-10-16

The owner reports slow movement toward the upper-left while the Hue HUD is open.
Closing the HUD restores normal gameplay control. The maintained input adapter
is `C:\DEV\CrimsonHue\native\CrimsonHue.Panel\overlay_input.cpp`; this product
consumes its synchronized copy under `native/CrimsonDesertTelemetry.Asi/crimsonhue-panel`.

## Evidence and implementation

The previous adapter copied `GetCursorPos` through `ScreenToClient` into ImGui on
every owned frame. It swallowed `WM_INPUT` without inspecting the mouse payload.
Consequently, any game-side system-cursor repositioning also moved the software
HUD cursor. This path is established from code and reproduced with mocked User32;
the game function responsible for the owner's actual drift was not traced.
The active CDT renderer does not enable ImGui keyboard navigation and does not
use the ImGui Win32 backend to move the OS cursor.

The adapter now latches a local HUD cursor after the first valid mouse
`WM_INPUT` packet reaches its window. Relative motion updates that cursor once;
subsequent OS cursor positions cannot move it. Absolute devices use normalized
primary-screen or signed virtual-desktop coordinates. Coordinates are bounded
in the client area, then mapped once to the renderer's display dimensions.
Hide, focus loss and window changes reset the latch and preserve input release.
Repeated `SetInputWindow` calls for the same window retain capture, closing a
previous per-frame gap through which game input could pass.

No devices are registered, no system cursor position is written, and no buffered
raw-input queue is drained. Without delivered mouse packets, normal OS-position
input remains available. Registration alone is insufficient: Microsoft's
[buffered input example](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input#performing-a-buffered-read-of-raw-input)
can consume raw input without dispatching `WM_INPUT`. The individual packet is
read before the existing `DefWindowProc` cleanup. Raw movement has the documented
[device-coordinate semantics](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse),
so its sensitivity need not match Windows pointer acceleration.

This is a narrow input correction. It does not change telemetry, Ambient mapping,
stream ownership, lighting output, room settings or the headless Hue controller.
For a game input route that never dispatches mouse `WM_INPUT`, the fallback
remains susceptible to game cursor warps; real-game drift acceptance is pending.

## Validation

Release build succeeds with `/W4 /WX`. **13/13 focused tests pass**, including
input ownership, overlay model, SDR/scRGB rendering, notifications, light HUD,
HDR compositing and the isolated WebSocket harness. The input test compiles the
actual adapter with mocked User32 and verifies:

- OS warps cannot move an established raw HUD cursor; genuine deltas apply once,
  cancel correctly and clamp to client bounds.
- Valid packet detection, malformed/keyboard/truncated packet rejection, and
  continued OS fallback when no mouse packet arrives.
- Client/render scaling, absolute devices and negative virtual-desktop origins.
- Same-window ownership, hide/reopen, immediate background passthrough,
  reentrant clip restoration, wheel delivery and zero registration writes.

The shared renderer smoke uses the immutable **Hue 0.4.0-asi.16** ASI, SHA256
`8F738B13DCB9C8AA994CFD95AD32F281097D6930081320484AC907E0BB3B3564`.
Its assertions now match that controller's 28 numeric controls, absent legacy
axis switches, and spherical influence-radius commands/default/range. Both Room
and Setup pass in a hidden D3D12 WARP window: **748580 visible pixels each**,
software-cursor rendering, independent hide/reopen, resize and orderly shutdown.
Both screenshots were visually inspected. No desktop/game capture, physical
lamp output or real-input injection was used.

Evidence under `artifacts/`:

- `cursor-input-build.log`, `cursor-input-tests.log`.
- `cursor-input-huepanel7/room.log`, `room.png`, `setup.log`, `setup.png`.
- `cursor-input-huepanel7/tested-overlay-hash.json` records the tested shared
  renderer library hash for comparison with the package build.

All seven synchronized panel files match the maintained Hue sources and their
`SOURCE-SHA256.txt` manifest. Original product main/research and older packages
remain untouched. This evidence is synthetic; no installed ASI was replaced.

## Immutable package

**CDT 2.2.5-huepanel.7**, production profile, research OFF, exact native build
**25477059**. Built with the maintained `Build-ModManagerPackage.ps1` and its
expanded-package/ZIP/self-test validator; all passed. The archive contains one
`CrimsonDesertTelemetry/` root and the expected nine files, with no loose EXE or
JSON. No INI overrides were used.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.7-ModManagers.zip`
- ZIP SHA256: `E58A052C2AACF733C97D0D46071246B62C21B6D450533FA08D6025701C7FF2A8`
- ASI SHA256: `079648F6B23699C838A9FDF603125BF660E56422114ABA5E0F6587DC7D80DD34`
- Tested and packaged `cdt_overlay.lib` SHA256:
  `3275C88DCD3658A1F6F269D01F4177969058C5632CCB39CE6F33716E5C88A94A`.
- Build/validation log: `artifacts/cursor-input-package.log`. Earlier sandbox
  profile and NuGet network failures are retained in `cursor-input-package-first.log`
  and `cursor-input-package-restore.log`; the completed build restored normally
  with advisory checks enabled and workspace-local CLI state.

This candidate is **not installed or game-tested**. It works with the existing
Hue .asi.16 controller; a Hue replacement is unnecessary for this cursor change.
Next input-specific step: install CDT only after game exit, then confirm the
owner's real HUD cursor movement and return of gameplay control after closing.
