# Single Hue HUD cursor — 2026-10-18

Owner reports two visible cursors in the Hue HUD, only one functional. The
maintained panel draws its own raw-input ImGui cursor but previously did not
suppress the native Windows cursor, allowing two different visible positions.
The exact owner-visible second cursor was not traced in a game capture.

Maintained source is CrimsonHue/native/CrimsonHue.Panel; the synchronized CDT
copy now hides the native cursor shape while HUD input is owned, foreground and
the physical pointer is inside the game client. It handles client WM_SETCURSOR
and reapplies suppression on owned frames. Hide restores a suppressed shape if
the same foreground client still has a null cursor. Newer visible game shapes,
background apps and outside-client pointers are preserved. ShowCursor counters,
cursor position and device registration are unchanged. The held-press/release
handoff from .10 remains intact; no synthetic game releases are generated.

This follows [SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor)
and [WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor).
Actual adapter tests replace User32 calls: one software cursor/native hide,
client/non-client routing, restore/latest shape, foreign ownership, outside-client
scope, waiting held-input barriers and reentrant calls pass. Raw motion, fallback,
DPI, wheel, queued releases, mixed routes and hidden drain regressions also pass.
A negative control omitting frame suppression fails specifically at the double
cursor assertion; restored maintained source passes. No foreground window,
cursor, process or physical lamp was changed by these tests.

The new pair is **CDT2.2.5-huepanel.11 + Hue0.4.0-asi.21**, production/research OFF,
native build25477059. Hue.21 supplies tighter Recommended/reset spatial defaults:
30-degree radius, 20% softness and focus 4. Saved Custom values remain separate.
The mapper algorithm and neutral telemetry API are unchanged.

Package profile/expanded/ZIP validators and **12/12** focused native tests pass.
Exact packaged Hue.21 passes hidden shared D3D12 WARP/pixel/hide/reopen/resize/
shutdown smoke (748,434 pixels); image reviewed. A stale 60-degree assertion in
the smoke was corrected; only that executable was rebuilt, with the renderer
library hash unchanged from the package build. SOURCE-SHA256 matches the
maintained panel. Packaging does not install the update or prove the second
cursor is absent in the actual game. Owner acceptance follows installation of
both packages with the game closed.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.11-ModManagers.zip`.
  SHA256 `027966C7D309E89761897FF0F18B1D899573C5F3E5DFDBAA9C23140BE53C0501`.
- ASI SHA256 `70677F4964C75564A921DFB13DFF199B283E644FB1678CB46975F3AE1C9BC501`.
- Tested renderer library SHA256
  `856B851C1BB53D1A30FE7F9C37CBB468B6B2B103805EA8DAAA04C45E445A9C10`.
- Hue.21 ZIP SHA256 `09512D09AF0110FC14BB1EB72FC09062BD3866717575CA78EE6C501C56F83A1B`.
- Logs: `artifacts/single-cursor-package.log`, `single-cursor-native-tests.log`,
  `single-cursor-smoke-build.log`, `single-cursor-shared-smoke.log`; image
  `artifacts/single-cursor/lighting.png`. Mutation/restored input logs are in
  the parent Hue repository's `artifacts/cursor-single-*.log`.
