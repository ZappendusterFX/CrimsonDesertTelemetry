# Public descriptions — 2.2.4, 2026-09-29

The ready-to-paste Nexus BBCode has not been updated since 2.2.1-era text and
is not part of the GitHub-only 2.2.4 release; review it before any Nexus upload.
The general feature draft below describes the 2.2 line as of 2.2.4. The GitHub owner is now
[ZappendusterFX](https://github.com/ZappendusterFX); old `fabianviol` links
redirect, but current texts use the new name. Earlier release notes and the
handover history keep their original links.

## GitHub About text

Live light, player, camera and ambient telemetry for Crimson Desert: all-around engine lights, per-light physics visibility, HTTP/WebSocket API, HUD and 3D radar.

## Nexus description draft — English

The ready-to-paste Nexus BBCode version is
[NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

### Crimson Desert Telemetry

Inspect live lights, player position and the render camera in Crimson Desert.
The mod provides a 3D light radar, fullscreen markers, status notices and local
HTTP/WebSocket APIs for overlays, tools and future lighting integrations.
It does not drive physical lamps itself.

**New in 2.2:**

- **Lights all around you.** Telemetry reads the game's light list before the
  renderer discards what the current view cannot see. Lights behind the camera
  stay in the API, radar and markers. A fire bowl arrives as one light.
- **Which lights actually reach you.** The game's own physics casts nine rays from
  the camera to every nearby light. A wall blocks all nine; a lantern cage only
  some, so the lantern stays visible. The HUD dims blocked lights (F11 shows only
  fresh `clear` ones) and the API reports `clear` / `blocked` / `unknown` per
  light. On by default.

Target: **Crimson Desert patch 2.03.02** on Steam for Windows (EXE `1.0.0.2976`,
Steam build `25477059`). Native capture rejects unknown builds.

The complete source and preserved research are on
[GitHub](https://github.com/ZappendusterFX/CrimsonDesertTelemetry). Testing,
technical ideas, reviews, forks and pull requests are welcome. Evidence should
identify the game build, light type and exact player/light position.

- All current engine lights around the player, including behind the camera; fire
  groups as one light; renderer selection per light.
- Rendered light positions, linear HDR RGB, luminance and recognized spotlight
  directions/cone angles; separate authored light records.
- Per-light physics visibility: `clear` / `blocked` / `unknown`, ray counts and
  measurement age.
- A separate grouped/EMA light feed preserving the original contributions.
- Player root and independent render-camera position, orientation and projection.
- Global sky RGB; camera-local sky exposure and a derived ambient estimate when
  available.
- Configurable HUD/radar, fullscreen markers and startup/error notices.
- Local HTTP snapshots, health, schema and WebSocket streaming, default port 27311.

Visibility is a sampled collision estimate, not how much light gets through.
Thin gaps can make a light behind a fence clear, and geometry without collision
does not block. By default unknown lights stay visible; F11 (`HideOccluded`)
shows only fresh `clear` results. Raw and smoothed API records and RGB values are
never removed; the separate `/v1/lights/visible` feed contains only `clear`
contributions. `[SourceVisibility] Enabled=0` switches it off.

Camera-local sky visibility and its derived estimate are optional, nullable
fields. Since 2.2.4 a startup race no longer leaves them unavailable for a whole
session.

### Controls

**F8** toggles the corner HUD/radar, **F9** details, **F10** fullscreen markers,
**F11** toggles showing only fresh `clear` lights (by default blocked lights are dimmed).
Each accepts a decimal Windows virtual-key code; **0** disables that shortcut.
Keys do not stop telemetry streaming.

| INI section | Key | Default |
| --- | --- | --- |
| Overlay | ToggleKey | 119 / F8 |
| Overlay | DetailsKey | 120 / F9 |
| LightOverlay | ToggleKey | 121 / F10 |
| LightOverlay | OcclusionToggleKey | 122 / F11 |

`[LightOverlay] Radius=100` is shared by markers, radar and visibility rays.
Automatic hiding in every menu is not implemented; use F8/F10.
Layout, scale, opacity, HDR white, marker limits, sampling and smoothing remain
configurable. INI changes need a game restart. See
[configuration](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/main/docs/INI_VALIDATION.md).
Research, Console and Explorer switches cannot activate through an old INI in the
production build; their separate research profile remains in source.

### Requirements and installation

Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), one x64 ASI loader and the
supported game build. No NVIDIA, DLSS, Streamline, Nsight or PIX runtime dependency.
Automated D3D12 tests cover SDR/HDR10/scRGB; live HDR-display, frame-generation and
AMD/Intel game acceptance remain pending.

Close the game, back up useful INI preferences, disable previous telemetry packages
and any separate legacy console ASI, then import and enable the complete ZIP
through DMM. Verify all six runtime files beside the game EXE, from the same version:

```text
CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg
```

Keep .cfg names unchanged. Migrate preferences into the supplied INI. Local DMM
2.8.1 and 3.1.1 testing showed that removal/upgrades can leave both Telemetry DLLs and both
lowercase CFG files behind. With the game closed, remove the old package in DMM,
delete only those four leftover Telemetry files, then import the new ZIP. Do not
delete the ASI loader. A clean import then deployed all six current files exactly.
JSON Mod Manager/manual-loader lifecycle validation remains pending.

### Data limits

Authored, rendered and upstream arrays overlap; do not add them together. One lamp
can produce several rendered contributions. The upstream list covers the lights the
game currently hands to the renderer; it is not a persistent world registry, and a
missing light does not prove OFF. Linear HDR RGB is renderer data, not lumens or
final screen pixels. Fast motion can expose projection latency. Display spot
lengths are schematic.

The host reads memory; the plugin uses guarded renderer/D3D12 hooks, GPU copies and
game physics queries. No gameplay-control API or anti-cheat bypass. Exact scans
apply only to the hashes in the versioned validation record.

[Source](https://github.com/ZappendusterFX/CrimsonDesertTelemetry) and [API](API.md).
Created by ZappendusterFX with Claude and Codex (OpenAI). Unofficial; not affiliated
with Pearl Abyss. MIT source; dependencies retain their notices.
