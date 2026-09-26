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
client +0x200 UUID[16]; +0xA0 worldPosition[3]
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

## Historical source-to-render link, not yet relocated

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

## Next bounded step

Relocate the **already proven Source-wrapper -> Owner/WorkItem association** for
this build, then establish the link to an actual current ManyLights contribution.
Use UUIDs for engine objects, but do not assign per-contribution IDs until the
join and reuse/lifetime are demonstrated. A moving torch and its possibly
multiple emitter children need a verified owner/contribution relationship. This
remains research; do not add guessed `sourceId` fields or motion matching to API.
