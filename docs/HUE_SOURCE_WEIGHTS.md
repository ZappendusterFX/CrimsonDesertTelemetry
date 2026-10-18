# Hue light balance controls — 2026-10-18

The owner requests both independent weighting of the raw light kinds and an
additional adjustable strength for their equipped lantern. The maintained Hue
panel exposes Point lights / Spotlights (0–200%, initially 100%) and Own lantern
(0–100%, Recommended starts at 10%) in both Recommended and Custom. The own
multiplier compounds with the spotlight weight; zero excludes the recognized
pair. The matched count describes current input, rather than measured delivery.

The actual mapping, optional source metadata, conservative build-25477059
lantern signature and persistence live in the separate Hue consumer. CDT's
neutral API/acquisition is unchanged. No semantic fire/blue-light types or engine
owner IDs are invented. Grouped/standalone contributions share their point/spot
weight; unknown kinds remain neutral. Unsupported/partial/ambiguous lantern
signatures remain at their ordinary type weight. Hue's 31 numeric controls and
version-2 presets include all three values; version-1 imports stay compatible.

Room labels the existing per-segment span **Segment width** and explains horizontal
washers: distinct Side offsets with equal Height angles. Channel identities and
personal calibration are preserved. No input/cursor/acquisition logic is changed;
the .10 handoff and .11 native cursor correction remain.

Maintained source is `C:\DEV\CrimsonHue\native\CrimsonHue.Panel`. The fresh pair is
**CDT 2.2.5-huepanel.13 + Hue 0.4.0-asi.23**, production/research OFF, native game
build **25477059**. The earlier unshipped checkbox candidate (.12/.22) remains
preserved under ignored artifacts; it is superseded by these three sliders.

Hue Core **123/123**, exact packaged controller, both expanded/ZIP package
validators and CDT **12/12** focused native regressions pass. Exact packaged Hue.23
passes hidden shared D3D12 WARP smoke in Recommended and Custom: 31 controls,
weight commands, independent mode preservation, pixel readback (748,434 visible
pixels), hide/reopen, resize and shutdown. Both screenshots reviewed. Maintained/
imported sources and SOURCE-SHA256 match. See the parent Hue repository's
`docs/SOURCE-WEIGHTS-VALIDATION.md` for controlled on/off/on presence evidence and
recognition limits.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.13-ModManagers.zip`.
  SHA256 `7CF6BFEB11D344069D629DAA5E8EBBBE79469A7178DDFFC194F80634BB616D8E`.
- ASI SHA256 `232A2904E419507F4033D197EA2D80C684A161E0D97966AEB0AE3A162EA707F6`.
- Tested renderer library SHA256
  `B7D6EA6AACBD25D687DEF061F591CDDB21E1915620F66EE75CD7AA82DE2A3D89`.
- Hue ZIP SHA256 `C88F87B6F1098A34AFE6E470B3EE412716910E5E8CE9DE7D3182E1DB247BB429`.
- Logs: `artifacts/source-weights-package.log`, `source-weights-native-tests.log`,
  `source-weights-shared-smoke.log`, `source-weights-custom-smoke.log`.
  Images: `artifacts/source-weights/lighting.png` and `custom.png`.

No install, real settings/input/process writes or physical
output were performed. Next: owner exits game, installs the matching pair and
assesses light balance plus horizontal washer calibration.
