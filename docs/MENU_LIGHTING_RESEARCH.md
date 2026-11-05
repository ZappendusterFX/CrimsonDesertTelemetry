# Menu lighting / capture pause — 2026-11-04

## Build 26448710 — UI observer revalidated 2026-11-05

The owner reports that many cutscenes can stop simulation ticks and wants to
continue investigating the UI signal while keeping the tick path. The UI reader
now selects the exact EXE profile by SHA-256 at capture time. Both 26423880 and
26448710 pass offline identity checks for 13 code spans and six vtables, and
both pass the synthetic bounded reader tests.

| Anchor | 26423880 | 26448710 |
|---|---:|---:|
| UI application global RVA | 0x71BDF48 | 0x71BDF48 |
| Subscription callback RVA | 0x1022660 | 0x1022660 |
| MainMenu vtable RVA | 0x5A012F0 | 0x5A012F0 |
| Shared indexed getter body RVA | 0xA188F90 | 0xA13BC90 |
| Shared event handler body RVA | 0xA3CD5A0 | 0xA370FF0 |
| Handler sequence following entry +0xA0 → holder +0x28 | 0xA3CD715 | 0xA371169 |

The new executable's MainMenu RTTI resolves to the same vtable and shared slot
6 and 146 thunks. Following those thunks locates the relocated implementations.
The relevant getter and control access bytes match, including the accesses to
control +0x27C/+0x27D and the backlink at +0x120. Both registration and removal
still reference the same subscription callback.

One unlabeled live read on 26448710 (PID 15612) passed the code and vtable
guards. It found a stable registry with 144 subscriptions, six readable
control chains, zero rejected roots, and MainMenu flags 01/00 with a matching
backlink. Activity reported advancing `running` around the read. The capture
establishes live addressability. The tick-based runtime behavior stays in place.

Owner-confirmed Skills at 20:42 CET produced three stable samples in the same
process. All six root, owner, indexed entry and control addresses matched the
earlier read. MainMenu and SkillTree changed from 01/00 to 01/03. The four other
controls stayed 01/00. Fresh activity samples bracketing the read advanced from
sequence 79790 to 79829 and both reported `paused`, `simulation-idle`,
`acquisitionPaused=true` at epoch 74.

Owner-confirmed gameplay return at 20:48 CET yielded three stable samples in
the same process, with 144 subscriptions and no rejected roots. MainMenu and
SkillTree returned from 01/03 to 01/00. The four other controls stayed 01/00
and all six root, owner, entry and control identities matched Skills. Activity
advanced from sequence 87014 to 87064 and both samples reported `running`,
`simulation-progressing`, `acquisitionPaused=false` at epoch 75.

Evidence is in `artifacts/menu-relocation-26448710/` and
`artifacts/menu-state-unknown-20261105-203951-426-a87d18281c90474ea201b1ccb3d7f322.json`.
The Skills capture is `artifacts/menu-state-skills-20261105-204200-600-7b0808bff25c474b85f680373409307a.json`.
The return capture is `artifacts/menu-state-gameplay-20261105-204801-906-e09a6447f5054dfca15f25d527875934.json`.

### Hernand Kuku Shop before crafting

At 20:50 CET the owner confirmed the Kuku Shop menu was open in the world
scene, before selecting the item that opens Kuku Pot crafting. Three stable
samples on the same process found the same 144 subscriptions and six control
identities as the gameplay return. All six flags remained 01/00. Activity
advanced from sequence 89858 to 89901 and reported `paused`,
`simulation-idle` and `acquisitionPaused=true` at epoch 76 before and after.
The paused shop state therefore shares the measured UI flags with gameplay.

Raw read and activity comparison:
`artifacts/menu-relocation-26448710/menu-state-kuku-shop-20261105-205023-376-2b14d2bfc6874f838f5a50d4f52876fd{,-activity}.json`.
Compare the Kuku Pot crafting screen separately. A cutscene with stalled
simulation ticks remains a separate comparison.

## Earlier investigation on build 26423880

Owner reports skill-menu stalls and rapidly repeated hover sounds while the skill
preview loads, also with Hue alone. Earlier OpenRGB/Razer isolation is withdrawn.
The requested behavior is fixed room lighting in menus and a real pause of the
normal acquisition path. The gameplay/Skills/gameplay tick comparison now passes;
menu-mode implementation is tracked in HANDOVER and SIMULATION_TICK_DIAGNOSTIC.md.
Stutter improvement and physical menu output still need owner acceptance.

## Existing state is insufficient

`Program.cs` reports playing after valid player/camera reads. API.md explicitly
defines this as availability, not game UI state. All 1,788 envelopes in the owner's
30-second skills capture remained playing. Cursor visibility, Escape events,
unchanging camera or missing frames do not establish every game-menu transition.

## World Builder leads, checked against the current EXE

Inspected official source at commit
[6ad72dcb0d82b80a8eb420c5f48d5f9bdcfe8a7b](https://github.com/Moon-yungg/crimson-desert-world-builder/tree/6ad72dcb0d82b80a8eb420c5f48d5f9bdcfe8a7b).
Its [RTTI inventory](https://github.com/Moon-yungg/crimson-desert-world-builder/blob/6ad72dcb0d82b80a8eb420c5f48d5f9bdcfe8a7b/notes/rtti_static.txt#L14269)
provides real game UI class names. They are research anchors, not a working
opened-menu reader. Source was read as data; no World Builder binary installed.

Offline relocation against the preserved build26423880 EXE, version1.0.0.3527,
SHA256 DC4237F21D6CE1E49F8E5D6FA89BF803FBBE6549123078EFDC393265C7BF6841:

| Game class | Current vtable RVA |
|---|---|
| UIGamePlayControlRootGamePause | 0x59F4458 |
| UIGamePlayControlRootSkillList | 0x5A6D5D0 |
| UIGamePlayControlRootSkillTree | 0x5A6D068 |
| UIGamePlayControlRootInventoryEquipment | 0x59F9138 |
| UIGamePlayControlRootWorldMap | 0x5A053E8 |
| UIGamePlayControl_Root_MainMenu | 0x5A012F0 |
| UIPhaseData_MainMenu | 0x591DF40 |

Each has one matching TypeDescriptor, valid self-referencing CompleteObjectLocator
and hierarchy, and a vtable with executable function pointers. This proves class
presence only. Live instances, active/visible fields and field semantics remain
unresolved. Do not hardcode these RVAs as a production menu-state contract.

World Builder's `g_menuOpen` is its editor window, not the game's menu. Its
`GameClockNow()` rejects server ticks older than two seconds: that can also mean
a stall and is not verified Skills detection. See
[editor flag](https://github.com/Moon-yungg/crimson-desert-world-builder/blob/6ad72dcb0d82b80a8eb420c5f48d5f9bdcfe8a7b/asi/cdmodkit/core.h#L50)
and [clock heuristic](https://github.com/Moon-yungg/crimson-desert-world-builder/blob/6ad72dcb0d82b80a8eb420c5f48d5f9bdcfe8a7b/asi/cdmodkit/cdmodkit.cpp#L5425).

Repeatable artifact script and exclusive JSON report:
`artifacts/worldbuilder-menu-review-20261104-181905/inspect_menu_rtti.py` and
`menu-rtti-current-build.json`. No live game process was opened by that check.
Next research step: resolve a Skills/Pause instance and its actual active state,
then validate menu entry/exit independently of preview playback and stalled frames.

## Integration boundaries once the signal is validated

### World Builder follow-up coverage

On owner request, a repository-wide targeted search covered all 120 tracked text
files (124 tracked files including four PNGs); relevant implementations and callers
were read. Remote main still resolves to the pinned 6ad72dcb commit. No ready-made
game-menu visibility detector was found. This is not a line-by-line audit of every
dependency, historical commit or issue.

`overlay.cpp:1137-1142` assigns g_menuOpen from editor::IsOpen()/Placing().
`editor.cpp:5621-5626` labels the game paused after 500 ms without PumpTicks changes.
GameClockNow uses 2000 ms tick freshness; ServerFieldTicking uses 3000 ms for the
mod's load/play-mode lifecycle. Standalone Freecam only checks camera availability.
None distinguishes actual Skills visibility from other unavailable/stalled states.
Menu-class identifiers occur in the RTTI inventory, not a runtime menu reader.

Evidence and exact search patterns/hit logs:
`artifacts/worldbuilder-menu-coverage-20261104-1930/FINDINGS.md` and `coverage.json`.
All tracked files were present; no git submodules. External ImGui/MinHook build
dependencies were not downloaded or audited. No foreign source was executed.

### Producer and consumer changes

CDT must own and publish a fresh neutral menu/capture-pause status. Hue consumes
it for constant output. A constant Hue value alone does not pause native work.

- Gate `render_capture.cpp` Record before Resolve and new GPU copies (ManyLights
  and sky Ambient), while continuing PollCapture for in-flight completion.
- Gate `spatial_acquire.cpp` Dispatch before new GI/Ambient/SDF readback work;
  do not destroy resources or skip completion of existing transactions.
- Gate physics batch arming in Poll and claiming in RayHook; disarm pending work
  under its mutex. Preserve every original game query exactly once. Stopping the
  managed requester alone cannot cancel already-armed native queries.
- Resume with new capture and visibility data, discarding pre-menu results.
  Define freshness/disconnect handling explicitly so a stale menu bit cannot
  indefinitely authorize lamp output or keep acquisition disabled.

The startup CaptureReady event is one-shot and must not be repurposed as pause.
The hidden Hue-panel input review found no demonstrated synthetic-click cause;
hover sounds alone are not proof of injected button events. Runtime acceptance
of any eventual implementation remains separate from these source/offline checks.

## Intended behavior and focused validation

Owner asks how to proceed on2026-11-04. Work starts with Skills and Pause and
extends only to menu classes whose active-state semantics have been checked.
The presence of a UI object, or a generic enabled bit used by ordinary HUD
elements, is insufficient. This is not a promise that every menu is covered.

1. Prepare a bounded read-only observer for the relevant UI instance/state, with
   the current EXE and live type identity checked. Reuse existing game roots if
   possible instead of repeatedly scanning the heap or adding a live hook.
2. Compare gameplay, menu entry, several skill previews and return to gameplay.
   The signal must stay menu-active while preview effects run, then clear on
   actual exit. Repeat a distinct menu before treating common-base behavior as
   general. A stale/unreadable pointer must produce unknown, never menu/gameplay
   by guess. Ask for owner transitions only when this observer is ready.
3. Once proven, implement the producer's native acquisition gates plus a fresh
   neutral status. Continue only the lightweight state/liveness observation and
   completion of in-flight GPU work while acquisition is paused.
4. In Hue, use a configurable fixed menu color/brightness, respect existing
   output limits, preserve stream ownership and keep Demo output-free. Retain
   the connection instead of repeated stale black/restore/reconnect transitions.
5. On verified exit, invalidate old light/visibility history, acquire fresh
   gameplay data, then blend back to normal mapping. Bound the wait for fresh
   data and retain explicit handling of disconnect, game exit and ownership loss.

First acceptance is narrow: correct menu entry/exit, no normal acquisition while
paused, no reuse of preview/pre-menu samples after exit, and whether the owner's
stutter/hover-sound symptom actually improves. Correct constant lamp output alone
does not prove that the game's stall is fixed. No new packages or production
menu-state contract should be based solely on a guessed object field.

## Prepared read-only observer

`scripts/Read-MenuStateCandidates.ps1` uses the offline-derived CommonRoot event
subscription path: globalRVA0x71BDF48 -> pointer+0xB0 -> event pointer+0xA38,
32-byte entries at event+0x40, count/capacity at+0x48/+0x4C. Registration callback
RVA0x1022660 and exact vtables filter six real menu-root types. This is a candidate
live lookup, not yet validated against the running game. Root fields through
0xC1 are sufficient for the first comparison, so each root read is only0xC2bytes.

Offline registration evidence: `artifacts/menu-instance-20261104-1833/`.
Field evidence: `artifacts/menu-state-fields-20261104-183416/FINDINGS.md` and
`focused-field-evidence.json`. B9 is initialized toTRUE and changes child tags;
stateA8 and bytesBA/BC remain opaque until a controlled menu comparison.

```powershell
# Already passed; opens no process:
& ./scripts/Read-MenuStateCandidates.ps1 -ValidateOnly
& ./tests/scripts/Test-MenuStateCandidates.ps1
# Only after the owner confirms the corresponding game state:
& ./scripts/Read-MenuStateCandidates.ps1 -Label gameplay
& ./scripts/Read-MenuStateCandidates.ps1 -Label skills
# On another owner-confirmed return, use gameplay again (fresh output each time).
```

Default3samples100ms apart; each has maximum1024handler entries/64matching roots,
1MiB byte budget and1s read-issuance budget (checked between reads, not an OS read
timeout). Exact EXE hash, four live code guards and six first-vtable-method checks
precede instance reads. Registry chain/header/entries are checked again afterwards.
Changed/unreadable/absent candidates cannot establish menu-open or menu-closed.
Fresh exclusive JSON keeps partial failures and the owner-supplied label; it never
claims automatic detection or an atomic view of mutable game memory.

Offline guard verification and synthetic fixtures passed, including foreign-type
filtering, field widths, registry changes, oversized-vector refusal before array
read, and null chains. Logs are `synthetic-check.log` and `offline-validation.log`
in the instance artifact folder. No live process opened during preparation.

## First labeled live comparison — 2026-11-04

Same PID16716/start17:30:10UTC. Owner-labeled gameplay18:47:58CET and
Skills18:49:17CET each yield three stable registry samples,144subscriptions,
six matching roots, no rejected roots,11660bytes/sample. Exact guards pass.
Only emitted-field change: SkillTree root+0xBA goes0 ->1 and stays1 in all three
Skills samples. All six A4/A8 remain593, AC/B0/B4 remain0; other roots and identities
are unchanged. Gameplay return18:51:08CET keeps SkillTree+BA=1 in all three samples:
the BA open/closed hypothesis is REJECTED. This may record another lifecycle event;
its actual meaning is unresolved. Do not infer menu state from this field.

Evidence: `artifacts/menu-state-gameplay-20261104-184757-378-bf992a97a14545619c5d8afd8a3d0e75.json`
and `artifacts/menu-state-skills-20261104-184916-617-81e54805303046e19fd6270411c39e6a.json`.
Return: `artifacts/menu-state-gameplay-20261104-185107-324-a9a1637548714070a2377a9ccd7589fd.json`.

## Observer v2: reversible field and UI-entry prerequisites

Offline shared slot6 getter0xEE43C0 jumps to0xA188F90: validate root+8 owner,
UInt16 owner+8A !=FFFF, then owner+58 ->+B0 ->+B8 vector(data+0,count+8), requiring
index<count and entry!=null; only then return root+130. Slot146 atFFBCE0 jumps
toA3CD5A0 and sets130=1 on event0E, clears130=0 on0F. Field128 is also set/cleared.
These are proven accesses, not verified named SHOW/HIDE events or rendered visibility.
Owner itself has no established plain vtable; never invent that identity check.
Evidence in `artifacts/menu-owner-fields-20261104-185229/` and
`artifacts/menu-owner-20261104-1856/FINDINGS.md`.

Observer v2 extends each root read to0x132 bytes; records opaque128..131, the
getter's indexed-entry prerequisites, and a separate optional owner+28 ->+10
control byte4E from callbackDEF900. Exact eight code guards and six vtable anchors
are checked before reads. Optional null/unreadable/changed chains stay explicit;
no game function is called and no automatic menu verdict is produced.
Synthetic field/control/race tests and sentinel/out-of-range/null-entry checks
pass, as does -ValidateOnly against the preserved EXE. Final logs:
`artifacts/menu-state-v2-indexed-synthetic-20261104.log` and
`artifacts/menu-state-v2-indexed-offline-20261104.log`.

Latest v2 gameplay baseline18:58:47CET in PID16716 is three stable samples, no
rejected roots,13028bytes/sample. All six indexed entries exist and remain stable
in a153entry vector. SkillTree/Pause/Inventory/SkillList130=0, WorldMap130=0/131=1,
but MainMenu130=1 already in gameplay. Therefore a blanket OR over these roots is
invalid even with the getter's entry prerequisite. All field128 values are zero;
all secondary DEF900 controls are absent. Scope the next comparison to actual
Skills entry/exit; general menu coverage and hierarchy remain unresolved.

Baseline: `artifacts/menu-state-gameplay-20261104-185846-409-1f87c270dcbf4ce790435177aac5d5da.json`.
An earlier extended baseline without indexed fields is retained at
`artifacts/menu-state-gameplay-20261104-185556-481-b736fd93080a41f2b6a4c69787edaab1.json`.

The owner-confirmed Skills capture at 19:03:20 CET has the same values for every
emitted v2 field in all three stable samples (same PID). SkillTree 130 stays zero.
This candidate does not distinguish Skills from gameplay; the owner agrees a
redundant close comparison is unnecessary. Evidence:
`artifacts/menu-state-skills-20261104-190320-160-4078ef02eff24049a7bc7918b3e03dc4.json`.

## Observer v3: indexed UI control flags

The event handler at RVA 0xA3CD715 follows indexedEntry+A0 -> holder+28 to bytes
control+27C/27D. Code at 0xA3CD743 also accesses a backlink at control+120. V3
records these raw values with separate absent/unreadable/changed statuses and
pointer rechecks. It does not invoke the handler or infer visible/closed state.
Two added exact guards bring the total to ten. The optional A0 block is not a
script root or a virtual object; do not invent a vtable validation for it.
Offline evidence: `artifacts/menu-control-flags-20261104-190607/FINDINGS.md` and
`artifacts/menu-owner-20261104-1856/INSTANCE_FOLLOWUP.md`.

Synthetic tests cover missing holder/control and changed chains; exact-EXE
validation also passes. Logs: `artifacts/menu-state-v3-synthetic-20261104.log`
and `artifacts/menu-state-v3-offline-20261104.log`. Read-only peer review found
no concrete chain, width or guard discrepancy.

Skills capture 19:07:51 CET (last owner-confirmed state), same PID, three stable
samples, no rejected roots, 13328 bytes/sample: 27C/27D = SkillTree 0103,
MainMenu 0103, Pause 0140, SkillList 0180, Inventory 0120, WorldMap 0100.
Every backlink equals its indexed entry. Root 130 remains unchanged.
Evidence: `artifacts/menu-state-skills-20261104-190750-303-bbcf916a91664aa1ba7b9272ed4a9e10.json`.
These control fields were absent from earlier gameplay captures; a new owner
gameplay handoff is required before comparing them. No semantic verdict yet.

### Gameplay return and pause requirement

Owner-confirmed gameplay at 19:17:12 CET produces three stable samples in the
same PID 16716, with unchanged root/entry/control identities and no rejected
roots. SkillTree and MainMenu control byte 27D both change 03 -> 00 compared
with Skills 19:07:51; byte 27C stays 01. The four other controls stay unchanged.
Evidence: `artifacts/menu-state-gameplay-20261104-191711-945-2548b59bdfb745c5bcefc5627904d613.json`.
This establishes an observed Skills-to-gameplay correlation for those bits.
It does not yet establish general pause semantics, the precise meaning of each
bit, or that MainMenu is the parent of SkillTree. The next owner handoff is
Inventory to test whether MainMenu also tracks a distinct menu.

The owner explicitly confirms every in-game menu pauses the game. A reliable
pause signal therefore suffices for the desired fixed light and acquisition
bypass; identifying individual menus is not required. World Builder's paused
indicator is a tick-age heuristic, not an explicit engine pause field. Its
specific tick has not been compared during this test, and normal host polling
must not be mistaken for that simulation tick. No need to reject pause as a
product criterion; the remaining task is validating a reliable observation.

### Inventory and the selected tick approach

Owner-confirmed Inventory at 19:21:56 CET, same PID 16716, gives three stable
samples with no rejected roots: MainMenu 27D=03, InventoryEquipment=23,
SkillTree=00. Thus MainMenu follows Skills -> gameplay -> Inventory as
03 -> 00 -> 03; Inventory retains its higher bit while low bits change 20 -> 23.
Evidence: `artifacts/menu-state-inventory-20261104-192156-026-c32803a39a2a4a06a988771778936eac.json`
and independent `artifacts/menu-shared-tabs-comparison-20261104-1922.txt`.
This covers the observed tabs; it does not establish a universal pause flag.

The owner explicitly states pause and menus are one-to-one and prefers World
Builder's tick approach because of update resilience. That is the selected path;
stop requesting further UI transitions merely to enumerate every menu. Owner
is now freely playing; no further labeled live recordings without a new handoff.

Further offline checking found the same World Builder pump signature uniquely
at function starts in three preserved builds (25477059, 26361232, 26423880),
with identical first 28 bytes at different RVAs. Its counter is mod-owned and
requires a hook, not a ready-to-read game-global. CDT now has an opt-in, exact-
build diagnostic entry counter and independent 500 ms inactivity policy. The
production menu-light/capture-pause integration remains pending runtime evidence.
See `docs/SIMULATION_TICK_DIAGNOSTIC.md` and
`artifacts/worldbuilder-tick-20261104/FINDINGS.md` for implementation and limits.
