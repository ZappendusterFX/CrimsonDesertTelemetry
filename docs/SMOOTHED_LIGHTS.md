# Smoothed local-light stream

An additional vendor-neutral consumer signal alongside raw `/v1/snapshot` and
`/v1/stream`. It groups and smooths current local-light RGB; ambient is provided
by the separate [ambient feed](AMBIENT_STREAM.md). Install one complete telemetry
package through DMM and keep research switches at `0` for production use.

Current production development adds optional [per-light source visibility](SOURCE_VISIBILITY.md)
to each group's original `contributions`. Raw records, RGB/luminance and this
unfiltered EMA group RGB are unchanged, and blocked/unknown contributions remain
present. Metadata is not averaged into a group verdict or multiplied into this color.
The per-light implementation has synthetic coverage; live visible/blocked/visible
and behind-camera acceptance remains pending. Camera-local ambient already passed
its separate open/enclosed/open test in OFF v2.1.9 on build 25246367.

- HTTP: `GET http://127.0.0.1:27311/v1/lights/smoothed`
- WebSocket: `ws://127.0.0.1:27311/v1/lights/smoothed/stream`

The separate **visibility-filtered** form has the same envelope:

- HTTP: `GET http://127.0.0.1:27311/v1/lights/visible`
- WebSocket: `ws://127.0.0.1:27311/v1/lights/visible/stream`

It admits only individual contributions whose current `sourceVisibility.status`
is `clear`, then groups and smooths that subset independently. `blocked`, `unknown`,
missing and stale measurements contribute no RGB and produce no group in this feed;
they are **not** relabeled as measured blocked in the raw stream. If visibility
is disabled/missing for all current lights, this feed reports `unavailable` with
`visibility-unavailable`, not a falsely measured empty scene. An available empty
`sources` array means no contribution passed the filter. Tracking IDs belong to
this feed/session and are not object IDs or interchangeable with the unfiltered feed.
An unchanged GPU capture is not smoothed twice. If its visibility selection
changes between host publications, the affected filtered result is rebuilt
immediately without an EMA tail from a newly hidden contribution. The estimate
remains subject to the ray sampler's latency and geometry limits.

Both return the same separate envelope, `schemaVersion: "1.0"`. HTTP returns
200 even when unavailable; inspect `status`, never infer availability from HTTP.
WebSocket shares the existing loopback-only browser-origin policy and bounded,
latest-only queue. Enabling `[Lights] Enabled=1` is required for its source.

## Consumer contract

Envelope fields: `schemaVersion`, `status` (`available`/`unavailable`), `source`,
`coverage`, `publishedAt`, `settings`. With `[Lights] Upstream=1` (default) the feed
groups the all-around ManyLights input (`lights.upstream`, including lights behind
the camera): `source` = `spatially-grouped-manylights-input`, `coverage` =
`current-engine-lights-before-view-selection`. Engine groups such as a fire's flame
particles already arrive as one summed light. That input then owns the feed: an
unavailable input sample makes the smoothed feed unavailable instead of silently
switching coverage. With `Upstream=0` the previous `spatially-grouped-filtered-manylights`
/ `view-filtered-not-complete-360` feed is unchanged.
Available data also includes `sourceCaptureSequence`, `sourceFrameNumber`,
`capturedAt`, `ageMilliseconds`, `sources`. Unavailable data omits those fields
and includes `unavailableReason`. An **available empty sources array is valid**.

Each source group contains:

| Field | Meaning |
| --- | --- |
| `trackingId` | Approximate spatial track, unique within this host session; NOT engine/object identity. |
| `position` | Current unweighted centroid in existing world/game-unit axes. No temporal position lag. |
| `colorLinear` | Smoothed sum of contributing linear HDR RGB values (`x/y/z` = R/G/B). No clamping to 0..1. |
| `luminanceLinear` | Weighted luminance of that smoothed RGB, same weights as raw light decoder; NOT lumens/nits. |
| `rawSumColorLinear` | Sum before temporal filtering, useful for diagnostics. |
| `contributions` | Current raw rendered records including sample index, position, RGB, kind, direction, cone and optional `sourceVisibility`. These are NOT additional lights to add again. |

For a grouped color signal, consume each group's **`colorLinear` once** at its
position. Do not add its `contributions` again, or also add the raw light stream.
Physical output mapping, exposure/brightness scaling, lamp geometry and output
rate limits belong to the consumer. No physical lamps are driven by telemetry.

For custom visibility-aware processing of the unfiltered feed, inspect each contribution's metadata separately:
members of a group may have different visibility. The existing group EMA includes
all members; applying one member's verdict to that whole color is not valid.
Require matching capture sequence and fresh usable metadata as specified in the
[source-visibility contract](SOURCE_VISIBILITY.md). Unknown or stale metadata does
not invalidate healthy raw or smoothed light data. The current stability release
excludes source occlusion and HUD hiding: metadata is unknown/disabled, with no
attenuation factor. The experimental contract remains preserved for later work.

Do not average conflicting spot directions into a fabricated cone. Contributions
retain their own directions; the aggregate has no single `kind`/cone/direction.
The RGB sum is a useful signal, **not predicted on-screen color or illumination
at a receiver**. Distance, attenuation, occlusion, material, exposure and
tonemapping still matter. A scalar luminance-weighted color average would lose
energy; these HDR contributions are summed in linear space instead.

## Grouping, tracking, smoothing

Default grouping extent is 0.15 game units, reusing the HUD's proximity scale.
Groups conservatively fit inside a box whose diagonal is at most that extent.
No transitive chains: A near B and B near C does not imply one large group.
This may split some close contributions; two separate lamps closer than the
threshold may also merge. **Spatial grouping is not proven physical identity.**
Only the current filtered ManyLights array is used, not the separate authored
list (which could double count). Off-screen/culled contributions remain absent.

Tracks are one-to-one nearest spatial matches to the previous capture, within
max(0.5, 2 × GroupRadius) game units. Sample indices are never identities.
Nearby moving/crossing lights and merges/splits may change or exchange tracks;
IDs do not survive culling, faults, loading or host restarts. The ID should not
be used as a saved physical lamp mapping.

Sum first, then smooth RGB with `alpha = 1 - exp(-dt / timeConstant)` using
elapsed source capture time. **Only a new GPU capture advances the filter**;
60 Hz host publication/extra HTTP requests do not repeatedly smooth 20 Hz data.
New tracks start at their observed value. Single lights use the same filter.
Default time constant 200 ms reaches about 63% of a step after 200 ms; it is not
a fixed 200 ms delay. Real fire pulses are attenuated, not claimed to be noise.

Missing groups disappear immediately; there is no invented fade-out/afterglow.
Unavailable, invalid, stale (>500 ms) or discontinuous source data resets state.
Health-only errors/loading/stop also invalidate and notify derived subscribers.
This does not prove an absent light was switched off: it may have been culled.

**Consumer watchdog:** stop using values when `status != available`, clear absent
groups on each fresh array, and expire all output after at most 500 ms without
fresh **`capturedAt`/`sourceCaptureSequence`** progress. `publishedAt` can advance
while the source sample is repeated. Network silence/host failure cannot deliver
an explicit error message, so a local freshness watchdog remains mandatory.

## Historical smoothing validation

Private local-lights.1 passed a 12-second stationary game capture on build25116796
(PID23572, 2026-09-07): 717 messages on each WebSocket,188 matched GPU captures,
no unavailable samples, age0..94ms. All8791 contribution comparisons and22773
EMA lane checks passed; repeated publications did not advance smoothing.
One nearby two-contribution fire and one blue singleton each retained one
tracking ID despite changing raw sample indices. Measured summed absolute
luminance-step variation fell by44.53% and16.10%, respectively. This does not
establish physical Hue output, movement/culling behavior, off/on acceptance or
the accuracy of spatial grouping for every scene object. Evidence is in the
current product handover; raw captures remain private ignored artifacts.

## Configuration

Current configuration (restart after edits):

```ini
[LightSmoothing]
TimeConstantMilliseconds=200
GroupRadius=0.15
```

Time constant: 0..2000 ms; 0 disables temporal filtering but retains grouping.
Group radius: 0.01..1 game units, decimal point. Configuration is global per
host and returned in `settings`; raw streams/HUD remain untouched.
Standalone development host:

```powershell
dotnet run --project C:\DEV\CrimsonDesertTelemetry\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights --light-smoothing-ms 200 --light-group-radius 0.15
```

Do not run a second host on an occupied port. Normal production ambient and
per-light visibility run alongside ManyLights/smoothing. The preserved research
`AmbientProbe` diagnostic is exclusive and can pause normal publication; it must
remain disabled for production validation.
