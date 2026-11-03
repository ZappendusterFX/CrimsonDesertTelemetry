Crimson Desert Telemetry
========================
Live light contributions, player/render-camera telemetry and local ambient data,
with a corner HUD, 3D light radar, fullscreen markers and startup/error notices.
Local HTTP/WebSocket APIs support other tools. This mod does not drive lamps.

Included telemetry features:
- Lights all around you. Telemetry reads the game's light list before the
  renderer discards what the current view cannot see, so lights behind the
  camera stay in the API, radar and markers. Particle groups become summed
  contributions; one physical fire can still produce several contributions.
- Physics collision checks. Nine rays sample camera-to-light paths, reporting
  clear/blocked/unknown. ON by default. This is not optical visibility: dense
  cages, closed collision hulls and colliding glass can exclude visibly shining
  lamps; noncolliding geometry may fail to block. F11 shows only fresh clear ones.

Fixed in 2.2.6:
- Restores camera/player, filtered and all-around light data, Ambient/GI and
  per-light physics checks for game patch2.04.05, Steam build26361232.
- Exact EXE and instruction/context guards remain; unknown executables fail closed.

Fixed in 2.2.4:
- Camera-local sky visibility and the local ambient estimate no longer stay
  unavailable for a whole session. Their copy was sometimes recorded during
  loading, before the GPU submission hook existed.
- No extra Direct3D 12 queue is created at world entry. That step coincided with
  a GPU hang in one crash. The submission hook now comes from the game's own
  presentation queue when the HUD, markers or notices are enabled.

Source and support: https://github.com/ZappendusterFX/CrimsonDesertTelemetry
Validation: https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/main/docs/releases/v2.2.6-validation.md
API: https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/main/docs/API.md

Requirements and installation
-----------------------------
Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), one x64 ASI loader,
and Crimson Desert patch version 2.04.05 on Steam for Windows. Technical identity:
EXE file version 1.0.0.3479, Steam build ID 26361232. Native capture rejects
unknown EXEs. The file/Steam numbers are not the game's public patch version.
Close the game, back up useful INI preferences and disable previous telemetry
packages and any separate legacy console ASI, if installed.
Import and enable the complete ZIP in DMM. Keep your existing loader and other
mods. Verify ALL six runtime files beside the game EXE, from the SAME version:

CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg

Important DMM upgrade/removal note: local tests with DMM 2.8.1 and 3.1.1 showed that
deleting this package can leave both Telemetry DLLs and both lowercase CFG files behind,
and an upgrade can retain an older deps.cfg. With the game closed, remove the old
package in DMM, then verify and delete only these four leftover Telemetry files
before importing the new ZIP:

CrimsonDesertTelemetry.Core.dll
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg

Do not delete your ASI loader. Back up a customized Telemetry INI before replacing
the package. A clean DMM 2.8.1 import was verified to deploy all six current files.

Keep .cfg names unchanged; some manager versions interpret loose JSON as patches.
The bootstrap caches only runtime configuration text under
LOCALAPPDATA/CrimsonDesertTelemetry/Runtime, keyed and verified by SHA-256.
No executable or game data is copied into that cache. Hot-unloading is unsupported.
Complete ZIP contents do not prove complete manager deployment. JSON Mod Manager
and manual ASI-loader installation need the same files; their current full
installation/uninstallation matrix remains pending.

Controls and configuration
--------------------------
The supplied INI enables the supported telemetry, ambient, upstream-light,
light-visibility and HUD paths. F8: corner HUD/radar. F9: details.
F10: fullscreen light markers. F11 (HideOccluded): show only lights with a fresh
clear result; blocked, unknown, missing and stale ones are hidden until a new
clear measurement arrives. Off by default: blocked lights are only dimmed.
Each shortcut accepts a decimal Windows virtual-key code; 0 disables that key.
Defaults: Overlay.ToggleKey=119, Overlay.DetailsKey=120,
LightOverlay.ToggleKey=121, LightOverlay.OcclusionToggleKey=122.
Keys do not stop API capture.
Automatic hiding in every menu is not implemented; use F8/F10 to hide the views.
INI changes require a game restart; display keys work immediately.

Research, Console, Explorer, the broad player/all-source bridge and OcclusionTest
remain absent from this production build. Merge preferences into the new template.
Configuration ranges, dependencies and tests:
https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/main/docs/INI_VALIDATION.md

Lights.Enabled includes authored lights; ManyLights adds renderer contributions
at 20 Hz by default and Upstream=1 adds all current engine lights from the same
capture. Ambient and SourceVisibility require ManyLights. Server.SampleRateHz
defaults to 60;
faster API polling does not create additional GPU samples. NearbyRadius uses game
units. LightSmoothing controls grouping and EMA; raw values remain unchanged.
Radar3D=0 selects the compass. AutoScale/Scale/Opacity/Corner control layout.
LightOverlay.Radius (default 100) is shared by markers, radar and visibility rays;
more lights share the 256-target visibility budget. MaxMarkers/MaxLabels bound
visual clutter, not source discovery.
HdrPaperWhiteNits=200 sets shared HDR UI white, clamped to 80-500 nits.
Notifications show readiness briefly or persistent actionable errors. Readiness
requires valid player and render-camera data. Native light/sky capture waits for
that first playable-world signal, so initial menu/shader loading cannot consume a
GPU transaction. Consult logs if graphics initialization fails.
Set Enabled=0 in Overlay, LightOverlay, Notifications AND optional CrimsonHue
integration to disable all UI owners and UI hooks/client. Native capture then locates its submission hook with its own
queue at world entry, the method used before 2.2.4. InitiallyVisible=0 only hides
an enabled view. Server and native light capture are independently configured.

API and data limits
-------------------
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream
Ambient:   http://127.0.0.1:27311/v1/ambient
Smoothed:  http://127.0.0.1:27311/v1/lights/smoothed
Visible:   http://127.0.0.1:27311/v1/lights/visible  (clear contributions only)

The host listens on loopback only. Do not launch another host on its port.
Routes remain v1; light-enabled snapshots use additive schema 1.6, otherwise 1.1.
lights.upstream lists every current engine light of a capture, including behind
the camera; rendererSelected marks those the renderer also used in this view.
Upstream and rendered lights carry sourceVisibility clear/blocked/unknown with
method physics-ray-fan, ray counts and measurement age (expires after 500 ms).
In the raw and smoothed feeds unknown/stale lights remain present with null
attenuation; nothing is removed. Only the visible feed omits non-clear lights.
Authored/rendered/upstream arrays overlap; do not add them together. It is not a
persistent light registry; a missing source does not prove OFF.
Linear HDR RGB/luminance are renderer values, not lumens or final pixels.
Ambient contains global sky, camera-local sky exposure and their derived estimate.
Exposure is sampled at the camera location, not the fraction of sky on screen.
The local fields stay optional: they are null without a valid sample, e.g. where
the engine's sky volume does not cover the camera. An earlier package passed an
open/enclosed/open test. It is not measured room brightness. Camera-orientation sensitivity
remains under investigation. Root orientation is not body pose; display spot-arrow
and frustum lengths are schematic. Fast motion can expose projection latency.

Graphics and compatibility
--------------------------
Automated D3D12 tests cover SDR/HDR10/scRGB and all UI owners together.
Live HDR-display, frame-generation and AMD/Intel game acceptance remain pending.
No NVIDIA, DLSS, Streamline, Nsight or PIX runtime dependency is required.
Unknown output format/color-space combinations are unsupported. HDR UI uses two
extra full-resolution textures and a scene composite; SDR has no extra composite.
Software-GPU tests do not establish compatibility with every game setup.

Diagnostics
-----------
The host reads memory; the plugin uses guarded code/D3D12 hooks and GPU copies.
No gameplay-control API or anti-cheat bypass is provided.
Logs beside the plugin: CrimsonDesertTelemetry.bootstrap.log (startup/host),
CrimsonDesertTelemetry.host.log, CrimsonDesertTelemetry.overlay.log (UI),
CrimsonDesertTelemetry.native.log (light/ambient capture).
Exact ASI/ZIP scan and live results belong to the versioned validation record;
an earlier clean scan is not a verdict for this binary.

Light visibility limits
-----------------------
Visibility is a sampled collision estimate, not optical visibility or brightness.
Nine rays go from the camera to fixed points around each light: any free ray
counts as clear. Dense cages, closed collision hulls and colliding glass can
exclude visibly shining lamps. Thin gaps may be missed; geometry without suitable
collision (foliage, some decorations) may not block. Renderer selection does not
prove optical visibility. It runs
only on the exact supported game build; otherwise every light stays unknown.
SourceVisibility.Enabled=0 switches it off. The normal light, smoothed-light and
ambient streams do not depend on this setting.
The complete source and preserved research are on GitHub. Forks are welcome.

Created by ZappendusterFX, developed with Claude and Codex (OpenAI).
See THIRD-PARTY-NOTICES.txt for dependency licenses.
Unofficial community project; not affiliated with Pearl Abyss.

New in 2.2.5: optional CrimsonHue 1.0.0 panel
-------------------------------------------
Install CrimsonHue 1.0.0 separately for Philips Hue Entertainment lighting.
CDT remains independently usable; its ZIP does not contain the Hue controller.
CDT hosts the mixer inside its existing D3D12/ImGui renderer. The separate Hue
ASI owns the headless controller, protected settings and lamp communication;
there is no second set of graphics hooks. Raw telemetry and the neutral API stay
unchanged. Hue additionally requires .NET 8 Desktop Runtime (x64).

Lighting / Room / Setup / Advanced provide bridge/Entertainment setup, spatial
calibration, Recommended and preserved Custom controls, presets and diagnostics.
Weights: Point lights 0-200%, Spotlights 0-1000%, Own lantern 0-100%.
Point/spot are independent of standalone/grouped. Recommended own lantern
starts at 10%; matching uses a conservative complete pair on build 25477059.
Unknown/partial/ambiguous lanterns retain ordinary type weighting.
Room supports ordinary Point and segmented Wall areas. Horizontal washers use
different Side offsets and equal Height angles; Segment width is per segment.
Recommended spatial defaults: 30-degree radius, softness 20%, focus 4.
Refresh changed Entertainment layouts before restarting lighting sync.

Ctrl+Shift+H opens/hides Hue; change its binding in Setup. Title-bar X and Hide
settings also hide without stopping sync. Use Stop to end it, and wait for stopped
status before game exit; process termination cannot guarantee bridge restoration.
No physical stream starts just by loading either module. Demo preview sends none.
[CrimsonHue] Enabled=0 disables optional discovery. Install/update both matching
packages with the game closed. Old independent-renderer Hue ASIs are incompatible.

Input/cursor corrections retain cursor-atlas metadata, use raw mouse motion with
fallback, wait for observed held game presses to release, and finish HUD-owned
releases when hiding. Release held keys/buttons if the handoff reminder appears.
The native Windows pointer is suppressed inside the owned foreground client,
leaving the panel's software cursor. Mocked-input tests do not prove coverage of
every independently polled/buffered game-input route.

Current Hue mapping requires fresh clear physics results. A visibly shining lamp
classified blocked is excluded before type weighting; raising percentages cannot
restore it. Renderer selection does not prove optical visibility. Renderer-derived
light-contribution research remains unfinished and is not included as a feature.
