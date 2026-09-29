---
name: dmm-package
description: Build, verify and hand over a Crimson Desert Telemetry mod-manager (DMM) ZIP with scripts/Build-ModManagerPackage.ps1 - private test packages with baked-in INI switches as well as release candidates. Use this whenever the owner asks for a new package, test build, Testpaket, ZIP, "zum Testen", release candidate or new release, or whenever a code change is ready for an in-game test, even if they never say "DMM" or name the script.
---

# Building a DMM package

The owner installs every in-game build as a whole ZIP through DMM
(`C:\Modding\CrimsonDesert\DMM\DMM.exe`). One script builds the managed host and
the native ASI, stages the payload, zips it and self-tests it. Your job is to pick
the right kind of package, invoke the script correctly, verify what came out,
record it and hand it over. Packages under `artifacts/mod-manager/` are evidence:
never overwrite, delete or repack one.

## 1. Decide the kind of package

| | Private test package | Release |
|---|---|---|
| Version | prerelease suffix: `2.2.1-<topic>.<n>`, e.g. `2.1.15-physics.10`, `2.1.15-upstream.1` | plain: `2.2.0` |
| Native profile | `-Research auto` resolves to ON (`CDT_RESEARCH=ON`) | resolves to OFF (production) |
| INI template | `packaging/mod-manager/CrimsonDesertTelemetry.research.ini` | `packaging/mod-manager/CrimsonDesertTelemetry.ini` |
| Packaged README | `README.research.txt` | `README.txt` |
| `-IniOverrides` | allowed; bake the test switches in | refused by the script |
| Build tree | `build/native-package` | `build/native-package-release` |

`-Research off` on a prerelease version builds a private production-profile
package (use it to test exactly what a release would contain). The two
`-UnvalidatedDiagnostic` / `-IntegratedDiagnostic` modes are exact-build research
diagnostics; use them only when the handover asks for one.

Why overrides matter: a private package must run as intended straight from DMM.
A hand-edit of the INI after install has already cost the owner a wasted game
start, so bake every switch the test needs into the package.

## 2. Pick a version that does not exist yet

```powershell
Get-ChildItem artifacts/mod-manager -Filter 'CrimsonDesertTelemetry-v*-ModManagers.zip' | Sort-Object LastWriteTime | Select-Object -Last 5 Name
```

Count up the `<n>` of the current topic, or start a new topic at `.1`. The script
refuses an existing version; that refusal means "choose a new version", never
"delete the old ZIP". Its `-Version` default is the last release, so always pass
`-Version` explicitly.

For a release, also set `<Version>` in `Directory.Build.props` and the `$Version`
default in `scripts/Build-ModManagerPackage.ps1` to the new number, and update
`CHANGELOG.md`, `docs/releases/v<version>.md`, `README.md`,
`packaging/mod-manager/README.txt` and the production INI template before building.

## 3. Before building

- Commit the source changes first (explicit paths only; the repo root has many
  untracked scratch files that must not be committed). Then the package maps to
  one commit.
- Run the managed tests. The script itself does not run them:
  ```powershell
  dotnet build .\CrimsonDesertTelemetry.sln -c Release
  dotnet run --project .\tests\CrimsonDesertTelemetry.Tests -c Release --no-build
  ```
  Every line should start with `PASS` and the exit code should be 0.
- The game must be closed only for the owner's install, not for building.

## 4. Build

Run the script inside the PowerShell tool with the call operator. Do not use
`pwsh -File`: a hashtable cannot be passed on that command line and the call fails.

```powershell
# private test package with baked-in switches (research profile)
& ./scripts/Build-ModManagerPackage.ps1 -Version '2.2.1-topic.1' -IniOverrides @{ 'Experimental.PhysicsVisibility' = '1'; 'LightOverlay.Radius' = '100' }

# release (production profile, no overrides)
& ./scripts/Build-ModManagerPackage.ps1 -Version '2.2.1'
```

Override keys are `Section.Key`, and each must exist exactly once in the chosen
template. Research-only sections (`[Experimental]`, `[Research]`, `[Console]`,
`[Explorer]`) do not exist in the production template, so such overrides fail
with `-Research off`. The build takes a few minutes; give it a 10-minute timeout.
cmake is found through vswhere; the native build uses `/W4 /WX`, so warnings fail it.

A good run ends with three `PASS` lines (profile self-test, expanded package,
ZIP payload), then `Package:`, `Expanded package:` and `SHA-256:`.

## 5. Verify the result

```powershell
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build/native-package-release -C Release   # build/native-package for research
Select-String -Path build/native-package-release/CMakeCache.txt -Pattern '^CDT_RESEARCH'
$d = '<Expanded package path>'
Get-ChildItem $d -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName).Hash, $_.Name }
```

- All CTest tests pass on the tree that produced this ASI, and `CDT_RESEARCH` matches the profile.
- The packaged `CrimsonDesertTelemetry.ini` shows the values the test needs (grep the relevant sections).
- When a feature must be in or out of the ASI, a log string check is quick evidence:
  `grep -a -c "<log text>" CrimsonDesertTelemetry.asi`.
- The payload is nine files: ASI, `CrimsonDesertTelemetry.Core.dll`,
  `crimson-desert-telemetry.dll`, `.deps.cfg`, `.runtimeconfig.cfg`, INI,
  `README.txt`, `THIRD-PARTY-NOTICES.txt`, `LICENSE.txt`.

## 6. Record and hand over

Write the current checkpoint in `docs/HANDOVER.md`: version, purpose, ZIP path,
ZIP SHA256, ASI SHA256, expanded directory, the baked-in INI values and the test
results. Commit it. Then end the turn with a short German handover and do not
wait or poll; the owner returns with a result. The handover contains:

1. Game closed; remove the previous Telemetry package in DMM.
2. DMM 2.8.1 can leave `CrimsonDesertTelemetry.Core.dll`, `crimson-desert-telemetry.dll`,
   `crimson-desert-telemetry.deps.cfg` and `crimson-desert-telemetry.runtimeconfig.cfg`
   behind; delete only these if present, never the ASI loader.
3. Import and activate the complete ZIP (path), then load a save.
4. Exactly what to look at in the game, and which log line proves the feature
   started (`CrimsonDesertTelemetry.native.log` / `.bootstrap.log` beside the game EXE).

## 7. Release only: after the owner's release go

The live test covers these exact bytes. If anything changes afterwards, build a
new version instead of rebuilding the same one.

- Antivirus: `python scripts/Get-VirusTotalVerdict.py <ZIP> <ASI>` looks up hashes
  only. Uploading (`--submit`) sends the files to VirusTotal and needs the owner's
  explicit OK for this run. Report the ZIP and ASI verdicts separately, and don't
  call a package "virus-free" when the ASI has detections. Background:
  `docs/ANTIVIRUS_FINDINGS.md`.
- Write `docs/releases/v<version>-validation.md`; use `docs/releases/v2.2.0-validation.md` as the model.
- Push, tag `v<version>` and create the GitHub release with the ZIP and the
  release notes, but only on the owner's go. Nexus only when the owner asks for it.
- Python for repo scripts: see the Python section in `docs/TOOLING.md`.
