# Nexus listing copy — Crimson Desert Telemetry 2.2.10

Prepared for manual Nexus publication. Full copy: [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

## Field counts

Counts exclude block delimiters and the final newline. Version/fileName must use
the recorded allowed alphabets and maximum 50 characters. Summary <=350 and file
description/short changelog <=255 are conservative preparation targets, not a
claim about every Nexus field. [Recorded constraints](NEXUS_PUBLISHING.md#constraints-worth-knowing).

| Field | Characters |
| --- | ---: |
| title | 24 |
| version | 6 |
| fileName | 34 |
| summary | 217 |
| fileDescription | 225 |
| shortChangelog | 250 |

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
Live lights, player, camera and ambient telemetry for Crimson Desert, with a 3D radar, local APIs and simulation pause. The optional CrimsonHue companion adds independent Hue, OpenRGB and Tuya Wi-Fi 3.5 room lighting.
```

## File description

```text
CDT 2.2.10 adds shared HUD controls for independent Hue/OpenRGB/Tuya outputs through optional CrimsonHue 2.0.0. Requires an x64 ASI loader and .NET 8 ASP.NET Core Runtime x64; CrimsonHue also needs .NET 8 Desktop Runtime x64.
```

## Short changelog

```text
Adds independent output selection and the Tuya LAN device/room page for CrimsonHue 2.0.0. OpenRGB-only and Tuya-only need no Hue bridge. Existing telemetry/menu-pause behavior remains. No controller Skills-menu fix or live Tuya acceptance is claimed.
```

## Publication handoff notes

- GitHub file: `CrimsonDesertTelemetry-v2.2.10-ModManagers.zip`.
- Automatic recovery was verified on Steam build 26448710 with the installed candidate.
- The validated native reference remains Steam build 26423880 / EXE 1.0.0.3527.
  Compatible address changes may recover; engine layout changes can need an update.
- CDT remains independent of lighting hardware. The optional companion owns output,
  device configuration, credentials and cleanup. Keys never enter CDT's HUD.
- Hue requires a bridge/Entertainment area; OpenRGB requires its SDK server; Tuya
  requires compatible Wi-Fi 3.5 RGB devices with local connection details/keys.
  Tuya 3.3/3.4, Zigbee and Bluetooth are not implemented.
- Tuya uses protected atomic storage, a read-only TCP check, at least 300 ms between
  changed-color writes and independent failure backoff. Restore requires prior-state
  knowledge and a successful ownership comparison; otherwise the last state may remain.
- Install only with the game closed. Keep all six CDT runtime files from the same
  package. Installation and Nexus publication remain the owner's responsibility.
