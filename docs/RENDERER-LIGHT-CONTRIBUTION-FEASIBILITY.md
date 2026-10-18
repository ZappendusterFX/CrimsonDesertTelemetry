# Renderer light contribution: bounded feasibility check — 2026-10-18, Codex

**Verdict: promising enough for one targeted diagnostic, not a visibility feature
or a live fix.** The owner requested a short check only. Existing exported GPU
code has a light-indexed screen-space hit-data path feeding actual lighting.
Its exact behavior at the Scholastone cages or through glass is not established.

## Proven offline findings

Reused the preserved `pix-provenance-20260909/cpp` export and its shader map.
Extracted exact capture containers with `Map-PixExportShaders.py`, disassembled
with DXC, and resolved registers with `Resolve-PixExportBindings.py`. No new GPU
capture, replay, game hook, native query or production change was made.

- PSO22557, `SampleManyLightsCS`, binds `g_manyLightsHitDataUAV` at `u1,space38`
  to resource17495, a **640x360 R32G32_UINT** texture. Its light buffer at
  `t4,space37` is resource217, structured stride48. Shader lines1573–1590 pack
  the selected light index into the high bits of the first word, a bounded
  distance into its low16 bits, and a separate float-bit payload into word2.
  The screen-space loop samples normal/depth data and reconstructs a hit position.
  IMPORTANT: this shader's hit-data store is conditional on a decoded normal/depth
  class equal to29. This check does not establish that it produces every sample
  used by ordinary opaque lighting; other producers/variants remain to inspect.
- PSO22324, `IntegrateManyLightsUpsampleCS`, reads `g_manyLightsHitData` at
  `t14,space36` from the **same resource17495** and the same stride48 light buffer
  resource217 at `t4,space37`. Shader lines493–550 unpack the light index and
  distance, use that index to load light RGB, and retain them in shared
  `LightVisibilityInfo`. Lines1893 onward load that index again for the actual
  light records and use the retained distance downstream. This proves a concrete
  source-indexed intermediate, not merely an evocative shader name.
- The integrate dispatch's outputs resolve to scene diffuse resource17501
  (`u5,space38`), scene specular17538 (`u6,space38`), and specular result17503
  (`u8,space38`); tiled masks are17478 (`t16,space36`). Bindings are resolved at
  `ExecuteIndirect` events with descriptor rewrites accounted for.
- These are multiple passes on reused resources. A common resource and compatible
  packing do **not** establish a simple chronological producer->consumer pair
  between the two selected dispatches; remaining writers and execution order
  must be identified before choosing a capture point.
- PSO21752, `RaytracingVisibilityCS`, is **not** the desired lamp visibility
  result. Its sole UAV is `RAYTRACING_INSTANCE_DESC`; it processes instance
  transforms/packed instance fields, rather than reading lamps or tracing light
  visibility. Do not pursue it merely because of its name.

## Fresh neutral telemetry control

Two GET-only snapshots at the owner's location, build25477059, playing:
sequence115785/capture30076/age31ms and sequence118622/capture30818/age16ms.
Player remained `(-11954.664,835.4076,-5068.714)`. Both have128 upstream sources;
the later snapshot confirms the previously associated five cage lights are still
renderer-selected standalone points reporting blocked,0/9. An additional three
similar nearby point records also pass the diagnostic position/color selection;
this is not a source ownership or exhaustive fixture association claim.

These progressing controls establish the current input context only. They do not
contain resource17495 or link the old GPU capture's indices to current lights.
No hit texture was read from the running game and no optical ABA test was done.

## Limits and one next step

This is a sampled screen-space lighting intermediate, not a persistent per-lamp
visible/hidden flag or a 360-degree optical signal. Frame-local light indices,
surface pixel coordinates, quantization/sentinels, sampling, temporal reuse and
other producers need to be understood. The class-dependent depth loop does not
prove transparent-material handling. Glowing surfaces can also be emissive without
a corresponding light-source record. No GPU cost, extraction cost, cage/glass
acceptance or brightness mapping is established by the code inspection.

Next, if the owner chooses to continue: one bounded capture/readback of hit data
and its **same-pass light buffer, camera and receiver data** at this scene, after
identifying the relevant producer. Check whether the known cool point lights
have usable samples on visible surfaces. Only then compare a blocked view; do
not invent a public schema or consumer override from the present result.

## Evidence

Original export: `C:/DEV/CrimsonDesertTelemetry/artifacts/light-research/pix-provenance-20260909/cpp`.
New ignored evidence in this worktree: `artifacts/renderer-visibility-feasibility-20261018/`:
three DXBC/LL files, `sample-bindings.txt`, `integrate-bindings.txt`, shader hashes,
and `live-20261018-102955.json` / `live-20261018-103043.json`.
Shader SHA256s are recorded in `shader-hashes.json`. Source packages, settings,
physical lighting and game input are unchanged. This completes the requested
short feasibility check; no installation or owner action is required for it.
