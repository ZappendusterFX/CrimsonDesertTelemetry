# CrimsonDesertTelemetry — shared Codex / Claude rules

Documentation is for the assistants, not homework for the user.

Current user priority (2026-09-26): release 2.2.0 = all-around ManyLights input
plus physics source visibility, ON by default in production (owner decision).
Publish on GitHub only. No separate live confirmation of the exact release ZIP
is required (owner decision, 2026-09-29).
GitHub owner is now ZappendusterFX (formerly fabianviol). Light switching is
deferred. Read HANDOVER first. Preserve published packages and historical crash
evidence. This does not authorize unrelated fixes.

1. Keep work scoped. Implement, test proportionately, save and stop. No unrelated cleanup.
2. Reuse existing findings. On takeover check Git status and `docs/HANDOVER.md`; follow its relevant research links before new experiments.
3. Preserve both assistants' work. Commit completed changes with clear ownership. Never overwrite a versioned release or delete old research/captures.
3a. Preserve every game executable you work against: run `scripts/Backup-GameExecutable.ps1` after any install or update, before Steam replaces the file. Relocating a hardcoded address after an update can be impossible without the PREVIOUS binary to read context from, and an overwritten executable cannot be recovered.
4. Verify the actual result. Separate measured facts, hypotheses and invalid tests. A failed reader proves no absence; game tests need a progressing control.
5. Maintain one concise current checkpoint in `docs/HANDOVER.md`: result, remaining work, relevant evidence and one next step. Raw captures stay out of Git.
6. Scoped reversible console/graphics instrumentation is authorized for lighting research. Never call an instrumented run an untouched baseline. Require game shutdown for ASI replacement.
7. Use PowerShell 7 where suitable. Keep `AGENTS.md` and `CLAUDE.md` byte-identical.
8. When a game action or user input is needed, finish the current turn immediately. Do not keep a command/session open, poll, sleep, or spend tokens waiting for the owner. The owner will return with `AN`, `AUS`, `weiter`, or another message; resume only then. Keep each handoff short.
9. For rendered-light data and AN/AUS status, start with the proven, camera-paired filtered ManyLights path. Never guess a substitute from the authored-light vector or another nearby structure. After a game update, validate the exact EXE, hook/context bytes, live bridge and controlled light response before trusting that path. If it fails, report unavailable and diagnose the failure; do not restart broad light-source research by default.

## Locations

- Product / single ASI source: this repository, `native/CrimsonDesertTelemetry.Asi`.
- Research: `research/`, a preserved independent Git repository (commit its changes separately).
- Research entry points: `research/light-source-tests/CODEX_HANDOVER_FIRE.md` and `research/console-enabler/HANDOVER.md`; historical checkpoints remain, latest overrides stale claims.
- Captures: `artifacts/`; third-party checkouts: `external/`; obsolete copies: `archive/crimsonhue-workspace-20260906/`. Never develop the archived standalone copy.
- Future Philips Hue consumer: `C:\DEV\CrimsonHue`; it consumes the telemetry API and contains no telemetry implementation.
- Tooling: read [docs/TOOLING.md](docs/TOOLING.md) before reaching for a GPU capture, a native build or a repository script. pixtool export-to-cpp answers resource identity and provenance far faster than shader disassembly, and cmake/ctest are not on PATH.
- GPU capture forensics: [docs/GPU_CAPTURE_FORENSICS.md](docs/GPU_CAPTURE_FORENSICS.md) is self-contained. Read it before any claim about what a frame did. It carries the four-script chain from a PIX export to a proven shader-register-to-resource binding, the identities found so far, and a ledger separating established findings from hypotheses and from ones already withdrawn.
- Old research paths under `C:\DEV\CrimsonHue` map to this repository after the 2026-09-06 migration.
- Antivirus detections: [docs/ANTIVIRUS_FINDINGS.md](docs/ANTIVIRUS_FINDINGS.md) records what was MEASURED. Microsoft's Wacatac verdict is toolchain drift, proven by rebuilding the 2.0.0 source today, and no feature removal fixes it. Bitdefender's Barys signature is pinned to commit 6937fa9, the bounded repeated readback series. Do not cut functionality on a hunch; both detections were misattributed at first glance.
- Build 25246367: current production Ambient Occlusion passed controlled open/enclosed/open validation on 2026-09-12. Production per-light source visibility is still under implementation/validation; preserved research success is not production acceptance. Exact state and evidence are in `docs/HANDOVER.md`. Hardcoded native anchors also live in `spatial_acquire.cpp`; verify the applicable anchors after any update.
- Keep raw/EMA light records intact and research preserved. Do not claim the
  visibility classifier is reliable or complete.
