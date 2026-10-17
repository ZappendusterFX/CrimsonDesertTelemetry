# Hue input handoff — 2026-10-17

Owner observation: a sword attack continued indefinitely after an uncertain HUD
close sequence; reopening and closing the HUD resolved it. No input trace of that
specific incident exists. Code inspection confirmed a related defect in the
maintained Hue input adapter: it started capture with game presses already down,
then swallowed their releases. The old tests covered input while fully open or
closed, but did not model a press spanning the transition.

The maintained source is CrimsonHue/native/CrimsonHue.Panel; the copied files and
SOURCE-SHA256.txt match. The controller and neutral telemetry API are unchanged.

## Change

- Track delivered normal and raw mouse/keyboard presses independently. Before
  acquiring HUD input, require physical release and completion of the observed
  game-owned releases. A physical-up sample does not settle an already queued up.
- Freeze routing of each message while capture/shortcut/render state can change.
  Preserve complete mixed raw packets and all five raw mouse button bit pairs.
- Hide/X/shortcut/Stop drop active HUD focus and restore clipping, while finishing
  HUD-owned pairs before forwarding new game presses. The worker maintains the
  transition even if all overlays are hidden, with no additional rendered frame
  needed for delivered release messages.
- Reset local tracking on focus loss/window changes. Clear ImGui input and pending
  events on its render thread before reuse. The HUD reminds users to release held
  buttons/keys if a previous game press is still finishing.

No artificial key-up, SendInput, cursor warp, raw registration or buffered raw-input
queue read is used. Only dispatched Win32 mouse/keyboard messages and physical
state are observed. Independently polled/buffered game routes remain outside this
adapter's demonstrated input suppression. See Microsoft's
[normal and buffered raw input examples](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input)
and [RAWKEYBOARD](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawkeyboard).

## Verification

The actual adapter is compiled with mocked User32. It passes held attack and
queued-up-before-delivery, both raw/legacy up orders, raw-only and mixed
release/new-press packets, keyboard/right modifiers, close-with-click-held,
no-visible-frame and worker completion, focus return and stale ImGui state.
Earlier cursor warp/relative/absolute/DPI/fallback/wheel/reentrancy checks pass.
A mutation of the game-held eligibility check fails specifically at
"HUD trusted physical-up before the queued release reached the game"; restoring
the actual source passes. Logs: `artifacts/input-handoff-build.log`,
`input-handoff-tests.log`, `input-handoff-mutant-build.log`,
`input-handoff-mutant-test.log`.

**12/12** focused CTest input/overlay/notification cases pass, along with one
hidden shared D3D12 WARP test using the exact packaged Hue .asi.20. It reads back
748,434 visible pixels and checks independent hide, Hue-only reopen, resize and
shutdown. Normal HUD screenshot visually reviewed. Logs:
`artifacts/input-handoff-native-tests.log`, `artifacts/input-handoff-shared-smoke.log`;
image `artifacts/input-handoff/lighting.png`. The tested shared renderer library
retains the package-build hash recorded below.

## Immutable package

**CDT2.2.5-huepanel.10**, production/research OFF, native build25477059. Keep the
existing **Hue0.4.0-asi.20** controller. Built with the maintained package script;
in-memory profile/companion regression, expanded payload and ZIP validators pass.
The packaged README now identifies this exact pair and corrected input behavior.

- ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.2.5-huepanel.10-ModManagers.zip`.
- ZIP SHA256: `92426F7A898A24EB5CC19F09845C4956B32EF392C21FE95A13B6D6DB4315315D`.
- ASI SHA256: `CE219A4FAAEBC40D2164F139AB07B13ADED712E520D81E5EA5CA1DB92EE735D2`.
- Shared renderer library SHA256:
  `5E9560A63CEC19D690DEC97C06F67D8230A0D1B4A0D941B70169B58C83FC86AD`.
- Package log: `artifacts/input-handoff-package.log`.

No real keyboard/mouse action, foreground manipulation, game/process write,
installation, settings change or physical lamp output was performed. Old packages
and product main/research remain untouched. After game exit install CDT.10 only,
keep Hue.20, then owner acceptance checks the sword/HUD transition and input
return; synthetic reproduction does not establish the exact cause in the game.
