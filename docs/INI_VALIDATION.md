# INI profiles and validation — 2026-09-12

This is the option inventory and test ledger, not a claim that every configuration
has passed in the game. The previous combined template contained **60 settings**.
Public 2.1.11 offered **31**; later candidates offered **34** after restoring
SourceVisibility, HideOccluded and OcclusionToggleKey. Release 2.2.0 offers **35**
(adds Lights.Upstream). The unchanged
research template retains all **60**, including the 26 research-only settings below.
No research implementation or evidence was removed.

`Build-ModManagerPackage.ps1 -Research off` selects
`packaging/mod-manager/CrimsonDesertTelemetry.ini`; `-Research on` selects
`CrimsonDesertTelemetry.research.ini` from the same directory. `auto` selects ON
for prerelease version strings containing `-`, OFF otherwise. Both package the
selected file as **CrimsonDesertTelemetry.ini** and retain the same nine-file
package contract. Unsupported OFF overrides fail before building; changing the
profile does not add a second runtime INI to the ZIP.

Package validation checks exact supported keys, sections, values and payload
companions for the selected profile. It does not prove installed files, hardware
compatibility, runtime effects or live feature combinations. A user-edited INI
does not automatically pass through the package validator.

## Evidence and remaining acceptance

- Ambient alone has controlled live open/enclosed/open acceptance in OFF v2.1.9,
  build 25246367. That is not acceptance of every INI value or combination.
- Public 2.1.11 excludes per-light visibility after failed live acceptance. Current
  OFF source restores only the earlier camera-to-rendered-source path that worked
  for fires, together with HideOccluded/F11. Its new package requires a controlled
  live test and exact ASI/ZIP antivirus checks before release. The broader
  player/all-source research bridge remains ON-only.
- The native tests below are synthetic: configuration parsing, model behavior,
  acquisition and graphics fixtures. A unit-test result is not a live game result.
- rc.1 first SDR game start now passes with all offered booleans1; two30-second
  stream controls have no unavailable light/ambient samples. User confirms
  F8/F9/F10 toggling and menu entry/exit without flicker/crash. This does not cover
  every numeric value, remapped hardware key, HDR, or a new quit/restart cycle.
- The historic combined template allowed genuine conflicting research behavior in
  OFF. The profile split removes those offered controls from production; native
  OFF gating of old research INIs is a separate required check.

Test references used in the tables:

| Code | Existing evidence / scope |
| --- | --- |
| P | Profile/package validator controls in `tests/Test-ModManagerPackage.ps1`: supported values, profiles, companions and ZIP equality; no runtime execution. |
| O | `native/CrimsonDesertTelemetry.Asi/tests/overlay_tests.cpp:635`: defaults, disabled/hidden views, bounds, HDR white, four remapped/disabled keys and model behavior. Coverage varies by setting; the table says where boundaries lack a direct control. |
| G | `tests/graphics_smoke.cpp:330` under the same native directory: synthetic HUD, markers, markers-only, notices-only, resize and SDR/scRGB modes. New combined HUD + markers + notifications tests now pass in SDR and scRGB at720p/1080p/4K; this does not exercise the live game or every INI value. |
| B | Native `tests/bootstrap_smoke.cpp:55`: actual host launch using CFG companions and HTTP checks, including smoothing time forwarding. It is a separate executable, not a registered CTest entry. |
| M | `tests/CrimsonDesertTelemetry.Tests/SmoothedLightTests.cs`: grouping, EMA, repeated samples, zero smoothing and invalid options; not all ASI INI forwarding paths. |
| A | Native sampler/sky capture tests and managed sky reader tests; live ambient route above. |
| V | Native SDF acquisition/trace/bridge tests and managed rendered-light/overlay metadata tests; current OFF candidate includes a dedicated clear/blocked/front/behind/raw-preservation control. Live production verdicts pending. |
| R | Direct research spatial/readback/ambient-event tests; these do not establish INI orchestration or coexistence with production. |

## Production options

All supported boolean values in packaged profiles are `0` and `1`. Runtime
readers have historically accepted other nonzero integers; that is not an
additional advertised boolean syntax. Changes require a game restart except
the four runtime display toggles. Fractional values use a decimal point.
Every supported boolean feature is 1 in the 2.2.0 release template (the display
preference HideOccluded stays 0), including ShowDetails and `SourceVisibility.Enabled`, which drives the physics ray fan in
production since 2.2.0. The earlier production SDF classifier (2.1.14 and earlier,
default 0 after false-visible results through solid geometry) is no longer
selected by OFF; its research path and evidence remain in the ON profile.

Bootstrap settings are read in `src/bootstrap.cpp:252-268`, light/ambient source
selection in `src/instruments.cpp:110-207`, UI settings in
`src/overlay_client.cpp:340-376`, all relative to the native ASI directory.

| Section / key | Supported range and effect / dependency | Test coverage and gap |
| --- | --- | --- |
| Server.Enabled | 0/1; controls ASI-owned host launch. Does not disable independent UI/native capture. UI requires an available host. | B covers enabled launch; disabled/combined startup needs explicit orchestration coverage. |
| Server.Port | Integer 1024-65535; shared host/UI loopback port. | B covers a nondefault port;20 O port cases now pass after signed host/UI clamp parity was corrected. |
| Server.SampleRateHz | Integer 1-240; host publication, not GPU sampling rate. Runtime clamps. | B covers launch; boundary INI forwarding lacks direct controls; P. |
| Notifications.Enabled | 0/1; separate UI owner, works with both views disabled. | O/G notices-only/all-three-UI SDR/scRGB pass; rc.1 all-enabled SDR startup logs report ready. |
| Notifications.DurationMilliseconds | Integer 5000-10000; success-notice duration, not persistent-fault duration. Runtime clamps. | O tests short/long and nondefault values; G notice rendering; P. |
| Lights.Enabled | 0/1; authored host lights and prerequisite for native rendered capture. | B schema selection; O readiness expectations; live default. Isolation combinations pending. |
| Lights.NearbyRadius | Integer 1-100000 game units around player; cannot discover omitted rendered sources. Runtime clamps. | Decoder radius tests; direct INI boundary forwarding pending; P. |
| Lights.ManyLights | 0/1; native rendered capture, requires Lights.Enabled. Also required by Ambient. | O expected-feed flags and native capture controls; isolation startup matrix pending. |
| Lights.ManyLightsSampleRateHz | Integer 1-60; capture interval is integer `1000 / rate` ms (`render_capture.cpp:547`). | Native copy tests; INI min/max cadence not a measured game result; P. |
| Lights.Upstream | 0/1, missing = 1; also copies the ManyLights INPUT (before view selection) with every sample; requires Lights.Enabled/ManyLights. Feeds `lights.upstream`, HUD/radar, PhysicsVisibility targets and smoothing. Refused cleanly if exact-build input anchors or enhanced barriers do not verify. | WARP copy/fence/debug-layer test, v4 bridge and decoder tests, HUD model/render tests, offline replay of the live PID2252 pairs; live game acceptance pending; P. |
| Ambient.Enabled | 0/1; global sky + camera visibility, requires Lights.Enabled/ManyLights. Independent of HUD visibility. | A plus live ambient default; dependency/combined startup pending. |
| SourceVisibility.Enabled | 0/1, default 1 since 2.2.0; in OFF enables the continuous physics ray fan (9 rays camera -> each light within LightOverlay.Radius, 256 targets, 20 batches/s, 2 ms budget, 500 ms expiry, exact build only); requires Lights.Enabled/ManyLights. Targets are the upstream lights when Lights.Upstream=1. OFF no longer selects the SDF classifier. It does not enable the ON-only player/all-source bridge or manual probes. | Native physics tests (ON test target), managed PhysicsVisibility/UpstreamLightTests; private physics.10 and 2.1.15-upstream.1 live use reported working by the owner; exact 2.2.0 ZIP live test pending; P. |
| LightSmoothing.TimeConstantMilliseconds | Integer 0-2000; 0 keeps grouping but disables temporal smoothing. | B forwards time; M covers zero/invalid/EMA; P. |
| LightSmoothing.GroupRadius | Decimal 0.01-1 game units; maximal group extent, not physical identity. Invalid runtime input falls back to 0.15 and logs. | M validates options/grouping; ASI malformed/boundary forwarding pending; P. |
| Overlay.Enabled | 0/1; corner HUD. Other UI owners can keep graphics/client active when 0. | O/G disabled, enabled and view-isolation controls. |
| Overlay.InitiallyVisible | 0/1; initial display only when Enabled=1; does not stop capture/client. | O/G hidden startup and later display. |
| Overlay.ShowDetails | 0/1; initial diagnostics, changed by DetailsKey. | O/model/layout and G details rendering. |
| Overlay.ShowAmbient | 0/1; if Overlay.Enabled, polls ambient every 500 ms even while hidden/details closed (`overlay_client.cpp:244`). Needs Ambient.Enabled for data. | O default/model values; individual INI off/poll-suppression integration pending. |
| Overlay.Radar3D | 0/1; 0 uses legacy compass, 1 light radar. | O covers both layouts/scales; user confirms rc.1 radar/HUD with all features on. |
| Overlay.ToggleKey | Integer 0-255 Windows VK; 0 disables; default119/F8; requires Overlay.Enabled. | O remap/disable and shortcut state machine; user confirms rc.1 default F8/F9/F10 off/on twice. Remapped physical keys remain untested live. |
| Overlay.DetailsKey | Integer 0-255 Windows VK; 0 disables; default120/F9; requires Overlay.Enabled. | O remap/disable; same key limits below. |
| Overlay.Corner | Integer 0-3: TL/TR/BL/BR. Runtime clamps. | O/G default rendering; all four anchors/boundaries need explicit controls; P. |
| Overlay.AutoScale | 0/1; resolution scaling in addition to Scale; fit-to-display limit remains. | O tests manual/automatic across resolutions. |
| Overlay.Scale | Decimal 0.5-3; HUD/display multiplier with fit bound. Runtime clamps or defaults to1 for malformed/nonfinite input. | O manual/auto and max-scale fit; parser boundary controls incomplete; P. |
| Overlay.Opacity | Decimal 0.2-1; corner HUD panel background alpha. Malformed/nonfinite runtime input defaults to0.92. | G default pixels; direct min/max alpha controls pending; P. |
| Overlay.HdrPaperWhiteNits | Decimal 80-500; shared HDR UI reference white, even with corner HUD off. Does not alter game HDR/API values. | O clamp/nonfinite/notices-only; HDR synthetic pixel controls; live HDR pending. |
| Overlay.StaleMilliseconds | Integer 100-10000; HUD envelope timeout. Separate rendered-light limit remains500 ms, visibility1500 ms. | O freshness models; direct INI boundaries pending; P. |
| LightOverlay.Enabled | 0/1; independent fullscreen markers; radar belongs to Overlay. | O/G markers-only, HUD+markers/all-three-UI pass; rc.1 all-enabled SDR and user F10 control pass without reported flicker. |
| LightOverlay.InitiallyVisible | 0/1; initial marker state, changed by ToggleKey; requires Enabled. | O/G hidden startup and toggle regression. |
| LightOverlay.ToggleKey | Integer 0-255 Windows VK; 0 disables; default121/F10. | O remap/disable/bounds and G runtime display state. |
| LightOverlay.HideOccluded | 0/1, default 0 (blocked lights are dimmed); at 1, HUD/radar show only fresh `clear` records. Blocked, unknown, missing and stale records are hidden. Raw and unfiltered EMA API records remain unchanged; the separate `/v1/lights/visible` feed applies clear-only filtering regardless of this HUD switch. | O model controls; V raw preservation; moving-source live acceptance pending. |
| LightOverlay.OcclusionToggleKey | Integer 0-255 Windows VK; 0 disables; default122/F11. | O remap/disable/bounds and shortcut state machine; live F11 depends on accepted visibility verdicts. |
| LightOverlay.Radius | Decimal 1-500 game units around the player, 100 in the 2.2.0 template; shared by radar, markers and physics visibility targets. With SourceVisibility the host raises its API capture radius to at least this value; it never creates lights the game does not hand to the renderer. | O nondefault/bounds/nonfinite; G views; managed physics radius boundary tests; P. |
| LightOverlay.MaxMarkers | Integer 1-2048; bound in both views. 0 clamps to1, does not disable. | O small/large bounds; G rendering; P. |
| LightOverlay.MaxLabels | Integer 0-16; fullscreen detail-label bound. 0 keeps markers without labels. | O small/large bounds; label-zero render check pending; P. |

Reserved/unavailable virtual keys may not produce a useful key event despite
falling inside 0-255. Duplicate assignments are not rejected: one press can
toggle multiple functions. Game bindings still receive the key. Independent
remapping/zero-disable tests do not establish every key/game-binding combination.

Disabling a prerequisite is a supported isolation configuration, not a reason
to fabricate data: for example Ambient=1 with ManyLights=0 cannot provide ambient
measurements. The package validator checks the settings themselves and does not
claim unavailable dependent features are active.

## Research-only options preserved in the ON template

These 26 settings are absent from the production template and rejected as OFF
package overrides. ON retains the original controls, including their existing
limitations. Their presence is not a promise that all can run together.

| Section / key | ON effect, range and dependency | Test coverage / limit |
| --- | --- | --- |
| Research.AmbientProbe | 0/1; exclusive event-triggered diagnostic, replaces normal ManyLights/sky stream and prevents spatial readback combinations. | R tests event/copy mechanics; incompatible with normal all-on streaming. |
| Research.SpatialProbe | 0/1; enables passive/event probe when Lights/ManyLights are active. | R; INI orchestration pending. |
| Research.SpatialReadback | 0/1; guarded R8 readback; requires probe/source prerequisites and AmbientProbe=0. SDF mode takes precedence when also selected. | R; simultaneous modes are not independent output promises. |
| Research.SpatialReadbackCount | Integer1-8; runtime clamps, contrary to the preserved old comment saying fallback-to-one. | R series tests; P. |
| Research.SpatialReadbackIntervalMs | Integer250-10000; values outside range become1000 at runtime. Opportunity-based, not exact cadence. | R; P. |
| Research.SpatialVisibilitySeconds | Unsigned seconds; 0 disables the timed extension. Positive values derive a bounded series, at most3600 transactions. | R arithmetic, not every duration or live cadence. |
| Research.SignedDistanceReadback | 0/1; R16 acquisition/export mode, distinct from production current-volume acquisition; AmbientProbe must be0. | R and preserved captures; no all-on acceptance. |
| Research.SignedDistanceReadbackCount | Integer1-120; runtime clamps. | R; P. |
| Research.SignedDistanceReadbackIntervalMs | Integer250-10000; outside range becomes1000. | R; P. |
| LightOverlay.OcclusionTest | 0/1; legacy trace/details path; native activation also requires LightOverlay.Enabled. Can acquire SDF even if production SourceVisibility=0. | O config/legacy trace tests; no new live production acceptance. |
| Console.EnableConsole | 0/1; research discovery, console window hook and commands. | No direct parser/coexistence test found. |
| Console.PatchGates | 0/1; patch guarded console gates when research startup is requested. Explorer-only can request that startup too. | Preserved research; current combinations unaccepted. |
| Console.PatchDevFlags | 0/1; writes discovered developer flags during research discovery. | Same limitation. |
| Console.EarlyBreakpoint | 0/1; early one-shot object capture when research startup is requested. | Preserved breakpoint machinery; configuration matrix pending. |
| Console.HideNotification | 0/1; game's console notification path, not telemetry Notifications.Enabled. | `console.cpp:194`; current live acceptance pending. |
| Console.VerboseDiscovery | 0/1; additional discovery diagnostics/dumps. | `discovery.cpp:401-476`; no parser matrix. |
| Console.InitDelayMs | Integer1-30000; runtime clamps before delayed research startup. Does not postpone EarlyPatch. | `instruments.cpp` startup; boundary test pending. |
| Explorer.EnableExplorer | 0/1; file-command worker; can trigger early research setup even when individual Allow flags are0. | Current parser/start-stop tests absent. |
| Explorer.AllowWrite | 0/1; permits memory-write commands when Explorer active. | Preserved commands; no combined acceptance. |
| Explorer.AllowCall | 0/1; permits call/vcall commands when Explorer active. | Same limitation. |
| Explorer.AllowDebugCommands | 0/1; permits game debug commands and associated discovery. | Same limitation. |
| Explorer.AllowBreakpoints | 0/1; permits breakpoint commands; some ManyLights commands require this too. | Same limitation. |
| Explorer.AllowManyLights | 0/1; permits manual renderer research commands. Automatic production capture does not require it. | Known stale ownership guard, below; all-on unsupported. |
| Explorer.PollIntervalMs | Runtime unsigned value without clamp. Package validator accepts1-4294967294 and rejects busy0/INFINITE4294967295; very long finite sleeps can still delay shutdown. | P rejects0/INFINITE; native boundary/shutdown test absent. |
| Explorer.CmdFile | Package accepts a nonempty local filename with no path separators/Windows punctuation/control characters or trailing dot/space. Native worker accepts a broader string. | P filename grammar; actual file-path/config tests absent. |
| Explorer.ResultFile | Same package filename grammar; must differ from CmdFile ignoring case. | P checks distinct names; actual file-path/config tests absent. |

Research reader evidence: `src/spatial_probe.cpp:851` (series policy),
`src/console/util.cpp:110-155` (parser), `src/console/common.h:43-60` (defaults),
`src/console/explorer.cpp:446` (polling), relative to the native ASI directory.
The native console parser ignores section headers and uses permissive first-letter
booleans. Malformed/empty values can enable features; some missing permission
flags default true. Complete validated templates avoid missing keys, but this
does not repair arbitrary hand-edited ON INIs.

## Concrete defects and exclusions found by this audit

1. Negative `Server.Port` was clamped differently in bootstrap (signed1024) and
   UI (unsigned65535), splitting host and client. The UI now uses the same signed
   clamp;20 synthetic port cases pass. This defect is corrected in source, not
   evidence that every invalid value or installed-game configuration was tested.
2. In the old OFF orchestration, `AmbientProbe=1` selected the actual exclusive
   diagnostic instead of normal capture. Console/Explorer also remained callable.
   Counts/intervals/duration/persistence passed into OFF `spatial_acquire::Start`
   were ignored. The profile split must accompany native OFF guards, not merely
   hide conflicting options in documentation.
3. **Known ON shortcut conflict:** Console's fixed F11 action collides with the
   product's default F11 toggle (`console/signatures.h:118`, `console/console.cpp:130`).
   Reassign `[LightOverlay] OcclusionToggleKey` to an unused Windows virtual-key
   code, set it to0 to disable only that product shortcut, or leave the console
   disabled. Default F11 with both functions enabled is not accepted as two
   independent controls. The OFF profile contains no Console control.
4. Manual ManyLights' ownership guard checks old RVA0x3CB65CA
   (`console/manylights.cpp:35,1975`); current capture uses0x3CB89DA. Do not rely on
   that guard to permit concurrent manual/automatic instrumentation on this build.
5. Research diagnostics are mutually exclusive in some modes. A requirement to
   test all supported production functions together does not make those diagnostic
   modes combinable. Their exclusions, parser/worker gaps and pending live results
   must remain visible until separately corrected and tested.

No option was removed because of an antivirus hypothesis. The production/research
split reflects actual runtime scope and conflicting functions. Current package
and virus-scan results are recorded separately for each exact artifact.

## Checks for this profile change

The previous complete INI was copied byte-for-byte to the research template:
SHA256 `3731CD2796F239CE2B7D5815065715DB5BB00FDCB2A67BB5D3FB7E929BEA2593`.
The standalone package validator's in-memory profile/nondefault/range/companion
controls passed for the earlier 34-key production and60-key research templates.
The builder's isolated preflight (repository path supplied to the extracted
pre-build statements) passes explicit OFF/ON/auto selection and supported
overrides. Five negative cases reject research/console/explorer/legacy HUD keys
in OFF, including a removed key set to0, plus a newline-containing override.
No build command or package write was executed by these preflight checks.

Separately, root reports the OFF native targets build,20 port controls and the
seven existing UI modes pass, and the two new combined UI modes pass in SDR/scRGB
at720p/1080p/4K. The ON ASI builds and its overlay-model controls, including legacy
OcclusionTest, pass. Those are synthetic/source-build results; installed-game
combinations and live per-light acceptance remain subject to the current handover.
No full INI or live all-functions pass is claimed.

## Release 2.2.0 template

The OFF template has 35 settings: `Lights.Upstream=1` was added, and
`SourceVisibility.Enabled=1` now selects the physics ray fan. The package self-test
keeps rejecting Research, Console, Explorer and OcclusionTest in OFF.

## Earlier production candidate follow-up

The OFF template then had 34 settings. SourceVisibility.Enabled, HideOccluded and
OcclusionToggleKey are restored; the ON template remains the preserved 60-setting
research profile. The profile self-test accepts and bounds the three restored OFF
settings while continuing to reject Research, Console, Explorer and OcclusionTest.
The native `release-fire-visibility` control exercises first-volume waiting,
clear/blocked verdicts in front of and behind the camera, disabling and byte-for-byte
preservation of raw scene/light/counter data. Exact package, live and scan status
belong in [the handover](HANDOVER.md) until a new release ledger exists.
