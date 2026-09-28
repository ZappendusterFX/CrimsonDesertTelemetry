# Capture arming hang at world entry — 2026-09-27, Claude → Codex

## Recurrence — 2026-09-28, Codex

The owner reported many clean runs with the private
`v2.2.1-capture-arming.1` ZIP, then a new world-entry hang on 2026-09-28.
Unlike the previous hard lockup, the owner could close this one without
restarting Windows. Installed ASI SHA-256
`A2EAE81F6618417367F296FA402FF101E2D620593257714E5F12A483E91741CD`;
game EXE SHA-256
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`
(Steam build 25477059). Native log passed every preparation stage through
`ManyLights recurring capture ready`. The local API later reported `loading`,
last capture 17:43:42, with `Native camera source is not advancing`.

Read-only MiniDumpNormal + thread info at 17:44:58, while PID 1792 was still
hung: `artifacts/crash-reports/local-20260928-hang/hang-174458-pid1792.dmp`,
355,419 bytes, SHA-256
`681C77974198EE0E5E75DE9DF6D5CB0DD930BD82CD2AE87EF1479554CE7900B9`.
All 102 threads have suspend count zero. CDT worker TID 5932 returned to the
regular 5 ms `WaitForSingleObject(stopEvent, 5)` loop (`CDT+0x5BC5D` is
the return address); it is **not** in the old `probeQueue->Release()` call.
The apparent main game thread TID 25836 waits inside a game-code 10 ms
`WaitForSingleObject` loop (return `CrimsonDesert.exe+0x3E008E1`). This dump
does not identify what its game-side wait depends on.

ReShade 6.8 registered Crimson Weather v0.8.1. Its final log line at 17:43:41
entered `CreateCommandQueue` of type 2 (compute) on TID 5932. That same thread
is back in the CDT wait loop in the later dump, so the line is a timing marker,
**not evidence the queue call remained blocked**. NVIDIA/D3D12 worker threads
are also in waits. No `nvlddmkm` or Display reset event was found in the
17:40–17:50 System log. Windows wrote AppHangB1 at 17:46:53 as the owner
closed the unresponsive application; Windows did not close it spontaneously.
No causal attribution to CDT, ReShade, Weather or the driver is established.

Immediate no-change retry: the owner restarted at 17:50 with the same private
ASI hash and no settings change. At 17:52 the game process was responsive,
the API reported `playing` and supported build 25477059, sequence 1982→1983,
and both light feeds available. The native log again reached recurring capture
ready. This establishes intermittent behavior under the same configuration;
one successful retry does not validate the mitigation.

The owner then reported that the one deliberate difference in the failed run
was hiding the HUD before loading; it was left visible in the clean retry.
The exact HUD/key needs confirmation. If it was CDT's F8 corner HUD,
`overlay_graphics.cpp` toggles only `state.visible` and corner-HUD drawing;
F10 world markers are separate, while native capture and the overlay client
continue. This may change graphics scheduling, but no causal link is proven.
The next control should alternate otherwise-identical visible/hidden HUD
world entries before changing the Crimson Weather add-on.

This recurrence passed `Prepare()`, so retaining the probe queue eliminated
the previously observed blocked `Release()` but did not eliminate the overall
sporadic world-entry hang. The public v2.2.1 ASI is bytewise different from
this private candidate, but the source diff from the private candidate's
commit to the public tag contains no native-code change; public exposure is
open. The Crimson Weather control remains a later isolation step if the HUD
comparison does not resolve the trigger; a single clean start is inconclusive.

## Follow-up — 2026-09-27, Codex

Commit `98f5bed` removes the observed last `probeQueue->Release()` from world
entry by retaining one queue for the ASI process lifetime. Preparation stage logs
were added. The private production-profile DMM package
`CrimsonDesertTelemetry-v2.2.1-capture-arming.1-ModManagers.zip` passed build,
capture tests and package validation; see `docs/HANDOVER.md` for hashes. It is
not installed or live-tested. This is a targeted mitigation of the dump's blocked
call, not proof that queue destruction caused the GPU hang.

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
