# Hue HUD close behavior — 2026-10-17

The title-bar X now saves and hides the Hue HUD while lighting sync continues,
matching Ctrl+Shift+H and Hide settings. Explicit Stop remains the way to stop
the Entertainment stream. The controller fix is in Hue .asi.19: the existing
`close` action saves settings without calling Stop. CDT keeps its existing hide
and command path, and updates the footer to describe the corrected behavior.
The maintained panel sources were synchronized from CrimsonHue and match their
SOURCE-SHA256.txt manifest. Protocol 1 is unchanged; older compatible CDT panels
also use the corrected controller behavior, with their previous footer text.

New immutable pair: **CDT 2.2.5-huepanel.9 + Hue 0.4.0-asi.19**. CDT was built
with `scripts/Build-ModManagerPackage.ps1 -Version 2.2.5-huepanel.9
-NativeBuildId 25477059 -Research off`. The in-memory package/profile regression,
expanded package validator and ZIP comparison pass.

- CDT ZIP:
  `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.9-ModManagers.zip`.
  SHA256 `A418EAAF59731F633968955A4187B6335DC672A7A5BB0B581D9B409F38D94D17`.
- CDT ASI SHA256
  `E2E9646AF728CF7144B08847E26572034F29F44449E19F0953B3D90E2E49DCC2`.
- Tested/shared renderer `build/native-package-release/Release/cdt_overlay.lib`
  SHA256 `37ECF3CAD8E41A2598082441C8E16AF515970CEB8887B061EA1884EF2837E22E`.
- Exact paired Hue ASI SHA256
  `C2D6427A3965777BCC47674DE156995E8C2B3EC36159D0FDAF2B36174B8E57CF`.
  Hue ZIP SHA256 `D1999EBD166DDF7226574937223202D541B393FD974F6899A072365416389BAF`.

**12/12** focused CTest input/overlay/notification checks passed. One hidden
D3D12 WARP run against the exact packaged Hue .asi.19 passed pixel readback
(748,434 visible pixels), independent HUD/hide, Hue-only reopen, resize and
shutdown. The screenshot including the footer was visually reviewed. Controller
startup also runs the actual close and Stop handlers with an isolated cancellable
session marker, checking that close retains it and explicit Stop cancels it.
No physical stream is opened by these tests.

Evidence: `artifacts/hud-close-package.log`, `artifacts/hud-close-native-tests.log`,
`artifacts/hud-close-shared-smoke.log`, `artifacts/hud-close/lighting.png`.
The existing Hue core suite passes110/110. No real input, game state, installed
ASI, saved settings or physical output was changed. Old packages remain immutable.
Next: owner installs with the game closed and checks X/shortcut reopen with
continuing physical output. Other stream-stop causes remain independent.

## Controller follow-up: Hue .asi.20

Keep this same CDT.9 package with the newer Hue .asi.20 controller. It includes
the X fix and also recognizes edits to separate Hue service-placement positions
when the bridge leaves channel coordinates unchanged. Geometry representations
remain separate; the controller does not invent segment coordinates from the
service positions. The five new Hue test groups bring its core suite to115/115.
Exact packaged-controller smoke and one further hidden shared D3D12 WARP run pass,
including748,434 visible pixels, hide/reopen, resize and shutdown. Log:
`artifacts/service-layout-shared-smoke.log`. No CDT source or binary rebuild was
needed after the .9 validation above.

Hue.20 ZIP SHA256:
`02DAE64D672E64E4BA79AAE0E586C3F0E6A3D9B1AA177B0DAA13D0E7AC241665`.
Hue.20 ASI SHA256:
`D858B78136D5F7926017E261796A96951D6C58899BDCADAB97EE6A86715C3511`.
No installation, game input or physical lamp output was performed.
