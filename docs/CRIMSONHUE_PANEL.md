# Optional CrimsonHue panel — private 2.2.5-huepanel.5 preview

The owner authorized hosting the Hue mixer inside mandatory CDT after both
independent overlays disappeared in the game. A deterministic two-DLL MinHook
test proved that interleaved CreateHook/EnableHook calls can bypass the first
overlay; exact live interleaving remains unproven. This integration removes Hue's
second graphics-hook set instead of adding another coexistence workaround.

CDT owns all rendering, the ImGui context and panel input. The compatible
**CrimsonHue 0.4.0-asi.12** ASI owns its hidden managed controller, Hue bridge,
protected settings, wall calibration and stream cleanup. The neutral telemetry
API is unchanged. No Hue credentials or lamp network client are added to CDT.
Old independent-renderer Hue ASIs do not export protocol 1 and are rejected.

The worker discovers only already loaded modules, checks the protocol and pins
the compatible ASI. It reads credential-free JSON snapshots (256 KiB maximum)
and drains a 64-command/16-KiB-per-command FIFO. Present sees cached state and
queues commands without calling .NET or network I/O. Both HUD and Hue settings
use one ImGui frame and CDT's SDR/scRGB/HDR10 output. Passive HUD/markers draw
behind the interactive panel.

`[CrimsonHue] Enabled=1` enables optional discovery (the default for old INIs).
With the setting disabled, there is no Hue panel integration. With every CDT
view off and no compatible Hue module loaded, the discovery worker installs no
graphics hooks and starts no API client. A compatible module enables Hue-only
rendering. As with the existing overlay, late loading after swapchain creation
requires a restart. There is no hot unload or live replacement.

Ctrl+Shift+H is the default focus-gated Hue shortcut, configurable in Hue Setup.
Its user binding remains in CrimsonHue settings. Hide leaves sync running; X
queues Close/Stop/save. Graphics faults queue Stop and release input. The owner
must stop the stream before game exit because process termination cannot promise
restoration. Loading either module never starts a physical stream.

Sources in `native/CrimsonDesertTelemetry.Asi/crimsonhue-panel` are maintained in
CrimsonHue's `native/CrimsonHue.Panel`. Use its `scripts/Sync-CdtPanel.ps1` when
updating them; SOURCE-SHA256.txt records exact bytes. The copied module has no
DXGI/MinHook/device ownership or telemetry acquisition.

Build with the maintained package builder:

```powershell
pwsh -File scripts/Build-ModManagerPackage.ps1 -Version 2.2.5-huepanel.5 -NativeBuildId 25477059 -Research off
build\native-package-release\Release\CrimsonHue.OverlaySmoke.exe C:\DEV\CrimsonHue\artifacts\asi\CrimsonHue.asi artifacts\hue-panel\ingame.png
```

The synthetic harness loads the actual Hue ASI, checks its hidden controller and
bounded command queue, renders the real washer/mixer UI with CDT's overlay,
reads pixels and exercises independent hiding, Hue-only reopen and resize.
It never loads saved credentials or contacts lamps. Native regression tests
cover CDT without Hue plus SDR/scRGB/HDR paths. Game/input, physical lamps and
DMM installation remain separate acceptance. Install both matching ZIPs with
the game closed; never replace modules during the owner's current session.

In `huepanel.5`, pair with Hue `.asi.11`: the mixer uses CDT's 16-pixel body text
and the same display scale, with CDT's blue/cyan colors. An immutable last view
remains drawn if the snapshot mutex is briefly busy; such contention previously
skipped just the Hue panel for a frame. This fixes a possible flicker source,
not all causes of missed GPU submissions. Setup shows explicit found/link-button/
pairing/connected/failed states and an area placeholder instead of an empty combo.
The header shows connection status on every tab. The controller confirms connection
only after authenticated area retrieval and reuses a matching stored pairing.
The actual in-game HTTPS failure still needs validation with the new bounded
connection diagnostics; the separate client can read the owner's real bridge.
