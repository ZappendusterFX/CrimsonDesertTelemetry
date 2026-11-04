CrimsonDesertTelemetry - private simulation tick diagnostic

This package measures a movement/simulation function entry counter. It is a
diagnostic candidate, not the automatic menu-light or capture-pause feature.
The ordinary telemetry and lighting paths retain their existing behavior.

The optional diagnostic is enabled by Diagnostics.SimulationTicks=1 in this
private package. It requires the exact validated game EXE for build 26423880,
a unique code signature, unchanged prologue and a verified function boundary.
If validation fails, the diagnostic remains unavailable. Normal packages leave
the switch disabled. General research instrumentation is compiled out.

The counter uses the function-discovery approach found in World Builder.
CDT counts function entries; World Builder counts after the function returns.
This diagnostic must establish whether the counter actually stops while skill
previews run and resumes during gameplay. No progress at startup is unknown.
After observed progress, 500 ms without another entry is recorded as inactivity.
That observation does not independently distinguish a menu from a game stall.

Requirements and installation:
- Windows x64 and the existing x64 ASI loader.
- .NET 8 Desktop Runtime x64 and ASP.NET Core Runtime x64, installed separately.
- Only the owner installs or replaces packages, with the game closed, using DMM.
- Keep all files in this package together; do not mix old telemetry DLL/CFG files.
- CrimsonHue is a separate optional package; this ZIP does not replace it.
- Stop lighting sync before leaving the game for an installation/restart.

The assistant can read the diagnostic after the owner reports installation and
restart. It is exposed through a process-specific local read-only diagnostic
channel. No public HTTP schema or Hue output setting is changed by this build.

This is not a public release and has not yet passed in-game menu acceptance.
Sources and license: LICENSE.txt and THIRD-PARTY-NOTICES.txt in this package.
