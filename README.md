# Crimson Desert Telemetry

**Live light, player, camera and local ambient data for Crimson Desert, with HTTP/WebSocket APIs, fullscreen light markers and a 3D radar.**

Inspect positions, colors and brightness of current light contributions from fires, candles, lanterns and glass/crystal lamps, alongside the player and render camera. Use the data in your own overlays, tools and lighting integrations.

**New in 2.2:**

- **Lights all around you.** Telemetry now reads the game's light list *before* the renderer discards what the current view cannot see. Lights behind the camera and out of view stay in the API, radar and markers. A fire bowl arrives as one light instead of many flickering particles.
- **Which lights actually reach you.** The game's own physics casts nine rays from the camera to every nearby light. A wall blocks all nine; a lantern cage blocks only some, so the lantern stays visible. The HUD dims blocked lights (F11 shows only fresh `clear` ones) and the API reports `clear` / `blocked` / `unknown` per light.

**Fixed in 2.2.4:** camera-local sky visibility and the local ambient estimate no
longer stay unavailable for a whole session, and no extra Direct3D 12 queue is
created at world entry. See the [2.2.4 release notes](docs/releases/v2.2.4.md).

[Published downloads](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/releases)
· [Watch the demo](https://youtu.be/eyRkkTXAU64)
· [API reference](docs/API.md)
· [Client examples](examples)

The [grouped and smoothed local-light stream](docs/SMOOTHED_LIGHTS.md) retains
the original contributions. The separate [ambient feed](docs/AMBIENT_STREAM.md)
carries global sky RGB. Camera-local sky visibility and its derived working RGB
estimate are available only when the native spatial sample succeeds. Before
2.2.4 a startup race could leave them unavailable for a whole session; that
cause is fixed. They remain nullable, for example where the engine's sky volume
does not cover the camera, and must not be treated as guaranteed output. Global
sky RGB itself remains unoccluded.

The product goal is to answer both **how exposed the player/camera location is to
the sky and environment** and **which individual nearby game lights can actually
reach it through world geometry**, including sources off-screen or behind the
camera. The current source targets **Crimson Desert patch 2.03.02** on Steam
for Windows. Internal EXE/Steam identifiers are listed under Compatibility.

Per-light visibility is a sampled collision estimate, not a measurement of how
much light gets through: any free ray counts as visible, all nine blocked as
blocked, and anything missing, stale or failed stays `unknown` in the raw data.
It is **on by default** in 2.2; raw and unfiltered smoothed light data do not depend on it.
With `HideOccluded=1`, the HUD shows only fresh `clear` results. The separate
`/v1/lights/visible` feed applies the same fail-closed rule to grouped/smoothed RGB.
Methods and limits are in [source visibility](docs/SOURCE_VISIBILITY.md).

[![Crimson Desert Telemetry: fullscreen light details and a 3D radar with the camera frustum](media/screenshot1.jpg)](https://youtu.be/eyRkkTXAU64)

*In-game light markers, separate contributions around a fire, and the player-centered 3D radar. Click the screenshot to watch the video.*

## What you get

| Data / view | Included |
|---|---|
| **Lights** | All current engine lights around the player (including behind the camera) plus the renderer-selected contributions: world position, linear HDR RGB, luminance, spot direction/cone, and whether the renderer selected them |
| **Light visibility** | Per-light `clear` / `blocked` / `unknown` from nine physics rays between camera and light, with measurement age |
| **Player** | World position, physics-root forward/up vectors and heading when valid |
| **Camera** | Native render-camera position, orientation, vertical FOV, aspect ratio and near plane |
| **Fullscreen light overlay** | Markers at projected light positions; aim toward a source to inspect position, color, brightness and distance |
| **3D light radar** | All nearby lights with height (filled = renderer-selected, hollow = outside the current view), blocked lights dimmed, player heading and a camera frustum that follows pitch and roll |
| **Local API** | HTTP snapshots and health, WebSocket streaming, JSON Schema and JSON Lines recordings |
| **Ambient / sky** | Global sky RGB; optional camera-local sky visibility and derived working RGB when a valid spatial sample arrives (nullable fields). |
| **Status notices** | Brief success when data becomes ready; actionable startup/build/capture errors |

Lighting, both HUD views and status notices are enabled in the supplied configuration. Each can be configured separately; the HUD is not required to consume the API.

**No NVIDIA or DLSS requirement.** The camera is read directly from the engine, not from a DLSS/Streamline data stream. Light capture and the overlay use standard DirectX 12 interfaces. Neither Nsight nor PIX is needed to run the mod. AMD/Intel game compatibility has not yet been tested.

**SDR and HDR overlays.** The DirectX 12 renderer supports 8-bit and 10-bit SDR, HDR10 (10-bit PQ/Rec.2020), and FP16 scRGB. It automatically uses the game's buffer format and color space for the HUD, light markers and notices. The recorded HDR validation includes real ImGui rendering and SDR/scRGB transitions; no live HDR-display/game test has been performed. [Validation details](docs/OVERLAY_VALIDATION.md).

## Install or upgrade

Requirements:

- Windows x64 and a supported Crimson Desert build.
- Microsoft **.NET 8 ASP.NET Core Runtime (x64)**.
- An x64 ASI loader, such as Ultimate ASI Loader, installed for the game.

1. Close Crimson Desert.
2. Download the **ModManagers.zip** asset from the release — not GitHub's source-code archive.
3. Disable the previous telemetry package and any separate legacy console ASI in your mod manager.
4. Import **and activate/deploy** the new package in Definitive Mod Manager (DMM) or JSON Mod Manager. Keep all included files together.
5. Start the game and load a save. The local host starts automatically.

DMM 2.8.1 clean deployment was verified locally. Its upgrade/removal path, also
in DMM 3.1.1, can leave both Telemetry DLLs and both lowercase CFG files behind. With the game
closed, remove the old package, verify and delete only those four leftover
Telemetry files, then import the new ZIP. Do not delete your ASI loader. A complete
matrix for other systems remains open; see the [validation record](docs/MOD_MANAGER_VALIDATION.md).

Do not merge old binaries or metadata into the new package. Preserve your INI preferences, but migrate them into the current sections. Replace both `.cfg` companions together with the DLLs; they contain .NET metadata and must **not** be renamed to `.json`, which DMM can interpret as game patches. Do not leave the former console ASI in a loader search path.

### Controls and configuration

| Key | Action |
|---|---|
| **F8** | Show/hide the corner HUD and 3D radar |
| **F9** | Toggle additional diagnostics |
| **F10** | Show/hide fullscreen light markers |
| **F11** | Toggle `HideOccluded`: show only lights with a fresh `clear` result; blocked, unknown and stale lights stay hidden until a new `clear` measurement |

The HUD does not capture mouse input. Hiding it does not stop telemetry.
These are defaults: all four shortcuts can be reassigned in the INI using decimal
Windows virtual-key codes, or individually disabled with `0`.

Edit `CrimsonDesertTelemetry.ini` before starting the game:

```ini
[Server]
Enabled=1
Port=27311
SampleRateHz=60

[Lights]
Enabled=1
NearbyRadius=100
ManyLights=1
ManyLightsSampleRateHz=20
Upstream=1

[Ambient]
Enabled=1

[SourceVisibility]
; Nine physics rays camera -> light; a sampled collision estimate.
Enabled=1

[Overlay]
Enabled=1
InitiallyVisible=1
ShowDetails=1
ToggleKey=119
DetailsKey=120
Radar3D=1
ShowAmbient=1
HdrPaperWhiteNits=200

[LightOverlay]
Enabled=1
InitiallyVisible=1
ToggleKey=121
HideOccluded=0
OcclusionToggleKey=122
Radius=100
MaxMarkers=512
MaxLabels=6

[Notifications]
Enabled=1
DurationMilliseconds=6000
```

- `[Lights] Enabled=0` disables both authored and rendered light feeds. `ManyLights=0` disables only the native GPU light capture.
- `[Lights] Upstream=1` (default) also captures the renderer's light **input** with every sample: all current engine lights, including behind the camera. HUD, radar, visibility and smoothing then use it; `0` keeps only the view-filtered output.
- `[Ambient] Enabled=1` enables the separate sky/ambient path and requires native ManyLights capture.
- `NearbyRadius` controls the API's player-centered light radius; `LightOverlay.Radius` controls both light views. Distances are **game units**, not a claimed metre conversion.
- Disable **Overlay, LightOverlay and Notifications** to skip all UI hooks/client. The server and light capture have their own switches. Without any UI, native capture locates its submission hook with its own queue at world entry, as before 2.2.4.
- `InitiallyVisible=0` hides an enabled view at launch; hotkeys cannot enable a view whose `Enabled=0`.
- `HdrPaperWhiteNits` controls all HDR UI brightness, including markers and notices with the corner HUD disabled. The default is 200 nits, clamped to 80–500. It does not change the game's HDR settings or metadata.
- Radar/marker swatches visualize measured HDR values; they do not reproduce the game's tone mapping. Nearby contributions share a detail box without merging, summing or smoothing their raw measurements.
- `[SourceVisibility] Enabled=1` (default) casts nine physics rays from the camera to each light within `[LightOverlay] Radius`. Any free ray reports `clear`, all nine blocked reports `blocked`, and missing, stale or failed results stay `unknown`. It is a sampled collision estimate, not optical transmission, and needs `ManyLights=1`. `0` switches it off; all other light data stays unchanged.
- `HideOccluded=1` or F11 shows only lights with a fresh `clear` result in the HUD/radar; `blocked`, `unknown`, missing and stale results are hidden. With the filter off, raw lights remain visible and blocked ones are dimmed. Raw and unfiltered smoothed API records/RGB stay complete; the separate visible API feed contains only `clear` contributions.
- The camera frustum uses the real basis and view angles; its drawn length is schematic. World X/Z axes are not compass north; player-root orientation is not an animated body pose.

The production `CDT_RESEARCH=OFF` profile contains the continuous physics ray fan
for light visibility and bounded ambient acquisition, without history buffers,
manual probes or diagnostic tracing. Research, Console, Explorer, the broad player/all-source bridge and legacy
`OcclusionTest` remain in a separate `CDT_RESEARCH=ON` profile. See
[configuration dependencies and validation](docs/INI_VALIDATION.md).

### Startup and errors

There is no persistent "loading" notification. A success notice appears for six
seconds once valid player and render-camera data establish a playable world. The
native light/sky capture waits for this one-shot signal, so initial menu and shader
loading cannot consume a GPU capture transaction. The duration is configurable
from 5–10 seconds. Actionable errors may appear immediately and remain until resolved.

The status display starts independently of game-memory validation, so an unknown EXE or missing host/runtime can still produce an explanation. If the graphics overlay itself cannot run, consult the logs beside the plugin:

- `CrimsonDesertTelemetry.bootstrap.log`
- `CrimsonDesertTelemetry.host.log`
- `CrimsonDesertTelemetry.native.log`
- `CrimsonDesertTelemetry.overlay.log`

Known behavior: after returning to the title screen without restarting, data/HUD may persist briefly before becoming stale or being replaced during loading. The views remain hideable with F8/F10. Telemetry availability is not a definitive menu/loading-screen detector.
Automatic hiding in every game menu is not implemented.

## Use the data

Connect through **HTTP** at `http://127.0.0.1:27311` or **WebSocket** at `ws://127.0.0.1:27311/v1/stream`. Both carry JSON; JSON is the data format, not a separate connection method. The server binds to **IPv4 loopback only**.

| Endpoint | Purpose |
|---|---|
| `GET /v1/snapshot` | Latest telemetry snapshot |
| `GET /v1/health` | Game, compatibility and capture health |
| `GET /v1/schema` | JSON Schema for the active payload |
| `WS /v1/stream` | Live JSON messages |
| `GET /v1/lights/smoothed`, `WS /v1/lights/smoothed/stream` | All current grouped/smoothed light contributions, regardless of visibility. |
| `GET /v1/lights/visible`, `WS /v1/lights/visible/stream` | Grouped/smoothed light contributions with a `clear` visibility result only; `unknown` and `blocked` contribute nothing. |

```powershell
Invoke-RestMethod http://127.0.0.1:27311/v1/snapshot
```

Product versions and API versions are separate: routes remain **HTTP API v1**.
The current source uses snapshot schema **1.6** with lights enabled and reports
per-light physics visibility on each rendered and upstream light. With lights disabled it remains
**1.1**. Optional per-light
metadata adds no route and changes no original RGB values. Clients should check
capability/status fields and freshness instead of assuming every source is always available.

- `lights.sources` contains authored engine-light records.
- `lights.rendered.sources` contains current filtered renderer contributions, including the investigated fire/candle path, reconstructed using the camera paired with their capture.
- `lights.upstream.sources` (schema 1.6, `Upstream=1`) contains every current engine light from the same capture before the renderer's view selection, including behind the camera. Fire bowls arrive as one summed group; `rendererSelected` marks lights that are also in `lights.rendered`.
- With the default INI, upstream and rendered lights carry `sourceVisibility`
  (`clear` / `blocked` / `unknown`, method `physics-ray-fan`, ray counts and
  measurement age). A rendered contribution reports the result of its upstream
  light. `[SourceVisibility] Enabled=0` leaves it `unknown` / `disabled`; original
  records and RGB smoothing never depend on it.
- Player/camera telemetry defaults to 60 Hz; native light capture defaults to 20 Hz. Faster API polling does not create additional GPU samples.
- The arrays **overlap**; do not add them together. One physical lamp can produce several contributions.

See [API fields, examples and freshness rules](docs/API.md) and the [client examples](examples). This is a local data API, not an API for controlling the game.

### Running from source

For development, install the .NET 8 SDK:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- diagnose
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- track 0 60 --lights > telemetry.jsonl
```

The external host alone can read player/camera and supported authored lights. **Filtered rendered lights require the unified ASI in the game.** Do not start a second host on the same port as the ASI-owned one. Status messages go to stderr; JSON Lines go to stdout.

## Compatibility and limits

The new package targets **Crimson Desert patch 2.03.02** on Steam for Windows.
Technical identity: EXE file version `1.0.0.2976`, Steam build ID `25477059`,
and the exact validated EXE SHA-256. The file version is not the public game
patch version. Older
player/camera profiles remain preserved; they do not establish compatibility of
the current native package with an older or unknown game executable.

Native capture validates the EXE, hook instructions and surrounding caller/binding contexts before instrumentation. Shared build contracts and the read-only command below help recover after updates:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- check-update 'C:\path\to\CrimsonDesert.exe'
```

The report **does not enable an unknown build**. Historical basic-telemetry layouts have guarded automatic relocation, but the current direct-camera/ManyLights path is not automatically promoted. Future engine or shader-only asset changes can still require an update. See the [update recovery reference](docs/UPDATE_RECOVERY.md).

Important boundaries:

- `lights.rendered` is current **filtered renderer data**. `lights.upstream` covers every current record the GPU light pass receives, but is not a persistent lamp registry either. Sun, sky, emissive surfaces and every possible effect are not all covered.
- A missing contribution is **not** a permanent physical OFF state. Stable lamp IDs, physical lumens and validated light ranges are not supplied.
- Linear HDR RGB/luminance can vary with effects and exposure; they are not final screen pixels or exposure-normalized lamp colors.
- With `Upstream=1` the radar shows all current engine lights around the player: filled dots are renderer-selected, hollow rings are current lights the renderer did not select in this view (not OFF). Without it, the radar only shows what the renderer selected.
- Visibility rays sample fixed points on the camera-to-light path. Thin gaps can make a light behind a fence or cage `clear`, and geometry without collision (foliage, some decorations) does not block. Results can be up to 500 ms old; fast motion can expose capture/projection latency. Visibility does not establish correct candles, lamps or a complete list of all sources.
- HDR UI is composited in linear light, with configurable white brightness and unchanged pixels outside the UI. It does not tone-map the whole scene. Rendering HDR UI uses two extra full-resolution GPU textures plus a scene copy/composite; the SDR path has no extra compositor pass.
- Unrecognized output format/color-space combinations remain unsupported. Automated HDR rendering tests do not establish live HDR game or display compatibility; frame generation, other upscalers and AMD/Intel game setups remain unvalidated.
- Earlier versions sporadically hung the GPU at world entry in a setup with ReShade. 2.2.4 no longer creates a Direct3D 12 queue there while any UI is enabled. Six test starts with and without ReShade were clean; that does not prove the issue is gone. Please report a recurrence with the four logs listed above.
- The external host reads process memory. The unified ASI uses guarded renderer hooks, GPU copies and optional UI hooks; the full system is **not purely read-only instrumentation**. The console that can change debug values is available only in a separate `CDT_RESEARCH=ON` build.

## Build, test and contribute

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet run --project .\tests\CrimsonDesertTelemetry.Tests -c Release
.\scripts\Build-ModManagerPackage.ps1 -Version 0.0.0-local.1 -Research off
ctest --test-dir build/native-package-release -C Release --output-on-failure
```

The native build requires Visual Studio C++/Windows SDK/CMake; see [local tooling](docs/TOOLING.md) for executable paths. Choose an unused local test version: package builds refuse to overwrite an existing versioned ZIP.

The production package uses `CDT_RESEARCH=OFF`; bounded ambient acquisition and
the continuous physics visibility ray fan are included.
Repeated captures, history, dumps, diagnostic tracing and the broad player/all-source
bridge stay excluded. `-Research on` selects
[CrimsonDesertTelemetry.research.ini](packaging/mod-manager/CrimsonDesertTelemetry.research.ini)
under the normal packaged filename. See [INI validation](docs/INI_VALIDATION.md).

GitHub CI covers managed/API tests and native capture, guard and UI tests. Current acceptance and outstanding checks are recorded in [the handover](docs/HANDOVER.md); historical test totals are not a result for a new package.

See [contributing](CONTRIBUTING.md), [research provenance](docs/PROVENANCE.md), [current release notes](docs/releases/v2.2.4.md), [2.0 release notes](docs/releases/v2.0.0.md), [Nexus publishing](docs/NEXUS_PUBLISHING.md) and [current public description drafts](docs/PUBLIC_DESCRIPTIONS.md). Game binaries, memory dumps, private captures and third-party checkout directories do not belong in the public repository.

## Credits and scope

Created and maintained by [ZappendusterFX](https://github.com/ZappendusterFX), developed with **Claude** and **Codex (OpenAI)**. Runtime third-party components are credited in [provenance](docs/PROVENANCE.md) and the package's `THIRD-PARTY-NOTICES.txt`.

Thanks to [Moon-yungg and the World Builder project](https://github.com/Moon-yungg/crimson-desert-world-builder) for contributing [automatic direct-camera compatibility](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/pull/2) and sharing engine research that informs ongoing work.

This unofficial community project is not affiliated with or endorsed by Pearl Abyss. Use it only where game terms and applicable restrictions permit; no anti-cheat bypass or competitive advantage is provided. Source is licensed under the [MIT License](LICENSE).
