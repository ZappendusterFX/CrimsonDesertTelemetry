# Independent output HUD candidate — 2026-11-04

Private pair: **CDT 2.2.10-outputs.1 / CrimsonHue 1.0.8-outputs.1**.
CDT hosts the shared output selection and Tuya page. Hue owns all lighting
backends; CDT does not receive Tuya addresses, device IDs or local keys.
Telemetry acquisition/input behavior is unchanged. This does not fix the
controller-only Skills-menu bug.

Production package uses **Research OFF, native build 26423880** and the normal
production INI (Pause.Enabled=1, Diagnostics.SimulationTicks=0). The supported
reference remains executable SHA-256
`DC4237F21D6CE1E49F8E5D6FA89BF803FBBE6549123078EFDC393265C7BF6841`;
synthetic validation is not new game-build acceptance.

## Evidence

- Maintained builder and package validator/self-test pass; nine expected files,
  one package root, no loose JSON/EXE or credentials.
- **45/45 production native tests pass** in 41.44 seconds:
  `artifacts/independent-outputs-native-tests.log`.
- Final production shared renderer and exact packaged Hue ASI pass OpenRGB-only
  and Tuya-only D3D12 smokes, including physical-output isolation, commands,
  preview rendering, resize and shutdown. Logs/screenshots:
  `artifacts/independent-outputs-packaged-{openrgb,tuya}.*`.
- Setup and Lighting render paths also pass; screenshots visually checked.
- Paired Hue **214/214 Core tests**, exact packaged controller smoke and WPF
  setup rendering pass. CDT managed code is unchanged and was not retested.
- Shared panel copies/manifest and AGENTS/CLAUDE parity checked.

No installation, physical lamp commands, game/input acceptance or live
three-backend test. Previous public versions remain immutable and published;
these candidates and new Nexus drafts were not uploaded.

## Package

- Source: `72d699a`.
- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.10-outputs.1-ModManagers.zip`.
- Size: **1,186,599 bytes**.
- SHA-256: `15557E07FBACF886113BA4297E57D9C23836ACCB4126366369EEC072490AC620`.
- Adjacent `.sha256`; per-file receipt:
  `artifacts/independent-outputs-receipt.json`.
- Required partner: `C:/DEV/CrimsonHue/dist/CrimsonHue-1.0.8-outputs.1-asi-win-x64.zip`
  (SHA-256 `4A415B51B25CD098D18FBF368E27A2AF180DDD1C7F03978AD859526FC870B579`).

Owner-only installation: stop sync, close game and install both matching ZIPs
through DMM. Resume acceptance only after the owner reports installation/restart.
