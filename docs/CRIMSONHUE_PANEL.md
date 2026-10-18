# Optional CrimsonHue panel — CDT 2.2.5 / CrimsonHue 1.0.0

CDT 2.2.5 can host the interactive mixer for the separately installed
[CrimsonHue 1.0.0](https://github.com/ZappendusterFX/CrimsonHue) Philips Hue
Entertainment companion. CDT remains independently usable without CrimsonHue.
No Hue controller is bundled in the Telemetry ZIP, and loading either module
does not start physical lighting output.

## Install and use

Install both matching release packages with the game closed. Telemetry requires
the .NET 8 ASP.NET Core Runtime (x64); CrimsonHue additionally requires the
.NET 8 Desktop Runtime (x64). Keep each complete mod-manager package together.
Neither product requires PIX or another GPU capture tool to run.

Ctrl+Shift+H opens or hides the mixer; change its binding in Hue Setup. The tabs
are **Lighting / Room / Setup / Advanced**. Setup pairs the bridge and imports an
existing Entertainment area. Room calibrates ordinary Point lights or segmented
Wall areas while retaining imported Hue placement. Horizontal washers use distinct
Side offsets and equal Height angles; Segment width applies to each segment.

Lighting offers Overall brightness, Local light effects, Background light and
Quick/Balanced/Calm response, with separate Recommended and preserved Custom
settings. Both modes include Point lights (0–200%), Spotlights (0–1000%) and
Own lantern (0–100%). Point/spot weights initially start at 100%; Recommended
starts the additional own-lantern weight at 10%. Point/spot describes light
geometry independently of standalone/grouped contributions, not fire versus
blue lamps. The conservative lantern signature is limited to the recognized
complete pair on game build 25477059; unsupported, partial or ambiguous matches
keep the ordinary type weight. These controls change Hue output, not raw telemetry.

Advanced contains detailed mixer controls, presets and raw CDT data. Recommended
spatial defaults use a 30-degree radius, 20% softness and focus 4. Saved Custom
values remain separate. Entertainment layout changes require refresh before
restarting sync; do not assume new service placement supplies new segment positions.

The title-bar X, Hide settings and shortcut hide the panel while sync continues.
Use **Stop** to end lighting sync, and wait for stopped status before game exit:
process termination cannot guarantee bridge restoration. Demo preview sends no
physical output. The interactive panel captures input while open. If a handoff
reminder appears, release held keys and mouse buttons before using it.

## Single renderer and separate controller

CDT owns D3D12 rendering, the ImGui context and panel input. CrimsonHue owns its
hidden managed controller, Hue bridge communication, current-user protected
settings, spatial calibration and stream cleanup. CrimsonHue adds no second
graphics-hook set. No Hue credentials or lamp network client are added to CDT;
the neutral HTTP/WebSocket telemetry API remains unchanged.

The worker discovers only already loaded modules, checks protocol 1 and pins a
compatible controller. It reads credential-free JSON snapshots (256 KiB maximum)
and drains a 64-command FIFO with 16 KiB maximum per command. Present uses cached
state and queues commands without calling .NET or doing network I/O. Both panels
share one ImGui frame and CDT's SDR/scRGB/HDR10 output. Passive telemetry views
draw behind the interactive mixer. Old independent-renderer Hue ASIs are incompatible.

`[CrimsonHue] Enabled=1` enables optional discovery and is the default for old
INIs. `Enabled=0` disables it. With every CDT view off and no compatible Hue module
loaded, discovery installs no graphics hooks and starts no API client. A compatible
module can enable Hue-only rendering. Late loading after swapchain creation
requires a restart. Hot unload and replacing loaded ASIs are unsupported.

## Input and visibility limits

The panel retains cursor-atlas metadata, uses delivered raw mouse motion with a
legacy fallback, and suppresses the native Windows pointer while its software
cursor owns the foreground game client. It restores applicable clipping/cursor
state on hiding. It finishes observed game-owned releases before taking input
and completes panel-owned releases when hiding; no artificial key-up or cursor
warp is generated. Mocked Win32 regressions cover these transitions. This does
not prove suppression of every independently polled or buffered game-input route.

Hue currently accepts fresh `clear` physics-ray-fan light contributions. Dense
cages, closed collision hulls and colliding glass can therefore exclude visibly
shining lamps; noncolliding geometry may have the opposite effect. Raising a
type weight cannot restore a source already excluded by that filter. Physics
collision and renderer selection are not optical visibility. Renderer-derived
contribution research remains unfinished and is not part of this release.

## Maintenance and validation

Sources in `native/CrimsonDesertTelemetry.Asi/crimsonhue-panel` are maintained in
CrimsonHue's `native/CrimsonHue.Panel`. Update them with its
`scripts/Sync-CdtPanel.ps1`; SOURCE-SHA256.txt records exact imported bytes.
The copy owns no DXGI/MinHook/device lifetime or telemetry acquisition.

Build with the maintained package builder, choosing an unused version:

```powershell
pwsh -File scripts/Build-ModManagerPackage.ps1 -Version 2.2.5 -NativeBuildId 25477059 -Research off
build\native-package-release\Release\CrimsonHue.OverlaySmoke.exe C:\DEV\CrimsonHue\artifacts\asi\CrimsonHue.asi artifacts\hue-panel\ingame.png
```

The hidden software-GPU harness loads the actual Hue ASI and exercises its bounded
commands, real shared panel, pixel readback, hide/reopen, resize and shutdown.
It does not load saved credentials or contact lamps. Managed controller and
mocked-input checks, package validation, owner gameplay observations and physical
lamp acceptance remain distinct evidence. Earlier preview validation is retained
in HUE_LIGHTING_UI.md, HUE_HUD_CLOSE.md, HUE_INPUT_HANDOFF.md, HUE_SINGLE_CURSOR.md,
HUE_SOURCE_WEIGHTS.md and HUE_SPOTLIGHT_RANGE.md; those versioned records describe
their own artifacts, not a new release binary.