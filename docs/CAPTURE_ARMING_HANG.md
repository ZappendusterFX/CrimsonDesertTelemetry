# Capture arming hang at world entry — 2026-09-27, Claude → Codex

Owner request: make CDT robust against this. Nothing in CDT was changed; this is
evidence plus a proposed fix for the main developer.

## Symptom

Sporadically, exactly when entering the world, the game hangs (once it crashed
with `0x887A0006 DXGI_ERROR_DEVICE_HUNG`). The native log ends with
"Playable-world signal received; native light and sky capture armed." and nothing
after it: no "ManyLights GPU Modulator …", no "ManyLights recurring capture
ready". In good runs those lines follow immediately.

## Setup (measured)

- Game build 25477059, CrimsonDesert.exe 1.0.0.2976.
- CDT = the lightshow package build from worktree
  `C:\DEV\CrimsonDesertTelemetry-package-lightshow4` (51be0e9, CDT_RESEARCH=OFF):
  installed ASI SHA-256 `85D3A5C2CD6DF1A4BBB77A42D9F2CFCE9C742E276A79C0D9695D32A67398E6CE`,
  PE timestamp `0x6AB8D83D`, size 1,561,088 — byte-identical to
  `…\build\native-package-release\Release\CrimsonDesertTelemetry.asi`.
- ReShade 6.8.0.2155 (`bin64\dxgi.dll`) with the add-on **Crimson Weather v0.8.1**
  (`bin64\CrimsonWeather.addon64`, installed 2026-09-27 09:31; ReShade.log:
  `Registered add-on "Crimson Weather" v0.8.1.0 using ReShade API version 18`).
- FlyMode.asi; CrimsonDesertNpcSpawn.asi (separate project `C:\DEV\CrimsonDesertNpcSpawn`,
  hooks the game's message thunk only on the first spawn request).

## Runs today

| Time | CDT | NpcSpawn | Result |
| --- | --- | --- | --- |
| 15:09 | 15:06 install | active, hook installed at start | world entry, then DXGI_ERROR_DEVICE_HUNG at 15:10:57 (Pearl Abyss dump, since deleted by the game's reporter); CDT log had "ManyLights GPU Modulator initialization failed (upload buffer or shared bridge)" and "recurring capture ready" |
| 15:21 | same | active, hook installed at start | hang at world entry; read-only dump taken (below) |
| 15:32 | same | inert (Enabled=0) | loaded, but massive graphics stalls |
| 15:45 | clean reinstall (owner removed stale CDT leftovers from bin64) | none | clean |
| 15:54 | clean | active, hook deferred (never installed before F12) | clean; spawning worked |
| 16:18 | clean | active, hook deferred, F12 never pressed | **hang at world entry** — NpcSpawn touched no game code in this run |

## Evidence from the hung process (15:23)

Dump: `C:\DEV\CrimsonDesertNpcSpawn\artifacts\crash-20260927\hang-152330-pid14592.dmp`
(MiniDumpNormal + thread info). Same CDT binary as above (module timestamp and size match).

- All 101 threads have suspend count 0 (no MinHook freeze/deadlock between plugins).
- One CDT thread (start `CDT+0x5EDB4` = `render_capture.cpp:834`, the capture
  worker) is blocked:
  `CDT+0x5F92C` → ReShade `dxgi.dll+0x134689` → `D3D12.dll` → `D3D12Core.dll` →
  `nvwgf2umx.dll` → `win32u.dll` (kernel wait).
  `CDT+0x5F92C` is the return address of `call [rdx+0x10]` (IUnknown::Release)
  right after `MH_EnableHook(executeTarget)`, i.e. **`probeQueue->Release()` in
  `Prepare()`, `render_capture.cpp:518`**.
- Game threads wait in `D3D12Core.dll`.

Symbolization: rebuilt 51be0e9 in a scratch folder with the package flags plus
`/Zi` and `/DEBUG /OPT:REF /OPT:ICF`: same image size, `.text` differs in 434 of
1,049,088 bytes; source lines from that PDB were used. To reproduce: configure
`native/CrimsonDesertTelemetry.Asi` with `-DCDT_NATIVE_BUILD_ID=25477059
-DCDT_RESEARCH=OFF` and those flags (use PowerShell; Git Bash rewrites `/O2`).

## Reading

The probe-queue create/hook/release has existed since afcc1cc8 (2026-09-06) and
ran for weeks. New since today are the Crimson Weather ReShade add-on (ReShade
forwards command-queue creation and destruction to add-ons, and the blocked
Release runs inside ReShade's queue wrapper) and the modulator initialization
right after this point (51be0e9).

Hypothesis, not proven: releasing a freshly created command queue at world entry
sporadically blocks inside ReShade/add-on/driver handling. Alternative: the GPU is
already hung for another reason and the Release only waits on it (the 15:09 run
reached "recurring capture ready" and still ended in DEVICE_HUNG).

## Proposed

1. Isolation (owner, no code): remove `CrimsonWeather.addon64` and enter the world
   a few times.
2. Robustness (code): do not create or destroy a D3D12 command queue at world
   entry. Minimal: keep the probe queue alive until `StopCapture()` instead of
   releasing it in `Prepare()`. Cleaner: take `ExecuteCommandLists` from the queue
   the game already uses for its swapchain, so no extra queue exists at all.
3. Diagnosis if it persists: timestamped log lines around each `Prepare()` step
   (create resource, fence, queue, hook, release, modulator init); optionally
   DRED breadcrumbs/page-fault data in a research build.

## Side notes

- `crashpad_handler.exe` lives in bin64 and loads `winmm.dll`, so it loads every
  ASI too. Each game start shows "Another bootstrap instance owns port 27311;
  skipping duplicate host launch." from that process. Harmless, but CDT could stay
  inert unless the host is `CrimsonDesert.exe` (NpcSpawn does this since b43e59c).
- Stale CDT files in bin64 after removing the mod produced massive loading stalls
  (15:32); a clean reinstall removed them.
- Claude's mistake today: commit 8549a2b accidentally landed on
  `feature/lightshow-modulator` with Antigravity's uncommitted `docs/HANDOVER.md`;
  it was reset (`--mixed`) immediately. HEAD is 51be0e9 and those edits are
  uncommitted exactly as before. Nothing was pushed.
