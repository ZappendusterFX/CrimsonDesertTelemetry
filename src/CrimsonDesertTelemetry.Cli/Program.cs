using System.Diagnostics;
using System.ComponentModel;
using System.Globalization;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Text.Json;
using CrimsonDesertTelemetry.Cli;
using CrimsonDesertTelemetry.Core;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

const string baseSchemaVersion = "1.1";
const string lightsSchemaVersion = "1.6";
var capabilities = new[] { "player.position", "camera.transform", "camera.projection" };
var coordinateSystem = new CoordinateSystemSnapshot("game-unit", "right", "y");
var jsonOptions = new JsonSerializerOptions
{
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    WriteIndented = false
};

try
{
    var command = args.FirstOrDefault()?.ToLowerInvariant() ?? "diagnose";
    return command switch
    {
        "diagnose" => Diagnose(),
        "check-compatibility" => CheckCompatibility(args.Skip(1).ToArray()),
        "check-update" => CheckUpdate(args.Skip(1).ToArray()),
        "discover" => Discover(),
        "snapshot" => Snapshot(args.Skip(1).ToArray()),
        "track" => Track(args.Skip(1).ToArray()),
        "trace-camera-copies" => TraceCameraCopies(args.Skip(1).ToArray()),
        "serve" => Serve(args.Skip(1).ToArray()),
        "--version" or "-v" or "version" => Version(),
        "--help" or "-h" or "help" => Help(),
        _ => UsageError($"Unknown command: {command}")
    };
}
catch (InvalidOperationException exception)
{
    Console.Error.WriteLine(exception.Message);
    return 3;
}
catch (InvalidDataException exception)
{
    Console.Error.WriteLine(exception.Message);
    return 4;
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Unexpected failure: {exception.Message}");
    return 1;
}

int Diagnose()
{
    using var process = GameDiscovery.FindRunningProcess();
    if (process is null)
    {
        Console.WriteLine("Crimson Desert is not running.");
        return 3;
    }
    var executable = process.MainModule?.FileName
        ?? throw new InvalidOperationException("The Crimson Desert executable path is unavailable.");
    var resolved = ResolveBuild(executable);
    Console.WriteLine($"Process: {process.Id}");
    Console.WriteLine($"Executable: {executable}");
    Console.WriteLine($"SHA-256: {resolved.Compatibility.ExecutableSha256}");
    Console.WriteLine($"Compatibility: {resolved.Compatibility.Mode}; installed build: {resolved.GameBuild}");
    Console.WriteLine("Runtime camera/player validation still applies to each sample.");
    return 0;
}

int CheckCompatibility(string[] commandArgs)
{
    if (commandArgs.Length != 1) return UsageError("check-compatibility expects an executable path.");
    var resolved = BuildCompatibility.CheckAutomatic(commandArgs[0], LoadDefinitions());
    var camera = resolved.Definition.EngineCamera!;
    Console.WriteLine(JsonSerializer.Serialize(new { resolved.GameBuild, resolved.Compatibility,
        resolvedAddresses = new { camera.MainRootReferenceRva, camera.MainRootGlobalRva,
            camera.CameraReferenceRva, camera.CameraGlobalRva, camera.ContextVtableRva, camera.CameraVtableRva } },
        jsonOptions));
    Console.Error.WriteLine("Offline automatic checks passed. This is not a live-game validation.");
    return 0;
}

int CheckUpdate(string[] commandArgs)
{
    if (commandArgs.Length != 1) return UsageError("check-update expects an executable path.");
    var report = UpdateCheck.Run(commandArgs[0], LoadDefinitions());
    Console.WriteLine(JsonSerializer.Serialize(report, jsonOptions));
    Console.Error.WriteLine("Read-only offline recovery report. This command never enables hooks, lights or unknown builds.");
    return report.Status == "anchor-check-failed" ? 4 : 0;
}

int Discover()
{
    using var runtime = OpenRuntime();
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    var watch = Stopwatch.StartNew();
    _ = runtime.ReadOrientation(player);
    var frame = runtime.Camera.Capture(player);
    watch.Stop();
    var consensus = new RenderCameraConsensus(frame.Camera, frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates);
    Console.WriteLine(JsonSerializer.Serialize(new
    {
        build = runtime.GameBuild,
        source = EngineCameraReader.SourceName,
        discoveryMilliseconds = watch.Elapsed.TotalMilliseconds,
        discoveredCopies = runtime.Camera.AddressCount,
        validCopies = frame.ValidCopies,
        consensusCopies = consensus.CopyCount,
        distinctStates = consensus.DistinctStateCount,
        camera = ToCameraConsensus(consensus)
    }, jsonOptions));
    return 0;
}

int Snapshot(string[] commandArgs)
{
    if (!TryParseLightOptions(commandArgs, out var positional, out var lightOptions, out var error) || positional.Length != 0)
        return UsageError(error ?? "snapshot accepts only --lights and --light-radius <game-units>.");
    using var runtime = OpenRuntime(lightOptions);
    var (player, orientation) = runtime.ReadPose();
    var frame = runtime.Camera.Capture(player);
    var lights = runtime.CaptureLights(player);
    var snapshot = ToSnapshot(runtime.GameBuild, 0, player, frame, 0, orientation,
        lights, runtime.SupportsLights, SchemaFor(lightOptions));
    Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
    // An absent orientation is a normal JSON null, which tells the operator nothing
    // about why. The reason goes to stderr so it cannot disturb the JSON on stdout.
    if (orientation is null)
        Console.Error.WriteLine($"Player orientation unavailable: {runtime.OrientationFailureReason}");
    return 0;
}

int Track(string[] commandArgs)
{
    if (!TryParseLightOptions(commandArgs, out var positional, out var lightOptions, out var lightError) ||
        positional.Length > 2 ||
        positional.Length >= 1 && !int.TryParse(positional[0], out _) ||
        positional.Length >= 2 && !int.TryParse(positional[1], out _))
        return UsageError(lightError ?? "track expects [samples] [rate-hz] [--lights] [--light-radius <game-units>].");
    var samples = positional.Length >= 1 ? int.Parse(positional[0]) : 0;
    var rateHz = positional.Length >= 2 ? int.Parse(positional[1]) : 60;
    if (samples is < 0 or > 100_000 || rateHz is < 1 or > 240)
        return UsageError("track expects [samples; 0 means unlimited] [rate-hz; 1-240].");

    using var runtime = OpenRuntime(lightOptions);
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    var watch = Stopwatch.StartNew();
    var tracker = runtime.Camera;
    _ = runtime.ReadOrientation(player);
    _ = tracker.Capture(player);
    watch.Stop();
    Console.Error.WriteLine($"Ready: {EngineCameraReader.SourceName}, {tracker.AddressCount} source validated in {watch.Elapsed.TotalMilliseconds:0.###} ms.");

    using var cancellation = new CancellationTokenSource();
    ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
    {
        eventArgs.Cancel = true;
        cancellation.Cancel();
    };
    Console.CancelKeyPress += cancelHandler;
    long sequence = 0;
    var tickInterval = TimeSpan.FromSeconds(1d / rateHz);
    var nextTick = Stopwatch.GetTimestamp();
    while (!cancellation.IsCancellationRequested && (samples == 0 || sequence < samples))
    {
        var captureWatch = Stopwatch.StartNew();
        PlayerOrientationSnapshot? orientation;
        (player, orientation) = runtime.ReadPose();
        var frame = tracker.Capture(player);
        var lights = runtime.CaptureLights(player);
        captureWatch.Stop();
        var snapshot = ToSnapshot(runtime.GameBuild, sequence, player, frame,
            captureWatch.ElapsedTicks * 1_000_000 / Stopwatch.Frequency, orientation,
            lights, runtime.SupportsLights, SchemaFor(lightOptions));
        Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
        sequence++;
        nextTick += (long)(tickInterval.TotalSeconds * Stopwatch.Frequency);
        var remainingTicks = nextTick - Stopwatch.GetTimestamp();
        if (remainingTicks > 0 && (samples == 0 || sequence < samples) &&
            cancellation.Token.WaitHandle.WaitOne(TimeSpan.FromSeconds((double)remainingTicks / Stopwatch.Frequency)))
            break;
    }
    Console.CancelKeyPress -= cancelHandler;
    return 0;
}

int TraceCameraCopies(string[] commandArgs)
{
    if (commandArgs.Length != 3 ||
        !int.TryParse(commandArgs[0], out var seconds) || seconds is < 1 or > 120 ||
        !int.TryParse(commandArgs[1], out var rateHz) || rateHz is < 1 or > 120)
        return UsageError("trace-camera-copies expects <seconds; 1-120> <hz; 1-120> <new-output-file>.");

    var path = Path.GetFullPath(commandArgs[2]);
    if (File.Exists(path))
        return UsageError("Refusing to overwrite an existing camera-copy recording.");
    using var runtime = OpenRuntime();
    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
    using var output = new StreamWriter(new FileStream(path, FileMode.CreateNew, FileAccess.Write,
        FileShare.Read), new System.Text.UTF8Encoding(false));
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    Console.Error.WriteLine("Preparing one read-only camera discovery; keep the camera still until RECORDING.");
    var discoveryWatch = Stopwatch.StartNew();
    var discovered = RenderCameraConstantsScanner.Find(runtime.Reader, player);
    discoveryWatch.Stop();
    var addresses = discovered.Select(static candidate => candidate.Address).Distinct().ToArray();
    if (addresses.Length == 0)
        throw new InvalidDataException("No camera copies were discovered.");
    var startedAt = DateTimeOffset.UtcNow;
    output.WriteLine(JsonSerializer.Serialize(new
    {
        kind = "camera-copy-trace", traceVersion = 1, startedAt,
        gameBuild = runtime.GameBuild, processId = runtime.Process.Id,
        processStartedAt = runtime.Process.StartTime.ToUniversalTime(), rateHz,
        discoveryMilliseconds = discoveryWatch.Elapsed.TotalMilliseconds, addresses,
        note = "Independent diagnostic discovery, not necessarily the installed host's cached addresses. " +
            "Private session addresses; not a public telemetry schema or engine frame timestamps."
    }, jsonOptions));
    output.Flush();

    using var cancellation = new CancellationTokenSource();
    ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
    {
        eventArgs.Cancel = true;
        cancellation.Cancel();
    };
    Console.CancelKeyPress += cancelHandler;
    Console.Error.WriteLine($"RECORDING {seconds} seconds at {rateHz} Hz: {addresses.Length} copies. Output: {path}");
    var clock = Stopwatch.StartNew();
    long sequence = 0;
    try
    {
        while (clock.Elapsed.TotalSeconds < seconds && !cancellation.IsCancellationRequested)
        {
            var capturedAt = DateTimeOffset.UtcNow;
            player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
            var copies = RenderCameraConstantsScanner.Refresh(runtime.Reader, addresses, player);
            var consensus = RenderCameraConstantsScanner.SelectConsensus(copies);
            output.WriteLine(JsonSerializer.Serialize(new
            {
                kind = "sample", sequence, capturedAt, elapsedMs = clock.Elapsed.TotalMilliseconds,
                player = ToVector(player), candidates = copies, consensus
            }, jsonOptions));
            sequence++;
            var remaining = TimeSpan.FromSeconds((double)sequence / rateHz) - clock.Elapsed;
            if (remaining > TimeSpan.Zero && cancellation.Token.WaitHandle.WaitOne(remaining)) break;
        }
    }
    finally
    {
        Console.CancelKeyPress -= cancelHandler;
    }
    Console.Error.WriteLine($"Recorded {sequence} camera-copy samples. No game values were changed.");
    return 0;
}

int Serve(string[] commandArgs)
{
    var privateExactCount = commandArgs.Count(argument =>
        argument.Equals("--private-exact-build", StringComparison.OrdinalIgnoreCase));
    if (privateExactCount > 1) return UsageError("--private-exact-build may appear only once.");
    var privateExact = privateExactCount == 1;
    if (privateExact)
        commandArgs = commandArgs.Where(argument =>
            !argument.Equals("--private-exact-build", StringComparison.OrdinalIgnoreCase)).ToArray();
    if (!TryParseSmoothingOptions(commandArgs, out commandArgs, out var smoothing, out var smoothingError))
        return UsageError(smoothingError!);
    if (!TryParseLightOptions(commandArgs, out var positional, out var lightOptions, out var lightError) ||
        positional.Length > 2 ||
        positional.Length >= 1 && !int.TryParse(positional[0], out _) ||
        positional.Length >= 2 && !int.TryParse(positional[1], out _))
        return UsageError(lightError ?? "serve expects [port] [rate-hz] [--lights] [--light-radius <game-units>].");
    var port = positional.Length >= 1 ? int.Parse(positional[0]) : 27311;
    var rateHz = positional.Length >= 2 ? int.Parse(positional[1]) : 60;
    if (port is < 1024 or > 65535 || rateHz is < 1 or > 240)
        return UsageError("serve expects [port; 1024-65535] [rate-hz; 1-240].");
    return RunServer(port, rateHz, lightOptions, smoothing, privateExact);
}

int RunServer(int port, int rateHz, LightOptions lightOptions, LightSmoothingOptions smoothing,
    bool privateExact)
{
    using var cancellation = new CancellationTokenSource();
    using var timerResolution = WindowsTimerResolution.RequestFor(rateHz);
    ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
    {
        eventArgs.Cancel = true;
        cancellation.Cancel();
    };
    Console.CancelKeyPress += cancelHandler;
    try
    {
        var activeSchemaVersion = SchemaFor(lightOptions);
        var state = new TelemetryServerState(jsonOptions, rateHz, activeSchemaVersion, smoothing);
        var builder = WebApplication.CreateSlimBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
        var app = builder.Build();
        app.UseWebSockets();
        app.Use(async (context, next) =>
        {
            var origin = context.Request.Headers.Origin.ToString();
            if (IsAllowedBrowserOrigin(origin))
            {
                context.Response.Headers.AccessControlAllowOrigin = origin;
                context.Response.Headers.Vary = "Origin";
            }
            await next();
        });
        app.MapGet("/", () => Results.Json(new
        {
            name = "Crimson Desert Telemetry",
            schemaVersion = activeSchemaVersion,
            endpoints = new[] { "/v1/health", "/v1/snapshot", "/v1/schema", "/v1/stream",
                "/v1/lights/smoothed", "/v1/lights/smoothed/stream", "/v1/lights/visible", "/v1/lights/visible/stream",
                "/v1/ambient", "/v1/ambient/stream", "/v1/ambient/schema" }
        }, jsonOptions));
        app.MapGet("/v1/health", () => Results.Json(state.Health, jsonOptions));
        app.MapGet("/v1/snapshot", () => state.Latest is { } snapshot
            ? Results.Json(snapshot, jsonOptions)
            : Results.Json(state.Health, jsonOptions, statusCode: StatusCodes.Status503ServiceUnavailable));
        app.MapGet("/v1/schema", () => Results.Bytes(LoadEmbeddedSchema(), "application/schema+json"));
        app.Map("/v1/stream", context => StreamWebSocket(context, state, cancellation.Token));
        app.MapGet("/v1/lights/smoothed", () => Results.Json(state.LatestSmoothed, jsonOptions));
        app.MapGet("/v1/lights/visible", () => Results.Json(state.LatestVisible, jsonOptions));
        app.MapGet("/v1/ambient", () => Results.Json(state.LatestSky, jsonOptions));
        app.MapGet("/v1/ambient/schema", () => Results.Bytes(LoadEmbeddedSchema("ambient-v1.schema.json"), "application/schema+json"));
        app.Map("/v1/ambient/stream", context => StreamWebSocket(context, state, cancellation.Token, sky: true));
        app.Map("/v1/lights/smoothed/stream", context => StreamWebSocket(context, state, cancellation.Token, true));
        app.Map("/v1/lights/visible/stream", context => StreamWebSocket(context, state, cancellation.Token, visible: true));

        Console.Error.WriteLine($"Listening on http://127.0.0.1:{port} at {rateHz} Hz.");
        var samplingTask = Task.Run(() => SampleContinuously(state, rateHz, lightOptions, privateExact, cancellation.Token),
            cancellation.Token);
        try
        {
            app.StartAsync(cancellation.Token).GetAwaiter().GetResult();
            app.WaitForShutdownAsync(cancellation.Token).GetAwaiter().GetResult();
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        finally
        {
            cancellation.Cancel();
            try { app.StopAsync().GetAwaiter().GetResult(); }
            catch (OperationCanceledException) { }
            try { samplingTask.GetAwaiter().GetResult(); }
            catch (OperationCanceledException) { }
        }
        return 0;
    }
    finally
    {
        Console.CancelKeyPress -= cancelHandler;
    }
}

async Task StreamWebSocket(HttpContext context, TelemetryServerState state, CancellationToken cancellationToken,
    bool smoothed = false, bool sky = false, bool visible = false)
{
    var origin = context.Request.Headers.Origin.ToString();
    if (!string.IsNullOrEmpty(origin) && !IsAllowedBrowserOrigin(origin))
    {
        context.Response.StatusCode = StatusCodes.Status403Forbidden;
        return;
    }
    if (!context.WebSockets.IsWebSocketRequest)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        await context.Response.WriteAsync("A WebSocket connection is required.", cancellationToken);
        return;
    }
    using var socket = await context.WebSockets.AcceptWebSocketAsync();
    using var subscription = sky ? state.SubscribeSky() : visible ? state.SubscribeVisible() : state.Subscribe(smoothed);
    using var connectionCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
    try
    {
        var sendTask = SendSnapshots(socket, subscription,
            sky ? state.LatestSkyBytes : visible ? state.LatestVisibleBytes : smoothed ? state.LatestSmoothedBytes : state.LatestBytes,
            connectionCancellation.Token);
        var receiveTask = WaitForWebSocketClose(socket, connectionCancellation.Token);
        await Task.WhenAny(sendTask, receiveTask);
        connectionCancellation.Cancel();
        try { await Task.WhenAll(sendTask, receiveTask); }
        catch (OperationCanceledException) { }
        if (socket.State is WebSocketState.Open or WebSocketState.CloseReceived)
            await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "Closing", CancellationToken.None);
    }
    catch (Exception exception) when (exception is OperationCanceledException or WebSocketException)
    {
        // Disconnects and server shutdown are normal for streaming clients.
    }
}

static bool IsAllowedBrowserOrigin(string origin) =>
    Uri.TryCreate(origin, UriKind.Absolute, out var uri) && uri.IsLoopback;

async Task SendSnapshots(WebSocket socket, TelemetryServerState.TelemetrySubscription subscription,
    byte[]? initialSnapshot, CancellationToken cancellationToken)
{
    if (initialSnapshot is not null)
        await socket.SendAsync(initialSnapshot, WebSocketMessageType.Text, true, cancellationToken);
    await foreach (var bytes in subscription.Reader.ReadAllAsync(cancellationToken))
    {
        if (socket.State != WebSocketState.Open) break;
        await socket.SendAsync(bytes, WebSocketMessageType.Text, true, cancellationToken);
    }
}

async Task WaitForWebSocketClose(WebSocket socket, CancellationToken cancellationToken)
{
    var buffer = new byte[256];
    while (socket.State == WebSocketState.Open)
    {
        var result = await socket.ReceiveAsync(buffer, cancellationToken);
        if (result.MessageType == WebSocketMessageType.Close) break;
        while (!result.EndOfMessage)
        {
            result = await socket.ReceiveAsync(buffer, cancellationToken);
        }
    }
}

async Task SampleContinuously(TelemetryServerState state, int rateHz, LightOptions lightOptions, bool privateExact,
    CancellationToken cancellationToken)
{
    long sequence = 0;
    while (!cancellationToken.IsCancellationRequested)
    {
        RuntimeContext? runtime = null;
        try
        {
            state.SetHealth("waiting-for-game", false, null, null, 0, null, null);
            try { runtime = OpenRuntime(lightOptions, privateExact); }
            catch (InvalidOperationException)
            {
                await Task.Delay(1000, cancellationToken);
                continue;
            }
            catch (InvalidDataException exception)
            {
                state.SetHealth("unsupported-build", true, false, null, 0, null, exception.Message);
                await Task.Delay(5000, cancellationToken);
                continue;
            }

            state.SetCompatibility(runtime.Compatibility);
            var initialPlayer = await WaitForPlayerPosition(runtime, state,
                () => ToUnavailableSnapshot(runtime, sequence++, "loading", "player-unavailable"),
                cancellationToken);
            if (initialPlayer is null) continue;
            var player = initialPlayer.Value;
            state.SetHealth("discovering", true, true, runtime.GameBuild, 0, null, null);
            var tracker = runtime.Camera;
            var discoveryMilliseconds = tracker.ReferenceResolutionMilliseconds;
            var tickTicks = Math.Max(1, Stopwatch.Frequency / rateHz);
            var nextTick = Stopwatch.GetTimestamp();
            var nativeCaptureReadySignaled = false;
            var nextNativeCaptureSignalAttempt = 0L;

            while (!cancellationToken.IsCancellationRequested && !runtime.Process.HasExited)
            {
                try
                {
                    var captureWatch = Stopwatch.StartNew();
                    PlayerOrientationSnapshot? orientation;
                    (player, orientation) = runtime.ReadPose();
                    var frame = tracker.Capture(player);
                    if (!nativeCaptureReadySignaled && Environment.TickCount64 >= nextNativeCaptureSignalAttempt)
                    {
                        nativeCaptureReadySignaled = NativeCaptureReadySignal.TrySet(runtime.Process.Id);
                        nextNativeCaptureSignalAttempt = Environment.TickCount64 + 1000;
                    }
                    var lights = runtime.CaptureLights(player);
                    captureWatch.Stop();
                    var snapshot = ToSnapshot(runtime.GameBuild, sequence++, player, frame,
                        captureWatch.ElapsedTicks * 1_000_000 / Stopwatch.Frequency, orientation,
                        lights, runtime.SupportsLights, SchemaFor(lightOptions));
                    state.Publish(snapshot, tracker.AddressCount, discoveryMilliseconds);
                    // Only an untested build leaves Sky null now, so the reason fits.
                    state.PublishSky(runtime.Sky?.Capture() ?? SkyAmbientReader.Unavailable("unsupported-build"));
                }
                catch (Exception exception) when (exception is InvalidDataException or Win32Exception)
                {
                    if (state.Health.Status != "loading")
                        state.Publish(ToUnavailableSnapshot(runtime, sequence++, "loading", "required-telemetry-unavailable"),
                            tracker.AddressCount, discoveryMilliseconds);
                    state.SetHealth("loading", true, true, runtime.GameBuild,
                        tracker.AddressCount, discoveryMilliseconds, exception.Message);
                }

                nextTick += tickTicks;
                var remaining = nextTick - Stopwatch.GetTimestamp();
                if (remaining > 0)
                    await Task.Delay(TimeSpan.FromSeconds((double)remaining / Stopwatch.Frequency), cancellationToken);
                else
                    nextTick = Stopwatch.GetTimestamp();
            }
            if (!cancellationToken.IsCancellationRequested && runtime.Process.HasExited)
                state.Publish(ToUnavailableSnapshot(runtime, sequence++, "stopped", "game-stopped"),
                    tracker.AddressCount, discoveryMilliseconds);
        }
        catch (Exception exception) when (exception is InvalidDataException or Win32Exception or
                                           ArgumentException or InvalidOperationException)
        {
            state.SetHealth("error", runtime is not null, runtime is not null,
                runtime?.GameBuild, 0, null, exception.Message);
            await Task.Delay(2000, cancellationToken);
        }
        finally
        {
            runtime?.Dispose();
        }
    }
}

async Task<(float X, float Y, float Z)?> WaitForPlayerPosition(RuntimeContext runtime,
    TelemetryServerState state, Func<TelemetrySnapshot> createLoadingSnapshot,
    CancellationToken cancellationToken)
{
    while (!cancellationToken.IsCancellationRequested && !runtime.Process.HasExited)
    {
        try
        {
            return StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
        }
        catch (Exception exception) when (exception is InvalidDataException or Win32Exception)
        {
            if (state.Health.Status != "loading")
                state.Publish(createLoadingSnapshot(), 0, null);
            state.SetHealth("loading", true, true, runtime.GameBuild,
                0, null, exception.Message);
            await Task.Delay(250, cancellationToken);
        }
    }
    return null;
}

RuntimeContext OpenRuntime(LightOptions lightOptions = default, bool privateExact = false)
{
    var process = GameDiscovery.FindRunningProcess()
        ?? throw new InvalidOperationException("Crimson Desert is not running.");
    try
    {
        var executable = process.MainModule?.FileName
            ?? throw new InvalidOperationException("The Crimson Desert executable path is unavailable.");
        var resolved = ResolveBuild(executable, privateExact);
        var definition = resolved.Definition;
        if (resolved.Compatibility.Mode == "automatic")
            Console.Error.WriteLine($"Untested executable accepted by automatic compatibility checks " +
                $"(reference layout {resolved.Compatibility.ReferenceBuild}). Runtime validation remains active.");
        if (resolved.Compatibility.Mode == "research-exact")
            Console.Error.WriteLine("PRIVATE UNVALIDATED exact-build integration test. Not a supported release.");
        // Only an exact-hash, explicitly private research launch may exercise
        // candidate player anchors; all signature and live pointer guards remain active.
        var allowResearchCandidate = resolved.Compatibility.Mode == "research-exact";
        var addresses = StaticPositionProbe.Resolve(process, executable, definition, allowResearchCandidate);
        var orientation = PlayerOrientationReader.Resolve(process, executable, definition, allowResearchCandidate);
        var reader = new ReadOnlyProcess(process);
        try
        {
            var cameraDefinition = definition.EngineCamera
                ?? throw new InvalidDataException("This build has no supported native camera definition.");
            var camera = new EngineCameraReader(reader,
                checked((ulong)process.MainModule!.BaseAddress.ToInt64()), cameraDefinition);
            EngineLightReader? lights = null;
            var exactLightProfile = resolved.Compatibility.Mode is "tested" or "research-exact";
            if (lightOptions.Enabled && exactLightProfile && definition.EngineLights is { } lightDefinition)
                lights = new EngineLightReader(reader,
                    checked((ulong)process.MainModule!.BaseAddress.ToInt64()), lightDefinition);
            var rendered = lightOptions.Enabled && exactLightProfile &&
                           definition.NativeCapture is not null
                ? new RenderLightReader(process.Id, process.StartTime.ToFileTimeUtc()) : null;
            SourceVisibilityClient? visibility = null;
            if (lightOptions.SourceVisibilityQuery && exactLightProfile &&
                definition.NativeCapture is not null)
            {
                try { visibility = new SourceVisibilityClient(process.Id, process.StartTime.ToFileTimeUtc()); }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException)
                {
                    visibility = null;
                }
            }
            return new RuntimeContext(process, reader, resolved, addresses, orientation, camera, lights, rendered,
                visibility, lightOptions.PhysicsVisibility && exactLightProfile
                    ? new PhysicsVisibilityClient(process.Id, process.StartTime.ToFileTimeUtc(),
                        radius: lightOptions.PhysicsVisibilityRadius) : null, lightOptions);
        }
        catch { reader.Dispose(); throw; }
    }
    catch
    {
        process.Dispose();
        throw;
    }
}

IReadOnlyList<BuildDefinition> LoadDefinitions() =>
    BuildDefinition.LoadAll(Path.Combine(AppContext.BaseDirectory, "definitions"));

ResolvedBuild ResolveBuild(string executable, bool privateExact = false) =>
    BuildCompatibility.Resolve(executable, LoadDefinitions(), privateExact);

byte[] LoadEmbeddedSchema(string fileName = "telemetry-v1.schema.json")
{
    var assembly = typeof(RuntimeContext).Assembly;
    var resourceName = assembly.GetManifestResourceNames().Single(name =>
        name.EndsWith("schema." + fileName, StringComparison.OrdinalIgnoreCase));
    using var stream = assembly.GetManifestResourceStream(resourceName)
                       ?? throw new InvalidDataException("The embedded telemetry schema is missing.");
    using var buffer = new MemoryStream();
    stream.CopyTo(buffer);
    return buffer.ToArray();
}

CameraSnapshot ToCameraConsensus(RenderCameraConsensus consensus)
{
    var camera = consensus.Camera;
    return new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
        camera.NearPlane, camera.FarPlane == float.MaxValue ? null : camera.FarPlane,
        camera.FieldOfViewRadians * 180f / MathF.PI, camera.AspectRatio);
}

TelemetrySnapshot ToSnapshot(string build, long sequence, (float X, float Y, float Z) player,
    RenderCameraFrame frame, long captureDurationMicroseconds, PlayerOrientationSnapshot? orientation = null,
    EngineLightsSnapshot? lights = null, bool supportsLights = false, string schemaVersion = baseSchemaVersion) => new(
    schemaVersion,
    sequence,
    frame.Timestamp,
    new GameSnapshot(build, "playing"),
    coordinateSystem,
    CapabilitiesFor(orientation, supportsLights, lights?.Rendered is not null && lights.Rendered.UnavailableReason != "unsupported-build",
        lights?.Upstream is not null),
    new PlayerSnapshot(ToVector(player), orientation),
    ToCameraConsensus(new RenderCameraConsensus(
        frame.Camera, frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates)),
    new QualitySnapshot(frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates,
        frame.Rediscovered, captureDurationMicroseconds),
    lights);

IReadOnlyList<string> CapabilitiesFor(PlayerOrientationSnapshot? orientation, bool supportsLights = false,
    bool supportsRendered = false, bool supportsUpstream = false)
{
    IEnumerable<string> result = capabilities;
    if (orientation is not null) result = result.Append("player.orientation");
    if (supportsLights) result = result.Append("lights.engine");
    if (supportsRendered) result = result.Append("lights.rendered");
    if (supportsUpstream) result = result.Append("lights.upstream");
    return result.ToArray();
}

TelemetrySnapshot ToUnavailableSnapshot(RuntimeContext runtime, long sequence, string state, string lightReason) => new(
    SchemaFor(runtime.LightOptions),
    sequence,
    DateTimeOffset.UtcNow,
    new GameSnapshot(runtime.GameBuild, state),
    coordinateSystem,
    CapabilitiesFor(null, runtime.SupportsLights, runtime.Rendered is not null),
    null,
    null,
    null,
    runtime.UnavailableLights(lightReason));

static CameraVector3 ToVector((float X, float Y, float Z) value) => new(value.X, value.Y, value.Z);

int Help()
{
    Console.WriteLine("Crimson Desert Telemetry");
    Console.WriteLine("  diagnose                 Check the running game and build support");
    Console.WriteLine("  check-compatibility <exe>  Offline automatic checks (also on known EXEs)");
    Console.WriteLine("  check-update <exe>         Read-only per-anchor recovery report; never enables hooks or lights");
    Console.WriteLine("  discover                 Validate and summarize the native camera source");
    Console.WriteLine("  snapshot [--lights] [--light-radius N]  Emit one JSON telemetry snapshot");
    Console.WriteLine("  track [samples] [hz] [--lights] [--light-radius N]  Emit JSON Lines");
    Console.WriteLine("  trace-camera-copies <seconds> <hz> <file>   Record all copies for offline diagnosis");
    Console.WriteLine("  serve [port] [hz] [--lights] [--light-radius N]  Serve HTTP/WebSocket telemetry");
    Console.WriteLine("    [--light-smoothing-ms 200] [--light-group-radius 0.15]  Separate derived local-light stream");
    Console.WriteLine("  version                  Show the program version");
    return 0;
}

int Version()
{
    Console.WriteLine(typeof(RuntimeContext).Assembly.GetName().Version?.ToString(3) ?? "unknown");
    return 0;
}

int UsageError(string message)
{
    Console.Error.WriteLine(message);
    Console.Error.WriteLine("Run with --help for usage.");
    return 2;
}

string SchemaFor(LightOptions options) => options.Enabled ? lightsSchemaVersion : baseSchemaVersion;

bool TryParseSmoothingOptions(string[] arguments, out string[] remaining, out LightSmoothingOptions options, out string? error)
{
    var values = new List<string>();
    var time = 200d; var radius = .15f;
    error = null;
    for (var index = 0; index < arguments.Length; index++)
    {
        var flag = arguments[index].ToLowerInvariant();
        if (flag is not ("--light-smoothing-ms" or "--light-group-radius")) { values.Add(arguments[index]); continue; }
        if (++index >= arguments.Length || !double.TryParse(arguments[index], NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
        { error = $"{flag} expects a finite number in invariant format."; break; }
        if (flag == "--light-smoothing-ms") time = number; else radius = (float)number;
    }
    options = new(time, radius); remaining = values.ToArray();
    try { options.Validate(); } catch (ArgumentOutOfRangeException exception) { error = exception.Message; }
    return error is null;
}

bool TryParseLightOptions(string[] arguments, out string[] positional, out LightOptions options,
    out string? error)
{
    var values = new List<string>();
    var enabled = false;
    var sourceVisibilityQuery = false;
    var physicsVisibility = false;
    var physicsRadius = PhysicsVisibilityClient.DefaultRadius;
    var radius = LightOptions.DefaultRadius;
    error = null;
    for (var index = 0; index < arguments.Length; index++)
    {
        var argument = arguments[index];
        if (argument.Equals("--lights", StringComparison.OrdinalIgnoreCase))
        {
            enabled = true;
            continue;
        }
        if (argument.Equals("--physics-visibility", StringComparison.OrdinalIgnoreCase))
        { enabled = true; physicsVisibility = true; continue; }
        if (argument.Equals("--physics-visibility-radius", StringComparison.OrdinalIgnoreCase))
        {
            if (++index >= arguments.Length ||
                !float.TryParse(arguments[index], NumberStyles.Float, CultureInfo.InvariantCulture, out physicsRadius) ||
                !float.IsFinite(physicsRadius) || physicsRadius is < 1 or > PhysicsVisibilityClient.MaximumRadius)
            {
                positional = [];
                options = default;
                error = "--physics-visibility-radius must be between 1 and 500 game units.";
                return false;
            }
            continue;
        }
        if (argument.Equals("--research-source-visibility", StringComparison.OrdinalIgnoreCase))
        {
            enabled = true;
            sourceVisibilityQuery = true;
            continue;
        }
        if (argument.Equals("--light-radius", StringComparison.OrdinalIgnoreCase))
        {
            if (++index >= arguments.Length ||
                !float.TryParse(arguments[index], NumberStyles.Float, CultureInfo.InvariantCulture, out radius))
            {
                positional = [];
                options = default;
                error = "--light-radius expects a number in invariant format.";
                return false;
            }
            enabled = true;
            continue;
        }
        values.Add(argument);
    }
    if (!float.IsFinite(radius) || radius is < 1 or > 100_000)
    {
        positional = [];
        options = default;
        error = "--light-radius must be between 1 and 100000 game units.";
        return false;
    }
    positional = values.ToArray();
    // Do not silently cut the shared HUD/physics range at Lights.NearbyRadius.
    // Raising coverage preserves raw records; it never shrinks a larger API radius.
    if (physicsVisibility) radius = Math.Max(radius, physicsRadius);
    options = new LightOptions(enabled, radius, sourceVisibilityQuery && !physicsVisibility, physicsVisibility, physicsRadius);
    return true;
}

sealed class RuntimeContext(
    Process process,
    ReadOnlyProcess reader,
    ResolvedBuild resolved,
    StaticPositionAddresses positionAddresses,
    PlayerOrientationReader? orientation,
    EngineCameraReader camera,
    EngineLightReader? lights,
    RenderLightReader? rendered,
    SourceVisibilityClient? visibility,
    PhysicsVisibilityClient? physicsVisibility,
    LightOptions lightOptions) : IDisposable
{
    public Process Process { get; } = process;
    public ReadOnlyProcess Reader { get; } = reader;
    public BuildDefinition Definition { get; } = resolved.Definition;
    public string GameBuild { get; } = resolved.GameBuild;
    public CompatibilityInfo Compatibility { get; } = resolved.Compatibility;
    public StaticPositionAddresses PositionAddresses { get; } = positionAddresses;
    public PlayerOrientationReader? Orientation { get; } = orientation;
    public EngineCameraReader Camera { get; } = camera;
    public EngineLightReader? Lights { get; } = lights;
    public RenderLightReader? Rendered { get; } = rendered;
    public SourceVisibilityClient? Visibility { get; } = visibility;
    public PhysicsVisibilityClient? PhysicsVisibility { get; } = physicsVisibility;
    /// <summary>
    /// The sky reader attaches on any exactly tested build. It used to require build
    /// 25116796 by name, which silently killed the ambient feed on 25246367 even
    /// after the native hooks were relocated for it: the reader was never
    /// constructed, so the endpoint reported "unsupported-build" whatever the INI
    /// and the ASI actually did.
    ///
    /// Nothing is lost by dropping the build name. Attaching is not trusting: the
    /// bridge validates its own magic, version, bounds, owning process id and start
    /// time before a sample is believed, and reports its own state -- bridge-missing
    /// when the native side never opened it, unsupported-build when the ASI itself
    /// refused the executable. Those answers are accurate, where a hardcoded build
    /// name could only ever go stale at the next game update.
    /// </summary>
    public SkyAmbientReader? Sky { get; } = resolved.Compatibility.Mode is "tested" or "research-exact"
        ? new SkyAmbientReader(process.Id, process.StartTime.ToFileTimeUtc(),
            resolved.Definition.NativeCapture?.SkyProducerRva ?? SkyAmbientReader.SkyProducerRva) : null;
    public LightOptions LightOptions { get; } = lightOptions;
    public bool SupportsLights => Lights is not null;

    /// <summary>
    /// Why the last orientation read produced nothing, or null when it succeeded or
    /// when this build has no player-root definition at all.
    /// </summary>
    public string? OrientationFailureReason =>
        Orientation is null ? "This build has no player-root definition." : Orientation.LastFailureReason;

    public PlayerOrientationSnapshot? ReadOrientation((float X, float Y, float Z) player)
    {
        var value = Orientation?.Read(Reader, player);
        if (Compatibility.Mode == "automatic" && value is null)
            throw new InvalidDataException("Automatic compatibility requires a valid player-root type, basis and " +
                $"position: {OrientationFailureReason}");
        return value;
    }

    /// <summary>
    /// Read the player position and its orientation as one pose.
    /// </summary>
    /// <remarks>
    /// Deliberately no retry. An earlier version re-read both values once when the
    /// first attempt was rejected, on the assumption that a torn read would clear
    /// immediately. Measured while riding, that retry rescued none of eighteen
    /// rejections: the player global and the physics object are written at
    /// different points within a frame, and the sub-millisecond window between
    /// those writes is wide enough that a retry lands inside it too. The tearing is
    /// handled where it belongs, in the reader's position tolerance.
    /// </remarks>
    public ((float X, float Y, float Z) Player, PlayerOrientationSnapshot? Orientation) ReadPose()
    {
        var player = StaticPositionProbe.Read(Reader, PositionAddresses);
        return (player, ReadOrientation(player));
    }

    public EngineLightsSnapshot? CaptureLights((float X, float Y, float Z) player)
    {
        if (!LightOptions.Enabled) return null;
        var authored = Lights?.Capture(player, LightOptions.NearbyRadius) ?? UnsupportedLights();
        // One bridge sample supplies both views, so they share sequence, camera and fence.
        var capture = Rendered?.CaptureAll(player, LightOptions.NearbyRadius);
        var combined = authored with
        {
            Rendered = capture?.Rendered ?? RenderLightReader.Unavailable("unsupported-build"),
            Upstream = capture?.Upstream
        };
        return PhysicsVisibility?.Apply(player, combined) ?? Visibility?.Apply(player, combined) ?? combined;
    }

    public EngineLightsSnapshot? UnavailableLights(string reason)
    {
        if (!LightOptions.Enabled) return null;
        var authored = Lights?.Unavailable(LightOptions.NearbyRadius, reason) ?? UnsupportedLights();
        return authored with { Rendered = RenderLightReader.Unavailable(
            Rendered is null ? "unsupported-build" : reason) };
    }

    private EngineLightsSnapshot UnsupportedLights() => new(
        "unavailable", EngineLightReader.SourceName, LightOptions.NearbyRadius, null,
        new EngineLightDiagnosticsSnapshot(0, 0, 0, 0, 0, 0, 0, 0, 0, 0), "unsupported-build");

    public void Dispose()
    {
        Rendered?.Dispose();
        Visibility?.Dispose();
        PhysicsVisibility?.Dispose();
        Sky?.Dispose();
        Reader.Dispose();
        Process.Dispose();
    }
}

readonly record struct LightOptions(bool Enabled, float NearbyRadius, bool SourceVisibilityQuery, bool PhysicsVisibility = false,
    float PhysicsVisibilityRadius = PhysicsVisibilityClient.DefaultRadius)
{
    public const float DefaultRadius = 100f;
}

sealed class WindowsTimerResolution : IDisposable
{
    private readonly uint _period;

    private WindowsTimerResolution(uint period)
    {
        _period = period;
        if (_period != 0 && TimeBeginPeriod(_period) != 0)
            throw new InvalidOperationException("Windows did not grant the requested timer resolution.");
    }

    public static WindowsTimerResolution RequestFor(int rateHz) => new(rateHz > 120 ? 1u : 0u);

    public void Dispose()
    {
        if (_period != 0) _ = TimeEndPeriod(_period);
    }

    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    private static extern uint TimeBeginPeriod(uint period);

    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    private static extern uint TimeEndPeriod(uint period);
}
