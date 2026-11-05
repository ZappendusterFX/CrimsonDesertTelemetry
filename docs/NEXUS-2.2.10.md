# Nexus listing copy — Crimson Desert Telemetry 2.2.10

Copy for the Nexus update from **2.2.8 to 2.2.10**. Full listing: [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

## Field counts

Counts exclude block delimiters and the final newline. Version/fileName must use
the recorded allowed alphabets and maximum 50 characters. Summary <=350 and file
description <=255 are conservative preparation targets. [Recorded constraints](NEXUS_PUBLISHING.md#constraints-worth-knowing).

| Field | Characters |
| --- | ---: |
| title | 24 |
| version | 6 |
| fileName | 34 |
| summary | 181 |
| fileDescription | 212 |
| changelog | 876 including line breaks |

## Mod title

```text
Crimson Desert Telemetry
```

## Version

```text
2.2.10
```

## File name

```text
CrimsonDesertTelemetry-ModManagers
```

## Summary

```text
Live light, player, camera and ambient telemetry for Crimson Desert, with all-around light capture, physics visibility, fullscreen markers, a 3D radar and local HTTP/WebSocket APIs.
```

## File description

```text
CDT 2.2.10 adds simulation pause, fresh-data resume and quieter menu notifications since 2.2.8. Automatic recovery verified on Steam build 26448710. Requires an x64 ASI loader and .NET 8 ASP.NET Core Runtime x64.
```

## Changelog since 2.2.8

Paste one entry per line.

```text
Pauses light, ambient and visibility capture and player/camera sampling after 500 ms without simulation progress.
Resumes with fresh captures and clears pre-pause smoothing history.
Adds /v1/activity and /v1/activity/stream for simulation state.
Suppresses false light errors and repeated ready notices on menu resume while retaining genuine error and reload notices.
Stops the simulation observer on normal shutdown whenever it was started.
Adds editable menu color and brightness with Save, Reset and an optional Gold preset through CrimsonHue.
Adds OpenRGB reset and disconnected-device cleanup in the shared HUD.
Adds independent Hue, OpenRGB and Tuya LAN selection with CrimsonHue 2.0.0. OpenRGB and Tuya can each run without a Hue bridge.
Adds a Tuya LAN tab for device setup, 3D placement and per-lamp status.
Verifies automatic update recovery on Steam build 26448710.
```

## Publication handoff notes

- GitHub file: `CrimsonDesertTelemetry-v2.2.10-ModManagers.zip`.
- Automatic recovery was verified on Steam build 26448710 with the installed candidate.
- The validated native reference remains Steam build 26423880 / EXE 1.0.0.3527.
  Compatible address changes may recover; engine layout changes can need an update.
- Optional lighting setup and device requirements are covered by the [CrimsonHue guide](https://github.com/ZappendusterFX/CrimsonHue/blob/v2.0.0/docs/USER-GUIDE.md).
- Install only with the game closed. Keep all six CDT runtime files from the same
  package. Installation and Nexus publication remain the owner's responsibility.
