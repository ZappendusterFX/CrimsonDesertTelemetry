# ReShade 6.8.0 and native capture error 6 — 2026-09-27

The owner reproduced `Capture error 6` in the game after installing ReShade
6.8.0.2155. In PID 28924, native capture armed and reached recurring capture
ready, then stopped with `0x00000006`. The bootstrap and host ran normally.
This is `render_capture.cpp`'s `ERROR_INVALID_HANDLE` from the source, counter
or command-list device check. It is separate from the older, unconfirmed
host-start error 6. Sky streaming was enabled, which bypasses the list-type
comparison in this run.

ReShade can return its D3D12 device proxy from a resource while an unwrapped
compute command list returns the native device. Its v6.8.0 source exposes the
native device through `IID_UnwrappedObject`. The fix in commit `00dabc7`
requests `ID3D12Device` from each child, optionally unwraps this ReShade proxy,
then compares canonical native `IUnknown` identities. It applies to preparation,
source, counter, command list, upstream input and submitting queue. An unknown
proxy or genuinely different D3D12 device still fails closed; adapter LUID is
never used as a substitute for device identity.

With the exact saved ReShade DLL, the offline WARP `--compute-reshade` test
confirmed that the old raw resource/list identities differ, then published two
fence-completed samples with the fix. A counter from a different hardware device
was rejected both with and without ReShade. The production ASI builds with
`CDT_RESEARCH=OFF`, and seven focused native CTests pass in both the isolated
checkout and the active worktree with its uncommitted modulator changes.

Evidence is in `artifacts/error6-reshade-20260927-093329-pid28924/`: screenshot,
native/overlay/bootstrap/ReShade logs, exact installed ASI and `dxgi.dll`, hashes,
and the offline ReShade test log. These artifacts are intentionally outside Git.
No running-game ASI or immutable package was replaced. The fix has not been
validated in the game or published. The owner declined a ReShade-disabled A/B
run; the next live check should use a new integrated package with ReShade active.

ReShade source references: [resource device hook](https://github.com/crosire/reshade/blob/v6.8.0/source/d3d12/d3d12_resource.cpp),
[compute-list handling](https://github.com/crosire/reshade/blob/v6.8.0/source/d3d12/d3d12_command_queue.cpp),
[unwrapping interface](https://github.com/crosire/reshade/blob/v6.8.0/source/com_utils.hpp).
