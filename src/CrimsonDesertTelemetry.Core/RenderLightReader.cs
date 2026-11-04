using System.IO.MemoryMappedFiles;
using System.Numerics;

namespace CrimsonDesertTelemetry.Core;

/// <summary>One bridge sample: the filtered renderer output and, when requested, the
/// paired pre-selection ManyLights input of the same capture.</summary>
public sealed record RenderCapture(RenderLightsSnapshot Rendered, UpstreamLightsSnapshot? Upstream);

/// <summary>Consumes fenced, camera-paired samples from the unified in-process ASI.</summary>
public sealed class RenderLightReader(int processId, long processStartFileTime) : IDisposable
{
    public const string SourceName = "filtered-manylights";
    public const int HeaderBytes = 256;
    public const int SceneBytes = 2816;
    public const int RawCount = 32768;
    public const int Stride = 48;
    public const int CounterBytes = 256;
    public const int CounterOffset = HeaderBytes + SceneBytes + RawCount * Stride;
    public const int TotalBytes = CounterOffset + CounterBytes;
    public const int VisibilityEntryBytes = 8;
    public const int VisibilityTotalBytes = TotalBytes + RawCount * VisibilityEntryBytes;
    // Version 4 appends the full-capacity INPUT after the unchanged version-3 mapping.
    public const int InputOffset = VisibilityTotalBytes;
    public const int InputTotalBytes = InputOffset + RawCount * Stride;
    public const long MaximumVisibilityAgeMilliseconds = 1500;
    public const long MaximumAgeMilliseconds = 500;
    private MemoryMappedFile? _mapping;
    private MemoryMappedViewAccessor? _view;
    private byte[]? _lastBytes;
    private ulong _lastLock;
    private long _retryAfter;
    private int _mappingBytes;

    public RenderLightsSnapshot Capture((float X, float Y, float Z) player, float radius) =>
        CaptureAll(player, radius).Rendered;

    public RenderCapture CaptureAll((float X, float Y, float Z) player, float radius, long minimumCaptureTickMs = 0)
    {
        try
        {
            if (_view is null)
            {
                if (Environment.TickCount64 < _retryAfter) return new(Unavailable("bridge-missing"), null);
                _mapping = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.Render.{processId}",
                    MemoryMappedFileRights.Read);
                // Read the fixed header before choosing one of the bounded layouts.
                // Never trust a shared-memory length for allocation.
                using (var header = _mapping.CreateViewAccessor(0, HeaderBytes, MemoryMappedFileAccess.Read))
                    _mappingBytes = MappingSize(header.ReadUInt32(4), header.ReadUInt32(12));
                _view = _mapping.CreateViewAccessor(0, _mappingBytes, MemoryMappedFileAccess.Read);
            }
            for (var attempt = 0; attempt < 3; attempt++)
            {
                var before = _view.ReadUInt64(16);
                if ((before & 1) != 0) continue;
                Thread.MemoryBarrier();
                if (_lastBytes is not null && before == _lastLock)
                    return DecodeAll(_lastBytes, processId, processStartFileTime, Environment.TickCount64, player, radius, minimumCaptureTickMs);
                var bytes = new byte[_mappingBytes];
                if (_view.ReadArray(0, bytes, 0, bytes.Length) != bytes.Length)
                    throw new InvalidDataException("Truncated native render bridge.");
                Thread.MemoryBarrier();
                var after = _view.ReadUInt64(16);
                if (before != after || (after & 1) != 0 || BitConverter.ToUInt64(bytes, 16) != after)
                    continue;
                var decoded = DecodeAll(bytes, processId, processStartFileTime, Environment.TickCount64, player, radius, minimumCaptureTickMs);
                _lastBytes = bytes;
                _lastLock = after;
                return decoded;
            }
            // A writer in progress does not invalidate the preceding complete
            // capture. Reuse the existing one-capture cache, with its original
            // timestamp and the same 500 ms freshness limit, never partial bytes.
            return _lastBytes is null
                ? new(Unavailable("bridge-changing"), _mappingBytes == InputTotalBytes
                    ? UpstreamLightDecoder.Unavailable("bridge-changing") : null)
                : DecodeAll(_lastBytes, processId, processStartFileTime, Environment.TickCount64, player, radius, minimumCaptureTickMs);
        }
        catch (FileNotFoundException)
        {
            Reset();
            _retryAfter = Environment.TickCount64 + 500;
            return new(Unavailable("bridge-missing"), null);
        }
        catch (Exception exception) when (exception is InvalidDataException or IOException or UnauthorizedAccessException or
                                           ArgumentException or OverflowException)
        {
            Reset();
            _retryAfter = Environment.TickCount64 + 500;
            return new(Unavailable("bridge-invalid"), null);
        }
    }

    public static RenderLightsSnapshot Unavailable(string reason) => new(
        "unavailable", SourceName, null, null, null, null, null, null, new(0, 0, 0, 0), reason);

    public static RenderLightsSnapshot Decode(byte[] snapshot, int expectedPid, long expectedStartFileTime,
        long nowTickMs, (float X, float Y, float Z) player, float nearbyRadius) =>
        DecodeAll(snapshot, expectedPid, expectedStartFileTime, nowTickMs, player, nearbyRadius).Rendered;

    public static RenderCapture DecodeAll(byte[] snapshot, int expectedPid, long expectedStartFileTime,
        long nowTickMs, (float X, float Y, float Z) player, float nearbyRadius, long minimumCaptureTickMs = 0)
    {
        if (snapshot.Length < HeaderBytes ||
            snapshot.Length != MappingSize(BitConverter.ToUInt32(snapshot, 4), BitConverter.ToUInt32(snapshot, 12)) ||
            BitConverter.ToUInt32(snapshot, 0) != 0x52445443 || BitConverter.ToUInt32(snapshot, 8) != HeaderBytes ||
            (BitConverter.ToUInt64(snapshot, 16) & 1) != 0 ||
            BitConverter.ToUInt32(snapshot, 24) != expectedPid ||
            BitConverter.ToInt64(snapshot, 32) != expectedStartFileTime ||
            BitConverter.ToUInt32(snapshot, 68) != SceneBytes ||
            BitConverter.ToUInt32(snapshot, 72) != RawCount || BitConverter.ToUInt32(snapshot, 76) != Stride ||
            BitConverter.ToUInt32(snapshot, 116) != CounterBytes)
            throw new InvalidDataException("Native render bridge version, bounds or process identity disagree.");
        if (!float.IsFinite(player.X) || !float.IsFinite(player.Y) || !float.IsFinite(player.Z) ||
            !float.IsFinite(nearbyRadius) || nearbyRadius is <= 0 or > 100000)
            throw new InvalidDataException("Invalid rendered-light radius or player position.");

        var state = BitConverter.ToUInt32(snapshot, 28);
        if (state > 5) throw new InvalidDataException("Unknown native render bridge state.");
        var upstreamRequested = snapshot.Length == InputTotalBytes;
        RenderCapture Missing(string reason) =>
            new(Unavailable(reason), upstreamRequested ? UpstreamLightDecoder.Unavailable(reason) : null);
        if (state != 1)
            return Missing(state switch
            {
                0 => "bridge-waiting", 2 => "unsupported-build", 3 => "native-fault",
                4 => "legacy-plugin-conflict", 5 => "game-stopped", _ => "bridge-invalid"
            });
        if (BitConverter.ToUInt32(snapshot, 84) != 15 || BitConverter.ToUInt64(snapshot, 88) == 0 ||
            BitConverter.ToUInt64(snapshot, 96) == 0 ||
            BitConverter.ToUInt64(snapshot, 88) == BitConverter.ToUInt64(snapshot, 96))
            throw new InvalidDataException("Render sample lacks build, fence, scene or paired-counter validation.");
        var capturedTick = BitConverter.ToInt64(snapshot, 48);
        var publishedTick = BitConverter.ToInt64(snapshot, 56);
        var sequence = BitConverter.ToUInt64(snapshot, 40);
        if (sequence == 0 || capturedTick < 0 || publishedTick < capturedTick || nowTickMs < publishedTick)
            throw new InvalidDataException("Invalid native render timing or sequence.");
        if (minimumCaptureTickMs > 0 && capturedTick <= minimumCaptureTickMs)
            return Missing("awaiting-post-resume-capture");
        var age = nowTickMs - capturedTick;
        if (age > MaximumAgeMilliseconds) return Missing("bridge-stale");

        var scene = SceneConstantsDecoder.Decode(snapshot.AsSpan(HeaderBytes, SceneBytes).ToArray());
        if (scene.FrameNumber != BitConverter.ToUInt32(snapshot, 64))
            throw new InvalidDataException("Render sample and paired camera frame disagree.");
        var camera = scene.Camera;
        // ProcessManyLightsCS appends at RWByteAddressBuffer byte 4 (DWORD[1]).
        // InitSortingData[Indirect]CS independently limits valid keys to this
        // prefix. A pi marker in the retained capacity tail is NOT freshness.
        var validCount = BitConverter.ToUInt32(snapshot, CounterOffset + 4);
        if (validCount > RawCount)
            throw new InvalidDataException("Filtered light count exceeds its resource capacity.");
        var sources = new List<RenderedLightSnapshot>();
        var active = 0;
        var malformed = 0;
        var outside = 0;
        var radiusSquared = (double)nearbyRadius * nearbyRadius;
        for (var index = 0; index < validCount; index++)
        {
            var record = snapshot.AsSpan(HeaderBytes + SceneBytes + index * Stride, Stride);
            var marker = F(record, 12);
            if (marker == 0) continue;
            if (!float.IsFinite(marker) || Math.Abs(marker - MathF.PI) > .0001f)
            {
                malformed++;
                continue;
            }
            active++;
            var relative = new CameraVector3(F(record, 0), F(record, 4), F(record, 8));
            var rgb = new CameraVector3(F(record, 16), F(record, 20), F(record, 24));
            var world = new CameraVector3(camera.Position.X + relative.X,
                camera.Position.Y + relative.Y, camera.Position.Z + relative.Z);
            var luminance = rgb.X * .212671f + rgb.Y * .71516f + rgb.Z * .07216f;
            if (!Plausible(relative) || !Plausible(world) || !Plausible(rgb) ||
                rgb.X < 0 || rgb.Y < 0 || rgb.Z < 0 || !float.IsFinite(luminance))
            {
                malformed++;
                continue;
            }
            var dx = (double)world.X - player.X;
            var dy = (double)world.Y - player.Y;
            var dz = (double)world.Z - player.Z;
            if (dx * dx + dy * dy + dz * dz > radiusSquared)
            {
                outside++;
                continue;
            }
            var (kind, direction, halfAngle) = DecodeKind(H(record, 38),
                new Vector3(H(record, 40), H(record, 42), H(record, 44)));
            var visibility = snapshot.Length >= VisibilityTotalBytes
                ? DecodeVisibility(snapshot, index, publishedTick, sequence, camera.Position) : null;
            sources.Add(new RenderedLightSnapshot(index, world, rgb, luminance, kind, direction, halfAngle, visibility));
        }
        var capturedAt = DateTimeOffset.UtcNow.AddMilliseconds(-age);
        var rendered = new RenderLightsSnapshot("available", SourceName, sequence, scene.FrameNumber, capturedAt, age,
            new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
                camera.NearPlane, camera.FarPlane == float.MaxValue ? null : camera.FarPlane,
                camera.FieldOfViewRadians * 180 / MathF.PI, camera.AspectRatio),
            sources, new(active, sources.Count, malformed, outside));
        if (!upstreamRequested) return new(rendered, null);
        // The optional input never invalidates the proven filtered output of this sample.
        UpstreamLightsSnapshot upstream;
        var inputResource = BitConverter.ToUInt64(snapshot, 160);
        var inputState = BitConverter.ToUInt32(snapshot, 168);
        if (BitConverter.ToUInt32(snapshot, 172) != RawCount * Stride)
            upstream = UpstreamLightDecoder.Unavailable("input-invalid");
        else if (inputState == 1)
        {
            try
            {
                if (inputResource == 0 || inputResource == BitConverter.ToUInt64(snapshot, 88) ||
                    inputResource == BitConverter.ToUInt64(snapshot, 96))
                    throw new InvalidDataException("Input resource is missing or aliases output/counter.");
                upstream = UpstreamLightDecoder.Decode(snapshot.AsSpan(InputOffset, RawCount * Stride),
                    BitConverter.ToUInt32(snapshot, CounterOffset),
                    snapshot.AsSpan(HeaderBytes + SceneBytes, RawCount * Stride), validCount, camera.Position,
                    player, nearbyRadius, sequence, scene.FrameNumber, capturedAt, age);
            }
            catch (InvalidDataException) { upstream = UpstreamLightDecoder.Unavailable("input-invalid"); }
        }
        else upstream = UpstreamLightDecoder.Unavailable(inputState switch
        {
            0 => "input-disabled", 2 => "input-unavailable", 3 => "input-refused", _ => "input-invalid"
        });
        return new(rendered, upstream);
    }

    /// <summary>Packed half cone at +38 (-1 point) and look direction at +40..+44.</summary>
    internal static (string? Kind, CameraVector3? Direction, float? HalfAngleDegrees) DecodeKind(float cone, Vector3 look)
    {
        if (cone == -1) return ("point", null, null);
        if (!float.IsFinite(cone) || cone <= 0 || cone > MathF.PI / 2) return (null, null, null);
        CameraVector3? direction = null;
        if (float.IsFinite(look.X) && float.IsFinite(look.Y) && float.IsFinite(look.Z) &&
            Math.Abs(look.LengthSquared() - 1) <= .005f)
        {
            look = Vector3.Normalize(look);
            direction = new CameraVector3(look.X, look.Y, look.Z);
        }
        return ("spot", direction, cone * 180 / MathF.PI);
    }

    private static int MappingSize(uint version, uint totalBytes) => (version, totalBytes) switch
    {
        (2, TotalBytes) => TotalBytes,
        (3, VisibilityTotalBytes) => VisibilityTotalBytes,
        (4, InputTotalBytes) => InputTotalBytes,
        _ => throw new InvalidDataException("Unsupported native render bridge layout.")
    };

    private static SourceVisibilitySnapshot DecodeVisibility(byte[] bytes, int index, long publishedTick,
        ulong lightSequence, CameraVector3 referencePosition)
    {
        SourceVisibilitySnapshot Unknown(string reason, ulong? volume = null, uint? frame = null, long? age = null) =>
            new("unknown", null, reason, referencePosition, lightSequence, volume, frame, age, null);

        // An invalid optional block must not discard healthy raw source records.
        if (BitConverter.ToUInt32(bytes, 120) != 1 || BitConverter.ToUInt32(bytes, 124) != VisibilityEntryBytes ||
            BitConverter.ToUInt32(bytes, 148) != MaximumVisibilityAgeMilliseconds ||
            BitConverter.ToUInt32(bytes, 152) != 256 || BitConverter.ToUInt32(bytes, 156) != 0)
            return Unknown("invalid-metadata");
        var volumeSequence = BitConverter.ToUInt64(bytes, 128);
        var volumeTick = BitConverter.ToUInt64(bytes, 136);
        var frame = BitConverter.ToUInt32(bytes, 144);
        if ((volumeSequence == 0) != (volumeTick == 0) || volumeTick > (ulong)publishedTick ||
            (volumeSequence == 0 && frame != 0))
            return Unknown("invalid-metadata");
        ulong? volume = volumeSequence == 0 ? null : volumeSequence;
        uint? contextFrame = volume is null ? null : frame;
        long? age = volume is null ? null : publishedTick - (long)volumeTick;
        var offset = TotalBytes + index * VisibilityEntryBytes;
        var code = BitConverter.ToUInt32(bytes, offset);
        if (code > 11) return Unknown("invalid-metadata");
        if (code is 1 or 2)
        {
            if (age is null) return Unknown("invalid-metadata");
            if (age > MaximumVisibilityAgeMilliseconds) return Unknown("stale-volume", volume, contextFrame, age);
            var closest = BitConverter.ToSingle(bytes, offset + 4);
            if (!float.IsFinite(closest) || (code == 1 && closest <= 0) || (code == 2 && closest > 0))
                return Unknown("invalid-metadata", volume, contextFrame, age);
            return new(code == 1 ? "clear" : "blocked", code == 1 ? 1 : 0, null,
                referencePosition, lightSequence, volume, contextFrame, age, closest);
        }
        return Unknown(code switch
        {
            0 => "waiting-for-volume", 3 => "uncovered", 4 => "too-short", 5 => "iteration-limit",
            6 => "invalid-sdf-sample", 7 => "trace-budget-exceeded", 8 => "outside-trace-radius",
            9 => "stale-volume", 10 => "invalid-context", 11 => "disabled", _ => "invalid-metadata"
        }, volume, contextFrame, age);
    }

    private static bool Plausible(CameraVector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) &&
        Math.Abs(value.X) <= 10_000_000 && Math.Abs(value.Y) <= 10_000_000 && Math.Abs(value.Z) <= 10_000_000;
    private static float F(ReadOnlySpan<byte> bytes, int offset) => BitConverter.ToSingle(bytes.Slice(offset, 4));
    private static float H(ReadOnlySpan<byte> bytes, int offset) =>
        (float)BitConverter.UInt16BitsToHalf(BitConverter.ToUInt16(bytes.Slice(offset, 2)));
    private void Reset()
    {
        _lastBytes = null;
        _mappingBytes = 0;
        _view?.Dispose();
        _view = null;
        _mapping?.Dispose();
        _mapping = null;
    }
    public void Dispose() => Reset();
}
