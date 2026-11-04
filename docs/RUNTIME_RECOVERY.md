# Conditional in-plugin update recovery

Implemented on 2026-11-04 at the owner's request. The purpose is to avoid a new
mod build for game updates that only relocate the existing acquisition code.
This is part of the integrated plugin and its already-shipped managed host;
it needs no Python, development checkout, network access or downloaded offsets.
CrimsonHue continues to consume the neutral API without game-build gates.

## Lifecycle

1. The native worker verifies the EXE SHA and all existing startup code guards.
   A matching baseline follows the normal path without a full recovery scan.
   An unknown hash or failed guard requests one recovery for that game process.
2. The host verifies the per-process handshake (PID, process start time, actual
   executable hash and exact reference contract). It resolves only player/camera
   anchors initially. These bounded reads are needed to determine world readiness.
3. A valid typed player chain and decoded camera must accompany at least eight
   advancing scene-buffer frames over one second. Stationary players are valid.
   Loading sentinels, frozen/reversed frames, missing poses and sampling gaps reset
   the window. No native game hook is enabled on a merely plausible address.
4. Once the world is ready, the existing file-backed code/signature, PE unwind,
   profiler-label and vtable checks recover the complete acquisition family.
   The world readiness window is checked again after discovery, before handoff.
5. Native code independently validates the bounded proposed addresses and actual
   live instruction bytes, unchanged layout/profile values, register contexts,
   code relationships, collector table and live scene type/constants. Only a
   complete accepted contract replaces the process-local address set, before hooks.
6. Existing GPU/resource, scene-pairing, freshness and physics natural-query/control
   guards remain active. The success notice additionally requires progressing,
   fresh requested light/ambient streams and a valid complete physics measurement
   when there are eligible local targets. A zero-light scene is legitimate.

Loading during final validation pauses its timeout and starts a fresh observation
window on return. A terminal recovery failure does not repeatedly rescan the EXE.
The next game session gets a new attempt. No runtime hot patching or second hook
set is attempted after activation. The original raw telemetry semantics remain.

## Cache and status

The optional cache is in `%LOCALAPPDATA%\CrimsonDesertTelemetry\Recovery`.
Its key includes the actual EXE SHA256 plus the reference definition/recovery
version fingerprint. It stores bounded RVAs and instruction guards, never game
pointers, credentials or a promoted build definition. A hit repeats address,
role and live checks. A malformed/stale/inconsistent entry falls back to one
fresh search. Failure to write the cache does not block otherwise valid recovery.

The corner HUD and existing notifications show waiting for world, scanning,
validation, activation, success or failure. Percentages correspond to completed
checks/stages; waiting for game data has no invented percentage. Notification
opt-out is respected. Bootstrap/native logs remain the fallback if graphics cannot
show a notice. `/v1/health.recovery` adds the optional neutral object
`{state,title,detail,progress}`; `progress` is null or 0..100. Recovered sessions use
`compatibility.mode = recovered` and `supportedBuild = null`, not a manually tested
build declaration. HTTP/WS telemetry schemas and the physical light consumers are
unchanged.

## Scope and refusal

The current reference is build26423880 with complete Support metadata. Recovered
addresses cover player globals/types, direct camera, ManyLights output/input,
Ambient, spatial GI/exposure and physics entries/callers/collector. Repeated short
patterns are resolved through their known function/call relationships; the first
hit is never accepted. Cache validation uses the same roles.

Only relative address operands may change inside guarded instructions. The reviewed
field offsets, register assignments, command/resource ABI, light packing and
physics query profile remain fixed. Changed function relationships, missing or
ambiguous roles, a changed layout, foreign natural-query profile or invalid live
data are refused. Shader-only changes remain a limitation: there is still no
active PSO/shader-identity gate. This feature cannot guarantee compatibility with
every future engine update or silently repair new data semantics.

Historical Support-less/research-instrumentation builds keep their previous exact
guard. Release2.2.8 uses production code (`Research off`) with all normal features
enabled and the locally validated26423880 reference. Its one-time diagnostic is
disabled. Existing packages are preserved. Only the owner installs packages.

## One-time in-game diagnostic

The owner can test the entire recovery path on the known current executable,
without deleting or falsifying its profile. Normal INIs contain an empty
`[Compatibility] RecoveryTestOnce=`. A private test package can supply a fresh
nonzero 32-hex token through `Build-ModManagerPackage.ps1 -RecoveryTestOnce TOKEN`;
the builder requires an integrated private diagnostic, production code and a full
Support reference. Releases cannot accidentally inherit this switch.

With an unused token, the native worker bypasses only the known-build shortcut.
The host ignores the address cache for this attempt, still waits for the existing
live-world gate, then atomically writes
`%LOCALAPPDATA%\CrimsonDesertTelemetry\RecoveryTests\TOKEN.done` immediately before
the full cold search. Quitting at the menu leaves the test armed. Once the attempt
starts it is consumed even if discovery fails or the process exits; the marker
records an attempt, not success. The native worker only reads this marker, and no
installed INI/profile is rewritten. A later launch uses ordinary compatibility
checks; a real unknown build still triggers normal recovery independently.

The diagnostic handoff uses version2 with its explicit token, so an older host
cannot silently treat the test as ordinary cached recovery. The normal protocol
remains version1. A resumed host after submission does not consume the request a
second time. All code, object, layout, GPU and natural-physics guards remain active.
Success is shown as **Recovery test passed** only after fresh requested captures;
loading, scanning and failures are also explicitly labelled as a test. This proves
the current in-game recovery route, not compatibility with arbitrary future layouts.

The owner installs the preconfigured private ZIP themselves, starts the game and
loads a save. No light toggling, special movement or manual configuration is needed
for this test. After their installation/restart report, inspect health.recovery,
compatibility=recovered, cold-search/consumption logs and fresh requested feeds.
Do not mistake an attempt marker alone for a completed test.

## Verification

Synthetic tests cover full shifted PE recovery, ambiguous/missing/changed contexts,
named roles, corrupted/duplicate caches, cancellation, handshake identity/lifecycle,
native guard mutations, loading/freshness and meaningful success cases. The complete
shared D3D12 HUD renders progress and failure notices in the existing renderer.
Preserved current26423880 and previous26361232 executables both resolve from the
same26423880 template, including guarded cache reuse and rejection/recovery of a
deliberately corrupted entry. Those are file-backed replay results, not installed
game acceptance. Current evidence is under ignored `artifacts/runtime-recovery*`
and `artifacts/light-research/runtime-recovery-*`.

The owner-installed recovery.2 candidate passed a forced cold run on26423880:
all30 anchors were recovered and native guards accepted them; progressing
light/input/Ambient feeds and complete fresh physics results were verified.
This evidence supports the regular2.2.8 reference; no repeated AN/AUS comparison
or arbitrary future-layout compatibility is claimed. See the
[release validation record](releases/v2.2.8-validation.md) and HANDOVER for the
exact package and live evidence. The assistant did not install any package.
