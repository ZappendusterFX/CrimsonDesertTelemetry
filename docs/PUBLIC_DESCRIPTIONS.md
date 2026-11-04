# Public descriptions — 2.2.8, 2026-11-04

Current GitHub/product text for CDT 2.2.8 and optional CrimsonHue 1.0.6.
The historical Nexus 2.2.5 text remains in [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode)
and [NEXUS-2.2.5.md](NEXUS-2.2.5.md); it has not been updated or uploaded here.
The owner publishes Nexus manually. GitHub publication does not imply a Nexus upload.
Historical release notes remain unchanged.

## GitHub About text

Live light, player, camera and ambient telemetry for Crimson Desert, with guarded update recovery, local APIs, 3D radar and optional Hue/OpenRGB integration through CrimsonHue.

## Feature description — English

Inspect live lights, player position and the render camera in Crimson Desert.
Crimson Desert Telemetry provides a 3D light radar, fullscreen markers, status
notices and local HTTP/WebSocket APIs for overlays, tools and lighting integrations.
It works independently and does not itself drive physical lamps.

**New in 2.2.8:** restores the complete telemetry path for Steam build 26423880
and adds automatic recovery for compatible game updates. CDT checks whether
recovery is needed, waits for valid in-game player/camera data, then finds and
validates the required addresses. The HUD shows progress and a clear result.
Saved addresses are rechecked before reuse. The known build follows the normal
quick startup, and the release has no forced diagnostic test enabled. Updates
that change engine layouts, physics or shader semantics can still need a mod update.

Install the separate **CrimsonHue 1.0.6** companion for Philips Hue Entertainment,
optional parallel OpenRGB lighting and an in-game mixer. CDT hosts that optional panel
inside its existing renderer; CrimsonHue owns the headless lamp controller.
There is no second graphics renderer and no Hue controller bundled in the CDT ZIP.
The neutral telemetry API and raw light values remain unchanged.

The separate OpenRGB page discovers devices through the local SDK server and
lets you select and place them in a three-dimensional room. Each device receives
a color from its virtual position. Independent Light coverage and Background
light controls help local effects remain distinct from ambient fill. OpenRGB is
disabled by default and uses the common Start/Stop alongside Hue; a configured
Hue bridge/Entertainment area remains required. Other applications controlling
the same RGB devices should be closed.

The mixer offers Lighting, Room, Setup and Advanced tabs; bridge/Entertainment
setup; ordinary and segmented wall-light calibration; Recommended and preserved
Custom settings; separate Point lights, Spotlights and Own lantern weights;
presets and diagnostic data. Point lights range from 0–200%, Spotlights from
0–1000%, and Own lantern from 0–100%. Point/spot can each be standalone or grouped.
Own-lantern matching uses a conservative paired emission/cone signature near the
player, including the lamp's movement, rather than a general engine ownership ID.
It consumes CDT metadata without a separate game-build allowlist. Local output
smoothing reduces rapid changes after room mapping; genuine short effects can
still be softened, and unmeasured sources remain excluded.

Ctrl+Shift+H opens or hides Hue. Its title-bar X and Hide settings also keep
lighting sync running; use Stop to end it. Input handoff handles held delivered
game presses and panel-owned releases, and the panel suppresses the duplicate
Windows cursor inside the owned foreground client. Release held buttons/keys
when prompted. No claim is made that every independent game-input route is covered.

The existing Telemetry features remain available:

- Current engine light contributions all around the player, including behind
  the camera, read before view selection. Summed particle groups are decoded;
  one physical fire can still produce several contributions.
- Renderer-selected contributions, world positions, linear HDR RGB, luminance
  and recognized spot direction/cone angles; separate authored records.
- Camera-to-light physics checks: `clear` / `blocked` / `unknown`, ray counts
  and measurement age, on by default.
- Separate grouped/smoothed and clear-only grouped/smoothed feeds, preserving
  original raw contributions.
- Player position/root orientation, independent native render camera and projection.
- Global sky RGB and optional camera-local sky exposure/derived ambient estimate.
- Configurable HUD/radar, fullscreen markers, notices, HTTP snapshots, schema,
  health and WebSocket streams on loopback port 27311 by default.

Validated reference: **Crimson Desert Steam build 26423880** on Windows
(EXE `1.0.0.3527`). Changed executables require successful recovery before native
capture can start; unsupported layouts remain disabled. No NVIDIA,
DLSS, Streamline, Nsight or PIX runtime dependency is required.

## Visibility and data limits

Nine rays sample collision from the camera toward points around a source. Any
free ray gives `clear`; all nine blocked gives `blocked`; invalid, incomplete or
older-than-500-ms results are `unknown`. This is not optical transmission or the
renderer's measured visible light contribution. Dense lamp cages, simplified
closed collision hulls and colliding glass can hide visibly shining lamps from
the clear-only feed. Noncolliding occluders may fail to block. Renderer selection
alone does not resolve those cases. GPU contribution research is not a release feature.

With the default unfiltered HUD, blocked lights are dimmed and unknown sources
remain visible. F11 shows only fresh `clear` results. Raw and unfiltered smoothed
records/RGB remain intact; the separate `/v1/lights/visible` feed includes only
clear contributions. The current Hue consumer similarly requires fresh clear
physics results. `[SourceVisibility] Enabled=0` disables measurements without
removing raw sources, but then provides no measured clear-only output.

Authored, rendered and upstream arrays overlap; do not add them together.
The upstream list is the current input to the light pass, not a persistent world
registry. Missing does not prove OFF. Linear HDR RGB is not physical lumens or
final screen pixels. Sky/ambient local fields are optional and can be null.
Fast motion may expose capture/projection latency; displayed spot lengths are schematic.

## Controls and installation

F8 toggles the telemetry HUD/radar, F9 details, F10 fullscreen markers, and F11
clear-only filtering. Each accepts a decimal Windows virtual-key code in the
INI; 0 disables that key. These shortcuts do not stop telemetry. Automatic hiding
in every game menu is not implemented. INI changes require a restart.

Requirements: Windows x64, the supported game build, .NET 8 ASP.NET Core Runtime
(x64) and one compatible x64 ASI loader. Close the game before installing or
upgrading. Disable the previous Telemetry package and any separate legacy
console ASI; import and activate/deploy the complete ModManagers.zip. Keep all
six Telemetry runtime files together:

```text
CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg
```

DMM 2.8.1 and 3.1.1 removal/upgrades can leave both DLLs and both lowercase CFG
files behind. With the game closed and the old package removed, delete only
those stale Telemetry companions before redeploying; preserve your ASI loader.
Keep CFG filenames unchanged and migrate useful INI preferences into the current
template. Importing without activating/deploying is insufficient. Other manager
lifecycle coverage remains limited.

For Hue/OpenRGB, install **CrimsonHue 1.0.6 separately**, plus .NET 8 Desktop Runtime
(x64). It is optional. Stop Hue sync and wait for stopped status before exiting
the game; process termination cannot guarantee bridge restoration. No stream
starts just by loading the modules. Hot unloading is unsupported.

Automated SDR/HDR10/scRGB rendering does not establish live HDR-display,
frame-generation or AMD/Intel game acceptance. Native instrumentation uses
guarded hooks, GPU copies and game physics queries; the full product is not
purely read-only. It supplies no gameplay-control API or anti-cheat bypass.

[Source and downloads](https://github.com/ZappendusterFX/CrimsonDesertTelemetry)
and [API reference](API.md). Created by ZappendusterFX with Claude and Codex
(OpenAI). Unofficial; not affiliated with Pearl Abyss. MIT source; dependencies
retain their notices.
