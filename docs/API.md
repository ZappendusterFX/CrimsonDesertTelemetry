# API reference

The API carries raw light contributions, a separate [grouped and smoothed
local-light feed](SMOOTHED_LIGHTS.md), player/camera poses, and an independent
[ambient feed](AMBIENT_STREAM.md). Product versions, route versions and JSON
schema versions are separate. Routes remain **v1**; the current development host
uses schema **1.6** with lights enabled and **1.1** with lights disabled. Release
2.2.0 uses schema 1.6 and attaches physics `sourceVisibility` to upstream and
rendered lights by default (public 2.1.11 used 1.5 with visibility disabled).
Schema 1.6 adds [`lights.upstream`](#current-engine-lights-before-view-selection-schema-16):
all current engine lights, including those behind the camera.

## Current development scope — 2026-09-12

Camera-local Ambient Occlusion passed a controlled open/enclosed/open route in
the OFF v2.1.9 package on game build 25246367. `/v1/ambient` carries global sky,
the camera's independent `visibility` value/frame/age, and
`localEnvironmentAmbientEstimateWorking`. Global sky RGB itself is unoccluded;
the product estimate is not measured room brightness or physical illumination.

Public production 2.1.11 excludes per-light geometric visibility after its failed
live acceptance. Current `CDT_RESEARCH=OFF` source restores the earlier narrow
camera-to-rendered-source classifier that previously worked live for fires. Its
new production package and exact binaries still require live and antivirus
acceptance. The broader player-to-authored-and-rendered-source bridge remains
isolated under `CDT_RESEARCH=ON`. Original records, raw RGB/luminance, grouping and
EMA RGB remain unchanged; no source is removed or attenuated by the API.

See [source visibility](SOURCE_VISIBILITY.md) for the exact classifier, bounds,
offline evidence and required live controls. Older packages may omit these fields;
legacy native v2 captures remain readable. See [release validation](STABLE_RELEASE_VALIDATION.md)
for the stable package's completed test record.

## Historical published validation

The installed 2.0.0 package passed a fresh-start, progressing live-data check on
exact Steam build 25116796. Earlier native-capture checks covered cold start,
camera movement and a physical lamp A-B-A experiment. These establish the tested
paths, not complete lighting coverage or compatibility with every hardware setup.

## Connect

The ASI package starts a separate local telemetry host. Your mod or application
connects to that host; it does not need to load a .NET assembly into the game,
install the HUD, or implement a memory reader. The supported integration boundary
is HTTP/WebSocket/JSON Lines, not a native C ABI or an in-process callback API.

Default addresses:

- HTTP: `http://127.0.0.1:27311`
- WebSocket: `ws://127.0.0.1:27311/v1/stream`

The server listens on IPv4 loopback only. There is no API key, authentication,
TLS, game-control endpoint or runtime-configuration endpoint. Do not expose it to
the network through a proxy. CORS/origin checks are not authentication of local apps.

Configure the ASI before starting the game. These settings describe the 2.2.0
production template:

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
Enabled=1

[Overlay]
Enabled=1
InitiallyVisible=1
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

[Notifications]
Enabled=1
DurationMilliseconds=6000
```

`Port` supports 1024–65535; `SampleRateHz` supports 1–240. `NearbyRadius` supports
1–100000 game units and `ManyLightsSampleRateHz` supports 1–60. The ASI clamps values
outside those ranges. Changes require a game restart. Keep the `.cfg` runtime
metadata unchanged; it is not user configuration. The default shortcuts are F8
for the corner HUD/radar, F9 for diagnostics, F10 for fullscreen markers and F11
for display-only hiding of freshly blocked sources. All four keys can be reassigned
using decimal Windows virtual-key codes, or individually disabled with `0`. No API
record or RGB is filtered.
Without source visibility, display radius does not expand API source coverage or
its configured nearby radius.

Since 2.2.0, production `[SourceVisibility] Enabled=1` (research profile:
`[Experimental] PhysicsVisibility=1`) runs the physics ray fan. Then
`[LightOverlay] Radius` (1..500 game units) is shared by HUD and visibility target
selection around the player; rays still originate at the paired camera. The host
raises its API capture radius to at least this value, without shrinking a larger
`[Lights] NearbyRadius`. Restart after edits. This does not create renderer-missing
lights or guarantee collision coverage at long range. The 2.2.0 template uses 100.
The CLI equivalent is `--physics-visibility --physics-visibility-radius 100`.

`ShowAmbient=1` makes F9 diagnostics poll the existing `/v1/ambient` endpoint and
display global sky, camera sky visibility and the local estimate with their separate
ages. Ambient requires native ManyLights capture. Current `CDT_RESEARCH=OFF`
accepts Upstream, SourceVisibility, HideOccluded and OcclusionToggleKey, but ignores Research,
Console, Explorer and legacy `OcclusionTest` controls left in an old INI. Those
settings cannot enable the broad research architecture.
`CDT_RESEARCH=ON` preserves the experimental paths and uses a separate
`CrimsonDesertTelemetry.research.ini` template, packaged as the normal INI filename.
See [configuration dependencies and validation](INI_VALIDATION.md).

Public production 2.1.11 published additive rendered `sourceVisibility` as
`unknown` with reason `disabled`. Release 2.2.0 attaches physics `physics-ray-fan`
results (or an explicit `unknown` with a reason) to upstream and rendered records;
`[SourceVisibility] Enabled=0` restores `unknown` / `disabled`. The schema 1.5
research host separately attaches player-to-source results to authored and rendered
records through the independent bridge described in
[source visibility](SOURCE_VISIBILITY.md).

Normal startup/loading/discovery notices are silent. Success appears once the API
reports `playing` and the requested feeds are fresh; a valid empty light feed
counts as ready. This can happen during the game's visible loading sequence.
The configured 6000 ms duration is clamped to 5000–10000 ms. Local bootstrap/native
faults and host errors can appear before valid game data or a working host exists;
explicitly disabling notifications suppresses them. Drawing supports D3D12 SDR,
HDR10 and scRGB output as described below, including notification-only operation.
Consult logs if the output combination is unsupported or graphics initialization
fails. `[Overlay] HdrPaperWhiteNits=200` controls all HDR UI white brightness,
clamped to 80–500 nits, even when the corner HUD is disabled. It changes no API
values, game HDR settings or metadata. A live HDR-output game run remains unvalidated.

Disable `[Overlay] Enabled`, `[LightOverlay] Enabled` and `[Notifications] Enabled`
to skip all UI hooks/client. Missing UI sections/keys default to disabled, whereas
the packaged INI explicitly enables them. Lighting has independent `[Lights]`
switches: hiding/disabling the views does not stop capture. The external host
reads memory, while native ManyLights capture uses guarded renderer hooks and GPU
readback in the ASI. It is an instrumented path, not an untouched baseline.
Live validation with all production features enabled remains pending. Automatic
HUD hiding in every game menu is not implemented; `playing`/`loading` remains a
telemetry availability contract, not a menu-state API.

Alternatively, from a source checkout with the .NET 8 SDK:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights --light-radius 100
```

The CLI rejects out-of-range arguments instead of clamping them. Do not start a
second host on a port already used by the ASI. A manually started `serve` process
can wait for the game and survive game restarts. An ASI-owned host normally exits
with the game: clients must also handle a disconnected socket, not only a `stopped`
message. A final state message is not guaranteed before process termination.

`--lights` enables light reading in the host; rendered light data also requires
the running ASI's supported native capture bridge. A standalone CLI does not
install that renderer instrumentation.

## HTTP endpoints

GET responses are JSON (`application/json`), except the schema's
`application/schema+json` content type and the plain-text WebSocket error below.
Responses have camelCase keys. There are no mandatory custom request headers.

| Method and path | Response |
|---|---|
| `GET /` | 200: object containing `name`, `schemaVersion` and an `endpoints` array. |
| `GET /v1/health` | 200: health object, even when the game is absent or unsupported. |
| `GET /v1/snapshot` | 200: latest published snapshot, including a `loading` or `stopped` snapshot. 503: health object if **no snapshot has been published yet**. |
| `GET /v1/schema` | 200: embedded [JSON Schema](../schema/telemetry-v1.schema.json), draft 2020-12, for snapshot payloads. It does not describe health responses. |
| `GET /v1/stream` with WebSocket upgrade | 101: WebSocket connection. 400 without an upgrade (`A WebSocket connection is required.`). 403 for a supplied, disallowed `Origin`. |

There is no pagination, history query, replay cursor, per-client rate parameter or
delta format. Repeated snapshot GETs can return the same `sequence`.

**HTTP 200 is not proof of live gameplay data.** Check the payload's state and
timestamp. The snapshot endpoint retains the last published object; a health-only
change (for example `error`) does not clear it. Use health and a freshness timeout
as well. A missing host results in a connection failure, not an HTTP 503.

### Health response

```json
{
  "schemaVersion": "1.1",
  "status": "playing",
  "gameRunning": true,
  "supportedBuild": true,
  "gameBuild": "24994088",
  "sampleRateHz": 60,
  "lastSequence": 42,
  "lastCapture": "2026-08-31T18:00:00Z",
  "connectedClients": 1,
  "discoveredCopies": 1,
  "discoveryMilliseconds": 1.64,
  "error": null,
  "compatibility": {
    "mode": "tested",
    "executableSha256": "B596A498701DFCDC49C486D890C42755DABC8C174314C7F26F7329394452446D",
    "executableVersion": "1.0.0.2658",
    "referenceBuild": "24994088"
  }
}
```

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | `1.1` with lights disabled; additive `1.5` when the current development light module is requested. Stable 2.1.10 emits 1.4. |
| `status` | string | Sampler status; see below. |
| `gameRunning` | boolean | Sampler's current process-presence observation, not a guarantee of playable data. |
| `supportedBuild` | boolean or null | `null` before support is known; `false` when compatibility checks rejected the executable; `true` after a runtime was opened through either compatibility mode. On `error`, do not interpret it as successful live-data validation. |
| `gameBuild` | string or null | Installed Steam build ID when available, not the telemetry product version. `"unknown"` can appear in snapshots if an automatically recognized installation has no readable manifest. Null means no runtime is currently identified. |
| `sampleRateHz` | integer | Configured target rate, not a measured delivery rate. |
| `lastSequence` | integer or null | Last published snapshot sequence, including unavailable-state snapshots; null before the first publication. |
| `lastCapture` | date-time string or null | Timestamp of that publication. Retained across health-only updates. |
| `connectedClients` | integer | Active WebSocket subscriptions, including the HUD when enabled. HTTP polling is not counted. |
| `discoveredCopies` | integer | Current native reader reports one source when resolved; zero before resolution/reset. Historical name, not a current heap-scan count. |
| `discoveryMilliseconds` | number or null | Reference-resolution time in milliseconds; not total game startup, executable hashing time or per-sample latency. |
| `error` | string or null | Human-readable diagnostic; wording is not a stable machine-readable error code. |
| `compatibility` | object or null | Executable identity and selection result for the current runtime; null before selection or after rejection/exit. See below. |

`compatibility.mode` is `"tested"` only for an exact, locally validated executable
SHA-256. `"automatic"` means a different executable was accepted by unique code,
data-section, player-type and multi-slot object-table guards using `referenceBuild`
as its layout. It does **not** mean that executable received a manual in-game test.
`executableSha256` always describes the installed EXE; `executableVersion` can be
null. This health-only object is additive metadata and is not part of snapshot schema 1.1.

Automatic relocation covers historical player/camera reference layouts. It does
not enable native lighting on an unknown EXE, and the current direct camera/scene
layout has no automatic promotion path. `check-update <exe>` reports offline
anchor/recovery results without enabling a candidate; see
[update recovery](UPDATE_RECOVERY.md).

Possible `status` values:

| Status | Interpretation |
|---|---|
| `waiting-for-game` | No usable runtime is open; retrying. Initially the latest snapshot is absent. |
| `unsupported-build` | Executable failed exact identification and automatic compatibility checks; no guessed offsets are used. |
| `discovering` | Supported runtime is open and camera sampling is starting; can be very brief. |
| `playing` | The last publication contained validated player position and camera data. |
| `loading` | Required data is unavailable or failed validation; see the important distinction below. |
| `stopped` | A game-exit snapshot was published. Can quickly change to `waiting-for-game`. |
| `error` | An outer sampler operation failed; consult `error` and wait for recovery. |

`loading` is a **telemetry availability state**, not a reverse-engineered loading-screen
flag. Loading screens, changing objects, invalid reads and a stalled camera render
context can all produce it. It must not be used as proof that a particular game UI
or worldspace transition is occurring. Statuses may be skipped between polls.
Conversely, `playing` data may arrive while the loading screen is still visible.
Returning to the title screen can retain HUD/data for several seconds until stale
or another load. This accepted behavior is not a reliable title-screen detector.

## Snapshot contract

The same snapshot shape is used by HTTP 200 responses, WebSocket text messages,
and successful CLI `snapshot`/`track` output. The following examples are synthetic.

```json
{
  "schemaVersion": "1.1",
  "sequence": 42,
  "capturedAt": "2026-08-31T18:00:00Z",
  "game": { "build": "24994088", "state": "playing" },
  "coordinateSystem": { "unit": "game-unit", "handedness": "right", "upAxis": "y" },
  "capabilities": ["player.position", "camera.transform", "camera.projection", "player.orientation"],
  "player": {
    "position": { "x": 0, "y": 0, "z": 0 },
    "orientation": {
      "source": "player-physics-root",
      "forward": { "x": 0, "y": 0, "z": 1 },
      "up": { "x": 0, "y": 1, "z": 0 },
      "headingDegrees": 0
    }
  },
  "camera": {
    "position": { "x": 0, "y": 2, "z": -5 },
    "up": { "x": 0, "y": 1, "z": 0 },
    "right": { "x": 1, "y": 0, "z": 0 },
    "forward": { "x": 0, "y": 0, "z": 1 },
    "nearPlane": 0.2,
    "farPlane": null,
    "verticalFovDegrees": 50,
    "aspectRatio": 1.7777778
  },
  "quality": {
    "consensusCopies": 1,
    "validCopies": 1,
    "distinctStates": 1,
    "rediscovered": false,
    "captureDurationMicroseconds": 128
  }
}
```

### Envelope and availability

All top-level fields in the 1.1 example are required. Schemas 1.2–1.6 additionally
require `lights`; 1.4 to 1.6 also require `lights.rendered`. Only 1.6 may carry
`lights.upstream`, and only when the input stream is configured. `vector3` below means an object
with three required, finite JSON numbers: `x`, `y`, `z`. There are no string-encoded
numbers, NaN values, addresses, process handles or memory blobs in this contract.

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | `1.1` without lights or `1.6` with current development light telemetry. Stable 2.1.10 emits 1.4, 2.1.11 emits 1.5. Compare as strings/components, not floating-point numbers. |
| `sequence` | nonnegative integer | Publication counter in this host/command invocation, starting at zero. Not an engine frame number. It continues across game restarts if the same host survives, and resets when a new host starts. |
| `capturedAt` | date-time string | Sampling timestamp with a UTC offset, currently emitted in UTC. Not an engine timestamp or time elapsed since game launch. |
| `game.build` | string | Supported game's Steam build ID. |
| `game.state` | string | Exactly `playing`, `loading` or `stopped`; health has additional statuses. |
| `coordinateSystem` | object | Currently `unit=game-unit`, `handedness=right`, `upAxis=y`. |
| `capabilities` | array of unique strings | Recognized capabilities for this payload; see below. Ordering is not meaningful. |
| `player` | object or null | Null when required telemetry is unavailable. |
| `camera` | object or null | Null when required telemetry is unavailable. |
| `quality` | object or null | Null when required telemetry is unavailable. |
| `lights` | object (schema 1.2+) | Current nearby engine-light result and diagnostics; never emitted in schema 1.1. |

Currently known capabilities:

- `player.position`: world position.
- `camera.transform`: camera position and basis vectors.
- `camera.projection`: near plane, vertical FOV, aspect ratio; **not** a promise of a known far plane.
- `player.orientation`: validated player physics-root orientation for this sample.
- `lights.engine`: the exact-build Engine-Light reader is available. Its presence
  does not mean `lights.status` is currently `available`.
- `lights.rendered`: this exact build has a supported native filtered-ManyLights bridge;
  require `lights.rendered.status == "available"` before consuming its sources.
- `lights.upstream`: the bridge carries the paired ManyLights input (`[Lights] Upstream=1`);
  require `lights.upstream.status == "available"` before consuming its sources.

Important: `loading`/`stopped` messages still list the three base capabilities, but
their `player`, `camera`, and `quality` fields are null. Capabilities alone are not
a validity check. Require `game.state == "playing"` and the objects you need.

### Authored engine lights

The authored reader uses an exact validated build profile, including the current
`25246367` profile and preserved older definitions. An unsupported profile produces
`lights.status="unavailable"` with reason `unsupported-build`; it never guesses
offsets. A changing or unreadable scene walk likewise publishes no source array.

An available result has `status="available"`, `source="engine-light-source-array"`,
the configured `nearbyRadius`, a possibly empty `sources` array and diagnostics.
Each source can contain:

| Field | Meaning |
|---|---|
| `position` | Validated world position in the same game coordinates as the player. |
| `kind` | `point` or `spot` only when the engine encoding is recognized; otherwise omitted. |
| `direction` | Normalized world-space emission direction for a `spot`, obtained by rotating local +Z with its validated transform quaternion. Omitted for point lights and if the quaternion is invalid. |
| `coneHalfAngleDegrees` | Recognized spotlight cone half-angle; omitted for point lights. |
| `colorLinear` | Engine record's emitted linear RGB base color. |
| `recordActive` | Record-maintenance flag; **not** a promise that the light is visibly emitting. |
| `rendererSelected` | Whether this record is selected for the mapped renderer path. |
| `rendererScale`, `rendererRgbLinear` | Both present only for a selected record with a positive finite scalar. The scalar is authored renderer data, not physical lumens; RGB is `colorLinear * rendererScale`. |
| `sourceVisibility` | Optional schema 1.5 research metadata for the player-to-source geometric test. Camera direction and screen projection are absent. See [complete contract](SOURCE_VISIBILITY.md). |

No address, durable ID, range, lumens, generic `enabled` field or fire/effect
source is exposed. Array order and engine handles must not be used as
identity across snapshots or restarts. Multiple records at one position are retained.

Diagnostics distinguish malformed records, valid records outside the configured
radius, unknown kind encodings, unavailable renderer fields and non-positive renderer
scales. `walkChanged`, `walkRetrySucceeded` and `walkUnavailable` are cumulative for
the current game runtime. The reader resolves the profile's scene/array walk afresh,
checks its identity/backlinks and vector bounds, copies the contiguous records, then resolves
the complete walk again. One complete retry is allowed; no previous light snapshot
is reused after failure. This protects against scene transitions but is not an
engine-frame atomicity guarantee.

### Current rendered light contributions (schema 1.4/1.5)

`lights.rendered` is independent of the authored reader's availability. It has
`status`, `source="filtered-manylights"`, and `diagnostics`. An available sample
also has `captureSequence`, the captured engine `frameNumber`, `capturedAt`,
`ageMilliseconds`, its paired `camera`, and `sources` (possibly empty). Camera-relative
GPU positions are converted using **this paired camera**, not the envelope's newer camera.
The sample is published only after the GPU fence completes; samples older than 500 ms
are unavailable. The default native capture rate is 20 Hz, independent of API publication.
From 1.3.0-preview.3, lights and their GPU valid-prefix count are copied in the same
command list and completed by the same fence. Only that prefix is decoded: retained
capacity-tail records are not current lights, even when their fields look plausible.
Preview.1/2 lacked this bound and could publish camera-attached ghost contributions.

| Contribution field | Meaning |
|---|---|
| `sampleIndex` | Index within this GPU sample, **not an object ID**. |
| `position` | World position in the same coordinates as the player. |
| `colorLinear` | Current linear HDR renderer RGB, which can vary with effects and exposure. Not sRGB, lumens or an exposure-normalized source intensity. |
| `luminanceLinear` | Derived RGB luminance using coefficients 0.212671, 0.71516, 0.07216. Not physical brightness. |
| `kind` | Recognized `point` or `spot`; otherwise omitted. |
| `direction`, `coneHalfAngleDegrees` | Spotlight emission direction and cone; invalid/unknown direction is omitted. Points have no direction. |
| `sourceVisibility` | Optional geometric measurement: `clear`/`blocked`/`unknown`. The physics path (production default since 2.2.0) reports the latest complete `physics-ray-fan` result immediately, without motion invalidation or confirmation delay, expiring after 500 ms. A newly moved target near a recent blocker can be `unknown` with reason `pending-near-recent-blocker`; this is a HUD presentation hint, **not** a measured blocked verdict or a durable source ID. Includes sampled path counts, measured camera/capture/age and optional `measurementSequence` / `measuredAtTickMilliseconds` (Windows uptime, not Unix time). This is a measured-pose result, not guaranteed current-pose visibility or optical transmission %. Raw light records stay intact. Legacy SDF metadata remains separate. See [complete contract](SOURCE_VISIBILITY.md). |

Diagnostics count active records, published records, malformed records and records
outside `lights.nearbyRadius`. Unavailable results omit sources/camera/timing and
include `unavailableReason`, for example `bridge-missing`, `bridge-waiting`,
`bridge-stale`, `native-fault`, `legacy-plugin-conflict`, or `unsupported-build`.
The bridge validates protocol, PID **and process creation time**, sequence, dimensions,
camera frame, paired-counter bounds and completion flags. No previous successful
result is relabelled as fresh. An invalid counter makes rendered lights unavailable;
the reader does not fall back to scanning the entire allocation.

Use rendered contributions as measured inputs to a local-lighting consumer and
authored records for their separate base/selection metadata. Renderer RGB alone
does not predict final scene pixels or a physical lamp's output. The arrays
overlap: **do not sum both feeds**.
One physical flame can contribute multiple records. Absence does not distinguish
OFF from culling/unloaded sources; no generic enabled flag or permanent identity is
invented. Sun/sky, emissive surfaces and every other possible lighting path are not
claimed covered. The unified native path is exact-build gated, unlike the basic
player/camera compatibility resolver.

### Current engine lights before view selection (schema 1.6)

`lights.rendered` is what the renderer selected for the current view: its GPU pass
tests range, frustum, depth (HiZ) and brightness first, so lights behind the camera
or out of view are missing. `lights.upstream` is that pass's **input**, copied in the
same command list and completed by the same fence as the rendered sample. It has
the same `captureSequence`, `frameNumber`, `capturedAt` and `ageMilliseconds`, the
same 500 ms freshness limit and the same player-centred `nearbyRadius`. Present only
when `[Lights] Upstream=1`; exact-build input anchors are verified before use.

Records are decoded with the engine shader's own rules. Only the consumer counter's
current records are read, never the retained tail. An engine **group** (for
example the flame particles of one fire bowl) becomes one light: its RGB is the
member sum and its position the member mean. The renderer instead picks a noisy,
brightness-weighted member each frame, so group positions are derived, not the
renderer's exact choice. RGB uses the renderer's own 5% luminance floor and colour
matrix, so a selected light equals its filtered contribution.

| Light field | Meaning |
|---|---|
| `sampleIndex` | Header/record slot in this capture, **not an object ID**; slots change every frame. |
| `type` | `group` or `standalone`. |
| `memberCount` | Group members summed into this light (groups only). |
| `position`, `colorLinear`, `luminanceLinear` | World position (group: member mean) and renderer-space RGB/luminance, as for rendered contributions. |
| `kind`, `direction`, `coneHalfAngleDegrees` | As for rendered contributions. |
| `rendererSelected` | Whether this capture's `lights.rendered` contains the same contribution. `false` is **not** OFF and not occluded: the renderer did not select it for this view. |
| `renderedSampleIndex` | The paired `lights.rendered` `sampleIndex` when selected. |
| `sourceVisibility` | As for rendered contributions. Physics targets are these lights, including behind the camera; a selected rendered contribution reports its input light's result. |

`inputRecords` is the number of current input records at every distance. The
diagnostics count groups and members, standalone and global records, member records
skipped as standalone lights, zero-colour and malformed records, records outside the
radius, published and renderer-selected lights, and `renderedUnmatched`: rendered
contributions within the radius without an input partner. Records with negative
RGB are exposure-dependent specials. The engine stores their x/y in view space, so
they are **excluded** (`specialExcluded`) rather than published at a false position.
Unavailable reasons include the rendered ones plus `input-unavailable` (not copied
in this sample), `input-refused` (anchors or device support did not verify) and
`input-invalid`. The input never makes `lights.rendered` unavailable.

The feed covers every current record the GPU light pass receives; it is not a
persistent lamp registry. Whether the engine skips distant emitters or lights before
uploading them is not established.

```json
{
  "schemaVersion": "1.1",
  "sequence": 43,
  "capturedAt": "2026-08-31T18:00:01Z",
  "game": { "build": "24994088", "state": "loading" },
  "coordinateSystem": { "unit": "game-unit", "handedness": "right", "upAxis": "y" },
  "capabilities": ["player.position", "camera.transform", "camera.projection"],
  "player": null,
  "camera": null,
  "quality": null
}
```

### Player

| Field | Type | Meaning |
|---|---|---|
| `player.position` | vector3 | Authoritative world position in game units. Shared coordinate space with `camera.position`; not the physics object's rebased local position. |
| `player.orientation` | object or null; schema also allows omission | Can be unavailable while position/camera remain valid. Current publisher emits null and omits the capability when validation fails. |
| `orientation.source` | string | `player-physics-root`. Root/physics facing, not animated body, bones, aiming direction or camera facing. |
| `orientation.forward` | vector3 | Validated forward unit vector in world axes, already sign-corrected by the reader; do not negate it again. |
| `orientation.up` | vector3 | Validated root up unit vector in world axes. |
| `orientation.headingDegrees` | number or null | Heading in `[0,360)`. Null if the forward vector's XZ length is less than 0.1; the orientation itself may still be valid. |

Do not retain a previous orientation when the field is null/missing or the
`player.orientation` capability is absent. Rolls and gliding animations can occur
while the physics root stays upright. No animated pitch/roll or quaternion is
published. A zero coordinate or a heading of zero is valid, not missing data.

### Camera and quality

| Field | Type | Meaning |
|---|---|---|
| `camera.position` | vector3 | Native render-camera world position in game units. |
| `camera.up`, `.right`, `.forward` | vector3 each | World-space basis vectors; approximately unit length and mutually orthogonal, with `cross(right, up) ≈ forward`. Independent of player facing. |
| `camera.nearPlane` | positive number | Near distance in game units. |
| `camera.farPlane` | positive number or null | Always null in the current native reader: unverified/unknown. **Do not interpret null as a validated infinite far plane.** |
| `camera.verticalFovDegrees` | positive number | Full vertical perspective field of view in degrees, not radians, horizontal FOV or half-angle. Schema permits up to 180; the reader uses stricter plausibility limits. |
| `camera.aspectRatio` | positive number | Projection width divided by height, not a pixel resolution. |
| `quality.consensusCopies`, `.validCopies`, `.distinctStates` | positive integers | Each is 1 for the current single native source. Historical names retained for compatibility, not confidence scores or frame ages. |
| `quality.rediscovered` | boolean | Whether the resolved camera object chain changed relative to the previous accepted chain; normally false on the first capture. |
| `quality.captureDurationMicroseconds` | nonnegative integer | Read/validation duration, excluding transport and client processing. CLI one-shot `snapshot` currently reports 0 rather than measuring it. |

## Coordinate recipes

Positions share world axes with Y up. A game-unit-to-meter conversion and mapping
of X/Z onto compass north/east have **not** been established. Worldspace/scene ID
is not available, so do not infer it from altitude alone.

For either published forward vector `f`, horizontal heading is:

```text
heading = ((atan2(f.x, f.z) * 180 / pi) % 360 + 360) % 360
```

This gives **+Z = 0°, +X = 90°, -Z = 180°, -X = 270°**. Ignore heading near a
vertical forward vector. When deriving camera pitch, use
`atan2(f.y, sqrt(f.x*f.x + f.z*f.z)) * 180/pi`; positive pitch points upward.
Camera Euler angles are derived conveniences, not additional API fields.

To express a world-space point `p` in the published camera basis:

```text
d = p - camera.position
localX = dot(d, camera.right)
localY = dot(d, camera.up)
localZ = dot(d, camera.forward)
```

These are basis coordinates, not screen pixels. When building an engine-specific
view matrix/quaternion, explicitly account for row/column order, handedness and
whether that engine uses +Z or -Z for forward. No matrix-storage convention is
implicitly specified by this vector API. Horizontal FOV can be derived as
`2 * atan(tan(verticalFovRadians / 2) * aspectRatio)` in radians.

## WebSocket delivery and freshness

Connect without a subscription message or subprotocol. The host sends one full
snapshot per UTF-8 text message, not newline-separated records within a message.
Low-level WebSocket libraries must assemble fragments through end-of-message before
JSON parsing. Client text/binary messages are ignored; they do not control the game
or sampling rate. There is no application-level ping command or acknowledgement.

- A new connection receives the last published snapshot if one exists, then new
  publications. That first snapshot can already be old or unavailable. With no
  publications yet, the socket can open successfully and remain silent.
- All clients share one sampler. There is a **one-item, drop-oldest queue per
  subscriber**. Slow clients skip samples; this is a latest-state feed, not a
  lossless recording channel. Already-sent transport buffers can still add latency.
- Do not assume exactly-once delivery: the initial snapshot can overlap with the
  subscription queue. Ignore duplicate/non-increasing sequence values within a
  connection, and reset that comparison when reconnecting to a potentially new host.
- Stationary positions still produce new sequences while the render context advances.
  Sampling faster than rendering can publish the same transform more than once.
- Unavailable-state messages are generally emitted on transition, not continuously
  at the configured rate. A quiet socket does not establish fresh gameplay data.
- The native reader rejects a render-context counter stalled for one second.
  This is separate from your own transport freshness deadline. Double reads and
  pointer checks reduce torn reads; they do not guarantee an atomic engine frame.

Recommended client behavior:

1. Start unavailable. Check schema major version, state, required objects and
   required capabilities before consuming a snapshot.
2. Check `capturedAt` as well as local time since the last **new** sequence. Repeated
   HTTP responses or duplicate messages must not reset a freshness watchdog.
3. Clear dependent output on unavailable data, stale timestamps, socket closure or
   relevant health errors. Do not keep driving a device from the last known transform.
4. Choose a timeout suited to the configured rate (for example
   `max(1500 ms, 3 * 1000 / sampleRateHz)`), and use a monotonic local clock for silence
   detection. Wall-clock changes can affect comparisons with `capturedAt`.
5. Reconnect with a bounded delay when the host restarts. There is no resume/replay.

The 60 Hz default and 240 Hz limit are target polling rates, **not** hard real-time
guarantees, promised FPS, or a promise to capture every rendered state.

## Working examples

- [Python HTTP example](../examples/http_snapshot.py): Python 3.10+, standard library
  only; checks health, distinguishes HTTP 503 health bodies, validates freshness,
  and handles optional orientation. Run `python examples/http_snapshot.py`.
- [JavaScript WebSocket example](../examples/websocket_client.mjs): Node.js 22+,
  built-in WebSocket, no packages. Run `node examples/websocket_client.mjs`.
  It reconnects, rejects unavailable/old/duplicate samples, and clears state after
  a silence timeout. Its callback prints a small display summary, not the public
  JSON Lines contract. Adapt the timeout for low sample rates.

Both accept an optional first argument overriding their default HTTP/WS URL.
They demonstrate consuming the API; they do not launch the host or change game data.
Example regression checks can be run without the game using
`node --test tests/api_examples.test.mjs` (Python must be on PATH, or set `CDT_PYTHON`
to its executable). These check the examples, not overall game compatibility.

For a browser client, serve your page from a loopback origin such as
`http://127.0.0.1:8080`. HTTP responses allow loopback origins through CORS; other
origins do not receive that permission. WebSocket handshakes with a non-loopback
`Origin` receive 403. Desktop clients without an Origin header are accepted.
Opening an HTML file directly (`Origin: null`) is not supported by these checks.
Browser mixed-content/private-network rules may impose additional restrictions.

### CLI / JSON Lines

For clean redirected JSON output, build first, then invoke the host directly:

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet .\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.dll snapshot
dotnet .\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.dll track 600 60 > telemetry.jsonl
```

`track [samples] [hz]` defaults to `0 60`; 0 means unlimited, otherwise the sample
limit is 1–100000. Each stdout line is one complete snapshot; diagnostics go to
stderr. PowerShell redirection encoding varies by version; configure your reader
accordingly. Do not parse build-tool output as telemetry JSON.

Unlike `serve`, `snapshot` and `track` require valid game data immediately and
can terminate on a loading/read error. They do not reconnect or emit a continuous
loading-state stream. Exit codes: 0 success, 1 unexpected failure, 2 invalid command
arguments, 3 unavailable/invalid operation, 4 invalid data or unsupported build.
`diagnose` prints human-readable build checks; `discover` and `trace-camera-copies`
are diagnostics, not this public snapshot contract.

## Compatibility and limits

Ignore unknown optional fields/capability strings and handle null or missing
optional fields. The published schema is strict for the supported 1.1/1.2/1.3/1.4/1.5 shapes
(`additionalProperties: false`); do not use an old strict
schema to reject a future additive revision that your consumer can otherwise handle.
Reject an unsupported API/schema major version explicitly. Breaking contract
changes require a new major contract version. Product version numbers do not
imply the same API/schema number. Build offsets are not part of the public API.

Not included: complete global/emissive lighting, weather/time of day, durable worldspace
identifiers, bones, animated pose, event history or GPU timestamps. The current build's
full SceneConstantBuffer validates the camera independently of player distance; the
older indirect camera layouts retain their 50-game-unit distance guard.
The [documented game build](../README.md#compatibility-and-limits) is manually validated.
Historical player/camera layouts can accept other executable hashes only through
complete unique guarded resolution and continuing runtime validation; this is
reported as automatic compatibility. The current direct-layout/native lighting
path remains tied to its exact validated build and rejects unknown native hook
targets. An unchanged EXE hash does not detect every shader-only asset change;
no active-PSO/shader-identity gate is implemented.

Camera/light telemetry has no DLSS, Streamline or NVIDIA runtime dependency.
In-game validation used NVIDIA hardware; AMD/Intel game setups remain untested.
Native light capture and the external host/API do not gate telemetry on the
display's HDR output mode; a live HDR-output game run remains unvalidated.
The API's linear HDR RGB values describe renderer contributions independently
of the display mode.

The optional D3D12 UI automatically selects these declared output combinations:

| Buffer format | Declared color space | UI path |
|---|---|---|
| R8G8B8A8/B8G8R8A8 UNORM or R10G10B10A2 UNORM | Full-range G22/Rec.709 | SDR |
| R10G10B10A2 UNORM | Full-range PQ/Rec.2020 | HDR10 |
| R16G16B16A16 FLOAT | Full-range linear/Rec.709 | scRGB |

Other combinations remain unsupported. A 10-bit buffer alone does not establish
HDR; its declared color space matters. All views and notices share the selected
path and `HdrPaperWhiteNits` setting. UI colors retain their existing appearance,
then are decoded and composited over the game in linear light. HDR10 uses PQ
decode/encode and Rec.709-to-Rec.2020 UI conversion; scRGB uses 80 nits per linear
unit. Pixels without UI are preserved, with no whole-scene tone mapping.

HDR rendering uses two extra full-resolution GPU textures plus a scene copy and
composite while UI is drawn. SDR retains the existing direct rendering path.
Historical HDR validation passed 14 native tests, including three HDR paths and
transitions between SDR and scRGB on the same swapchain. This is not live
HDR-display/game validation or a current full-suite result. The HDR compositor
does not change API RGB values.
Frame-generation/coexistence coverage remains incomplete.
Marker projection uses the latest published camera, not a Present-synchronous
camera, and performs no screen-depth test. Optional HUD geometry hiding uses
fresh SDF metadata instead; missing, stale and unknown verdicts remain visible.
Its current production live acceptance is pending. Display swatches are a
visualization, not game tone mapping. Generic light-exposure normalization remains
unfinished. See [current compatibility reports](COMPATIBILITY_ISSUES.md) for the
separate DMM deployment and graphics-crash reports with unproven root causes.

## Troubleshooting

| Symptom | Check |
|---|---|
| Connection refused | Game/ASI host is running, `[Server] Enabled=1`, configured port, ASI loader and .NET 8 ASP.NET Core Runtime x64. Inspect bootstrap/host logs. |
| 503 snapshot response | Read the health object; no snapshot has been published yet. Do not parse it as a player/camera object. |
| 200 but null objects or old timestamp | Check `game.state`, health and freshness; wait for valid data, do not reuse old values. |
| `unsupported-build` | The exact hash was unknown and automatic checks failed. Record the health error, EXE hash/version and installed build ID when reporting it; do not bypass the checks. |
| Skipped sequences | Expected for slower clients; reduce client work or configured sampling rate. Not a lossless replay API. |
| Heading seems unrelated to animation | Root facing and camera facing are independent; neither describes all body/bone animations. Check the axis recipe. |
| Browser fails while a desktop client works | Check loopback Origin/CORS and browser security rules. Use a local HTTP origin, not a directly opened file. |
| Upgrade behaves inconsistently | Close the game; ensure DLLs and both `.cfg` metadata companions come from the same package. Preserve only customized INI settings. |
