# Build26423880 recovery — 2026-11-04

Owner reports an overnight update, leaves the game running and requests a combined
recovery using existing tools, plus merging the OpenRGB branches to main. No new
AN/AUS comparison or full historical game test is requested. Installation remains
exclusively the owner's responsibility. No deployed file or game setting changed.

## Identity and initial failure

Steam manifest3321460 reports26423880, DLC5001840/depot5001841 installed. EXE
1.0.0.3527,328899480bytes, SHA256
DC4237F21D6CE1E49F8E5D6FA89BF803FBBE6549123078EFDC393265C7BF6841.
Backup-GameExecutable.ps1 preserved it at
artifacts/recovery/20261104-093224-build-26423880/CrimsonDesert.exe.
Previous26361232 EXE remains preserved. No marketing patch number is inferred.

Installed PID6340's native log refuses the changed hash as designed. HTTP and
WebSockets answer, but the automatic25477059 camera template uses the old+428
source and reports loading/layout-invalid. This is a rejected old reader, not
evidence that current engine data is absent. Hue's neutral consumer needs no
game-build allowlist change.

## One combined existing-path port

Reused check-update, Inspect-WorldBuilderAnchors.py, preset-static-20260924.py,
preserved old/new EXEs and production Core read-only player/camera/light readers.
One-off private artifact scripts only orchestrate those PE/Capstone/read APIs;
no new telemetry source, heap scan, game call, injected hook or capture was used.

| Anchor | Current evidence / value |
|---|---|
| Scene reference/global/vtable | 2FC2147 /70DA028 /60EB0D8; unique reference + two-slot fingerprint |
| Scene layout | source+430, frame+2D0, authored array+F98 retained; full2816-byte decoder passes |
| ProcessManyLights/hook | 40A5F90 /40A7593; all7 exact context bytes retained, each+5A60 |
| Input/binder | 40A71BF /40A7275; owner+628 and register roles retained |
| Command/owner bank | +8C0 /+8E0; exact Dispatch and load contexts retained |
| Ambient A/B | hooks3C0EF57 /3C12FD3, source loads3C0EC4F /3C12F0B; sky+A8, same1/1/1 producers |
| Spatial Dispatch/exposure return | 3B79F30 /38FD13A; renderer GI688 /sky690 /exposure6B8 retained |
| Physics shape/ray | 45B3EE0 /45B3DE0; ray selected through named TtWorldCastRay, not first matching prologue |
| Collector/method | 60DD908 /3CB8A70; unique12-slot shape, method slot5; live read agrees |
| Natural callers | shape3512502 /ray35121B7; existing guarded dispatch roles retained |

The old exact0x40000017 natural profile remains the candidate; no bits are guessed
or rewritten. Same-call control, collector/lifetime/thread/budget/fault/freshness
checks remain. Actual natural-profile acceptance and completed nine-ray results
on this EXE require the installed candidate. An adjacent/foreign profile must
remain unknown. Static relocation is not collision/optical acceptance.

Read-only sample:24/24 distinct progressing scene frames,24/24 validated player
orientations, authored reader available46sources. Authored data is layout evidence
only and is never substituted for paired filtered ManyLights. No new GPU buffer,
shader identity/packing, light switching or physical Hue/OpenRGB result is claimed.

## Making future recovery more robust

The new definition centrally contains11 bounded acquisition anchors plus their
layout/return/vtable values. CMake generates the native constants from that same
definition. Legacy contracts remain reproducible. All11 new contexts are checked
together before any native game hook installs; individual component checks remain.
Malformed/missing layouts, foreign query profiles and weak recovery patterns fail.

The existing check-update now covers Ambient, GI/exposure and physics in addition
to camera/player/ManyLights. Exact profiles check bytes at their guarded locations;
short repeated input fingerprints no longer require image-wide uniqueness. On an
unknown EXE a repeated ManyLights context can be verified at a bounded relative
position from an already uniquely recovered context. This is offline evidence,
never a runtime profile or hook authorization. Collector recovery requires the
unique method plus12-slot shape. Current full report:28matched anchors; authored
layout explicitly unverified; status exact-research-anchors-checked, activates=false.

This is not an in-plugin automatic recovery implementation. Owner's follow-up
suggestion is feasible as a conditional, per-EXE diagnostic/recovery mode. Unknown
addresses alone cannot establish field layouts, native profile semantics or shader
contracts. Current automatic compatibility and native exact-hash gates are not
weakened. Startup search/cache/automatic activation needs a separately tested change.

## Evidence and package status

Private evidence under artifacts/light-research/update-26423880-* includes initial
feed check, World Builder matches, named profiler contexts, exact byte relocation,
live layout and full check. The first live entry report compared five ambiguous
ray candidates to one last-candidate byte string, causing four diagnostic comparison
mismatches. This was an artifact-reporting mistake, not four changed live entries;
use selected TtWorldCastRay and the final exact-profile report for guard acceptance.
All raw reports are preserved.

Managed79/79 and Hue161/161 tests passed initially. Native/package validation is
recorded in the current HANDOVER after completion. Target paired packages:
CDT2.2.8-build26423880-integrated.1, production code with general research compiled
out, exact research contract/all9 features ON; Hue1.0.5 with optional OpenRGB.
No public release tag or asset publication is requested/performed. Installed
acquisition and the latest OpenRGB color appearance remain pending owner feedback.
