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

Cache version 2 also stores the validated owner-to-physics field offset.
Earlier cache files remain preserved. The resolver rechecks recovered renderer
operands from their instruction bytes, and the player field must still pass the
live typed-chain and pose checks on every launch.

Every new game process using these addresses repeats the guarded startup and
live checks, including cache hits for the same EXE. The HUD calls this
`INITIALIZING` and the completion notice says `Telemetry is ready`.
Waiting in the main menu keeps the normal game-world wait without treating
elapsed time as evidence of a game update. The host log records whether the
address check reused the cache or completed a full search.

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

The current reference is build26448710 with complete Support metadata. Recovered
addresses cover player globals/types, direct camera, ManyLights output/input,
Ambient, spatial GI/exposure and physics entries/callers/collector. Repeated short
patterns are resolved through their known function/call relationships; the first
hit is never accepted. Cache validation uses the same roles.

The closest-hit collector has nine slots. Recovery checks the guarded method
in slot 5, executable targets, the repeated slots and the RIP-relative store
in slot 1 that installs the same table. Build 26480954 changed the neighboring
table after slot 8. Its entries are outside the collector guard.

Recovery permits a common shift of the renderer buffer block by at most 0x100
bytes, aligned to eight bytes. Only the four displacement operands in the bank
and input-load contexts may differ. Input, counter and output spacing and the
bank-index relationship must agree. Native validation independently repeats these
checks before publishing the offsets. GI, sky and exposure readers use the
recovered fields and retain their object-backpointer and resource checks.

The command vtable has two reviewed layouts. Build 26949954 moved the
ManyLights UAV binders by 0x10, the ManyLights, Ambient and Exposure dispatch
slots by eight bytes, and the ManyLights post-dispatch virtual call by eight
bytes. The host and ASI require all of these operands to agree on one layout
and keep the shared direct-call target and the surrounding instructions guarded.
The natural ray caller can move up to 0x40 bytes from its previous position
relative to the shape caller. The host requires a unique match for the full
call and its trailing context, and the ASI checks the live bytes and range.

If a progressing scene has a valid position/camera but the final player physics
field fails, the host checks only the 33 aligned fields within 0x80 bytes of the
reference field in the RTTI-verified player owner. A unique candidate must pass
the existing orthonormal basis, handedness and chunk-position agreement checks.
The full player chain and advancing scene must then pass the normal readiness
window before activation. Multiple candidates stop initialization.

The atmospheric ambient source is identified by its profiler role and the
paired dispatch-hook context in the same unwind function. Its distance from the
other ambient function can change.

Register assignments, command/resource ABI, light packing, other field offsets
and physics query profile retain their existing guards. Missing or ambiguous
roles, inconsistent buffer shifts and invalid live data are refused.
Shader-only changes remain a limitation: there is still no
active PSO/shader-identity gate. This feature cannot guarantee compatibility with
every future engine update or silently repair new data semantics.

### Guarded menu observation

After the exact-build checks or successful native recovery, the activity worker
resolves the menu registry once in the loaded PE. A unique registration path
must validate its relative global, matching removal callback and record layout.
Bounded samples identify the MainMenu root by its MSVC RTTI and verify the
getter and control-event code reached through that vtable. The reader checks
the two low bits of the control byte on a stable pointer chain. Observed open
values include 03 and 63. A pair with only one bit set is unavailable.
Missing or changed menu anchors leave acquisition enabled without blocking the
other telemetry feeds. The simulation pump remains an independent observation.
No game menu offsets are fetched or promoted into a game-build definition.

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
