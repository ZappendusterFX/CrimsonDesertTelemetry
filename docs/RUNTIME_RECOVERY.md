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
guard. The new private package is production code (`Research off`) with all normal
features enabled and the research-status26423880 reference. Existing public releases
and immutable packages are preserved. Only the owner installs packages.

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

Package receipt, final test counts and the installation handoff are recorded in
the current HANDOVER checkpoint. No new plugin has been installed or live-accepted
by the assistant.
