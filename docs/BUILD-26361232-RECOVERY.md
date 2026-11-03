# Patch 2.04.05 / build 26361232 recovery — 2026-11-03, Codex

## Current continuation: owner-installed .2, visible diagnostic .3

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
