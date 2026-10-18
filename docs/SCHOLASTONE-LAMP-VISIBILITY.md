# Scholastone caged lamps — 2026-10-18, Codex

## Established failure

The owner reports visible cool-blue wall fixtures at Scholastone Institute, but
almost no corresponding Hue output. Five source positions project onto five
fixtures in the supplied screenshot. All are present in both the paired upstream
input and renderer-selected feed, as `type: standalone`, `kind: point`. Each has
a fresh `physics-ray-fan` verdict of `blocked`, with **0/9 clear rays**. This is
not a missing light kind. The earlier blue table lantern was a spotlight at a
different location; color does not identify the geometric light kind.

Two passive captures at the same player position
`(-11954.755, 835.4775, -5068.3525)` establish progress rather than a frozen feed:

| Capture | Envelope sequence | Input age | Visibility measurement sequences | Five lamps |
|---|---:|---:|---|---|
| 2895 | 11461 | 15 ms | 2800 / 2801 | blocked, 0/9, age 31–94 ms |
| 4018 | 15712 | 47 ms | 3908 / 3909 | blocked, 0/9, age 31–109 ms |

The capture-paired camera at the first observation is
`(-11955.813, 838.556, -5062.522)`, vertical FOV 50 degrees, aspect 1.7777778.
Projection below is expressed in the displayed 2048 × 1149 screenshot:

| Source world position | Projected pixel | Approximate fixture |
|---|---|---|
| (-11951.586, 840.19806, -5073.2676) | (588, 221) | upper front left |
| (-11952.398, 839.5354, -5073.7764) | (702, 305) | lower front left |
| (-11956.91, 839.5354, -5073.705) | (1204, 302) | front right |
| (-11953.128, 838.8923, -5087.0596) | (943, 396) | middle left |
| (-11957.57, 838.8923, -5087.124) | (1169, 396) | middle right |

This is position/projection association, not durable light identity or an
exhaustive inventory of blue fixtures. First-capture RGB is
`(1.160546, 1.197379, 1.3309906)` for all five; second-capture RGB changed to
`(1.1854111, 1.2230331, 1.359508)`. No controlled brightness claim follows.

## Consumer isolation

The passive, credential-free Hue HUD snapshot reports Streaming at 30 Hz,
17/128 confirmed-visible sources, Point 100%, Spot 504%, Own lantern 0%, and no
untyped sources. Most channels have only tiny Ambient; channel 5 is warm.
Hue's `TelemetryParser` excludes blocked sources before light-kind weights and
spatial mapping, so raising a strength slider cannot recover these lamps.

An offline replay of the actual production mapper, retaining the captured room
geometry and controls but disabling Ambient and smoothing, adds only these five
raw contributions after that visibility filter. Accepted sources rise from 17
to 22. Play Bar 0 changes from RGB zero to `(0.91861, 0.93137, 0.97582)` and
Play Bar 1 to `(0.82231, 0.83381, 0.87386)`; warm channel 5 is unchanged.
This counterfactual isolates the exclusion. It is not a justified clear verdict,
a proposed production bypass, or physical-lamp acceptance.

## Cage hypothesis and bounded next step

The owner confirms these fixtures have a substantially enclosing cage. The cage
or mounting pillar is a plausible blocker, but the current API has neither hit
points nor collider identity. The screenshot and owner observation establish a
visible lighting effect, not which collision body stopped a ray.

The existing fan targets the source center and offsets of 0.05/0.15 game units
in a plane normal to the sightline. Those offsets are not a known emitter size.
The native query already copies each collector and validates its hit fraction,
normal and contact position; continuous publication currently reduces this to
sample counts. Therefore the next useful diagnostic is a bounded export of
those **existing** per-ray results, paired with source, camera, capture and
measurement time, without extra queries or a changed verdict. Preserve opaque
body tokens as diagnostic equality only, without claiming lamp ownership.

Compare this fixture's contacts against an intervening pillar/wall control before
choosing an emitter-near handling rule. Reuse the prior evidence in
[PHYSICS_QUERY_RESEARCH.md](PHYSICS_QUERY_RESEARCH.md): one visible cage had
contacts 0.123–0.178 game units before the source and 4/9 clear neighbors, but a
real wall also had contacts only 0.38–0.49 game units before the source and 0/9
clear. A blanket last-half-unit exemption would invalidate that wall control.

A limited housing tolerance or additional local samples are candidates only;
distance alone does not distinguish a cage from a nearby wall. Coarse collision
shapes may not contain optical openings at all. Do not relabel renderer-selected
or endpoint-near blocked lights as clear, interpret ray counts as transmission,
or create a special rule for blue/point lights/these coordinates. If a perceptual
fallback is later chosen, keep it explicit and separate from measured visibility.
Acceptance needs visible cage -> intervening wall/pillar -> visible cage, plus
an ordinary clear source. No new live control is requested until its diagnostic
is ready; finish the turn immediately when owner action is required.

## Preserved evidence and scope

Raw, ignored files are currently in `C:/DEV/CrimsonHue/artifacts/`:

- `scholastone-lamps-20261018-100033.json` (12:00:33 Europe/Berlin).
- `scholastone-lamps-control-20261018-100144.json` (12:01:44 Europe/Berlin).
- `scholastone-projection-20261018.json`.
- `hue-live-state/scholastone-lamps-20261018.json` (passive UI snapshot).
- `scholastone-audit/Program.cs` and `scholastone-audit/counterfactual.json`.

Screenshot: `C:/Users/fabia/AppData/Local/Temp/codex-clipboard-64057d38-8bb0-488b-bacf-482c45a771d4.png`.
No production code, settings, package, installation, game input or physical
output changed for this diagnosis. Exact collider/optical treatment remains open.
