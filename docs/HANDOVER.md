# Current checkpoint — 2026-09-29, private sky-visibility fix ZIP; 2.2.3 world-entry GPU hang

**Fix (commit `cc10a5e`):** `spatial_acquire.cpp` records no ambient copy until
`OnSubmission` has been called once (proves the render ExecuteHook). A copy
unobserved for 5 s is abandoned: its buffer is retained unreleased, a new one
is used with the same fence; at 8 abandonments per process the path stops and
publishes Unavailable. New WARP test `spatial-acquire-latch` (70 checks);
mutations removing the gate or the timeout each fail it. Production CTest
38/38, managed 79/79, package validators PASS.

Private production-profile ZIP (`-Research off`, INI byte-identical to 2.2.3's):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.4-skyvisibility.1-ModManagers.zip`,
921,469 bytes, SHA-256 `5BFED5ACC7FD141B23362DA6F246ADC99AE5F8A0C69B52042385D138BCCC5816`;
ASI `4113097C56F4B9A65A580A7A4E79ED5A9E37E279F01C50C75122F8476201927E`.
Expanded: `artifacts/mod-manager/v2.2.4-skyvisibility.1-20260929-200842-802-e5dafc0d/`.
Not installed or live-tested. Only the owner activates it in DMM; never write
to `bin64`. `Read-SkyVisibilityState.py` has a profile for this ASI; expected menu
class `gated-until-submission-hook`, in-world `acquisition-cycling`.

**Crash, published 2.2.3 (PID 22304, 20:01):** the last CDT action was the
world-entry compute probe queue creation (ReShade 20:01:16, CDT worker TID
27760). nvlddmkm 153 followed at 20:01:23; `CreateCommandQueue` returned
E_OUTOFMEMORY (native log 20:01:27), and the game crashed with
DXGI_ERROR_DEVICE_HUNG at 20:01:28. Correlation, not proven cause. Unrelated
to the sky fix, which does not touch this. Evidence: `artifacts/crash-reports/local-20260929-2001/`,
details in `docs/CAPTURE_ARMING_HANG.md`.

**Live, owner-installed via DMM, PID 39076 (all six runtime files = ZIP):**
main menu `gated-until-submission-hook`: no copy, no buffer, fence/frame 0,
observer not yet called, render `Discover` (2.2.3 had a frame-1 copy here).
In-world `acquisition-cycling`: observed 1, fence 707→765 and frame
7342→7414 in 1.8 s, `Valid`, visibility frame = `latestFrame`, 0 abandoned.
`/v1/ambient`: visibility 0.333 age 16 ms, local ambient RGB available, not
stale. Native log reached recurring capture ready; no crash this start. The
latch precondition is measured absent; one start does not bound the rarer
timeout path. Evidence: `sky-visibility-state-39076-{menu,world}-*.json`.

Codex reviewed both (see CAPTURE_ARMING_HANG.md). No objection to the sky fix.
It notes pre-existing gaps: no timeout after an observed submission, unchecked
`queue->Signal()`, `GetCompletedValue()` UINT64_MAX treated as complete, and no
test for late execution of an abandoned list.

**Next (owner go pending):** one private package with (a) those sky-path gaps
closed, and (b) the hook address taken from the game's swapchain queue when the
overlay has one. It keeps the device-identity check and falls back to today's
probe queue otherwise, with a WARP test that a DIRECT-derived hook observes a
compute copy and fence.

# Previous checkpoint — 2026-09-29, sky-visibility acquisition latch identified in code

2.2.3 is published on GitHub; no release work remains. Diagnosis only; no
product source, INI or package changed. Stage of the failed run is still NOT
measured.

Code path (`spatial_acquire.cpp`, production): `Start` enables the GI dispatch
hook at process start. `Dispatch` records one copy whenever `Resolve` passes and
nothing is in flight, then sets `activeList` and `acquisitionInFlight`.
`OnSubmission` is called ONLY from render_capture's `ExecuteHook`, which is
installed in `Prepare` after the playable-world gate. `Poll` returns while
`activeList` is set. There is no timeout or re-arm. A copy recorded on a list
that is not submitted again after that hook exists therefore stops the path
permanently, and `PublishVisibility` is never called: state/frame/tick stay 0,
exactly the failed signature. Unavailable publication also zeroes frame/tick,
so the bridge alone cannot separate the two. Measured precedent (2026-09-23,
same hook and `Resolve`): SDF acquisition caught loading frames 7/8 before
the gate. SDF was then gated on `CaptureReady`; the ambient copy never was.
Leading hypothesis, not proven for the failed run.

New read-only reader `scripts/Read-SkyVisibilityState.py` (exact 2.2.3 ASI
hash, in-memory code guard; RVAs from `dumpbin /disasm`, no PDB exists) reads the
spatial statics, render hook state, game hook bytes and sky visibility block,
and classifies the stage. Control in the working PID 34740: fence 30943→31002
and `latestFrame` 45109→45189 in 2 s, visibility frame equal to `latestFrame`,
GI constants decode `Ok`/clipmap 1. Evidence:
`artifacts/light-research/sky-visibility-state-34740-20260929-194407.json`.

Measured, PID 34740 → restart PID 29116 (same ASI hash), main menu:
`recorded-before-submission-hook`. Copy recorded at `latestFrame` 1, GI world
(0,0,0), fence 0, render phase `Discover`, ExecuteHook not installed, stable
over 2 s. `activeList` 0x20C250500 has its vtable in `dxgi.dll` = ReShade 6.8,
so it is a wrapper list. Evidence:
`artifacts/light-research/sky-visibility-state-29116-menu-20260929-195346.json`.
The latch precondition occurs on a normal start; whether it clears is open.

In-world, same PID, owner confirmed it works: `acquisition-cycling`, fence
791→846 and frame 10213→10287 in 1.8 s, `Valid`. The render hook patched
ReShade's queue (`dxgi.dll+0x1347E0`), so it sees wrapper lists, consistent with
`activeList`. A 5 s read-only sampling found the GI copy alternating between
exactly two lists, 0x20C250500 and 0x20FE53F40. The frame-1 list is one of the
two in-world lists, so its first observed submission cleared the latch.
Evidence: `sky-visibility-state-29116-world-20260929-195621.json` and
`sky-visibility-active-lists-29116-*.json` in `artifacts/light-research/`.
Minor, from code (not observed): that first publish carries frame 1 / GI world
(0,0,0) with a current tick.

Conclusion: the precondition occurs on normal starts; recovery depends on the
frame-1 list being reused after the hook exists. The failed run's stage is
still not measured (no reader ran in it).

**Next (owner decision):** fix = record no ambient copy until the render
submission hook is installed, plus a bounded submission timeout. That needs a
new version; 2.2.3 stays unchanged. On any recurrence first run
`Read-SkyVisibilityState.py` (expect `recorded-before-submission-hook`).
Only then fix: gate the ambient copy on `CaptureReady` and/or abandon an
unobserved list after a timeout. Never derive local RGB from global sky.

# Previous checkpoint — 2026-09-29, camera sky visibility recovered on a later start

After the 2.2.3 release, the owner disabled CrimsonDesertNpcSpawn in DMM and
camera sky visibility/local ambient RGB became available. The owner then enabled
NpcSpawn again; both fields still worked. The only other reported change was
CrimsonWeather's `HotkeyToggleEffect` from F10 to F4. This A/B/A observation does
not establish which change, restart or deployment timing caused recovery.

Read-only evidence from the current game process (PID 34740, started 19:25:29):
health is `playing` with sequence 9703; `/v1/ambient` is available, sky capture
sequence 319, visibility value 0.478868 at frame 10560, and
`localEnvironmentAmbientEstimateWorking.available=true` with fresh RGB. The
deployed Telemetry ASI still hashes to the exact 2.2.3 release
`D53DD63956251E444C92F6F99FA8C65A373F7E7015E4285BA5A30971CA590179`.
NpcSpawn's log shows its swapchain hooks chained in front of Telemetry's and
both running in this successful process. CrimsonWeather also initialized.
Its INI was last written at 19:26:43, after this process started; no measured
sample establishes when its hotkey change took effect.

The prior failing process had fresh global sky but sky-bridge visibility
state/frame/tick all zero. There is no evidence yet for a permanent NpcSpawn
conflict or a direct F10 cause. Investigate the intermittent native spatial
acquisition/startup path and capture the first failed guard or readback stage on
a recurrence. Do not infer local RGB from global sky alone. GitHub release 2.2.3
is published with the exact validated ZIP; do not rebuild or replace it for
this diagnosis.

# Previous checkpoint — 2026-09-29, 2.2.3 live test passed with optional sky gap

At the owner's request, the 2.2.2 functional changes and native HUD refactor
are packaged as the final-version **2.2.3 production ZIP**:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.3-ModManagers.zip`
(921,294 bytes), SHA-256
`8088CCABF2F89345A392EC21356336766A13BE002FBEDDE90E1C1D9185CFE99D`.
The ASI SHA-256 is `D53DD63956251E444C92F6F99FA8C65A373F7E7015E4285BA5A30971CA590179`,
matching the scanner-clean private candidate. Exact installed game EXE hash
matches supported Steam build 25477059. Package validation, managed Release
build and regression suite, HTTP/WebSocket smoke and all 37/37 native tests
pass. Defender custom scans of temporary copies of the final ASI and ZIP
passed with unchanged hashes and no new detection events. Details:
`docs/releases/v2.2.3-validation.md` and release notes `v2.2.3.md`.

The owner enabled this exact ZIP in DMM and reported that the game runs. All
six deployed runtime files in DMM and `bin64` match the ZIP. A read-only live
check showed `playing`, supported build, advancing health sequence 6465→12972,
global sky capture sequence 216→432 and recurring native ManyLights capture.
The owner also reported missing Camera Sky Visibility/local RGB. The ambient
API has fresh global sky but `visibility: null`; the sky bridge's visibility
state/frame/tick are all zero after several minutes. This optional path was
also absent in a 2.1.14 session on the same patch; the 2.2.3 code did not
change its spatial acquisition. The exact native reason remains unknown.
Release notes and validation now state this limitation explicitly. The owner
has confirmed the exact ZIP's DMM/game startup, satisfying the live release
gate. Next: GitHub-only publication of the exact immutable ZIP; no Nexus upload.

# Previous checkpoint — 2026-09-29, Defender blocked 2.2.2 ASI

The owner could not enable 2.2.2 in DMM (Windows error 225); the game also
stopped starting with that ASI. Defender Operational events 1116/1117 at
18:17–18:35 report `Trojan:Win32/Wacatac.C!ml` and quarantine of
`CrimsonDesertTelemetry.asi` in both game `bin64` and the DMM library. The
2.2.2 ZIP remains intact (SHA-256
`FFB74EB94FB4B84A3C3019CBFE5D3BC71BFCDA4A8198ADA9CD882E383F30D442`);
its ASI is SHA-256 `152C7B72E2D23322D009F6630A8F6BFA50D91756EB929DEB8B4BAAECA7A23A7E`.
The 2.2.1 and 2.2.2 ASIs have identical length, imported DLLs/functions and
nearly identical PE section sizes/entropy. No new suspicious import explains
this verdict. Prior Wacatac toolchain findings are relevant but do not prove
this new verdict false. At inspection, all six runtime files in DMM and `bin64`
matched the working 2.2.1 ZIP; no game process was running.

Private production-profile candidate `2.2.3-avfix.1` preserves the 2.2.2
functional changes. The native HUD filtered-light path now checks freshness on
the original visibility record rather than copying its strings for every
marker; `CurrentSourceVisibility` uses the same freshness helper. All seven
overlay/graphics tests and the maintained DMM package validator pass. A local
Defender custom scan of a temporary ASI copy under its real filename passed
without detection, and its hash was unchanged after scanning. ASI SHA-256
`D53DD63956251E444C92F6F99FA8C65A373F7E7015E4285BA5A30971CA590179`.
ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.3-avfix.1-ModManagers.zip`,
SHA-256 `F997C95B7A0DE6351D51760C8E8FD691B51FB61DA202F8B98E497ECB9015BE9D`.
This is a local scanner result, not a guarantee against later definitions. The
candidate is not installed, game-tested or published. Only the owner deploys it
via DMM. Next: owner tries this exact ZIP with the game closed, then checks
whether DMM enables it and whether the game reaches a progressing telemetry
feed; preserve Defender events if it recurs.

# Previous checkpoint — 2026-09-28, world-entry hang recurred

Release-package update, 2026-09-28: at the owner's request, production-profile
`2.2.2` was built from commit `6f058a9` for exact EXE hash
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`.
ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.2-ModManagers.zip`,
SHA-256 `FFB74EB94FB4B84A3C3019CBFE5D3BC71BFCDA4A8198ADA9CD882E383F30D442`.
The maintained package builder and validator passed; no additional tests were
requested or run for this exact ZIP. It is not installed, game-tested or
published. Only the owner deploys it through DMM. The sporadic world-entry hang
below remains unresolved; a ZIP build does not establish live compatibility.

After many clean runs reported by the owner, the game hung again on world entry
with the private capture-arming ZIP installed (ASI SHA-256
`A2EAE81F6618417367F296FA402FF101E2D620593257714E5F12A483E91741CD`).
The owner could close this hang normally, unlike the earlier harder lockup.
The native log reached **queue hook ready, modulator initialized, recurring
capture ready**. The API then reported `loading` with no advancing native
camera. A read-only dump of hung PID 1792 at 17:44:58 is preserved in
`artifacts/crash-reports/local-20260928-hang/`; its 102 threads had no
suspend count. CDT worker TID 5932 was in its regular 5 ms wait, **not** the
old probe-queue `Release()`. The game thread was waiting inside the engine.
ReShade's final log line entered a compute queue creation on TID 5932, but the
dump shows that thread had returned to the CDT loop; it does not prove the
queue call blocked. Windows logged AppHangB1 when the owner closed the game;
no NVIDIA reset event was found in that time window. Full evidence and limits:
`docs/CAPTURE_ARMING_HANG.md`.

The installed private candidate differs bytewise from public v2.2.1, but the
source diff to the public tag contains no native-code change. Treat public
impact as open. The queue-retention mitigation removed the previously observed
blocking call but did not resolve this sporadic hang. The owner immediately
restarted **without changing any settings** at 17:50; the same ASI hash was
loaded, and the game was responsive and `playing` at 17:52 with sequence
1982→1983 and rendered lights available. This confirms the failure is
intermittent in the same setup. The owner clarified that they pressed **F8,
F9 and F10 before loading** in the failed run, but not in the clean retry.
With the installed INI starting both visual layers visible and details on,
these keys hide the corner HUD, switch its details off, and hide world light
markers. Native capture and the overlay client continue. When both visual
layers are hidden and no notification is shown, the overlay skips its GPU
draw; this is a testable timing clue, not a proven cause. **Next:** keep CDT,
ReShade and Weather unchanged and compare otherwise-identical world entries
with the default overlay state versus F8/F9/F10 pressed once before loading.
Record each outcome and preserve a fresh dump/log on recurrence; defer the
Crimson Weather control until this clue is tested.

Parallel light-ID result, 2026-09-28: a bounded read-only live comparison in
PID 37460 found three same-UUID moving sources with guarded prefab label
`cd_t0000_torch_0001.prefab`. They moved 9.4–46.5 gu; positive-luminance
ManyLights samples followed each within 0.32–0.56 gu at both endpoints. This is
strong spatial evidence, not a pointer-level source-to-GPU join or a durable
public ID. No plugin/game writes. The registry-root snapshots missed this
dynamic route. Occlusion transition for an exact UUID and cross-restart
persistence remain open. Details and evidence: `docs/LIGHT_IDENTITY_RESEARCH.md`.
The owner reports that moving torches briefly reappear before occlusion hides
them. Code review explains a likely cause: the physics cache matches only
within 0.12 gu, while the HUD leaves a newly `unknown` light visible and hides
it only once `blocked` arrives. The old marker disappears because the moving
light no longer supplies data at its previous position, not from an occlusion
decision there. No wall-crossing test is needed to pursue this
flicker. The owner chose a stricter fail-closed display/consumer rule: with
`HideOccluded=1`, HUD/radar show only fresh `clear` results; blocked, unknown,
missing and stale records are hidden. The original raw and smoothed API feeds
stay complete and keep honest `unknown` metadata. A new independent
`/v1/lights/visible` HTTP/WebSocket feed includes only `clear` contributions
before grouping/smoothing, for consumers that need a visibility-filtered RGB
signal. Same-capture clear-to-unknown transitions are applied immediately, with
no hidden contribution left in EMA RGB. The 250 ms nearby-blocker presentation
hint was removed. Managed suite, HTTP/WebSocket smoke, native overlay and
graphics smoke tests pass. The exact installed EXE SHA-256 matched the
25477059 profile. Private production-profile DMM ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.1-visibility-failclosed.1-ModManagers.zip`,
SHA-256 `01B09E527856DD09F62D487220F0F25472C1C53EC7254FD7BCF2C7FB85058B5A`.
The package validator passed. It has **not** been installed or game-tested;
only the owner installs via DMM. Do not use sample index as a source ID or reuse
an old ray as a new physics verdict. Next: owner installs the private ZIP via
DMM, enables `HideOccluded` with F11 if its INI starts at 0, and observes a
moving torch behind the hut wall.

# Previous checkpoint — 2026-09-27, 2.2.1 published on GitHub

[GitHub release v2.2.1](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/releases/tag/v2.2.1)
is public, non-prerelease and latest. The owner authorized GitHub-only publication.
Tag points to `08d755931854c844f20d57b8eeb944f5df79109f`. Release ID397798275,
asset ID593707002, 920030 bytes. GitHub's public asset digest matches the exact
local ZIP SHA-256 `BD83103552B69CE10847313EF39E22FC9676FF03C4CCE3AD7135CD5549D7DF12`.
The tag's release workflow succeeded; the Nexus publication workflow was skipped.
Managed Release suite, 37/37 production native tests and package validation passed.
The sporadic capture-start hang is not proven resolved. Preserve the ZIP and prior
hang evidence. No release work remains; a recurrence needs that run's native log
and dump.

# Previous checkpoint — 2026-09-27, first live capture-arming run

The owner reinstalled the private `v2.2.1-capture-arming.1` ZIP and entered the
game without a hang. At 17:24 all six runtime files in `bin64` matched the ZIP.
The native log reached queue-hook ready, GPU Modulator initialized and recurring
capture ready. A passive API check at 17:36 reported supported Steam build
25477059, `playing`, sequence 37727, and available rendered lights; the earlier
read had sequence 36826, so publication was progressing. The owner was still
in-game. This is one successful start of a sporadic failure, not resolution.

**Next:** owner repeats several world entries in the same ReShade/Crimson
Weather setup. If a hang recurs, preserve that run's native log and dump.

# Previous checkpoint — 2026-09-27, DMM 3.1.1 removal failure

The owner installed then deleted the private capture-arming ZIP in DMM. No live
game test of this candidate was reported. DMM removed the ASI and INI from its
library and game `bin64`, but left both Telemetry DLLs and both CFG companions
in both locations; all four match the ZIP. DMM has `.dmm_added` ownership
markers for all four, and its configuration still lists the plugin as active.
This reproduces the previous incomplete-removal issue on DMM 3.1.1. The
official DMM changelog says this package layout and companion types are
supported; the exact faulty cleanup path remains unknown. Full evidence is
in `docs/MOD_MANAGER_VALIDATION.md`. At this checkpoint the game was closed.
The owner subsequently reinstalled the ZIP, verified all six runtime files,
and posted the DMM bug report on Discord; this issue is paused.

# Previous checkpoint — 2026-09-27, Codex: bounded queue retention candidate

The 15:23 hung-process dump identified CDT's capture worker blocked in the last
`probeQueue->Release()` through ReShade and the NVIDIA driver. Commit `98f5bed`
keeps this single probe queue for the ASI's process lifetime instead of destroying
it at world entry. Preparation now logs its resource, queue-hook and modulator
stages. This removes the observed blocking call; it does not prove whether that
call caused the GPU hang or encountered an already hung driver. No installed game
files were changed.

Production `CDT_RESEARCH=OFF` build passed. Seven capture D3D12 tests and two
modulator/bridge tests passed; the seven capture tests also passed in the exact
package build. The package builder verified the expanded payload and ZIP. The
installed game EXE still matches exact build 25477059 / SHA-256
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`.

Private DMM ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.1-capture-arming.1-ModManagers.zip`.
ZIP SHA-256 `E1FBA32008D8E9E29462346DAA618B35AA78340914E348D016AC6D32D76F4004`;
ASI SHA-256 `A2EAE81F6618417367F296FA402FF101E2D620593257714E5F12A483E91741CD`.
It has not been installed or tested in-game. Preserve the previous ZIPs and dump.

**Next:** with the game closed, the owner installs this exact private ZIP through
DMM and repeats several world entries with the same ReShade/Crimson Weather setup.
If it hangs, preserve the new native log and dump: the last preparation-stage line
will distinguish queue creation/hooking from modulator init. Do not call one clean
start a fix for a sporadic hang.

# Previous checkpoint — 2026-09-27, Claude → Codex: capture arming hang at world entry

Sporadic hang (once DXGI_ERROR_DEVICE_HUNG) right after "Playable-world signal
received; native light and sky capture armed." with the lightshow package build
(51be0e9). A read-only dump of the hung game shows the capture worker blocked in
`probeQueue->Release()` (`render_capture.cpp:518`, inside `Prepare()`) through
ReShade's D3D12 wrapper into the NVIDIA driver; no thread suspended. New today in
that environment: ReShade 6.8, its Crimson Weather add-on, 00dabc7 and the
modulator init. The same probe queue is in published v2.2.0, so 2.2.0 users with
ReShade may be exposed: open. Owner asks to make CDT robust. Evidence, controls
(without the add-on; v2.2.0 with it) and proposed fix (no queue create/destroy at
world entry): docs/CAPTURE_ARMING_HANG.md. CDT code unchanged. Next: Codex runs
or requests the controls and implements the fix.

# Previous checkpoint — 2026-09-27, Antigravity: ZappFX Migration to C:\DEV\ZappFX

As requested by the owner, the standalone ZappFX Audio & MIDI Lightshow Engine (including Web DJ Console, DAW tracks, Visual Timeline Sync Aligner, in-game hotkeys, and optional Stage Director / NPC Spawner) has been migrated cleanly to its own dedicated repository at `C:\DEV\ZappFX` with its own `ZappFX.csproj` and Git repository. All companion files (`tools/CrimsonDesertAudioLightshow`, `Start-Lightshow.*`, `thunderstruck_test.mid`) have been removed from CDT so CDT remains clean.

# Previous checkpoint — 2026-09-27, Antigravity: Generic MIDI Instrument Channels, Dedicated Thunder Toms, DAW Track Rows & Full-Screen UI

1. **Generic MIDI Channel Engine**:
   - Implemented standard General MIDI (GM 128) program patch resolver. Any loaded MIDI file now automatically discovers all active channels and names them by instrument with appropriate icons (e.g. 🎸 Overdriven Guitar, 🎸 Electric Bass, 🎹 Piano, 🎻 Strings, 🎺 Brass, 🗣️ Vocals).
   - Dynamic per-channel color palette cycling, staggered target light group assignment (`all`, `foreground`, `midground`, `background`, `even`, `odd`), and intelligent pitch dance mode detection (`stereo_chromatic` for guitars/leads/solos, `dispersion` for others).
2. **Dedicated Thunder Toms & 5-Way Drum Split**:
   - Analyzed `Thunderstruck.mid` note distribution: the iconic double hits during "THUNDER!" ("Thun-" = Toms [43, 45, 47], "-der!" = Toms + Kick [35, 43, 45, 47]) were previously lumped with 1,172 continuous hi-hat ticks.
   - Refactored drum channel into 5 dedicated functional drum types:
     - 💥 **Kick Drum / Bass Drop** (Notes 35, 36) — Red (#EF4444)
     - ⚡ **Thunder Toms / Accent Hits** (Notes 41, 43, 45, 47, 48, 50) — Blazing Orange (#F97316), Peak 20x, stereo chromatic dance
     - 🥁 **Snare / Rimshot** (Notes 38, 40) — Pure White (#FFFFFF)
     - 🕒 **Rhythm Hi-Hats** (Notes 42, 44, 46) — Cyan (#00FFFF)
     - ✨ **Crash Cymbals & Percussion** (All remaining drum notes) — Gold (#FACC15)
3. **DAW Channel Strip Row Layout ("untereinander in Zeilen")**:
   - Replaced bulky vertical tile cards with sleek, DAW-style horizontal track rows stacked vertically.
   - Proportional grid columns: Track/Channel & Mute -> Activity Meter -> Color & Peak Brightness -> Instrument Patch -> Target Group -> Pitch Dance Mode.
4. **Full-Screen Widescreen Layout**:
   - Removed fixed 1240px container max-width (`width: 100%; max-width: 100%`).
   - Dynamic canvas resizing on both the Studio Visual Timeline Aligner and RTA Spectrum to ensure razor-sharp graphics across any resolution (1080p, 1440p, 4K, ultrawide).
5. **Packaging & Verification**:
   - Rebuilt Release binary, published, and updated `artifacts/ZappFX-Lightshow-Companion-v2.2.1.zip`.
   - Companion process running live on `http://localhost:8888`. Verified with `Thunderstruck.mid`: all 9 channels active and responding.

**Next:** Owner refreshes `http://localhost:8888` and verifies the full-screen row view and Thunder Toms response in game.

# Previous checkpoint — 2026-09-27, Antigravity: ZappFX Sync Offset Direction Fix & Playback Note Scheduling

Fixed critical SyncOffsetMs direction inversion and note scheduling in ZappFX companion:
1. **Sync Offset Polarity Fix**:
   - In `MidiEngine.cs` and `Program.cs`, `effectiveCurr` was previously calculated as `CurrentTimeSec + offset`.
   - On the visual canvas, dragging to the left produces a negative offset (e.g. -1280 ms), shifting the note block earlier so it hits sooner.
   - However, in the playback loop, adding a negative offset delayed the note by +1280 ms, creating an inverse $2 \times \Delta t$ discrepancy between canvas visuals and audio/light playback.
   - Fixed by subtracting the offset: `effectiveCurr = CurrentTimeSec - (SyncOffsetMs / 1000.0)`. Now when notes align with audio peaks on the canvas, they fire at that exact millisecond during playback.
2. **Reliable Note Scheduling (`_lastProcessedTimeSec`)**:
   - Replaced fragile frame-to-frame delta with persistent `_lastProcessedTimeSec` cursor.
   - Prevents buffer progress jitter from dropping or double-firing notes on audio device startup or seek.
   - Reset cleanly on `Stop()` and `Seek()`.
3. **Packaging & Verification**:
   - Compiled Release binary, updated `artifacts/ZappFX-Lightshow-Companion-v2.2.1.zip`.
   - Running live on `http://localhost:8888`. Verified with live mode and offset endpoints.

**Next:** Owner tests playback in browser and in-game with aligned MIDI offset.

# Previous checkpoint — 2026-09-27, Codex: local lightshow merge

`feature/lightshow-modulator` merges Antigravity's native ManyLights RGB modulator
into local `main`. GPU upload slices remain owned until their exact queue fences
complete; the shared-memory bridge publishes whole snapshots and disengages after
350 ms without a valid heartbeat. The Release ASI, seven focused D3D12/render
tests and bridge tests pass offline. This source was not installed or tested in-game.
The audio/MIDI companion is a separate mod that consumes CDT; its local project
under `tools/CrimsonDesertAudioLightshow` is not part of this CDT commit. Preserve
its files and existing packages for the owner and Antigravity to move separately.
Next: require owner confirmation of an exact ZIP before any GitHub publication.

# Previous checkpoint — 2026-09-27, Antigravity: ZappFX Studio Visual Timeline Sync Aligner & Horizontal Navigation

Implemented, verified, and running live in ZappFX companion web dashboard (`http://localhost:8888`):
1. **Studio Visual Timeline Sync Aligner (English UI, No 3rd-Party Name)**:
   - Completely purged 3rd-party naming ("Audacity"); updated to clean DAW/studio terminology.
   - All tooltips, badges, buttons, and track labels translated to English.
   - **Fixed MIDI Note Rendering**: Resolved data format mismatch (notes sent as compact arrays `[t, ch, n, d, v]`). Now renders colored note blocks with full chromatic heatmap palette ($B_4$ violet, $D\sharp_5$ cyan, $E_5$ lime, $F\sharp_5$ gold, $G_5$ orange, $B_5$ white, drums in bottom lane) and pitch labels when zoomed in.
2. **Horizontal Navigation & Scrollbar**:
   - Added interactive horizontal timeline scrollbar slider directly beneath the canvas with real-time time interval badge (`0:00 - 0:22`).
   - Added instant one-click `[⏮ Back to 0:00]` buttons in both the Quick Views toolbar and next to the scrollbar.
   - **Dual-Track Dragging**:
     - Hovering over upper half (Audio waveform): shows `ew-resize` cursor; left-dragging pans the timeline view horizontally through the song.
     - Hovering over lower half (MIDI notes): shows `grab`/`grabbing` cursor; left-dragging slides the MIDI sync offset with real-time millisecond badge update.
   - Smooth mousewheel zoom clamped within valid song bounds `[0 .. totalDuration]` preventing jumpiness or getting stuck.
3. **Packaging & Live State**:
   - Rebuilt Release binary and updated `artifacts/ZappFX-Lightshow-Companion-v2.2.1.zip`.
   - Companion process running live at `http://localhost:8888` connected to game PID 28924.

**Next:** Owner refreshes `http://localhost:8888`, checks the rendered MIDI notes, navigates with the new horizontal scrollbar and `[⏮ Back to 0:00]` button, and tests sync alignment.

# Previous checkpoint — 2026-09-26, Codex: same bowl after process restart

Owner AN; new PID796 (same EXE SHA). Player again at the same bowl. Fresh registry
has7557/7557 UUID matches and stable header; API upstream has the active spot near
(-9788.826,637.53,24.669) with progressing captures. Authored bowl/effect UUIDs
32A1624E…00000000/…01000000 remain the same across the restart. Runtime bowl/effect
UUIDs changed to185D70725613… / 5A5D70725613…. This limits the prior result:
the runtime source UUID survived one OFF/ON in one process, not a restart.

The new effect component0x22156A29120 resolves by the same opaque key
DEBA1DCD943EBDAA and new handle11206 to one0x68 manager instance,
index36/39. Its list has six IDENTICAL 12-byte entries; the middle DWORD is36,
the instance vector index. A bounded read of all39 instances found each first
entry's middle DWORD equal to its own vector index. These are not evidence for
six distinct lights or a known GPU emitter link. Details and paths in
LIGHT_IDENTITY_RESEARCH.md. The virtual+28 manager method only queues a request
through140635FE0; no GPU join established from that method.

**Next:** identify the consumer/meaning of the repeated 12-byte entries and
an actual emitter allocation or another source-to-GPU link. Do not key the public
API by the effect handle or runtime UUID across restarts. No user toggle needed.
Manager virtual+0x10/+0x18 registration was inspected; its separate0x88 vector
at manager+0xF41F8 was empty in PID796, so that is not a live bowl join.
Moving-source candidate: player's equip lantern prefab UUID
85F7456E561300000000000000000000 stayed at the same source pointer while
moving47.12gu with player between registry snapshots230907 and232746. Later
233131 registry +233140 chain seeded `lantern` explicitly; its root has0 children
and0 WorkItems. No lit emitter or GPU light link is proven. The latter registries
had changing headers, so use per-row UUID checks, not atomic-count claims.
**Owner input needed:** visibly turn on/equip the portable lantern, then report AN
while standing still for fresh active-effect capture; one movement test follows.
No plugin changes; preserve concurrent lightshow/modulator work.

# Previous checkpoint — 2026-09-26, Codex: bowl A–B–A confirms source UUID continuity

Owner AN completes the bowl comparison in PID29928. Same runtime source UUID
447CB15E561300000000000000000000 at0x5ECAF5C0AE0 in ALL three phases; authored
UUIDs and runtime bowl297CB15E… also persist at the same pointers. Effect child
count1->0->1, components13->12->13. New effect component0x5EC4E7B4C90 has SAME
keyDEBA1DCD943EBDAA but NEW handle6010 (A5191). Exact manager lookup finds record
0x5EC29FDB770, index38/39, translation(-9788.865,636.78723,24.670557), list count6
(element semantics still unknown). UUID, not effect handle, survives this toggle.

Near-bowl API rendered/upstream: A1/2, B0/0, A2=1/1; progressing brackets in each
phase. Do not claim identical contribution counts: A's weak extra point is absent
in A2. Evidence: scene-identity-20260926-223737-463.json (7475 matches, stable),
light-identity-chain-20260926-223744-491.json (184 WorkItems, zero failures),
world-effect-identity-20260926-223848-233.json (frames16114->16125).
See LIGHT_IDENTITY_RESEARCH.md for the complete phase comparison and caveats.

**Next:** trace this exact0x68 instance's linked emitter list toward GPU allocation
and ManyLights membership. Source UUID -> GPU contribution join is NOT solved;
neither moving-torch continuity nor unload/restart persistence is validated.
No new user toggle needed. Read-only research, no production/plugin changes,
no background wait. Concurrent modulator work remains untouched.

# Previous checkpoint — 2026-09-26, Codex: bowl UUID -> keyed world-effect instance

ID research remains MAIN; no positional tracking or production changes. PID29928
at bowl near(-9788.865,636.7872,24.67056), player(-9790.68,635.81,23.38).
Queue->UUID reproduced, but this bowl uses a SECOND route with Owner+250 null:
component+288 effect key + component+270 handle -> renderer+106248 manager maps
-> exact0x68 instance. Live twice: keyDEBA1DCD943EBDAA, handle5191, index35/39,
source UUID447CB15E561300000000000000000000. No positional ID assignment.
Code and exact guarded read path: `docs/LIGHT_IDENTITY_RESEARCH.md`, new bowl section.

Reader `scripts/Read-WorldEffectIdentity.ps1`; positive evidence
`artifacts/light-research/world-effect-identity-20260926-222051-427.json` includes
progressing bracketing API frames18126->18137. NOT an atomic GPU allocation join.
Fresh chain: `light-identity-chain-20260926-221617-114.json`.
Component0x5EC4E7A47E0. Prior component detached while owner moved; old negative
map search is INVALID absence evidence. Always recheck source UUID/handle.
Authored UUID32A1624E430E74563C6A6EAD00000000 (bowl), ...01000000 (effect)
persisted while pointers/runtime UUID changed. Authored->runtime link unproven.

**Next:** owner AUS/AN on this bowl: fresh registry+chain and API phase snapshots,
inspect key/handle/runtime-vs-authored identity transitions; do not treat a reader
rejection as OFF. Then follow the instance's opaque linked list (count6, type not
decoded) toward GPU allocation. No GPU per-light ID or moving-torch fix yet.
No capture/wait running. Concurrent lightshow/modulator changes remain untouched;
private accepted visibility fix and published2.2.0 are not altered or retested.

# Previous checkpoint — 2026-09-26, Codex: renderer WorkItems -> UUIDs verified

Real light-source identity remains the owner's MAIN investigation; no positional
tracking workaround. The private mass-flash fix was visually accepted; do not
retest it. Moving-light flicker is separate and not yet fixed.

Read `docs/LIGHT_IDENTITY_RESEARCH.md`. Exact-build read-only PID26452 snapshot
resolved184 renderer WorkItems through83 owners to83 SceneObjectClient UUIDs.
Queue slot, owner array membership and source links validated; zero read failures.
Several WorkItems share a source UUID. NOT184 lights or83 physical lamps: these
sources are absent from the earlier nearby registry subset and may be local
effect instances. The join to concrete GPU ManyLights contributions is still open.
Forward121 effect components had null Owner+250; current code confirms that offset,
so do not repeat a guessed offset search. Owner work array/count DID move -8.

Evidence: `artifacts/light-research/light-identity-chain-20260926-214049-545.json`
and the setter/queue live dumps listed in the research note. Reusable reader:
`scripts/Read-LightIdentityChain.ps1`. No game writes or production changes by Codex.
After the owner's app restart no CrimsonDesert process was present. Old PID26452
pointers are historical evidence only; take fresh snapshots when game is running.
Other concurrent uncommitted modulator/lightshow work is NOT ours; preserve it.
Managed outputs may still contain the abandoned tracking draft: rebuild before
packaging, never overwrite the accepted/private or published ZIPs.

**One next step:** capture one active light's emitter/particle allocation together
with the current queue/ManyLights state to prove WorkItem -> GPU contribution;
then test the real UUID on a moving source. No polling/wait left running.

# Previous checkpoint — 2026-09-26, Codex: engine UUIDs confirmed; ID research is MAIN

Owner installed private `2.2.1-visibility-fix.1` and visually CONFIRMED the
mass-flash fix works. Do not request another recording/retest of that pattern.
Separate individual moving-light flicker remains; owner suspects NPC torches.
Owner rejected positional tracking: finding real source IDs is now the priority.

Read `docs/LIGHT_IDENTITY_RESEARCH.md` next. Current-build read-only PID26452
registry traversal resolved8,830 SceneObjectClient entries, ALL with registry UUID
equal to client+0x200. Within100gu of current player:5,116 distinct object UUIDs,
200 lamp/torch/candle/brazier/fire-named prefab candidates (NOT200 active lights).
Native lookup RVA1851010 is confirmed; it returns ClientSyncSceneObjectData,
whose+58 points to client+28. Do not blindly use server UUID offset+1D8 on clients.
No moving-object lifetime or current ManyLights-to-UUID join proven yet.

Production source unchanged. Unshipped motion-tracking draft was removed and
preserved ONLY as `artifacts/light-research/visibility-position-tracking-unshipped-20260926.patch`.
Do not restore it. Managed build outputs can still contain that abandoned draft:
rebuild from source before any new packaging; immutable fix ZIP is unaffected.
Research reader: `scripts/Read-SceneObjectIdentity.ps1`, exact-build/bounded/read-only.
Latest evidence: `artifacts/light-research/scene-identity-20260926-211223-185.json`.

**One next step:** relocate the old proven Source-wrapper -> Owner/WorkItem join
on this build and connect an actual current ManyLights contribution to its UUID.
Same-position prefab duplicates are distinct scene instances, not safe to merge.
No game writes, plugin replacement, publication or background wait in this turn.

# Previous checkpoint — 2026-09-26, Codex: measured context mismatch; private FIX ZIP READY

PID456 synchronized 20s capture completed: 1170 API frames, 302 native rounds.
All 10 coherent false-clear rounds used query DWORD+0x14 `0x40004027`;
291 normal rounds used `0x40000016`, one normal round `0x40004024`.
World/inner/caller were identical. This is measured context-family correlation,
not a complete decoding of collision-mask semantics. Details and evidence:
`docs/PHYSICS_QUERY_RESEARCH.md`, section "2.2.0 intermittent mass flashes".

Private fix accepts ONLY the previously validated `0x40000016` family for
continuous visibility. Foreign calls still execute untouched and leave the pending
measurement available for the next matching call. Defensive replay guard included.
No filter-bit edits, new rays, timing/expiry changes, motion gates or HUD concealment.
Manual research captures and bounded native trace remain available.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.1-visibility-fix.1-ModManagers.zip`
  SHA256 `93FEF6D4565D8FA89C6C6464135FFDC945B9BB3A0E404B479A96442B31414E14`.
- Expanded: `artifacts/mod-manager/v2.2.1-visibility-fix.1-20260926-202832-039-eef51a8d/CrimsonDesertTelemetry/`.
  ASI SHA256 `46E83140FF63EC5995FFD1B74BFA4FD2EC6FFE759803AD29164A503D49DC1B92`.
- 177 synthetic native checks pass; focused CTest physics observer + release-fire
  pass; production build and package self-test/expanded/ZIP validators pass.
  `-Research off`, normal production INI unchanged. NOT live-tested or published.
  Published 2.2.0 ZIP hash rechecked unchanged. Installed game was not modified.
- SEPARATE owner observation: individual moving lights repeatedly flicker; owner
  is very sure they move and suspects NPC torches. Not established as the same
  cause. Managed position-only association (0.12gu) can lose a moving source's
  previous result, but that is a code-path hypothesis, NOT measured attribution.
  Do not claim this package fixes that second pattern or assign persistent identity
  from sample indices. Raw/smoothed light data and unknown handling remain intact.

**One next step:** owner closes game, installs the WHOLE fix ZIP via DMM (no INI
edits), returns to the moving test. On `go`, immediate START feedback then the
already-built recorder: `& .\artifacts\tools\visibility-recorder\VisibilityRecorder.exe 20 --trace`.
Check mass false-clear rounds AND unknown/expiry separately; narrower selection
must not merely trade false-clear for unavailable results. Track moving-source
flicker separately. END feedback after capture; no background waiting for owner.

# Previous checkpoint — 2026-09-26, Codex: native context diagnostic ZIP READY

Owner clarification: the intermittent flashes occur EVERYWHERE, not only in the
hut. The hut was merely the recorded test location. Treat this as a general
moving-visibility problem; next capture can be wherever it reproduces. Do not
require returning indoors or assume a hut-specific geometry/collision defect.

Owner authorized continuing after the captured false-clear batches. No guessed
mask/filter fix. New private package preserves the2.2.0 production visibility
algorithm, query count, budgets and defaults (`CDT_RESEARCH=OFF`). Added an idle
named event that records ALREADY COPIED native context for20s on explicit request.
No hook-path change/additional pointer reads/rays. JSON serialization and file I/O
run in Poll after normal result publication. Bounds:500rows/32MiB,3sessions/process.
Query prefix0xA0, collector0x140 before/after, world/inner/caller/thread, camera,
per-target result and measurementSequence allow correlation with the API recording.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.1-visibility-trace.1-ModManagers.zip`
  SHA256 `663CBC1F9BDEEBDD170719A6F5257F5E25D11AA1823143A11A2E3CD349CC727A`.
- Expanded: `artifacts/mod-manager/v2.2.1-visibility-trace.1-20260926-201245-487-a64efcd5/CrimsonDesertTelemetry/`.
- Synthetic:168 native checks passed, including passive serialization, explicit
  triggering, no extension on repeated triggers, duration/row/byte/session bounds.
  Build and package self-test/expanded/ZIP validators pass. No live test of this
  instrument yet. Recorder prebuilt with `--trace` support; no new dependency.
- Existing archived stationary contexts DO differ outside geometry (including
  query+0x14 and collector+0x18), but that does NOT identify a bad flag/context.
  Only a moving failed-vs-normal batch comparison can establish the correlation.

**One next step:** owner closes game and installs the whole private ZIP via DMM.
No INI edits needed; F11 may be used to hide blocked markers. Do NOT replace the
ASI while game runs. Then on owner `go`, immediate START feedback and execute:
`& .\artifacts\tools\visibility-recorder\VisibilityRecorder.exe 20 --trace`
No preflight/build at `go`. END feedback after20s. Retrieve the native JSONL from
bin64 (preserve it) and join it with the new gzip API capture by measurementSequence.
No running capture, game modification or background wait left open. Published2.2.0
and its immutable ZIP remain unchanged. This test ZIP is NOT a fix or public release.

# Previous checkpoint — 2026-09-26, Codex: MOVING visibility flash captured

20s recording COMPLETE after owner's go: 1155 frames, 17:58:09.166–17:58:29.159 UTC,
PID30924. Camera moved (axis spans X7.879/Y1.672/Z5.593gu and large direction change).
Evidence: `artifacts/light-research/visibility-motion-20260926-195809-4258bc2e2b944244908fb0b5df137861/`
(`visibility.jsonl.gz`, `summary.json`). Owner reports roughly30 flashes while walking
longer than the capture; do not equate that count with the recorded20s.

**The captured mass flashes are TRUE `clear` outputs, NOT mass `unknown`/expiry.**
- Example sequence145975 ->145976 (3.615s): clear count2 ->149 of248 input lights.
  All149 share measurementSequence37646 and clearSampleCount9/9. Of those,147 were
  blocked in the preceding snapshot at the same sampleIndex and within0.02gu.
  Examples return to blocked after152–469ms with reference-camera changes0.49–0.54gu.
- Five large rounds (>80 simultaneous clear) =37592:144,37628:141,37646:149,
  37671:96,37680:119. ALL accepted targets in each of these rounds report9/9 clear.
  Larger-count episodes last about0.18–0.27s; smaller flashes may also exist.
- No null visibility; raw capture age never above94ms. Separate minor unknowns
  exist (budget/waiting/expiry), but do not explain these coherent false-clear rounds.
- Native `RunVisibilityBatch` shares ONE borrowed natural query context for all
  targets. `PrepareRayCopy` preserves nongeometry fields. Natural-ray replay control
  matching does not establish that this context's filters/world are suitable for
  scene visibility. Variable context/filter state is the LEADING HYPOTHESIS, not
  yet a measured root cause; the recorder does not contain query/collector bytes.

One next step: compare native query/collector/world context of an all-clear round
with a neighboring normal round, using bounded diagnostic metadata if necessary.
Do not "fix" this by HUD unknown handling, retaining old values, motion gates or
slower expiry. No plugin/INI/package changes in this diagnostic turn;2.2.0 untouched.

# Previous checkpoint — 2026-09-26, Codex: intermittent visibility flash reported

Owner reports 2.2.0 works in a hut (one visible light), but movement often briefly
reveals all hidden lights. Read-only diagnosis; no code, INI, package or game changes.

- Confirmed code path: HUD hides/dims only `blocked`; `unknown` reappears at normal
  alpha (`overlay_model.cpp:625`, `overlay_hud.cpp:190,552`). Thus loss of verdicts
  can look like measured visibility without the API actually reporting `clear`.
- Possible synchronized loss: managed `PhysicsVisibilityClient.Apply` clears its
  cache above 250 ms rendered capture age (HUD light freshness allows 500 ms);
  native missing natural query after 350 ms returns code2 for the whole batch,
  which becomes `physics-query-unavailable`; measurements otherwise expire at
  500 ms. These are inspected paths, NOT a confirmed cause of this occurrence.
- Live PID30924 raw evidence: `artifacts/light-research/visibility-flash-30924-20260926-live1/`.
  581 frames, 17:35:53.787–17:36:03.793 UTC; 239–240 upstream sources, max238 blocked,
  max4 clear, max11 unknown (waiting-for-physics/stale-physics), no null visibility.
  Camera X/Z unchanged, Y span0.004gu: effectively STATIONARY. No all-light flash
  captured. Planned15s recorder hit its existing128MiB raw limit after10s; complete
  JSONL frames are usable, `after.json` was not produced. Do not call it a movement test.

- Follow-up on owner's `go`: `artifacts/light-research/visibility-motion-20260926-194008/`
  contains547 raw frames, 17:40:09.454–17:40:18.884 UTC (9.43s;128MiB limit again).
  251–255 sources; max4 clear, max17 unknown, no null visibility or scene-wide flash.
  Unknown reasons include outside-physics-budget, waiting-for-physics, isolated
  physics-query-unavailable and stale-physics. Player and camera remained essentially
  stationary throughout (camera Y span0.0012gu, X0, Z0.000011; forward unchanged).
  Initial compact Python recorder failed before connecting because `websocket` is
  not installed; existing PowerShell recorder was used immediately instead.
  This again does NOT validate movement; ask whether the owner saw a flash during
  the recording before attributing a cause. No plugin/INI changes.

**READY for owner's next `go`:** no capture/process left running. The owner says
previous recordings started too late and explicitly requests immediate START
feedback and a 20s recording. First send `Aufnahme startet jetzt – bitte 20 Sekunden
bewegen`, then run ONLY this prebuilt command (cwd product repository), no build,
dependency checks or HTTP preflight:

`& .\artifacts\tools\visibility-recorder\VisibilityRecorder.exe 20`

Source: `tools/VisibilityRecorder/`. Compiled now, offline replay checked all 547
preserved frames: camera/player/capture metadata and both streams' per-light
visibility/positions identical; gzip JSONL is 7.0MB instead of 134MB. No live capture
during preparation. Saves a fresh `artifacts/light-research/visibility-motion-*/`
with `visibility.jsonl.gz` and `summary.json`; all received frames,20s after WebSocket
connect, bounded8MiB per message/1GiB input. No authored vector or second subscription.
Send END feedback when done; analyze whether the flash is unknown/null or trueclear.

One next step: that capture DURING owner movement. Do not add movement invalidation,
hysteresis, conceal stale measurements or rewrite scheduling on this hypothesis.
Published 2.2.0 below remains unchanged.

# Previous checkpoint — 2026-09-26, Codex: 2.2.0 PUBLISHED on GitHub

Owner explicitly authorized publication after the exact-ZIP live test and scans.
[GitHub release v2.2.0](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/releases/tag/v2.2.0)
is public, non-prerelease and latest, published 2026-09-26 16:57:22 UTC.
Tag points to `31c88361fdbb54e29b6bd976f04ba4d9cc78541c`. Release ID397320736,
asset ID591055272, ZIP905575bytes. GitHub's asset digest matches the tested ZIP:
`9DEACE1ACAA6E953FA72140E0C7A7A3C13CC4FDCE7708D703DAC130792F6C014`.
Unauthenticated public API verification confirmed release and asset. GitHub About
now uses the prepared all-around lights / physics visibility description.
Corrected smoothing fallback wording and full ZIP0/67 / ASI4/71 / Defender results
are in the published release notes. No binary rebuild/repack; Nexus untouched.
No release work remains for this request. Prior candidate checklist below is history.

## Previous checkpoint — Claude: release candidate 2.2.0

**Antivirus COMPLETE, Codex:** owner requested VirusTotal checking; exact ZIP
and ASI submitted. ZIP: 0/67 detections (1 engine failure, 7 unsupported).
Separate ASI: 4/71 (CrowdStrike, Cynet, McAfeeD, Microsoft; 66 undetected,
1 timeout, 4 unsupported). Same four vendors as preserved 2.1.12-fire-visibility.2,
not proof of false positives; no Bitdefender/Barys family in this scan.
Defender reports no threats for both, exit0, remediation disabled and hashes
unchanged. Details/links: [releases/v2.2.0-validation.md](releases/v2.2.0-validation.md).
Raw reports: `artifacts/validation-v2.2.0-antivirus-20260926-184457/`.

**Owner confirmation, 2026-09-26, Codex takeover:** owner explicitly confirmed
the exact `v2.2.0-ModManagers.zip` live test: "Hat soweit alles geklappt."
Codex rechecked ZIP/expanded ASI hashes against the values below; both match.
This is owner-reported live acceptance, not an additional instrumented API test.
No rebuild needed. The D3D12 test executables and Claude's new `--upstream` /
`--engine-lights` modes are present; package-build CTest logs record their passes.
They use synthetic WARP data, separately from the successful game test.

Owner asked for a new release after upstream.1 worked. Decisions (owner): version
**2.2.0**; physics visibility is "DAS MAIN FEATURE von 2.2" -> production, ON by
default; publish on **GitHub only**, after the owner live-tests the exact ZIP. The
owner renamed the GitHub account to **ZappendusterFX**: origin now points to
https://github.com/ZappendusterFX/CrimsonDesertTelemetry.git; current-facing texts
use the new name, historical notes keep old links (GitHub redirects).

- Code (27259c6): OFF compiles `physics_probe.cpp`; `[SourceVisibility] Enabled=1`
  drives the continuous 9-ray fan (`Start` refuses non-continuous in OFF, manual
  probe log is ON-only). OFF sets the SDF `sourceVisibility` path false. Physics
  test target builds with `CDT_RESEARCH=1`. Template: Upstream=1, Enabled=1,
  HideOccluded=0, Radius=100 (35 settings). Docs, changelog, `docs/releases/v2.2.0.md`,
  packaged READMEs and description drafts updated. Nexus BBCode keeps a
  `VALIDATION_PENDING` placeholder for the live/scan result (not for Nexus now).
- Tests: managed 78/78 PASS; native CTest 34/34 on the exact package build
  (`build/native-package-release`, `CDT_RESEARCH:BOOL=OFF`); package self-test,
  expanded payload and ZIP payload PASS. ASI strings: "Light visibility
  unavailable" and "ManyLights INPUT stream enabled" present, "Physics manual probe
  ready" absent.
- Package: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.0-ModManagers.zip`
  ZIP SHA256 `9DEACE1ACAA6E953FA72140E0C7A7A3C13CC4FDCE7708D703DAC130792F6C014`,
  ASI `CDEAB32F8559A39803C70CE036F60B6B476A411BDDF58F4890A67D29BC866A98`.
  Expanded: `artifacts/mod-manager/v2.2.0-20260926-173840-107-ce091b02/`.
- Not done: `git push`, tag `v2.2.0`, GitHub release, GitHub About text (draft in
  PUBLIC_DESCRIPTIONS.md). Local main is ahead of origin/main (fast-forward).

**One next step:** report the completed scan, then finish the pending GitHub-only
publication with the exact unchanged ZIP. Do not request the completed owner live test
again. The owner authorized pushing the completed work after the small text fix.
The release-note fallback sentence is now corrected: the HUD falls back to rendered
output, but configured upstream smoothing deliberately reports unavailable rather
than silently changing its source. No binary/package change. Use the unchanged ZIP
and corrected `docs/releases/v2.2.0.md` for the separate GitHub release step.

# Previous checkpoint — 2026-09-26, Claude: all-around engine lights integrated

Owner result: the private 2.1.15-upstream.1 package below "Funktioniert Problemfrei".

Owner asked to put the ManyLights-input findings into plugin and HUD, changing
existing behaviour where sensible. **Implemented; NOT yet live-tested in the game.**

- ASI: `[Lights] Upstream=1` (default) copies the full ManyLights INPUT with EVERY
  filtered sample at the existing hook, same list and fence. It uses the verified
  enhanced-barrier SRV round-trip and the same exact-build anchors as the pair
  diagnostic (owner+0x628 load, binder bytes, build 25477059). Render MMF v4 = the
  unchanged v3 prefix + input block, with header inputResource/inputState/inputBytes
  at 160/168/172. A faulting input wrapper costs only that sample's input
  (`ResolveInput` has its own SEH); refused anchors or enhanced barriers yield
  inputState Refused. The pair diagnostic keeps working beside it (only a pending
  SAVE is "busy").
- Host: `UpstreamLightDecoder` applies the PSO475 rules and publishes
  `lights.upstream` (schema 1.6). It reads the consumer DWORD0 bound, turns -2/count
  groups into one summed light at the derived member mean, skips members as
  standalone, excludes and counts special negative-RGB records (view-space x/y),
  and applies the floor+matrix colour. `rendererSelected`/`renderedSampleIndex`
  pair each light with this capture's filtered output by colour fingerprint plus a
  position bound. Physics targets are now the input lights (behind the camera too),
  and paired filtered contributions reuse their input light's result. The smoothed
  feed uses the input exclusively when configured (new source/coverage labels).
- HUD: prefers the fresh input (radar and markers all around), falls back to the
  filtered output with the label "RENDERED ONLY / ENGINE INPUT UNAVAILABLE". Filled =
  renderer-selected, hollow = current but not selected here (not OFF). Detail
  cards show GROUP of N / SELECTED; the legend adds a coverage line; refused input
  raises an error notice.
- Validation: real PID2252 pairs replayed through the production decoder
  reproduce Codex's results exactly. ABA headers 1462/1615 -> none -> 1360/1513;
  1360 = RGB 2.0874570/0.6492280/0.1595906 -> output slot 4; 1770/1736/1770. Camp
  facing: 110 lights, 97 selected, the 3 unmatched outputs = the 3 excluded
  specials, so all 100 outputs are explained. Camp away: 109 lights, only 3 selected.
  Tests: 34/34 CTest research AND production builds, including new
  `manylights-upstream-d3d12` (WARP + debug layer) and `engine-lights-d3d12`;
  managed suite plus 4 new tests; 160 Python tests; package self-test. Schema
  accepts real output and rejects 5 negative controls (mini validator, scratch only).
- Package (private, research, game CLOSED for DMM install):
  `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-upstream.1-ModManagers.zip`,
  ZIP SHA256 `B6864F3C31F2F5812F59680C4CE2610D201F1AFFA6ABC66BD31A3C89A1A1A185`,
  ASI `32C1F895184C00A1BA76AE3FD240B3BF098261F1A354091829FE7FB678EB55DA`.
  INI = manylights.1 values (PhysicsVisibility=1, ManyLightsPair=1, Radius=100,
  HideOccluded=0) plus Upstream=1.

Not established: live GPU cost of the second 1.5 MB copy per sample, CPU-side
emitter/light culling before upload, per-light physics refresh with more targets,
and HUD readability in dense scenes. **One next step:** the owner installs the whole
ZIP via DMM with the game closed, then checks the camp: turn away from the lamps.
Radar should keep them as hollow rings, with the legend showing ENGINE LIGHTS.
Check the native log for "ManyLights INPUT stream enabled" and no "INPUT unavailable"
spam. End for owner action; do not poll.

# Earlier checkpoint — 2026-09-26, Claude: PIX question answered

**Codex's pending PIX question is answered OFFLINE; the requested PIX UI screenshot
is not needed.** The complete access history of ManyLights input 213 was derived
from the existing export. A hit counts only where the executed shader declares
the register; all 287 compute PSOs were extracted. Nothing writes 213 before
ProcessManyLights (GlobalId93) in the captured frame, so it consumes the PREVIOUS
frame's producers. Producers: q4 InjectLightsCS 757 (new: CPU light lists) ->
InjectLightGroupsCS 759 -> GPUSpawnPointUpdateCS 771/775 (group headers) ->
InjectEmitterLodLightCS 779, then q1 GPUParticleUpdateCS 11304/12952-12967 (new:
the actual group-member writer). Fence values chain them into the next frame's
ProcessManyLights. The structure counter alternates per frame (230 consumer, 234
producer here); the paired diagnostic reads the consumer counter, which is correct.
Group members are one emitter's particles: negative color.w, slot = header+1+index,
RGB clamped [0,200]. The special negative-RGB records come from InjectLightsCS and
carry VIEW-space x/y, not world x/y. Details: LIGHT_CONTROL_RESEARCH.md, "Resource
213 access history, resolved offline". Evidence:
`artifacts/light-research/manylights-input-history-20260926/`.

No game, plugin, package, config or PIX UI action. The game was not running at
session start; WinPix was left open by the owner and untouched. Still open before
integration: group presentation (derived centroid), special records (exclude and
count), a continuous bounded input readback (the diagnostic allows 8 transactions
per process) and unexamined CPU-side upload selection.
**One next step:** with owner approval, design the default-off upstream stream:
consumer-bounded standalone and group records with derived centroid, special
negative-RGB records excluded and counted, renderer output kept as reference.

# Previous handoff to Claude — 2026-09-26, Codex (history)

**Cold re-entry:** read [CLAUDE_REENTRY_20260926.md](CLAUDE_REENTRY_20260926.md)
first. It covers product/private-release state, camera PR, the working experimental
physics path, unresolved ambient limits, the completed upstream-light ABA, tools,
evidence and the next scoped decision. Older "next" paragraphs below are history.
Owner stopped PIX Computer Use and requested this handoff: only the queue menu was
opened/highlighted; resource213 history was NOT inspected/exported. No new live
result, game/plugin change or release in this handoff. Do not resume UI automation
or repeat ABA automatically. Last runtime verification is PID2252, pair7/8, AN;
current process liveness is not checked. Next: resolve or explicitly scope upstream
group-position/special-RGB semantics before integration, retaining paired output
as reference; use existing PIX exports for the bounded producer question if needed.

## Paired ManyLights input/output diagnostic checkpoint

**Latest bounded PIX producer check:** owner has the old capture open and analysis
started. GPUSpawnPointUpdateCS (PSO21575, GlobalIds771/775) writes the exact
`look.x=-2 / look.y=count` group header at byte40, then zero-initializes members;
u38/space39 resolves to213, counter u19 to234. This is an initializer, not the
final member-color writer or a proven join to the live bowl. Separately extracted
PSO21582 InjectLightGroupsCS (GlobalId759): t16/space37 ->14963, u12/space39 ->213;
it copies named LightGroupInstanceData from a dynamically indexed SRV array.
Do not conflate its "groups" with the 16-member particle groups. Details in
LIGHT_CONTROL_RESEARCH.md. No live/plugin/API changes. Next PIX question is
resource213 write history relative to ProcessManyLightsCS GlobalId93; numeric
GlobalId order alone is not queue execution/dependency order.

**ABA COMPLETE, specific rear bowl:** PID2252, owner-operated AN/AUS/AN,
fixed anchor(-10492.520996,606.882263,-4446.792969), not the shrine.
Current input groups2 ->0 ->2; matched members32 ->0 ->32; paired output
matches1 ->0 ->1 within0.45gu. Out-of-bound position matches stay89 throughout:
tail presence cannot represent ON. Input bounds1770 ->1736 ->1770, difference34
equals two headers +32 members. A1 headers1462/1615 become1360/1513 in A2;
slot numbers are transient, not object identity. A2 group1360 converted RGB
matches output slot4 within4.452e-8; the other group has no nearby output match.
A1 run5 tick7000687 frame40626 fence88893; B run6 tick7196187 frame52356
fence92403; A2 run7 tick7369453 frame62750 fence95511. Progressing, fenced pairs.
Evidence: `artifacts/light-research/manylights-aba-2252-rear-bowl-20260926/`
with `A1-on/`, `B-off/`, `A2-on/`, each original binary/log/INI/groups.json.
Player before A1(-10493.513,605.4289,-4447.177). Specific-source freshness/control
validated; not complete360 coverage or ready-to-publish semantics. No further
owner toggle required. Next: resolve/declare group position and special colors,
then scoped upstream-feed integration retaining renderer output as reference.
No polling/session wait between owner replies. 7/8 requests consumed.

**LATEST: input bounds + group semantics resolved offline on saved live pairs.**
Use `groups.json` in each of the four pair directories, not full-capacity PI
counts as current counts. PIX PSO475 reads SAME structure counter DWORD0 as
input thread bound and DWORD1 as append/output count. This is NOT the separate
producer/free-list counter from older research. Live pair bounds are2441/1641/
1749/1833. A group header has signed int32 at+40=-2, +44=count; its next count
records with color.w<0 are summed, not standalone lights. Group-member standalone
threads skip when color.w<0 AND half(+46)<0. Header itself need not have PI marker.
Rear bowl121 proximity hits =32 in current input bound +89 out-of-bound tail;
two explicit16-member groups at headers1383/1570. Sum RGB, apply shader5% luminance
channel floor then exact3x3 matrix -> output slots40/37, max errors1.925e-7/1.754e-8.
L1 at front80 hits =16 current members +64 tail -> ONE group/contribution. Blue3
=1 current standalone +2 tail. Shrine220 includes192 tail and28 matched current
members belonging to TWO16-member groups (4 members outside proximity tolerance).
All known front-facing lamp/glass output colors agree within4.41e-8; current
groups/standalones persist with camera away even though output matches vanish.
Tool `scripts/Analyze-ManyLightsInputGroups.py` +6 tests added; combined12 tests PASS.
This is strong live agreement with the captured shader rules, NOT proof current
shader bytecode is identical or all upstream records are product-ready. Exact
group render-position uses additional constants/blue-noise selection; special
negative RGB needs exposure constants (3 such prefix slots in front pair).
Next: finish position/special-case handling or explicitly scoped derived semantics
before wiring a new current upstream feed. No public API/plugin/HUD change yet;
no more camera-turn tests needed for this finding. Do not dedup arbitrarily.

Private `2.1.15-manylights.1` adds a bounded input/output snapshot at the EXISTING
post-ProcessManyLights hook. No new hook, no public API change, no 360-coverage
claim. Installed and first paired readback LIVE-validated in PID2252, started
2026-09-26 13:16:36+02. The input is diagnostic-only, NOT shown in HUD/API yet.
Private diagnostic package (unchanged):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-manylights.1-ModManagers.zip`.
ZIP SHA256 `9B19C6EFE06D417A39622AD9089D66DF014154745A031C81F112CC91E63B5052`;
ASI SHA256 `27FD7CF81876AC2D9BC89D0EA7610AA9D9263451759D237325C7C3C644BB0556`.
Expanded: `artifacts/mod-manager/v2.1.15-manylights.1-20260926-100424-306-34410caf/CrimsonDesertTelemetry`.
PASS7 focused native tests (pair WARP, existing lights/ambient/both sky orders,
preflight and thunk),6 decoder tests, package rules and exact ZIP/expanded check.
Initial package validation rejected the new INI key because its allow-list had
not yet been extended. Fixed validator and reran successfully against the SAME
immutable ZIP; no binary/config/archive replacement during validation.
INI: Research.ManyLightsPair=1, PhysicsVisibility=1, Radius=100, HideOccluded=0,
legacy SourceVisibility=0. Installed ASI hash and these INI values verified.
Seven requests consumed7 of8 transactions. `scripts/Start-ManyLightsPair.ps1`
requests ONCE and returns immediately, no waiting.
Save the new `manylights-pair-<pid>-<tick>-<run>.bin` and native log from the ASI
directory. **Rotation comparison and known-source group/color controls COMPLETE.
Next follows the LATEST checkpoint above. No more camera turns required now.**
Do not publish all PI candidates or replace raw output with retained history.
Decode with `scripts/Decode-ManyLightsPair.py --input-space world` for this build;
`--anchor NAME X Y Z` repeats. INPUT positions are already WORLD coordinates;
OUTPUT positions still require the paired camera addition.
Each file includes scene2816 + output full capacity + output counter256 + input
full capacity, same submission fence. Input PI-marker/position matches are
candidates, NOT an active-light count; never apply output counter to input.
Control expires after5s without an eligible frame; max8 paired transactions per
process; idle until explicit request; rejects loading, busy paired requests and
faults. Copy restores input shader-resource access with enhanced buffer barriers.
See LIGHT_CONTROL_RESEARCH.md for build-specific input provenance.

**Specific rear bowl14:07:** owner corrected the mistaken use of shrine anchor;
new location measured explicitly. API159942 player(-10485.007,605.23517,-4439.536),
fresh pair4 frame27555/fence45753 has the only nearby rear light cluster within
35gu at(-10492.52100,606.88226,-4446.79297),10.58gu from player. TWO valid output
contributions (slots37/40), centroid used as anchor;121 PI input candidates
within0.45gu. Widening match radius0.6/0.75/1gu keeps121. This is NOT the shrine
bowl's220. Association is proximity behind camera plus owner's bowl description,
not a decoded physical-object name. Input positions span X[-10492.590,-10492.297],
Y[606.605,607.236],Z[-4446.79297,-4446.79248]. Evidence:
`artifacts/light-research/manylights-pair-2252-20260926-rear-bowl-1407/` original
4596968-4.bin,log,INI,decoded-world.json. No new source-validity conclusion.

**Latest pair13:59, camp BEHIND:** frame352,fence37401,output23 valid (2 behind).
L1/L2/L3/L4/shrine/blue/Twilight input candidate counts remain EXACTLY
80/79/79/79/220/3/1; all seven anchor locations now have ZERO output matches,
versus1/1/1/1/2/1/1 at13:56. All matched input positions are behind camera.
Camera(-10507.34277,608.14697,-4450.25195),forward(-0.373713,-0.188034,-0.908285).
Forward changed155.197deg; camera displaced11.627gu during third-person orbit
(owner reports only turning, player pose not stored in paired binary). Samples
184.875s apart, not simultaneous across poses; each input/output pair IS fenced
together. This locates known-position loss in renderer selection before HUD;
it does NOT establish full input freshness/completeness or one fixed near radius.
Evidence: `artifacts/light-research/manylights-pair-2252-20260926-camp-away-1359/`
contains original4116015-3.bin,log,INI anddecoded-world.json. Initial terminal
angle180 was a PowerShell integer Clamp overload artifact; corrected explicit
double Clamp yields155.197deg. Input rawColor.w sign is NOT an active flag:
all matched fire records are negative even in the front-facing control that
produces output. No game/config/HUD change. Result saved, no owner wait.

**Control pair13:56, camp IN FRONT:** frame55620,fence34188,camera
(-10508.72949,607.93359,-4461.79443),forward(0.121950,-0.152331,0.980777).
100 valid output records. L1/L2/L3/L4 each input80/79/79/79 candidates and1
output; shrine input220/output2; blue input3/output1; Twilight Glass input1/
output1. All anchor matches now in front. Both previously missing static lamps
are present in output again, but owner moved since first pair: NOT a controlled
rotation-only comparison yet. Use this pose as fresh control. Evidence:
`artifacts/light-research/manylights-pair-2252-20260926-camp-facing-1356/`
contains original3931140-2.bin,log,INI anddecoded-world.json. No feed/HUD change.

**First live pair, camp behind:** frame35571,fence7092,output61 valid records.
Known blue IC White Pavilion and Twilight Glass positions match input EXACTLY
but are absent from simultaneous output. Shrine matches input only within0.45gu.
Four known lantern anchors each have ONE output match in this sample; do not
claim all four vanished. Input has18922 PI candidates, NOT18922 current lamps:
repeated/generation validity and input color semantics remain unresolved.
Evidence: `artifacts/light-research/manylights-pair-2252-20260926-camp-behind/`:
original binary, native log, INI and `decoded-world.json`. Initial `decoded.json`
is INVALID FOR INPUT POSITIONS (assumed camera-relative, added camera twice);
preserved for provenance, superseded by explicit world-input decode and test.
No public feed/HUD change or rebuild this turn. Older "next" entries below are
historical; the first input readback is now complete.

**Owner's distinction (2026-09-26):** renderer camera/view-dependent selection
versus geometric line-of-sight from the camera POSITION, including behind camera.
Keep both separate from screen-frustum membership and actual object ON/OFF.
Input/output comparison concerns renderer selection; physics sampling concerns
line-of-sight. Missing from output is NOT automatically occluded or switched off.

**Historical claim check:** owner quotes Nexus2.1.10 changelog: "Expanded
rendered-light capture to include off-screen and behind-camera sources."
Exact sentence not found in tracked Git history; do not dismiss the owner's
public-release evidence. At release099c39e, RenderLightReader.cs and
filter_thunk.asm are byte-identical to current HEAD; native capture already
copied filtered R12 output + paired counter, not the upstream input. Archived
Nexus description atddf1bf9 explicitly says view-filtered/not a complete world
registry. Thus off-screen support was claimed, but complete360 coverage or a
removed input-capture feature is not established. Historical broader coverage
and possible game-side changes remain unresolved; no public text changed.

**100gu audit,2026-09-26:** owner reports a rear bowl disappearing with distance,
returning on camera turn; suggests either forgotten plugin radius or engine near
coverage. Installed Lights.NearbyRadius=100, LightOverlay.Radius=100. Live host
PID27540 command line has BOTH `--light-radius 100` and
`--physics-visibility-radius 100`; API reports nearbyRadius100. Snapshot85929 /
capture21654 publishes a front source at36.35gu with fresh physics-clear63ms,
and rear sources18.75/18.73gu. HUD uses configured radius for radar/markers;
physics annotates records without removing them; native continuous limit500gu.
No forgotten general35gu cutoff found. A view-dependent ENGINE influence/near
region remains plausible, not a measured fixed engine distance. Our radius
does not expand the engine-selected output. Owner has moved since the first
paired capture; do not call a later camera-facing sample a same-location control
without checking its pose. No config/plugin change for this audit.

**Current: off-screen coverage audit, 2026-09-26 (no runtime/code change).**
Owner needs CURRENT lights around360 degrees, including behind the camera, and
remembers broader previous radar coverage. At night more lights return; rotating
the camera brings more into the feed. Do not dismiss that observation or call
complete360 coverage impossible. ESC is being used by owner to freeze game time.

**Latest: actual ManyLights INPUT resolved, read-only,2026-09-26.** Owner rightly
redirected investigation to the INPUT, not another output/HUD audit. Current EXE's
ProcessManyLights setup loads `[R13+0x628]` at VA0x143DA9406, registers the literal
`g_manyLightsDataBuffer` at0x143DA9452 and passes that wrapper as R8 to its SRV
binder at0x143DA94C5, before Dispatch at0x143DA97D4. R13 is the same owner already
captured in our Render bridge. Live PID24696/sample74395/frame48659,age47ms:
owner0x4B1AC67DC00 -> +0x628 outer0x4B18A88D200 -> +0x30 inner0x4B18B2F2840;
stride48,count32768,resource0x133430C90, DISTINCT from output0x133436AC0.
Live load bytes `498BB528060000` and name string agree with exact-EXE analysis.
This verifies the input route/resource wrapper, NOT GPU contents or complete360
coverage. No new hook, query, game write, package or config change. Details and
evidence: LIGHT_CONTROL_RESEARCH.md, section "ManyLights input for off-screen
coverage". Next is a fence-completed INPUT+OUTPUT+scene paired diagnostic at the
existing boundary; retain current public output. Do not apply output valid-prefix
counter semantics to input or blindly publish all32768 producer slots.

**Latest: known physical anchors missing when looking away,09:19.** Owner asks
whether HUD dots are real lights or artifacts, then supplies screenshot4e2246e5
looking away from camp,player(-10538.404,608.976,-4431.55),radius100,22 shown/0 hidden.
Passive API sample51909/frame41062,age31ms at this pose has26 sources (NOT the
exact screenshot frame). Compared with saved40947: shrine bowl's two known
contributions, exact blue IC White Pavilion position and exact Twilight Glass
position were present before and now absent, despite current player distances
33.14/66.25/80.82gu respectively. Prior anchor matches within0.4gu, none now.
This is concrete loss of previously physically-associated lights, not merely
count fluctuations. Known four lantern anchors are absent in BOTH compared
samples; do not claim this pair proves their disappearance specifically.
Evidence: artifacts/light-research/offscreen-known-anchors-20260926-0919.json;
prior physical controls: CODEX_HANDOVER_FIRE.md candle/shrine/blue/blind-object
checkpoints. Subset association supports genuine lights, NOT authenticity of every
unlabelled point or historical complete360 coverage. No new toggle requested.

**Latest DIRECT raw audit,09:14 (owner explicitly corrected focus to reader loss):**
Read an existing stable/seqlocked native Render MMF, no new GPU query or game write.
PID24696,capture47052/frame26305,age78ms. Capacity32768; paired GPU validCounter=90.
All90 prefix markers are PI,0 zero,0 other; original decoder on SAME bytes/player
with radius100 publishes74,outside16,malformed0. Diagnostic-only decode with
radius100000 publishesALL90. The16 excluded positions are113.06..481.91gu away,
so no current valid-prefix light INSIDE100gu was lost in this sample.
Rest of buffer contains415 more PI-marked records OUTSIDE valid prefix; existence
is measured, current-frame validity is NOT. Never publish these as live or call
them additional active lights based only on marker. Earlier diagnostic output
was truncated, so it was repeated with bounded SUMMARY output; final evidence is
artifacts/light-research/offscreen-raw-prefix-audit-20260926-0914.json.
No settings/code changed. This audits our POST-filter copy, not the upstream
ManyLights INPUT, which has not been read/compared live in this investigation.

**Rear-source distinction,09:07/09:08:** new screenshot yaw240.4,pitch-0.9
matches two passive API reads40947/frame8194 and42216/frame11959, camera identical.
74 published (90 active,16 outside100gu,0 malformed);8 behind camera:6 spots,
2 points. Rear distances11.12..17.92gu,angles133.3..165.3 degrees from forward:
genuinely behind, not merely outside the drawn frustum. Front sources extend87.79gu.
Rear luminance0.034..0.842 includes faint spots, not only exceptionally bright lights;
all8 physics-clear, but hide-blocked=0 in screenshot, so that is not why others vanish.
Strong observed distinction is proximity, not a unique point/spot class.
At XZ(-10497.55,-4358.118), earlier front-facing screenshot yaw28.6 shows TWO point
contributions (L~0.1846/1.171); both current rear-facing reads contain only ONE,
L0.842 at(-10497.625,611.2619,-4358.118). This supports per-contribution selection
and brightness/extent dependence, NOT a proven current shader branch: screenshots
are different times and individual effect contributions can change. Do not invent
a physical lamp name, stable slot identity or measured influence radius.
Evidence: artifacts/light-research/offscreen-survivors-20260926-0907.json;
owner screenshots83e3a71c/eff9da2f/37435a6d in chat (76/36/74 shown,all0 hidden).
No plugin/config change or new native sampling was performed.

Two passive API samples during owner-reported ESC pause,08:47:22/08:47:59:
sequence93995->96244,capture23927->24462,renderer frame23084->24657,age79/16ms.
Both contain76 lights:52 point/front,15 spot/front,2 point/behind,7 spot/behind
(front/behind = dot with paired camera forward, NOT screen-frustum membership).
Player and complete paired camera identical; light POSITION multisets identical
ignoring slot/order, RGB/luminance multisets differ. Fresh rendering continues
while pose is static; game-time freeze is OWNER-REPORTED, not measured by this API.
Evidence: artifacts/light-research/offscreen-pause-20260926-0847.json.

Offline comparison v2.0.0->HEAD: filter_thunk.asm byte-identical; managed
RenderLightReader keeps the same valid-prefix, world transform and source
acceptance/radius logic. Added visibility metadata and last-whole-sample reuse do
not filter raw records. PhysicsVisibilityClient adds metadata with Select;
radar only applies player radius, marker cap and optional hide-blocked, no frustum.
Native Record still copies post-filter output+counter at the same semantic
boundary; changes are ambient sharing, startup gating, submission handling and
exact-build relocation, not a new per-light selection loop. This is NOT a live
old/new game-build comparison or proof that all engine selection is unchanged.

Existing CAPTURE-EXTRACTED pso-475.ll (light-control-pix-20260924) confirms
ProcessManyLightsCS uses SceneConstantBuffer _frustumPlanes at1872 (CB rows117+)
and g_hiZMap in selection before atomic append to output. The tested extent
depends on RGB/brightness/distance, not simply whether the source point lies in
the view cone; special branches also exist. This supports view-dependent mixed
coverage, NOT a claim that point/spot alone determines inclusion or which branch
rejected a particular current lamp. v2.0.0 already used filtered-manylights;
complete360 was not proven for that release. Never restore stale capacity tails
to imitate a larger list. Owner's earlier denser view is not disproven.

**Next (input wrapper now resolved above):** compare the EXISTING ManyLights input
before filtering with the current output/camera. Captured binding is
t18/space37 resource213 -> u13/space39 resource217 (LIGHT_CONTROL_RESEARCH.md).
Do not mistake initial PIX resource bytes or old producer allocation contents for
fresh complete lights: validity/generation, position and RGB transformations must
be checked before exposing that input. Preserve current raw/render stream. No new
hook, build, game write, installation or rotation request performed for this audit.

**Earlier sparse daylight observations (preserved):**
Owner sees fewer radar lights even with blocked sources shown and explicitly needs
sources behind camera, not merely visible lights. One passive /v1/snapshot read
(no extra native/game query): sequence36114, playing, rendered age62ms, API radius100,
4 published/active sources, all within35gu,2 behind paired camera, all4 clear;
malformed=0,outsideRadius=0. The small set is already in the source stream, not
caused by HUD hide-blocked or current range rejection. Why fewer are selected than
in the owner's earlier view is NOT established by this single snapshot.
Second passive snapshot while owner reports NO radar lights at the four-lantern
shed: sequence42034, player(-10537.646,609.02704,-4426.5254), rendered age32ms,
capture10709/frame43672; active5,published4,malformed0,outsideRadius1. All4 clear,
measurement ages16/16/16/172ms. Three positions within~0.6gu of paired camera,
one near player; none corresponds to the four documented fixed lantern positions.
An unused PowerShell delta-array expression errored; reported positions/counts/
metadata came directly from the same JSON snapshot, not that failed expression.
Do NOT conclude definite frustum culling or complete360 impossibility explains
this new sparsity: first check established ManyLights capture/view/counter pairing
and actual lamp state. No HUD filter restores records absent from its input.
Code: radar has no camera-frustum test; PhysicsVisibilityClient only adds metadata,
never removes raw sources. .10 did not change native ManyLights capture/decoder.
Current source is renderer-selected filtered ManyLights, not a proven complete
360-degree registry. Preserve it; if full loaded-source coverage is required,
next inspect EXISTING pre-filter ManyLights findings/inputs, do not resurrect stale
capacity-tail records or fabricate an all-lights stream. No implementation change,
new capture/research or user movement test performed for this check.

**Current: owner reports successful UNCHANGED .10 restart and normal gameplay.**
No INI edits were made: Radius=100 and PhysicsVisibility=1 remain the tested
configuration. Neither the proposed35gu comparison nor PhysicsVisibility=0 was
performed. Owner says everything looks good. During loading, the NVIDIA FPS
counter displayed9999 for a prolonged period, then normal FPS ingame. This is a
reported COUNTER DISPLAY, not a verified actual frame rate or established cause.
Owner requests documentation only; do not investigate this deeply unless strong
relevance emerges. No new process/API inspection, config change or test this turn.
Successful startup does not explain the previous hang or establish reliability.
**Next:** continue normal use at100gu; no forced configuration downgrade, extra
scene test or public release. Wait for the owner's next request, not a live poll.

**Historical .10 startup hang, 2026-09-26 08:06 run, PID16988.** Owner reported
frozen startup with continuing sound; at inspection process was alive, Responding=false.
All THREE installed binaries match the exact .10 ZIP; Radius=100 and physics
enabled confirmed in INI/bootstrap/native logs. Not another wrong-file assumption.
Host started, overlay ready, playable-world signal received; no explicit failure
logged. No new Application1000/1001/1002 since launch or System4101 in checked
window. These absences do not exclude a GPU failure or prove a physics deadlock.
Only logs/process state/config/hashes inspected; no API, extra game calls, dump,
debugger, runtime writes or process termination. Cause remains UNKNOWN.
Evidence: `artifacts/light-research/loading-hang-16988-physics10-20260926-081043`
(copied logs+INI). The2ms native issue budget cannot interrupt a blocked call.
After closing, the process remained listed (four threads); owner reported GPU
usage>90% and taskkill /PID16988 /F returned "Von dieser Aufgabe wird momentan
keine Instanz ausgefuehrt." Windows restart was recommended. No root cause proven.
Proposed35gu/off comparisons were NOT performed; superseded by unchanged success
above. Preserve the evidence; do not blame or exonerate100gu/the mod from one run.

Owner reports the current installation works well / almost live; occasional
"measurement expired" and many "outside sampled range". The preceding loading
hang was withdrawn by owner (likely wrong file); cause remains unproven and its
saved evidence is preserved. No further hang or ingame investigation requested.

**physics.10:** owner approved trying100gu and INI control. Reuses existing
`[LightOverlay] Radius` (1..500), no competing second setting. Target selection
now uses PLAYER center like HUD; actual rays still start at paired CAMERA.
Host receives the same HUD-parsed value via --physics-visibility-radius and raises
API coverage to at least that radius (preserves any larger Lights/NearbyRadius).
Native continuous query/fan/segment bounds extended together to cover500gu plus
12gu camera offset/fan offsets; manual diagnostic49.5/50gu bounds unchanged.
Max256 targets,20 rounds/sec,shared2ms issue budget,9 rays and500ms expiry unchanged.
Radius vs no-target reasons now separate. Raw records/RGB and latest-result policy
preserved. At packaging time it was not installed/live-tested; subsequent startup
failure and owner's unchanged successful run are recorded above.

PASS focused managed physics tests (1/35/100/500gu boundaries, camera offset/orbit,
300 raw lights vs256 targets, invalid radius, existing freshness/fault cases),
native physics + overlay-model tests, CLI invalid-radius rejection before game
access, package profile/expanded/ZIP validation. No performance claim at100gu yet.
Ready WHOLE private DMM ZIP (game must be CLOSED for replacement):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.10-ModManagers.zip`.
ZIP SHA256 `F53DB9D85B4AE429CD0C1724328BD5710C68F3D74D9F4B573648FA9341BBB4D2`.
ASI SHA256 `68B7E2A0088801BC0E1A455A39BC63FD56A4C6E39994A2DD7DD4A4B011EC2346`.
Expanded `artifacts/mod-manager/v2.1.15-physics.10-20260926-080406-439-80d87ea4/CrimsonDesertTelemetry`.
Radius=100; PhysicsVisibility/HideOccluded ON, manual/legacy/ambient paths OFF as.9.
**Historical next (superseded by hang above):** owner installs whole .10 via DMM and judges ordinary use at100gu. Do not
wait/poll or start another scene experiment without owner input. No public release.

## Historical physics.9 checkpoint

**Current: physics.9 implements owner's direct/raw-measurement policy.**
Owner: camera orbit alone makes .8 show all hidden markers as "refreshing after
movement"; explicitly no more ingame checks. Asked to REMOVE movement analysis
and consume latest complete measurements. This supersedes the proposed native
rewrite and the earlier "do not remove distance gate alone" recommendation below.

API+HUD no longer invalidate on camera movement; no two-measurement confirmation.
Each complete9-ray result applies immediately. Frame/camera provenance remains
ACTUAL measured data. New optional measurementSequence + measuredAtTickMilliseconds
(Windows uptime, not UTC) allow consumers to distinguish real samples from repeat
delivery. Age cap500ms; invalid/incomplete/fault ->unknown; skipped budget doesn't
refresh age. HUD no longer adds unrelated GPU age to physics age. Raw light data
unchanged. Native ray code/rate/budget/256-target queue unchanged (log stillv7).

Focused checks: managed physics test now rapid orbit and immediate alternating
verdicts for96 targets, raw metadata/JSON and expiry; native overlay-model movement,
hiding, transport age and expiry. No ingame access/new scene experiment this turn.
Both focused tests PASS; .9 expanded/ZIP/config validation PASS. No live acceptance.
Ready WHOLE ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.9-ModManagers.zip`.
ZIP SHA256 `391F185D462CA520A4B7DC2C810A737819FA98BAF261612E4785729800C50913`.
ASI SHA256 `7DEB0F584A3582A7B0E8080F1CD6101879FF0510243082DCB9111AF6EF1C5136`.
Expanded `artifacts/mod-manager/v2.1.15-physics.9-20260925-235907-563-e7b75063/CrimsonDesertTelemetry`.
PhysicsVisibility/HideOccluded ON; old visibility/manual/ambient paths OFF as .8.
**Next:** private .9 whole ZIP via DMM with game CLOSED, no INI hand edits; owner
can judge direct response on ordinary camera movement. No public release/push.
Do not claim live acceptance or assume higher Hz; max20 rounds/sec is not a
guaranteed per-light rate when shared2ms budget is exhausted. End for owner action.

## Historical physics.8 moving failure and superseded design proposal

**Current: physics.8 FAILED moving acceptance; stationary works (owner report).**
Owner sees all markers "refreshing after movement" while running, correct hiding
when stationary. Explicitly NO further ingame inspection requested; none performed.
Do not call .8 accepted or ask for the same wall test again. No new ZIP this turn.

Offline code confirms TWO pose-distance gates: PhysicsVisibilityClient.Resolve
invalidates displacement>.25gu; ApplyResponse also resets blocked confirmation
unless successive measurement receivers stay within.25gu. HUD repeats the first
gate. With max20Hz requests, straight movement>5gu/sec already defeats consecutive
confirmation even with zero round-trip overhead. The96-source synthetic movement
test only moves.2gu/60ms (=3.33gu/sec), so it did not cover running. This is a test
gap and a freshness/hysteresis design flaw, not disproven physics geometry.

Next direction (NOT IMPLEMENTED): keep proven ray fans/ManyLights, but native-side
scheduling using owned camera-paired capture copies, avoiding managed-host query
round trip; native calls remain in validated original physics context, NEVER on
render/readback worker using borrowed game pointers. Treat separately (a) each
measurement's actual camera/frame/time, (b) bounded display/consumer latency, and
(c) temporal confirmation of successive NEW results. Ordinary continuous motion
must not require a stationary receiver to confirm fresh blocked results. Teleport,
loading, source change, missing/expired measurement still invalidate. Removing
the distance gate alone, relabelling old results fresh or enlarging queues is not
an accepted fix. Need high-speed/delayed-response regressions before next package.
Native PublishSample already has paired scene/lights/counters after GPU fence;
this is an existing input boundary, NOT proof of zero latency or a safe query thread.

## Historical physics.8 build checkpoint

**At build time: physics.8 package / v7 batch refresh; .6 live worked only while stationary.**
Owner observed .6 hides walls after ~1s but movement brings markers back. PID468,
installed ASI matched .6; native/result mapping had valid9-ray results (including
clear=0), API had clear plus14 camera-moved unknowns. Cause:20 individual fans/sec
round-robin versus .25gu receiver-displacement invalidation, not a broken query.

.7 batches the nearby scene at max20 rounds/sec with ONE shared2ms issue window.
Owner rejected24-target ceiling: queue now holds up to256 distinct nearest
positions within35gu, oldest attempted first (untested budget tail prioritized).
Safety/context/controls, raw streams, .25gu invalidation, two-blocked confirmation
unchanged. No pretending an old pose is current; motion/overload can still yield
unknown. V2 MMFs32800bytes. Full contract in SOURCE_VISIBILITY.md.

WHOLE private DMM ZIP, ready settings (same as .6):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.8-ModManagers.zip`
ZIP SHA256 `072975997028A02332D311CD44E3D6468835E64D812634B5BD7AFF473D7E8CCE`.
ASI SHA256 `29A6438229447B4C7708C4720D6A05911026987F62F2858F0FBFFEDCA1982E9F`.
Expanded `artifacts/mod-manager/v2.1.15-physics.8-20260925-232643-119-12e30330/CrimsonDesertTelemetry`.
physics.7 ZIP retained but not delivered: its INI comment still said24 targets.
physics.8 corrects this to256; same runtime implementation (native logv7).
Experimental.PhysicsVisibility=1, HideOccluded=1; legacy/manual/ambient off.
Managed suite includes96 moving positions and partial-budget queue fairness;
138 native checks include mid-fan budget expiry and batch-wide fault stop.
All31 CTest suites passed on this native binary; full managed suite and .8
expanded/ZIP payload/config validation passed. No live .8 acceptance yet.
No auto-install, push or public release. Old ZIPs/unrelated untracked work preserved.
**Historical next step (now completed, movement FAILED):** install WHOLE .8 ZIP via DMM, then normal movement past the
known wall/visible cage. Check actual refresh/budget rates if HUD still falls back;
no broad RE or blind drift-tolerance increase. END for owner action, never wait.

## Historical physics.6 build checkpoint (superseded by live result above)

**At build time: private physics.6 built and verified; NOT yet live-tested.**
Owner closed game, authorized practical integration AND removal of legacy HUD
visibility clutter. Continuous native physics now feeds rendered sourceVisibility,
radar and fullscreen markers. Raw RGB/positions/authored lights remain unchanged.
Any clear neighbor retains a source; all9 blocked needs2 consecutive valid fans.
Unknown/stale/moved remains visible. No optical percentage. New method metadata
records actual measured camera/capture, not falsely paired to a later frame.

Budget:24 nearest unique rendered positions within35gu; max20 fans/sec (one control
+9 rays each). Same original thread/lifetime;2ms further-call issue window; slow or
faulted native path latches off. Cache expires2500ms, camera movement>.25gu invalidates.
Geometry/contact consistency checked; no body skipping/near-source hit cutoff.
This bounded implementation still needs live sustained-load acceptance; .5 proved
only individual fans. Missing renderer-selected light is not OFF/blocked.

WHOLE DMM ZIP, config ready:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.6-ModManagers.zip`
ZIP SHA256 `82A5612C3C7C1A70699F27844B869FD87E8F63C8549DF0BCCAAE4B0D1C4AF4B5`.
ASI SHA256 `9522B3044A2DA70BCB7A3366218ACB9468C94609165B38F2B923B9D8CFD70E50`.
Expanded `artifacts/mod-manager/v2.1.15-physics.6-20260925-230019-258-2a46e1b6/CrimsonDesertTelemetry`.
Experimental.PhysicsVisibility=1; HideOccluded=1 (F11 toggles); legacy SourceVisibility,
PhysicsProbe/manual requests, Ambient/spatial diagnostics OFF; normal telemetry/HUD ON.
Default research template keeps new feature OFF. Legacy SDF HUD traces and misleading
contribution-percent labels removed; old research implementation/artifacts preserved.

Validation: all31 native CTest suites,129 physics checks, full managed test suite,
new MMF/hysteresis/freshness/raw-preservation tests and package profile/payload checks
pass. Initial package validator lacked new Experimental section; fixed validator
and revalidated the SAME immutable ZIP successfully (no binary/package rewrite).
No installation/publish/push. Unrelated untracked work untouched.

**Next:** owner installs whole ZIP via DMM, returns at visible lamp. Verify ASI/logv6,
read API briefly and use HUD; then wall comparison. No additional manual rayfan
requests (continuous mode owns instrument). END for owner actions; never wait/poll.
Implementation/contract: `docs/SOURCE_VISIBILITY.md`, `physics_probe.cpp`,
`physics_visibility_bridge.h`, `PhysicsVisibilityClient.cs`. No new broad research.

## Historical physics.5 acceptance checkpoint

**Current checkpoint: physics.5 visible-lantern AND wall fans PASSED, PID35192.**
Owner requests PRACTICAL scope, not extended research: visible lanterns must not
vanish because of cage bars; a wall must block them. Small comparison is complete;
next is bounded practical integration, not a new broad investigation.
Usual visible lantern: center hit0.14528gu before source; inner0.05gu rays3hit/1clear,
outer0.15gu rays1hit/3clear. All nine completed, matching control/guards/originals
passed, no exception; deployed ASI matches .5, logv5. Wall fan: ALL9 hit, NO clear
path, contacts0.38..0.49gu before source. This is a different outside-wall view
from old PID37148 (camera now X=-10523.083), not a repeat of its5gu contact.
Both transactions passed all controls.20 extra calls used;4 remain, insufficient
for another fan. No pending query. Telemetry still playing.
Evidence: final PHYSICS_QUERY_RESEARCH section (raw+decoded artifacts retained).
New `rayfan`: one matching natural-ray control + nine same-context rays (center,
four offsets at0.05gu, four at0.15gu). Diagnostic stencil, NOT source extent or
optical coverage %. Per-sample guards/fresh copies, freshness/time limits, shared
max12 transactions AND24 extra calls/process. No HUD/API visibility integration.
121 synthetic native checks, 15 decoder tests, five focused CTest suites,
PowerShell syntax and expanded/ZIP/INI package checks pass.

Install the WHOLE private ZIP via DMM with game CLOSED:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.5-ModManagers.zip`
ZIP SHA256 `103A9C5B05CF35FBF79926D694A1AE24A22AD6692B0750770F80A3F601EBE78C`.
ASI SHA256 `28EBB3BD3AB2883FD9726C95A49E7607229DB6DAD4E4A17F2351EBCCF7B7AD79`.
Expanded: `artifacts/mod-manager/v2.1.15-physics.5-20260925-222544-219-bc249ef1/CrimsonDesertTelemetry`.
Ready INI: PhysicsProbe=1, telemetry/ManyLights/HUD ON; Ambient/SourceVisibility/
spatial diagnostics OFF. No automatic install, push or public release.

Previous game: PID37148 with physics.4, six transactions used/six remain,
no pending request. Live thin ray: ground positive control passed; visible cage
contact0.14522gu before source versus outside-wall contact5.03646gu before source.
All controls passed. These are collision evidence, not full optical coverage.
HUD source disappearance was NOT physics filtering: source counts recovered
after darkening with unchanged view (usual lamp0/44 ->47/47 captures). Exact
renderer rejection cause remains unisolated; absence != OFF or occluded.

**One next step:** bounded experimental production integration: all sampled paths
blocked -> blocked; any clear neighbors -> retain as visible/partial estimate;
invalid/stale -> unknown. Preserve raw data and do NOT publish hit ratio as optical
attenuation. Add temporal stability and bounded scheduling; current one-shot
instrument does not validate sustained query rate. No more owner comparison or
source-radius research needed for this first practical approach. Full evidence,
budgets, invocation and limitations: final sections of `PHYSICS_QUERY_RESEARCH.md`.
END turn for owner actions; do not poll/wait. Unrelated untracked work is untouched.

## Historical physics.4 checkpoints (superseded by current checkpoint above)

**Current checkpoint: physics.4 LIVE controls succeeded, PID 37148.** Deployed
ASI matches package (SHA256 5751B070A63E3F34BB11E96154A5B0989D4293E240F659413C91BDF6ED2D6809).
Identical native ray replay matched a natural HIT. Downward ray hit ground at
Y=609.156670 (player feet 609.156616), normal up; surface/segment residual
0.00000188 gu. Full ray to usual test lantern hit 0.14522 gu before its source;
comparison stopping 0.4 gu before a fresh paired source had NO hit. This is
source-adjacent collision, NOT established total optical occlusion or bar identity.
Tiny camera/source drift between requests means this is not an identical-line
truncation experiment. All controls/guards/original checks passed; no exception.
Four transactions used, EIGHT remain. No pending request. Telemetry remained
available (sequence 15589, light frame 17229). Evidence/details at end of physics doc.

Previous PID 34448 ran physics.3. TWO actual ray
observations succeeded; no replay/extra query in this process. Query uses
double3 origin +0x40/delta +0x60, float3 inverse +0x80/length +0x8C. Prefix 0xA0
unchanged. First collector is the established closest-hit type, starts at q+A0;
second is a different type and is NOT assigned those semantics. Reports and
`physics-ray-natural-34448-20260925.native.log` archived under light-research.
Details, coordinates and static caller evidence: final PHYSICS_QUERY_RESEARCH section.

physics.4 adds explicit `rayreplay` / `raysegment`: same original thread/stack
lifetime, known caller/collector, isolated bounded copies, matching control
before a changed segment, guard/original checks, shared max 12 sphere/ray replay
transactions/process. Faults latch disabled until restart. Zero-axis segments
still refused. Sphere modes retained; NO public visibility classification.
82 synthetic native checks, 11 decoder tests, five focused CTest suites and
package INI/payload checks passed. Native ray replay, ground hit and a light
segment now passed the bounded live checks above; optical coverage is still open.

Current WHOLE private ZIP (already installed):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.4-ModManagers.zip`
SHA256 `D24A0EBE22C0E369887F32156DA70621A2A20C06517F1E7084BEDCD4742A6FC6`.
Expanded: `artifacts/mod-manager/v2.1.15-physics.4-20260925-213046-194-423876cf/CrimsonDesertTelemetry`.
INI ready: PhysicsProbe=1; Ambient, SourceVisibility/spatial diagnostics OFF;
normal telemetry/ManyLights/HUD ON. No automatic install/publish/push.

**Latest: owner now outside wall, asks why most HUD lights vanished.** Bounded
3-second API capture: 44 unique native captures, 34..37 active, 32..35 published,
only 3..6 inside HUD radius 35; malformed=0. Usual test lantern absent in all
44, despite its known location well inside radius. Later briefly present but
fresh selector refused (no request sent). Missing entries precede HUD display;
SourceVisibility/OcclusionTest/HideOccluded remain 0. Renderer/HUD reader code
unchanged by physics.3->4. Old confirmed ProcessManyLightsCS has HiZ-based
selection; this is a plausible cause, NOT proven current-frame rejection branch.
Evidence: `artifacts/light-research/physics-wall-missing-lights-37148-20260925/`.
One alternative full ray to currently supplied known brazier returned NO hit;
different target, NOT a successful wall-versus-usual-lantern comparison. Five
transactions consumed in PID 37148, seven remain; no pending request.

**Superseding result: darker scene, NO camera turn.** Owner returned after ~10min,
reported game darker and more lights. New 3s capture has 47 unique native frames:
94 active, 15 inside35gu, usual lamp present in ALL47 (previously0/44). Camera
forward identical, X/Z identical, only tiny idle Y changes; no setting/package
change. Strong temporal/lighting-state correlation, NOT proof of a particular
exposure or time-of-day switch. Camera-direction-only explanation insufficient.
Stored `physics-wall-darker-37148-20260925/`. No camera-turn request pending.

Fresh-source wall ray NOW succeeded for usual lamp, request
`1a1275fc96c84848a38bac7a52838ae8`: hit5.03646gu before source, almost horizontal
wall normal, geometry residual0.00000153gu; all controls pass, no exception.
Compare inside-view endpoint contact0.14522gu. SIX transactions used, six remain.
No pending query. Do not conflate source absence with OFF/blocked or this wall
hit with a complete partial-coverage classifier. **Next bounded development:**
plan justified multi-point coverage around the source using retained sphere/ray
evidence; source extent and optical-vs-collision limitations remain unresolved.
No new build, production fix or public visibility behavior is authorized by these
diagnostic results alone. No polling/waiting for owner.

Keep wide sphere tests as complementary evidence. Owner rightly emphasizes
partial cage-bar obstruction: a sphere hit does NOT mean full occlusion; one
thin ray can also hit a bar. Multiple justified spatial samples are a later
step, not implemented or validated. No arbitrary near-source cutoff/body skip.

## Preserved evidence leading to physics.3 (earlier checkpoints)

Owner explicitly deferred light switching in favor of unresolved occlusion.
Read `docs/PHYSICS_QUERY_RESEARCH.md`; use World Builder's verified native-query
route, not more SDF tolerance tuning. Working paired ManyLights remains the source.

**physics.1 baseline:** research-only `PhysicsProbe` observes ONE natural player-near sphere cast,
including input bytes, collector output, thread/caller and exact EXE. No replay,
no extra physics call, no public visibility change. It is gated to patch 2.03.02 /
EXE 1.0.0.2976 by hash, instruction bytes and collector vtable; other builds refuse.
First draft replay was deliberately removed before build. Host tests pass; now
TWO live natural-query observations succeeded in PID 12228 at the known camp.
Installed ASI matches physics.1; ManyLights/telemetry remained live. Both raw
results are no-hit, NOT a visibility result. Details/evidence in the physics doc.

Critical live findings: +0x50.xyz holds reciprocals of +0x40.xyz, not a duplicate
delta (confirmed twice). All query objects are stack-local and broad captured
ranges overlap. Do not blindly copy WB's ground-replay vector writes or remap all
qwords in overlapping windows. Caller is a forwarding wrapper, not the builder.

Earlier package: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.1-ModManagers.zip`.
Built and validated; SHA256 `0D5B720185DA6C628EB2D113586467F01E39AE5C42C1C18EFAE2708C2C7B4002`.
25 synthetic physics checks and 5 focused native CTest suites pass, including
build guard, render bridge/filter and HUD model. Saved current EXE passes actual
hash verification. ZIP/expanded payload equality and INI/package self-tests pass.
PhysicsProbe=1; Ambient and SourceVisibility/spatial diagnostics OFF for this run.
Normal player/camera + ManyLights + HUD stay ON. User installs the WHOLE ZIP via
DMM with game closed. Nothing was installed automatically; no public release/push.

**Current package: physics.2.** Owner closed the game. New opt-in `replay`
and `segment` requests run after the original call returns, on the same thread
while its caller stack is alive; guarded private copies and matching-result
control, no queued stack pointers. This context has now passed the bounded live
controls below; this is not general safety or optical-visibility acceptance.
Read the final section in the physics doc before use; native hangs cannot be
timed out safely. Segment inversion corrected; zero-axis convention refused.
No result is published as optical visibility; max 12 replay transactions/process.
ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.2-ModManagers.zip`.
SHA256 `CC84F118B1E92064B0389F89399CBC19F731D1ABCE0C42CA6334AF401A0E661F`.
Same ready-made INI as physics.1. Package content/INI validation passed. Native
tests include copy isolation, control mismatch, fault latch, canaries and bounds.
Live replay and segment controls now succeeded in PID 5468 (see below).
No public API/published release was changed.

**Earlier live result (PID 5468, installed physics.2 verified):** replay matched;
ground control hit at fraction 0.311135 with upward normal. Two camp-lantern
segments hit late (~0.907 / 0.911); stopping the first path 1 gu before its light
gave no hit. Thus collision queries distinguish hit/no-hit, but endpoint/fixture
contact is not established optical occlusion. All 5 transactions passed control,
guards and original-data checks; no exception; ManyLights continued advancing.
At that checkpoint 7 of the 12 replay transactions remained. Reports are archived;
the physics doc. No further native change or reinstall needed for the wall test.

**Outside-wall test succeeded, same PID 5468.** Owner supplied a screenshot
of the plank wall and considers all four lanterns covered; gaps between planks
are visible. Two paired-camera segments contact at 7.25 / 7.22 gu from camera,
4.96 / 9.91 gu BEFORE their lights, with near-horizontal, similarly oriented
normals. This is strong evidence for intervening wall collision, unlike the prior
near-source contacts; not proof of exact optical coverage through plank gaps.
Both controls matched, guards/originals passed, no exception, telemetry advanced.
One intervening request safely refused a natural query with a different fifth
argument (`unknown-call-context`); NO extra call, not a miss or guard failure.
7 replay transactions consumed, 5 remain. Evidence in the physics doc.
Owner notes the lamps are wall-mounted: earlier endpoint contacts could indeed
be their mounting wall; fixture identity was not established.

**Latest: open-side comparison, same PID.** Owner confirmed three lanterns visible,
usual test lamp (-10529.755,611.292,-4420.3) hidden. ALL FOUR measured with their
own fresh paired ManyLights endpoints. The three visible sources hit only
0.2054 / 0.1826 / 0.2153 gu before the source; hidden test lamp hits 5.4327 gu
before it. All successful controls/guards/original checks passed; no exception.
Two requests refused another fifth-argument context without an extra call; one
retry each succeeded. 11 replay transactions consumed, only ONE remains.
This is a positive wall/open-view comparison, not a ready optical classifier:
every full segment hit, including visible lamps. Blind hit => blocked is wrong.
No public visibility or plugin/package change; results/screenshot preserved.

**Brazier follow-up:** owner moved in front of a fire bowl. Closest paired source
near (-10520.227,610.455,-4423.204), aligned with player forward, gave NO collision:
count=0, fraction~1, successful control/guards/original checks, exception=0. This
is a clear full-length source path, unlike the three visible cage-lantern hits.
Supports (does not prove) owner's cage-bars explanation. Full result in physics
doc. ALL 12 replay transactions are now consumed in PID 5468; no more replay in
this process. No restart needed for the next offline step, and none requested.

**Next bounded step:** use these captures plus existing WB/query code to establish
the sphere radius and returned hit identity/filter fields, and design explicit
source/fixture versus intervening-obstacle handling before a public classifier.
Do not guess a universal endpoint cutoff, ask for another equivalent lamp test,
or exhaust the remaining process budget by default. For any justified later live
test use `Start-PhysicsProbe.ps1 -Mode segment -NearLightPosition @(x,y,z)`.
The helper resolves the source in the same fresh camera-paired frame; never reuse
a transient sample index. `-StopBeforeLight` remains diagnostic only.
No active request remains; no waiting for owner input. Restart on replay faults.
Collision outcome is not yet optical visibility. Current SDF classifications remain
experimental/default-off; do not declare the open occlusion problem solved.

## Earlier: camera compatibility PR merged; research preserved

Owner merged GitHub PR #2 (Moon-yungg, head `40f4d0d`, remote merge `9ce0b67`).
Local merge `e09cc60` incorporates it without losing our five local research
commits. Combined managed Release build: zero warnings/errors; 73/73 tests pass.
No installed ASI, package or published release was replaced. No push performed.

This is NOT an RTTI-name camera resolver: the camera vtable lacks the usual
MSVC locator (independently checked). It enables automatic direct-camera
relocation using the existing RIP load and two vtable-slot fingerprints.
Offline review reproduced the exact current profile and also resolved preserved
EXEs 1.0.0.2850 and 1.0.0.2949. Unknown builds still lose NativeCapture and
EngineLights; layout/freshness validation remains. No live-game validation of
the new automatic path. Minor review follow-up: the new NativeCapture-null
assertion starts with null; seed a valid native contract to test its removal.

The later physics-first checkpoint above supersedes the object observation below;
merging this contribution does not authorize a new release or broad discovery.

## World Builder reuse / update recovery — 2026-09-24, Codex

Owner broadened the investigation beyond switching lights: examine World Builder
for unresolved occlusion/sky and alternative recovery of working telemetry too.
Read **docs/WORLD_BUILDER_RESEARCH.md** first. Pinned full source clone is under
`external/crimson-desert-world-builder`, commit `ee1f05a3ad1a61cd4aee66946155d0315fdc14e7`.

Concrete findings: native scene create/enable and server gimmick paths, plus
physics sphere sweeps for ground placement. The latter is a new route around
our SDF false clears, NOT an already validated optical visibility solution.
Public C/HTTP APIs only manage WB-owned objects; no ready-made lamp switch/RGB
or all-world object API. Our exact known fire-lamp prefab occurs in its tables.
Keep camera-paired ManyLights; WB's camera pose/heap ranking is not a replacement.

Offline current patch 2.03.02 / EXE 1.0.0.2976 / build 25477059: 17 literal
patterns checked, 15 unique, 1 absent on disk, and 15 references agreeing on one
world global. Named create/ray/shape entry points also recovered; CastShape has
two candidates. See the report for exact RVAs, limits and private evidence paths.
New `scripts/Inspect-WorldBuilderAnchors.py` is read-only; 10 targeted tests pass.
No third-party binary built/installed, live write, product build or release change.

**Next bounded step:** prepare default-off observation-only capture at the native
scene create/enable boundary for our known lamp; validate instructions/context
before arming. Recover current object/child lifetime before a reversible write.
Physics query validation is a separate next avenue, not silently enabled now.
Do not ask for another broad scan or install a second ASI. The earlier preset
read and post-filter modulation remain alternatives, not mandatory next actions.

# Earlier light-control / preset checkpoint — 2026-09-24, Codex

Owner now wants to investigate switching/dimming game lights for a music light
show. The earlier release STOP below was superseded by the owner's explicit
publication instruction; preserve the released package and all historical crash
evidence. Do not resume release/crash work implicitly.

Read `docs/LIGHT_CONTROL_RESEARCH.md`. Old PIX export was reused to prove the
filter-output -> light-tree binding (resources 213 -> 217 -> tree 248). It gives
a precise candidate for a per-lamp RGB intervention; no control experiment or
live write has run. Physical Source evidence survives in the old fire handover.
Current RTTI lookup was repaired for renamed `.xdata`, with offline tests.
Two bounded current-process position scans were incomplete, NOT negative.
The owner closed PID 13528. No hook was armed and no live write ran.
Owner clarification: use PIX as a source of captured data, NOT a scene-replay
project. Stop replay work; the separate C++ build was terminated, its files and
the original capture/export preserved. Baseline replay did run but both its
output and the capture's stored original have HUD over a black scene (already
noted in the historical handover). No visual light-control result follows.
Owner then recalled the `Let There Be Light` preset route; inspect that upstream
route before implementing persistent render modulation. Current loader and
owner paths are relocated OFFLINE: owner `module+0x6C8E9D0`, independent parent
`module+0x6C8E978` -> **+0x778** (old +0x760 is obsolete). Details/evidence in
`LIGHT_CONTROL_RESEARCH.md`. Next owner action: start/load the game. Then one
bounded read-only comparison of both paths and named preset tables; no toggles,
heap scan or writes. Existing direct consumers are Nit/material, NOT a proven
Lumen/lamp path. The two paths are not yet live validated on this build.
No production plugin/package changes; post-filter modulation remains fallback.

Targeted PIX re-read for control (no replay): event 779 `InjectEmitterLodLightCS`
really multiplies RGB by `_lodLightScale`, reads named 80-byte `LodLightInfo`
entries, and writes unfiltered resource 213. Event 685 uploads 14 such entries
from UPLOAD resource 107 +86944 to 15397. Useful earlier alternative, NOT proof
of controlling the nearby fire or every light. Details/caveats and the other
named candidate fields are in `LIGHT_CONTROL_RESEARCH.md`; post-filter remains
the bounded first proof. No new build, hook or live write in this check.

# Historical 2.1.14 crash A/B and deployment correction — 2026-09-23, Codex

The owner disabled only `CrimsonDesertTelemetry` in DMM, launched the same save,
and reported that it loaded and ran. DMM activity records the ASI disabled at
22:20:33 and the game launched at 22:20:39; the ASI file was absent from
`bin64` when checked. The owner then closed the game. This strengthens the ASI
association but a single A/B run does not isolate the native GPU path from a
mixed installation or a transient driver failure.

The owner correctly notes that earlier private builds ran before the Error-6
bootstrap change. Keep that temporal association in the differential diagnosis.
The exact diff only made the host log and NUL stdin inheritable for `CreateProcessW`;
it did not edit D3D12 command generation, and `bInheritHandles` was already true
before the change. An indirect startup/timing effect is possible, not established.
If a clean same-build retry still hangs, compare an otherwise identical build
with only this bootstrap change reverted before opening broad GPU research.

DMM's library held the correct 2.1.14 `crimson-desert-telemetry.deps.cfg` but
`bin64` retained the 2.1.11-rc.2 copy. With the game closed, the exact old file
was backed up to
`artifacts/deployment-backups/20260923-2230-stale-deps/crimson-desert-telemetry.deps.cfg`
and only `bin64/crimson-desert-telemetry.deps.cfg` was replaced with the exact
2.1.14 package file. The target hash now matches the package
(`AD78244EFB69E454E4709C395E74E0B38924DFC3AE4ACC8D2A2C40E17307BCA7`).
Do not infer that this fixes the GPU hang: the .NET host started with the old
CFG. **Next action:** owner re-enables the same 2.1.14 ASI in DMM but does not
start the game yet. Verify all six deployed runtime hashes against the immutable
ZIP, then ask for one same-save retest. If it hangs again, stop repeating and
diagnose the native D3D12 path offline. No public release until resolved.

# Earlier 2.1.14 live crash — release STOPPED, 2026-09-23, Codex

The owner installed the exact 2.1.14 ZIP via DMM and the game crashed while
loading a save at 22:09:51. DMM reported minidump exception `0x887A0006` in
`kernelbase.dll+0xC41CA`; Microsoft identifies that HRESULT as
`DXGI_ERROR_DEVICE_HUNG`, which can arise from invalid GPU commands and does
not by itself prove bad hardware. The owner confirmed that game audio kept
running. Windows System log has an `nvlddmkm` event 153 at 22:09:46. DMM's
headline "not a mod" is not an ASI-plugin exoneration; its own panel listed
`CrimsonDesertTelemetry` as injected. Native log shows the exact ManyLights
detour installed, first playable-world signal received, and recurring capture
armed. Overlay reached D3D12/SDR ready. There was no fatal detail in host,
native, overlay or bootstrap logs. The game's crash log is
`C:\Users\fabia\AppData\Local\Pearl Abyss\log\Launcher_2026_09_23_22_08_14_8272.log`;
dump is `C:\Users\fabia\AppData\Local\Pearl Abyss\DumpCache\reports\39e5f532-15dc-49f4-9b1e-b7d1c443b31c.dmp`.

Five of the six deployed runtime files matched the exact ZIP. DMM left an old
`crimson-desert-telemetry.deps.cfg` from 2.1.11-rc.2 instead of the 2.1.14
companion; the host did start, so this is a real packaging/deployment defect
but not established as the GPU-hang cause. Do not publish 2.1.14. Keep its ZIP
immutable. Before attributing blame to game, driver or plugin, run the same
save with the Telemetry **ASI plugin disabled in DMM** (data mods are a separate
category). Ask the owner for the result, then compare one controlled plugin-on
run only if justified. Do not wait/poll between owner actions.

DMM called Telemetry "no log" even though four component logs existed beside
the ASI. Its search rule is unproven; the local DMM binary was inspected but
source was unavailable. New un-packaged source mirrors bootstrap messages to
the conventional `CrimsonDesertTelemetry.log` while preserving
`.bootstrap.log`. Targeted native build and no-parent-stdin bootstrap smoke
passed; the smoke checks that the primary log is actually written. This change
is **not** in immutable 2.1.14 and is not evidence about the GPU hang. Only
package it in a later version after the crash investigation.

# Earlier 2.1.14 candidate checkpoint — superseded by live crash

**Use only the latest candidate**:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.14-ModManagers.zip`, SHA-256
`C325B987FDE681C7516C9F298D6D8E1939195BD7AE5A624C6C68A73CB0898F1A`.
Its package README and repository README lead with the public Crimson Desert
patch version **2.03.02**, confirmed against Pearl Abyss's 2026-09-23 patch
notes (<https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=133>).
EXE file version 1.0.0.2976 and Steam build ID 25477059 are only technical
identifiers. The installed EXE hash matches the exact guarded build profile.
The owner correctly distinguished the public patch version from EXE metadata;
do not call 1.0.0.2976 the game version again.

Package self-test and exact ZIP/expanded-payload equality passed; final
2.1.14 expanded package passed the bootstrap smoke with no parent stdin.
30/30 native tests and managed tests passed on the same code. No new in-game
run has happened yet. Earlier immutable local candidates 2.1.12 and 2.1.13
were never installed or published; do **not** use them for the one live test.
Next action is DMM install of exactly 2.1.14 while the game is closed, followed
by one live integrated check. End the turn at user action; no polling.

# Earlier 2.1.12 candidate checkpoint — superseded by 2.1.14

The owner chose one production ZIP, keeping the narrow per-light geometric
visibility path compiled but marking it EXPERIMENTAL and setting
`SourceVisibility.Enabled=0` in the shipped INI. A live view in front of a shed
had falsely labelled lights behind solid wood `SOURCE VISIBLE`; do not promote
that classifier or include it in release acceptance. Normal raw/EMA ManyLights,
ambient, HTTP/WebSocket and both HUDs remain enabled. Published 2.1.11 is untouched.

Nexus user buck0021 reported only "error 6" and a speculative WebView diagnosis.
The actual source and log have not been supplied, so the cause is unconfirmed.
The ASI bootstrap did violate `STARTF_USESTDHANDLES`: it passed a non-inheritable
host-log handle and the game's possibly absent stdin to `CreateProcessW`. The
new code opens inheritable host-log and NUL-input handles, closes them after
launch, and falls back to no redirection if NUL cannot be opened. The boot
smoke now clears its own stdin to model a GUI process. This is a concrete
hardening fix, not proof that the external report is resolved. A comment already
requests the user's actual error location and bootstrap log; do not claim WebView
is a requirement.

The exact build 25477059 profile was promoted to `locally-validated` based on
the prior all-on live run plus subsequent HUD captures after camp-to-shed player
movement: position and yaw changed with the scene, native camera/ManyLights
remained live, and ambient values were populated. Exact EXE hash is
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`;
`allowAutomaticCompatibility=false`, so other EXEs still fail closed. The
separate per-light visibility verdict is not part of this validation claim.

The immutable candidate ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-ModManagers.zip`, SHA-256
`B37EE43EEE85E9DA4E71852CC29258E37B1D242D557D72FF3A850AF3BAC42016`.
It is `CDT_RESEARCH=OFF`, targets Steam build 25477059, and contains the fixed
bootstrap and default-off experimental INI. Package self-test passed; 30/30
native CTests passed; managed tests passed after adapting the private-candidate
gate test to an in-memory research fixture; the exact expanded ZIP passed the
bootstrap smoke with no parent stdin and live HTTP health/schema/smoothed route.
These are offline/host tests, not the owner's new game test.

**Next step:** with the game closed, install exactly this ZIP through DMM; start
and load one save. When the owner reports in-game, run one bounded integrated
check (`scripts/Check-IntegratedTelemetry.ps1`) and inspect the status/HUD and
bootstrap log. If it passes, finish main README/release ledger, commit, and
publish to GitHub and Nexus. Never wait/poll for the owner between turns; and
never overwrite either published 2.1.11 or this candidate ZIP.

# Build 25477059 integrated.3 live check and integrated.4 candidate — 2026-09-23, Codex

The owner installed `integrated.3` and sent a screenshot showing LIVE player,
camera, native light radar and fullscreen rendered lights. The bounded one-pass
`scripts/Check-IntegratedTelemetry.ps1` report is
`artifacts/integrated-check-20260923-205124.json`: health `playing`, exact
private compatibility `research-exact`, player/camera/orientation and authored
plus rendered lights available, smoothed groups available, all three WebSocket
connections received. The ambient HTTP sample was available, but its WebSocket
sample and most subsequent samples were unavailable. All 11–15 current rendered
light sources had visibility `unknown/waiting-for-volume`, never fabricated
clear/blocked. This was one all-on run, not a series of isolated feature tests.

Ambient intermittency had a precise cache bug: `SkyAmbientReader.Capture`
decoded a cached unchanged sample with the old default producer RVA instead of
the build-specific `expectedProducerRva`. It reset the mapping and reported
`bridge-invalid` followed by `bridge-missing` during its retry interval. Fixed
in source and covered by a two-read relocated-producer mapping regression test.

The SDF visibility acquisition was read *without changing game memory* using
the preserved exact-ASI state-inspection script, retargeted in-memory to the
installed ASI hash. Evidence is `artifacts/sdf-state-build25477059-20260923-2055.json`.
Three stable readings showed `phase=Failed`, reason
`list-reset-before-confirmed-submit`, no completed readback, and the last
contexts at loading frames 7/8 with zero camera. The SDF path had armed before
the managed host's playable-world signal, while ManyLights itself already
waited for that signal. The change delays SDF interception until the same
signal. It does **not** relax list/fence/identity guards or claim that the
remaining GPU-list behavior is validated; a new live check is required.

The next immutable private DMM ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.4-ModManagers.zip`
(SHA256 `11634393F778FF2A3C0BA6F57CF7BE9CA04488A6970945529C7339D54DFC9163`).
`CDT_RESEARCH=OFF`, exact EXE hash, all current product switches on, profile
still `research`. Managed tests and all 30 native CTests passed; package
self-test passed. `.4` is built but **not installed or live validated**. The
game was still open at last check with `.3`. Close it before replacing via DMM;
after restart, run one bounded integrated report and inspect source-visibility
reasons. If still unknown, inspect the exact ASI's SDF phase once, not another
blind toggle series. Do not poll/wait for the owner; end the turn at user action.

# Build 25477059 private all-on startup correction — 2026-09-23, Codex

The owner installed `integrated.2` via DMM and started the game. Screenshot
showed both HUDs but `WAITING FOR DATA` and status `This game build needs an
update / Static position is not validated for this build.` Read-only health
confirmed `unsupported-build` with that exact error. Native log confirmed
the exact-build ManyLights detour and sky hook installed, but the managed host
stopped before its first sample. This was a double confidence gate: the private
exact-hash compatibility resolver accepted the research profile, then
`StaticPositionProbe.Resolve` rejected its `candidate` anchors (and the
orientation reader would have done likewise). No evidence here that the
ManyLights/Ambient/visibility paths themselves failed.

The corrected immutable ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.3-ModManagers.zip`
(SHA256 `FFA331A8895EF32365E08FF306769191BBD30B53C85C1617518C8D418250F731`).
Only `research-exact` may read candidate player anchors; public `tested` and
`automatic` modes keep the prior trusted-anchor rule. RIP uniqueness, profile,
pointer-chain, RTTI and per-sample plausibility guards remain. Managed tests,
including the candidate gate, and package self-test passed. The `.3` package
has **not yet been installed or live-tested**. Game was still open at last
check. Next step: close game, replace `.2` with `.3` through DMM, start/load,
then run the one-pass `scripts/Check-IntegratedTelemetry.ps1` and inspect the
HUD. Do not poll/wait for user action; end turn and resume on message. Older
`.1` and `.2` packages are retained but must not be installed for this test.

# Build 25477059 private all-on integration package — 2026-09-23, Codex

The owner rejected another sequence of single-feature game runs: test all
current **product** features at once. This entry describes the superseded
private ZIP
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.2-ModManagers.zip`
(SHA256 `7E4791625AA50D480CAB0B8AF18A73E7B334CF110549A61FB86D2BB1A8DE0703`).
Immutable `integrated.1` preceded the build-specific Ambient reader correction;
do not install `.1`. Nothing has been installed or publicly released yet.

This exact-hash, `research`-status package has `CDT_RESEARCH=OFF` (current
product paths, not mutually exclusive console/probe modes). INI enables
Server, Notifications, Lights, ManyLights, Ambient, SourceVisibility, Overlay
and LightOverlay. The bootstrap passes `--private-exact-build` to the managed
host only because its native contract is explicitly private/research. The
ordinary CLI resolver still rejects the research profile. The host labels
compatibility `research-exact`; health `supportedBuild` remains null, never
claims public support. Game EXE hash and native hook/context bytes must match.
The Ambient reader now checks the per-build sky producer RVA from the profile;
otherwise the relocated hook would have been falsely reported as invalid.

Managed tests passed. Native CTest passed 30/30; offline preflight matched the
new ManyLights and Ambient callsites against the installed EXE. The package
self-test passed, and all eight product switches above were confirmed `1` in
the nine-file ZIP. Those are build/offline checks, not a live all-feature result.
One bounded post-install check is `scripts/Check-IntegratedTelemetry.ps1`,
which reads health, player/camera, raw/EMA lights, visibility, ambient and the
three WebSocket streams into one fresh report under `artifacts/`. It makes no
game changes and does not wait for the owner. The owner should also visually
confirm both HUDs/status; the report cannot judge presentation.

**One next step:** install integrated `.2` via DMM while the game is closed,
start/load once, then run the one-pass check when the owner reports in-game.
Separate genuine failures by their status/reason; do not restart broad light
research or claim everything works merely because the INI enabled it. End the
turn whenever user action is needed; no polling or waiting.

# Build 25477059 ManyLights recovery — controlled fire AN/AUS/AN, 2026-09-23, Codex

Interaction rule from the owner: never keep a turn/session running while waiting
for an in-game action or reply. End the turn; the owner resumes with `AN`,
`AUS`, `weiter`, or another message. No polling or sleep in the meantime.

The owner completed the isolated fire AN/AUS/AN with the portable lantern
stowed: visible illumination vanished on AUS and returned on AN. The full
authored-light vector stayed unchanged at three progressing frames; do not
repeat that branch. The previously proven filtered ManyLights path now also
passed a controlled AN/AUS/AN on the new game build (details below).
The owner explicitly wants future rendered-light checks to reuse this secured
ManyLights path, not to infer status from another array. Its exact build guards
and live response must be revalidated after updates; failure means unavailable
until diagnosed, not permission to guess a replacement.

The exact-build `25477059` definition is deliberately `research`, so the
managed public compatibility resolver still rejects it. A separate explicit
`-UnvalidatedDiagnostic` package path compiles a hash- and byte-guarded native
capture only, with server, ambient, visibility, notifications and HUDs OFF.
The ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-diagnostic.2-ModManagers.zip`
(SHA256 `FDE598D02DB617C194C3A0320097C12217D574EA3E844B21FE4467CE67CB9C4E`).
Diagnostic `.1` is an earlier immutable build; use `.2`. `.2` was installed
privately via DMM and run in the game; nothing was released publicly. Managed
tests passed; native CTest 30/30 passed;
native file preflight matched all ManyLights hook/caller bytes and both ambient
hook contexts against the current EXE without accessing the game process.
Ambient/spatial source locations remain static candidates, not runtime-validated.

The diagnostic has `Server.Enabled=0`, so the native detour initially remained
`bridge-waiting` despite successfully installing. Once the owner confirmed the
playable world, Codex used the existing
`NativeCaptureReadySignal.TrySet(processId)` one-shot gate; no plugin change or
rebuild was needed. That manual signal must be repeated after a restart of this
private package. The running process was PID 19204. The exact-build,
read-only `scripts/Capture-PrivateManyLights.ps1` recorded three fresh, advancing
bridge samples under `artifacts/light-research/`:

| Owner state | File suffix | Sequence / frame | Nearby count | Light within 0.5 gu of known fire `(-10529.755,611.292,-4420.300)` |
|---|---|---:|---:|---|
| AN | `201348-AN.json` | 232 / 25201 | 8 | 1 at `(-10529.738,611.467,-4420.300)`, sample #31 |
| AUS | `201437-AUS.json` | 979 / 27817 | 7 | 0 |
| AN | `201530-AN.json` | 1785 / 30650 | 8 | 1 at `(-10529.749,611.467,-4420.301)`, sample #24 |

The paired camera X/Z remained identical to 0.001 gu; captures were 15–47 ms
old. The sample index is transient, while the world-space location and the
AN/AUS/AN response identify the fire contribution. This validates the native
filtered ManyLights path for this fire on build 25477059, **not** the whole
product, ambient, source visibility, all lights, or update stability. Keep the
build definition in `research` status; do not promote it or publish `.2`.

**One next step:** validate the remaining product paths on this build in
separate bounded controls (starting with ambient/spatial and player movement),
then consider an exact-build production package. Preserve these three raw
captures and the old release package. When user action is needed, end the turn;
do not poll or wait.

# Game update 25477059 — checkpoint, 2026-09-23, Codex

Steam briefly installed build `25455892` and then replaced it with `25477059`.
The owner confirmed the second update was finished. Both EXEs were preserved
before further Steam changes:

| Build | Private backup | EXE identity |
|---|---|---|
| 25455892 (superseded) | `artifacts/recovery/20260923-190119-unknown-a9e5ca20/` | `1.0.0.2949`, SHA256 `A9E5CA2076367E7995B81A3A4803F7259AB7DAC3415DF8EA949043EF635A174A` |
| **25477059 (installed)** | `artifacts/recovery/20260923-191056-build-25477059/` | `1.0.0.2976`, 384521624 bytes, SHA256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7` |

`dotnet run --project src/CrimsonDesertTelemetry.Cli -c Release -- check-update
'C:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'` reports
`anchor-check-failed` against the old definition. It is read-only and did not
enable any hooks. Do **not** install or advertise compatibility yet. No ASI was
installed. The existing untracked analysis files were left untouched.

Bounded old/new EXE disassembly gives *static candidates*, not runtime proof:

| Path | Candidate RVA in 25477059 | Evidence still needed |
|---|---|---|
| ProcessManyLights entry | `0x3DA8210` | Live owner/wrapper/resource and scene pairing |
| Counter binder / filtered binder | `0x3DA96CC` / `0x3DA9790` | Same R15/R12 roles seen statically; virtual binder slot changed `+0x4D0` → `+0x4E8` |
| ManyLights hook | `0x3DA97DA` | Instruction boundary verified; dispatch slot `+0x328` → `+0x338`, next call `+0x208` → `+0x218`; active shader still unverified |
| Ambient source A / hook A | `0x393100F` / `0x3931317` | Paired source-to-hook structure and Dispatch(1,1,1) seen; output layout/live values unverified |
| Ambient source B / hook B (diagnostic only) | `0x393424B` / `0x3934313` | Same caveat; do not enable B as product path |
| Spatial dispatch / exposure return | `0x389C000` / `0x362595A` | Unique longer dispatch-body prefix and matching consumer shape; live ownership/resource checks still required |

Read-only runtime spot check on PID 11280 (started 19:22; no ASI): the relocated
scene global `+0x6C8CF30` pointed to a live scene object with vtable RVA
`0x5D20718`. `scene+0x428` pointed to a 2816-byte-shaped scene buffer: 3840×2160
and camera XYZ about `(-10027.9, 522.4, -4434.8)`. The relocated static player
slot `+0x6D77858` gave about `(-10030.1, 519.3, -4429.3)`; proximity is plausible,
not a movement control. The scene frame counter `+0x2C8` advanced from `0x2580`
to `0x3CB7`, so the scene was progressing. **The old authored-light pointer at
`scene+0xF08` was null.** `scene+0xF10` was nonnull but its target did not have
the expected vector at `+0x10`; do not substitute it by guess. A bounded read-only
scan of only `scene+0x800..0x2000` then found one vector-shaped candidate at
**`scene+0xF90`**. Its descriptor contained 15 records, capacity 18, stride
`0xB8`; all 15 decoded at the old position/color/cone/active/selected offsets.
Eight were active with plausible nearby world coordinates, including two at
exactly `(-10029.832, 520.2973, -4428.764)`, close to the player. The inactive
records had zero positions. This strongly identifies the relocated authored-light
vector. A later controlled fire AN/AUS/AN left this vector unchanged; see below.
With only these candidate
offsets changed in diagnostic definition objects (not game memory; no profile
promoted), the full existing
`EngineCameraReader.Capture` passed basis/projection/scene checks twice, and the
full `EngineLightReader.Capture` returned `available`: 15 source records, 8
within 200 gu, 0 malformed/unsupported, 0 walk races. The eight decoded records
include point and spot sources, plausible linear colors, renderer scales and
directions. These are data/layout checks, not visual source identification.
The old RTTI-guarded player chain failed only at `owner+0x2B8`; a bounded owner
scan found exactly one physics candidate at **`owner+0x2C0`**. With that offset
in the diagnostic definition, the unchanged `PlayerOrientationReader` passed its
RTTI, position agreement and orthonormal basis checks, reporting heading 333.8
degrees. This
is not yet a movement/rotation control.
All addresses in this paragraph except module-relative RVAs are session-specific
and must not become update anchors.

Next: prepare an exact-build, fail-closed **private diagnostic** capture using
the changed native signatures and anchors, then repeat this known-light AN/AUS/AN
through paired ManyLights. Run tests and validate ambient before any promotion.
An EXE-only match does not validate shaders or per-light visibility.

Controlled AN/AUS/AN at the known four-fire-lantern camp, player
`(-10530.401, 609.15674, -4419.537)`, no ASI and no player movement: read-only
authored-vector samples at progressing scene frames 59996/61855/65107 all had
16 records, 15 `recordActive` and 6 `rendererSelected`. No position or status
disappeared and returned with the switched lantern; only two near-player record
positions drifted slightly while their flags and scales stayed constant. Thus
this CPU authored-light vector is **not a validated status source for this fire
lantern**. Do not treat this negative result as refuting the previously measured
ManyLights/rendered-fire path. Next controlled check must use the exact-build
ManyLights capture, not more searches in this array.

Follow-up visual clarification: the owner's *portable lantern* was still on
during that fire-switch series and masked the perceived illumination change.
With the fire on, stowing the portable lantern kept PID 11280 and the player
near the camp; authored slots 11 and 12 both changed from active/selected with
positive renderer scales (13.094/5.238) to inactive/unselected with scale 0.
Their positions had followed the player. This independently identifies those
two contributions as the portable lantern, not the switched fire. Slot 15 was
reused by a different record and must not be treated as a permanent light ID.
The isolated fire AN/AUS/AN was then completed with the portable lantern stowed,
same PID 11280 and stationary player `(-10530.632,609.1567,-4419.819)`.
Progressing scene frames 9331/18291/21721 all had 16 authored records, 14
active and 4 renderer-selected; the entire record set was unchanged between
these samples. The owner reported that the fire illumination visibly vanished
on AUS and returned on AN. This is a valid visual control that the earlier
portable light had obscured. Consequently the authored array is **not** the
fire-light state channel for this lantern; it says nothing negative about the
ManyLights path. Stop testing this array for that fire and move to the exact-build
render capture.

# Current checkpoint — handover to Codex, 2026-09-23, Claude

Nine days with no commits. This replaces four stacked checkpoints from 13–14
September with one statement of where everything actually stands, verified today
rather than recalled. Codex's own 13 September checkpoints stay below as history;
the fire-visibility one still holds the live test that was never run.

## Verified today

| | state |
|---|---|
| Nexus | `2.1.11` is `main`, uploaded 09-13 17:54, `2.1.10` archived. Nothing newer uploaded. |
| Git | `origin/main` == local, nothing unpushed, last commit `b80e22e` on 09-14. |
| Machine | **The mod is not installed.** `bin64` holds only leftover `crimson-desert-telemetry.deps.cfg` and `.runtimeconfig.cfg` — the known DMM removal behaviour, visible again. Game not running. |
| `C:\CrashDumps` | **empty.** WER has been armed for `CrimsonDesert.exe` since 09-13 and the unexplained local crash has not recurred in nine days. |

## The crash is done

`2.1.11` fixes a proven ordering violation: Telemetry installed its HUD swapchain
hooks synchronously inside DXGI's `CreateSwapChainForHwnd`, before Streamline had
associated the new swapchain with its command queue, which is exactly where the
users' `E_ACCESSDENIED` came from. Verified against NVIDIA's published Streamline
v2.11.1 source.

**jimos87 confirmed it externally** on 09-14 09:57: "It is now working for me :D ty"
(RTX 4080 SUPER, 616.92, DMM 2.8.1). That is one user, not the three consecutive
starts the acceptance gate asked for, and his own evidence had shown the failure was
intermittent. WHOLE (RTX 4090) never reported back. Public wording says one of two
confirmed and should not be tightened further without WHOLE.

Two things remain open and are recorded rather than solved: nobody ever answered how
many monitors the affected users run, and the owner's own 09-13 19:20 crash has no
dump and no explanation. Both of the owner's local crashes happened on a two-monitor
extended setup during an output change — a configuration the users may not share, and
a different signature (runtime, `+0x3D05303`) from theirs (startup, `+0x3D0FEA6`).
Do not merge those two.

## Goal 1 is done; goal 2 is explained, not solved

**Ambient / sky occlusion works and ships.** Measured 0.3916 on open ground, 0.0476
in an open-fronted stable, and **exactly 0** in a closed interior. Its one product
consequence: `sky × visibility` drives a lamp to black indoors, so the consumer needs
a perceptual curve with a floor. That is not optional.

**Per-light occlusion: the blocker is now explained.** See
[SDF_BAND_LIMIT.md](SDF_BAND_LIMIT.md). The field stores a thin signed band around
surfaces, not solid interiors: probing down through ground the player stands on, the
negative region is 0.6 gu wide (room) and 0.3 gu (camp), reaching −0.181 and −0.092,
and immediately below it the value returns to **+0.53027**, the positive clamp. So
"inside" is not a persistent state, and a ray registers a nonpositive value only when
it crosses that one-to-two-cell band in a way trilinear interpolation preserves —
which the room case proves it often does not.

**Therefore `value <= 0 along the segment` cannot be made reliable here.** Not by
smaller steps, denser sampling or a larger tolerance. Tracing, addressing and
acquisition are all fine; offline traces reproduce the native `closestApproach`
values exactly and the geometry is present as negative texels.

Two repairs are ruled out with evidence, not argument:

- **Counting solid texels beside the path** — refuted on the controlled same-source
  A/B: blocked pose touched 4, the three exposed poses touched 3. A room always has
  walls and a floor nearby.
- **Raising the hit tolerance** — it looks tempting because within the traced range
  the room separates by a factor of thirty (blocked 0.005/0.016, clear 0.529/0.517 at
  the clamp), but that is fitted to one room. Note the regression table's 0.160 and
  0.125 for the clear controls are **complete-path** minima landing on the lights' own
  housings inside the end margin; `left-3.0` has length 7.733 with its minimum at
  exactly 7.733.

Still worth fixing separately: the forced 0.05-gu minimum step skipped a genuine
negative interval on `near-box-b` (dense sampling finds −0.0073 at t = 13.25 against a
traced verdict of clear). A sphere trace is only valid stepping by at most the sampled
distance. It recovers that one case and no other.

## The decision that blocks everything

A swept query — "does anything come within radius r of the segment" — is the only
remaining candidate on this data, because it uses the distance rather than relying on
the sign surviving interpolation. Checked at the room minima: both blocked paths have
their nearest surface to the side, not the floor, so it would not merely be detecting
ground. It is untested and inherently conservative.

**Before building it, the product question should be answered:** the consumer is a set
of Hue lamps, not a renderer. Does it need per-source geometric truth, or roughly how
much light reaches the player and from where — which the working ambient feed already
answers? The user has not yet been asked this. Asking is cheaper than building for
either answer.

## Untested and waiting

`v2.1.12-fire-visibility.2` is built, scanned (ASI 4/71, ZIP 0/68) and **never
installed**. Its exact next test is in Codex's own checkpoint below and has not been
performed: one known fire, `visible -> blocked -> visible`, F11 in show-blocked mode
first, then rotate the camera without moving to confirm direction does not change the
verdict. Do not claim candles, lamps or the all-source requirement from one pass.

## Housekeeping, recorded not done

26 untracked throwaway files sit in the repository root (`capture_aba*.py`,
`parse_aba*.py`, `vt_*.py`, `test.cpp`, `debug_test.cpp`, `set_ini.py`, `temp_zip/`)
plus `native/.../src/spatial_readback_stub.cpp`, which is in no CMake list. They are
not mine and may hold the only copy of an analysis, so they were left alone.

## One next step

Ask the user how much per-light fidelity the lighting product actually needs. If the
answer is "geometric truth per source", the swept query needs a live controlled test
with `r` chosen on physical grounds. If "a conservative approximation is fine", the
existing narrow fire path plus ambient may already suffice and the remaining work is
in the consumer, not the renderer.

---

# Previous checkpoint — narrow rendered/fire visibility restored, 2026-09-13 late, Codex

## User decision and scope

The user ended the feature pause after 2.1.11 publication and asked for the earlier
live-working fire visibility behavior back **without new renderer research**. This
checkpoint restores only that small production path. Do not treat it as completion
of the all-light/player goal, and do not replace it with the broader research bridge
unless a later controlled failure requires that work.

Public 2.1.11 remains unchanged on Nexus. A matching GitHub release now also exists:
<https://github.com/fabianviol/CrimsonDesertTelemetry/releases/tag/v2.1.11>.
Its tag points to stable commit `a8c5ed87925a688532b50b00102d90a46ee69d46`
and its uploaded asset is the exact existing stable ZIP, SHA-256
`CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`.
The new visibility candidate below has **not** been published there or on Nexus.

## Concrete production restoration

`CDT_RESEARCH=OFF` now:

- compiles `sdf_acquire.cpp`, which retains one fenced current R16 SDF volume;
- links `sdf_visibility.cpp` as `cdt_sdf_fire` with the exact earlier Variant A
  classifier: 0.6 start skip, 1.0 end margin, `max(distance, 0.05)` steps,
  nonpositive sample = blocked, at most 400 iterations;
- classifies only current renderer-selected records in `render_bridge.cpp`, from
  their capture-paired camera position. Camera direction and projection are not
  inputs, so a retained behind-camera record is still eligible;
- reads `[SourceVisibility] Enabled`, publishes additive metadata and restores
  `HideOccluded` plus configurable/default F11 in both light views;
- leaves raw and smoothed records, positions and RGB unchanged. Unknown/stale
  records remain displayed even when hiding is active.

The host creates the broad managed player/all-known-source query mapping only when
the ON ASI passes its private `--research-source-visibility` flag. OFF does not run
that client or compile the native result bridge, history, diagnostic series, dump
or trace infrastructure.
This is the same architectural family that previously produced working fire
verdicts; candles, lamps, authored-only sources and a complete 360-degree registry
are not claimed.

## Exact candidate and verification

Private package:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-fire-visibility.2-ModManagers.zip`

Expanded folder:

`artifacts/mod-manager/v2.1.12-fire-visibility.2-20260913-235738-037-8b9b65b8/CrimsonDesertTelemetry`

| Exact file | SHA-256 | VirusTotal | Microsoft Defender |
| --- | --- | --- | --- |
| ASI | `03989712E34BF418BB326A835A998CAC206863775D8EB182BA98A2C55ADAC1D7` | **4/71**: CrowdStrike, Cynet, McAfeeD, Microsoft | No matching detection |
| ZIP | `B2CF14D58F9F31F6238C617D77B66499CE13BCF3D88A55C12170CF1CA719C0BD` | **0/68** | No matching detection |

The VT result is equal to/better than the immediate v2.1.9 predecessor (4/71) and
earlier narrow visibility candidates (5/70–5/71, ZIP 0/67–0/68). It does **not**
reproduce the later broad research-bridge regression (ASI 11/71, ZIP 7/68).
This directly answers the user's AV question: restoring the narrow path did not
damage the measured scan profile. The known deterministic Bitdefender family came
from commit `6937fa9`'s bounded repeated diagnostic readback series, which remains
compiled out of OFF; see `ANTIVIRUS_FINDINGS.md`.

Verification completed against this production build:

- all **30/30 native CTests** pass;
- all **71/71 managed tests** pass;
- the new `release-fire-visibility` test covers first-volume waiting, clear/blocked
  fields, sources in front of and behind the camera, disabling, and byte-for-byte
  raw scene/light/counter preservation;
- the production package validator accepts all **34** offered settings, rejects
  research-only settings, verifies all nine files and verifies ZIP/staging equality.

## One next step

With the game closed, install the exact private ZIP above cleanly. Use one known
fire and perform `visible -> solid wall/building blocked -> visible`, keeping F11 in
show-blocked mode first so wrong verdicts cannot disappear. Rotate away if useful:
camera direction must not change the verdict while the renderer record remains in
the feed. If that passes, repeat with F11 hiding, record the live result here and
decide whether this limited fire restoration is releasable. Do not claim candles,
lamps or the final all-source product requirement from that one pass.

---

# Previous checkpoint — 2.1.11 PUBLISHED, 2026-09-13 evening, Claude

> Supersedes the "One next step" of the 18:46 checkpoint below, which still asked for
> external validation BEFORE publishing. The user overrode that and released.

## What changed after 18:46

**`v2.1.11` is live on Nexus.** Published 19:54, verified through the authenticated
API: `2.1.11 main 09/13/2026 19:54:00 id=38521561693168`, with **2.1.10 archived** at
the user's explicit instruction and the page version moved. Changelog appended from
`docs/releases/v2.1.11.md`. The exact ZIP `CB7DD68F…5052` and ASI `06EE760E…1B63`
were re-hashed from the published archive itself before upload, not trusted from the
documentation.

**The publish tool was broken and is fixed (`380eaa9`).** The first attempt died with
a bare 403 on the presigned storage PUT. `tools/nexus/NexusMods.psm1` deliberately
left `Content-Type` unset, with a comment claiming it is not signed. That comment was
wrong. Reading the storage provider's own error body showed
`X-Amz-SignedHeaders=content-disposition;content-md5;content-type;host`, and Cloudflare
R2 rebuilding the canonical request with an empty content-type. Nexus signs for
`application/octet-stream`; `application/zip` is rejected with the same 403. The lesson
worth keeping: a presigned PUT's 403 body carries the whole diagnosis, and the module
was discarding it.

**`docs/NEXUS_DESCRIPTION.bbcode` corrected in two places.** Its "Validation and
integrity" section still said the package needed the live second-monitor/HDR
replacement test -- that ran today and passed, so it now states the live validation
actually performed and leaves external confirmation as the only open item. And "What
the measurements mean" now says the linear HDR values are unclamped, routinely exceed
1.0 and are not 0-255 colours; the owner asked exactly that question, so users will.
Everything else of Codex's 2.1.11 rewrite is untouched, including the schema 1.5
statement, which matches `lightsSchemaVersion = "1.5"` in Program.cs.

## New evidence: a local crash the fix did NOT prevent

At about 19:20 the game ended after 2 h 15 min with 2.1.11 installed, following an
HDR on -> off change. **Telemetry's replacement path had completed successfully**
moments earlier -- `overlay.log` ends at 19:19:51 with the full sequence including
2.1.11's new `Existing HUD swapchain released for replacement.` line, then
`Overlay ready`. No `CreateSwapChainForHwnd` failure, no `E_ACCESSDENIED`, no
exception in any Telemetry log. Logs preserved under
`artifacts/crash-reports/local-20260913-1919-hdr-off/` with a CONTEXT.txt stating
what they do and do not establish.

**There is no dump for it.** The game's own handler did not write `last_crash.dmp`,
and Windows Error Reporting was configured per-application only, without
`CrimsonDesert.exe`. So the process vanished without either mechanism catching it,
which is itself a difference from the 16:17 crash that did produce one.

**WER is now armed** (user ran it elevated, verified):
`HKLM\...\Windows Error Reporting\LocalDumps\CrimsonDesert.exe`, DumpFolder
`C:\CrashDumps`, DumpType 2 (full), DumpCount 10; the folder exists. The next
unhandled crash produces a full dump automatically. This does not cover a hard
termination or a driver-level TDR.

**Do not merge this with the users' crash.** Codex's earlier warning was right and I
briefly violated it. The user then raised the decisive point: their machine now has a
**two-monitor extended setup with HDR on one screen**, and reaching the Windows
display settings requires leaving the game. Both local crashes happened during an
output change in that configuration. The users' crashes are at **startup**, with a
different signature. Whether the two overlap is unknown -- nobody has asked the users
how many monitors they run.

Retests tonight after the crash, all passing, all with 2.1.11: HDR switch at the main
menu (no HUD swapchain to release, so the new path did not even run), then HDR switch
in game (the release path did run, logged cleanly, no crash). So the crash is
**intermittent**, matching jimos87's report of one good run followed by a bad one. The
only obvious difference in the crashing run is uptime: 2 h 15 min versus ~20 min.

## Replies drafted for both Nexus users

Full text given to the user for posting, not yet posted. Both credit the specific
evidence that made the diagnosis possible -- WHOLE's Streamline log and exception
offset, jimos87's minidump and his observation of intermittency -- state plainly that
the cause was ours, and explicitly do NOT claim the crash is fixed for them. Both ask
for three consecutive starts and, for the first time, **how many monitors they run and
whether HDR is active**. jimos87's additionally covers the DMM leftover cleanup.

## Verified machine state

Installed and running 2.1.11. Five of the six deployed files match the published ZIP
byte for byte; only `crimson-desert-telemetry.deps.cfg` differs, carrying older
version labels with identical runtime assets -- the known DMM behaviour, now measured
rather than assumed.

## One next step

Post the two replies and wait for an affected user. Until one of them reports three
clean starts, 2.1.11 is a diagnosed and locally verified fix, not a closed report --
that wording is in the description and the changelog and should not be tightened
without their evidence. If a local crash recurs, `C:\CrashDumps` will now hold a full
dump: compare its exception offset and loaded modules against the two known cases
(`+0x3D0FEA6` external startup, `+0x3D05303` local runtime) before touching code.

---

# Previous checkpoint — Nexus crash compatibility, 2026-09-13 18:46, Codex

> This section supersedes the older source-visibility checkpoint below. The user has
> explicitly paused all occlusion research until the current Nexus crashes are fixed.

## Current external evidence

The Nexus posts page contained nine comments when checked on 2026-09-13. In
chronological order:

- jimos87, 2026-09-10 11:40: initial crash report.
- WHOLE, 2026-09-12 06:45: 2.0.2 package/deployment report and graphics-feature
  crash matrix; backend-only operation worked.
- fabianviol, 2026-09-12 10:51: replied to both threads, acknowledged the
  failures and requested native log/crash evidence from WHOLE.
- jimos87, 2026-09-12 17:18: current download still crashed.
- fabianviol, 2026-09-12 19:38: requested a clean 2.1.10 install and exact logs.
- fabianviol, 2026-09-12 19:40: announced 2.1.10 and requested the same controls.
- **WHOLE, 2026-09-13 05:04 (current):** build 25246367, RTX 4090, NVIDIA
  616.92, all six files copied manually, reported overlays off. Streamline
  DLSS/DLSS-RR 2.11.1 logs `linkSwapchainToCmdQueue` /
  `CreateSwapChainForHwnd` failure `0x80070005 (E_ACCESSDENIED)`, followed about
  one second later by `0xC0000005` at `CrimsonDesert.exe+0x3D0FEA6`.
- **jimos87, 2026-09-13 08:49 (current):** 2.1.10 sometimes reaches the game but
  still fails; supplied a DMM support package:
  <https://drive.proton.me/urls/V1AX5VHNV0#sIhaw6yiTPQJ>.

The support ZIP is preserved read-only under
`%TEMP%/nexus3374-support-jimos87-20260913`; no downloaded binary was executed.
Its SHA-256 is
`6062BD48244BE56220C75C14EED06FE55D66070BC0B3A0DF186DE44CA242DBE0`.
It proves DMM 2.8.1, RTX 4080 SUPER, driver 616.92, game build 25246367 and an
intermittent pattern: one enabled run exited normally, while the next enabled run
logged the same Streamline `E_ACCESSDENIED` and crashed.

The minidump proves the loaded Telemetry ASI is the exact public 2.1.10 build:
PE timestamp `0x6AA59B26`, mapped size `0x17C000`, matching local SHA-256
`8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78`.
It also contains Streamline, Steam's `gameoverlayrenderer64.dll`,
`DesertLinkCore_v1.0.7.asi` and `MasterLooter.asi`. The exception is the same
`CrimsonDesert.exe+0x3D0FEA6` reported independently by WHOLE. This is no longer
an unverified stale-file or packaging hypothesis.

The owner, WHOLE and jimos87 all used NVIDIA driver **616.92** in the relevant
tests (RTX 3080, 4090 and 4080 SUPER respectively). Parsing jimos87's exception
context against the exact local game executable (matching PE timestamp
`0x6AA22ABB` and image size
`0x16B0E000`) proves that the faulting instruction is `mov rax, [rcx]` with
`rcx == 0`. The crash thread's retained stack contains CrimsonDesert, `sl.common`,
`sl.reflex`, `sl.interposer` and `nvwgf2umx`, but no Telemetry frame. This supports
the sequence shown by the game log: swapchain creation fails first, then the game
dereferences its missing graphics object. It does not by itself absolve Telemetry
of causing the preceding swapchain failure.

Disassembly of the exact game image now identifies that missing object precisely.
The fault is in the routine beginning at RVA `0x3D0FE00`; it loads the member at
`[rbx+0xA8]` and calls vtable offset `0x120`, which is method slot 36,
`IDXGISwapChain3::GetCurrentBackBufferIndex`. The same member is used earlier with
vtable offset `0x50` and fullscreen arguments, matching
`IDXGISwapChain::SetFullscreenState`. Therefore the access violation is the game's
direct null dereference of its swapchain member after Streamline reported that the
swapchain could not be created/linked. HDR is not implicated: WHOLE reproduced the
same failure in SDR.

The exact shipped `sl.dlss_g.dll` (SHA-256
`DAE3F24A690F3DEE3FBD89BDDE47FBEB6BF480CEE376ECFD0242A50E0BB6E6C6`) was also
disassembled. The `linkSwapchainToCmdQueue` error path at DLL RVAs
`0x4B735`/`0x4BC31` logs the negative HRESULT returned directly by its internal
factory `CreateSwapChain`/`CreateSwapChainForHwnd` call. Combined with the official
v2.11.1 factory order, this places the failure in DLFG's post-create linking work:
the base swapchain has been returned, but the Streamline after-hook has not finished.
That is the precise interval in which 2.1.10 installed five shared swapchain-method
hooks and which 2.1.11 now leaves untouched. The external startup failure remains
distinct from the owner's later runtime display-transition crash until affected-user
tests establish whether both were fixed.

## First concrete defect and narrow fix

`overlay_graphics.cpp` hooked Present/Present1/Resize/SetColorSpace synchronously
inside the real DXGI `CreateSwapChainForHwnd` detour. At that point the inner DXGI
call has returned, but NVIDIA Streamline's outer wrapper has not yet associated the
new swapchain with its command queue. The code therefore modified the swapchain
implementation during Streamline's still-active creation path, exactly before its
`linkSwapchainToCmdQueue` failure.

This ordering is confirmed by NVIDIA's official Streamline **v2.11.1** source
(tag commit `019994e18d256a3e92347888deb527feb7f58bc0`), the exact Streamline version
reported by both users. Its `IDXGIFactory2_CreateSwapChainForHwnd` calls the base
factory first, then invokes Streamline's after-hooks, and only afterward calls
`setupSwapchainProxy`. Telemetry 2.1.10 ran `Track()` while that base call was
returning, so its five swapchain hooks were installed before those two Streamline
steps. The deferred tracking therefore removes a demonstrated ordering violation rather
than merely tuning a timeout after the crash.

The first rc.2 attempt still retained the newly returned swapchain in a pending
`ComPtr`. Microsoft documents that a flip-model HWND can have only one swapchain;
keeping the old one alive can make replacement creation fail. The owner then
reproduced that exact DXGI failure locally at 16:17:07 by switching the game's
output/HDR state for a newly connected second monitor. The game logged
`CreateSwapChainForHwnd failed: -2147024891` twice and crashed with `0xC0000005`
at `CrimsonDesert.exe+0x3D05303`. This is a runtime replacement path and a different
downstream crash routine from the users' startup RVA `0x3D0FEA6`; do not merge the
two acceptance claims.

The local DMM support ZIP and its 17,409,728-byte minidump are preserved under the
Git-ignored `artifacts/crash-reports/local-20260913-161707`; dump SHA-256 is
`290587A6193989B6AE8D8E23A4A7DF71BB7774498D3A9A59F3CF178310D2DF68`.
Disassembly of the local fault shows a null/invalid structure at `mov rsi,[rax+30h]`
inside the game's swapchain/backbuffer rebuild routine, not the external users'
`GetCurrentBackBufferIndex` null call.

The final narrow fix records each pending HWND and, before either hooked DXGI
factory create function runs, releases only Telemetry's pending or active
swapchain/renderer/queue references for that same HWND. It waits for submitted HUD
GPU work before releasing active resources. The worker still waits 500 ms before
installing presentation hooks. No research hook, tracing system or new production
feature was added.

The completed fix is commit `a8c5ed8` (`Release swapchain replacement compatibility
fix`). It is pushed to both `origin/main` and
`origin/codex/streamline-swapchain-fix`; local `main` is at the same commit. The
implementation was prepared in `C:/DEV/CrimsonDesertTelemetry-hotfix`. Paused
source-visibility commit `f750974` is preserved on branch
`codex/source-visibility-paused` and was not included.

Exact final-version package used for the local acceptance test:

- `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-ModManagers.zip`
- production `CDT_RESEARCH=OFF` ASI: 1,276,416 bytes, SHA-256
  `06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`
- ZIP: 800,147 bytes, SHA-256
  `CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`

Build, 71 managed controls, 30 native CTests and package self-test pass. All eight
D3D12 HUD/notification/light-marker SDR/scRGB smoke modes now create a replacement
immediately while the first chain is pending and again after the HUD accepted the
next chain. Microsoft Defender found no threats in either exact 2.1.11 file.
VirusTotal reports 0/68 for the exact ZIP and 4/71 for the exact ASI. The ASI
result is not a regression from public 2.1.10's 5/71 result. Local Defender found
no threat in either exact file.
The source and release documentation are on GitHub. Before telling users that
2.1.11 is available on Nexus, verify the Nexus file entry itself; the last
independently confirmed public Nexus binary in this checkpoint was 2.1.10.
Repeated starts by an affected 616.92 user are still required before calling the
external startup crash fixed because their evidence proves the old failure is
intermittent.

### Local live evidence

On 2026-09-13 at 15:03, the owner launched the exact rc.2 ASI on an RTX 3080 with
NVIDIA driver 616.92. That superseded ASI's SHA-256 was
95D733491FB8C09CCD7D80C5800F87C6379BFD6B178299C1902D6F0F89541EEF. The
overlay log records the intended sequence:

```text
Waiting for the game's D3D12 swapchain.
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

After five minutes the game process was still responsive. Native light capture
was armed after the playable-world signal, recurring rendered-light capture was
ready, `/v1/health` reported `playing`, and player/camera, authored lights,
rendered lights and Ambient were all live. This is the first successful cold-start
control on the affected driver. At 16:17 the later monitor/HDR switch reproduced
the separate runtime replacement crash described above.

The owner then installed the exact final 2.1.11 production package and started the
game at 17:04 on the same RTX 3080 / NVIDIA 616.92 system. The installed ASI hash
was verified as
`06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`, exactly
matching the final package. At about 17:16 the owner repeated the same output/HDR
switch to the connected TV which had caused the 16:17 crash. The game remained
responsive. `CrimsonDesertTelemetry.overlay.log` recorded a second complete
replacement sequence:

```text
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

The current game log contained no `CreateSwapChainForHwnd`, `E_ACCESSDENIED`, crash
or exception entry after the transition, and `/v1/ambient` continued returning
fresh data on port 27311. This is a passed one-way local reproduction test of the
runtime replacement crash. The reverse switch to the original output was requested
but had not been performed when the user asked for this handover. External RTX
4080/4090 startup acceptance remains separate and decisive for the Nexus reports.

## One next step

If desired, complete the local symmetry check by switching once back to the original
display and confirming another `Overlay ready` line without a game-log swapchain
error. Do not delay external validation for that check. Send the exact unchanged
2.1.11 ZIP and hashes above to at least one of the two affected users for three clean
starts with Overlay, Notifications, Lights and LightOverlay enabled. One
affected user reproducing three successful starts is enough for the first external
acceptance gate; the second user is useful confirmation, not a prerequisite for
learning whether the ordering fix works. Ask whether the HUD/markers initialize and
whether the Streamline `E_ACCESSDENIED` remains. If a run still crashes, preserve the
new game log and dump before changing code; compare the exception offset and loaded
modules to this checkpoint.

---

# Previous checkpoint — per-light direct visibility, 2026-09-12 (paused)

## Do not confuse the Nexus release with the research package

The public Nexus release is still the unchanged **2.1.10** production package,
uploaded on 2026-09-12 at 21:28. Nothing built after that upload was sent to
Nexus. The authenticated Nexus API confirms 2.1.10 is the active main version.

Exact published 2.1.10 artifacts:

| File | Bytes | SHA-256 | VirusTotal |
| --- | ---: | --- | --- |
| production ASI | 1,267,712 | `8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78` | 5/71 |
| production ZIP | 791,357 | `1A8DD7641E9431AA0FE383E68706D9519A0B93E2ED5E6A70129C4C4552DEBD24` | 0/68 |

Nexus reports that public file as safe. Do not remove, replace, warn about or
otherwise change the Nexus 2.1.10 release because of the later research scan.

The higher **ASI 11/71 / ZIP 7/68** results belong exclusively to the superseded
local `2.1.11-source-visibility.2` `CDT_RESEARCH=ON` package built around 23:18.
That package adds SDF readback, a player-to-light IPC bridge and other research
instrumentation. It was never uploaded to Nexus and must remain private. GitHub
`main` contains its source and documentation for continued development; this does
not alter the immutable production binary already hosted on Nexus.

## Previous priority (paused)

Focus only on this question for each local light already known to the project:

```text
Does solid world geometry block the direct segment from the player to this source?
```

Camera direction, on-screen state, Ambient, image brightness and bounced light are
outside this test. Fire, torch, candle, lamp, point and spot sources use the same
geometry rule. Stable release 2.1.10 and its hotfix/packaging work are complete;
do not reopen them unless they directly block this task. Do not work on CrimsonHue.

The production goal still requires both Ambient Occlusion and per-light Source
Occlusion in `CDT_RESEARCH=OFF`. Ambient already passed its controlled
open -> enclosed -> open test. Per-light visibility is not complete until research
and production each pass controlled live visible -> blocked -> visible tests,
including a retained source behind or outside the camera view.

## Current implementation

The previous camera-origin, renderer-only zero-crossing trace was rejected for two
measured reasons: it did not cover all known sources or the requested player
receiver, and filtered t233 wall samples can remain slightly positive.

The new research candidate consists of:

- `SourceVisibilityClient`: publishes player position + `(0,1,0)` and the spatially
  deduplicated union of authored and rendered light positions;
- `source_visibility_bridge`: a separate seqlocked native query/result mapping;
- `sdf_visibility`: quarter-cell segment sampling, near-surface threshold
  `0.5 * cellSize`, and blocked after one complete cell of connected samples;
- schema 1.5 metadata on both authored and rendered records, position-matched back
  without changing positions, RGB, raw records or smoothed contributions.

No camera direction or projection exists in the new bridge. The native worker
traces every new query against one immutable SDF volume. Unknown/stale results do
not hide or attenuate API records. The old render-capture visibility writer is
explicitly disabled while this research bridge is active.

The bridge is bounded to the nearest 256 distinct source positions, merges within
0.1 game unit, rejects a response after the player receiver moved more than
0.75 game unit, and expires results after 1500 ms. Sources absent from both input
feeds cannot be classified and must never be called blocked.

Implementation and API details: [SOURCE_VISIBILITY.md](SOURCE_VISIBILITY.md).
Preserved live failures/captures: [SOURCE_VISIBILITY_REGRESSION.md](SOURCE_VISIBILITY_REGRESSION.md).

## Evidence before live test

The exact native classifier reproduces all 17 preserved labelled controls:

- room: two blocked and two clear sources;
- same room source: three fresh clear-pose captures;
- Serkis: clear -> blocked -> clear;
- Warspike: two clear -> three blocked -> two clear.

This is offline replay of real retained volumes, not live acceptance. Relevant
automated checks pass:

- complete managed test executable, including the schema 1.5 player/query bridge;
- native SDF clear/blocked/freshness/context controls;
- native direction-independent query/result bridge;
- 384 WARP/debug-layer readback controls;
- research ASI compile and expanded/ZIP package validation.

The JSON schema parses successfully. `AGENTS.md` and `CLAUDE.md` are byte-identical.
The first `.2` game start exposed one compatibility defect: the managed host
correctly emitted new schema 1.5, but the ASI HUD parser still accepted only
through 1.4 and therefore labelled the entire stream incompatible. `.3` adds 1.5
to that strict allowlist and its additive-schema HUD regression. This was not an
ASI/DLL file mix and has no effect on Nexus production 2.1.10.

## Exact package for the next tester

Use only:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-source-visibility.3-ModManagers.zip`

This is a private `CDT_RESEARCH=ON` package for Steam build 25246367, not a public
release. It contains its own research README and a ready test INI:

- `SourceVisibility.Enabled=1`
- `Overlay.ShowDetails=1`
- `LightOverlay.HideOccluded=0` so wrong verdicts remain visible
- `Research.SignedDistanceReadbackIntervalMs=1000`
- up to 120 automatically started SDF transactions

Exact artifacts:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `CrimsonDesertTelemetry.asi` | 1,591,296 | `7C9B007E8DA4F73FDAF8047F5E9C74478E70005A226EBB82D6C6D9B2A37CCC02` |
| test ZIP | 943,190 | `96AB18A4E9BFF21694F9173CFB50A41F1CCE83A81405A3807DDDA8ED3FD7112E` |

Local Microsoft Defender custom scans passed both exact files with no threats.
Completed VirusTotal results for these exact `.3` research artifacts are
**ASI 11/71** and **ZIP 7/68** (the ZIP has 60 undetected and one analysis
failure; the ASI has one timeout and one failure). This is a
significant regression from the stable production build and blocks using this
architecture as a release candidate. It does not invalidate a private functional
test, but it must not be published or promoted to `CDT_RESEARCH=OFF` unchanged.
Investigate it later as a product/build regression without obfuscation or signature
gaming. The earlier `.1` and schema-incompatible `.2` artifacts are superseded
and must not be tested.

## One next step

With the game closed, install the `.3` ZIP cleanly through DMM. In the known room,
hold one player position where two identified sources are physically clear and two
are behind solid wood. Keep F11 in show-blocked mode. Verify that all four records
receive the correct status. Rotate the camera without moving the player and verify
retained verdicts do not change. Then perform visible -> blocked -> visible on one
identified fire/candle/lamp.

If the live result fails, capture the exact source positions, player position,
verdicts, `closestApproach`, `volumeSequence`, `contextFrame` and ages before tuning
anything. If it passes across relevant source types and behind-camera coverage,
move only the minimal bridge/classifier/acquisition pieces into `CDT_RESEARCH=OFF`
and repeat the same production test.

## Worktree care

Do not stage the unrelated root-level capture/VT helper scripts, JSON outputs or
scratch C++ files already present as untracked evidence. Do not delete prior
artifacts. Commit only the explicit implementation, tests, package tooling and
documentation for this candidate.
