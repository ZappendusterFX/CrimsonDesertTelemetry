# Mod-manager validation

## DMM 3.1.1 ASI removal reproducer (2026-09-27)

The owner used DMM's ASI plugin delete button with the game closed. DMM's
`logs/activity.json` recorded at 17:14:00: `Uninstalled ASI:
CrimsonDesertTelemetry.asi, CrimsonDesertTelemetry.ini,
CrimsonDesertTelemetry.ini`. The ASI and INI disappeared from both the DMM
library and `bin64`, so the plugin vanished from the ASI list. The library
folder remained with seven files. The four runtime companions below remained
in **both** locations and matched the exact private
`v2.2.1-capture-arming.1` ZIP byte-for-byte:

- `crimson-desert-telemetry.deps.cfg`
- `crimson-desert-telemetry.dll`
- `crimson-desert-telemetry.runtimeconfig.cfg`
- `CrimsonDesertTelemetry.Core.dll`

This also happened in DMM 1.9.4 and 2.8.1. The owner's separate
disable-then-delete sequence logged `ASI disabled` at 17:10:25 and
`Uninstalled ASI: CrimsonDesertTelemetry.ini` at 17:11:17, showing that the
delete path can run after the ASI has already been removed from `bin64`.
The current `config.json` still lists `CrimsonDesertTelemetry` in
`activeAsiMods` and its files in `definitiveLoadOrder`. In
`backups/asi_overwrite/CrimsonDesertTelemetry.asi`, DMM has `.dmm_added`
ownership markers for **all four** leftover runtime files. Its
`vanilla_manifest.json` also lists the two CFG files and the INI as part of
the `bin64` baseline, so that baseline is not a reliable clean-game inventory.
The exact DMM code path causing the skipped cleanup is not established; the
observable failure is that deleting the ASI did not clean its marked
companions or remove the library folder.

The [DMM Nexus changelog](https://www.nexusmods.com/crimsondesert/mods/633)
states that a subfolder containing an ASI and companions is supported and
that `.ini`, `.cfg`, and `.dll` companions deploy with it (v1 ASI pipeline).
It later claims companion cleanup on disable (v1.3.9) and complete bin64
cleanup on delete (v3.0.0). The ZIP has one `CrimsonDesertTelemetry/` folder
with the ASI and companions side by side; no separate ASI packaging
specification or manifest requirement was found. Therefore the evidence does
not support a claim that this ZIP is incorrectly packed. Do not assume DMM's
remove state means those files are gone. With the game closed, check the
six runtime files against the intended ZIP before every replacement, and
remove only the four identified leftovers when a clean install is needed.

## Current checkpoint (2026-09-12)

The new working-feature candidate is tracked in [stable release validation](STABLE_RELEASE_VALIDATION.md).
Per-light occlusion is paused and excluded. All six runtime files, including both
CFG companions, must match that candidate after user-operated DMM deployment.
The earlier checks below do not accept the new package automatically.

rc.1 installed/live at19:26:27 CEST: all six runtime files in the DMM library match
the ZIP. In bin64, five match; deps.cfg retains2.1.8-diagnostic-final labels.
Replacing only the version labels makes its JSON structure identical to rc.1:
runtime filenames and dependencies are unchanged. This local mismatch therefore
occurs between the correctly updated DMM library and deployed game metadata.
The new host runs, all feeds are available and the supplied all-enabled INI matches.
The reason DMM retains that one file is not established; no exact six-file deploy
or fix for the external missing-companions report is claimed.

The user then exited the game and deleted the package in DMM. At19:43:05 CEST,
ASI/INI/logs are absent, but both DLLs and both CFG companions remain in bin64.
Game/ASI-managed host are absent and port27311 is free. Functional deactivation
therefore succeeds; complete file cleanup is not established. User-operated
reimport and restart are the next check.

Reimport through user-confirmed DMM1.9.4 reproduced the mismatch: library6/6
correct, bin64 deps.cfg still old. The user requires complete runtime-file removal.
The user updated to DMM2.8.1. The same removal/deployment problem remained: package
deletion left both Telemetry DLLs and both CFG files, and reimport retained the old
deps.cfg. After the user manually removed those four owned leftovers with the game
closed, a clean DMM2.8.1 import deployed all six rc.1 files with exact hashes.
Release instructions therefore require this cleanup when upgrading; the manager's
enabled/deleted state alone is insufficient evidence of exact deployment.

For final2.1.10 acceptance, the user again manually removed all old Telemetry
files from bin64 before import. All six deployed runtime files then matched the
final ZIP exactly and the live game test passed. Inspection of DMM2.8.1 state found
older ASI overwrite backups and `.dmm_added` records; the dirty lifecycle had
restored older DLL backups and retained CFG companions. This explains the local
stale-file behavior but is not a complete manager-lifecycle fix.

The user installed v2.1.9-ambient-input-fix through DMM; the ASI, both DLLs, INI
and runtimeconfig matched the package. DMM retained v2.1.8 deps.cfg with older
version labels but the same dependency structure. Ambient passed the controlled
open/enclosed/open route on game build 25246367. Current per-light production
work remains under live validation; see [handover](HANDOVER.md).

An external Nexus report from around 08:30 on 2026-09-12 describes incomplete DMM
deployment and a separate graphics-feature crash with public version 2.0.2. Its
exact file hashes and crash environment remain unverified. See [current compatibility reports](COMPATIBILITY_ISSUES.md).
No complete DMM/JSON Mod Manager lifecycle or cross-hardware acceptance is claimed.

## Historical: 2.0.0 HDR validation package (2026-09-07)

The HDR follow-up passes the native Release build and all14 CTests, including
software GPU pixel-readback and scRGB ImGui/notification/resize tests. See
[overlay validation](OVERLAY_VALIDATION.md). No live HDR game/display acceptance
is available, and this updated ASI has not been installed during this pass.

Separate package: `artifacts/mod-manager/v2.0.0-hdr-20260907-001024/CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`.
SHA-256: `8B92CE3AC3E4EC09A468DF772AC84BFFA127895E3507D317A28FB8B775D71E80`.
ASI SHA-256: `C781D4F04B2A24D7403DA073150CDC86406B7537D32120E2773946B64619F26E`.
This archive contains the rebuilt native ASI/current INI and README, plus unchanged
managed binaries/runtime metadata from the earlier verified2.0.0 package. Nine-file
validation, negative controls, ZIP/expanded equality and comparison of the bundled
README/INI against current sources passed. The earlier immutable ZIP below is
preserved byte-for-byte; this is not a silent replacement or a published release.

## Previous: 2.0.0 local release verification before HDR (2026-09-06)

The installed 2.0.0 package passed a fresh game start and a progressing live API
check; the user confirmed it works. The included INI enables authored/rendered
lights, both HUD views and notifications. The optional console/explorer remain
disabled. This is one ASI with native renderer instrumentation and an external
memory-reading host, not an untouched read-only game run.

Local Release verification passed:

- Managed build with zero warnings and all 56 managed regression tests.
- HTTP/WebSocket smoke and all 11 native CTest paths, including paired GPU light/
  counter readback, exact-hook rejection and UI/client/notification coverage.
- Package validator and negative cases, plus expanded/ZIP payload equality.
- Exact current EXE hook/caller anchors match the generated build contract;
  negative production `StartCapture` cases leave code untouched with no trampoline.

Verified pre-publication package:
`CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`.
SHA-256: `E8A268B2B0A9D2C592A13789E0C67FC0F5C7E75BF81F09ADE7736B5784D16D12`.
ASI SHA-256: `F2B62762945EC0E3A1FEDFB5B8836FB927A1DF81520A777D1F4EE6A521B1387D`.
The package was built before this public-documentation refresh; these hashes
identify the artifact tested below, not any subsequently repackaged archive.

The game started at 23:20:22 CEST (PID 40280). Its loaded ASI's disk hash matched
the package, and the current-start log recorded guarded capture at 20 Hz with
paired counter/fence plus a successfully started host (PID 39732). The read-only
API control at 23:23:25–27 CEST reported `playing`/`tested`, sequence 4767→4845,
capture sequence 2460→2480, engine frame 8436→8496 and light age 16→15 ms. Player
and camera poses were independently available, with no health error. That scene
contained 18 authored records and 56 filtered contributions inside the configured
radius, with zero malformed records; these are not distinct physical-lamp counts.

On a later repeated startup, the user accepted the success notice appearing as
soon as data arrives, during visible loading. Its INI duration is 6000 ms; precise
screen duration was not separately measured. Returning to the title screen can
leave HUD/data visible for several seconds until stale or another load, also
explicitly accepted. The measured PID/API trace above belongs to the earlier run.

This establishes the tested local startup/data path, not a complete mod-manager
lifecycle matrix. DMM disable/uninstall/owned-host cleanup and the complete JSON
Mod Manager lifecycle still lack a current full acceptance record. Preserve
custom INI settings and verify installed binaries plus both `.cfg` companions
after upgrades; earlier DMM installs sometimes retained old metadata. Always close
the game before replacing the ASI. AMD/Intel in-game hardware, HDR and frame
generation are not established by these checks; HUD rendering supports D3D12/8-bit SDR.

## Historical records

The following entries describe their named artifacts only. Earlier defaults,
test counts, camera/light limitations and preview acceptance gates do not override
the current 2.0.0 result above.

## Historical: v1.2.0 engine-light release candidate (2026-09-04)

The managed Release build and all 43 regression tests passed, including the new
direct camera and direct scene/light layouts. All three native CTests, four API
example tests, and expanded-package/ZIP validators passed. The actual Release
binary was then run read-only against Steam build `25116796`: schema 1.2 reported
player, orientation, camera and available engine lights; 230 source records yielded
135 within radius 100, with zero malformed, unsupported or unavailable-walk records.

Final package: `CrimsonDesertTelemetry-v1.2.0-ModManagers.zip`.
SHA-256: `2102189439F629FF564ECFCC837C6F54F931AA701EEBC55E1C2521FC2EF87EDE`.

## Historical: v1.1.0 automatic-compatibility release candidate (2026-09-01)

The managed Release build, 37 managed regression tests, loopback HTTP/WebSocket
smoke test, all three native CTests and package/ZIP validators passed. Forced
automatic resolution of the real, locally validated game EXE independently found
the same six player/camera RVAs as its exact-hash definition. Synthetic tests also
cover relocated layouts and fail-closed behavior for missing/ambiguous code,
malformed PE data, invalid data targets and weak/ambiguous object-table fingerprints.

Final package: `CrimsonDesertTelemetry-v1.1.0-ModManagers.zip`.
SHA-256: `ED291A162400BC8BD3DA5A824554A4949776DF35708479B05383845D50C897D3`.

The candidate was then imported through DMM. DMM deployed all binaries but retained
the old `deps.cfg`; replacing that one file from the imported candidate made all six
runtime files match. Before launch, Steam updated the game to build `25050808`, EXE
`1.0.0.2692`, SHA-256
`A93336BC08F1613B609F4F331C687B13A8DB729DD1AB99D8FD09C9FBE06BC210`.
The new hash passed automatic offline resolution, finding the same six guarded RVAs.

A fresh live launch then reported `playing`, `compatibility.mode: automatic`,
reference build `24994088`, build `25050808`, no error and roughly 60 samples/second.
Player, player orientation, camera and quality were present at `1/1/1`. A camera-only
turn changed the camera while player position/heading remained exact. Subsequent
player movement changed position by approximately `(-2.71, -0.08, +1.07)` and player
heading from `228.56` to `302.94` degrees while camera heading remained `306.78`.
This constitutes local validation of the automatically recognized build. The final
package adds its exact build definition and release-version metadata; runtime data
sources are unchanged. That exact path selects the same confirmed six RVAs without
the automatic scan and does not require a redundant second movement test.

## Historical: v1.0.0 release candidate (2026-08-31)

The release package has been built locally with the MIT license, native camera
reader and HUD disabled by default. The following checks passed for this artifact:

- Release managed build and all 30 managed regression tests.
- HTTP/WebSocket API smoke test.
- Three native CTests: HUD model, isolated D3D12 renderer and WebSocket client.
- Expanded-package checks and ZIP payload comparison (nine files).
- Packaged version 1.0.0, MIT license and `[Overlay] Enabled=0` verified.

Package: `CrimsonDesertTelemetry-v1.0.0-ModManagers.zip`.
SHA-256: `4F8EEAFFA0030DDE4BD8095ACB606D3CB73F9E3A9F03D15FD14DEC5CE9F363C9`.

Final installed-file/startup acceptance passed on 2026-08-31 after correcting
two retained metadata files (see below). All six runtime files, including the INI,
matched the v1.0.0 package by SHA-256. The ASI was loaded in the restarted game;
health reported `playing`, supported build 24994088 and no error. Player position,
player orientation and camera data were present. The sequence advanced by 62 over
1.0261 capture seconds (approximately 60 Hz). With `[Overlay] Enabled=0`, health
reported zero WebSocket clients and the HUD log had no entries from this launch.

During this DMM upgrade, the library contained the correct new `.cfg` companions,
but `bin64` retained the older dependency/runtime metadata while the DLLs updated.
With the game closed, both old files were backed up and replaced from the release
package before the successful restart. This verifies the corrected installation,
not automatic metadata replacement by DMM. Preserve customized INI settings when
upgrading, but replace both `.cfg` companions alongside the binaries.

JSON Mod Manager lifecycle validation remains pending; do not advertise it as
fully tested. The preview records below are historical evidence, not additional
checks of the final archive.

## Historical: preview.7 native-camera candidate (2026-08-30)

The managed host now uses the native engine camera, validated after a cold start
with upscaling off. The previous HUD remains, with a single-source diagnostic
label. DMM import/deployment, packaged HUD acceptance, reload/teleport, and testing
other upscalers are still pending for this package; earlier passes do not cover it.

Preview.7 build checks passed: 30 managed tests, three native CTests, loopback
HTTP/WebSocket smoke, expanded-package validation, and ZIP payload verification.
Package SHA-256: `FD893319C0B16BFF07B159701C7C4069F5CB5D2EA797A75B5C775D4C8FD42F67`.
The installed DMM/game files were not changed by this build.

## Historical: preview.4 in-game observation / preview.6 candidate (2026-08-30)

The new optional Dear ImGui HUD has native model, isolated D3D12 and loopback
WebSocket tests. Its manager ZIP adds THIRD-PARTY-NOTICES.txt (nine payload files).
The preview.4 HUD appeared in the actual game, but was too small at 4K and the user
reported camera-arrow lag/backsteps. Preview.6 includes resolution scaling and a
temporal camera-selection fix verified in offline replay; renewed game acceptance,
recording and manager lifecycle tests remain pending. Importing the
ZIP into DMM updated its library but initially left the old ASI in game `bin64`.
With the game closed, disabling/re-enabling the plugin deployed the matching ASI.
Verify deployed file hashes after upgrades; the DMM status badge alone is insufficient.
See
[OVERLAY_VALIDATION.md](OVERLAY_VALIDATION.md). The preview.2 results below are
historical evidence for the telemetry bootstrap, not proof of the new render hooks.

## Historical preview.2 record

Date: 2026-08-30. Package: `0.1.0-preview.2`.

This record is separate from the validated external telemetry tests. DMM deployment
and an actual in-game startup pass; full lifecycle and JSON Mod Manager validation
are not yet complete.

## DMM metadata collision and fix

DMM 1.9.4 interpreted `crimson-desert-telemetry.deps.json` as a JSON game mod and
reported `invalid type: map, expected a sequence`. The file was valid .NET JSON,
but not DMM's game-patch schema. The runtimeconfig JSON was also incorrectly
added to the manager's mod load order.

The package now ships `.deps.cfg` and `.runtimeconfig.cfg`. The bootstrap uses
`dotnet exec --depsfile` for the dependency metadata. A direct runtime `.cfg`
argument failed on the installed .NET host: hostfxr normalized the requested name
to `.json`. Therefore the bootstrap caches only the runtime text under
`%LOCALAPPDATA%\CrimsonDesertTelemetry\Runtime\runtime-<SHA256>.json` and supplies
that absolute path via `--runtimeconfig`. Cache contents are verified before use.
No JSON or executable is generated in the mod library.

## Completed checks

- Native ASI and managed host build successfully.
- All 12 managed tests pass, including the embedded build definition.
- Package tests reject either old JSON filename, a missing companion, and a loose EXE.
- ZIP and expanded payload contain exactly the same eight expected files.
- An isolated loader process successfully starts the actual ASI from a path with
  spaces, without adjacent `.json`, definition, or schema files.
- HTTP health and embedded schema endpoints respond from that host.
- Host terminates when the isolated loader process exits.
- No `.json` appears in the package directory after startup.
- After replacing the old library folder and restarting DMM, its 09:34:44 log
  reports one ASI plugin and `winmm.dll`; the new startup scan has no invalid-JSON
  error. Older log entries retain the original error as historical records.

The old test folder and archive were backed up outside DMM's mods directory.
The user's INI settings and existing ASI loader were preserved.

## DMM deployment and in-game startup

Verified with DMM 1.9.4 and package `0.1.0-preview.2` on 2026-08-30:

- After the user enabled the plugin, DMM listed it in `activeAsiMods`. All six
  runtime companions in `bin64` matched the library files byte-for-byte (SHA-256):
  the ASI, INI, core DLL, host DLL, dependency CFG, and runtime CFG.
- The user started the game and loaded a save. Game PID 16600 started at 09:43:20
  local time. Its loaded modules included the game-directory `winmm.dll` and
  `CrimsonDesertTelemetry.asi`.
- The bootstrap log recorded starting host PID 1720 at 09:43:22 on loopback port
  27311 at 60 Hz. No separate manual telemetry launch was used for this test.
- HTTP health and snapshot returned 200, state `playing`, supported build
  `24994088`, and no health error. Discovery found 39 copies in 12,580.8 ms.
- Player and camera positions, the camera basis, FOV, and aspect ratio were
  present. A subsequent WebSocket check received 121 valid snapshots over
  1.9952 capture seconds (60.15 Hz), with no sequence gaps and no missing player
  or camera objects. All samples reported `playing`.
- Reported capture duration over those 121 samples averaged 126.9 microseconds
  (minimum 96, maximum 494).

DMM's UI simultaneously showed `NOT MOUNTED`, one active ASI, and an available
loader. The badge's cause was not investigated; the file hashes and loaded-module
check establish deployment and loading independently of that ambiguous display.
This startup check does not replace movement, reload, or shutdown lifecycle tests.

## Historical preview.2 pending checks

- DMM: clean disable/uninstall and game shutdown with owned-host cleanup.
- JSON Mod Manager 9.9.4: import, deploy, startup, and uninstall the same archive.
- Repeat the game lifecycle tests through the ASI bootstrap, including failure
  paths for missing runtime, an occupied port, and unsupported game builds.

These were the preview.2 acceptance gates. The current scope of completed and
outstanding 2.0.0 validation is recorded at the beginning of this document.
