# Simplified Hue lighting controls — 2026-10-16

The normal Hue HUD now has **Lighting, Room, Setup and Advanced**. Lighting shows
Overall brightness, Local light effects, Background light and a Quick / Balanced /
Calm response choice. The current assessment explains missing input, deliberately
disabled background or calculated output. The room preview groups channel output
by light and shows its calculated average channel level as a percentage.
These are command levels, not measured physical brightness or delivery receipts.
Positive levels below one percent display `<1%`; other levels use one decimal.
A physical segment identification test replaces the normal assessment with its
explicit two-second, 10%-white test status.

All 28 detailed mapping controls remain under Advanced, together with presets
and CDT raw input. Advanced shows the current effective values while Recommended
is active, with editing disabled. **Use Custom settings** explicitly restores the
preserved custom mapping. In Custom mode, Lighting hides the inactive Recommended
controls and **Use Recommended** provides an explicit path back. Applying a preset
selects Custom through the controller. Room placement and bridge setup remain separate.

The renderer consumes the new Hue controller fields; it does not implement the
lighting profile, change brightness automatically or infer missing game data.
The Recommended starting profile is deterministic, not per-room calibration.
An older snapshot without a `lighting` object retains the previous detailed HUD
for compatibility. Product validation must use the paired newer Hue controller.

## Snapshot and command contract

- `lighting`: `recommended` boolean, `brightness` 0..1, `localAmount` 0..2,
  `backgroundAmount` 0..1, `response` string (`Quick`, `Balanced`, `Calm`),
  `hasCustom` boolean.
- `assessment`: `code`, `summary`, `detail` strings and `warning` boolean.
- Simple changes: `{action:"lighting", key:"Brightness"|"LocalAmount"|
  "BackgroundAmount"|"Response", value:number}`. Amounts are fractions;
  response is 0 / 1 / 2. The UI displays amounts as percentages.
- Profile changes: `{action:"profile", recommended:boolean}`.
- Calculated output uses the existing channel RGB and each light's segment
  references; no additional telemetry or bridge contract is needed.

Raw snapshots and captured values are shown only inside Advanced for new
controllers. Existing `set`, `reset`, `switch` and preset commands remain available
to compatible older renderers; the controller owns their Custom-mode behavior.
Test selection supports Lighting / Room / Setup / Advanced and nested Output /
Ambient / Color / Space / Presets / CDT raw input (`Raw` is an alias).

## Validation

Final pair: **CDT 2.2.5-huepanel.8 + Hue 0.4.0-asi.18**, production profile,
research OFF, native build 25477059. The previous huepanel.7 package is immutable
and its cursor correction remains included; see [HUE_CURSOR_DRIFT.md](HUE_CURSOR_DRIFT.md).

The expanded shared smoke checks initial Recommended mode, all simple controls
and their effective mapping, rejection of invalid values, exact restoration of
custom controls, user-facing assessment, Point/Wall placement, physical-output
isolation, bounded commands, actual D3D12 rendering, software cursor, hide/reopen,
resize and shutdown. Options `--lighting`, `--advanced`, `--custom`, `--raw` and
`--setup` select isolated screenshots; with no option, it selects Room.

The final native Release build and **13/13 focused input/overlay checks pass**;
logs are `artifacts/simple-lighting-final-build.log` and `simple-lighting-final-tests.log`.
All six shared-renderer runs pass against the exact packaged Hue .asi.18:
Lighting, Custom, Advanced, Setup, Room and CDT raw input. Each reads back
**748434 visible pixels** from a hidden D3D12 WARP window. All six screenshots
were inspected; Lighting/Custom/Advanced also received independent review.
Normal controls fit without scrolling; Custom hides inactive profile values;
the detailed controls and raw input remain under Advanced. No test borrows the
owner's real foreground focus or sends physical lamp output.

Final evidence: `artifacts/simple-lighting-final/`, containing one PNG and log
for each view plus controller/library hashes. Earlier candidate images under
`simple-lighting-preview/` are preliminary and are superseded by the final set.

- Exact tested Hue .asi.18 SHA256:
  `96C651CEE778C17A0452132B1720E9C500F3B83B205B9F6F3AA6834C34275511`.
- Hue .asi.18 ZIP SHA256:
  `022FB7504C5BF53AB336D0E1F3B602918AD89C8C630BB3F0B08AA86B149DDE06`.
- Tested shared renderer library SHA256:
  `B927A1C66A084B8CBF8084BE48B90B444228D80699BA0A03788E9BD9338B0950`.

No real input, saved settings, credentials, game process or lamp output was
changed by these UI tests. Game appearance, cursor stability and physical lighting
remain acceptance work after installation with the game closed.

## Immutable package

Built with `scripts/Build-ModManagerPackage.ps1 -Version 2.2.5-huepanel.8
-NativeBuildId 25477059 -Research off`. Managed/native production builds and the
maintained in-memory profile regression, expanded payload and ZIP validators
pass; see `artifacts/simple-lighting-package.log`. No loose JSON or executable
companions are included. The staged ASI matches the production build, and the
shared renderer library still has the tested hash above after packaging.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.8-ModManagers.zip`.
- ZIP SHA256:
  `7CF3B41B43C48CE75058BEF88AF5F975EC55D65A79D84BE9C066F73F8FCB75F0`.
- ASI SHA256:
  `92B93E9D6D495AB05155FCD891F29B4DE95BFA3988A64BB8D24806632B6772B8`.
- Expanded payload:
  `artifacts/mod-manager/v2.2.5-huepanel.8-20261016-232238-486-9218766e/CrimsonDesertTelemetry`.

The synchronized panel files match their manifest and maintained CrimsonHue
sources. Previous packages remain immutable. This candidate is not installed;
it must be paired with Hue .asi.18 for the validated profile behavior.
