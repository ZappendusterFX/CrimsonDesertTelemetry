# In-game overlay validation

## Current development: ambient and per-light display, 2026-09-12

Camera-local ambient passed the controlled open/enclosed/open test in OFF v2.1.9
on build 25246367; the diagnostics page reports its age separately from global
sky. Current per-light production metadata has synthetic coverage, but real
visible/blocked/visible and behind-camera acceptance is still pending.

The HUD/radar display clear/blocked/unknown per-source metadata. Optional
`HideOccluded=1` hides fresh known blocked sources and provisionally hides a
pending unknown near a blocker measured within 250 ms. Other stale/unknown
results stay visible. The provisional hint is not a blocked measurement.
Default F11 toggles this mode without changing any raw or smoothed API
record or RGB. All four default shortcuts F8/F9/F10/F11 can be reassigned in the
INI or individually disabled with `0`; see [controls](../README.md#controls-and-configuration)
and [source visibility](SOURCE_VISIBILITY.md). These current changes do not inherit
live acceptance from the historical tests below, and no final release is claimed.

The production `CDT_RESEARCH=OFF` build ignores Research, Console, Explorer and
the legacy `OcclusionTest`, even when an old INI enables them. Production source
visibility and its HUD controls remain independent of those experimental paths.
`CDT_RESEARCH=ON` retains the research controls with a separate INI template.
The boundary fixes conflicting or inactive configuration options, not antivirus
detections. See [configuration validation](INI_VALIDATION.md) for the setting
matrix and recorded synthetic test results.

A live run with all production features enabled has not passed acceptance.
Automatic HUD hiding in every game menu is not implemented; no reliable generic
menu-state signal is established. Existing `playing`/`loading` values describe
telemetry availability and cannot validate menu hiding.

## Historical: 2.0.0 HDR compositor, 2026-09-07

The D3D12 UI now supports HDR10 and scRGB, in addition to 8-bit and 10-bit SDR.
The radar, fullscreen markers and notifications automatically follow the game's
buffer format and declared color space. `[Overlay] HdrPaperWhiteNits=200` sets
white brightness for all HDR UI, clamped to 80–500 nits, including markers and
notices while the corner HUD is disabled. No game HDR settings or metadata change.

The final native Release rebuild and all 14 native CTest paths passed locally in
8.73 seconds, including existing capture/guard, SDR UI/markers and WebSocket
coverage. The three new HDR test paths passed:

| Test | Verified | Result |
|---|---|---|
| `overlay-hdr-composite` | WARP readback: output-mode matrix, PQ/scRGB reference values, sRGB decode, Rec.709→2020 conversion, linear-light alpha, transparent finite-scene preservation, extended/negative scRGB, PQ extremes and repeated resource states | Pass |
| `overlay-scrgb-d3d12` | Real ImGui rendering, resize recovery and FP16→8-bit SDR→FP16 transitions on the same swapchain, with correct labels and first-frame output | Pass |
| `notifications-scrgb-d3d12` | Notification-only rendering through the scRGB path | Pass |

These tests use offscreen WARP textures/hidden windows and do not require an HDR display. Live HDR-display
and HDR game acceptance have not been performed; no suitable setup was available.
Prior installed-game acceptance below predates this HDR addition. Release
publication was resumed by the user after automated verification; no HDR build
has been declared live-game validated.

Supported output selection is strict:

| Buffer | Declared color space | Mode |
|---|---|---|
| R8G8B8A8/B8G8R8A8 UNORM, R10G10B10A2 UNORM | Full-range G22/Rec.709 | SDR |
| R10G10B10A2 UNORM | Full-range PQ/Rec.2020 | HDR10 |
| R16G16B16A16 FLOAT | Full-range linear/Rec.709 | scRGB |

Unknown combinations remain unsupported. FP16 defaults to linear/scRGB; a 10-bit
buffer defaults to SDR until a supported HDR color space is declared. Output-mode
changes use the existing idle/rebuild path instead of changing the game output.

Stock ImGui appearance is retained inside a transparent FP16 UI target. The
compositor unpremultiplies and decodes that gamma-coded UI before blending over
the scene in linear light. scRGB uses 1.0 = 80 nits; HDR10 decodes PQ to nits,
converts UI Rec.709 primaries to Rec.2020, blends and re-encodes PQ. It preserves
scene alpha and bypasses conversion for zero-alpha UI; it does not tone-map the
whole game. Tests compare unchanged finite transparent pixels byte-for-byte;
arbitrary FP16 NaN payload preservation is not claimed. The color conventions
follow [Microsoft's Advanced Color guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).

HDR rendering owns two additional full-resolution GPU textures and records a
scene copy/composite while UI is drawn. Shader compilation/allocation occurs on
the initialization worker, with no extra SDR compositor pass. Performance on an
HDR game setup has not been measured. This changes only UI presentation; the
local HTTP and WebSocket APIs, JSON schemas and raw telemetry remain unchanged.

## Previous: 2.0.0 light visualization before HDR, 2026-09-06

The package enables a larger oblique 3D light radar in the corner HUD and an
independent fullscreen light-marker layer. F8 toggles the corner HUD, F9 its
diagnostics and F10 fullscreen markers. The user confirmed the enlarged radar,
camera frustum and grouped detail UI in game on preview.4, then confirmed the
installed 2.0.0 package after restart. The
[in-game demonstration](https://youtu.be/eyRkkTXAU64) and
[supplied screenshot](../media/screenshot1.jpg) show the light visualization.

The radar uses measured camera forward/right/up, vertical FOV and aspect for its
pitch/roll-aware frustum. Nearby contribution cards group by complete-link
distance at most 0.15 game units while preserving individual current RGB values.
No RGB sum, smoothing or physical-object identity is invented; GPU slots appear
only in diagnostics. The raw API arrays are unchanged by presentation grouping.

All six UI/client test paths passed: model, HUD, notifications, combined lights,
lights-only and WebSocket. Raster fixtures cover nearby independent contributions,
camera pitch/roll with unchanged yaw, invalid projection/basis, clipping, stale
clearing, resizing and 800×480/4K. Pitched 4K and compact images were visually
inspected. The final 2.0.0 suite passed 11 native CTest paths in total, 56 managed
tests, HTTP/WebSocket smoke and package validation. These are local automated
checks; synthetic raster fixtures are not game screenshots.

A fresh installed 2.0.0 run passed progressing API/capture/frame counters with
independent player/camera poses and available lights. The user subsequently
accepted success appearing as soon as fresh requested data is available, even
during visible loading. Startup/loading/discovery waiting messages are silent;
the configured success duration is 6000 ms. Local errors can render independently
of a working host or native game hooks, subject to the supported graphics path.
Returning to the title screen can leave HUD/data visible for several seconds
until stale or another load. This accepted edge case remains; F8/F10 hide the views.

Only current filtered contributions are drawn, using the paired valid-prefix
counter introduced in preview.3. The API reconstructs world positions with the
capture-paired camera, while the HUD projects with the latest published camera.
It is not synchronized to Present; precise motion alignment/latency remains
unmeasured. There is no scene-depth test, so markers can show through walls.
This pre-HDR package supported D3D12/8-bit SDR only. No DLSS, Streamline
or NVIDIA runtime dependency is used, but AMD/Intel in-game setups and frame
generation remain unvalidated. See [package evidence](MOD_MANAGER_VALIDATION.md).

## Historical: 1.3.0-preview.2 light visualization, 2026-09-06

This checkpoint predates the valid-prefix fix and enlarged/grouped preview.4 UI.
Its 337-record replay is parser/projection evidence only: preview.1/2 could include
stale buffer-tail contributions, so it is not a valid current-light-count control.

Adds a compact oblique light radar to the corner HUD and an independently toggled
fullscreen world-marker layer in the same ASI. F8 HUD, F9 diagnostics, F10 markers.
Both views enabled in this preview's INI; missing configuration remains opt-in.
Filtered rendered records only, no scene-depth test, no invented persistent IDs.

Release build and six native overlay/client/notification test targets pass.
Real D3D12 fixture readback verifies projected markers, spot direction, behind/near
clipping, changing radar heights, independent and initially-hidden toggles,
stale/missing clearing, Present/Present1, both resize paths and 4K. The aimed-at
center label's collision gap was corrected after visual inspection. Small/4K
combined and marker-only images are in `build/light-overlay-*.bmp` (synthetic test
data, not game screenshots). Reader/projection also accepts the real A2 JSONL:
337 records, 178 in front of its recorded camera, 77 in the viewport.

**Then-pending live check:** install through DMM with the game closed; verify marker
alignment at a known lamp, pan/turn/walk, then F8/F10 independently. Projection
uses latest published camera, not a frame-synchronous Present camera; motion
latency remains unmeasured. HDR/frame-generation/recording coverage remains open.
Light-source reconstruction/capture itself is unchanged from the tested preview.1.

## Historical: 1.0.0 release preparation

The user confirmed working in-game behavior after restarting with preview.7.
Version 1.0.0 keeps that camera backend and makes the HUD opt-in: `[Overlay] Enabled=0`
by default, including missing INI/section/key. The disabled path skips HUD workers,
graphics hooks, module pinning for the HUD, hotkeys and its WebSocket connection;
the telemetry bootstrap still runs independently. Configuration is read at startup,
not hot-reloaded. Existing customized `Enabled=1` settings are preserved.

Verification of the prior 0.1.0 package: all 30 managed checks and all three native CTest
targets passed. Native tests cover absent/disabled/opt-in/initially-hidden HUD
configuration, skipped graphics installation and no WebSocket connection while
disabled; opt-in rendering and networking still pass. The nine-file release ZIP
passed payload validation and a check for Server Enabled=1 / Overlay Enabled=0.
Final packaged in-game acceptance remains pending for v1.0.0.

## Historical preview.7 preparation

Preview.7 replaces the camera backend with the native engine source, including a
successful cold-start/direction test without upscaling. Packaged HUD acceptance
still needs a fresh in-game test. The prior preview.6 HUD was reported smooth by
the user with DLSS enabled, but its old camera source froze with upscaling off.

## Historical preview.6 preparation

Status: preview.4 appeared in the actual game, but the HUD was too small at 4K and
the user reported camera-arrow lag/backsteps. Preview.6 includes readability
improvements and a temporal camera-selection fix verified by production-code replay.
Complete actual-game / NVIDIA recording acceptance is **pending**. Do not describe
the graphics smoke test as an in-game test.
Light-source research is paused and is not compiled or packaged in this release.

## Current implementation

- Dear ImGui 1.91.9b, MinHook 1.3.4 and JSON for Modern C++ 3.12.0 are pinned by
  official archive URL and SHA-256 in the native CMake project. They are statically
  linked; full notices are generated from their source licenses into the package.
- Uses the locally installed Segoe UI font, with embedded ProggyClean fallback.
  No Windows font file, game assets or memory dumps are distributed.
- Automatic resolution scaling uses render height relative to 1080p (2x at 2160p).
  The font atlas is baked at the matching size, not just enlarged as a bitmap.
  `[Overlay] AutoScale=0` disables this; `Scale` remains a user multiplier. The HUD
  is clamped to fit small windows and secondary text has increased contrast.
- `[Overlay]` and `[LightOverlay]` draw passive English views; the packaged INI
  enables both. Missing UI configuration defaults off. The host is an external
  memory reader, while the ASI hooks DXGI for display and instruments the renderer
  for GPU light capture. Blanket "read-only ASI" or "no hooks" claims do not apply.
- Hooks factory CreateSwapChain/CreateSwapChainForHwnd, Present/Present1,
  ResizeBuffers/ResizeBuffers1 and SetColorSpace1. The actual D3D12 device and
  presentation queue come from swapchain creation, never from an arbitrary queue.
- Graphics resource initialization, font loading/upload and shader compilation run
  outside Present. Present only tries locks and skips busy GPU/upload-ring slots.
  Each submitted draw has a fence. Resize waits up to two seconds for owned GPU
  work; on timeout resources are retained, which can cause DXGI to reject that
  resize. A restart is then recommended instead of risking an in-flight release.
- HDR uses an original compositor alongside the unchanged ImGui dependency.
  The FP16 UI target and same-format scene copy finish in shader-resource state;
  the backbuffer returns to Present. Same-direct-queue ordering and completion
  fences govern reuse/destruction. SDR keeps its existing direct draw path.
- Telemetry comes from `/v1/stream` on configured IPv4 loopback. Redirects/proxies
  are disabled. The dedicated WinHTTP worker parses bounded complete messages.
  Its blocking WebSocket receive never blocks rendering or graphics maintenance.
- The render thread reads the latest coherent snapshot without waiting for the
  network. Source timestamp plus local monotonic age controls staleness. Disconnect,
  malformed data and loading clear live values. No player direction is invented
  from camera direction, movement, or body animation.
- The passive HUD does not replace WndProc or capture input. Foreground-only
  key-edge polling controls visibility/details. Native light capture separately
  installs guarded code detours and GPU copies. Config is read at startup.
- The graphics-enabled ASI is pinned for process lifetime. **No hot-unload**. All
  changes/install/uninstall require a closed game. All three UI modules (Overlay,
  LightOverlay and Notifications) must be disabled to skip UI hooks/client.
  Light capture is independent. Graphics smoke executables are not in the mod ZIP.

## Historical preview.6 automated verification

Verified locally on 2026-08-30:

- Managed Release build: zero warnings/errors; all 22 checks passed, including nine
  new camera-selection checks. The 3600-sample production replay has 803 direction
  changes, zero backwards steps and zero unavailable samples.
- All three native CTest targets passed (model, D3D12, WebSocket).
- Hidden-window D3D12 output visually inspected; the scaled 3840x2160
  output is at `build/overlay-scaled-4k.png` (synthetic fixture, not the game).
- Expanded package and ZIP validator passed; exactly nine expected payload files.
- The preview.6 packaged ASI started the real host from an isolated path containing spaces on
  port 27329; health/schema responded. The host stopped when its loader exited.
  No loose JSON was created beside the ASI.
- Preview.6 ZIP SHA-256: `B90A97FAE2C965D4115385482758995D5E48A0E7A636776AEEB9682EBB4980F9`.
- Preview.6 ASI SHA-256: `161510E515A0F7F57A291D22E64CCF314C1B864EC466D1DC49002F7AFC879916`.

These are automated/local tests, not the manual in-game checklist below.

Run from the repository root after building the native targets:

```powershell
ctest --test-dir build/native-package -C Release --output-on-failure
```

`overlay-model` checks independent headings, vertical projection, unavailable
orientation, loading invalidation, timestamps/staleness, invalid schema/axes and
1080p/1440p/2160p/fixed-size/small-window scaling.

`overlay-d3d12` uses a **hidden** window with the Windows WARP D3D12 adapter. It
checks actual Present/Present1 hooks, upload/allocator reuse, hidden/test-only
presentation, both resize methods, recovery and a 3840x2160 resize/draw. An optional
first argument to `CrimsonDesertTelemetryGraphicsSmoke.exe` saves the initial
960x720 BMP; an optional second argument saves the 4K BMP. Its data
are explicitly synthetic test fixtures, not a Crimson Desert recording.

`overlay-websocket` uses a local ephemeral-port WebSocket fixture server to test
WinHTTP upgrade, fragmented JSON, loading, disconnect/reconnect and invalid data.

Also run the existing managed tests, bootstrap smoke test and package validator.
The manager payload now contains **nine files**, including THIRD-PARTY-NOTICES.txt,
and no loose JSON or EXE. This does not replace manager acceptance tests.

## Historical preview.6 manual acceptance checklist

Preserved as the checklist for that earlier candidate, not as an unresolved 2.0.0
release gate. Its defaults and camera-source descriptions are historical. Current
acceptance and remaining coverage are recorded above and below.

1. Close the game. Import preview.6 with DMM; enable/deploy it and launch normally.
   Ensure there is only one enabled telemetry ASI and preserve customized INI settings.
   Verify the deployed ASI hash: library import alone previously left the old game
   copy installed. Disabling/re-enabling the plugin with the game closed deployed it.
2. On the tested Steam build, load a save. Verify an English HUD and live values.
   Check the overlay log if absent; the ASI must load before swapchain creation.
3. Rotate only the camera: amber direction changes independently from cyan player root.
   Tilt up/down: pitch changes; near-vertical horizontal direction becomes unavailable.
4. Move/turn the character: position and cyan arrow respond. The cyan direction is
   the physics root, **not** the animated flying/rolling body pose.
5. Press F8 twice and F9 twice. Verify visibility/details and unchanged game controls.
6. Alt-tab, change resolution/window mode, load a save and teleport. Confirm recovery
   and no stale coordinates presented as live data. Compare frame timing HUD off/on.
7. Make a short NVIDIA **game** recording containing visible HUD, F8 hide and F8 show.
   Review the saved recording, not just the live screen. Frame-generation interaction
   still requires separate verification; successful SDR recording does not prove HDR.
8. Exit normally; verify the background host exits. Repeat launch. Then test disabling
   `[Overlay] Enabled=0`: telemetry works without any UI/render hooks.
9. Repeat install/deploy/start/disable/uninstall with JSON Mod Manager before claiming
   full JMM compatibility. Prior manager tests do not establish new HUD compatibility.

## Historical 2.0.0 limitations / release wording

- **D3D12: 8-bit/10-bit SDR, HDR10 and scRGB**. Other format/color-space combinations,
  alternate render APIs and multi-queue presentation remain unsupported. HDR has
  automated WARP coverage, not live display/game acceptance. API sampling is
  independent of HUD visibility.
- Other overlays, DXGI wrappers and frame generation need live coexistence testing.
  A late-loaded or unrecognized swapchain fails without guessing its command queue.
- The camera/player and lighting startup paths before HDR have local game acceptance.
  The HUD draws received directions without smoothing; low transport age alone
  does not prove Present-synchronous alignment. Exact motion latency and frame
  generation still need dedicated measurements.
- Character switch, animated body pose and worldspace identifiers remain outside
  the validated telemetry contract. Light telemetry is included, but the filtered
  feed is not a complete lamp registry and omission does not establish permanent
  OFF. Linear HDR values are renderer contributions, not physical lumens or final
  pixel colors; exposure normalization remains unfinished.
- The user selected version 2.0.0 and accepted its startup/title-screen behavior.
  Keep tested-build, graphics and remaining manager-lifecycle limits alongside
  the release; historical preview checklists do not establish new coverage.
