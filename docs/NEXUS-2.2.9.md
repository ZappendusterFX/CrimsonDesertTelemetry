# Nexus copy text — Crimson Desert Telemetry 2.2.9

Prepared text only. The owner publishes Nexus manually; nothing here uploads or
edits Nexus. The full page text is [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

## Field counts

Counts cover the exact one-line text in each block, excluding the final newline.
Version and API file name use the repository's recorded maximum of 50 characters
and allowed alphabets in [NEXUS_PUBLISHING.md](NEXUS_PUBLISHING.md#constraints-worth-knowing).
Summary <=350 and file description <=255 are conservative preparation targets,
not independently verified Nexus field limits.

| Field | Characters |
| --- | ---: |
| title | 24 |
| version | 5 |
| fileName | 34 |
| summary | 173 |
| fileDescription | 204 |
| shortChangelog | 224 |
| changelog | 489 |

## Mod title

```text
Crimson Desert Telemetry
```

## Version

```text
2.2.9
```

## File name

```text
CrimsonDesertTelemetry-ModManagers
```

## Summary

```text
Live lights, player, camera and ambient telemetry for Crimson Desert, with a 3D radar, local APIs, update recovery and optional Hue/OpenRGB menu lighting through CrimsonHue.
```

## File description

```text
CDT 2.2.9 for Steam build 26423880. Adds simulation pause and quiet menu resume. Requires an x64 ASI loader and .NET 8 ASP.NET Core Runtime x64. Optional CrimsonHue 1.0.7 needs .NET 8 Desktop Runtime x64.
```

## Short changelog

```text
Adds simulation-pause detection, fresh-data resume and quiet menu notifications. With CrimsonHue 1.0.7: editable menu lighting, optional Gold preset and OpenRGB reset/cleanup. The controller Skills-menu issue remains unresolved.
```

## Changelog

```text
Added simulation-based acquisition pause and independent /v1/activity HTTP/WebSocket feeds. New captures replace old data on resume. The optional CrimsonHue 1.0.7 HUD adds editable fixed menu color/brightness, Save/Reset, optional Gold preset, OpenRGB reset and disconnected-device cleanup. Intentional pauses no longer show light errors or repeat the ready notice; startup, real reload and error recovery notices remain. The controller-only Skills-menu stall is not fixed by this release.
```

## Requirements and optional companion

- Required: Windows x64, Crimson Desert Steam build 26423880 as the validated
  reference, an x64 ASI loader and Microsoft .NET 8 ASP.NET Core Runtime x64.
  Compatible later address changes may recover; changed layouts remain unsupported.
- Optional: CrimsonHue 1.0.7, downloaded separately; additionally requires
  Microsoft .NET 8 Desktop Runtime x64. CDT works without Hue hardware/controller.
- OpenRGB output: local OpenRGB SDK server and the companion's configured Hue
  bridge/Entertainment area.
- File to upload: `CrimsonDesertTelemetry-v2.2.9-ModManagers.zip`.
- Full page text: [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).
- Longer notes: [releases/v2.2.9.md](releases/v2.2.9.md).

Close the game before installation. Import and activate/deploy the whole ZIP;
keep all six Telemetry runtime files together. DMM may leave older DLL/CFG
companions behind during removal/upgrades. Do not combine old and new companions.
