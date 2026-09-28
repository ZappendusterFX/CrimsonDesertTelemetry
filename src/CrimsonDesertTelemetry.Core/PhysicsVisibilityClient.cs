using System.IO.MemoryMappedFiles;

namespace CrimsonDesertTelemetry.Core;

/// <summary>Experimental sampled collision visibility, never an optical transmission measurement.
/// Only fresh camera-paired ManyLights are queried. Raw records/RGB remain untouched.</summary>
public sealed class PhysicsVisibilityClient : IDisposable
{
    public const string Method = "physics-ray-fan";
    public const int PacketBytes = 128, MaximumTargets = 256;
    public const int HeaderBytes = 32, BatchBytes = HeaderBytes + PacketBytes * MaximumTargets;
    // Latest measurement, not a prediction for today's camera pose. Do not
    // retain a hidden marker for seconds if queries stop delivering results.
    public const long MaximumAgeMilliseconds = 500;
    public const float DefaultRadius = 35, MaximumRadius = 500;
    private readonly float _radiusSquared;
    private readonly int _pid;
    private readonly ulong _born;
    private readonly Func<long> _now;
    private readonly MemoryMappedFile _query;
    private readonly MemoryMappedViewAccessor _write;
    private MemoryMappedFile? _result;
    private MemoryMappedViewAccessor? _read;
    private long _retry, _lastRequest;
    private ulong _sequence, _consumed;
    private byte[]? _pending;
    private bool _faulted;
    private readonly List<Entry> _cache = [];

    private sealed class Entry(CameraVector3 position)
    {
        public CameraVector3 Position = position, Receiver = position;
        public long Measured, LastRequested, LastChecked;
        public ulong Capture, MeasurementSequence;
        public uint Frame, Clear;
        public string Status = "unknown", Reason = "waiting-for-physics";
    }

    public PhysicsVisibilityClient(int processId, long processStartFileTime, Func<long>? tickCount = null,
        float radius = DefaultRadius)
    {
        if (!float.IsFinite(radius) || radius is < 1 or > MaximumRadius)
            throw new ArgumentOutOfRangeException(nameof(radius));
        _radiusSquared = radius * radius;
        _pid = processId; _born = unchecked((ulong)processStartFileTime);
        _now = tickCount ?? (() => Environment.TickCount64);
        _query = MemoryMappedFile.CreateOrOpen($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityQueryV2.{_pid}",
            BatchBytes, MemoryMappedFileAccess.ReadWrite);
        _write = _query.CreateViewAccessor(0, BatchBytes, MemoryMappedFileAccess.ReadWrite);
    }

    public EngineLightsSnapshot Apply((float X, float Y, float Z) player, EngineLightsSnapshot input)
    {
        var now = _now();
        ReadResponse(now);
        var rendered = input.Rendered;
        var upstream = input.Upstream;
        if (rendered?.Status != "available" || rendered.Camera is null || rendered.Sources is null ||
            rendered.CaptureSequence is not > 0 || rendered.AgeMilliseconds is not >= 0 or > 250)
        {
            _cache.Clear();
            return input with
            {
                Rendered = rendered is null ? null : rendered with
                    { Sources = rendered.Sources?.Select(s => s with { SourceVisibility = null }).ToArray() },
                Upstream = upstream is null ? null : upstream with
                    { Sources = upstream.Sources?.Select(s => s with { SourceVisibility = null }).ToArray() }
            };
        }
        // The pre-selection input of the SAME capture covers sources behind the camera.
        // Its derived group centre is the ray target; a paired filtered contribution
        // reuses that result instead of chasing its noisy per-frame member position.
        var current = upstream is { Status: "available", Sources: not null } &&
            upstream.CaptureSequence == rendered.CaptureSequence ? upstream.Sources : null;
        var renderedTarget = new Dictionary<int, CameraVector3>();
        foreach (var light in current ?? [])
            if (light.RenderedSampleIndex is { } index) renderedTarget[index] = light.Position;
        var camera = rendered.Camera.Position;
        var center = new CameraVector3(player.X, player.Y, player.Z);
        _cache.RemoveAll(e => now - Math.Max(e.Measured, e.LastRequested) > 5000);
        var targets = new List<CameraVector3>();
        // Match the HUD's player-centered sphere; rays still originate at the
        // paired camera. A third-person offset must not exclude edge markers.
        var positions = (current?.Select(light => light.Position) ?? []).Concat(rendered.Sources
            .Where(source => !renderedTarget.ContainsKey(source.SampleIndex)).Select(source => source.Position));
        foreach (var position in positions.OrderBy(p => DistanceSquared(p, center)))
        {
            if (DistanceSquared(position, center) > _radiusSquared) continue;
            if (targets.Any(p => DistanceSquared(p, position) <= .01f)) continue;
            if (targets.Count == MaximumTargets) break;
            targets.Add(position);
        }
        foreach (var position in targets)
            if (Find(position) is null && _cache.Count < MaximumTargets * 2) _cache.Add(new Entry(position));

        if (!_faulted && now - _lastRequest >= 50 &&
            (_pending is null || _consumed == _sequence || now - _lastRequest > 400))
        {
            // Refresh the scene together, rather than a 24-light round robin at
            // 20 lights/sec. Oldest attempts first if native exhausts its
            // shared time budget; skipped tail entries must not starve, and
            // invalid targets must not occupy the front on every round.
            var next = targets.Select(p => (Position: p, Entry: Find(p)))
                .Where(v => v.Entry is not null).OrderBy(v => v.Entry!.LastChecked).ToArray();
            if (next.Length > 0)
            {
                foreach (var item in next) item.Entry!.LastRequested = now;
                Publish(player, camera, next.Select(v => v.Position).ToArray(), rendered, now);
            }
        }
        SourceVisibilitySnapshot Resolve(CameraVector3 position)
        {
            var e = Find(position);
            var reason = _faulted ? "physics-stopped-restart-required" :
                DistanceSquared(position, center) > _radiusSquared ? "outside-physics-radius" :
                e is null ? "outside-physics-budget" :
                e.Measured == 0 ? e.Reason : now - e.Measured > MaximumAgeMilliseconds ? "stale-physics" : e.Reason;
            var status = reason.Length == 0 ? e!.Status : "unknown";
            // Preserve actual measurement origin/capture; never pretend an older ray
            // was executed on the current frame. HUD understands this method explicitly.
            return new SourceVisibilitySnapshot(status, status == "unknown" ? null : status == "blocked" ? 0 : 1,
                status == "unknown" ? reason : null, e?.Receiver ?? camera, e?.Capture > 0 ? e.Capture : rendered.CaptureSequence.Value,
                null, e?.Frame, e?.Measured > 0 ? now - e.Measured : null, null,
                Method, e?.Measured > 0 ? 9 : null, e?.Measured > 0 ? (int)e.Clear : null,
                e?.Measured > 0 ? e.MeasurementSequence : null, e?.Measured > 0 ? e.Measured : null);
        }
        return input with
        {
            Rendered = rendered with
            {
                Sources = rendered.Sources.Select(s => s with { SourceVisibility = Resolve(
                    renderedTarget.TryGetValue(s.SampleIndex, out var target) ? target : s.Position) }).ToArray()
            },
            Upstream = upstream is null ? null : upstream with
            {
                Sources = current is null
                    ? upstream.Sources?.Select(s => s with { SourceVisibility = null }).ToArray()
                    : current.Select(s => s with { SourceVisibility = Resolve(s.Position) }).ToArray()
            }
        };
    }

    private Entry? Find(CameraVector3 p) => _cache.Where(e => DistanceSquared(e.Position, p) <= .0144f)
        .MinBy(e => DistanceSquared(e.Position, p));

    private void Publish((float X, float Y, float Z) player, CameraVector3 camera, CameraVector3[] targets,
        RenderLightsSnapshot rendered, long now)
    {
        var p = new byte[BatchBytes];
        Put(p, 0, 0x50564443u); Put(p, 4, 2u); Put(p, 8, (uint)BatchBytes);
        Put(p, 12, (uint)targets.Length); Put(p, 24, ++_sequence);
        for (var n = 0; n < targets.Length; n++)
        {
            var o = HeaderBytes + n * PacketBytes;
            Put(p, o, 0x50564443u); Put(p, o+4, 1u); Put(p, o+8, 128u); Put(p, o+24, (uint)_pid);
            Put(p, o+32, _born); Put(p, o+40, _sequence); Put(p, o+48, (ulong)now);
            Put(p, o+64, rendered.CaptureSequence!.Value); Put(p, o+72, rendered.FrameNumber ?? 0);
            Put(p, o+84, (float)rendered.AgeMilliseconds!.Value);
            Vector(p, o+88, new(player.X, player.Y, player.Z)); Vector(p, o+100, camera); Vector(p, o+112, targets[n]);
        }
        var seq = _write.ReadInt64(16); if ((seq & 1) != 0) seq++;
        _write.Write(16, seq + 1); Thread.MemoryBarrier();
        _write.WriteArray(0, p, 0, 16); _write.WriteArray(24, p, 24, BatchBytes - 24);
        Thread.MemoryBarrier(); _write.Write(16, seq + 2);
        _pending = p; _lastRequest = now;
    }

    private void ReadResponse(long now)
    {
        if (_pending is null) return;
        try
        {
            if (_read is null)
            {
                if (now < _retry) return;
                _result = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityResultV2.{_pid}", MemoryMappedFileRights.Read);
                _read = _result.CreateViewAccessor(0, BatchBytes, MemoryMappedFileAccess.Read);
            }
            var p = new byte[BatchBytes];
            var seq = _read.ReadInt64(16); Thread.MemoryBarrier(); if ((seq & 1) != 0) return;
            _read.ReadArray(0, p, 0, BatchBytes); Thread.MemoryBarrier();
            if (_read.ReadInt64(16) != seq || BitConverter.ToInt64(p, 16) != seq) return;
            if (U32(p, 0) != 0x53564443 || U32(p, 4) != 2 || U32(p, 8) != BatchBytes ||
                U32(p, 12) != U32(_pending, 12) || U32(p, 12) is 0 or > MaximumTargets ||
                U64(p, 24) != _sequence || _sequence == _consumed) return;
            // Validate the entire batch BEFORE applying any entry. Every result
            // must belong to its exact request, including camera, source and age.
            for (var n = 0; n < U32(p, 12); n++)
            {
                var o = HeaderBytes + n * PacketBytes;
                if (U32(p,o) != 0x53564443 || U32(p,o+4) != 1 || U32(p,o+8) != PacketBytes ||
                    U32(p,o+24) != _pid || U64(p,o+32) != _born || U32(p,o+12) != 1 || U64(p,o+40) != _sequence ||
                    !p.AsSpan(o+64,12).SequenceEqual(_pending.AsSpan(o+64,12)) ||
                    !p.AsSpan(o+88,36).SequenceEqual(_pending.AsSpan(o+88,36)) || U64(p,o+48) != U64(_pending,o+48)) return;
                var completed = U64(p,o+56);
                if (completed < U64(p,o+48) || completed > (ulong)now || (ulong)now-completed > MaximumAgeMilliseconds) return;
            }
            _consumed = _sequence;
            for (var n = 0; n < U32(p,12); n++) ApplyResponse(p, HeaderBytes + n*PacketBytes, now);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            _read?.Dispose(); _result?.Dispose(); _read = null; _result = null; _retry = now + 250;
        }
    }
    private void ApplyResponse(byte[] batch, int offset, long now)
    {
            var p = batch.AsSpan(offset,PacketBytes).ToArray();
            var code = U32(p,28);
            if (code == 3) _faulted = true;
            var e = Find(Vector(p,112)); if (e is null) return;
            if (code == 4) // Budget skip is NOT a fresh measurement or a failed ray.
            { if (e.Measured == 0) e.Reason = "physics-budget-pending"; return; }
            e.LastChecked = now;
            if (code != 1 || U32(p, 76) != 9 || U32(p, 80) > 9)
            { e.Status = "unknown"; e.Reason = "physics-query-unavailable"; return; }
            var tick = U64(p,56);
            var receiver = Vector(p, 100);
            e.Clear = U32(p, 80);
            e.Status = e.Clear > 0 ? "clear" : "blocked";
            e.Reason = "";
            e.Receiver = receiver; e.Position = Vector(p, 112); e.Measured = (long)tick;
            e.MeasurementSequence = U64(p,40);
            e.Capture = U64(p, 64); e.Frame = U32(p, 72);
    }
    private static uint U32(byte[] p, int i) => BitConverter.ToUInt32(p, i);
    private static ulong U64(byte[] p, int i) => BitConverter.ToUInt64(p, i);
    private static void Put(byte[] p, int i, uint v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Put(byte[] p, int i, ulong v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Put(byte[] p, int i, float v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Vector(byte[] p, int i, CameraVector3 v) { Put(p, i, v.X); Put(p, i+4, v.Y); Put(p, i+8, v.Z); }
    private static CameraVector3 Vector(byte[] p, int i) => new(BitConverter.ToSingle(p,i), BitConverter.ToSingle(p,i+4), BitConverter.ToSingle(p,i+8));
    private static float DistanceSquared(CameraVector3 a, CameraVector3 b) =>
        (a.X-b.X)*(a.X-b.X)+(a.Y-b.Y)*(a.Y-b.Y)+(a.Z-b.Z)*(a.Z-b.Z);
    public void Dispose() { _read?.Dispose(); _result?.Dispose(); _write.Dispose(); _query.Dispose(); }
}
