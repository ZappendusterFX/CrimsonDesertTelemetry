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
