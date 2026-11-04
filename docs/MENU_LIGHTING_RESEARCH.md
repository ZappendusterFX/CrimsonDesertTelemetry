# Menu lighting / capture pause — 2026-11-04

Owner reports skill-menu stalls and rapidly repeated hover sounds while the skill
preview loads, also with Hue alone. Earlier OpenRGB/Razer isolation is withdrawn.
The requested behavior is fixed room lighting in menus and a real pause of the
normal acquisition path. No automatic menu mode or stall fix is implemented yet.

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
are unchanged. This is an activation candidate, not a proven general menu flag.
Gameplay return and another menu/control are not yet measured.

Evidence: `artifacts/menu-state-gameplay-20261104-184757-378-bf992a97a14545619c5d8afd8a3d0e75.json`
and `artifacts/menu-state-skills-20261104-184916-617-81e54805303046e19fd6270411c39e6a.json`.
