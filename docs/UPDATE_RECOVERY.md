# Update recovery — current direct-camera / ManyLights path

Current implementation: [conditional runtime recovery](RUNTIME_RECOVERY.md), based
on the [build26423880 reference](BUILD-26423880-RECOVERY.md). The plugin now requests
its own complete address recovery only on an unknown EXE or failed startup guard.
The shipped host performs the existing file-backed searches, waits for a validated
live world, and hands a bounded candidate to independent native validation. Cache,
progress and terminal failure handling are included. No external recovery tool or
download is needed for unchanged-layout address shifts. The baseline's research
status is preserved; recovered is not a tested-build promotion.

The remainder preserves the manual workflow and historical reference addresses.
Its old exact-build-only restrictions describe the previous path; runtime recovery
is the narrowly validated exception described above. Do not use historical offsets
as defaults or repeat a full manual investigation before inspecting recovery status.

Assistant reference, not user homework. Start here after a game update, not at the
oldest fire handover. This recipe and its three reused archive tools travel with
the product Git clone. Raw game material stays private under ignored `artifacts/`.

## Safe first check

```powershell
dotnet run --project src/CrimsonDesertTelemetry.Cli -c Release -- check-update 'C:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'
```

This reads the EXE on disk; it neither opens the process nor installs hooks, writes
a profile, or promotes an unknown build. Read each anchor result, not just the exit
code. A relocated candidate is a lead, not proof of compatible object layouts.
The `check-compatibility` command runs the runtime automatic-compatibility resolver.
It now has one template per camera layout: `build-24994088.json` (historical context
chain) and `build-25477059.json` (direct camera). The direct template relocates only
the camera: the scene global comes from the RIP-relative load pattern, the scene
vtable from two slot fingerprints. The renderer camera/scene class has **no MSVC
RTTI locator** (the qword before vtable RVA0x5D20718 on 1.0.0.2976 is an ordinary
function pointer), so RTTI cannot guard it; the slot fingerprints are its type guard,
while the player chain keeps its three RTTI names. Slot 2 is byte-identical to the
1.0.0.2658 camera fingerprint; slot 1 differs only in two register-allocation bytes,
which are wildcarded. On the 1.0.0.2976 EXE the resolver reproduces the validated
profile exactly (reference RVA0x2D14367, global RVA0x6C8CF30, vtable RVA0x5D20718,
frame counter +0x2C8). A relocated profile never inherits `engineLights` or
`nativeCapture`: lights and native instrumentation stay exact-EXE-only.

The shared baseline is `definitions/build-25116796.json`, including `nativeCapture`.
CMake generates the native contract from it. Build hash, scene global, hook and
surrounding register/binding signatures must agree; never fix an update by changing
only a hash. Native game instrumentation remains exact-EXE-only. Graphics notices
start independently and can explain an unsupported build without enabling that
instrumentation. Normal loading is silent; ready requires playing/fresh requested
data. Unsupported graphics can still prevent notices: bootstrap/native/overlay logs
are the fallback, not a promise that every failure can be rendered.

Baseline: Steam25116796, EXE1.0.0.2760, 379781016 bytes, SHA256
`4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454`.
Preserve this EXE privately BEFORE Steam overwrites it:

```powershell
.\scripts\Backup-GameExecutable.ps1
```

Idempotent by content, so running it twice costs nothing; it files the copy under
`artifacts/recovery/` with a manifest and never overwrites an existing one.

This is not bookkeeping. On 2026-09-11 the spatial probe's anchor could not be
relocated from its own signature -- an ordinary function prologue occurring 1390
times in the image -- and was only recovered by reading 32 bytes of context at the
old address in the PREVIOUS executable and searching for that. It was unique on the
first try, and it showed the anchor had moved by the same +0x21C0 as the ambient
pair, which is itself the evidence that the region shifted rather than changed.
None of that exists without the old binary. The former lost EXE cost us
RTTI/provenance evidence; do not repeat that failure.

## Recover only the broken layer

| Layer | Existing anchor / proof to repeat |
|---|---|
| Player | Static XY/Z stores share a unique writer; RTTI-guarded world→actor manager→player→control→owner→physics chain. Current owner+0x2B8, formerly+0x298: offsets really can change. Require movement and orientation controls, not plausible XYZ alone. |
| Direct camera / authored lights | RIP-relative direct-camera load resolves scene global RVA0x6B4EFB8; scene vtable RVA0x5C01A20. Camera frame counter+0x2C8; scene constant pointer+0x428. Authored array is a different source: scene+0xF08→descriptor+0x10/+0x18, stride0xB8. |
| Native capture | ProcessManyLights function entry RVA0x3CB5000, paired counter R15 and output R12 binder contexts, then Dispatch(512,1,1). Hook RVA0x3CB65CA, NEVER0x3CB65C9 (mid-instruction, caused a crash). Owner R13, command wrapper RBX. Shared JSON contains exact bytes and layouts. |
| Resource pairing | FilterOwner+0x8F8 is bank index; counter outer at owner+index*8+0x638, output outer+0x648. Outer+0x30→inner; inner+0x168→resource. Output stride/count must be48/32768. Command wrapper+0x800→holder+8→command list. Check descriptions and queue states; resource pointers alone prove nothing. |
| Readback | Output and256-byte counter copy on the same command list/fence; UAV→COPY_SOURCE→UAV. Scene/frame/pose paired before/after; stale/torn data rejected. BridgeABI2 remains unchanged. |
| Shader semantics | Counter DWORD1/byte4 is the valid filtered prefix. 48-byte record XYZ+0 camera-relative, RGB+16 linear HDR, cone half+38, direction halfs+40/+42/+44. Tail records may look valid forever: marker≈π is NOT a live-count test. |

For a changed function, use old/new disassembly and known binder/Dispatch provenance
before any new hook. A whole-function shift is simpler than changed register lifetime.
The offline checker reports candidates; it cannot prove command-list state, active
shader identity or runtime-unpacked branches. Do not return to broad heap scans or
test the previously untested UAV candidates unless this confirmed path fails.

## Shader contract

An unchanged EXE does not prove unchanged shader assets. The current implementation
has **no active-PSO/shader-identity gate**. Numerical plausibility is insufficient;
shader-only updates remain an explicit risk. These are inspected archived variants,
not proof that the current live PSO uses precisely that variant.

Reuse CrimsonForge (local `external/crimsonforge`, reference revision
`09f821638da4e1f04b26be43af408b4d59337ad3`, upstream
<https://github.com/hzeemr/crimsonforge>) rather than reconstructing the archive format.
Preserved tool sources in `tools/update-recovery/` come from research commit`af5485b`:

1. `Find-ArchiveLightAssets.py --forge ... --packages ... --group 0017 --pattern '\.padxil$' --out artifacts/light-research/<fresh-index>.json`
2. `Read-ArchiveLightAsset.py --forge ... --deps <matching-Python-deps> --index ... --group 0017 --path <exact-indexed-path> --out artifacts/light-research/<fresh>.padxil`
3. `Inspect-ArchiveShader.py --input ... --dxc <SDK-dxc.exe> --out artifacts/light-research/<fresh-prefix>`

Run Python with `-B`; tools keep game files read-only, require fresh output paths and
save validation/hashes. Indexing needs CrimsonForge dependencies; use its requirements
with a matching Python environment (our saved lz4 is cp312, not Python3.14). Disassembly
uses SDK DXC. Treat zero results/decode failures as invalid readers until controlled.

Exact case-sensitive UTF-8 entry-name lookup:
`core.crypto_engine.hashlittle(entryName, 0xC5EDE)`.
Controls: CSMainSkinnedMeshStreamOutVertexData→3964b6b0,
RenderDiffuseCS→4c5a6808, PadClipmapBorderTexelsCS→051aeafb.
Group0017 current path family:
`shadercache__/63bb3e83_9d3ccf48_5_<entry>_3_deba1dcd_b13a9f29.padxil`.
An update can change group/family/variant; use the fresh index, not an assumed path.

| Kernel | Entry hash | Existing evidence |
|---|---|---|
| ProcessManyLightsCS | 29588660 | atomic add at counterbyte4 → output index×48; DXIL hashc40f89b9b04627219c4b77a1736da3b7 |
| InitSortingDataCS | 84574902 | consumes byte4 as valid bound |
| InitSortingDataIndirectCS | 5eecfe7e | independent byte4 bound |
| SetDispatchIndirectArgumentsRecursiveCS | a2b2b7e9 | dispatch setup, do not confuse source/filtered counts |
| InjectLightsCS | b606b219 | exposure-dependent RGB path |
| InjectLightGroupsCS | 05125ef9 | grouped exposure-dependent RGB path |

Local evidence prefixes in `artifacts/light-research/`:
`filtered-count-exact-20260906-2113-<entry>` and
`light-rgb-inject-20260906-2215-<entry>` (`.padxil`,`.dxbc`,`.ll`,hash metadata).
Original index: `crimsonforge-shader-index-20260905.json`.
Source/current-render RGB are different contracts: exposure normalization is still
unfinished. Do not silently divide/smooth RGB while repairing compatibility.

## Preserve and revalidate

### Additional native-object / physics anchors (2026-09-24)

[WORLD_BUILDER_RESEARCH.md](WORLD_BUILDER_RESEARCH.md) records a pinned upstream
source and current patch 2.03.02 checks: native create/enable, world global,
actor/UUID routes and physics ray/shape functions. It also distinguishes useful
alternative player/camera anchors from our stronger frame-paired production path.
Use `scripts/Inspect-WorldBuilderAnchors.py` on each preserved new EXE to report
all literal-signature matches; command and limitations are in TOOLING.md.

For broken native-object/physics anchors, prefer named profiler string references
plus unwind metadata, RTTI or collector-vtable shape as independent controls.
Do not take the first candidate: current `TtCastShape` already has two functions.
Keep source revision, EXE hash, all matches, calling context and live validation
separate. This supplements our recovery procedure; it neither relocates the
ManyLights/ambient producers nor proves unchanged field offsets or shader assets.

### Existing evidence backup

`scripts/Backup-UpdateEvidence.ps1 -ExecutablePath <baseline-exe>` creates and verifies
private product/research Git bundles, a baseline EXE copy and the small selected
shader evidence/index, with a hash manifest in `artifacts/recovery/<fresh>/`.
It excludes huge PIX/video/API captures and uncommitted files (reported in manifest).
This is same-disk recovery, NOT off-device backup. Never publish EXE/shader bytes,
the private index or bundle of research without reviewing its contents.

Before accepting a new build: definition validation and all unit/native tests;
offline old/new anchor comparison; guarded cold launch with success/error notices;
progressing camera/movement control; light AN→AUS→AN at an already documented lamp;
fresh prefix-count/scene pairing and one stable colored source. No fresh GPU buffer
read can by itself establish permanent physical IDs, full360° coverage, or OFF state.
Save the new EXE/evidence privately and commit the validated profile/provenance.
# Global sky preview recovery addendum (2.0.1-sky.1)

Sky reuses the exact-build capture gate and paired scene layout. Its additional
producer signatures/context checks currently live in `src/ambient_probe.h` and
`CheckAmbientPreflight` in `render_capture.cpp` under the native ASI. Only native
A RVA3849BB7 is enabled for the stream; B384CBA3 is diagnostic only. Preflight
runs before installing ManyLights, since it also verifies those original bytes.
Failure disables sky independently; never relax the hash/signatures to force it.

`docs/AMBIENT_DECODE.md` preserves the six identical shader-body variants, source
chain/layout, SH packing, exact float32 normalization/color matrix and captures.
`SkyAmbientReader` is cross-checked against `scripts/AmbientSh.psm1` through the
test runner's `--replay-sky <ambient-derived-candidate.json>` option. Shared WARP
tests cover either first producer, direct/compute queues, real submission fences
and feed isolation. Do not rediscover these facts after an update.

The offline update checker now includes the full acquisition anchor set when the
reference contains Support metadata. The runtime resolver adds role/unwind checks
for ambiguous signatures. Neither verifies bound shaders: EXE identity does not
detect all shader-only asset changes.
Before promoting a new profile verify producer/context bytes, resource layout,
paired scene frame, shader packing/body and a short live combined-feed check.
