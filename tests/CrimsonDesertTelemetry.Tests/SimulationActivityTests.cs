using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using CrimsonDesertTelemetry.Cli;
using CrimsonDesertTelemetry.Core;

internal static class SimulationActivityTests
{
    private const int Pid = 42;
    private const long Born = 123;
    private const long Tick = 10000;
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private static void Check(bool value, string message)
    { if (!value) throw new InvalidOperationException(message); }
    private static void U32(byte[] b, int o, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(o), v);
    private static void U64(byte[] b, int o, ulong v) => BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(o), v);
    private static byte[] Bridge(long now = Tick, int pid = Pid, long born = Born)
    {
        var b = new byte[128];
        U32(b, 0, 0x54445443); U32(b, 4, 2); U32(b, 8, 128); U32(b, 12, 128);
        U64(b, 16, 2); U32(b, 24, (uint)pid); U32(b, 28, 2); U64(b, 32, (ulong)born);
        U64(b, 40, 50); U64(b, 48, (ulong)now); U64(b, 56, (ulong)(now - 800)); U64(b, 64, 800);
        U32(b, 72, 2); U32(b, 84, 15); U64(b, 96, 2);
        return b;
    }
    private static SimulationActivityObservation Decode(byte[] b, long now = Tick) =>
        SimulationActivityReader.Decode(b, Pid, Born, now);
    public static void Protocol()
    {
        var b = Bridge(); var paused = Decode(b);
        Check(paused is { State: "paused", AcquisitionPaused: true, ControlEnabled: true, Epoch: 2 },
            "A live observer with an idle simulation was not paused.");
        Check(paused.Envelope(1, "session", DateTimeOffset.UtcNow, Tick + 50).ObservationAgeMilliseconds == 50,
            "Observation freshness used time since simulation progress instead of publisher age.");
        Check(Decode(b, Tick + 350).AcquisitionPaused, "Freshness boundary expired too early.");
        Check(Decode(b, Tick + 351) is { State: "unknown", AcquisitionPaused: false, ControlEnabled: false },
            "A stale publisher held acquisition paused.");
        U32(b, 4, 1); U32(b, 84, 3);
        Check(Decode(b) is { State: "paused", AcquisitionPaused: false, ControlEnabled: false, Epoch: 0 },
            "Diagnostic-only version 1 controlled acquisition.");
        foreach (var mutation in new Action<byte[]>[] {
            x => U32(x, 0, 0), x => U32(x, 4, 3), x => U32(x, 8, 64), x => U32(x, 24, 99),
            x => U64(x, 32, 124), x => U64(x, 16, 3), x => U64(x, 48, Tick + 1),
            x => U32(x, 84, 31), x => U32(x, 84, 7), x => U64(x, 96, 0),
            x => U64(x, 104, Tick + 1), x => U32(x, 72, 1), x => U64(x, 64, 499) })
        {
            b = Bridge(); mutation(b); var rejected = false;
            try { _ = Decode(b); } catch (InvalidDataException) { rejected = true; }
            Check(rejected, "An inconsistent activity bridge was accepted.");
        }
        foreach (var status in new uint[] { 1, 3, 4 })
        {
            b = Bridge(); U32(b, 28, status);
            Check(Decode(b) is { State: "unknown", AcquisitionPaused: false }, "An inactive hook held pause.");
        }
        b = Bridge(); U32(b, 72, 0); U32(b, 84, 11); U64(b, 56, 0); U64(b, 64, ulong.MaxValue);
        Check(Decode(b) is { State: "unknown", AcquisitionPaused: false }, "Initial constant counter implied pause.");
    }
    public static void Boundary()
    {
        var boundary = new ActivityCaptureBoundary();
        Check(boundary.Observe(new("unknown", "waiting-for-progress", Tick, false, true, 1)) && boundary.Epoch == 1,
            "The valid initial epoch was lost before progression, blocking normal unknown acquisition.");
        Check(boundary.Observe(new("paused", "idle", Tick, true, true, 2)), "Pause did not change epoch.");
        Check(boundary.Observe(new("running", "ok", Tick + 100, false, true, 3, Tick + 100)) &&
            boundary.MinimumCaptureTickMs == Tick + 100, "Resume did not retain its capture boundary.");
        Check(!boundary.Observe(SimulationActivityObservation.Unknown("observer-stale")) &&
            boundary.Epoch == 3 && boundary.MinimumCaptureTickMs == Tick + 100,
            "A missing observer re-enabled pre-resume captures.");
    }
    public static void Transport()
    {
        var state = new TelemetryServerState(Json, 60, "1.6");
        using var stream = state.SubscribeActivity();
        var tick = Environment.TickCount64;
        state.BeginActivitySession("A");
        state.PublishActivity("A", new("running", "progress", tick, false, true, 1));
        var snapshot = new TelemetrySnapshot("1.6", 1, DateTimeOffset.UtcNow, new("fixture", "playing"),
            new("game-unit", "right", "y"), [], null, null, null, null);
        state.Publish(snapshot, 0, null, 1);
        Check(state.Latest is not null, "Running telemetry was rejected.");
        state.PublishActivity("A", new("paused", "idle", tick, true, true, 2));
        Check(state.Latest is null && state.LatestSmoothed.UnavailableReason == "simulation-paused" &&
            state.LatestVisible.UnavailableReason == "simulation-paused" && state.LatestSky.Reason == "simulation-paused",
            "Pause retained old raw, smoothed, visible or ambient data.");
        state.Publish(snapshot, 0, null, 1);
        Check(state.Latest is null, "An in-flight pre-pause publication was accepted.");
        state.PublishActivity("A", new("running", "progress", tick, false, true, 3, tick));
        state.Publish(snapshot, 0, null, 1);
        Check(state.Latest is null, "An old capture epoch was relabelled after resume.");
        state.Publish(snapshot with { Sequence = 2 }, 0, null, 3);
        Check(state.Latest?.Sequence == 2, "The resumed capture epoch could not publish.");
        Check(stream.Reader.TryRead(out var packet), "Independent activity stream did not publish.");
        using (var parsed = JsonDocument.Parse(packet!))
        {
            var p = parsed.RootElement;
            Check(p.GetProperty("schemaVersion").GetString() == "1.0" &&
                p.GetProperty("source").GetString() == "simulation-tick" && p.GetProperty("epoch").GetUInt64() == 3,
                "Activity JSON contract changed.");
        }
        var before = state.LatestActivity.Sequence;
        state.BeginActivitySession("B");
        state.PublishActivity("A", new("paused", "idle", tick, true, true, 2));
        Check(state.LatestActivity.State == "unknown", "An old process session revived pause.");
        state.PublishActivity("B", new("paused", "idle", tick - 1000, true, true, 2));
        Check(state.LatestActivity is { State: "unknown", AcquisitionPaused: false } &&
            state.LatestActivity.Sequence > before, "A new session reset host sequence or retained stale pause.");
    }
    public static void IndependentHeartbeat() => IndependentHeartbeatAsync().GetAwaiter().GetResult();
    private static async Task IndependentHeartbeatAsync()
    {
        using var process = Process.GetCurrentProcess();
        var pid = process.Id; var born = process.StartTime.ToFileTimeUtc();
        using var mapping = MemoryMappedFile.CreateNew(SimulationActivityReader.MappingName(pid), 128);
        using var writer = mapping.CreateViewAccessor();
        var b = Bridge(Environment.TickCount64, pid, born); writer.WriteArray(0, b, 0, b.Length);
        using (var reader = new SimulationActivityReader(pid, born))
        {
            Check(reader.Capture().AcquisitionPaused, "Read-only activity mapping was not decoded.");
            var after = new byte[128]; writer.ReadArray(0, after, 0, 128);
            Check(after.AsSpan().SequenceEqual(b), "The reader modified its native publisher.");
            U64(b, 16, 3); writer.WriteArray(0, b, 0, b.Length);
            Check(reader.Capture() is { State: "unknown", Reason: "bridge-changing" }, "Torn publication reused paused state.");
        }
        var state = new TelemetryServerState(Json, 60, "1.6");
        using var subscription = state.SubscribeActivity();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        b = Bridge(Environment.TickCount64, pid, born); writer.WriteArray(0, b, 0, b.Length);
        using (var session = new ActivitySession(process, state))
        {
            state.SetHealth("loading", true, true, "fixture", 0, null, "Pose unavailable");
            long previous = 0;
            for (var n = 0; n < 4; n++)
            {
                b = Bridge(Environment.TickCount64, pid, born); U64(b, 16, (ulong)(n + 2) * 2);
                writer.WriteArray(0, b, 0, b.Length);
                var packet = await subscription.Reader.ReadAsync(timeout.Token);
                var value = JsonSerializer.Deserialize<ActivitySnapshot>(packet, Json)!;
                Check(value.Sequence > previous && value.State == "paused" && value.AcquisitionPaused,
                    "Pose failure or acquisition pause stopped the separate heartbeat.");
                previous = value.Sequence;
            }
            U64(b, 16, 20); U64(b, 48, (ulong)(Environment.TickCount64 - 1000));
            U64(b, 56, (ulong)(Environment.TickCount64 - 1800)); writer.WriteArray(0, b, 0, b.Length);
            ActivitySnapshot valueAfterStale;
            do { valueAfterStale = JsonSerializer.Deserialize<ActivitySnapshot>(await subscription.Reader.ReadAsync(timeout.Token), Json)!; }
            while (valueAfterStale.State != "unknown");
            Check(!valueAfterStale.AcquisitionPaused, "Observer loss retained paused control.");
        }
        Check(state.LatestActivity is { State: "unknown", AcquisitionPaused: false, Reason: "session-ended" },
            "Session disposal retained native pause.");
    }
}
