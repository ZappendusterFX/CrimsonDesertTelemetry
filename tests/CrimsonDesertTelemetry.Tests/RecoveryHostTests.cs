using System.Text;
using System.Text.Json;
using CrimsonDesertTelemetry.Cli;
using CrimsonDesertTelemetry.Core;

internal static class RecoveryHostTests
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private const int Pid = 7654321;
    private const long Born = 134066592000000000;

    public static void Handshake()
    {
        using var standalone = System.Diagnostics.Process.GetCurrentProcess();
        Check(RecoveryHost.PrepareAsync(standalone, [], State(), CancellationToken.None)
            .GetAwaiter().GetResult() is null, "A standalone reader entered native recovery without bootstrap opt-in.");
        var header = Header();
        var request = NativeRecoveryBridge.DecodeHeader(header, Pid, Born);
        Check(request.State == 1 && request.RequestedFeatures == 15 && request.ContractId == "fixture-contract" &&
            request.TemplateSha256 == new string('A', 64) && request.ExecutableSha256 == new string('B', 64),
            "Valid recovery request lost its state, features or executable identity.");
        foreach (var length in new[] { 0, 31, 255, 257, 65536 })
            Reject(() => NativeRecoveryBridge.DecodeHeader(new byte[length], Pid, Born), "Truncated/oversized recovery header accepted.");
        Reject(() => NativeRecoveryBridge.DecodeHeader(header, Pid + 1, Born), "Foreign process id accepted.");
        Reject(() => NativeRecoveryBridge.DecodeHeader(header, Pid, Born + 1), "Reused process id accepted.");
        foreach (var field in new[] { (0, 0u), (4, 2u), (8, 1024u), (12, 6u), (16, 65281u), (20, 1u), (224, 16u) })
        {
            var invalid = (byte[])header.Clone(); Put(invalid, field.Item1, field.Item2);
            Reject(() => NativeRecoveryBridge.DecodeHeader(invalid, Pid, Born), "Invalid recovery header field accepted.");
        }
        foreach (var offset in new[] { 32, 95, 96, 159 })
        {
            var invalid = (byte[])header.Clone(); invalid[offset] = (byte)'G';
            Reject(() => NativeRecoveryBridge.DecodeHeader(invalid, Pid, Born), "Non-hex recovery identity accepted.");
        }
        foreach (var mutation in new Action<byte[]>[] {
            bytes => bytes[160] = 0,
            bytes => bytes[160] = 0xff,
            bytes => Array.Fill(bytes, (byte)'x', 160, 64) })
        {
            var invalid = (byte[])header.Clone(); mutation(invalid);
            Reject(() => NativeRecoveryBridge.DecodeHeader(invalid, Pid, Born), "Invalid recovery contract string accepted.");
        }
    }

    public static void WorldReadiness()
    {
        var gate = new RecoveryReadinessGate();
        for (uint frame = 0; frame < 8; frame++)
            Check(gate.Observe(frame, true, TimeSpan.FromMilliseconds(frame * 150)) == (frame == 7),
                "A stationary valid player with advancing frames did not satisfy the one-second gate.");
        gate.Reset();
        for (var sample = 0; sample < 12; sample++)
            Check(!gate.Observe(123, true, TimeSpan.FromMilliseconds(sample * 150)), "A frozen frame opened the gate.");
        gate.Reset();
        for (uint frame = 0; frame < 7; frame++) gate.Observe(frame, true, TimeSpan.FromMilliseconds(frame * 150));
        Check(!gate.Observe(7, true, TimeSpan.FromSeconds(3)), "A sampling gap retained old readiness evidence.");
        for (uint frame = 8; frame < 15; frame++)
            Check(!gate.Observe(frame, true, TimeSpan.FromMilliseconds(3000 + (frame - 7) * 150)),
                "A gap did not require a complete fresh readiness window.");
        Check(gate.Observe(15, true, TimeSpan.FromMilliseconds(4200)), "Readiness did not resume after a fresh window.");
        gate.Reset();
        for (uint frame = 0; frame < 7; frame++) gate.Observe(frame, true, TimeSpan.FromMilliseconds(frame * 150));
        Check(!gate.Observe(7, false, TimeSpan.FromMilliseconds(1050)), "Invalid player pose opened the gate.");
        Check(!gate.Observe(null, true, TimeSpan.FromMilliseconds(1200)), "Missing camera frame opened the gate.");
        Check(!gate.Observe(8, true, TimeSpan.FromMilliseconds(1350)), "Loading did not reset readiness evidence.");
        gate.Reset();
        for (var sample = 0; sample < 20; sample++)
            Check(!gate.Observe((uint)(sample % 2), true, TimeSpan.FromMilliseconds(sample * 150)),
                "Alternating old frame numbers were mistaken for advancing data.");
        gate.Reset();
        for (uint sample = 0; sample < 8; sample++)
            Check(gate.Observe(unchecked(uint.MaxValue - 3 + sample), true, TimeSpan.FromMilliseconds(sample * 150)) == (sample == 7),
                "A genuine uint frame-counter wrap was rejected.");
    }

    public static void LiveReadiness()
    {
        var state = State();
        var monitor = new RecoveryLiveMonitor(7, false);
        for (ulong sequence = 1; sequence <= 3; sequence++) monitor.Observe(Lights(sequence), Sky(sequence), state);
        Check(state.Health.Recovery?.State == "ready", "Fresh paired feeds did not finish recovery.");

        // A valid zero-light scene is distinct from a status-only/null feed.
        state = State(); monitor = new RecoveryLiveMonitor(7, true);
        for (ulong sequence = 1; sequence <= 3; sequence++)
        {
            var empty = Lights(sequence);
            empty = empty with { Rendered = empty.Rendered! with { Sources = [], Diagnostics = new(0, 0, 0, 0) },
                Upstream = empty.Upstream! with { Sources = [], InputRecords = 0, Diagnostics = new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) } };
            monitor.Observe(empty, Sky(sequence), state);
        }
        Check(state.Health.Recovery?.State == "ready", "A fresh zero-light scene was incorrectly required to contain a light.");

        foreach (var mutation in new Func<EngineLightsSnapshot, EngineLightsSnapshot>[] {
            l => l with { Rendered = l.Rendered! with { Sources = null } },
            l => l with { Rendered = l.Rendered! with { FrameNumber = null } },
            l => l with { Rendered = l.Rendered! with { Camera = null } },
            l => l with { Rendered = l.Rendered! with { CapturedAt = null } },
            l => l with { Rendered = l.Rendered! with { AgeMilliseconds = null } },
            l => l with { Rendered = l.Rendered! with { AgeMilliseconds = 10000 } },
            l => l with { Rendered = l.Rendered! with { CapturedAt = DateTimeOffset.UtcNow.AddMinutes(-1) } },
            l => l with { Rendered = l.Rendered! with { Status = "unavailable" } },
            l => l with { Upstream = l.Upstream! with { CaptureSequence = 999 } },
            l => l with { Upstream = l.Upstream! with { FrameNumber = 999 } },
            l => l with { Upstream = l.Upstream! with { Sources = null } },
            l => l with { Upstream = l.Upstream! with { AgeMilliseconds = 10000 } } })
        {
            state = State(); monitor = new RecoveryLiveMonitor(7, false);
            for (ulong sequence = 1; sequence <= 3; sequence++) monitor.Observe(mutation(Lights(sequence)), Sky(sequence), state);
            Check(state.Health.Recovery?.State != "ready", "Invalid, stale or unpaired light data completed recovery.");
        }
        foreach (var mutation in new Func<SkyAmbientSnapshot, SkyAmbientSnapshot>[] {
            s => s with { Sky = null }, s => s with { Status = "unavailable" },
            s => s with { AgeMilliseconds = null }, s => s with { AgeMilliseconds = 10000 },
            s => s with { CapturedAt = null }, s => s with { CapturedAt = DateTimeOffset.UtcNow.AddMinutes(-1) } })
        {
            state = State(); monitor = new RecoveryLiveMonitor(7, false);
            for (ulong sequence = 1; sequence <= 3; sequence++) monitor.Observe(Lights(sequence), mutation(Sky(sequence)), state);
            Check(state.Health.Recovery?.State != "ready", "Invalid or stale sky data completed recovery.");
        }
        state = State(); monitor = new RecoveryLiveMonitor(7, false);
        for (var index = 0; index < 10; index++)
        {
            var sequence = (ulong)(index % 2 + 1);
            monitor.Observe(Lights(sequence), Sky(sequence), state);
        }
        Check(state.Health.Recovery?.State != "ready", "Replayed/rewinding captures completed recovery.");
        state = State(); monitor = new RecoveryLiveMonitor(7, false);
        for (ulong sequence = 1; sequence <= 3; sequence++)
        {
            var lights = Lights(sequence);
            lights = lights with { Rendered = lights.Rendered! with { FrameNumber = 1 },
                Upstream = lights.Upstream! with { FrameNumber = 1 } };
            monitor.Observe(lights, Sky(sequence), state);
        }
        Check(state.Health.Recovery?.State != "ready", "New capture sequences hid a frozen game frame.");
        state = State(); monitor = new RecoveryLiveMonitor(7, false);
        monitor.Observe(Lights(1), Sky(1), state); monitor.Observe(Lights(2), Sky(2), state);
        monitor.Observe(null, SkyAmbientReader.Unavailable("loading"), state);
        monitor.Observe(Lights(3), Sky(3), state);
        Check(state.Health.Recovery?.State != "ready", "Loading retained an earlier partial acceptance window.");

        state = State(); monitor = new RecoveryLiveMonitor(7, false);
        monitor.Observe(Lights(1), Sky(1), state); monitor.Observe(Lights(2), Sky(2), state);
        monitor.Pause(state);
        Check(state.Health.Recovery?.State == "waiting-for-world", "Loading did not pause the recovery status.");
        monitor.Observe(Lights(3), Sky(3), state);
        Check(state.Health.Recovery?.State != "ready", "Loading retained the old live acceptance window.");
        monitor.Observe(Lights(4), Sky(4), state); monitor.Observe(Lights(5), Sky(5), state);
        Check(state.Health.Recovery?.State == "ready", "A fresh window did not complete after loading.");

        // Requested physics cannot borrow an old, partial or unknown measurement.
        foreach (var visibility in new[] {
            Visibility("unknown", 0, 9, 1), Visibility("clear", 10000, 9, 1),
            Visibility("clear", 0, 1, 1), Visibility("clear", 0, 9, 999) })
        {
            state = State(); monitor = new RecoveryLiveMonitor(15, false);
            for (ulong sequence = 1; sequence <= 3; sequence++)
            {
                var lights = Lights(sequence);
                var source = lights.Upstream!.Sources![0] with { SourceVisibility = visibility };
                lights = lights with { Upstream = lights.Upstream with { Sources = [source] } };
                monitor.Observe(lights, Sky(sequence), state);
            }
            Check(state.Health.Recovery?.State != "ready", "Invalid physics control completed recovery.");
        }
        state = State(); monitor = new RecoveryLiveMonitor(15, false);
        for (ulong sequence = 1; sequence <= 3; sequence++)
        {
            var lights = Lights(sequence);
            var source = lights.Upstream!.Sources![0] with { SourceVisibility = Visibility("clear", 0, 9, sequence) };
            lights = lights with { Upstream = lights.Upstream with { Sources = [source] } };
            monitor.Observe(lights, Sky(sequence), state);
        }
        Check(state.Health.Recovery?.State == "ready", "Fresh complete physics controls did not finish recovery.");
    }

    public static void HealthStatus()
    {
        var state = State();
        state.SetRecovery(new("scanning", "Checking updated game data", "Checking addresses.", 55));
        state.SetHealth("recovering", true, null, "fixture", 0, null, null);
        Check(state.Health.Recovery?.Progress == 55 && state.Health.SupportedBuild is null,
            "Updating health erased progress or advertised a tested recovered build.");
        var json = JsonSerializer.Serialize(state.Health, Json);
        using var parsed = JsonDocument.Parse(json);
        Check(parsed.RootElement.GetProperty("recovery").GetProperty("state").GetString() == "scanning" &&
            parsed.RootElement.GetProperty("recovery").GetProperty("progress").GetInt32() == 55,
            "Recovery health did not retain its neutral JSON shape.");
        state.SetRecovery(null);
        Check(state.Health.Recovery is null, "Explicit status clear failed.");
        state.SetRecovery(new("failed", "Recovery stopped", "Fixture failure."));
        state.SetHealth("waiting-for-game", false, null, null, 0, null, null);
        Check(state.Health.Recovery is null, "A stopped process retained a stale recovery warning.");
    }

    private static TelemetryServerState State()
    {
        var state = new TelemetryServerState(Json, 20, "1.6");
        state.SetRecovery(new("validating", "Checking live telemetry", "Waiting for fresh captures.", 90));
        return state;
    }
    private static EngineLightsSnapshot Lights(ulong sequence)
    {
        var now = DateTimeOffset.UtcNow;
        var camera = new CameraSnapshot(new(1, 2, 3), new(0, 1, 0), new(1, 0, 0), new(0, 0, 1), .1f, null, 60, 1.77f);
        var rendered = new RenderLightsSnapshot("available", "manylights", sequence, (uint)sequence, now, 0, camera,
            [new(0, new(1, 2, 5), new(1, .5f, .2f), .6f, "point", null, null)], new(1, 1, 0, 0));
        var upstream = new UpstreamLightsSnapshot("available", "manylights-input", sequence, (uint)sequence, now, 0, 1,
            [new(0, "standalone", null, new(1, 2, 5), new(1, .5f, .2f), .6f, "point", null, null, true, 0)],
            new(0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0));
        return new("available", "authored", 35, [], new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0), Rendered: rendered, Upstream: upstream);
    }
    private static SkyAmbientSnapshot Sky(ulong sequence) => new("available", CaptureSequence: sequence,
        FrameNumber: (uint)sequence, CapturedAt: DateTimeOffset.UtcNow, AgeMilliseconds: 0,
        Sky: new(Enumerable.Range(0, 3).Select(_ => new double[9]).ToArray(), [1, 1, 1], [1, 1, 1], [1, 1, 1], [1, 1, 1], 1));
    private static SourceVisibilitySnapshot Visibility(string status, long age, int count, ulong capture) =>
        new(status, status == "clear" ? 1 : null, null, new(1, 2, 3), capture, null, 1, age, null,
            "physics-ray-fan", count, count, 1, Environment.TickCount64 - age);
    private static byte[] Header()
    {
        var bytes = new byte[NativeRecoveryBridge.HeaderBytes];
        Put(bytes, 0, NativeRecoveryBridge.Magic); Put(bytes, 4, NativeRecoveryBridge.Version);
        Put(bytes, 8, NativeRecoveryBridge.Capacity); Put(bytes, 12, 1); Put(bytes, 20, Pid); Put(bytes, 224, 15);
        BitConverter.GetBytes(Born).CopyTo(bytes, 24);
        Encoding.ASCII.GetBytes(new string('A', 64)).CopyTo(bytes, 32);
        Encoding.ASCII.GetBytes(new string('B', 64)).CopyTo(bytes, 96);
        Encoding.ASCII.GetBytes("fixture-contract").CopyTo(bytes, 160);
        return bytes;
    }
    private static void Put(byte[] bytes, int offset, uint value) => BitConverter.GetBytes(value).CopyTo(bytes, offset);
    private static void Reject(Action action, string message)
    { try { action(); } catch (InvalidDataException) { return; } throw new InvalidOperationException(message); }
    private static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
}
