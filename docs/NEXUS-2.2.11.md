# Nexus file copy for Crimson Desert Telemetry 2.2.11

The previous active Nexus file is **2.2.10**. Use version `2.2.11` and the existing file name `Crimson Desert Telemetry - ASI Package`.

## File description

```text
CDT 2.2.11 starts directly on Steam build 26448710. Guarded menu detection enables Skills menu lighting and allows capture to continue through cutscenes with stalled ticks. Requires an x64 ASI loader and .NET 8 ASP.NET Core Runtime x64.
```

## Changelog since 2.2.10

Paste one entry per line.

```text
Starts directly on Steam build 26448710 (EXE 1.0.0.3549) after the compatibility checks.
Uses the game's guarded menu state to pause capture and activate optional CrimsonHue menu lighting in Skills.
Keeps capture enabled in tested cutscenes, Kuku Shop and Kuku Pot crafting, including when simulation ticks stop.
Keeps simulation ticks observable independently as simulationState in the activity API.
Recovers the menu signal after compatible updates and keeps capture enabled if it cannot verify the menu state.
```

Upload `CrimsonDesertTelemetry-v2.2.11-ModManagers.zip` from the GitHub release.
