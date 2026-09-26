# Light-source identity — 2026-09-26

**Owner's main investigation:** find real engine instance identity, especially for
moving lights. Do not substitute positional/motion tracking. No production API or
plugin change in this investigation. The separately accepted mass-flash fix stays.

## Result and limits

The current game has a directly reachable **ClientSyncSceneObjectManager UUID
registry**. Read-only live enumeration in PID26452 resolved **8,830 entries** to
`pa::SceneObjectClient`; every registry UUID exactly matched its client's 16 bytes
at **client +0x200**. This is engine identity, not a generated proximity ID.
It is NOT yet a verified identity for each published ManyLights contribution.

Within100gu of the current player (-9784.19,633.7999,-99.4503):5,116 objects,
5,116 distinct UUID byte strings;200 prefab names match lamp/torch/candle/brazier/
fire_spark. These are scene-object candidates, NOT200 active light sources.
There are multiple UUIDs for identical prefab names at effectively identical
positions. Distinguish authored/runtime children and effects before choosing the
physical source level; never deduplicate these identities by position alone.

Two snapshots about138s apart contained no matching object moving >0.1gu. They
therefore do NOT validate tracking an NPC torch. UUID lifetime across unload,
save/load or restart is also unproven. Zero read failures and stable registry
headers are consistency checks, NOT an atomic snapshot or lifetime guarantee.

## Current-build read path (verified)

Crimson Desert2.03.02, Steam25477059, EXE1.0.0.2976, base0x140000000.
SHA256 `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`.
Preserved EXE: `artifacts/recovery/20260923-191056-build-25477059/CrimsonDesert.exe`.

```text
world     = *(base + 0x6D69190)
container = *(world + 0xC8)
manager   = *container                  RTTI pa::ClientField
registry  = *(manager + 0x80)           RTTI pa::ClientSyncSceneObjectManager
registry +0x60 u32 bucketCount; +0x64 u32 entryCount
         +0x70 bucketArray; +0x78 nodePointerArray
bucket   = bucketArray + bucketIndex * 0x100
bucket +0 u32 count; +8 array of {hash u32, nodeIndex u32}, stride8
node     = nodePointerArray[nodeIndex]
node +8  UUID[16]; node +0x18 -> pa::ClientSyncSceneObjectData
syncData +0x58 -> client +0x28
client   = *(syncData +0x58) -0x28
client +0x200 UUID[16]; +0xA0 position[3] (world-like on tested registry roots)
client +0x60 -> stringDescriptor -> UTF-8 prefab characters
```

Observed current primary client vtable `0x14557E120`; RTTI resolver also lists
0x14557E0F8 and0x14557E110 (not live observed here). Sync-data vtable14559F9C0;
registry14559F8E0; ClientField1455BCEA0. Live registry5A6CA09A800 held509 buckets.
All addresses/offsets are exact-build research anchors, not update-independent.

The native UUID lookup at **0x141851010 (RVA0x1851010)** is independently matched
to World Builder's signature. Inner lookup0x1418510F0 hashes16bytes, searches the
bucket and compares all four UUID DWORDs. Outer function takes a registry lock.
**We do not call it remotely.** The script uses only ReadProcessMemory access.
Its result is not simply a raw scene pointer: out+0 is a vtable; out+8 is syncData,
out+0x10/+0x11 are flags. Current caller0x140743267..0x1407432CE explicitly reads
syncData+0x58 and subtracts0x28 to recover the client.

The `_sceneObjectUuid` named setter at0x1426CF440 copies16bytes to this+0x200.
Caller0x140743307..0x14074331C independently subtracts0x28 from an interior
reference, then passes interior+0x1D8 (=client+0x200) to the lookup. World Builder
also documents **SceneObjectServer+0x1D8**. Always establish class and pointer
origin: **+0x1D8 is not a universal client/light-object offset.**

## Historical source-to-render reference

`research/light-source-tests/CODEX_HANDOVER_FIRE.md` around lines3395–3565 already
proved one physical fire-lamp client source through AN/AUS generations:

```text
old Source0x227BAF37730
  -> source self-wrapper0x227E09DCC50, wrapper+8 = Source+0x28
  <- Owner/Upstream+0x1B0 references the exact same wrapper
  -> Owner WorkItem vector (6 records) -> CPU0xE0 records -> GPU path
```

Old page dumps `rawpages/fire-persistent-source-{on,off,on2}-full-pid23812.bin`
start at0x227BAF37000: add **0x730** before interpreting source-relative offsets.
All three hold UUID bytes `152BE1AF2D1300000000000000000000` at Source+0x200.
At Source+0x1D8 there are changing transform-looking bytes, NOT that UUID.
This supports one lamp's identity across an old-build AN/AUS cycle; it does not
prove current-build moving-source continuity. Old Owner+0x1E8 / record+0xD8 is a
generation/group value, not the stable physical source ID.

## Current-build renderer-to-UUID link — live verified

Read-only PID26452 capture `light-identity-chain-20260926-214049-545.json`
resolved **184 occupied renderer WorkItems to 83 distinct source UUIDs**, with
zero read failures. Multiple WorkItems correctly share their owner's source UUID.
The combined bounded traversal took1.185s. This is NOT184 lights or83 lamps.

```text
global   = *(base + 0x6C8D058)
renderer = *(global + 0x14818)          alternative +0x14820 was null
queue    = *(renderer + 0x6100)         2048 pointer slots
work     = queue[slot]                 work+0x2B8 must equal slot
owner    = *work                       current vtable base+0x5BB77A8
wrapper  = *(owner + 0x1B0)
source   = *(wrapper + 8) -0x28         SceneObjectClient, base+0x557E120
uuid     = source+0x200, 16 bytes
owner+0x2B0 -> WorkItem array; +0x2B8 u32 count; stride0x2F8
```

Every captured WorkItem also lay in its owner's bounded array at an exact stride.
Queue slot, owner, source-wrapper links and UUID were reread before acceptance.
This reduces torn-read risk, but does not make the snapshot atomic or prove
object lifetime. The array/count offsets moved **-8** from historical
Owner+0x2B8/+0x2C0; WorkItem+0x2B8 stayed the queue index. Do not extrapolate that
shift to other fields, especially the old generation ID at Owner+0x1E8.

**Important identity limit:** none of these83 UUIDs matched the earlier5,116
nearby registry rows. Sampled source positions were local-looking, prefab labels
unavailable and parent+0x88 null. Owner transform positions at+0xBC/+0xFC did
include positions near the actual player. These are usable engine effect/source
identities, not yet proven persistent physical-lamp identities. No GPU light
allocation was joined, and no moving NPC torch was followed in this capture.

The forward traversal of200 light-named registry roots visited395 clients and
121 effect components. All121 held a valid source wrapper at Parent+0x258 but
Parent+0x250 was null. This is **not evidence of missing lights or a shifted
offset**: the current source setter still explicitly uses both fields.
The83 reverse-route owners' +0x28 pointers did not match those121 captured
components either; their parent relationship remains to be inspected directly.

### Relocation evidence and reproducibility

- Current association thunk **0x142FA3E30 -> live0x151899210**, relocated from old
  0x142ECF350 through callers at0x1404C2DAD/0x1404C37BE. It writes Parent+0x258,
  gates on+0x5C/+0x250, then invokes current **0x1430E6D00**, which writes
  Owner+0x1B0. Existing source-wrapper helper is0x1403D5170.
- Old Owner method0x143010BB0 relocated to **0x1430E5770**; its live instructions
  establish global0x146C8D058, renderer fields+0x14818/+0x14820 and owner array.
- **0x142F84700 -> live0x151804440** clears renderer+0x6100[WorkItem+0x2B8]
  and resets the work slot. It is a **removal helper**, not an insertion proof.
  Do not inherit the old registration label uncritically.
- Anchors are exact-build only; unpacked addresses are evidence, not durable
  signatures. The old/new preserved executables and live byte dumps are retained.

Run `scripts/Read-LightIdentityChain.ps1 -SnapshotPath <fresh-registry.json>`.
Requires the current PID, a registry snapshot not older than that process, exact
EXE hash, live code guards and existing Core Release DLL. Caps scene roots256,
clients768, depth3 and queue2048; traversal budget10s. It records per-row failures,
unvisited scenes and occupied/inspected queue counts. No remote calls/writes.
After a restart, create a new registry snapshot; never reuse the old pointers.

Evidence under `artifacts/light-research/` (not committed):

- `light-identity-chain-20260926-214049-545.json`: authoritative combined snapshot,
  with bounded scene, owner and WorkItem byte arrays for offline inspection.
- `light-identity-chain-20260926-212755-789.json`: earlier forward-only traversal.
- `rawpages/light-identity-association-live-26452-20260926.{bin,meta.json}` and
  `light-identity-owner-setter-26452-20260926.{bin,meta.json}`: current setters.
- `rawpages/light-identity-workqueue-live-26452-20260926.{bin,meta.json}`:
  owner array loop and queue-removal helper. These dumps start at each exact
  requested address, NOT an aligned page; follow metadata offsets.
- `light-identity-workqueue-25477059-20260926.json`: static report; split unwind
  ranges truncate the owner function. The live dump supplies its continuation.

## Gemini / World Builder input: what to retain

World Builder pinned rev `ee1f05a3ad1a61cd4aee66946155d0315fdc14e7` in
`external/crimson-desert-world-builder`; `notes/GIMMICK_SPAWN.md` documents real
UUID use. `asi/cdmodkit/cdmodkit.cpp:1794` supplies the lookup signature.
Gimmick keys550011/549011, torch550012/549012, item IDs and sync key6687 identify
types/assets/state variants, **not an individual moving torch**. Useful as labels
or switching leads, not source identity. World Builder's own C-API vector indices
and HTTP-owned object IDs likewise are not a universal original-world light ID.
Gemini's claim that four switching paths are already functional exceeds our live
evidence: see LIGHT_CONTROL_RESEARCH.md; no new switching experiment was done.

## Reproduce / evidence

`scripts/Read-SceneObjectIdentity.ps1 -Center @(-9784.19,633.7999,-99.4503)`
performs one bounded snapshot (default10s, max15s), requires the exact EXE hash and
33-byte live lookup prefix, caps tables/indices, validates RTTI vtables and UUIDs,
rechecks node/source links, writes a fresh JSON and exits. Core Release DLL must
already exist. No hooks, remote calls, game writes or full-heap scan. Run only
when a scoped read is useful; no continuous polling or user wait.

Artifacts under `artifacts/light-research/` (outside Git):

- `light-identity-uuid-25477059-20260926.json`: static named setters and lookup
  callers; large/repetitive, select relevant functions instead of printing it all.
- `light-identity-uuid-lookup-inner-20260926.json`: bounded inner disassembly.
- `scene-identity-20260926-211223-185.json`: authoritative corrected snapshot,
 6.65s, all8830 UUIDs matched, near5116;3,431 labels decoded (others unavailable).
- `scene-identity-20260926-211005-209.json`: UUID/position data valid; optional
  prefab decoder was wrong (missing descriptor dereference), labels INVALID.
- `scene-identity-20260926-210807-323.json`: all8830 matched, but center was the old
  camp while player was elsewhere, hence near0; NOT missing-light evidence.
- `scene-identity-20260926-210506-899.json`: initial incorrect assumption that
  node+18 was a source wrapper; it is syncData. Zero matches are INVALID absence
  evidence. Corrected from the actual lookup caller, not a guessed offset scan.
- Initial live guard mismatch was a33-byte prefix compared against a32-byte read;
  corrected length matches live exactly, not a game-version mismatch.

## Feuerschale: second, handle-based effect path — PID29928

The new process reproduces the queue reader (171 WorkItems initially), but that
queue did not contain the nearby fire bowl. Do NOT force this source through the
PrivateEmitter/WorkItem route. Its effect component has Owner+0x250=0 yet a valid
**+0x270 instance handle** and **+0x288 opaque effect key**. Current transform
update0x142FA37C0 explicitly handles BOTH routes; after the Owner branch it calls
the manager below with key, handle and transform. This is code evidence, not a
guessed integer/position match.

```text
global = *(base+0x6C8D018)  (equal to the +0x6C8D058 global in this live process)
renderer = *(global+0x14818)
manager = *(renderer+0x106248), vtable base+0x5BB7978
manager virtual+0x20 = 0x1430EC910
  lookup key at component+0x288 in map manager+0xF4168, else +0xF41B0
  hash-map lookup0x1403D1E80 returns node+0x10, after node+8 key equality
  first map's instance vector = node+0x20; fallback vector = node+0x10
  vector {data pointer, u32 count}, records stride0x68
  0x1430EC820 binary-searches record+0 by component+0x270 handle
  exact matching instance has world translation at +0x48/+0x4C/+0x50
```

Reader `scripts/Read-WorldEffectIdentity.ps1` reimplements this READ-ONLY lookup
without calling engine functions. Requires fresh chain snapshot, exact hash,
PID/start-time and source UUID, component/manager vtables; caps tables and vectors,
checks links again, records table stability, exits after one bounded lookup (5s).
Optional `-CaptureApi` stores before/after API data, explicitly NOT atomic GPU/CPU
pairing. API calls have2s timeouts. A detached source or invalid handle is rejected,
not interpreted as a missing/off light.

Two positive reads, 22:18 and22:20 local, found the same exact instance:

- Runtime source `0x5ECAF5C0AE0`, UUID `447CB15E561300000000000000000000`;
  component `0x5EC4E7A47E0`, Owner null.
- Effect key `0xDEBA1DCD943EBDAA`, handle5191; first map has33 entries, matching
  vector39 instances. Index35 at `0x5EC29FDB638` stores5191 and translation
  `(-9788.865,636.7872,24.67056)`. The lookup used key+handle, NOT position.
- Both map headers stable; bracketing API capture progressed sequence21863→21866,
  frame18126→18137. This establishes a live control, not a GPU allocation join.
- Instance+8 looks like a separate list descriptor (count/capacity6 at+10/+14).
  A short prefix had repeating12-byte patterns, but its element type and relation
  to GPU emitter slots are UNPROVEN. Do not call these six lights or IDs.

Important lifetime observation: the earlier component `0x5EC32FFC990` detached
between captures (wrapper0, key/handle all-ones). The initial two-map negative read
was on that retired component and is NOT absence evidence. The guarded reader
rejects it. Old runtime UUID0ECB4E59… and new447CB15E… differ. Separately, the
authored bowl/effect UUIDs `32A1624E430E74563C6A6EAD00000000` and `…01000000`
remained while their pointers changed. Same prefab/position is NOT yet a verified
authored→runtime parent link. Cause of recreation (owner moved during this turn)
and persistence across restart remain unproven.

Artifacts under `artifacts/light-research/`:

- `scene-identity-20260926-215927-067.json`, chain `…215935-310.json`: first pose;
 7438 registry rows valid;171 queue rows; zero reported failures.
- `scene-identity-20260926-221609-411.json`, chain `…221617-114.json`: refreshed;
 7457 rows valid but registry header changed (not atomic);179 queue rows.
- `world-effect-identity-20260926-221822-730.json` and `…222051-427.json`: positive
  key/handle joins; latter includes bracketing API snapshots. Initial report field
  `assetKey` means the opaque effect key; no full asset-key semantics established.
- `rawpages/identity-{effect-vmethods,effect-handle-route,effect-handle-helpers,
  firepot-parent}-29928-20260926.{bin,meta.json}`: live call-path evidence.

### Owner AUS phase — same bowl, 22:29 local

Owner confirmed AUS after receiving exact bowl coordinates. PID29928 unchanged;
player(-9789.774,635.7512,24.24815). Registry snapshot
`scene-identity-20260926-222933-164.json`:7479 UUID matches, stable header,
zero failures,857 within30gu of the bowl. Chain/API snapshot
`light-identity-chain-20260926-222940-393.json`:79 scene nodes,183 accepted
WorkItems, one unrelated queue-slot1005 changed and was rejected, no remaining
scene traversal. The reader now accepts optional `-CaptureApi` so detached target
effects do not prevent retention of the bracketing API data.

Within2gu of effect anchor(-9788.865,636.7872,24.67056): earlier A had1 rendered
and2 upstream contributions. Both B brackets have0 rendered AND0 upstream, with
frame49019->49045, sequence30101->30108. Thus the observed disappearance is not
merely view filtering or a failed effect reader. No new on/off signal is inferred
from stale pointers; the owner action and progressing proven light feed agree.

Both authored UUIDs32A1624E…00000000/…01000000 AND the runtime bowl UUID
297CB15E561300000000000000000000 and effect UUID447CB15E… remain at the SAME
pointers as the refreshed A scene snapshot. Runtime effect source child count
changes1->0, component count13->12. The old render component0x5EC4E7A47E0 is no
longer its active child; the guarded old association read rejects **Source identity
changed**. This is not a valid old-component OFF flag: the pointer can be reused.
The persistent source UUID surviving this OFF is established; AN recovery,
per-GPU-contribution association and moving-source continuity remain unproven.

### Owner AN recovery — A–B–A complete, 22:37–22:38 local

Fresh registry `scene-identity-20260926-223737-463.json`: same PID29928,
7475 UUID matches, stable header, zero failures;857 nearby. Chain
`light-identity-chain-20260926-223744-491.json`:80 scenes,184 WorkItems,
zero failures, no remaining traversal. Both authored UUIDs and runtime bowl
297CB15E… / effect447CB15E… retain their exact A/B pointers and UUIDs.
Runtime effect source child count returns0->1 and component count12->13.

Guarded lookup `world-effect-identity-20260926-223848-233.json` finds:

- Source0x5ECAF5C0AE0, UUID447CB15E561300000000000000000000, unchanged.
- NEW component0x5EC4E7B4C90; Owner still null. SAME keyDEBA1DCD943EBDAA;
  NEW handle6010 (prior A5191). Handles are effect generations, not durable IDs.
- One exact match in first manager map: node0x5EBF6F3D2C0,
  record0x5EC29FDB770, index38/39. Translation(-9788.865,636.78723,24.670557).
  List pointer0x5ECD709A0F0, count6; element semantics still unproven.
  Both map headers stable. No position-based assignment of source identity.

API counts within2gu of the same effect anchor (both brackets agree per phase):

| Phase | Rendered | Upstream | Capture sequence | Frame within bracket |
|---|---:|---:|---|---|
| A,22:20 |1|2|21863->21866|18126->18137|
| B,22:29 |0|0|30101->30108|49019->49045|
| A2,22:38 |1|1|38707->38710|16114->16125|

A2's strong spot returns near(-9788.822,637.53,24.6702), luminance0.34–0.35.
The weak extra point from A is absent in A2; this is NOT identical contribution
count recovery or a proof that every nearby sample belongs to this bowl.
The earlier A2 chain brackets also had1 upstream (sequence37704->37710,
frame12294->12317). Each phase has a progressing feed; frame numbers are not
assumed globally monotonic between phases. Snapshots remain non-atomic CPU/GPU.

Established: this runtime source UUID survives a normal OFF/ON while its active
effect is removed/recreated with a different handle. Not established: per-light
GPU membership, the authored->runtime link, moving-torch lifetime, or persistence
through scene unload/restart. No additional user toggle is required for this result.

## Next bounded step

Follow the exact0x68 instance's linked emitter records to the GPU allocation/
ManyLights group, keeping source UUID separate from recreated effect handles. Use the
WorkItem route for sources actually registered there, not as a universal path.
Neither route yet yields a proven GPU contribution ID. Do not equate handles,
queue slots, vector indices or position matches with persistent light identity.
Moving-source continuity and unload/reuse/restart remain separate tests; no
production API/plugin change or positional tracking workaround.
