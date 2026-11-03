# Patch 2.04.05 / build 26361232 recovery — 2026-11-03, Codex

## Current continuation: integrated.3 passive live recovery passes

Owner reported ingame after installing complete integrated.3. PID32560 started
11:58:31; installed ASI/Core/host/bothCFGs/INI hashes all equal the immutable ZIP.
Startup selects exact-build natural profile0x40000017 and all requested features.
Health playing/errornull, exact build26361232 and DF531A... EXE hash. Original
source correction remains c29353d; no source, installed file, setting or game input
was changed during this verification. Existing recorder --trace adds no rays.

Existing bounded20s recorder finishes normally:1188 frames/API7405->8592.
Filtered and upstream status=available in every recorded frame; filtered ages
0..94ms. Counter/fence/camera pairing checks remain enforced. The explicitly
rejected malformed filtered record remains in diagnostics; no new shader packing
or optical-response claim is inferred. Follow-up saved API sequence12336 has
health playing, filtered/upstream available, Ambient available(age167ms), GI camera
sky visibility available(age16ms). This confirms continuing data after the trace.

The existing natural/control/visibility path now produces:

-241 accepted native templates, all profile0x40000017 and the exact guarded caller;
 native lastControlStatus=diagnostic-ray-fan-completed67 rounds or shared batch
 time-budget refusal174 rounds. No native fault/restart latch is reported.
-8751 completed targets with9 rays each:7986 all blocked,652 all clear and113 with
 1..8 clear samples. Completion code1 requires natural control matching, intact
 private guards, unchanged originals and no exception under the unchanged code.
-9473 budget-skipped target attempts and one unavailable attempt; these are not
 successful measurements. Existing per-scene2ms/256targets/20Hz limits remain.
 Completed-to-publication age0..16ms. A busy/moving scene can have unknown/stale
 entries; this is retained behavior rather than an all-target freshness promise.
-API supplies clear AND blocked results in both rendered/upstream feeds; known
 result ages0..500ms. Native traces contain mixed as well as all-clear/all-blocked
 outcomes, unlike the historic wrong-profile all-clear failure. No new controlled
 optical/material comparison is claimed. Latest saved upstream snapshot contains
 102 blocked,38 clear and one stale-physics unknown.

Offline correlation of the SAME recording matches116478 known-source observations
(111714 blocked,4764 clear) to240 native measurement sequences. Identity checks
include measurement/capture/frame, actual camera, exact completion tick,9 samples,
clear count and paired target within existing cache radius/JSON float precision,
plus0..500ms age. No mismatches remain.788 known observations belong to normal
measurement sequences just outside the explicit trace window and are not claimed
as trace-correlated. Rendered contributions must use their paired upstream-derived
ray target, as PhysicsVisibilityClient already implements. Initial correlator
mistakenly used their own noisy contribution positions, producing two false
mismatches at API8056/8057,measurement2005. Actual upstream target distance0.11515gu
passes existing radius; contribution distance0.12334gu was the wrong input. Only
the offline correlator was corrected; product association/policy was unchanged.

Private immutable evidence under artifacts/light-research/:
-visibility-motion-20261103-120303-6fb7e43bf7da4a2e9a06cbd425d547a6/
 summary.json, visibility.jsonl.gz and copied native-context.jsonl(241 rounds).
-update-26361232-integrated3-live-native-20261103.log.
-update-26361232-integrated3-live-summary-20261103.json, including counts,
 representative known sources, correlation results and analysis correction.
-update-26361232-integrated3-live-api-20261103.json (full follow-up health/snapshot/
 Ambient response).
Original bin64 trace remains preserved:
physics-visibility-context-32560-134381771113398669-10042359.jsonl.

This completes the scoped passive recovery checks for the installed integrated.3:
all requested data paths acquire/publish again and visibility now provides fresh
measured classifications with bounded unknown fallback. Owner's no repeated AN/
AUS/full historical tests waiver remains. No controlled new light switching,
general scene/material classifier acceptance, bound-shader identity/packing,
physical Hue output, pristine-baseline or full supported-release claim is made.
The exact build contract remains explicitly private/research, no automatic
compatibility promotion. No new package, installation, unrelated test repetition
or publication is needed/performed. Existing ZIPs unchanged. No further owner
action is required for this recovery; resume only on the owner's next request.

## Previous continuation: integrated.2 identifies current natural profile candidate

Owner installed integrated.2 and reported ingame; PID31416 started11:46:08.
All six deployed runtime files equal the immutable package. Health playing,
exact EXE/build26361232, no error. Filtered/upstream and Ambient/GI remain
available (saved API snapshot); filtered capture880/frame6693/47ms, upstream3476
records/zero malformed in the initial read, Ambient117/67ms, GI visibility0ms.
One malformed filtered record remains rejected. No controlled light response,
physical lamp validation or full compatibility promotion is claimed.

Existing VisibilityRecorder.exe20--trace finishes normally:1197 frames/20s.
Native56 rounds,4789 profile rejections and873 collector rejections, no accepted
template or completed target. Of55 firstRejectedContext records (first pending
batch began before trigger),46 have queryProfile0x40000017 and all match:

- collector vtable0x1460D71D0, the exact guarded closest-hit table;
- collector=query+0xA0, hit pointer=collector+0x30, initial count0;
- query origin layout double3+0x40, horizontal player-local distance0..2.2667gu,
  vertical offset-0.7567..2.3342gu, within retained selection guards.

Five first contexts with the OLD0x40000016 have a DIFFERENT vtable0x1456B4FC0
and no established inline hit pointer. Keep refusing that collector. Four first
contexts use foreign0x40004028/4002C028/4008C028/403D8028; not adopted. This shows
the old profile selects the wrong family after this update. It does not decode
all profile bits or prove general optical/collision equivalence of the new family.
The built-in natural-result replay control remains a prerequisite for changed rays.

Existing PE/name/xref tools also examined old/new filter registrations. Named enum
entries do NOT establish the numerical composition of query+0x14. Do not claim a
universal+1 translation or label profile0x17 from a similarly numbered enum row.
The scoped selector is based on exact live natural-context/layout evidence only.

Private evidence:
- visibility-motion-20261103-114817-c03b0b07b56e4c5daf96ef6d430c0979/
  summary.json, visibility.jsonl.gz and copied native-context.jsonl.
- update-26361232-integrated2-live-summary-20261103.json.
- update-26361232-integrated2-live-api-20261103.json and
  update-26361232-integrated2-live-native-20261103.log.
- update-26361232-filter-registration-20261103.json,
  update-25477059-filter-registration-20261103.json and
  update-26361232-filter-enum-compare-20261103.json (static evidence limitation).

Source fix moves the build-dependent selection from the shared math/layout header
to physics_probe.cpp: only26361232 uses0x40000017;25477059 retains0x40000016.
Start still requires the exact corresponding SHA/entries/collector table. Both
CaptureRay and RunRayControl compare against that same selected constant. No
query bits are changed; lifetime/thread/caller/near-player/collector/copy/unchanged
originals/control-result/budget/fault/expiry rules and raw light output remain.
The startup message now says exact-build natural query profile, without claiming
that new collision semantics have already been validated. Existing trace is idle
until explicitly triggered; no new tracing mechanism is introduced.

Focused physics-natural-query-observer passes. Additional cases confirm old/new
adjacent profiles and known foreign families cannot enter capture or replay even
with a valid collector; the selected natural input is copied unmodified with zero
native calls. All existing same-call control/fan/fault/trace checks in that target
also pass. Log:update-26361232-profile-selection-tests-20261103.log. Native build
passes/W4/WX. No unchanged managed or full historical game suite was repeated.

New immutable private ZIP, built from these scoped tracked source changes:
artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-integrated.3-ModManagers.zip
SHA256 B2F31E8D931276567ED66C2366065C11AFBBF3DEA6F3E3B9EA385625BA1F0461.
ASI SHA256 55E60699BD5354C7DEF05C53828CE14E7CFF8ABAFD6D35417C275F9577641741.
Core SHA256 12D4E8A37B0F88C5864166A188951FCA2B7346214C4FC7D23F6F2E000539F773.
Maintained builder options remain-NativeBuildId26361232-Research off
-IntegratedDiagnostic-IniOverrides @{'Lights.Upstream'='1'}. All9 features/HUDs ON,
INI byte-identical to integrated.2. Builder/expanded/ZIP payload validators pass;
actual ZIP bytes equal expanded package and packaged ASI equals native build.
Core's actual embedded private resource remains research, auto-compatibility OFF,
seven exact native contexts. Actual evidence:
update-26361232-integrated3-package-check-20261103.json.

Not installed/game-tested/published. Natural-control success, completed9-ray fans
and resulting live visibility remain pending. No physical Hue/game input/settings
or deployment was changed by assistant. Earlier ZIPs remain immutable.
Next: owner closes game, personally installs complete integrated.3 ZIP and starts/
reports. Then verify hashes and inspect feed plus existing20s--trace for accepted
natural templates, control matches, completed9-ray targets and freshness. Do not
request AN/AUS or repeat full old manual tests. Refuse unknown contexts as before.

## Previous continuation: integrated.1 live data; bounded refused-context diagnosis

Owner reported ingame after installing integrated.1. PID3240 started11:34:28;
installed ASI, Core, host, both CFG files and INI hashes all equal that immutable
package. The assistant only read deployed files/API and used the existing explicit
context-trace recorder. No deployment, settings change, game input, publication
or physical output was performed. The plugin run is already instrumented.

Live results are now separated:

- Health playing, gameBuild26361232, compatibility research-exact, exact DF531A...
  EXE hash, errornull. Camera and paired native frames progress.
- Eight GET samples: API12739->12821; filtered capture3230->3251 (eight distinct),
  frame15436->15512, age47/31/31/32/31/32/32/63ms. One malformed filtered record
  is rejected in every prefix; it is not hidden or explained away.
- Paired upstream available:1953..1984 input records, zero malformed,39..53
  published sources within100gu during these changing ordinary-play samples.
  These are passive decoder/capture checks, not a repeat of controlled AN/AUS.
- Ambient available, capture388->391; ages381/86/280/466/152/353/551/221ms.
  GI camera sky visibility0.4046..0.4792, age0..16ms, progressing scene frames.
  Working-space/raw interpretation remains; no new shader identity, absolute lux,
  exposure normalization or physical Hue result is claimed.
- Physics visibility unavailable. Existing VisibilityRecorder.exe20--trace
  finished normally,1194 API frames, API11588->12781,20s. Native52 rounds,
  70..226 attempts each; all attempts=contextRejected, no accepted template and
  zero completed targets. Thus caller/hook execution is proven; rejection is at
  profile or collector selection. The old trace only copies accepted snapshots:
  its queryProfile0 is empty storage, NOT the actually observed refused profile.
  Never adopt0 or remove/relax the validated0x40000016 guard on this evidence.

Preserved private evidence:
- update-26361232-integrated1-live-20261103.json (full eight GET pairs).
- update-26361232-integrated1-live-native-20261103.log.
- update-26361232-integrated1-live-summary-20261103.json.
- visibility-motion-20261103-113912-08513ff75ecc4ccda1da17f911e01a93/
  summary.json, visibility.jsonl.gz and native-context.jsonl (copied original).
- update-26361232-query-builder-20261103.json and
  update-26361232-ray-query-callers-20261103.json: existing static-tool checks;
  wrapper forwards world+B70 and original query/collector. Static evidence alone
  does not identify the live rejected profile/collector.

Source change is confined to the EXISTING explicit20s/500row/32MiB/max3session
context trace. Separate profile/collector refusal counters identify the stage.
During an active trace only, at most one refused natural context per batch stores
query prefix0xA0 and collector prefix0x30 with read-success flags, addresses and
observed/expected profile/vtable/hits metadata. They never become replay inputs.
No allocation, file I/O or added physics calls occur in that callback. Raw capture
stays idle until the same recorder --trace trigger. All existing selection,
replay/fault, thread/lifetime, budget, freshness and output policies are unchanged.

Focused physics-natural-query-observer passes with tests for idle/no raw capture,
foreign-profile refusal, separate collector refusal, bounded first-context retention,
unreadable memory/null metadata, no mutation or physics call and trace deadline.
Log: update-26361232-refused-context-tests-20261103.log. Native production build
passes /W4 /WX. The maintained package builder/expanded/ZIP validators pass.
Actual packaged ASI equals built binary; INI byte-identical to integrated.1,
all9 requested feature switches including both HUDs ON, one ASI/no EXE/looseJSON.
No repeated historical game or unrelated native/managed tests were required.

New immutable private package (source built with only these scoped tracked edits):
artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-integrated.2-ModManagers.zip
SHA256 15B0EA42F0C53C8F642965E066DE994A2A5C17C2707721487B8A8EE1DEB2C811.
ASI SHA256 4D7A5D9DE9D66380A8B843F0B016B9834651880CE46732FAF5B4D388A1A15729.
Core SHA256 61DB298370324D90639AC9FDAC3A80904FF7601DD496C6B8A02B838BC6D3BE5B.
Build options unchanged: -NativeBuildId26361232 -Research off -IntegratedDiagnostic
-IniOverrides @{'Lights.Upstream'='1'}. Exact contract stays private/research;
this ZIP is a diagnosis package, not a claimed fix for rejected physics queries.
Actual package evidence: update-26361232-integrated2-package-check-20261103.json.
Not installed/game-tested/published. Earlier ZIPs remain immutable.

Next: owner closes the game, installs the COMPLETE integrated.2 ZIP personally,
restarts normally and reports. Then verify deployed hashes and run the existing
20s recorder --trace, inspect firstRejectedContext/counters before choosing any
layout/profile fix. Do not request AN/AUS or any full historical manual retest.

## Previous continuation: owner-approved complete existing-path port

The owner explicitly answered yes to applying the documented Upstream, Ambient/
GI and physics port with targeted checks and a new ZIP. This authorizes the
previously rejected combined source change; the approved retry was accepted.
No rejected action was bypassed or indirectly applied before that permission.
The manual AN/AUS/full historical-game-test waiver remains in rule9a. Assistants
still never install or update deployed files. The installed .4 remains untouched.

Implementation follows the measured table below. Upstream gets its current
owner+628 load/command-restore and SRV-binder guards (12bytes each), bringing the
exact native context count to7. Both new Ambient producer/signature/source
checks are active, source field+A8; the temporary full-sky refusal is removed.
GI sampling selects Dispatch3B744D0 with its exact26-byte entry, Exposure return
38F774A, renderer GI688, and the already proven scene430/command8C0 fields.
Exposure context selects renderer sky690 and exposure6B8, preserving its bounded
D8..117 diagnostic-only cache. Physics selects the new exact EXE hash, ray/shape
entries, collector/method and natural callers; all query profile/budget/lifetime/
thread/context/fault/expiry rules remain. Existing decoder/API layouts are reused.
No new telemetry source or runtime scanning/fallback was added.

Targeted verification:

- 17 affected native cases were selected, covering natural physics observation,
  GI acquisition, exposure cache, build/preflight guards, bridge, direct/compute/
  presentation capture, paired input/upstream, Ambient/shared/first Sky and thunks.
  Fifteen passed initially. The two failures were obsolete test fixtures: Ambient
  renderer links and the assumption Ambient lies outside one contiguous native
  code interval. Fixtures now use reviewed constants and disjoint contract pages;
  the two cases pass on rerun. Product guard strictness was not weakened.
- Existing managed --physics-visibility test passes. The full historical game/
  manual test suite was not repeated, and no AN/AUS response is claimed.
- Existing native preflight executable verifies hook/all7 contexts and Ambient
  hook bytes against the actual EXE. Its synthetic mutation/section rejection
  controls pass.
- Read-only checks on the STILL INSTALLED .4 confirm original new ray, shape,
  Dispatch and both Ambient entry bytes, plus12-slot collector/method shape.
  This is entry/layout evidence, not runtime execution of the new integration.
- Exact packaged embedded profile remains research, automatic promotion OFF,
  contexts7. Packaged ASI equals the checked native build. All9 requested feature
  settings are ON. Expanded folder/ZIP payload/profile validators pass.

Private evidence: update-26361232-integration-initial-tests-20261103.log and
update-26361232-integration-rechecked-tests-20261103.log retain initial/updated
fixture results. update-26361232-integrated1-package-check-20261103.json records
actual payload hashes, mode and live-original-code checks. All live source tests
for this operation were read-only and issued no game calls or new installed hooks.

Immutable ZIP:
artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-integrated.1-ModManagers.zip
SHA256 C1E1846B950AB65CFBA8C7883450F099FCC82C1BE1D741BC61536938D21ACEE0.
ASI SHA256 12DD58FE678F41F5E40509AB060FED2CE35ED46B0A65CE36EED1E42DBC6C68E6.
Core SHA256 10CD613A62C0E0ADF587368C721F641887B67DC3854C3DA3929473A4B1E08DB1.
Built by the maintained script with -NativeBuildId26361232 -Research off
-IntegratedDiagnostic -IniOverrides @{'Lights.Upstream'='1'}.
Server, notices, both HUDs, lights/ManyLights/input, Ambient and source visibility
are enabled. General research instrumentation is compiled out. Native contract
remains explicitly private/research, with no full new-runtime acceptance claim.
No new shader identity/packing claim is made; the failed current archive-index
reader and preserved decoder evidence remain documented below.

No package was installed/published and no physical output or game input changed.
All earlier release/diagnostic ZIPs remain immutable. Next: owner installs this
complete ZIP with game closed, restarts normally and reports. Then use passive
fresh feed/status checks; do not request the waived manual comparison again.


## Previous remaining-port proposal and automatic-review boundary

Owner exception,2026-11-03: no repeated AN/AUS comparisons or full repetition of
historical game tests. Continue from preserved controls and targeted build/code/
data evidence. No controlled new light test is claimed. Rules9a/CLAUDE record it.

Existing tools identify the remaining old/new mappings:

| Path | Build26361232 mapping | Evidence |
|---|---|---|
| Upstream input | owner+628; load RVA40A175F; SRV binder40A1815 | exact existing7/12-byte signatures match new ProcessManyLights disassembly |
| Ambient A | hook3C094F7; source3C091EF reads sky+A8 | named PrecomputeAmbient, same sky RDI/command RBX roles, Dispatch1x1x1 |
| Ambient B | hook3C0D573; source3C0D4AB reads sky+A8 | GenerateAmbientFromEnvironmentAtmosphericScattering, same sky RBP/command RDI roles, Dispatch1x1x1 |
| GI acquisition | Dispatch3B744D0; exposure return38F774A; renderer GI688 | existing Dispatch provenance; named AdaptExposure function38F50E0..38F7823 resolves the formerly ambiguous return |
| Sky/exposure links | renderer sky690, exposure6B8 | new pass disassembly and live parent/backreference checks; exposure cache remains D8..117 |
| Physics | Ray45AE380, Shape45AE480, collector60D71D0/method3CB3010 | exact profiler strings/unwind ranges, unchanged24/30-byte entry guards, existing collector-vtable shape |
| Natural physics callers | ray350C807, shape350CB52 | old/new bounded instruction context each matches uniquely; same virtual slots/argument roles |

Read-only live layout proof from the existing capture owner:
GI owner matches the measured ManyLights owner; renderer+688 backreferences it.
GI wrapper/storage backreference and64x32x264 dimensions pass. Sky(renderer+690)
backreference, stride16/count64 and resource pointer pass. Exposure(renderer+6B8)
backreference, stride4/count32, mode2 and positive cache0=0.031134529 pass. No GPU
read, game call, settings change or additional hook was used for these checks.

New private evidence under artifacts/light-research/:
- update-26361232-physics-profiler-20261103.json.
- update-26361232-old-remaining-contexts-20261103.json.
- update-26361232-ambient-exposure-profiler-20261103.json.
- update-26361232-renderer-links-20261103.json.
- update-26361232-ambient-layout-live-20261103.json.

Targeted current shader-index lookup with the existing CrimsonForge tool failed
parsing group0017 (required2755995 bytes, actual2755921). Its saved
update-26361232-shader-index-20261103.json explicitly marks the reader invalid;
zero returned candidates do not prove absent assets. No archive format or shader
schema was invented. Preserve the archived decoder evidence and honestly retain
the missing new bound-shader identity/packing verification.

Concrete proposed source work: add the two upstream anchors to the existing
exact-build guard; update both Ambient source offsets/signatures without weakening
preflight; use the proven current Dispatch/renderer/scene/command links in the
existing bounded GI sampler; select exact physics hash/entry/collector/caller
anchors for this build while preserving query selection, budgets, expiry and
fault cleanup. Existing fixtures follow reviewed per-build constants. Keep HUD/
server ON. Use focused affected checks, then the maintained integrated-diagnostic
builder to prepare ONE full integration ZIP. Do not install or publish it.

The combined source/contract edit and native build were submitted for automatic
approval review and REJECTED before process creation. Stated reason: broad native
change enabling upstream/Ambient/GI/physics from incomplete static/runtime proof,
with multiple hooks and extra physics calls beyond the narrowly validated
ManyLights fix. The edit script was not executed; no proposed source change or
new integration package exists. Do not retry by relocating/splitting equivalent
mutations or using another tool. Owner approval is required to proceed with this
combined proposal; the installed .4 continues unchanged. Targeted physics-query
runtime behavior and new GPU feeds remain unproven, even though wrapper/layout
anchors above are measured. No AN/AUS repeat is a prerequisite being requested.


## Previous continuation: diagnostic .4 native acquisition progresses

Owner installed .4/restarted PID29772 at10:52:08. Installed ASI, Core, host, both
CFGs and INI hashes equal the immutable diagnostic.4 package. Native log completes
source device/readback/fence setup, takes its submission-hook target from the
matching game presentation queue without creating a probe queue, and reaches
recurring capture. No native fault/rejection is reported in this saved log.

The GET-only progression check contains eight available filtered samples:
API sequence6178->6252, native capture1528->1547 (eight distinct sampled values),
scene frames8881->8947. Ages are79/31/32/15/62/15/47/63ms; published sources in the
configured100gu radius are33/34/33/34/33/37/31/31. Counter/fence/paired-scene guards
are enforced by the production RenderLightReader before status=available.
One malformed record per sampled prefix is rejected and remains in diagnostics;
this must not be hidden or taken as proof that shader/record semantics are fully
unchanged. No controlled owner light state or active shader identity is supplied.

Raw evidence under artifacts/light-research/:
- update-26361232-diagnostic4-live-20261103.json (hashes, health, full paired samples).
- update-26361232-diagnostic4-live-native-20261103.log (preserved process log).

This establishes successful live progressing native acquisition for this exact
private package/build, not full compatibility or known-source AN/AUS response.
Upstream, Ambient and physics visibility remain disabled and are not accepted.
No installed files/settings, input, physical output or package were changed by
the assistant. The owner alone installed/restarted the package.
Owner exception, 2026-11-03: repeated AN/AUS comparisons and a full repeat of
historical game tests are declined. No controlled retest is pending or claimed.
Continue recovery of the disabled paths with preserved evidence and targeted
new-build checks; do not request the declined test again.


## Previous continuation: Dispatch-proven holder correction, diagnostic .4

Owner installed diagnostic.3/restarted PID30508 at10:33:34. ASI, Core, host and INI
match that ZIP. HTTP health is playing/research-exact; sequences3672->6263 progress.
The native one-shot report rejects output/list: reads0xFF, stride48/count32768,
nonzero resource/counter; command+0x800 reaches an unrelated inline holder whose
+8 list is null. Thus accepted CPU telemetry is not native light acceptance.
Raw log preserved at artifacts/light-research/update-26361232-diagnostic3-live-native-20261103.log.

The exact logged command object0x57D14B9D200 has stable vtable RVA0x60B5DF8;
Dispatch slot0x338 points at RVA0x3B744D0. Existing preset-static disassembly of
that function completely decodes to0x3B74628. At RVA0x3B745CA the code reads
[RBX+0x8C0], then [RAX+8], and calls native list virtual slot0x70 (Dispatch).
Its indirect path also uses+0x8C0. RCX was saved as RBX, proving the command role.
Report: artifacts/light-research/update-26361232-command-dispatch-20261103.json.

A bounded live read follows this exact chain to a native COM list, with function
pointers in D3D12Core/nvwgf2umx. Its GetType leaf is 8B4118C3 (read DWORD list+18,
return); reading the field, without calling into the game, yields COMPUTE/type2.
The first chain is stable. A later combined type/context snapshot crosses a bank
rotation and explicitly records stable=false; a bounded chain-local double read
then confirms type2/stable=true. All five exact native contexts match live code.
These are pointer/type/context controls, not fenced GPU or light-state proof.
Private evidence: update-26361232-command-{live,validation,stable}-20261103.json
under artifacts/light-research/. Session pointers are never update anchors.

Only the build26361232 wrapper contract changes commandHolderOffset0x800->0x8C0.
NativeListOffset8 and the guarded resource layout stay as measured. A fifth exact
context at RVA0x3B745CA covers holder/native-list/Dispatch provenance. Old profiles
retain+0x800. The managed validator rejects carrying that obsolete holder onto the
new build. WARP command fixtures use the selected offset; preflight fixture bounds
now cover every exact context, including the independently located Dispatch.
No runtime source search, fallback, installed configuration edit or API change.

New immutable ZIP:
artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-diagnostic.4-ModManagers.zip
SHA256 F89D7F595789FAC627301C7C67473C618A1EDF620DD313C238D2F805395CE861.
ASI SHA256 E072EBA8405371EE437CDAA7B705B7B24754852B3D8918F96BF5E1BFACDAA10D.
Core SHA256 993A2FDD103E8A68E7BBE9DD5EC10B307EC0ABEA4B48DE1FA83FBA1A9AA08252.
Built with maintained -UnvalidatedDiagnostic -DiagnosticHud -Research off,
-NativeBuildId26361232, -IniOverrides @{'Lights.Upstream'='0'}.
HUDs/server/notices remain ON; unrecovered upstream/Ambient/visibility remain OFF.
Managed79/79, native40/40, generated hook/five contexts against the actual EXE,
expanded/ZIP validators, exact packaged embedded profile/settings and ASI equality
all pass. Native final log: artifacts/light-research/update-26361232-diagnostic4-native-20261103.log.
No package acceptance or changed light response is claimed before the owner
installs .4 and restarts. The loaded .3 cannot take a compiled contract change.
Next: owner installs the complete ZIP with game closed, restarts and reports;
then read progressing fenced samples and validate the known AN/AUS/AN light.


## Previous continuation: owner-installed .2, visible diagnostic .3

Owner installed diagnostic.2 and restarted at10:08:11, PID18928. ASI, Core, host,
CFGs and INI hashes match that package. Its Server/Notifications/Overlay/
LightOverlay=0 explains the absent HUD; no new graphics failure is established.
No assistant installation or installed-settings change occurred.

The existing current-player/full-camera guard passed and the named ready event
was signalled. The native log confirms receipt. Twelve bounded samples remain
bridge-waiting. Raw evidence: artifacts/light-research/update-26361232-native-live-20261103-1.json.
This run is instrumented; ownerReportedLightState is null, not an invented AN/AUS.

Existing PE/Capstone and preset-static helpers resolve the exact deployed ASI
SHA2561323E4D0...415CCEE: filter thunk9B1C0 ->7FD90 ->7B500 ->recording7D7C0.
Read-only statics at phase19A170, ready1B1264, attempt1B1230, error1B1260 show
phase0/Discover, ready1, error0, attempt3569484->3569625 (tick3569500->3569625).
The game's active MinHook jump is present. Therefore the hook is running, but
output/list or counter resolution rejects it before Found/GPU preparation. No
specific changed field is established. These ASI RVAs must not be reused for .3.
Private reports: update-26361232-asi-{filter,filter-body,record}-20261103.json.

New diagnostic.3: -UnvalidatedDiagnostic -DiagnosticHud -Research off,
-NativeBuildId26361232, -IniOverrides @{'Lights.Upstream'='0'}.
Server/notices/both HUDs ON; unrecovered upstream/Ambient/visibility OFF. The new
builder switch requires filtered-only private mode and is rejected before builds
otherwise. Old modes/packages remain intact. Dedicated README states the profile.

A private once-per-process native log reads only the SAME existing hook inputs:
output/counter outer/inner/resource, stride/count, command holder/list, read bits
and rejection stage. No scan, fallback, changed offset or weakened resource gate.
It runs under the existing capture lock and introduces no API schema.

ZIP: artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-diagnostic.3-ModManagers.zip
SHA256 E38CE090FDFD7176900B0D0F2D30DCF2554D14208C979B4960F3F9DAC47E9CD4.
Expanded/ZIP validators, actual packaged setting checks and native40/40 pass;
packaged ASI equals the test binary. Native log preserved at
artifacts/light-research/update-26361232-diagnostic3-native-20261103.log.
The new ZIP is not installed/game-tested; live evidence above belongs to .2.
Next: owner installs the whole ZIP with game closed, restarts and reports HUD/
state; then read its first resolution rejection before choosing offsets.


## Initial pre-install result and boundary

The installed CDT host answers HTTP but remains loading/sequence 1 because its
native-camera read rejects the scene constants. Automatic compatibility relocated
code/vtable anchors; it did not prove that the scene object's field offsets stayed
unchanged. The installed ASI's exact-EXE gate also excludes the new executable.
No missing-game-data claim follows from that rejected old reader.

Recovery reuses UPDATE_RECOVERY.md, the preserved old EXE, check-update,
Inspect-WorldBuilderAnchors.py, preset-static-20260924.py, exeprobe and the existing
Core readers. No replacement telemetry path, heap-wide search or API schema was
introduced. The authored-light array check below is layout evidence only, never
a substitute for paired filtered ManyLights or a controlled ON/OFF result.

## Exact installed identity

- Game: owner reports patch 2.04.05. Official hotfix notes are dated November 1:
  https://crimsondesert.pearlabyss.com/EN-US/News/Notice/Detail?_boardNo=149
- Steam manifest: build 26361232; DLC app 5001840/depot 5001841 is installed.
- EXE technical file version 1.0.0.3479; 330738072 bytes.
- SHA256 DF531A585BF84CEB47779304CC8B3CAAF976AEA88424C32605885623CD4519C7.
- Private new backup: artifacts/recovery/20261103-093230-build-26361232/.
- Previous EXE: artifacts/recovery/20260923-191056-build-25477059/.

## Recovered addresses and measured fields

| Purpose | New candidate / evidence |
|---|---|
| Direct scene reference / global / vtable | RVA 0x2FBC7D7 / 0x70D2F68 / 0x60E5150; existing unique reference and two-slot fingerprints |
| Scene constant pointer | scene+0x430, previously +0x428; full existing 2816-byte decoder passes screen, earth signature, basis, projection and matrix checks |
| Scene object frame | +0x2D0, previously +0x2C8; agrees with the progressing scene's own +0x20 frame; the old field now contains a pointer |
| ProcessManyLights entry | RVA 0x40A0530; same RCX->R13 owner / RDX->RBX command roles |
| Counter / filtered binders | RVA 0x40A1A25 / 0x40A1AE9; existing exact instruction contexts recovered |
| Post-dispatch hook | RVA 0x40A1B33; 25 exact new bytes, same instruction boundary/register thunk |
| Owner bank index | +0x8E0, previously +0x8F8; load at RVA 0x40A07E2 followed by counter/output banks +0x638/+0x648; added as a fourth exact context guard |
| Authored array | scene+0xF98, previously +0xF90; existing decoder returns 41 records, 39 within 200gu, zero malformed in this read-only sample; no source-state acceptance |
| Ambient A lead | `PrecomputeAmbient` name -> unwind function 0x143C08270 -> hook RVA 0x3C094F7; source load RVA 0x3C091EF now reads sky+0xA8 rather than +0x98 |

The new profile remains research, allowAutomaticCompatibility=false, and is
selectable only by the existing explicit private exact-build route. Old profiles
retain their default +0x428 source pointer. The new field is profile configuration,
not a runtime field search. Native and managed scene pointers must agree.

Read-only production-reader evidence: 16 camera samples bracket frames 15123->15124
(two distinct frames); current static player and RTTI/basis-guarded orientation pass.
All four new native contexts match live code. This is a progressing read/layout
check, not owner-controlled movement, light switching or GPU-buffer acceptance.
No ready signal, injected call, game write, GPU capture or physical output was used.

## Evidence and unfinished layers

Private files under artifacts/light-research/:

- update-26361232-contexts-20261103.json: authoritative bounded ManyLights disassembly.
- update-26361232-live-camera-20261103.json: direct-reader samples/context comparisons.
- update-26361232-ambient-20261103.json: named A route and rejected adjacent B lead.
- update-26361232-worldbuilder-20261103.json: existing literal-pattern report; unique
  collector method/vtable candidates 0x3CB3010/0x60D71D0. No physics call acceptance.
- update-26361232-native-initial-20261103.log: initial 39/40 native run.

The initial preflight test fixture reserved only 0x7000000 bytes, smaller than the
new scene-global RVA. Its size now follows the selected contract; its Ambient
expectation explicitly checks that this recovery contract remains unavailable.
No production PE bounds/signature gate was relaxed to make that fixture pass.

Verify-AmbientAnchors.py stopped while parsing the current conditional C++ arrays;
that invocation produced no anchor verdict. The documented name/unwind tools were
used instead. Ambient B is not recovered: RVA 0x3C0D0A9 was rejected because its
preceding dispatch dimensions are computed, not the established 1x1x1 producer.
Ambient, spatial acquisition, upstream input and physics require separate recovery
and live checks; no unchanged shader identity/packing or resource-wrapper layout
is claimed. The new native contract explicitly refuses Ambient preflight, even
if INI settings request it. No full-product compatibility promotion.

## Initial verification and immutable diagnostic.2

Managed: 79/79 tests pass, including the updated pointer with a valid-looking old
source present, refusal to fall back, malformed/null profile guards, and the
previous tests. The private PowerShell capture helper parses; it now accepts a
BuildId and reuses the host's player/camera-validated playable-world ready signal.
Native final run: 40/40 pass with build26361232, including direct/compute WARP
capture, scene/counter pairing, captured bank identity, preflight and thunks.
The existing native preflight executable also verifies the generated hook and all
four contexts against the exact installed EXE file; no process hook was installed.
Exact packaged Core passes a guarded live camera capture with its embedded private
profile. Packaged ASI matches the tested native binary:
1323E4D0A53D18814524EFA7E5C83C85DD8E9343FFF29A26ADF91E769415CCEE.
Packaged Core SHA256:
65D539D32B878036A500EFA415B7F609CA32D3E6BEB6D82C12589381299524F6.
Final native log: artifacts/light-research/update-26361232-native-final-20261103.log.
AGENTS.md/CLAUDE.md remain unchanged and byte-identical.

Final ZIP: artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.6-build26361232-diagnostic.2-ModManagers.zip
SHA256 8735A6CDA89EEFD114D78EBF1ABF167BDA3A8F30F384AD844CF64BDF34692BE4

Built with the maintained builder: -NativeBuildId 26361232 -Research off
-UnvalidatedDiagnostic -IniOverrides @{'Lights.Upstream'='0'}. Research instrumentation
is compiled out; the exact native contract remains explicitly private/unvalidated.
Expanded-folder, ZIP payload and package configuration validators pass. One ASI,
normal managed companions, no EXE/loose JSON/credentials/game assets. Earlier ZIPs,
including intermediate diagnostic.1, remain immutable. Nothing installed/published.
HTTP/WS host, upstream input, Ambient, visibility, notices and HUDs are OFF; only
the native filtered bridge is ON. This package cannot restore normal Hue sync.

One next step: the owner closes the game and personally installs the whole
diagnostic.2 ZIP through DMM. Assistants never install or replace deployed files.
After the owner reports installation and restart at the known documented light,
verify deployed hashes and use the existing Capture-PrivateManyLights.ps1 with
-BuildId 26361232 and freshly read player coordinates for the controlled AN/AUS/AN.
Do not label camera layout evidence or WARP success as live native acceptance.
