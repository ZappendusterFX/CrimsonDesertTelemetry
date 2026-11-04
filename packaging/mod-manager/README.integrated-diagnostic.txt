PRIVATE INTEGRATION TEST — NOT A PUBLIC CRIMSON DESERT TELEMETRY RELEASE

This archive enables the complete current product configuration in one game
run: local HTTP/WebSocket telemetry, raw and smoothed lights, ambient, source
visibility, status notices, the corner HUD and fullscreen light overlay.

The reference game profile remains RESEARCH. On its exact executable the health
endpoint reports research-exact. Packages with complete recovery metadata can
also adapt to unchanged-layout address shifts after a game update: the plugin
waits for live player/camera data, searches once, validates code roles and live
bytes, then checks fresh captures. The HUD shows progress or a specific failure.
Such sessions report recovered, never tested. Cached addresses are rechecked.
Changed engine layouts or shader semantics can still require a mod update.
Each feed reports unavailable if its own runtime validation fails.

When Compatibility.RecoveryTestOnce contains a nonzero 32-hex token, this package
forces one full recovery test even on its known reference EXE. The correct EXE
hash and addresses remain intact. The test waits for valid in-game player/camera
data, ignores the address cache, searches every acquisition path and runs all
normal native/live-data checks. The HUD identifies this as a diagnostic test.
Once the world is ready, the host atomically consumes the token before the full
search begins, including attempts that later fail. Quitting at the main menu
before world readiness does not consume the test. The marker is stored at:
%LOCALAPPDATA%\CrimsonDesertTelemetry\RecoveryTests\<UPPERCASE-TOKEN>.done
Later launches with the same token use ordinary compatibility checks; the INI
does not need editing. An empty token leaves the diagnostic disabled.

Close the game before replacing the ASI via your mod manager. Keep the previous
package for rollback. Do not upload this archive to GitHub, Nexus or another
public release channel.
