# Physics-first occlusion investigation — 2026-09-25, Codex

**Re-entry update, 2026-09-26:** this file preserves the discovery/control history.
Continuous API/HUD integration progressed through physics.10 and is retained in
manylights.1. Owner reported near-live behavior; the movement/confirmation gates
and hysteresis described in earlier versions were REMOVED in physics.9. Current
behavior is in [SOURCE_VISIBILITY.md](SOURCE_VISIBILITY.md); current task/evidence
in [CLAUDE_REENTRY_20260926.md](CLAUDE_REENTRY_20260926.md). Do not repeat the
diagnostic controls or treat the older "not live-tested" entries as today's state.

**physics.6 integration supersedes the diagnostic-only state below:** continuous
experimental camera-to-rendered-light fans now feed API/HUD, bounded scheduling
and hysteresis. Built/host-tested, NOT live-tested. See current HANDOVER and
SOURCE_VISIBILITY.md for package, exact scope and limitations. The .5 evidence
below is the rationale, not acceptance of the continuous load.

**Current instrument: physics.5**, diagnostic nine-ray fan, described in the final
section. Visible-lantern and wall fans passed in PID35192. physics.4 ray controls passed in PID37148;
older sections below preserve earlier evidence, not the current package state.

Owner priority: resolve geometric light occlusion before the new light-control
idea. Reuse World Builder's native query knowledge, not another SDF threshold.
The SDF false-clear limitation remains measured in `SDF_BAND_LIMIT.md`.

## What is established

World Builder `asi/cdmodkit/cdmodkit.cpp`, pinned
`ee1f05a3ad1a61cd4aee66946155d0315fdc14e7`, captures a natural player-near
`hknpSphereShape` query and replays a copy for downward ground placement. Its
selection and layout are reused under MIT; notice in `licenses/WorldBuilder-MIT.txt`.
See `WORLD_BUILDER_RESEARCH.md` for the broader audit. This is NOT an existing
optical-visibility API. Collision masks, sphere radius, player self-hits, glass,
foliage and render-only geometry can all differ from optical visibility.

Offline anchors: patch **2.03.02**, EXE **1.0.0.2976**, build 25477059,
SHA256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`.
Saved EXE: `artifacts/recovery/20260923-191056-build-25477059/CrimsonDesert.exe`.

- WorldCastShape RVA `0x42B0C50`; verified five pointer/integer arguments:
  world, query, transform, collector, extra. Hook forwards them and preserves RAX.
- Collector vtable `0x5D13528`; slot 5 `0x39DBE20`, 12-slot shape also guarded.
- Shape RTTI `.?AVhknpSphereShape@@`, independently found at file offset `0x6A90040`.
- Query: shape pointer +0x28, start float3 +0x30; collector: inline buffer +0x20
  points to collector+0x30, count +0x0C, double fraction +0x10, normal float3 +0x80.
- Query/template sizes come from WB, not independently proven object extents.
  All snapshots are bounded safe reads; failed reads reject the candidate.
- Player-near selection uses truncation toward zero of X/Z /1000, a horizontal
  radius of 3 and vertical difference <=3. The live observation must confirm
  this convention for the current context; a timeout does not prove no query.

Evidence: `artifacts/light-research/worldbuilder-anchors-25477059-20260924-v2.json`
and `worldbuilder-profiler-25477059-20260924.json`.

## First instrument: observation only

`physics_probe.cpp` is compiled ONLY in research builds. `[Research] PhysicsProbe=0`
by default. The private test package enables it while disabling Ambient,
SourceVisibility and spatial readback so no unrelated spatial instrument runs.
Player/camera, ManyLights and both light views remain available.

After the shared exact-EXE check, an additional fixed diagnostic hash, entry bytes
and collector-vtable guard must match before installing one MinHook detour.
There is NO movement-tick hook, cloned replay, additional physics call or
visibility API mutation in this version. A previously drafted replay was removed
before build: its lifetime/stack-pointer assumptions need live evidence first.

Run `scripts/Start-PhysicsProbe.ps1` with the player in a loaded world. It sends
one atomic file request, bound to PID/start time and fresh telemetry, then returns
immediately. No waiting for owner input. The observer examines at most 512 calls
over 2 seconds, claims one matching natural query, copies its inputs and collector
output in the original call, and writes the report off-thread. In idle state it
only forwards the original call. No allocation or logging in the detour.

Output beside the ASI: `physics-probe-PID-REQUESTID.json`. Contains original
addresses, caller, thread/stack limits, world context, before/after bytes, raw
count/fraction/normal, and exact build/hash. Original calls and arguments are
unchanged. Timeout/rejection is unknown, never a clear-path verdict. Raw result
fields have NOT yet been promoted to validated collision semantics.

Host tests cover negative-tile selection, bounds, exact argument/return
forwarding, snapshots, invalid candidates, timeout and concurrent one-shot claim.
They do not validate the actual game's ABI, collision layers or optical behavior.

## Next steps, strictly in order

1. Read one successful natural query report. If there is no match, use recorded
   selection counters, not a new heap scan. No interpretation of a failed read.
2. Validate retained pointers/lifetimes, native execution context and collector
   behavior; then implement ONE replay control of that query. Do not call from
   the telemetry worker. WB's service is on the movement/game tick despite a
   misleading source comment saying it runs inside the cast.
3. Only after matching the natural control, test bounded start-to-end queries
   across known clear and walled paths. Targets and camera must come from the
   proven paired ManyLights capture, not the authored vector. Keep collision
   outcome separate from visible/blocked/unknown until empirically validated.
4. Consider an explicit ray/filter or calibrated small sphere and endpoint policy
   based on evidence, not a guessed universal wall/visibility flag.

No game run or public visibility acceptance is implied by building this package.
Do not implement light switching or replace the working telemetry pipeline here.

## Built checkpoint

`2.1.15-physics.1` private DMM ZIP is ready. 25 synthetic observer checks, five
focused native regression suites, exact saved-EXE hash verification and ZIP/INI
validation passed. Package validator now recognizes the new research-only switch
while accepting older INIs where it is absent (default OFF). Nothing installed
or published. See HANDOVER for artifact hash and the single next live action.

## Live observation — PID 12228, 2026-09-25

Owner at the known camp. Installed ASI hash matches physics.1. Native log confirms
the exact-build hook and playable-world ManyLights capture. Two explicit observer
requests succeeded, 65.407 seconds apart; no added physics calls or game writes
apart from the instrumentation itself. Telemetry was progressing, with 25 rendered
ManyLights sources at the sampled control. No collision/visibility acceptance yet.

Reports under `artifacts/light-research/`:

- `physics-natural-12228-33035429dbee48b28667237c04a41772.json`
- `physics-natural-12228-07a16c59e57244c0a684b10bd83effa9.json`
- `physics-natural-12228-20260925.native.log`
- `physics-natural-callers-12228-20260925.json` (saved-EXE disassembly, not live code)

Both have thread 6164, caller `module+0x32554FA`, fifth argument = collector, valid
before/after snapshots. First matched the second candidate, next the first. Count
was zero in both; fraction changed from ~1.84467e19 to 1.0, normal remained zero.
This is a natural no-hit result, NOT evidence that any lamp is unoccluded. These
short, diagonal player-near casts are not established to be ground probes.

Important new layout evidence (two independent samples):

- `query+0x40.xyz` = (0.1697388, 0.2169189, -0.376709) in sample 1.
- `query+0x50.xyz` = (5.891406, 4.610017, -2.654569). Componentwise products
  with +0x40 are 1 within 4.2e-8. Sample 2 reproduces the reciprocal relationship.
- `query+0x5C` matches the Euclidean length of +0x40 (0.46666342 / 0.46540228).
  Thus +0x50 is observed inverse-displacement data, NOT a duplicate delta vector.
  Do NOT blindly port WB's ground replay writes of -length into both Y fields.
  A generic segment needs correct preparation, including zero-component handling.
- All four objects are on the active thread stack: transform 0x13FE330,
  query 0x13FE430, collector 0x13FE4D0, shape 0x13FE610. Broad WB-sized captured
  ranges OVERLAP: query+0xA0 starts the collector, collector+0x140 starts the shape.
  They are readback windows, not proven object extents. Other pointer-looking
  words can be neighboring locals/stale hit payload, NOT established fields.
  Never generically remap all qwords across these overlapping windows or retain
  these stack addresses for a later asynchronous call.

The immediate caller is a forwarding/profiling wrapper at RVA 0x3255210, dispatching
five arguments through vtable+0x1F0; it does not build the query. Saved disassembly
again confirms WorldCastShape's five-argument forwarding and world+0xB70/+0xBC0.

Next private revision: controlled replay design with a valid execution context,
owned/accurately rebased data, and an identical-query control BEFORE arbitrary
segments. Observe-only physics.1 cannot replay; requires a closed-game package
change. Preserve this successful capture instead of restarting anchor discovery.

## physics.2: bounded control and segment requests

Owner closed the game after the two successful natural observations. This revision
keeps the default `observe` command and adds explicit `replay` / `segment` modes.
No automatic extra query at startup, no change to public visibility or raw lights.

Execution deliberately differs from WB's deferred movement-tick service: the
extra call runs **after the original WorldCastShape returns, before ShapeHook
returns to the still-active caller**. Same thread, fresh known caller, same world
and live source stack. This avoids dangling captured pointers and a second hook.
It is a new experimental context, NOT yet proven safe in this game. No locks from
our observer are held by the original engine call; the extra query uses the
trampoline and private buffers. A native deadlock cannot be canceled safely by
this instrument; the request/attempt caps are not a timeout for a stuck engine.

All four inputs are copied into guarded private buffers. Only the established
query-shape pointer and collector-inline-buffer pointer are rebased. No blanket
rebasing of the overlapping readback windows. Remaining references are usable
only while the captured caller is alive. Copies never escape for later execution.
Captured sizes still exceed proven extents; canaries are detection, not a proof
against all native bugs. Original query/transform/shape prefixes and the original
collector result are compared after calls; detected changes or exceptions disable
further replay in the process. SEH does not guarantee a faulted engine is healthy:
any such failure requires a restart, no retry in that process.

Each request makes at most one identical-query control and (segment mode only)
one modified segment. The latter is skipped if count/fraction/normal disagree
with the original, input context changes, result is implausible, or guards fail.
Maximum 12 replay transactions per process. A matching no-hit control alone is
weak evidence: require a positive ground control before interpreting wall tests.

Segment +0x50 is populated with reciprocal displacement, +0x5C with length.
Unverified zero/near-zero component convention is refused, not guessed. Limits:
length 0.05..50 gu, origin within 20 gu of player. Sphere radius and collision
filter remain inherited; contact/penetration is NOT automatically optical blocking.

Commands (one-shot, return immediately; no owner-waiting session):

```powershell
./scripts/Start-PhysicsProbe.ps1 -Mode replay
./scripts/Start-PhysicsProbe.ps1 -Mode segment -GroundControl
./scripts/Start-PhysicsProbe.ps1 -Mode segment -LightSampleIndex <current-index>
```

Run these in order only after inspecting the preceding report. Ground control
uses a slightly diagonal downward path near the player to keep all delta
components nonzero. Light mode takes start AND source from the same fresh,
filtered ManyLights snapshot and uses its paired camera. The sample index is
frame-local, not stable lamp identity. Reports retain exact world endpoints.
Do not automatically classify all lights, raycast every frame, or touch the SDF
pipeline. First assess ground, known open path, known wall, then limitations.

Built `CrimsonDesertTelemetry-v2.1.15-physics.2-ModManagers.zip`; SHA256
`CC84F118B1E92064B0389F89399CBC19F731D1ABCE0C42CA6334AF401A0E661F`.
44 synthetic physics checks and five focused native regression suites pass.
Package/INI/payload equality checks pass. Request script parsed and its actual
ground-coordinate assignments were exercised with synthetic camp coordinates.
Not installed automatically; all native replay/segment behavior still needs a
live game test. Default command remains observation-only.

## Live control and segment results — PID 5468

Installed physics.2 hash verified; five explicit transactions completed. All
controls matched, all guards intact, checked original ranges unchanged, exception
zero. ManyLights frame/telemetry sequence progressed throughout. No ASI replacement
or public visibility change. These are instrumented observations, not untouched
baseline evidence.

Reports in `artifacts/light-research/`:

| Report suffix (all .json) | Result |
|---|---|
| physics-control-5468-bc4e1385ec534e858d254af16f41dec8 | Natural no-hit reproduced exactly |
| physics-ground-5468-b9b4206d67304b81941f4ab749950993 | Hit, fraction 0.3111352026, normal (0.000044, 1, 0.000031) |
| physics-lantern-5468-a760c73276e242db958266b8cbb58e94 | First known lantern hit, fraction 0.9068721533 |
| physics-lantern-5468-a32ced963ae34a4cb2e258af9d07a4b7 | Other known lantern hit, fraction 0.9109512568 |
| physics-lantern-short-5468-ea5d1c1d0add4cb79c8f928e205972d3 | First path shortened by 1 gu: no hit, fraction 1 |

Log: `physics-controls-5468-20260925.native.log`.
Ground starts at player+(.6,1.5,.6), ends player+(.7,-3,.67); hit center Y about
609.2566 vs player feet Y 609.1567. This is consistent with a small sphere, not
a measured universal sphere radius. Natural inherited mask/radius were unchanged.

First light full segment: camera (-10535.5684,613.0045,-4420.8491) to paired source
(-10529.7480,611.4641,-4420.3008). Contact is about 0.56 gu before endpoint.
Second target (-10536.1660,611.4647,-4413.7915), contact about 0.65 gu before end.
Shortened first path misses. CORRECTION from the later offline decode: the full
first cast used radius 0.36157, the shortened cast 0.1. This was NOT a controlled
endpoint-isolation pair and cannot establish that shortening alone removed the hit.
The full cast itself places its center contact near the endpoint; it does NOT
identify the contacted object (fixture, character or nearby geometry).
No visually confirmed wall/clear pair yet. Do not mark the near-light contact as
an occluded lamp or solve it with a universal 1-gu cutoff.

Script-only improvements need no new plugin: `-NearLightPosition x,y,z` picks the
nearest actual ManyLights contribution within 0.5 gu IN THE SAME fresh paired
snapshot as its camera. `-StopBeforeLight 1` shortens that measured ray only for
endpoint-isolation experiments. A frame-local sample index from an earlier HTTP
request was not reused. Report endpoints are authoritative, not guessed names.

Next: owner outside the shed's closed wall, looking toward the four lanterns.
One wall-crossing segment, then compare with the retained camp results. Seven
transactions remain in PID 5468; no active request and no waiting session.

## Outside the plank wall — PID 5468, 2026-09-25

Owner stood outside the known shed, supplied a screenshot, and judged the four
lanterns covered. Plank gaps are visible, so this is not a solid optical barrier.
Owner also clarifies the lanterns are wall-mounted: the earlier near-source
contacts may be mounting-wall geometry, not necessarily the fixture itself.

Same installed physics.2, no native change. Player (-10534.5635,608.9869,-4425.0371),
paired camera (-10535.7783,611.6642,-4430.9141). Two successful light segments:

| Request ID | Actual light endpoint | Fraction | Sweep-center contact | Before light |
|---|---|---|---|---|
| 132cf90ed3ec42d3aafbca0a277ab7fe | (-10529.7500,611.4661,-4420.3008) | 0.5939830244 | (-10532.1976,611.5465,-4424.6100) | 4.9564 gu |
| 4e67fd9b9c0d43bc89667423319c5a0f | (-10536.1807,611.4673,-4413.7881) | 0.4216989279 | (-10535.9480,611.5812,-4423.6921) | 9.9074 gu |

Contact distances from camera: 7.2510 / 7.2245 gu. Normals respectively
(-0.182516,-0.001820,-0.983201) and (-0.247063,0.000624,-0.968999).
Positions are start + fraction * displacement: sphere-center locations at
contact, not decoded mesh contact points. Both controls matched, canaries intact,
checked original ranges unchanged, exception=0, segment result plausible.
Telemetry progressed from sequence 50975 / light frame 49093 to 56966 / 54459.

An intervening request `201efeb922fe47128efbca267f5170f2` was refused as
`unknown-call-context`: the captured natural query's fifth argument was NOT its
collector. `controlCalled=false`, `segmentCalled=false`; false guard/preservation
booleans are unexecuted defaults, NOT observed corruption. No replay budget used.
One explicit retry supplied the accepted second segment above. Preserve this
rejection as evidence; do not interpret its absent segment result as clear.

Inference: two paths now hit intervening geometry consistent with the visible
wall, well before either lamp. This supports native queries detecting objects /
walls, not just terrain. It does not yet prove optical visibility classification:
inherited sphere radius/mask, plank gaps and near-source collision remain limits.
Next: a visually unobstructed view from the open side/doorway to one of these same
sources, with paired endpoints again. No blanket endpoint cutoff or API change.

Artifacts in `artifacts/light-research/`: `physics-wall-5468-<request ID>.json`
for all three requests above, `physics-wall-5468-20260925.native.log`, and the
owner screenshot `physics-wall-5468-20260925.png`. Seven replay transactions used,
five remain in this process. No active request; no new plugin/install required.

## Open side: three visible, usual test lantern hidden — PID 5468

Owner supplied `physics-open-side-5468-20260925.png` and explicitly identified
three of four lanterns as visible, with the usual test lantern still hidden.
Player (-10531.75,609.08557,-4412.77393); paired camera X/Z (-10532.015625,
-4406.741211), Y 611.5155..611.52094 for successful measurements. Each target
and camera come from the SAME fresh ManyLights snapshot for that request.

Four successful reports, under `artifacts/light-research/`, named
`physics-open-side-5468-<request ID>.json`:

| Request ID | Target reference X/Z | Owner visibility | Fraction | Distance before source |
|---|---|---|---|---|
| 115fa56d845f4e90a822eaf36dd0a1d1 | -10536.194 / -4413.799 | Visible | 0.9748769999 | 0.205362 gu |
| 228ae7173dda4d2a88460e85c67f0ffe | -10529.755 / -4420.300 | Hidden | 0.6048502922 | 5.432692 gu |
| 307c615318b94274b9bb818302314af2 | -10536.044 / -4418.707 | Visible | 0.9854884744 | 0.182625 gu |
| 6d2a22282c7a42e882d8e59247d65e41 | -10530.790 / -4415.122 | Visible | 0.9745518565 | 0.215312 gu |

All four count=1, controlMatched/guardsIntact/originalsPreserved=true,
segmentResultPlausible=true, callException=0. Hidden-source sweep-center contact
(-10530.6423,611.4855,-4414.9427) is foreground geometry, near the front visible
lamp/post. Do not claim its exact collider identity is known. Three visible paths
have contact centers immediately near their respective endpoints. Therefore this
supports distinct intervening versus source-adjacent collision, NOT four blocked
lights. Together with the previous outside-wall view, the same source near
(-10536.194,-4413.799) changes from 9.907 gu-before-end to 0.205 gu-before-end.

Requests `ef7a35d2cd3f4375894a0fa9592111ea` and
`2312889c08ed46c5bff2d529a1ab63de` again refused a captured natural context whose
fifth argument differed from the collector (`unknown-call-context`). Neither
ran control/segment or consumed replay budget; one explicit retry each succeeded.
Their reports are preserved with the same filename convention. These are not
collision misses or measured memory corruption. For a future instrument revision,
consider filtering this known unsuitable context before one-shot claim, while
preserving observation-only coverage and counting rejections; no live patch here.

Log `physics-open-side-5468-20260925.native.log`; paired ManyLights remained
available and telemetry reached sequence 84520 at 20:24:52 local. No ongoing
request; 11 of 12 replay transactions now consumed. Do not use the last by default.
No public API, INI, ASI or package changed. Captures are instrumented, not baseline.

Next: inspect retained shape/query/collector evidence and the established native
source to validate radius and hit/filter identity. Need principled fixture/self
handling, not a fitted 0.22/0.5/1-gu cutoff. Collision filters, thin geometry,
glass and streamed objects remain optical limits. No further equivalent owner
scene setup is needed just to repeat this positive comparison.

## Fire bowl clear-path counterexample — PID 5468

Owner explained the lanterns have steel cage bars, then requested the fire bowl
directly ahead. Fresh ManyLights nearest source near (-10520.221,610.448,-4423.204)
was about 2 gu forward in X / -1.12 in Z from the player, matching the player's
forward (0.87707,0,-0.48036). Used position selector, not stale sample index.

Report `artifacts/light-research/physics-brazier-5468-c64c4027a2bb4317813ccbd11147bf7f.json`:
paired camera (-10527.284180,613.093567,-4419.924316), actual source
(-10520.226563,610.455139,-4423.204102), player
(-10522.221680,608.776794,-4422.083008). Full unshortened segment returned count=0,
fraction=0.9999999850988388, zero normal. Control matched; guard/preservation/
plausibility checks passed, exception=0. Native log archived as
`physics-brazier-5468-20260925.native.log`; telemetry remained available afterward.

Thus source-directed queries do NOT inherently hit every light: this fire bowl
has a clear measured path. Consistent with steel bars/fixture contact causing
the visible lantern hits, but no collision-object/material identity is proven.
This does not establish that all bowls are unoccluded or all lantern contacts
are their bars. No code/API/package change. Twelve replay transactions consumed;
process guard now prevents further replay until restart. Next step is offline
radius/hit-identity/filter analysis, no immediate owner action or restart needed.

## Offline radius, surface point and collision identity — 2026-09-25

Owner closed PID 5468. Reused saved EXE and reports only. New stdlib tool
`scripts/Decode-PhysicsProbe.py` has eight synthetic tests and rejects unknown
builds, wrong vtables, truncated bytes and failed controls. No-hit reports never
decode stale hit payload; collision is never promoted to optical visibility.
Saved result: `artifacts/light-research/physics-decoded-5468-20260925.json`.

**Radius is now measured, not estimated from feet:** sphere constructor at
RVA 0x42079E0 passes its float argument to base constructor 0x4286850, which writes
it to shape+0x68. It sets sphere vtable RVA 0x530ACE8. The closest-hit collector
method 0x39DBE20 copies a 0xA0-byte result to collector+0x80. The copied result has
normal float3 at +0x80 and tile-local surface position double3 at +0x90; fraction
double at +0xB0 also agrees with +0x10. Across ALL NINE successful hit reports:

`start + fraction * delta ~= surfacePositionWorld + radius * normal`

Residuals range from 0.0000104 to 0.000949 gu. Eight hit probes have radius 0.1;
the first inside-lantern hit `a760c732...` has radius 0.3615695. All four open-side
lantern comparisons have the SAME 0.1 radius; that comparison stands. Both no-hit
segments (shortened and brazier) also have radius 0.1. Ground surface Y=609.156555
versus player feet Y=609.156677 corroborates the coordinate conversion. Diameter
0.2 can contact bars a thin ray would miss; this is not yet a measured ray result.

**Hit identity is richer than a position but not yet object naming:** collector
filter at RVA 0x39DB920 passes result+0x60/+0x70 (collector+0xE0/+0xF0) into resolver
0x3A04DC0. It checks result+0x68 low24 against 0xFFFFFF. Resolver uses the second
argument's high bits to select a child of a compound shape in one branch. Thus
keep opaque 64-bit collision handle + raw body ID + subshape selector together,
not a pointer guess or persistent lamp ID. No live resolver call was made.

- Ground and both outside-wall hits share handle `0x80000000010007C8` but different
  subshape selectors (0x6BFFFFFF / 0x39FFFFFF / 0x397FFFFF). A body can represent
  aggregated world geometry, so ignoring an entire body could also ignore a wall.
- Open-side hidden test-lamp hit and front visible-lamp hit share handle
  `0x8000000002002219`, selectors 0x25FFFFFF / 0x07FFFFFF. Contact positions are
  near that front fixture/post; this supports a foreground obstruction but does
  not yet identify steel bars or mesh material.
- Physics surface contact is NOT a renderer illumination sample. These queries
  are camera-to-source, not rays emitted by the light in all directions.

Static evidence (all under artifacts/light-research):
`physics-radius-hit-static-20260925.json`, `physics-radius-layout-static-20260925.json`,
`physics-hit-resolver-static-20260925.json`. The radius-derived helper at
0x14428CAD0 is a jump to runtime code 0x155415340, not decoded by the unwind helper.
Packed shape+0x6C/+0x6E fields are also initialized by the constructor. A one-float
radius patch is therefore NOT established as a valid smaller-sphere constructor.

## physics.3: observe an actual native ray, no replay

Next bounded instrument adds `Start-PhysicsProbe.ps1 -Mode rayobserve` alongside
the preserved sphere modes. It hooks the exact-byte-guarded TtWorldCastRay wrapper
RVA 0x42B0B50, whose three arguments are verified by disassembly. One native call
is observed with original args/return untouched. Query 0x100 and collector 0x140
raw windows are copied before/after on that original thread; no additional ray,
no guessed collector interpretation, no pointer traversal, no public API change.
Captured query is not yet confirmed player-near or camera-to-light; owner/player
snapshot provides context only. Timeout is unknown, not proof of no native rays.
Reports mark primitive=native-ray-observation, rawResult=null, and report actual
window sizes. Observe-only ray mode does not consume sphere replay budget.

Sphere replay selection now skips unsuitable fifth-argument contexts before
claiming its one-shot (observation mode still records them). Original sphere
shape-preservation checks now include radius/packed fields through +0x70.
No radius edits, ray replay, multi-ray visibility classifier or source-ID join.
57 native synthetic checks and five focused CTest suites pass; eight decoder tests
pass. New ray prologue guard also matches the saved exact EXE. Game validation of
the new hook remains pending, distinct from the successful physics.2 controls.

## Native ray observed; physics.4 controlled replay prepared — 2026-09-25

Owner loaded physics.3 at the camp, PID 34448. Deployed ASI matches package
SHA256 `09B57CD53565E0FD93C6B4FDE69F109993A28C24DCAEA4C5157E271160CE478F`;
log says ray observer ready. TWO original rays captured, no extra query:

- `physics-ray-natural-34448-35138d057ee44b4090e90000dcb16bbc.json`
- `physics-ray-natural-34448-14d2dc18586342a2b93d31350d02639a.json`

Both archived under artifacts/light-research. Caller RVA 0x32551AF is the world
forwarder (enclosing function 0x3254EE0); static evidence is
`physics-ray-caller-static-20260925.json`, not a newly found query constructor.
Prefix 0xA0 bytes was unchanged after both original calls. Observed ray fields:

| Offset | Observed representation |
|---|---|
| +0x40 | tile-local origin double3 (negative X/Z tiles truncate toward zero) |
| +0x58 | double 0 |
| +0x60 | displacement double3 |
| +0x78 | double 1 |
| +0x80 | reciprocal displacement float3 |
| +0x8C | length float |
| +0x90 / +0x98 | retained opaque fields, not decoded |

First ray: local origin (-529.544922,613.590027,-419.810547), displacement
(0.01953125,-0.19903564453125,-0.00390625), length ~0.20003. Collector vtable
RVA 0x5D13528 is the established closest-hit collector, count=0, fraction=1.
Collector begins EXACTLY at query+0xA0. The raw 0x100 observation window therefore
overlaps it: do NOT clone that overlapping tail as query state. Its origin is
~4.43 gu above player feet; ray context selection allows <=3 gu horizontal,
<=6 vertical. Second ray starts at player, has a DIFFERENT collector vtable;
no closest-hit semantics assigned. Its zero Y displacement uses near-FLT_MAX
inverse, but one occurrence does not establish all zero-axis conventions.

physics.4 adds opt-in `rayreplay` / `raysegment`. It clones only query prefix
0xA0 and collector 0x140, rebasing only the proven inline hit pointer +0x20.
Known collector/caller, original thread, current stack lifetime, <=100 ms age,
unchanged query/world/output, initial empty collector, canaries and bounded
segments are required. Control must reproduce the original result before any
changed segment. A fault or original/guard damage latches replay disabled until
restart. Sphere and ray share 12 transactions/process; native hangs cannot be
safely timed out. No pointer use after hook return, no API/HUD visibility change.
Axis-aligned segments remain refused; unknown filter fields are preserved.

**Not live-validated yet:** ray replay, segment construction and ray hit geometry.
Next order: identical ray control, downward positive ground control, then one
known light using fresh paired ManyLights. A matching natural no-hit alone is
NOT evidence that modified queries can detect geometry. Decoder's ray branch
checks surface ~= segment point (radius 0); this is a consistency check awaiting
live evidence, not optical classification.

Owner clarified that cage bars can cause PARTIAL obstruction. Keep sphere
sweeps: a wide-sweep hit indicates some contact in the swept volume, not full
coverage or a blocked percentage. A single ray can also strike one bar.
Multiple spatially distributed rays may estimate coverage later, but require a
justified source extent/sampling pattern; neither that classifier nor a fitted
near-source cutoff is implemented. Do not discard a whole collision body, as
one body can include both lamp surroundings and walls.

## physics.4 first live ray controls — PID 37148, 2026-09-25

Installed ASI matches physics.4 package, SHA256
`5751B070A63E3F34BB11E96154A5B0989D4293E240F659413C91BDF6ED2D6809`.
Log reports v4/ray ready; exact supported EXE hash unchanged. Owner stationary
at (-10530.1015625,609.1566162109375,-4419.361328125). Four transactions:

| Request ID | Query | Measured result |
|---|---|---|
| d86cdbc5ee3b4791993d21890855d0a8 | identical ray replay | natural HIT fraction 0.924002815, replay matched |
| 11ee2d497e554f129247c9065ef78c52 | downward ground control | hit fraction 0.333321469, upward normal |
| 122cd235b2114de5b87c1ae862edad54 | full camera to usual test lantern | hit fraction 0.976077206, 0.145219855 gu before source |
| b5ec58b4fca54cdf899711e4cf6b8128 | stop 0.4 gu before fresh paired source | NO hit, fraction 1 |

All controls match; guardsIntact/originalsPreserved true, exception 0; segment
results plausible. Ground surface Y=609.156669601, essentially player feet,
and surface equals start+fraction*delta within 0.00000188 gu. Confirms a thin
ray's positive hit geometry, not just a no-hit replay. Sphere radius is absent.

Full lamp ray starts (-10535.659180,612.110046,-4421.516602), source
(-10529.747070,611.465393,-4420.299805). Surface contact
(-10529.888505,611.480815,-4420.328914), normal
(-0.993423,-0.027525,-0.111150). Geometry residual 0.0000000226 gu.
Opaque handle 0x80000000020023CA, subshape 0x1FFFFFFF; do not identify this as
the bars without further evidence. Ground uses handle 0x8000000001000996,
subshape 0x6BFFFFFF. Handles changed across process: not persistent light IDs.

Shortened comparison starts at camera Y=612.103516 (0.00653 gu different), ends
(-10530.134766,611.505615,-4420.376953). Fresh ManyLights source also animates.
Thus NOT an exact same-ray truncation test, but consistent with near-source
contact rather than a broad intervening wall at this viewpoint. The 0.4-gu
shortening is diagnostic ONLY, never a visibility tolerance. A thin ray still
hits some visible cages; total/partial optical coverage remains unresolved.

Artifacts: `physics-ray-37148-<requestId>.json`, decoded
`physics-ray-decoded-37148-{controls,lantern,lantern-short}.json`,
`physics-ray-short-request-37148-b5ec58b4fca54cdf899711e4cf6b8128.json`,
`physics-ray-37148-20260925.native.log`, all under artifacts/light-research.
Replay-only decoder row says collision=unknown because no diagnostic segment
was requested; it does NOT mean the replay control failed. Telemetry still
available at sequence 15589/light frame 17229, 21:54 local. Four of twelve
transactions consumed; eight remain. No code/package/API changes this turn.
Next: one thin ray from known outside-wall viewpoint, no reinstall needed.

## Outside-wall HUD disappearance investigation — PID 37148

Owner's screenshot showed 2 shown / 0 hidden / 2 unknown, source visibility
disabled. Owner moved to wall and asked to investigate unexpected reduction,
not to treat it as successful physics integration. There is NO physics-to-HUD
result bridge. Actual INI: SourceVisibility=0, HideOccluded=0, OcclusionTest=0,
HUD radius35, API radius100. Git diff f6aafbc..781b5cb has no ManyLights capture,
managed light reader or HUD code changes. Do not claim this excludes every
possible indirect instrumentation effect; no baseline A/B was run.

Reused `Capture-LightStreams.ps1 -Seconds 3`, saved raw/derived streams and
before/after snapshots to `artifacts/light-research/physics-wall-missing-lights-37148-20260925/`.
181 raw / 180 smoothed messages, 44 unique native capture sequences. Stationary
player (-10536.61,608.978,-4425.2666), camera X/Z(-10537.567,-4431.202), slight idle
Y movement. 34..37 active native records, 32..35 published, 3..6 inside35gu,
malformed=0. Usual test lamp near(-10529.755,-4420.300) absent in ALL44. Most
published records are distant camp sources. UI counts match the small local
subset rather than an occlusion-based removal of dozens of available sources.

RenderLightReader only accepts the paired GPU live-count prefix, pi markers,
plausible fields and API radius. HUD uses radius/projection and optional blocked
filter; unknown stays shown. Known lantern is within both radii, but absent from
these API samples. This narrows disappearance to supplied/render-selected data
or its acquisition, not the new physics classifier (there isn't one). Do not
promote this to proof of a particular current GPU branch or lamp switched OFF.

Existing evidence, NOT new shader research: LOCAL_ILLUMINATION_RESEARCH's
"Separate depth-resource route" and actual old PIX PSO475 listing
`light-control-pix-20260924/pso-475.ll` lines1020..1075 show five g_hiZMap samples
feeding an inclusion branch before append. Other frustum/threshold paths exist.
Thus view/depth-based renderer filtering is plausible; old capture does NOT
prove why this new frame excludes a particular source. No SDF-produced-depth
identity inferred from names. A stationary-camera-direction control is next.

Later one snapshot briefly included usual test lamp; immediate fresh selector
refused it as absent (NO native request/budget use). A separate fresh-source
query to known brazier near(-10520.227,610.455,-4423.204) succeeded:
`physics-ray-37148-b8155b729e9440ac9296b583de2ae98e.json`, full length19.099356gu,
no hit/fraction1; controls/guards/original checks pass, no exception. Camera
(-10537.567383,611.612549,-4431.202148), endpoint
(-10520.260742,610.453369,-4423.207031). This is a DIFFERENT source/direction and
does not prove a clear path through the pictured wall; geometry along that
ray has not been visually confirmed. Decode file
`physics-ray-decoded-37148-wall-brazier.json`. Five transactions now used; seven
remain. Telemetry available through sequence55744/frame51636. No code/config or
package changed. Ask owner to rotate only camera, then END turn, do not wait.

## Same view after darkening: sources return; thin ray wall hit — PID37148

Owner returned ~10min later, reported darker game and more HUD lights WITHOUT
rotating camera. Supersedes the camera-turn request. Same read-only capture,
3sec, `artifacts/light-research/physics-wall-darker-37148-20260925/`:
181raw/180smoothed messages, 47unique native captures. Compare previous run:

| Measure | Earlier brighter scene (44 captures) | Later darker scene (47 captures) |
|---|---|---|
| Active native records | 34..37 | 94 |
| Sources inside35gu | 3..6 | 15 |
| Usual test lamp present | 0/44 | 47/47 |
| Malformed | 0 | 0 |

Both camera forward exactly (0.28870547,-0.05402232,0.9558926); camera X/Z
(-10537.567,-4431.202); player X/Z(-10536.61,-4425.2666). First-frame camera Y
611.6074 vs611.6064, player Y608.978 vs608.9779 (idle variation). No config/code
change, no new native query between the two captures. Same process37148.
Thus a camera-direction-only explanation cannot account for this recovery.
Brightness/time/scene-state-dependent selection is supported as a hypothesis,
not isolated causation: no controlled day/night A-B-A or GPU branch trace.
Retained actual PIX PSO475 lines725..758/1050..1075 also show light luminance and
distance-related thresholds in addition to depth selection. Old-frame evidence
does not prove which threshold changed now. Do NOT call missing source OFF or
occluded. Camera-turn control no longer required as the immediate next step.

With fresh paired source now available, usual test-lamp wall ray completed:
`physics-ray-37148-1a1275fc96c84848a38bac7a52838ae8.json` and
`physics-ray-decoded-37148-wall-lantern.json` under artifacts/light-research.
Camera(-10537.567383,611.621460,-4431.202148) to source
(-10529.740234,611.461548,-4420.299805), length13.422031gu. HIT fraction
0.624761406, contact(-10532.677282,611.521553,-4424.390786), **5.036464gu before
source**; normal(-0.241517,-0.000209,-0.970397), nearly horizontal. Surface/segment
residual0.00000153gu. Opaque body0x8000000001000996, selector0x39FFFFFF; same
selector as earlier sphere wall, body also used by ground (different selector).
All control/guard/original/plausibility checks pass, exception0. This supports
an intervening wall contact, unlike inside-view source-adjacent hit0.14522gu.
It does not estimate plank-gap coverage or solve cage-bar partial occlusion.
Six of12 replay transactions used; six remain; no pending query. No plugin/API
or configuration change. Next work is principled spatial sampling/extent, not
another equivalent wall test or arbitrary near-source hit suppression.

## physics.5: bounded same-context diagnostic fan (not live-validated)

No validated physical flame/source radius was found in the existing ManyLights
contract. Reused old PIX PSO479 `BuildLightTreeLevel0CS`, preserved listing
`artifacts/light-research/light-control-pix-20260924/pso-479.ll`: the tree's
`_radiusSqr` at node+20 derives from luminance and scene thresholds (roughly
lines326..369). It is NOT a justified emitting-area radius. No new shader search
or public schema change. Retain broad sphere sweeps as complementary evidence:
they detect some contact in a volume, never a blocked-area fraction.

New explicit `rayfan` captures a fresh camera-paired filtered ManyLights target.
Nine endpoints: exact source center, then +/-right and +/-up at **0.05 and 0.15
game units**, in the plane normal to the sightline. These are diagnostic probe
offsets, NOT inferred flame dimensions. Samples can lie outside the emitter;
hit/no-hit counts must NOT be exported as optical attenuation or visibility %.

One matched natural replay control precedes all nine rays, on the same original
thread/stack lifetime. Each ray uses a fresh copy of the original pre-query input
and collector. All endpoints validated before control; per-sample canaries,
original preservation and plausible-result checks. Stop on any fault/guard failure
(latched off until restart), implausible result, >10ms series duration or >100ms
original context age. A synchronous native call itself cannot be safely timed out.
Target age includes conservative HTTP transit plus time until the hook: <=250ms
reported source age at request, <=500ms including dispatch delay. Missing/stale
source means refused, never replaced with a guessed authored-light target.

Budget remains max12 transactions/process, now also **max24 actual extra native
calls shared across all sphere/ray modes** (fan reserves10 before execution).
Reports retain each endpoint and collector plus frame/sample/sequence provenance.
Decoder reports per-sample contacts and hit/clear counts only for a fully completed,
guarded batch; partial/failed batch stays unknown, with no aggregate clear result.
No diagnostic-to-HUD/API bridge, no INI visibility enablement, no public release.

Validation: 121 synthetic native checks (including stale source, mid-series context
expiry, fault stop and shared budget); 15 decoder tests; five focused native CTest
suites pass. These do NOT validate game collision coverage. Private .5 ZIP carries
PhysicsProbe=1, normal telemetry/ManyLights/HUD on, Ambient/SourceVisibility/spatial
diagnostics off. Exact package hash/current deployment status: HANDOVER.

Next live step after owner installs whole .5 ZIP with game closed: verify actual
ASI hash/log v5; one fresh-source fan outside the known wall, then one at the visible
cage. Each fan already contains its matching natural replay control. Ground control
can consume2 extra calls if needed; two fans consume20 (within24). Stop for owner
viewpoint changes; do not keep a waiting session. Example, not for current .4:
`./scripts/Start-PhysicsProbe.ps1 -Mode rayfan -NearLightPosition @(-10529.755,611.292,-4420.300)`.
Compare center/inner/outer contacts, not just total hits. This is a bounded probe
of partial obstruction, not production acceptance or proof of material opacity.

## physics.5 first live fan: visible usual lantern — PID35192

Owner returned at usual lamp and explicitly requests user-relevant scope, not
"rocket science". Practical acceptance: cage bars must not make visible lamps
disappear; intervening walls must block them. Finish this small comparison before
further instrumentation or theoretical extent research.

Installed ASI hash matches .5; logv5, supported EXE unchanged, API playing.
Request `2d72b9ca2eae45c4b34fbfdfd9abfbbd`, lightframe9819/sample34, source age46ms.
Camera(-10535.707031,612.519470,-4417.542969), source
(-10529.738281,611.466736,-4420.300781), center distance6.65881gu.

| Sample group | Hit | No hit |
|---|---|---|
| Center | 1 | 0 |
| +/-right/up at0.05gu | 3 | 1 |
| +/-right/up at0.15gu | 1 | 3 |

Center contact0.145281gu before source. All five hits within0.123316..0.177555gu
of their target; same opaque body0x8000000002002438, selectors1BFFFFFF/1FFFFFFF/
7FFFFFFF. All geometry residuals <0.00000004gu. This supports localized fixture
obstruction with nearby clear paths, not fully blocked surroundings. It does NOT
prove a specific bar identity or mean44% optical visibility. All9 completed;
controlMatched/guards/originals true, exception0. Ten extra calls/one transaction
consumed;14 extra calls remain. Health after test playing, sequence10142,errornull.

Artifacts under light-research: `physics-rayfan-35192-2d72b9ca2eae45c4b34fbfdfd9abfbbd.json`
and `physics-rayfan-decoded-35192-lantern.json`. Decoder's aggregate collision=unknown
is intentional; inspect fanComplete and samples, not single-segment summary counts.
Next and only requested comparison: same lamp from outside the closed shed wall,
one fan (10calls). No new package, no HUD/API changes or long waiting session.

## physics.5 wall comparison completes the bounded experiment — PID35192

Owner moved outside wall, same usual lamp; request
`2b76d596cefc40cfb96966f4e0dfb32a`, frame18990/sample103, source age15ms.
Camera(-10523.083008,612.528625,-4421.712891), source
(-10529.749023,611.468567,-4420.300781). This is a different outside-wall view
from previous PID37148; do not expect the old5.04gu contact distance.

ALL9 rays HIT: center1/1, inner4/4, outer4/4. No clear neighboring ray.
Contacts roughly0.38..0.49gu before each endpoint; same opaque body
0x8000000001000AB5, mostly selector55FFFFFF, also2AFFFFFF and55BFFFFF.
Geometry consistent for all9. Matching control, canaries and originals pass;
exception0.20 extra calls used across two transactions,4 remain (no third fan).
Afterward health playing, sequence20140,errornull. No source-visibility HUD change.

Raw `physics-rayfan-35192-2b76d596cefc40cfb96966f4e0dfb32a.json`, decoded
`physics-rayfan-decoded-35192-wall.json`, under artifacts/light-research.

Practical conclusion in this paired test: visible fixture has clear neighbors
(4/9), wall has none (0/9), despite both center rays hitting. This supports a
conservative sampled-path classifier rather than single-hit removal. It is NOT
general material/scene acceptance or a44% light-transmission measurement.
Next: bounded experimental integration, keep some-clear lights rather than hide
them, all-blocked classification with temporal stability, failed/stale ->unknown;
retain raw streams. No further geometry research required before first practical
implementation. Sustained query scheduling/performance still needs bounded tests;
the one-shot diagnostic's success does not establish a safe per-frame all-light rate.

## 2.2.0 intermittent mass flashes — 2026-09-26, PID456 context comparison

Owner reports brief simultaneous appearances of many hidden lights while moving,
EVERYWHERE, not just indoors. The preceding PID30924 API recording established
real false `clear`/9-of-9 batches, not simply HUD fallback on `unknown`.

Private `2.2.1-visibility-trace.1` preserved the production sampler and recorded
already-copied query/collector data outside the hook, on an explicit bounded event.
This is an instrumented run, not an untouched baseline. Evidence directory:
`artifacts/light-research/visibility-motion-20260926-202000-92c19550ccaf4326996d20c0cb4f66c5/`
contains `visibility.jsonl.gz`, `summary.json`, and copied `native-context.jsonl`.
Original bin64 trace preserved:
`physics-visibility-context-456-134349202019731006-26961000.jsonl`.
API: 1170 frames, 18:20:00.636–18:20:20.621 UTC; native: 302 rounds over19.891s.

| Native rounds | Query DWORD +0x14 | Observed result |
|---|---|---|
| 10 | `0x40004027` | Every completed target reports all9 paths clear |
| 291 | `0x40000016` | Normal mixed/blocked results; at least20 blocked targets |
| 1 | `0x40004024` | Normal blocked results; not adopted without validation |

The 10 failed measurement sequences are1523,1598,1605,1606,1639,1660,1692,1734,
1739,1765; all correlate to API outputs. API peak:196 clear/18 unknown/0 blocked.
World5236376086560, inner5236470385152 and callerRVA0x32551AF are shared with normal
rounds. Threads also overlap. Collector vtable is unchanged. Other opaque bytes
differ too, so +0x14 is a discriminator, NOT proof that this DWORD alone explains
the filter, or that every `0x40004xxx` query is unsuitable. The original-call replay
control can match perfectly even when a borrowed query is unsuitable for visibility.
The trailing `unknown-batch-time-budget` concerns skipped targets; it does not
invalidate the earlier completed (but false-clear) fans in that batch.

`2.2.1-visibility-fix.1` now selects the already validated `0x40000016` native
family before claiming continuous work, with a second guard before replay.
Retained ground/lantern/wall controls used that family. Original calls, opaque
filter bits and collector bytes remain untouched; no context is synthesized.
The next matching natural call may claim the still-pending work. Existing limits,
timeouts, fan geometry and fail-unknown behavior are unchanged. Manual research
is not restricted. Trace now includes profile and rejected-context/attempt counts.
177 synthetic checks and focused observer/release-fire tests pass; package checks
pass. Live effectiveness and sufficient matching-call availability remain UNTESTED.

Separate open report: individual MOVING lights repeatedly flicker; owner strongly
suspects NPC torches. `PhysicsVisibilityClient.Find` associates by <=0.12gu, not
stable object identity. Position motion can therefore lose association before a
fresh result; this is a plausible inspected path, not a proven cause in these
captures. Do not conflate it with coherent false-clear rounds, silently retain
arbitrary stale measurements, or treat sampleIndex as persistent torch identity.
