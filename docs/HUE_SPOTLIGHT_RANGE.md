# Hue spotlight range — 2026-10-18

Owner requests a larger Spotlight control range because200% only starts to have
an effect. The maintained panel raises that slider to1000%; matching Hue.asi.24
raises Core/Recommended validation and Advanced Output to the same bound. Point
and own-lantern ranges, defaults and direct mixing behavior remain unchanged.
No API, acquisition, grouping, input or cursor logic changes.

Point/spot describes shape; standalone/group describes aggregation. Neutral
capture8934 has48 grouped/35 standalone point sources and31 grouped/5 standalone
spots. It is incorrect to equate Point lights with groups or grouped lights with
fire. Both standalone and grouped spot sources receive the Spotlight multiplier.

Fresh pair **CDT2.2.5-huepanel.14 + Hue0.4.0-asi.24**, production/research OFF,
native build25477059. Maintained source is the parent Hue repository's
`native/CrimsonHue.Panel`. Core124/124, exact packaged controller, both expanded/ZIP
validators and exact-Hue hidden shared D3D12 WARP smoke pass:1000% command/range,
31 controls, profile/custom preservation, physical-output isolation,748,434
visible pixels, hide/reopen, resize and shutdown. Image reviewed. Sources and
SOURCE-SHA256 match. Previous broader .13 native regression evidence remains
in HUE_SOURCE_WEIGHTS.md. No install, real settings/input/process writes or
physical output. Next: owner exits game and installs
both matching packages. Detailed limits and hashes: parent Hue
`docs/SPOTLIGHT-RANGE-VALIDATION.md`.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.14-ModManagers.zip`.
  SHA256 `8AF50F2810A9093DCF87FAC953C2827CB7282A8837641DAC88B3635710C1D1C5`.
- ASI SHA256 `32683DB18C8744BA33E70F32510D6C9DEE029C6256D9DB43FD7B843D370C3501`.
- Tested renderer library SHA256
  `2483D4675BFB78ED76B20DFF8B0822972E123E9F5472FC02CC0D01BFB96FCD8F`.
- Hue ZIP SHA256 `2943880DFA646870D8E20C6723BCF876BA7AB156C7E62EC600F208CCACC0CFD5`.
- Logs: `artifacts/spotlight-range-package.log`, `spotlight-range-shared-smoke.log`;
  image `artifacts/spotlight-range/lighting.png`.
