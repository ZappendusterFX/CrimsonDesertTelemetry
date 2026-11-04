using System.IO.MemoryMappedFiles;

namespace CrimsonDesertTelemetry.Core;

public sealed record SkyAmbientValues(
    double[][] CoefficientsWorking,
    double[] UpperHemisphereMeanWorking,
    double[] UpwardIrradianceOverPiWorking,
    double[] InverseMatrixMean,
    double[] InverseMatrixUpwardIrradianceOverPi,
    double Rec709MeanLuminanceEstimate);

/// <summary>
/// Camera sky visibility, produced by the spatial probe on ITS own cadence. The
/// frame and age are its own and must never be read as the sky sample's.
/// </summary>
public sealed record CameraSkyVisibility(double ValueWorking, uint FrameNumber, long AgeMilliseconds);

/// <summary>
/// Global sky times camera visibility. A working product estimate, not an exact
/// renderer term: the engine multiplies its environment cube by that factor, while
/// this multiplies an already hemisphere-averaged sky value. Both raw inputs stay
/// on the snapshot so the scaling question can be settled from measurements rather
/// than assumed here. Never normalise the visibility and never import the
/// renderer's internal constants.
/// </summary>
public sealed record LocalEnvironmentAmbientEstimate(double[]? RgbWorking, bool Available, bool Stale, string Basis);

/// <summary>Global sky only: not local room illumination, lux or final pixel color.</summary>
public sealed record SkyAmbientSnapshot(string Status, string? Reason = null,
    ulong? CaptureSequence = null, uint? FrameNumber = null, DateTimeOffset? CapturedAt = null,
    long? AgeMilliseconds = null, SkyAmbientValues? Sky = null,
    CameraSkyVisibility? Visibility = null,
    LocalEnvironmentAmbientEstimate? LocalEnvironmentAmbientEstimateWorking = null)
{
    public string SchemaVersion => "1.0";
    public string Source => "precompute-ambient-sky";
    public string Scope => "global-upper-hemisphere-sky";
    public string Units => "relative-shader-units";
    public bool LocalOcclusionIncluded => false;
    public bool ExposureNormalized => false;
    public bool DirectSunMoonSeparated => false;
    public string ColorInterpretation => "engine-working-rgb; inverse-matrix estimate, input primaries unverified";
}

/// <summary>Separate native mapping, PID/start identity, seqlock and freshness guards.</summary>
public sealed class SkyAmbientReader(int processId, long processStartFileTime,
    uint expectedProducerRva = SkyAmbientReader.SkyProducerRva) : IDisposable
{
    public const int HeaderBytes = 128, SceneBytes = 2816, PayloadBytes = 1024;
    public const int TotalBytes = HeaderBytes + SceneBytes + PayloadBytes;
    /// <summary>
    /// 2 added the camera visibility block. The check is exact on purpose: an older
    /// CLI paired with a newer plugin reports bridge-invalid, which is the safe
    /// failure, rather than reading the block as something else.
    /// </summary>
    public const uint BridgeVersion = 2;
    /// <summary>
    /// The producing hook's RVA, as the native side stamps it. Only the sky path
    /// (<c>AmbientHookRvas[0]</c>) may publish here; the second ambient path must be
    /// rejected rather than read as sky. This is an exact-executable address and moves
    /// with the game build: build 25246367 relocated it by +0x21C0 from 25116796.
    /// </summary>
    public const uint SkyProducerRva = 0x384BD77;
    // Byte offsets into the native header, pinned by static_assert on that side.
    private const int VisibilityValueOffset = 96, VisibilityStateOffset = 104,
        VisibilityFrameOffset = 108, VisibilityTickOffset = 112;
    private const uint VisibilityUnavailable = 0, VisibilityValid = 1, VisibilityFallback = 2;
    public const long MaximumAgeMilliseconds = 1500; // Three nominal 2Hz intervals; not 60Hz data.
    private MemoryMappedFile? _mapping;
    private MemoryMappedViewAccessor? _view;
    private byte[]? _cached;
    private ulong _lock;
    private long _retryAfter;

    public static SkyAmbientSnapshot Unavailable(string reason) => new("unavailable", reason);

    public SkyAmbientSnapshot Capture(long minimumCaptureTickMs = 0)
    {
        try
        {
            if (_view is null)
            {
                if (Environment.TickCount64 < _retryAfter) return Unavailable("bridge-missing");
                _mapping = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.Sky.{processId}", MemoryMappedFileRights.Read);
                _view = _mapping.CreateViewAccessor(0, TotalBytes, MemoryMappedFileAccess.Read);
            }
            for (var attempt = 0; attempt < 3; attempt++)
            {
                var before = _view.ReadUInt64(16);
                if ((before & 1) != 0) continue;
                Thread.MemoryBarrier();
                if (_cached is not null && before == _lock)
                    return Decode(_cached, processId, processStartFileTime, Environment.TickCount64,
                        expectedProducerRva, minimumCaptureTickMs);
                var bytes = new byte[TotalBytes];
                if (_view.ReadArray(0, bytes, 0, TotalBytes) != TotalBytes) throw new InvalidDataException("Short sky bridge.");
                Thread.MemoryBarrier();
                var after = _view.ReadUInt64(16);
                if (before != after || (after & 1) != 0 || BitConverter.ToUInt64(bytes, 16) != after) continue;
                var result = Decode(bytes, processId, processStartFileTime, Environment.TickCount64,
                    expectedProducerRva, minimumCaptureTickMs);
                _cached = bytes; _lock = after;
                return result;
            }
            return Unavailable("bridge-changing");
        }
        catch (FileNotFoundException) { Reset(); return Unavailable("bridge-missing"); }
        catch (Exception e) when (e is InvalidDataException or IOException or UnauthorizedAccessException or ArgumentException or OverflowException)
        { Reset(); return Unavailable("bridge-invalid"); }
    }

    public static SkyAmbientSnapshot Decode(byte[] bytes, int pid, long start, long now,
        uint expectedProducerRva = SkyProducerRva, long minimumCaptureTickMs = 0)
    {
        if (bytes.Length != TotalBytes || BitConverter.ToUInt32(bytes, 0) != 0x53445443 ||
            BitConverter.ToUInt32(bytes, 4) != BridgeVersion || BitConverter.ToUInt32(bytes, 8) != HeaderBytes ||
            BitConverter.ToUInt32(bytes, 12) != TotalBytes || (BitConverter.ToUInt64(bytes, 16) & 1) != 0 ||
            BitConverter.ToUInt32(bytes, 24) != pid || BitConverter.ToInt64(bytes, 32) != start ||
            BitConverter.ToUInt32(bytes, 68) != SceneBytes || BitConverter.ToUInt32(bytes, 72) != PayloadBytes)
            throw new InvalidDataException("Sky bridge version, bounds or process identity disagree.");
        var state = BitConverter.ToUInt32(bytes, 28);
        if (state > 5) throw new InvalidDataException("Unknown sky bridge state.");
        if (state != 1) return Unavailable(state switch
        {
            0 => "bridge-waiting", 2 => "unsupported-build", 3 => "native-fault",
            4 => "legacy-plugin-conflict", _ => "capture-disabled-or-stopped"
        });
        if (BitConverter.ToUInt32(bytes, 84) != 7 || BitConverter.ToUInt32(bytes, 76) != expectedProducerRva ||
            BitConverter.ToUInt64(bytes, 88) == 0 || BitConverter.ToUInt32(bytes, 80) != 0)
            throw new InvalidDataException("Sky sample lacks validated producer/fence/scene provenance.");
        var tick = BitConverter.ToInt64(bytes, 48);
        var published = BitConverter.ToInt64(bytes, 56);
        var sequence = BitConverter.ToUInt64(bytes, 40);
        if (tick < 0 || published < tick || now < published || sequence == 0)
            throw new InvalidDataException("Invalid sky sample timing.");
        if (minimumCaptureTickMs > 0 && tick <= minimumCaptureTickMs)
            return Unavailable("awaiting-post-resume-capture");
        var age = now - tick;
        if (age > MaximumAgeMilliseconds) return Unavailable("bridge-stale");
        var scene = SceneConstantsDecoder.Decode(bytes.AsSpan(HeaderBytes, SceneBytes).ToArray());
        if (scene.FrameNumber != BitConverter.ToUInt32(bytes, 64)) throw new InvalidDataException("Sky/scene frame mismatch.");
        var values = DecodePayload(bytes.AsSpan(HeaderBytes + SceneBytes, PayloadBytes));
        var visibility = DecodeVisibility(bytes, now, minimumCaptureTickMs);
        return new("available", null, sequence, scene.FrameNumber, DateTimeOffset.UtcNow.AddMilliseconds(-age), age,
            values, visibility, Combine(values, visibility));
    }

    /// <summary>
    /// The spatial probe publishes this on its own cadence, so it carries its own
    /// frame and age. A state other than valid yields null: the shader's fallback
    /// branch is one BY DEFINITION and is not a measurement, and treating it as one
    /// would restore full ambient exactly where the local sample is missing.
    /// </summary>
    public static CameraSkyVisibility? DecodeVisibility(byte[] bytes, long now, long minimumCaptureTickMs = 0)
    {
        if (BitConverter.ToUInt32(bytes, VisibilityStateOffset) != VisibilityValid) return null;
        var tick = BitConverter.ToInt64(bytes, VisibilityTickOffset);
        if (tick <= 0 || now < tick || tick <= minimumCaptureTickMs) return null;
        var value = BitConverter.ToDouble(bytes, VisibilityValueOffset);
        if (double.IsNaN(value) || value < 0 || value > 1) return null;
        return new(value, BitConverter.ToUInt32(bytes, VisibilityFrameOffset), now - tick);
    }

    /// <summary>
    /// Raw sky mean times raw visibility. Deliberately unnormalised: whether that
    /// is the right final scaling is an open measurement question, and both inputs
    /// stay on the snapshot so it can be answered from a controlled traverse.
    /// </summary>
    public static LocalEnvironmentAmbientEstimate Combine(SkyAmbientValues? sky, CameraSkyVisibility? visibility)
    {
        const string basis = "upperHemisphereMeanWorking * cameraSkyVisibilityWorking, both raw";
        if (sky is null) return new(null, false, false, basis);
        if (visibility is null) return new(null, false, false, basis);
        if (visibility.AgeMilliseconds > MaximumAgeMilliseconds) return new(null, false, true, basis);
        var rgb = new double[3];
        for (var c = 0; c < 3; c++) rgb[c] = sky.UpperHemisphereMeanWorking[c] * visibility.ValueWorking;
        return new(rgb, true, false, basis);
    }

    // Port of the independently tested scripts/AmbientSh.psm1. Keep the exact
    // float32 shader constants, signed SH, unclamped matrix inverse and units.
    public static SkyAmbientValues DecodePayload(ReadOnlySpan<byte> payload)
    {
        if (payload.Length != PayloadBytes) throw new InvalidDataException("Invalid sky payload size.");
        var coefficients = new double[3][];
        var mean = new double[3]; var up = new double[3];
        for (var c = 0; c < 3; c++)
        {
            coefficients[c] = new double[9];
            for (var j = 0; j < 9; j++)
            {
                var v = BitConverter.ToSingle(payload.Slice(j == 8 ? 96 + c * 4 : c * 32 + j * 4, 4));
                if (!float.IsFinite(v)) throw new InvalidDataException("Nonfinite sky coefficient.");
                coefficients[c][j] = v;
            }
            if (coefficients[c][0] < 0 || coefficients[c][1] > 0)
                throw new InvalidDataException("Sky coefficients contradict the validated hemisphere profile.");
            mean[c] = coefficients[c][0] / (2 * (double).282095f);
            up[c] = -coefficients[c][1] / (double).488603f;
        }
        var rgb = UndoMatrix(mean); var upward = UndoMatrix(up);
        return new(coefficients, mean, up, rgb, upward, rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722);
    }

    private static double[] UndoMatrix(double[] rgb)
    {
        double[,] a = {
            { .61312f, .33951f, .04737f, rgb[0] },
            { .07020f, .91636f, .01345f, rgb[1] },
            { .02062f, .10958f, .86980f, rgb[2] }
        };
        for (var c = 0; c < 3; c++)
        {
            var pivot = c;
            for (var r = c + 1; r < 3; r++) if (Math.Abs(a[r, c]) > Math.Abs(a[pivot, c])) pivot = r;
            for (var k = 0; k < 4; k++) (a[c, k], a[pivot, k]) = (a[pivot, k], a[c, k]);
            var divisor = a[c, c];
            for (var k = c; k < 4; k++) a[c, k] /= divisor;
            for (var r = 0; r < 3; r++)
            {
                if (r == c) continue;
                var factor = a[r, c];
                for (var k = c; k < 4; k++) a[r, k] -= factor * a[c, k];
            }
        }
        return [a[0, 3], a[1, 3], a[2, 3]];
    }
    private void Reset()
    {
        _cached = null; _view?.Dispose(); _view = null; _mapping?.Dispose(); _mapping = null;
        _retryAfter = Environment.TickCount64 + 500;
    }
    public void Dispose() => Reset();
}
