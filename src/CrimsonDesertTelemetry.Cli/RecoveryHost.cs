using System.ComponentModel;
using System.Diagnostics;
using CrimsonDesertTelemetry.Core;

namespace CrimsonDesertTelemetry.Cli;

internal sealed record RecoveryPreparation(ResolvedBuild Build, StaticPositionAddresses Position,
    ulong WorldAddress, uint RequestedFeatures, bool CacheHit);

/// <summary>Only the native plugin can request this path. Standalone readers never enable hooks.</summary>
internal static class RecoveryHost
{
    public static async Task<RecoveryPreparation?> PrepareAsync(Process process,
        IReadOnlyList<BuildDefinition> definitions, TelemetryServerState state, CancellationToken cancellation,
        bool requiredHandshake = false)
    {
        // Bootstrap opts its own host into the rendezvous. An independent CLI/API
        // reader (including transport tests) must never consume another host's request.
        if (!requiredHandshake) return null;
        NativeRecoveryBridge? bridge = null;
        var transferred = false;
        try
        {
            // The host and ASI start independently. Allow the native identity check to finish.
            var startup = Stopwatch.StartNew();
            NativeRecoveryRequest? request = null;
            while (!process.HasExited && startup.Elapsed < TimeSpan.FromSeconds(30))
            {
                cancellation.ThrowIfCancellationRequested();
                bridge ??= NativeRecoveryBridge.TryOpen(process.Id, process.StartTime.ToFileTimeUtc());
                if (bridge is not null)
                {
                    try { request = bridge.Read(); }
                    catch (InvalidDataException) when (startup.Elapsed < TimeSpan.FromSeconds(3)) { }
                    if (request is { State: not 0 }) break;
                }
                await Task.Delay(100, cancellation);
            }
            if (process.HasExited) return null;
            if (request is null || request.State == 0)
                throw new InvalidDataException("The plugin did not finish its game identity check. Check the native log and restart the game.");
            if (request.State == 5) { state.SetRecovery(null); return null; }
            if (request.State == 4)
                throw new InvalidDataException("Automatic recovery was refused in this game session. Check the native log; a compatible mod update may be required.");
            bridge!.Acknowledge();
            // A host restarted after handoff reconstructs the same guarded definition from disk/cache.
            var alreadySubmitted = request.State is 2 or 3;
            var reference = definitions.SingleOrDefault(d => string.Equals(d.ExecutableSha256,
                request.TemplateSha256, StringComparison.OrdinalIgnoreCase) && d.NativeCapture?.ContractId == request.ContractId)
                ?? throw new InvalidDataException("The plugin and its telemetry host have different recovery templates. Install the complete matching package.");
            var executable = process.MainModule?.FileName ?? throw new InvalidDataException("Game executable is unavailable.");
            var cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "CrimsonDesertTelemetry", "Recovery");
            state.SetHealth("recovering", true, null, null, 0, null, null);
            state.SetRecovery(new("scanning", "Checking the updated game", "Preparing player and camera checks.", 0));
            void Progress(RecoveryProgress progress)
            {
                var phase = progress.Phase == "waiting-for-world" ? "waiting-for-world" : "scanning";
                var detail = phase == "waiting-for-world" ? "Load a save and enter the game. Checking for live player and camera data." :
                    progress.Phase == "identifying" ? "Checking the game version and saved compatibility data." :
                    progress.Phase == "cache-rejected" ? "Saved addresses no longer match. Finding them again." :
                    progress.Phase == "preparing" ? "Locating player and camera data." :
                    progress.Detail.Contains("Physics", StringComparison.Ordinal) ? "Checking light visibility." :
                    progress.Detail.Contains("Ambient", StringComparison.Ordinal) ? "Checking ambient lighting." :
                    progress.Detail.Contains("Exposure", StringComparison.Ordinal) || progress.Detail.Contains("Spatial", StringComparison.Ordinal)
                        ? "Checking environment lighting and exposure." : "Checking local light capture.";
                state.SetRecovery(new(phase, phase == "waiting-for-world" ? "Waiting for the game world" :
                    "Adapting telemetry to this game update", detail,
                    phase == "waiting-for-world" ? null : Math.Clamp(progress.Completed * 80 / Math.Max(1, progress.Total), 0, 80)));
            }
            var readiness = RuntimeRecovery.ResolveReadiness(executable, reference, cache, Progress, cancellation);
            if (!string.Equals(request.ExecutableSha256, readiness.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The executable on disk differs from this running game. Restart the game before recovery.");
            var module = checked((ulong)process.MainModule!.BaseAddress.ToInt64());
            var positions = new StaticPositionAddresses(checked(module + readiness.StaticPositionXyRva),
                checked(module + readiness.StaticPositionZRva));
            var world = checked(module + readiness.PlayerWorldRva);
            using var reader = new ReadOnlyProcess(process);
            var camera = new EngineCameraReader(reader, module, readiness.Definition.EngineCamera!);
            var orientation = PlayerOrientationReader.FromResolvedAddress(world, reference.PlayerRoot!,
                allowResearchCandidate: reference.Status == "research");
            await WaitForWorld(process, reader, positions, orientation, camera, state, cancellation);
            var result = RuntimeRecovery.ResolveNative(readiness, reference, cache, Progress, cancellation);
            // A load/menu transition during the scan must not authorize a delayed activation.
            await WaitForWorld(process, reader, positions, orientation, camera, state, cancellation);
            if (!alreadySubmitted)
            {
                state.SetRecovery(new("activating", "Checking the recovered addresses",
                    "The plugin is validating live code and data before enabling capture.", 85));
                bridge!.Submit(result.Definition, request);
            }
            var activation = Stopwatch.StartNew();
            while (!process.HasExited)
            {
                cancellation.ThrowIfCancellationRequested();
                var current = bridge!.Read();
                if (current.State == 3) break;
                if (current.State == 4) throw new InvalidDataException("Live native validation refused the recovered addresses. See the in-game message and native log.");
                if (activation.Elapsed > TimeSpan.FromSeconds(30))
                    throw new InvalidDataException("Native activation did not complete. Restart the game; see the native log for the failed component.");
                await Task.Delay(100, cancellation);
            }
            if (process.HasExited) return null;
            var compatibility = new CompatibilityInfo("recovered", readiness.ExecutableSha256,
                result.Definition.ExecutableVersion, reference.SteamBuildId);
            var build = new ResolvedBuild(result.Definition,
                GameDiscovery.ReadSteamBuildIdForExecutable(executable) ?? "unknown", compatibility);
            state.SetRecovery(new("validating", "Checking live telemetry",
                "Addresses accepted. Waiting for fresh light and ambient captures.", 90));
            transferred = true;
            return new(build, positions, world, request.RequestedFeatures, result.CacheHit);
        }
        catch (Exception error) when (error is InvalidDataException or Win32Exception or IOException or
            UnauthorizedAccessException or OverflowException or ArgumentException)
        {
            var detail = error.Message[..Math.Min(error.Message.Length, 440)];
            Console.Error.WriteLine("Automatic recovery stopped: " + detail);
            state.SetHealth("unsupported-build", !process.HasExited, false, null, 0, null, detail);
            state.SetRecovery(new("failed", "This update could not be adapted automatically",
                detail + " No repeated scan will run in this session."));
            try { bridge?.Fail(detail); } catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { }
            // A rejected file/layout is not a reason to rescan every few seconds. Only a new
            // game session gets another attempt; this wait is the application's idle loop.
            while (!process.HasExited) await Task.Delay(1000, cancellation);
            return null;
        }
        finally
        {
            bridge?.Dispose();
            if (!transferred && process.HasExited) state.SetRecovery(null);
        }
    }

    private static async Task WaitForWorld(Process process, ReadOnlyProcess reader, StaticPositionAddresses position,
        PlayerOrientationReader orientation, EngineCameraReader camera, TelemetryServerState state,
        CancellationToken cancellation)
    {
        var watch = Stopwatch.StartNew();
        var gate = new RecoveryReadinessGate();
        var lastReason = "Player and camera are not available yet.";
        state.SetRecovery(new("waiting-for-world", "Waiting for the game world",
            "Load a save and enter the game. Recovery waits for a valid player and advancing camera data."));
        while (!process.HasExited)
        {
            cancellation.ThrowIfCancellationRequested();
            try
            {
                var player = StaticPositionProbe.Read(reader, position);
                var pose = orientation.Read(reader, player);
                _ = camera.Capture(player);
                if (gate.Observe(camera.LastFrameNumber, pose is not null, watch.Elapsed)) return;
                lastReason = pose is null ? orientation.LastFailureReason ?? "Player orientation is unavailable." :
                    "Camera frames have not advanced long enough.";
            }
            catch (Exception e) when (e is InvalidDataException or Win32Exception or OverflowException)
            { gate.Reset(); lastReason = e.Message; }
            if (watch.Elapsed > TimeSpan.FromSeconds(60))
                state.SetRecovery(new("waiting-for-world", "Still waiting for valid game data",
                    "If you are already in-game, the data layout may have changed: " + lastReason[..Math.Min(350, lastReason.Length)]));
            await Task.Delay(150, cancellation);
        }
        throw new InvalidOperationException("Game exited during recovery.");
    }
}

internal sealed class RecoveryLiveMonitor(uint requestedFeatures, bool cacheHit)
{
    private ulong? _lastLights, _lastSky;
    private uint? _lastLightFrame, _lastSkyFrame;
    private int _lights, _sky;
    private bool _finished;
    private readonly Stopwatch _watch = Stopwatch.StartNew();

    public void Pause(TelemetryServerState state)
    {
        if (_finished) return;
        _watch.Reset();
        _lastLights = _lastSky = null;
        _lastLightFrame = _lastSkyFrame = null;
        _lights = _sky = 0;
        state.SetRecovery(new("waiting-for-world", "Waiting for the game world",
            "The recovered addresses are retained. Live validation resumes when player and camera data return."));
    }

    public void Observe(EngineLightsSnapshot? lights, SkyAmbientSnapshot sky, TelemetryServerState state)
    {
        if (_finished) return;
        if (!_watch.IsRunning)
        {
            _watch.Start();
            state.SetRecovery(new("validating", "Checking live telemetry", "Waiting for fresh light and ambient captures.", 90));
        }
        var rendered = lights?.Rendered;
        var upstream = lights?.Upstream;
        var now = DateTimeOffset.UtcNow;
        var lightsValid = rendered?.Status == "available" && rendered.CaptureSequence is > 0 &&
            rendered.FrameNumber is not null && rendered.Sources is not null && rendered.Camera is not null &&
            Fresh(rendered.CapturedAt, rendered.AgeMilliseconds, now, RenderLightReader.MaximumAgeMilliseconds) &&
            ((requestedFeatures & 2) == 0 || upstream?.Status == "available" &&
                upstream.Sources is not null && upstream.InputRecords is not null &&
                upstream.CaptureSequence == rendered.CaptureSequence && upstream.FrameNumber == rendered.FrameNumber &&
                Fresh(upstream.CapturedAt, upstream.AgeMilliseconds, now, RenderLightReader.MaximumAgeMilliseconds));
        var skyValid = sky.Status == "available" && sky.CaptureSequence is > 0 && sky.FrameNumber is not null &&
            sky.Sky is not null && Fresh(sky.CapturedAt, sky.AgeMilliseconds, now, SkyAmbientReader.MaximumAgeMilliseconds);
        Advance(lightsValid, rendered?.CaptureSequence, rendered?.FrameNumber, ref _lastLights, ref _lastLightFrame, ref _lights);
        Advance(skyValid, sky.CaptureSequence, sky.FrameNumber, ref _lastSky, ref _lastSkyFrame, ref _sky);
        var observations = (requestedFeatures & 2) != 0
            ? upstream?.Sources?.Select(s => s.SourceVisibility).ToArray()
            : rendered?.Sources?.Select(s => s.SourceVisibility).ToArray();
        var needsPhysics = (requestedFeatures & 8) != 0 && observations?.Any(v =>
            v?.Reason != "outside-physics-radius") == true;
        var tick = Environment.TickCount64;
        var physics = observations?.Any(v => v is { Method: "physics-ray-fan", SampleCount: 9,
            Status: "clear" or "blocked", MeasurementSequence: > 0, ContextFrame: not null,
            VolumeAgeMillisecondsAtCapture: >= 0 and <= PhysicsVisibilityClient.MaximumAgeMilliseconds,
            MeasuredAtTickMilliseconds: > 0, ClearSampleCount: >= 0 and <= 9 } &&
            v.LightCaptureSequence > 0 && v.LightCaptureSequence <= rendered?.CaptureSequence &&
            tick - v.MeasuredAtTickMilliseconds is >= 0 and <= PhysicsVisibilityClient.MaximumAgeMilliseconds) == true;
        var capturesReady = lightsValid && _lights >= 3 &&
            ((requestedFeatures & 4) == 0 || skyValid && _sky >= 3);
        if (capturesReady && (!needsPhysics || physics))
        {
            _finished = true;
            state.SetRecovery(new("ready", "Telemetry adapted to this game update",
                cacheHit ? "Saved addresses rechecked; live telemetry is available." :
                "Addresses recovered and live telemetry checked. Future starts will reuse the validated address cache.", 100));
        }
        else if (_watch.Elapsed > TimeSpan.FromSeconds(45))
        {
            _finished = true;
            state.SetRecovery(new("failed", "Recovery needs attention",
                !capturesReady ? "Recovered hooks started, but the requested light or ambient feed has not produced fresh data. Check the native log; a mod update may still be required." :
                "Light data is available, but physics visibility has not passed its natural-query controls. Unknown visibility stays unknown; check the native log."));
        }
    }

    private static bool Fresh(DateTimeOffset? captured, long? age, DateTimeOffset now, long maximum) =>
        captured is { } time && age is { } elapsed && elapsed >= 0 && elapsed <= maximum &&
        (now - time).TotalMilliseconds >= 0 && (now - time).TotalMilliseconds <= maximum;

    private static void Advance(bool valid, ulong? sequence, uint? frame, ref ulong? previousSequence,
        ref uint? previousFrame, ref int count)
    {
        if (!valid) { previousSequence = null; previousFrame = null; count = 0; return; }
        if (previousSequence is null) count = 1;
        else if (previousSequence == sequence && previousFrame == frame) return;
        else if (sequence > previousSequence && frame != previousFrame &&
            unchecked(frame!.Value - previousFrame!.Value) < 0x80000000u) count++;
        else count = 0;
        previousSequence = sequence;
        previousFrame = frame;
    }
}
