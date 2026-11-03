# Visibility cache admission under effect churn — 2026-11-03

## Finding

The owner requested a solution for Hue flicker, with a review of today's CDT
update and the historical unknown/clear cases. The quiet corridor alone did not
establish a CDT defect. The subsequent owner-requested 30-second lightning-effect
recording establishes a separate, older cache-admission defect. This supersedes
any blanket conclusion that no CDT change is needed; it does not turn the earlier
moving-light/lantern attribution into evidence.

The recording is preserved outside Git in CrimsonHue/artifacts/
lightning-360-20261103-30s-go/: 1,784 raw frames, 456 native captures, 29.997 seconds
(15:12:48.519–15:13:18.516 CET). Both light feeds remain available, ages 0–109 ms.
The first frame already contains an effect; subsequent major bursts around +5
and +9.7 seconds have recorded lead-in and decay. Bright tails finish near
+19.8 seconds; approximately ten seconds of quieter output follow. The player
moves during this recording. Source indices are not persistent object IDs and
particle-group centres do not prove individual physical 360-degree positions.

There are 1,303 upstream `outside-physics-budget` observations, including 1,206
within Hue's 35 GU range. Reconstructing current upstream targets plus unpaired
rendered targets, nearest-first ordering, 0.1 GU deduplication and the 256-target
cap in a generous 100 GU envelope yields at most **112 selected targets**. Every
reported budget-missing position is within the existing 0.12 GU association
radius of one of those selected targets. Thus the 256-current-target bound does
not explain these omissions; this reconstruction does not assume a particular
installed HUD radius merely from the package default.

`PhysicsVisibilityClient` retained up to 512 old positional cache entries for
five seconds. Once full, it simply declined to create entries for new selected
positions. Those positions were then excluded from the request, even though the
current target set was much smaller than 256. This is a request-admission failure,
not a measured blocked result or proof that an unknown ray would have been clear.
It existed before the 2.2.6 game update; the managed client was unchanged there.

The strongest recorded group, capture 74627/index 4410, has RGB approximately
(5502,6810,38667). It starts unknown and then becomes clear 9/9 at +5.431 seconds,
measurement 71463, original capture 74627/frame 9293, age 16 ms (then 31/47 ms).
Other short groups remain unmeasured. Across 106,876 known-source observations,
status/ray-count/age checks and reference capture/frame/camera consistency have
zero mismatches. There is no demonstrated recurrence of the old wrong-profile
mass-clear bug, nor optical/material correctness proof.

## Correction

Before admitting new targets, protect all existing entries selected by the
current target set. If the 512-entry cache is full, remove the least recently
measured/requested entry that no current selected target uses. A newly admitted
entry starts unknown and must receive its own complete physics response.

Each pending request also retains the exact cache-entry reference associated
with each packet. A late response cannot repopulate an entry that was removed
or replace a new entry merely because its position is nearby. Native fault code
3 still latches the process off even when the originating entry has disappeared.

The selection radius, 0.1/0.12 GU geometry tolerances, nearest 256 targets,
50 ms minimum request interval, existing oldest-attempt queue order, native
2 ms shared budget, nine rays, exact-build profile/guards and 500 ms result expiry
are unchanged. Current entries keep measurement provenance, LastChecked and
LastRequested; a budget skip does not refresh their result. No source identity,
visibility prediction, extra ray budget, smoothing or fail-open rule is added.
Raw/optional streams and all native code are unchanged.

The existing priority of never-attempted entries over measured entries remains.
Sustained effect churn can still exhaust the native time budget or expire results;
this correction removes needless admission starvation, not every unknown phase.

## Verification

A synthetic test uses the actual client and its existing isolated V2 memory-map
transport: 32 scenes of 17 current targets, one persistent anchor plus 16 new
positions per 60 ms. It creates 513 positions within 1.92 seconds, while every
current scene remains far below the target cap. The previous client fails with
`retired-position cache must admit every current target below the 256-target cap`.
The corrected client schedules all current targets, retains the anchor, keeps new
positions unknown, preserves skipped-result timestamps and queue priority, and
still expires known results at 501 ms. Existing tests retain the nearest-256
cap, rapid camera movement, malformed/wrong-origin response and fault checks.

A separate test invalidates the cache while a request is pending, creates a new
entry at the same position and supplies the old response. It remains unknown
until its own new request completes. Replacing the entry-reference guard with
the previous positional response lookup makes this test fail; the restored fix
passes. This verifies why the late-response protection is necessary.

All 79 managed Release tests pass. Evidence logs stay in artifacts/light-research/
cache-admission-20261103/. No game input, installed file, live physics query,
settings or lamp output was changed by the assistant. The fix is synthetically
verified; live performance/optical/physical-Hue acceptance is not claimed. The
owner waived repeated AN/AUS and full historical manual retests. Installation
remains exclusively owner-managed.

Package and exact hashes: [2.2.7 validation](releases/v2.2.7-validation.md).
