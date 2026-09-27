# Capture arming hang at world entry — 2026-09-27, Claude → Codex

Owner request: make CDT robust against this. Revised after Codex merged main
(3bcbe2c). No CDT code was changed; this is evidence, a release-impact question
and a proposed fix for the main developer.

## Symptom

Sporadically, exactly when entering the world, the game hangs (once it crashed
with `0x887A0006 DXGI_ERROR_DEVICE_HUNG`). The native log ends with
"Playable-world signal received; native light and sky capture armed." and
nothing after it: no "ManyLights GPU Modulator …", no "ManyLights recurring
capture ready". In good runs those lines follow immediately.

## Environment (measured)

- Game build 25477059, CrimsonDesert.exe 1.0.0.2976.
- CDT = the lightshow package build from worktree
  `C:\DEV\CrimsonDesertTelemetry-package-lightshow4` (51be0e9, CDT_RESEARCH=OFF):
  ASI SHA-256 `85D3A5C2CD6DF1A4BBB77A42D9F2CFCE9C742E276A79C0D9695D32A67398E6CE`,
  PE timestamp `0x6AB8D83D`, 1,561,088 bytes.
- New in this environment today: ReShade 6.8.0.2155 (`bin64\dxgi.dll`, see
  docs/RESHADE_CAPTURE_ERROR_6.md), its add-on **Crimson Weather v0.8.1**
  (`bin64\CrimsonWeather.addon64`, 09:31; ReShade.log "Registered add-on
  "Crimson Weather" v0.8.1.0 using ReShade API version 18"), the ReShade
  device-identity fix 00dabc7 and the modulator init 51be0e9.
- Also loaded: FlyMode.asi and CrimsonDesertNpcSpawn.asi (separate project
  `C:\DEV\CrimsonDesertNpcSpawn`; it hooks the game's message thunk only on its
  first spawn request).

## Runs

| Time | CDT | NpcSpawn | Result |
| --- | --- | --- | --- |
| 15:09 | 15:06 install | hook installed at start | world entry, then DXGI_ERROR_DEVICE_HUNG at 15:10:57 (dump since deleted by the game's reporter); log had "ManyLights GPU Modulator initialization failed (upload buffer or shared bridge)" and "recurring capture ready" |
| 15:21 | same | hook installed at start | hang at world entry; read-only dump (below) |
| 15:32 | same | inert (Enabled=0) | loaded, but massive graphics stalls |
| 15:45 | clean reinstall (owner removed stale CDT leftovers from bin64) | none | clean |
| 15:54 | clean | hook deferred, never installed before F12 | clean; spawning worked |
| 16:18 | clean | hook deferred, F12 never pressed | **hang at world entry**; NpcSpawn touched no game code |

## Evidence from the hung process (15:23)

Dump: `C:\DEV\CrimsonDesertNpcSpawn\artifacts\crash-20260927\hang-152330-pid14592.dmp`
(MiniDumpNormal + thread info; module timestamp and size equal the ASI above).

- All 101 threads have suspend count 0: no MinHook freeze or cross-plugin deadlock.
- The CDT capture worker (thread start `CDT+0x5EDB4` = `render_capture.cpp:834`)
  is blocked: `CDT+0x5F92C` → ReShade `dxgi.dll+0x134689` → `D3D12.dll` →
  `D3D12Core.dll` → `nvwgf2umx.dll` → `win32u.dll` (kernel wait).
  `CDT+0x5F92C` is the return address of `call [rdx+0x10]` (IUnknown::Release)
  right after `MH_EnableHook(executeTarget)`: **`probeQueue->Release()` in
  `Prepare()`, `render_capture.cpp:518`**, on the queue created a few lines
  earlier only to read `ExecuteCommandLists` from its vtable.
- Game threads wait in `D3D12Core.dll`.

Symbolization: 51be0e9 rebuilt in a scratch folder with the package flags plus
`/Zi` and `/DEBUG /OPT:REF /OPT:ICF` (configure in PowerShell; Git Bash rewrites
`/O2`): same image size, `.text` differs in 434 of 1,049,088 bytes; source lines
come from that PDB.

## Reading

- The probe-queue create/hook/release exists since afcc1cc8 (2026-09-06) and is
  also in the published **v2.2.0** (tag 31c8836); the modulator is not.
- Under ReShade this path is only exercised since today. The error-6 run at 09:33
  (PID 28924, before 00dabc7) already reached "recurring capture ready", so the
  Release through ReShade's queue wrapper can complete; the hang is sporadic.
- ReShade with an add-on forwards command-queue creation and destruction to
  add-ons, and the blocked Release runs inside ReShade's queue wrapper.

Hypothesis, not proven: destroying a freshly created command queue at world
entry sporadically blocks inside ReShade/add-on/driver handling. Alternative: the
GPU is already hung for another reason and the Release only waits on it (the
15:09 run passed Prepare and still ended in DEVICE_HUNG).

## Release impact (open)

If the cause is the probe queue under ReShade with add-ons, published v2.2.0
users with ReShade are exposed as well: 2.2.0 runs the same `Prepare()`, and
with ReShade it would additionally stop later with capture error 6 (fixed only in
00dabc7, unpublished).

## Proposed

1. Controls (owner, no code): (a) current build without `CrimsonWeather.addon64`,
   several world entries; (b) the published v2.2.0 ZIP with ReShade and the
   add-on. (a) clean and (b) hanging would point at the probe queue under ReShade
   add-ons rather than the modulator.
2. Robustness (code): do not create or destroy a D3D12 command queue at world
   entry. Minimal: keep the probe queue alive until `StopCapture()` instead of
   releasing it in `Prepare()`. Cleaner: take `ExecuteCommandLists` from the queue
   the game already uses for its swapchain, so no extra queue exists at all.
3. Diagnosis if it persists: timestamped log lines around each `Prepare()` step
   (resource, fence, queue, hook, release, modulator); optionally DRED
   breadcrumbs/page-fault data in a research build.

## Side notes

- `crashpad_handler.exe` lives in bin64 and loads `winmm.dll`, so it loads every
  ASI. Each game start logs "Another bootstrap instance owns port 27311; skipping
  duplicate host launch." from that process. Harmless; CDT could stay inert unless
  the host is `CrimsonDesert.exe` (NpcSpawn does this since its commit b43e59c).
- Stale CDT files in bin64 after removing the mod produced massive loading stalls
  (15:32); a clean reinstall removed them.
- Claude's slip earlier today: commit 8549a2b accidentally landed on
  `feature/lightshow-modulator` with Antigravity's then-uncommitted
  `docs/HANDOVER.md`; it was reset (`--mixed`) at once and never pushed.
  Antigravity later committed that file itself (e9a9280).
