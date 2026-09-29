# Changelog

## 2.2.4 - 2026-09-29

Release notes: [v2.2.4](docs/releases/v2.2.4.md). Target remains Crimson Desert
patch 2.03.02 (EXE `1.0.0.2976`, Steam build `25477059`).

- Record the camera sky-visibility copy only after the render submission hook has
  observed a submission. A copy recorded during loading, before that hook existed,
  could stop camera-local sky visibility and the local ambient estimate for the
  whole session.
- Abandon a sky-visibility copy that stays unsubmitted for 5 s, whose fence
  `Signal` fails, or whose fence stays incomplete for 5 s. Its readback buffer
  stays reserved, at most 8 per process. A device-removed fence value is no
  longer treated as a completed copy.
- Take the submission hook from the game's own presentation queue instead of
  creating a D3D12 queue at world entry. The previous method remains the
  fallback when all UI is disabled or the queue belongs to another device.
- Correct the INI, packaged README and public texts: F11/`HideOccluded` shows only
  fresh `clear` lights, and the DMM leftover note also applies to DMM 3.1.1.
- Routes, schema 1.6 and INI values are unchanged.

## 2.2.3 - 2026-09-29

Release notes: [v2.2.3](docs/releases/v2.2.3.md).

- With `HideOccluded=1`, HUD and radar show only fresh `clear` lights. Blocked,
  unknown, missing and stale results are hidden, and the 250 ms nearby-blocker
  hint was removed.
- Add `/v1/lights/visible` over HTTP and WebSocket with only `clear`
  contributions before grouping and smoothing. Raw and smoothed feeds are unchanged.
- The native HUD checks filtered visibility on the original record without
  per-marker copies.

## 2.2.1 - 2026-09-27

Release notes: [v2.2.1](docs/releases/v2.2.1.md). Target remains Crimson Desert
patch 2.03.02 (EXE `1.0.0.2976`, Steam build `25477059`).

- Select the validated native physics-query context for continuous source
  visibility. This prevents the synchronized false-clear rounds observed in 2.2.0;
  the owner confirmed the focused private fix in game. Individual moving-light
  flicker remains a separate open issue.
- Compare canonical D3D12 device identity through ReShade's native-device proxy
  when present. The ReShade 6.8 capture-error-6 case passes an offline WARP test;
  unrelated devices still fail closed.
- Add a local ManyLights RGB modulator bridge for a separate companion. GPU upload
  memory remains owned until its submitting queue's fence completes, and the
  bridge disengages after its heartbeat expires. Light switching is not a
  supported telemetry feature in this release.
- Retain the D3D12 probe queue for the ASI process lifetime and log capture
  preparation stages. This removes the `Release()` call where a hung game dump
  found CDT's worker blocked. Whether that call caused the sporadic GPU hang is
  still unproven.
- Keep the HTTP/WebSocket routes, schema 1.6 and production INI defaults.

## 2.2.0 - 2026-09-26

Release notes: [v2.2.0](docs/releases/v2.2.0.md). Target unchanged: patch 2.03.02
(EXE `1.0.0.2976`, Steam build `25477059`).

- Add the all-around engine light stream. With `[Lights] Upstream=1` (default)
  every light sample also copies the ManyLights input on the same command list and
  fence, using exact-build anchors and an enhanced-barrier round trip. The render
  bridge becomes v4 (v3 prefix unchanged plus an input block). The host decodes it
  with the consumer's rules: fire groups become one summed light at the member
  mean, group members are not repeated, special view-space records are excluded
  and counted, and colour uses the renderer's floor and matrix.
- Publish `lights.upstream` in snapshot schema 1.6 and add capability
  `lights.upstream`. Each light carries `rendererSelected`, paired with the
  filtered output of the same capture.
- Move physics source visibility into the production build, on by default. Nine
  collision rays per light from the camera; any clear ray means `clear`, all nine
  blocked means `blocked`, anything missing or older than 500 ms means `unknown`.
  Targets are the upstream lights within `[LightOverlay] Radius` (100 in the
  supplied INI, shared by markers and radar) and include lights behind the camera.
  Manual probes and research diagnostics stay in `CDT_RESEARCH=ON`.
- HUD, radar and markers show the all-around list: filled = renderer-selected,
  hollow = not selected in this view. Detail cards show groups and selection.
  Blocked lights are dimmed; F11 hides them. Unknown lights are never hidden.
- The smoothed feed uses the upstream lights when available, with new source and
  coverage labels; otherwise the rendered output as before.
- Raw and authored records, rendered RGB values and routes are unchanged.

## Unreleased — production ambient and per-light occlusion

- Extend automatic compatibility to the direct camera layout, so the camera stays
  available after a game update that moves code and data without changing the
  camera object layout. `build-25477059.json` becomes the direct-layout template:
  scene global from its RIP-relative load pattern, scene vtable from two slot
  fingerprints (the class has no MSVC RTTI locator). Relocated profiles still drop
  `engineLights` and `nativeCapture`, keep all live decode/coherency checks and are
  labelled `automatic`, never `tested`. Offline, the resolver reproduces the
  validated 1.0.0.2976 profile exactly.
- Fix ambient input layout: retain the full contiguous 768-byte GI context and
  respect padded GPU readback rows. Decoder offsets/math stay unchanged. Camera-local
  Ambient Occlusion passed a controlled open/enclosed/open route in OFF v2.1.9
  on Steam build 25246367 on 2026-09-12.
- Add bounded production SDF acquisition and per-contribution
  `sourceVisibility` metadata. Raw light records, RGB/luminance and the smoothed
  feed's EMA values remain intact. Off-screen/behind-camera targets are supported
  when present in the current captured source list; it remains view-filtered.
- Add clear/blocked/unknown HUD/radar presentation and optional hiding of fresh
  blocked sources, toggled by default F11. All four HUD shortcuts (F8-F11) can be
  reassigned using decimal Windows virtual-key codes or disabled separately with
  `0`. Display hiding does not filter either API feed.
- Keep production acquisition in `CDT_RESEARCH=OFF` with bounded current data;
  private probe histories and capture dumps remain in the preserved research path.
- Give production and research builds separate INI templates. OFF ignores
  Research, Console, Explorer and legacy `OcclusionTest` entry points, including
  old INIs that enable them; ON retains the experimental behavior. This corrects
  conflicting or inactive configuration options and prevents diagnostic modes
  from replacing product streams. It is not an antivirus mitigation.
- Align HUD client and host port handling: negative or out-of-range configured
  ports use the same signed 1024–65535 bounds across all UI combinations.
  See [INI validation](docs/INI_VALIDATION.md) for configuration dependencies and
  synthetic coverage. Live acceptance with all production features enabled is
  still pending; automatic hiding in every game menu is not implemented.
- The new per-light path has synthetic coverage; real visible/blocked/visible
  and behind-camera acceptance and exact final ASI/ZIP virus checks remain
  outstanding. This entry is development status, not a published or final release.

## 2.0.2 - 2026-09-11

Compatibility release for Crimson Desert Steam build `25246367` (executable
`1.0.0.2850`). 2.0.0 fails closed on that build: it reports `unsupported-build`,
installs no hook and captures nothing. This release restores the 2.0.0 feature set
on the updated game and adds no new promised behavior.

- Validate and hook the new executable. `ProcessManyLights` moved by `0x2410`; its
  13,607-byte body keeps the same 2,890 normalized instructions, register lifetime
  and binder/Dispatch contexts, so the hook relocates to RVA `0x3CB89DA` rather
  than being re-derived. Camera and player anchors relocated; the scene vtable is
  `0x5C04768`; the RTTI-guarded player chain and all known object offsets still
  validate. Runtime preflight remains byte-exact and unknown executables are still
  barred from native hooks.
- Fix the update checker reporting the native hook missing on a relocated build.
  It compared a `call rel32` literally, which changes whenever the target moves;
  the offline finder now anchors that hook to its unique dispatch context.
- Native instrumentation is exact-executable and is built for `25246367` only.
  On Steam build `25116796` this package reports an unsupported build and installs
  nothing; 2.0.0 remains the version for that older build.
- Ship the new global ambient feed disabled. `[Ambient] Enabled=0` installs no sky
  hook, and `[Overlay] ShowAmbient=0` keeps it off the F9 diagnostics page. The
  feed is not part of the published feature set and is not validated on this build.
- The derived smoothed-light stream stays available and unchanged; raw API values,
  the HUD and `/v1/stream` are untouched by it. Private research switches under
  `[Research]` and the experimental `[LightOverlay] OcclusionTest` remain off.
- Verified before release: 25 native CTest paths, 66 managed tests, package
  validation including negative cases, the read-only offline anchor check against
  the new executable, and a live in-game run on `25246367` — `/v1/health` reporting
  `supportedBuild` with `compatibility.mode "tested"`, progressing capture and frame
  sequences, fresh paired copies at 0-47 ms and zero malformed records.
- Compile the private research instrumentation out of released packages. The
  spatial probe's GPU readback and barrier observation and the experimental
  line-of-sight test are reached by no released feature, and a published binary
  should not carry hooking and copying machinery it never runs. The new
  `CDT_RESEARCH` build option keeps them in development builds, where the default
  is unchanged; no source is removed and their tests still run against the real
  code. The released ASI drops from 1,563,648 to 1,385,984 bytes.
- 2.0.1 was built and rejected by the file host's antivirus scan before it reached
  anyone; nothing was published under that number. Its package is preserved
  unchanged as the record of what was rejected.

## 2.0.0 - 2026-09-06

- Publish current filtered renderer light contributions through HTTP/WebSocket:
  world position, linear HDR RGB, derived luminance and recognized spotlight
  direction/cone. Authored source records remain a separate feed; the two overlap
  and must not be summed. HTTP routes remain `/v1/*`, with additive schema 1.4
  when lights are enabled and schema 1.1 when disabled.
- Enable lighting, the corner HUD with a larger 3D light radar, fullscreen light
  markers and notifications in the package INI. F8 toggles the corner HUD, F9
  diagnostics and F10 markers. The radar uses camera pitch/roll and projection;
  nearby contributions share detail panels while retaining individual raw values.
- Add D3D12 HDR10 and scRGB rendering for the HUD, markers and notices, alongside
  8-bit and 10-bit SDR. Select the path from the game's format/color space without
  changing its settings or metadata. Composite HDR UI in linear light, preserving
  pixels outside the UI and scene alpha. `[Overlay] HdrPaperWhiteNits=200` adjusts
  all HDR UI white brightness within 80–500 nits. Three new automated HDR tests
  pass; live HDR-display/game validation has not been performed.
- Pair GPU light readback with the renderer's valid-prefix counter, completion
  fence and captured camera. Decode only that current prefix, fixing retained
  buffer-tail ghost contributions from 1.3.0-preview.1/2. GPU sample indices are
  transient slots, not physical-light identities.
- Unify telemetry bootstrap, guarded native renderer capture and the optional
  research console into one ASI. Console/explorer remain disabled by default.
  The external host reads memory; the native light path uses hooks and GPU copies.
- Keep normal startup/loading notices silent. Success starts when the API reports
  playing and requested data is fresh, including during visible loading; its INI
  duration is 6000 ms. Actionable local/host/native errors can appear immediately.
  The user accepted this behavior and several seconds of retained HUD/data after
  returning to the title screen.
- Generate managed/native build contracts from validated definitions, reject
  malformed profiles, and check exact native hook/caller contexts before patching.
  Add offline `check-update <exe>` diagnostics and preserved recovery helpers;
  unknown executables remain barred from native hooks, with no automatic promotion
  of current direct-layout candidates. Native tests now run in normal PR CI.
- Before the HDR addition, verify 56 managed tests, HTTP/WebSocket smoke,
  11 native CTest paths and package validation. A fresh installed 2.0.0 start passed a progressing player/camera/light
  API check and user acceptance. No DLSS/NVIDIA runtime dependency is required;
  AMD/Intel in-game setups remain untested. The final native Release rebuild and
  expanded 14-test suite pass, including the three new HDR paths and recovery
  across SDR/scRGB transitions on the same swapchain.

### Authored spotlight direction (developed before 2.0.0)

- Add a normalized world-space `direction` for authored spotlights by rotating
  their verified local +Z cone axis with the source-record quaternion. Point
  lights and invalid rotations omit it. This introduced additive schema 1.3;
  2.0.0 light snapshots use schema 1.4 and the disabled-light schema remains 1.1.

The entries below preserve the behavior and evidence of earlier releases.
Old HUD defaults, light exclusions and preview acceptance gates are historical;
the Unreleased section and current validation documents describe development status.

## 1.2.1 - 2026-09-04

- Correct the player position for Steam build `25116796`. The globals used since
  1.2.0 hold the renderer camera position, not the player's; they were
  bit-identical to `camera.position` and about 6.5 units from the player. The
  genuine player global was found by searching the executable for its structural
  shape, an 8-byte RIP-relative store paired with a 4-byte store into the same
  global plus eight, because the exact byte pattern no longer matched after the
  update. Exactly one of 4816 candidates is chunk-aligned to the physics root; it
  was live-validated over 27 units of player movement.
- Publish player orientation again for `25116796`, and withdraw the SQT
  player-transform claim from 1.2.0: `owner+0xE08` resolves to a zeroed object
  holding neither a position nor a rotation. The genuine transform is the
  unchanged `basis-v1` physics layout; only the physics link moved, from
  `owner+0x298` to `owner+0x2B8`. Verified live over 22 samples in which the
  player heading swept 355 degrees while the camera held still for seven of them,
  with the root basis upright throughout.
- Stop dropping the orientation while the player moves fast. The player global
  and the physics object are written at different points within a frame, so 3.6%
  of samples taken while riding saw them differ by one frame of travel, peaking
  at 0.28 units against a 0.15-unit tolerance. That tolerance is an identity
  check, not a freshness check, and is now two units: still rejecting any
  unrelated object by a wide margin, while no longer discarding an orientation
  for being one frame old. An immediate re-read was tried and removed, having
  rescued none of eighteen measured rejections.
- Report why an orientation is missing instead of returning a silent null. The
  pointer chain names the failing hop and its offset, RTTI mismatches report
  expected and actual type, and quaternion or position rejections include the
  values. `snapshot` writes the reason to stderr, leaving JSON on stdout
  unchanged.
- Add a Nexus Mods publishing workflow, scripts and documentation. Nothing
  publishes by accident: manual runs default to a dry run, and a published GitHub
  release is ignored unless the repository variable `NEXUS_AUTO_PUBLISH` is set.

## 1.2.0 - 2026-09-04

- Add opt-in, read-only nearby engine-light telemetry for the exactly validated
  `25050808` and `25116796` executables. The module reads the CPU source array, re-resolves and
  double-checks its complete pointer walk per snapshot, retries once, and fails closed.
- Publish verified position, point/spot kind when known, linear color, record/renderer
  flags, and conditional renderer scale/final linear RGB. Do not claim range, lumens,
  generic enabled state, direction, fire/effect lights, or durable source identities.
- Keep schema 1.1 and its payload unchanged while lights are disabled; opt-in light
  snapshots use additive schema 1.2 and separate diagnostic counters.
- Restore exact player, orientation, camera and light support after Steam build
  `25116796` / EXE `1.0.0.2760`, using the newly validated direct scene/camera
  root and SQT player transform. Unknown builds continue to fail closed.

## 1.1.0 - 2026-09-01

- Keep exact SHA-256 matches as explicitly tested builds, while allowing a different
  game EXE only when the complete reference layout can be relocated unambiguously.
- Resolve and validate player/static globals, both camera globals and two independent
  multi-slot object-table fingerprints from the executable. Retain all live pointer,
  RTTI, basis, projection, coherency, freshness and proximity checks; failures remain closed.
- Add health `compatibility` metadata that distinguishes `tested` from `automatic`,
  reports the actual EXE identity and names the manually validated reference layout.
- Add the offline `check-compatibility <exe>` diagnostic and synthetic regression
  coverage for relocation, ambiguity, malformed images and invalid section/target classes.
- Steam build `25050808` / EXE `1.0.0.2692` was accepted automatically from the
  original reference layout, then locally validated with a fresh launch and controlled
  independent camera/player movement; it is now also an exact known build.

## 1.0.0 - 2026-08-31

- First public release package: player position/root orientation, native render-camera
  position/basis/projection, loopback HTTP/WebSocket API and JSON Lines output.
- Product version 1.0.0 retains the existing HTTP v1 endpoints and JSON schema 1.1.
  No payload or endpoint migration is required for consumers of the development builds.
- Native camera reads now use guarded game globals, independent of DLSS/Streamline
  camera copies. Cold-start/upscaling-off and in-game restart tests passed on the
  development NVIDIA setup; other GPU hardware remains untested.
- Dear ImGui HUD is opt-in: `[Overlay] Enabled=0` by default, including when the
  INI/section/key is missing. No HUD hooks, hotkeys or HUD client start in this mode.
  `[Server] Enabled=1` remains the default; telemetry runs without the HUD.
- Set `[Overlay] Enabled=1` and restart to enable the HUD. `InitiallyVisible` and
  F8 control visibility only after opt-in. Existing customized INIs are not overridden.
- Light-source research is excluded. Unknown game builds are rejected. The optional
  HUD supports D3D12 / 8-bit SDR; HDR, frame generation and recording remain unverified.

### Development preview history (superseded where noted above)

- Preview.6: replace majority-only tracking with temporal activity selection over
  renderer copies, keeping the newest observed update among regularly changing
  sources. Retain learned sources during stillness; rediscover if they are lost.
- Preserve raw camera turns, reversals, position and projection. No display smoothing
  or fixed process addresses. Quality copy counts describe the selected state.
- Add nine targeted selection checks and optional production-code replay. The local
  controlled 3600-sample right-turn trace yields 803 direction changes without
  backwards steps or unavailable samples; renewed in-game acceptance remains pending.

- Preview.5: resolution-aware HUD size, matching font-atlas resolution, stronger
  secondary text contrast and configurable `AutoScale` (enabled by default).
- Add automatic 4K/resize graphics coverage and HUD scale calculation tests.
- Add a read-only stream recorder for controlled camera-lag investigations.
- Document Streamline provenance and unverified GPU/DLSS coverage.

- Preview.4 candidate: optional English Dear ImGui in-game HUD (D3D12 / 8-bit SDR),
  independent camera/player-root directions, position, FOV and diagnostics.
- Add configurable passive HUD hotkeys, source-age validation and unavailable-data states.
- Add pinned native dependencies and bundled third-party license notices.
- Add native parser tests and an isolated, hidden-window D3D12 graphics smoke test.
- Light-source research remains excluded; the HUD is visible in-game, but complete
  in-game/recording acceptance is pending.

- Add schema 1.1 player physics-root orientation (`forward`, `up`, optional heading) with per-frame availability semantics.
- Serialize concurrent ASI bootstrap startup so duplicate loader instances cannot launch competing telemetry hosts.

- Added a preview ASI bootstrap and mod-manager packaging for the existing read-only host.
- Fixed DMM misclassifying .NET dependency/runtime JSON as mod patches: the manager
  package now uses .cfg companions selected explicitly by dotnet exec.
- Added exact package-content and embedded-resource regression checks.
- Added read-only support for Crimson Desert Steam build `24994088`.
- Added version-bound static player-position discovery.
- Added address-independent render-camera structural discovery.
- Added redundant-copy consensus and automatic rediscovery.
- Added the versioned community JSON contract and JSON Lines CLI output.
- Added loopback HTTP snapshot/health/schema endpoints and a WebSocket stream.
- Added configurable 1–240 Hz shared sampling and automatic game-process reconnects.
- Classified the observed origin/height-1000 startup sentinel family, including floating-point noise,
  as loading state.
- Added offline validation and local API integration tests.
