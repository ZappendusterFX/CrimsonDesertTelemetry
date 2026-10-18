# Nexus copy text — Crimson Desert Telemetry 2.2.5

The owner publishes Nexus manually. Nothing in this file uploads or edits Nexus.
The full page description is [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

## Field counts

Counts below cover the exact one-line text inside each block, excluding its final
newline. The repository records a **50-character maximum for version and API file
name**, plus the allowed file-name alphabet, in [NEXUS_PUBLISHING.md](NEXUS_PUBLISHING.md#constraints-worth-knowing).
The short texts conservatively target summary <=350 and file description <=255
characters; those two thresholds are preparation targets, not independently
verified Nexus field limits. Full BBCode and release notes are separate texts.

| Field | Characters |
| --- | ---: |
| title | 24 |
| version | 5 |
| fileName | 34 |
| summary | 161 |
| fileDescription | 196 |
| changelog | 580 |

## Mod title

```text
Crimson Desert Telemetry
```

## Version

```text
2.2.5
```

## File name

```text
CrimsonDesertTelemetry-ModManagers
```

## Summary

```text
Live light, player, camera and ambient telemetry for Crimson Desert, with all-around lights, HTTP/WebSocket APIs, a 3D radar and optional CrimsonHue integration.
```

## File description

```text
CDT 2.2.5 for Steam build 25477059. Needs an x64 ASI loader and .NET 8 ASP.NET Core Runtime x64. Optional CrimsonHue 1.0.0 is a separate download and additionally needs .NET 8 Desktop Runtime x64.
```

## Changelog

```text
Added optional CrimsonHue 1.0.0 mixer integration using CDT's existing renderer. Separate Hue controller/download; CDT still works independently. Lighting, Room, Setup and Advanced tabs; point/spot/own-lantern weights; segmented and horizontal washer calibration. Hue X/Hide/shortcut preserve sync; use Stop to end it. Input handoff and cursor corrections included. Raw telemetry, HTTP API v1 and schema 1.6 unchanged. Clarified physics visibility limits: dense cages and colliding glass can exclude visibly shining lamps. Optical renderer-derived contribution remains unfinished.
```

## Requirements and optional companion

- Required: supported Crimson Desert Steam build, Windows x64, an x64 ASI loader,
  and Microsoft .NET 8 ASP.NET Core Runtime x64.
- Optional: CrimsonHue 1.0.0, downloaded separately; additionally requires
  Microsoft .NET 8 Desktop Runtime x64. CDT does not require Hue hardware/controller.
- File to upload: `CrimsonDesertTelemetry-v2.2.5-ModManagers.zip`.
- Full page text: [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).
- Longer release notes: [releases/v2.2.5.md](releases/v2.2.5.md).

Close the game before installation. Import and activate/deploy the whole ZIP;
keep all six Telemetry runtime files together. DMM may leave older DLL/CFG
companions behind during removal/upgrades. Do not combine old and new companions.