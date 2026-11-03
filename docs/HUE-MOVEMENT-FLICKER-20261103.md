# Hue movement flicker — owner-go passive diagnosis, 2026-11-03

Owner reports strong physical flicker while moving and asks for a short live-data
read, then sends go. Existing Capture-LightStreams.ps1 captured5s/raw286 and
smoothed286 messages with Ambient, without settings/input/output changes. Six
existing credential-free ReadHueState snapshots bracket14:05:18..14:05:23 CET.
PID27116. Installed Hue ASI hash matches public1.0.0(F75A928F...), not new1.0.1.
No controlled AN/AUS, physical light intervention or new test tool was used.

## Measured input and effective output settings

Both CDT light feeds are available throughout286 raw frames; ages15..93ms.
Ambient available in286 messages, age15..532ms. Thus this sample has no whole
local-feed availability/freshness gap. Effective Hue Recommended/Balanced uses
250ms local/600ms Ambient, Point200%, Spot102%, Own10%, local100%, Ambient40%,
master100%, Radius35GU. Streaming remains reported in all6 UI snapshots. Calculated
channel colors swing substantially before any physical-output claim, e.g. a red
channel changes about0.60->1.00 and back. UI RGB is not wire/lamp photometry.

Near35GU:68..87 sources,7..30 clear. At188 comparisons within the SAME native
light capture, identical-index/type/kind/position/RGB records change visibility:
111 unknown->clear,29 blocked->clear,21 clear->blocked,17 unknown->blocked,
6 clear->unknown,4 blocked->unknown. This directly demonstrates gating changes
without a new GPU RGB sample. SampleIndex is not a durable world-light identity.

A particularly bright moving player-near standalone spot occupies sampleIndex2
throughout this short window, with ONE RGB value(1.5816667,.63611203,.17278898)
and cone33.571743. Of286 observations it is waiting-for-physics228, clear52,
stale2, blocked4. Every one of78 new native captures begins unknown;33 later
become clear before the next capture. Its77 measured inter-capture position
steps are0.178..0.523GU, all beyond the existing0.12GU visibility-cache match
radius(PhysicsVisibilityClient.Find, squared0.0144). New positions therefore
lack the previous measured result and wait again. This spatial association/query
latency explains repeated removal/reinclusion of an otherwise steady-color source
in Hue's clear-only input. It is a concrete upstream flicker driver, not proof
that it accounts for every physical fluctuation. Do not blindly widen the cache
or retain unknown/blocked results as clear; movement through geometry still needs
fresh provenance and conservative visibility.

Ambient local level changes1.8266..8.7315. Global sky mean changes much less,
32.8469..35.4834; camera sky visibility changes0.0537..0.2462. This supplies another
varying target to Hue's600ms Ambient filter. Movement may legitimately change
exposure to geometry; this sample alone does not establish a faulty GI decoder.
The250ms local filter remains active in the installed1.0.0 source. Point200%
increases the visual impact of local changes; no user value was changed.

## Lantern and evidence boundaries

Actual1.0.1 packaged Core parses all286 snapshots offline, but recognizes ZERO
complete lantern pairs, just like the installed UI's0 matches. Removing the
build gate was necessary but does not adapt changed source signatures. The
observed moving spot's33.571743 cone differs from the retained31.137793/26.885372
pair. Do not identify it as the owned lantern solely from proximity, index or
color; CDT supplies no ownership ID. Own10% does not attenuate unmatched spots.
No fresh controlled lantern action was requested and no new ownership acceptance
is claimed.1.0.1 alone is not presented as a fix for this measured flicker.

Raw evidence:artifacts/light-research/hue-flicker-motion-20261103-go/ before/after,
raw.jsonl,smoothed.jsonl,ambient.jsonl,diagnosis.json,moving-source-detail.json.
Hue UI and replay evidence:CrimsonHue/artifacts/hue-live-state/
flicker-motion-20261103-go/state-1..6.json,lantern-replay.json.
An initial exploratory15GU fallback was wrong: UI exposes Radius=35, not FadeEnd.
A provisional distance-power proxy used a linear tail instead of the mapper's
smoothstep and was removed. The corrected diagnosis uses exact status/count/RGB/
Ambient observations only; no such provisional power estimate is a lamp result.

No product fix yet. Next scoped work, if requested: correct association/freshness
for moving sources in CDT while preserving fail-unknown/raw/provenance limits;
separately revisit the observed complete-pair metadata before changing Hue's
lantern heuristic. Calm(500/1000ms) can damp targets but does not repair source
association. No automatic tuning, installation or physical output was performed.
