# Per-light source visibility

**Current development, 2026-09-28:** with `HideOccluded=1`, the HUD/radar now
show only fresh `clear` results. Pending `unknown`, missing and stale results
are hidden along with `blocked`. The new `/v1/lights/visible` HTTP/WebSocket feed
filters the same way *before* grouping/smoothing; raw and existing unfiltered
streams preserve every record and its honest `unknown` status. This supersedes
the short-lived nearby-blocker presentation hint. Moving-torch live acceptance
is still pending; unknown hiding does not make the rays instantaneous.

**Release 2.2.0, 2026-09-26:** the continuous physics ray fan below is the
production feature, ON by default via `[SourceVisibility] Enabled=1` in the
`CDT_RESEARCH=OFF` build. Production runs only the continuous fan (same 9 rays,
256 targets, 20 batches/s, 2 ms budget, 500 ms expiry, exact-build hash check);
manual probes and research diagnostics stay ON-only, and OFF no longer selects
the SDF classifier. Targets are the upstream lights within `[LightOverlay]
Radius` (100 in the template). Owner live reports so far cover the private
physics.10 and 2.1.15-upstream.1 packages; the exact 2.2.0 ZIP is validated
separately in the handover. The limits below still apply: sampled collision,
not optical transmission; thin gaps and non-colliding geometry.

**Status update, 2026-09-26:** the owner subsequently reported near-live behavior
and a successful unchanged physics.10 run. Those observations supersede the older
"no live acceptance yet" build-time notes below, not all accuracy/performance
limits. This path remains experimental and is retained in private manylights.1.
Current investigation is missing upstream light coverage, not a new physics
classifier. See [CLAUDE_REENTRY_20260926.md](CLAUDE_REENTRY_20260926.md).

## Targets from the all-around engine input — 2026-09-26, Claude

With `[Lights] Upstream=1`, physics targets are the paired `lights.upstream`
lights (same capture sequence), so sources behind the camera are measured too.
A group's target is its derived member mean. A filtered contribution paired with an
input light (`renderedSampleIndex`) reports that light's result instead of chasing
its noisy per-frame member position; unpaired filtered contributions remain their
own targets. The classifier, the 9-ray fan, the 256-target cap, the 2 ms budget
and the 500 ms expiry are unchanged. More lights in the radius can lower each
light's refresh rate. Without an available paired input the previous rendered-only
selection applies. Managed regression: `UpstreamLightTests.PhysicsTargets`.

## Previous private implementation — physics.10: configurable shared radius

`[LightOverlay] Radius` now controls both HUD views AND physics target selection,
in a **player-centered** sphere, 1..500 game units. Rays still start at the actual
paired camera. Bootstrap uses the existing HUD float parser and passes the value
to the host as `--physics-visibility-radius`. When enabled, physics raises the
API capture radius to at least this value (never reduces a larger
`[Lights] NearbyRadius`). Hidden/disabled HUD views do not disable API sampling.
The private .10 package sets Radius=100; the generic template remains35/default-off.

Native validation allows the bounded500gu player sphere and12gu camera offset;
continuous fan construction allows512gu centers/513gu segments including offsets.
Manual diagnostic fan/segment bounds stay49.5/50gu. Same context, control ray,
buffer/lifetime/fault checks,256 targets,20 rounds/sec and2ms issue budget remain.
`outside-physics-radius` now distinguishes range from `outside-physics-budget`
(no cached target). The500ms expiry is unchanged. More lights may mean less
frequent per-light updates, not guaranteed20Hz each. Renderer coverage and collision
streaming still limit what can be observed; the radius does NOT load game objects.
Longer-range construction and selection have synthetic coverage, not live proof
of collision accuracy/performance at100..500gu. No game calls during this change.

## physics.9 policy retained: latest complete measurement

Owner explicitly requested removal of movement analysis rather than a larger
native-scheduling rewrite. Keep the proven nine-ray fan, native controls, existing
V2 batch transport, max20 rounds/sec,256 queued targets and shared2ms budget.
The former fixed35gu camera range is superseded by the .10 setting above.
Each complete result is emitted directly: any clear ray -> clear; all9 blocked
-> blocked IMMEDIATELY. No second-result confirmation, receiver-distance reset,
smoothing or camera-movement invalidation in either API or HUD. This is the latest
measured geometry at the RECORDED camera, not a guarantee about the current pose.

Bad/incomplete/native-fault results remain unknown. Budget skips do not renew
measurement timestamps. A result expires after500ms without a new valid result;
HUD adds elapsed transport/display time, NOT the unrelated GPU light-snapshot age
again. Loading/unavailable raw sources still invalidate. Raw RGB/positions and
authored records are untouched; the existing light-smoothing stream is unchanged.

Optional `measurementSequence` identifies the completed query round (not a light
ID or GPU capture). `measuredAtTickMilliseconds` is its native completion timestamp
in Windows monotonic uptime milliseconds, NOT Unix time. Reference camera, actual
older lightCaptureSequence/frame,9-ray sample counts and age remain published.
`volumeAgeMillisecondsAtCapture` retains its legacy API name but is measurement
age here. Old private schema samples up to2500ms remain readable by the schema;
the current producer/HUD applies500ms. No fake-fresh relabelling when camera moves.

Tests now cover first-result blocking, direct blocked/clear changes through rapid
camera orbit (>25cm per sample),96 targets, original provenance, incomplete/budget
responses, native-fault latch and exact500/501ms expiry. No ingame inspection or
live acceptance yet. Targeted managed physics and native overlay-model tests only;
no full research suite needed for this consumer-policy change.

## Historical private implementation — physics.7/.8 (2026-09-25)

**Live acceptance FAILED while running.** Owner reports stationary hiding works,
but moving yields "refreshing after movement" for all sources. No additional
ingame investigation performed (owner explicitly declined it). The .25gu check
is used both for cache validity AND consecutive blocked-result confirmation;
at20Hz, straight motion>5gu/sec defeats the latter even without IPC latency.
The synthetic96-light test used.2gu/60ms, only3.33gu/sec, so it did not cover this.
Batching alone does not solve moving acceptance. Next design must distinguish
measurement pose/time, consumer freshness and temporal verdict confirmation.
Preserve collision findings and raw streams; do not promote .8 or silently hold
stale hidden markers. See current HANDOVER for the proposed native scheduling path.

Install package physics.8; .7 was retained with an outdated INI comment saying24
targets. The runtime implementation is identical (native logv7); .8 documents256.

physics.6 LIVE returned valid clear/blocked fans in PID468, but was unsuitable
while moving: the serial scheduler allowed only20 LIGHTS/sec, up to24 positions,
so camera displacement>.25gu invalidated old verdicts faster than refresh.
Owner observed correct hiding after ~1s stationary, then markers returning on
movement. This was scheduling, not failure of the established collision query.

physics.7 sends a SCENE batch at most20 times/sec, queueing up to256 nearest
distinct rendered positions within35gu. All targets share ONE2ms native issue
window, same natural-call lifetime and per-fan matching controls. Budget exhaustion
skips the uncompleted fan/tail (code4), without manufacturing measurements or
disabling later rounds. Oldest attempted targets go first; skipped targets have
priority, whereas invalid targets rotate out of the front. Faults still latch.
Neither a native call nor already-running game code can be safely interrupted.

Versioned internal mappings are `PhysicsVisibilityQueryV2.<pid>` and
`PhysicsVisibilityResultV2.<pid>` with the same Local/CDT prefix. Each is32800bytes:
32-byte batch header (magic/version2/bytes/count/seqlock/sequence) followed by256
128-byte packets. All requested entries must share epoch, camera, player, frame,
capture and issue time. The host validates a complete reply before accepting any
entry. Per-target completion ticks remain actual timestamps; a budget skip never
refreshes a cached verdict. Existing API/method, .25gu displacement check,2500ms
expiry and two-blocked-fan confirmation remain unchanged. No stale-pose hiding
workaround. Fast movement or overloaded budgets can STILL be unknown; this is
not a guarantee of256 completed fans every frame.

Managed regression includes96 moving targets, skipped-tail priority, unknown on
budget exhaustion and unchanged raw streams. Native checks include shared budget
expiry partway through a fan and fault propagation across the remaining batch.
Live .7 motion/performance acceptance remains REQUIRED; only synthetic behavior
and package checks are established. Private ZIP is not a public release.

## Historical physics.6 implementation (superseded scheduling only)

Opt-in `[Experimental] PhysicsVisibility=1` in a research build selects native
physics, not the legacy SDF paths below. Private .6 enables it and HUD hiding;
default template leaves it OFF. Individual fan comparisons passed (visible cage:
4/9 clear; wall:0/9). Continuous integration is built/tested, **not live accepted**.

- Only fresh camera-paired filtered ManyLights; authored arrays remain untouched.
  At most24 nearest spatially merged positions within35gu of the paired camera.
- One9-ray fan + matching natural control at most every50ms; exact-build/context
  guards and fresh per-ray copies. Stop further calls after2ms; a native call itself
  cannot be interrupted. Fault/slow series latches disabled until game restart.
- Any clear neighbor means `clear` (retain), not unobstructed emitter area. All9
  hits need2 consecutive valid fans before `blocked`. First obstruction, error,
  unavailable context, source/camera movement or age>2500ms gives `unknown`.
  Cache source match<=.12gu and camera drift<=.25gu are explicit heuristics.
- Positions/RGB/raw records are never removed or rescaled. HUD F11 toggles hiding
  **confirmed blocked** records in both radar and markers. Unknown stays visible.
  Legacy SDF trace labels and contribution-percent claims removed from HUD.

Additive `sourceVisibility.method="physics-ray-fan"`, `sampleCount=9`,
`clearSampleCount=0..9`. Counts are NOT optical transmission or source-area %.
The existing binary `attenuationFactor` is a retain/hide policy only (0/1/null),
not measured light energy. `referencePosition` and `lightCaptureSequence` are the
ACTUAL older measurement origin/capture, not the current GPU capture. `contextFrame`
is that measurement's light frame; `volumeSequence` and `closestApproach` null
(no SDF volume). The retained field `volumeAgeMillisecondsAtCapture` represents
measurement age at publication for this method. Consumers must add transport age
and reject camera drift/staleness. HUD explicitly validates this method separately.

Two128-byte seqlock mappings `Local\\CrimsonDesertTelemetry.PhysicsVisibilityQuery.<pid>`
and `...PhysicsVisibilityResult.<pid>` are isolated from legacy SDF mappings.
PID/start epoch, exact query identity, timestamp, origin/target and capture pairing
are checked. Managed host handles bounded scheduling/cache/hysteresis; native hook
executes only in the proven original ray caller and stack lifetime. Continuous
mode does not accept file-driven diagnostic requests. No deferred game pointers.

129 native physics checks,31 CTest suites and full managed suite pass, including
new exchange/false-clear/movement/freshness/raw-preservation cases. Game materials
(glass/foliage), tiny openings and renderer omissions remain limitations, not a
claim of exact optical visibility. Sustained live performance remains to verify.

## Preserved legacy SDF implementations (not active in physics.6)

## Goal and current status

For every local source already present in the authored or rendered light feeds,
determine whether solid world geometry blocks the direct segment from the player
to that source. Fire, torch, candle, lamp, point and spot records use the same
test. Camera direction, screen projection, Ambient, image brightness and bounced
light do not participate.

Public production 2.1.11 keeps this capability disabled. Current source now
contains two deliberately separate implementations:

- `CDT_RESEARCH=OFF` restores the earlier minimal camera-to-rendered-source path
  whose fire verdicts previously worked live. It keeps only one current SDF volume,
  classifies records already present in the renderer capture, and publishes metadata
  without changing raw or smoothed records. A new package still needs live and
  antivirus acceptance.
- `CDT_RESEARCH=ON` retains the broader player-to-all-known-source candidate and its
  preserved offline evidence. Its ASI explicitly opts the host into the private
  query mapping; OFF does not run that client. The path still awaits reliable live
  acceptance.

The production fallback is an intentional recovery step, not completion of the
product goal. It does not claim authored-only sources, a complete 360-degree
registry, candles or lamps until those cases pass controlled live tests.

## Narrow production fallback

The OFF path uses the camera position paired with the renderer light capture as its
segment origin. Camera direction, projection and on-screen state do not participate,
so a behind-camera record can be classified while it remains in that capture. The
target is each valid renderer-selected source record, bounded by the current record
count. Sources absent from that feed receive no invented verdict.

It captures one fenced R16 SDF volume, keeps it for at most 1500 ms and uses the
historical Variant A marcher: skip the first 0.6 and final 1.0 game units, advance by
`max(distance, 0.05)`, and classify blocked at the first nonpositive sample. The
implementation has no repeated diagnostic series, history buffer, dump or trace
logger. This historical OFF fallback predates the current clear-only HUD policy;
the API records and RGB remain intact.

The dedicated OFF test verifies waiting before the first field, clear and blocked
results for sources in front of and behind the camera, disabling, and byte-for-byte
preservation of the raw scene/light/counter blocks. This is synthetic evidence. The
next live control is one known fire following `visible -> blocked -> visible`.

## Why the broader research implementation was created

The narrow path cannot satisfy the final all-source/player contract by itself:

1. It traced from the capture camera and could only classify renderer-selected
   contributions. The requested receiver is the player, and every source known to
   either existing light feed must be considered.
2. It requires a signed-distance zero crossing. The copied, filtered t233 field
   can represent a blocking wall with samples that remain slightly positive, so
   a zero-crossing sphere trace produced false `clear` verdicts.

The broader research candidate is a separate asynchronous query/result bridge. The managed host
publishes the player receiver and the union of `lights.sources` and
`lights.rendered.sources`. The native SDF worker traces those positions against
one immutable volume and echoes IDs and positions back. The host position-matches
the response onto both arrays. It never changes or removes original light data.

## Broad research trace definition

The receiver is:

```text
(player.x, player.y + 1.0, player.z)
```

The one-unit world-Y lift avoids starting at the physics root's ground contact
point. No camera vector, frustum or on-screen test is sent through the bridge.

t233 is a 128 x 64 x 1040 R16 clipmap with eight levels and cell size
`0.25 * 2^level` game units. The trace:

- ignores the first and last 0.25 game units of the segment;
- selects the finest covering clipmap at each sample;
- advances at one quarter of that level's cell size;
- treats a sample as near-surface when `distance <= 0.5 * cellSize`;
- returns `blocked` after one whole cell of connected near-surface samples;
- otherwise returns `clear` after reaching the target margin.

Connected length rejects isolated interpolation noise. The threshold scales with
the actual clipmap level rather than light type or scene. A blocked trace may have
a positive `closestApproach`; its sign alone is no longer the classifier.

## API contract (schema 1.5 development build)

Optional `sourceVisibility` is attached to rendered records by the OFF fallback and
independently to authored and rendered records by the ON research bridge. The
smoothed stream preserves it on the original contributions. `referencePosition` is
the paired camera for OFF and the raised player receiver for the broad ON path.

| Field | Meaning |
| --- | --- |
| `status` | `clear`, `blocked`, or `unknown`. |
| `attenuationFactor` | `1` for clear, `0` for blocked, `null` for unknown. |
| `reason` | Null for a usable verdict; explicit reason for unknown. |
| `referencePosition` | Raised player receiver used by the trace. |
| `lightCaptureSequence` | Legacy field name; in schema 1.5 it identifies the independent visibility query. |
| `volumeSequence` | SDF volume sequence, or null if unavailable. |
| `contextFrame` | CPU context frame paired with that SDF volume, or null. |
| `volumeAgeMillisecondsAtCapture` | SDF age when the native result was published. |
| `closestApproach` | Smallest sampled distance for a measured trace; it may be positive for `blocked`. |

Unknown reasons include `waiting-for-volume`, `receiver-moved`, `uncovered`,
`too-short`, `iteration-limit`, `invalid-sdf-sample`, `trace-budget-exceeded`,
`outside-trace-radius`, `stale-volume`, `invalid-context`, `native-fault`,
`game-stopped`, and `waiting-for-current-source`.

The bridge accepts the nearest 256 distinct positions after merging records
within 0.1 game unit. Results expire after 1500 ms. The receiver may move no more
than 0.75 game unit before a response becomes `unknown`. These bounds keep the
research worker finite and ensure stale geometry never silently becomes blocked.

## Preserved evidence

The candidate reproduces 17 labelled controls from retained SDF volumes:

- room: two physically blocked and two physically clear sources;
- same room source: three fresh clear-pose captures;
- Serkis: clear -> blocked -> clear;
- Warspike: two clear -> three blocked -> two clear.

The previous zero-crossing method failed part of this set. The new classifier
passes it in the native implementation and in an independent offline evaluator.
See [live regression evidence](SOURCE_VISIBILITY_REGRESSION.md) for captures and
the earlier [SDF calibration](SDF_VARIANT_A.md) for resource provenance.

## Required live acceptance

Use one fixed player position with at least one clear and one solidly blocked real
fire/candle/lamp. Keep all records shown and verify their positions and verdicts.
Rotate the camera without moving the player; retained sources must keep the same
verdict in front of, behind, and outside the view. Then move so one identified
source follows:

```text
visible -> blocked -> visible
```

The result must be reversible and agree with the wall/building/terrain between
player and source. Repeat across relevant detected light types. A source absent
from both existing light feeds is outside this classifier's input and must not be
reported as blocked.

The restored OFF fallback has its own smaller acceptance gate: first reproduce the
previous fire result with `visible -> blocked -> visible`, then scan the exact ASI
and ZIP. Passing that gate restores a useful fire feature but does not complete this
document's all-light goal. The broader ON path must still pass the fixed-player,
camera-rotation and multi-type controls above before replacing the fallback.
