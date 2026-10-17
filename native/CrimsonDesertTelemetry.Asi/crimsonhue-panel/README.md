# CrimsonHue panel hosted by CDT

Maintained in CrimsonHue's `native/CrimsonHue.Panel`; synchronize with
`scripts/Sync-CdtPanel.ps1 -TelemetryRoot <CDT checkout>` before committing both
repositories. CDT builds these sources into its own ImGui renderer. The generated
`SOURCE-SHA256.txt` records the copied source files. This folder owns no DXGI,
D3D12, MinHook, telemetry acquisition or Hue credentials.

The optional, already loaded `CrimsonHue.asi` must export `CrimsonHueUiProtocol`
returning 1, plus `CrimsonHueStatus`, `CrimsonHueReadState` and
`CrimsonHueSendCommand`. The ABI passes only UTF-8 JSON buffers and integer sizes;
no ImGui, C++ objects or memory ownership cross module boundaries. Snapshots are
bounded to 256 KiB, commands to 16 KiB and 64 queued commands. Only the worker
calls the controller; the render pass reads the last snapshot and queues actions.
Discovery pins compatible modules for process lifetime. Old independent-renderer
ASI versions are deliberately rejected. Ctrl+Shift+H is the default shortcut;
binding changes remain in CrimsonHue's protected user settings.

The title-bar X hides the panel and queues `close` to save controller settings;
it keeps lighting sync running, like the shortcut and Hide settings button.
Only explicit `stop`, host shutdown or a stream failure stops lighting sync.

Input ownership changes only after pre-existing game presses have released.
Legacy and delivered raw mouse/keyboard messages are tracked independently so
one route cannot swallow the other's queued release. Closing drops HUD focus and
restores clipping immediately, while finishing any HUD-owned press/release pairs
before forwarding new game presses. The worker progresses this handoff even when
all panels are hidden. ImGui input is reset on the render thread before reuse.
No synthetic input, raw-device registration or buffered-queue reads are used.
