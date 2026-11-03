using System.ComponentModel;
using System.Diagnostics;
using System.Numerics;

namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Native renderer source. Requires a hash-matched definition or one relocated by
/// BuildCompatibility. No heap scans or previous-process addresses.
/// </summary>
public sealed class EngineCameraReader
{
    public const string SourceName = "engine-render-camera";
    private const int SourceLength = 0x868;
    private readonly IReadOnlyProcessMemory _reader;
    private readonly ulong _mainGlobal, _cameraGlobal, _contextVtable, _cameraVtable;
    private readonly bool _directCamera;
    private readonly int _counterOffset, _sourcePointerOffset;
    private readonly Func<TimeSpan> _clock;
    private Chain? _lastChain;
    private uint _lastCounter;
    private TimeSpan _lastProgress;

    public int AddressCount => _lastChain is null ? 0 : 1;
    public double ReferenceResolutionMilliseconds { get; }

    public EngineCameraReader(IReadOnlyProcessMemory reader, ulong moduleBase,
        EngineCameraDefinition definition, Func<TimeSpan>? clock = null)
    {
        if (definition.Layout is not ("renderer-camera-v1" or "renderer-camera-direct-v1"))
            throw new InvalidDataException("Unsupported native camera layout.");
        if (definition.SourcePointerOffset is <= 0 or > 0x10000 || (definition.SourcePointerOffset & 7) != 0)
            throw new InvalidDataException("Invalid native camera source-pointer offset.");
        _sourcePointerOffset = definition.SourcePointerOffset;
        _reader = reader;
        var watch = Stopwatch.StartNew();
        _clock = clock ?? (() => watch.Elapsed);
        _cameraGlobal = ResolveReference(moduleBase, definition.CameraReferenceRva,
            definition.CameraReferencePattern, definition.CameraGlobalRva, 0x05);
        _cameraVtable = At(moduleBase, definition.CameraVtableRva);
        _directCamera = definition.Layout == "renderer-camera-direct-v1";
        if (_directCamera)
        {
            if (definition.FrameCounterOffset is <= 0 or > 0x10000 || (definition.FrameCounterOffset & 3) != 0)
                throw new InvalidDataException("Invalid native camera frame-counter offset.");
            _counterOffset = definition.FrameCounterOffset;
        }
        else
        {
            _mainGlobal = ResolveReference(moduleBase, definition.MainRootReferenceRva,
                definition.MainRootReferencePattern, definition.MainRootGlobalRva, 0x0D);
            _contextVtable = At(moduleBase, definition.ContextVtableRva);
            _counterOffset = 0x40;
        }
        ReferenceResolutionMilliseconds = watch.Elapsed.TotalMilliseconds;
    }

    public RenderCameraFrame Capture((float X, float Y, float Z) playerPosition)
    {
        // Bounded retries only. Fail closed during loading, source replacement,
        // concurrent updates or a stalled render context; never fall back to stale copies.
        for (var attempt = 0; attempt < 3; attempt++)
        {
            try
            {
                var before = ReadChain();
                var contextCounter = Counter(before.Context);
                var sourceLength = _directCamera ? SceneConstantsDecoder.SourceLength : SourceLength;
                var first = _reader.Read(Pointer(before.Source), sourceLength);
                var second = _reader.Read(Pointer(before.Source), sourceLength);
                var after = ReadChain();
                if (before != after || contextCounter != Counter(after.Context) ||
                    (_directCamera ? !first.AsSpan().SequenceEqual(second) : !SameCameraFields(first, second)))
                    continue;
                var scene = _directCamera ? SceneConstantsDecoder.Decode(second, after.Source, playerPosition) : null;
                var camera = scene?.Camera ?? Decode(second, after.Source, playerPosition);
                // The scene object's counter can advance even when its constants
                // have stopped updating. The buffer's own frame is authoritative
                // for the measured direct layout; the older layout is unchanged.
                var counter = scene?.FrameNumber ?? contextCounter;
                var now = _clock();
                var relocated = _lastChain is not null && _lastChain != after;
                if (_lastChain is null || relocated || counter != _lastCounter)
                    _lastProgress = now;
                else if (now - _lastProgress >= TimeSpan.FromSeconds(1))
                    throw new InvalidDataException("Native camera source is not advancing.");
                _lastChain = after;
                _lastCounter = counter;
                // Existing v1 quality counts describe one directly validated source,
                // not two copies just because its fields were read twice.
                return new RenderCameraFrame(DateTimeOffset.UtcNow, camera, 1, 1, 1, relocated);
            }
            catch (Exception exception) when (exception is InvalidDataException or Win32Exception or OverflowException)
            {
                if (attempt == 2)
                    throw new InvalidDataException("Native camera is unavailable or changing: " + exception.Message, exception);
            }
        }
        throw new InvalidDataException("Native camera changed during all three read attempts.");
    }

    private ulong ResolveReference(ulong moduleBase, ulong instructionRva, string patternText,
        ulong targetRva, byte operand)
    {
        var pattern = SignaturePattern.Parse(patternText);
        if (pattern.Length is < 7 or > 64)
            throw new InvalidDataException("Invalid native camera instruction guard length.");
        var instruction = At(moduleBase, instructionRva);
        var bytes = _reader.Read(Pointer(instruction), pattern.Length);
        if (!pattern.FindAll(bytes).SequenceEqual([0]) || bytes[0] != 0x48 || bytes[1] != 0x8B || bytes[2] != operand)
            throw new InvalidDataException("Native camera instruction guard does not match this game.");
        var target = checked((ulong)(checked((long)instruction) + 7 + BitConverter.ToInt32(bytes, 3)));
        if (target != At(moduleBase, targetRva))
            throw new InvalidDataException("Native camera instruction points to an unexpected global.");
        return target;
    }

    private Chain ReadChain()
    {
        if (_directCamera)
        {
            var directCamera = ReadPointer(_cameraGlobal);
            if (ReadPointer(directCamera) != _cameraVtable)
                throw new InvalidDataException("Native camera object type disagrees.");
            return new Chain(0, 0, directCamera, directCamera, ReadPointer(At(directCamera, checked((ulong)_sourcePointerOffset))));
        }

        var main = ReadPointer(_mainGlobal);
        var application = ReadPointer(At(main, 0x28));
        var context = ReadPointer(At(application, 0x18));
        var camera = ReadPointer(At(context, 0xE0));
        if (camera != ReadPointer(_cameraGlobal) || ReadPointer(context) != _contextVtable ||
            ReadPointer(camera) != _cameraVtable)
            throw new InvalidDataException("Native camera roots or object types disagree.");
        return new Chain(main, application, context, camera, ReadPointer(At(camera, checked((ulong)_sourcePointerOffset))));
    }

    private uint Counter(ulong context) => BitConverter.ToUInt32(
        _reader.Read(Pointer(At(context, checked((ulong)_counterOffset))), 4));
    private ulong ReadPointer(ulong address)
    {
        var value = BitConverter.ToUInt64(_reader.Read(Pointer(address), 8));
        _ = Pointer(value);
        return value;
    }

    private static bool SameCameraFields(byte[] first, byte[] second)
    {
        foreach (var (offset, length) in CameraRanges)
            if (!first.AsSpan(offset, length).SequenceEqual(second.AsSpan(offset, length))) return false;
        return true;
    }

    private static readonly (int Offset, int Length)[] CameraRanges =
        [(0x80, 12), (0x90, 12), (0x3E0, 8), (0x3F0, 8), (0x400, 8), (0x4E0, 64), (0x860, 4)];

    public static RenderCameraConstantsCandidate Decode(byte[] bytes, ulong address,
        (float X, float Y, float Z) player)
        => DecodeCamera(bytes, address, player, requirePlayerProximity: true);

    internal static RenderCameraConstantsCandidate DecodeCamera(byte[] bytes, ulong address,
        (float X, float Y, float Z)? player, bool requirePlayerProximity)
    {
        if (bytes.Length < SourceLength) throw new InvalidDataException("Truncated native camera source.");
        float F(int offset) => BitConverter.ToSingle(bytes, offset);
        Vector3 V(int offset) => new(F(offset), F(offset + 4), F(offset + 8));
        var position = V(0x80);
        var forward = V(0x90);
        var right = new Vector3(F(0x3E0), F(0x3F0), F(0x400));
        var up = new Vector3(F(0x3E4), F(0x3F4), F(0x404));
        var distance = player is { } reference
            ? Vector3.Distance(position, new Vector3(reference.X, reference.Y, reference.Z)) : float.NaN;
        if (!Finite(position) || (player is not null && !float.IsFinite(distance)) ||
            (requirePlayerProximity && (!float.IsFinite(distance) || distance > 50)) ||
            !Unit(right) || !Unit(up) || !Unit(forward) ||
            Math.Abs(Vector3.Dot(right, up)) > .01f || Math.Abs(Vector3.Dot(right, forward)) > .01f ||
            Math.Abs(Vector3.Dot(up, forward)) > .01f ||
            Math.Abs(Vector3.Dot(Vector3.Cross(right, up), forward) - 1) > .01f)
            throw new InvalidDataException("Native camera position or basis is implausible.");
        // Observed symmetric perspective projection; do not silently interpret
        // orthographic or asymmetric matrices as a supported perspective camera.
        for (var i = 0; i < 16; i++)
            if (!float.IsFinite(F(0x4E0 + i * 4))) throw new InvalidDataException("Non-finite camera projection.");
        foreach (var i in new[] { 1, 2, 3, 4, 6, 7, 8, 9, 12, 13, 15 })
            if (Math.Abs(F(0x4E0 + i * 4)) > .0001f) throw new InvalidDataException("Unsupported camera projection layout.");
        var sx = F(0x4E0); var sy = F(0x4F4);
        var near = F(0x860);
        var fov = 2 * MathF.Atan(1 / sy);
        var aspect = sy / sx;
        if (sx <= 0 || sy <= 0 || Math.Abs(F(0x50C) - 1) > .0001f ||
            !float.IsFinite(near) || near is < .001f or > 10 ||
            !float.IsFinite(fov) || fov is < .2f or > 3 ||
            !float.IsFinite(aspect) || aspect is < .5f or > 5)
            throw new InvalidDataException("Native camera projection values are implausible.");
        // Raw source +0x864 has not been validated as a finite far distance.
        // Use the existing candidate's unknown/unbounded marker, serialized as null.
        return new RenderCameraConstantsCandidate(address, ToVector(position), ToVector(up), ToVector(right),
            ToVector(forward), near, float.MaxValue, fov, aspect, distance);
    }

    private static bool Finite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);
    private static bool Unit(Vector3 v) => Finite(v) && Math.Abs(v.LengthSquared() - 1) < .005f;
    private static CameraVector3 ToVector(Vector3 v) => new(v.X, v.Y, v.Z);
    private static ulong At(ulong address, ulong offset)
    {
        _ = Pointer(address);
        if (offset > int.MaxValue) throw new InvalidDataException("Implausible native camera offset.");
        var result = checked(address + offset);
        _ = Pointer(result);
        return result;
    }
    private static IntPtr Pointer(ulong address) => address is >= 0x10000 and <= 0x00007FFFFFFFFFFF
        ? new IntPtr(checked((long)address)) : throw new InvalidDataException("Implausible native camera pointer.");
    private sealed record Chain(ulong Main, ulong Application, ulong Context, ulong Camera, ulong Source);
}
