# Simulation tick diagnostic

The owner confirms a one-to-one relation between in-game menus and paused
gameplay and prefers the World Builder tick approach for update resilience.
Menu UI evidence remains a cross-check, not the chosen production dependency.
This private diagnostic is preparation for a capture pause and fixed Hue light;
it does not implement either behavior yet.

Private candidate `2.2.9-tick.1` is built from source `3f810c0`, with Research OFF
and Diagnostics.SimulationTicks=1. Folder/ZIP validation, three native tests and
decoder/own-process reader tests pass. Archive:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.9-tick.1-ModManagers.zip`,
SHA256 `7A7819427F468A307918D5F4BCB7699B858DA0B4ACF8B3347EA81FC950053487`.
Receipt: `artifacts/simulation-tick-package-receipt-20261104.json`.
Not installed or game-tested; installation remains owner-only with game closed.

## Proven discovery, pending runtime semantics

World Builder locates a movement-pump function with
`48 8B C4 4C 89 48 ?? 48 89 50 ?? 55 41 56`. The same pattern has exactly one
executable-section match, at a PE unwind function start, in three preserved EXEs:
build 25477059 at RVA 0x42820E0, 26361232 at 0x457F910 and 26423880 at 0x4585370.
The first 28 bytes are identical. Thus those address shifts can be found
automatically; future instruction/semantic changes still require validation.
Evidence: `artifacts/worldbuilder-tick-20261104/FINDINGS.md`,
`pump-build-comparison.json`, `pump-pattern.json`, `pump-body-callers.json`.

World Builder's counter belongs to its hook, not a read-only game global.
Its UI declares paused after 500 ms without counter progress. CDT's diagnostic
uses a tiny entry thunk preserving registers/flags and tail-jumping to the
original trampoline. It counts entries, whereas World Builder counts returns.
No game work is called twice, queued or suppressed by this diagnostic.
Whether skill-preview playback advances this function is still unmeasured.

## Opt-in and independent observation

`Diagnostics.SimulationTicks` defaults to 0 in both package templates. The
maintained builder accepts `-SimulationTickDiagnostic` only for a private
prerelease with explicit `-Research off` and the supported current definition.
The native worker also requires exact EXE and capture-context checks, a unique
signature, exact prologue and function-start validation. A future unknown build
does not automatically gain this experimental hook from signature equality.

The pinned module owns the counter and trampoline. The hook only records an
atomic entry. An independent worker polls at a bounded cadence, without ties
to rendering/capture progression. Startup remains unknown until progress is
observed; after progress, 500 ms of inactivity is classified separately from
running. New progress resumes immediately; resets/clock rollback require new
progress. This is simulation inactivity, not proof of a particular menu.

## Diagnostic reader and handoff

`scripts/Read-SimulationTickDiagnostic.ps1` opens only the diagnostic mapping
with read access. It checks schema, process identity/start time, publication
sequence and worker freshness. Old or unavailable observations stay unknown.
It defaults to a bounded three-second, 20 Hz recording with a unique output.

After the owner reports installing the private package and restarting, first
check deployed identities without changing them, then use the owner-confirmed
state to label a recording:

```powershell
& ./scripts/Read-SimulationTickDiagnostic.ps1 -Label gameplay
& ./scripts/Read-SimulationTickDiagnostic.ps1 -Label skills
```

Each menu/game transition is a separate short owner handoff. Never wait or poll
for installation or a requested transition. Do not run a labeled capture while
the owner is freely playing and no longer holds that state.

The reference UI observations are Skills -> gameplay -> Inventory in PID 16716:
MainMenu control 27D goes 03 -> 00 -> 03, SkillTree 03 -> 00 -> 00, and Inventory
20 -> 20 -> 23. These bounded reads do not prove all-menu coverage. Exact files
and the owner-supplied labels are retained in `docs/MENU_LIGHTING_RESEARCH.md`.

Only after tick behavior is measured should it gate new GPU/physics acquisition.
Draining existing work and observing ticks must continue during a pause. Hue
then needs fresh neutral status for constant light, and fresh post-resume data
before blending back. No such status or output change is shipped by this probe.
