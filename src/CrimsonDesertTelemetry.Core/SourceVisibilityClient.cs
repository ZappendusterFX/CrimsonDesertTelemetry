using System.IO.MemoryMappedFiles;

namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Sends the current player receiver and every known local source to the native
/// SDF worker, then attaches its latest position-matched result. The exchange is
/// asynchronous: raw authored and rendered records remain available throughout.
/// </summary>
public sealed class SourceVisibilityClient : IDisposable
{
    public const int MaximumTargets = 256;
    public const int HeaderBytes = 128;
    public const int QueryEntryBytes = 16;
    public const int ResultEntryBytes = 24;
    public const int QueryBytes = HeaderBytes + MaximumTargets * QueryEntryBytes;
    public const int ResultBytes = HeaderBytes + MaximumTargets * ResultEntryBytes;
    public const long MaximumAgeMilliseconds = 1500;
    public const float ReceiverHeight = 1f;
    private const uint QueryMagic = 0x51564443;
    private const uint ResultMagic = 0x52564443;
    private const uint Version = 1;
    private const float MergeRadiusSquared = .01f;
    private const float MatchRadiusSquared = .04f;
    private const float ReceiverMatchRadiusSquared = .5625f;

    private readonly int _processId;
    private readonly ulong _processStartFileTime;
    private readonly MemoryMappedFile _queryMapping;
    private readonly MemoryMappedViewAccessor _queryView;
    private MemoryMappedFile? _resultMapping;
    private MemoryMappedViewAccessor? _resultView;
    private long _resultRetryAfter;
    private ulong _querySequence;
    private ulong _minimumCaptureTick;
    public void ResetForActivityEpoch(long minimumCaptureTickMs)
    {
        _minimumCaptureTick = (ulong)Math.Max(0, minimumCaptureTickMs);
        ResetResult();
    }

    public SourceVisibilityClient(int processId, long processStartFileTime)
    {
        _processId = processId;
        _processStartFileTime = unchecked((ulong)processStartFileTime);
        _queryMapping = MemoryMappedFile.CreateOrOpen(
            $"Local\\CrimsonDesertTelemetry.VisibilityQuery.{processId}", QueryBytes,
            MemoryMappedFileAccess.ReadWrite);
        _queryView = _queryMapping.CreateViewAccessor(0, QueryBytes, MemoryMappedFileAccess.ReadWrite);
    }

    public EngineLightsSnapshot Apply((float X, float Y, float Z) player, EngineLightsSnapshot input)
    {
        var receiver = new CameraVector3(player.X, player.Y + ReceiverHeight, player.Z);
        var targets = CollectTargets(input, receiver);
        Publish(receiver, targets);
        var response = ReadResult();
        if (response is null) return input;

        SourceVisibilitySnapshot Resolve(CameraVector3 position)
        {
            if (response.Reason is not null)
                return Unknown(response.Reason, receiver, _querySequence);
            if (DistanceSquared(receiver, response.Receiver) > ReceiverMatchRadiusSquared)
                return Unknown("receiver-moved", receiver, _querySequence);
            var match = response.Entries
                .Where(entry => DistanceSquared(position, entry.Position) <= MatchRadiusSquared)
                .MinBy(entry => DistanceSquared(position, entry.Position));
            if (match is null)
                return Unknown(targets.Count == MaximumTargets ? "trace-budget-exceeded" : "waiting-for-current-source",
                    response.Receiver, response.QuerySequence);
            return Decode(match, response);
        }

        var sources = input.Sources?.Select(source => source with
        {
            SourceVisibility = Resolve(source.Position)
        }).ToArray();
        var rendered = input.Rendered;
        if (rendered?.Sources is not null)
            rendered = rendered with
            {
                Sources = rendered.Sources.Select(source => source with
                {
                    SourceVisibility = Resolve(source.Position)
                }).ToArray()
            };
        return input with { Sources = sources, Rendered = rendered };
    }

    private List<Target> CollectTargets(EngineLightsSnapshot input, CameraVector3 receiver)
    {
        var candidates = new List<CameraVector3>();
        if (input.Sources is not null) candidates.AddRange(input.Sources.Select(source => source.Position));
        if (input.Rendered?.Sources is not null) candidates.AddRange(input.Rendered.Sources.Select(source => source.Position));
        candidates.Sort((left, right) => DistanceSquared(left, receiver).CompareTo(DistanceSquared(right, receiver)));
        var targets = new List<Target>(Math.Min(candidates.Count, MaximumTargets));
        foreach (var position in candidates)
        {
            if (targets.Any(target => DistanceSquared(position, target.Position) <= MergeRadiusSquared)) continue;
            if (targets.Count == MaximumTargets) break;
            targets.Add(new Target((uint)targets.Count + 1, position));
        }
        return targets;
    }

    private void Publish(CameraVector3 receiver, IReadOnlyList<Target> targets)
    {
        var before = _queryView.ReadInt64(16);
        if ((before & 1) != 0) before++;
        _queryView.Write(16, before + 1);
        Thread.MemoryBarrier();
        _queryView.Write(0, QueryMagic);
        _queryView.Write(4, Version);
        _queryView.Write(8, (uint)HeaderBytes);
        _queryView.Write(12, (uint)QueryBytes);
        _queryView.Write(24, (uint)_processId);
        _queryView.Write(28, (uint)targets.Count);
        _queryView.Write(32, _processStartFileTime);
        _queryView.Write(40, ++_querySequence);
        _queryView.Write(48, unchecked((ulong)Environment.TickCount64));
        WriteVector(_queryView, 56, receiver);
        for (var index = 0; index < targets.Count; index++)
        {
            var offset = HeaderBytes + index * QueryEntryBytes;
            _queryView.Write(offset, targets[index].Id);
            WriteVector(_queryView, offset + 4, targets[index].Position);
        }
        Thread.MemoryBarrier();
        _queryView.Write(16, before + 2);
    }

    private Response? ReadResult()
    {
        try
        {
            if (_resultView is null)
            {
                if (Environment.TickCount64 < _resultRetryAfter) return null;
                _resultMapping = MemoryMappedFile.OpenExisting(
                    $"Local\\CrimsonDesertTelemetry.VisibilityResult.{_processId}", MemoryMappedFileRights.Read);
                _resultView = _resultMapping.CreateViewAccessor(0, ResultBytes, MemoryMappedFileAccess.Read);
            }
            for (var attempt = 0; attempt < 3; attempt++)
            {
                var before = _resultView.ReadUInt64(16);
                if ((before & 1) != 0) continue;
                Thread.MemoryBarrier();
                var bytes = new byte[ResultBytes];
                if (_resultView.ReadArray(0, bytes, 0, bytes.Length) != bytes.Length) return InvalidResult();
                Thread.MemoryBarrier();
                var after = _resultView.ReadUInt64(16);
                if (before != after || (after & 1) != 0 || BitConverter.ToUInt64(bytes, 16) != after) continue;
                return DecodeResponse(bytes);
            }
            return null;
        }
        catch (FileNotFoundException)
        {
            ResetResult();
            _resultRetryAfter = Environment.TickCount64 + 250;
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                           ArgumentException or InvalidDataException or OverflowException)
        {
            ResetResult();
            _resultRetryAfter = Environment.TickCount64 + 250;
            return null;
        }
    }

    private Response? DecodeResponse(byte[] bytes)
    {
        if (BitConverter.ToUInt32(bytes, 0) != ResultMagic || BitConverter.ToUInt32(bytes, 4) != Version ||
            BitConverter.ToUInt32(bytes, 8) != HeaderBytes || BitConverter.ToUInt32(bytes, 12) != ResultBytes ||
            BitConverter.ToUInt32(bytes, 24) != _processId || BitConverter.ToUInt64(bytes, 32) != _processStartFileTime)
            throw new InvalidDataException("Native source-visibility bridge identity disagrees.");
        var state = BitConverter.ToUInt32(bytes, 28);
        if (state > 3) throw new InvalidDataException("Unknown source-visibility bridge state.");
        var receiver = Vector(bytes, 88);
        if (!Plausible(receiver)) throw new InvalidDataException("Invalid source-visibility receiver.");
        if (state != 1)
            return new Response(0, receiver, 0, null, null, null, [],
                state == 0 ? "waiting-for-volume" : state == 2 ? "native-fault" : "game-stopped");

        var querySequence = BitConverter.ToUInt64(bytes, 40);
        var publishedTick = BitConverter.ToUInt64(bytes, 48);
        var requestTick = BitConverter.ToUInt64(bytes, 56);
        var volumeSequence = BitConverter.ToUInt64(bytes, 64);
        var volumeTick = BitConverter.ToUInt64(bytes, 72);
        var contextFrame = BitConverter.ToUInt32(bytes, 80);
        var count = BitConverter.ToUInt32(bytes, 84);
        var now = unchecked((ulong)Environment.TickCount64);
        if (_minimumCaptureTick != 0 && (requestTick <= _minimumCaptureTick || volumeTick <= _minimumCaptureTick)) return null;
        if (querySequence == 0 || publishedTick == 0 || publishedTick > now || requestTick > publishedTick ||
            now - publishedTick > MaximumAgeMilliseconds || count > MaximumTargets ||
            (volumeSequence == 0) != (volumeTick == 0) || volumeTick > publishedTick)
            throw new InvalidDataException("Invalid or stale source-visibility result.");
        var entries = new List<ResultEntry>((int)count);
        for (var index = 0; index < count; index++)
        {
            var offset = HeaderBytes + index * ResultEntryBytes;
            var id = BitConverter.ToUInt32(bytes, offset);
            var code = BitConverter.ToUInt32(bytes, offset + 4);
            var closest = BitConverter.ToSingle(bytes, offset + 8);
            var position = Vector(bytes, offset + 12);
            if (id == 0 || code > 10 || !float.IsFinite(closest) || !Plausible(position))
                throw new InvalidDataException("Invalid source-visibility result entry.");
            entries.Add(new ResultEntry(id, code, closest, position));
        }
        return new Response(querySequence, receiver, publishedTick, volumeSequence, volumeTick,
            contextFrame, entries, null);
    }

    private SourceVisibilitySnapshot Decode(ResultEntry entry, Response response)
    {
        if (entry.Code is 1 or 2 && response.VolumeSequence is not null && response.VolumeTick is not null)
            return new SourceVisibilitySnapshot(entry.Code == 1 ? "clear" : "blocked", entry.Code == 1 ? 1 : 0,
                null, response.Receiver, response.QuerySequence, response.VolumeSequence, response.ContextFrame,
                checked((long)(response.PublishedTick - response.VolumeTick.Value)), entry.Closest);
        return Unknown(entry.Code switch
        {
            0 => "waiting-for-volume", 3 => "uncovered", 4 => "too-short", 5 => "iteration-limit",
            6 => "invalid-sdf-sample", 7 => "trace-budget-exceeded", 8 => "outside-trace-radius",
            9 => "stale-volume", 10 => "invalid-context", _ => "invalid-metadata"
        }, response.Receiver, response.QuerySequence, response.VolumeSequence, response.ContextFrame,
            response.VolumeTick is null ? null : checked((long)(response.PublishedTick - response.VolumeTick.Value)));
    }

    private static SourceVisibilitySnapshot Unknown(string reason, CameraVector3 reference, ulong querySequence,
        ulong? volume = null, uint? frame = null, long? age = null) =>
        new("unknown", null, reason, reference, Math.Max(1, querySequence), volume, frame, age, null);

    private Response? InvalidResult() { ResetResult(); return null; }
    private void ResetResult()
    {
        _resultView?.Dispose();
        _resultView = null;
        _resultMapping?.Dispose();
        _resultMapping = null;
    }
    private static void WriteVector(MemoryMappedViewAccessor view, long offset, CameraVector3 value)
    {
        view.Write(offset, value.X); view.Write(offset + 4, value.Y); view.Write(offset + 8, value.Z);
    }
    private static CameraVector3 Vector(byte[] bytes, int offset) => new(
        BitConverter.ToSingle(bytes, offset), BitConverter.ToSingle(bytes, offset + 4), BitConverter.ToSingle(bytes, offset + 8));
    private static bool Plausible(CameraVector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) &&
        float.IsFinite(value.Z) && Math.Abs(value.X) <= 1_000_000 && Math.Abs(value.Y) <= 1_000_000 &&
        Math.Abs(value.Z) <= 1_000_000;
    private static float DistanceSquared(CameraVector3 left, CameraVector3 right)
    {
        var x = left.X - right.X; var y = left.Y - right.Y; var z = left.Z - right.Z;
        return x * x + y * y + z * z;
    }

    public void Dispose()
    {
        ResetResult();
        _queryView.Dispose();
        _queryMapping.Dispose();
    }

    private sealed record Target(uint Id, CameraVector3 Position);
    private sealed record ResultEntry(uint Id, uint Code, float Closest, CameraVector3 Position);
    private sealed record Response(ulong QuerySequence, CameraVector3 Receiver, ulong PublishedTick,
        ulong? VolumeSequence, ulong? VolumeTick, uint? ContextFrame, IReadOnlyList<ResultEntry> Entries, string? Reason);
}
