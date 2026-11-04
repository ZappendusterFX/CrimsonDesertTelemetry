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

### First installed gameplay control — 2026-11-04

The owner subsequently reports installation and restart. PID 32972 starts at
19:40:59 CET. Read-only checks find the loaded ASI and all five executable/config
companions equal to the receipt hashes. Evidence:
`artifacts/tick-install-check-20261104-195207-7112eff963174661bdaa7ac7055149be.json`.

Owner-confirmed gameplay 19:52:07-10 CET yields 47 fresh observing/running samples,
no errors, RVA 0x4585370. Counter advances 22018 -> 22320 over 3000 ms of native
publication timestamps (~100.7 function entries/sec). That is neither an FPS
measurement nor the Hue sampling/output frequency. The reader's requested 20 Hz
is a polling target, not an assertion that PowerShell delivered 60 samples.
Evidence: `artifacts/simulation-ticks-gameplay-20261104-195207-550-5ef478405e6342a695786d43e6fc143d.json`.
This is the progressing control; a separately owner-confirmed Skills-preview
recording is next. No acquisition gating, fixed menu light or menu-semantic
acceptance follows from this gameplay control alone.

### Skills preview stops the candidate tick

Owner-confirmed Skills with preview at 19:53:42-45 CET, same PID 32972 and start
time: 47 fresh observing samples all report no-tick-500ms, with counter fixed at
25475 and no errors. Publication sequence advances 24314 -> 24408 and native
observer timestamps advance 2937 ms; progress age rises 18062 -> 20997 ms.
Thus the observer continues running while this function does not advance.
Evidence: `artifacts/simulation-ticks-skills-20261104-195342-032-fb797840242046b2bccea0cf341eed8e.json`.
Owner-confirmed return to gameplay at 19:55:26-29 CET completes the comparison:
47 fresh observing/running samples, same PID/start, counter 26707 -> 27001 over
3000 ms; no errors. Evidence:
`artifacts/simulation-ticks-gameplay-20261104-195526-618-02e6f6ae1df54f2ab18907f18ea8cd1a.json`.
The candidate resumes when Skills closes. This validates the targeted pause signal;
it does not verify every menu, the stutter fix, acquisition gating or lamp output.

## Promotion to menu mode

The next production-profile private menu package adds `[Pause] Enabled=1`.
This starts observation independently of the old diagnostic switch and bypasses
new light/ambient/physics acquisition after 500 ms without tick progress. Startup
without a progressing control remains unknown. The lightweight observer continues
independently; new progress resumes capture and changes its epoch. Completed work
from an old epoch is drained and discarded. It never replays a game query.
The neutral activity API conveys pause authorization to CrimsonHue, which owns
fixed menu output. Missing/stale observations never authorize held menu output.

Mapping v2 keeps the 128-byte layout and all existing offsets: flags bit2 means
acquisition paused, bit3 pause control enabled; offset96 is uint64 captureEpoch,
offset104 uint64 resumedAtTickMs. Diagnostic readers support v1 and v2. New
monitor-only packages explicitly set Pause.Enabled=0; research keeps it disabled.

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
