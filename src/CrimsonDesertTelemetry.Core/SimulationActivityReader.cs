using System.IO.MemoryMappedFiles;

namespace CrimsonDesertTelemetry.Core;

public sealed record ActivitySnapshot(string SchemaVersion, long Sequence, DateTimeOffset TimestampUtc,
    string SessionId, string State, string Source, double? ObservationAgeMilliseconds,
    bool AcquisitionPaused, ulong Epoch, string Reason);

/// <summary>Native publisher liveness is independent of simulation progression.</summary>
public sealed record SimulationActivityObservation(string State, string Reason, long? PublisherTickMs = null,
    bool AcquisitionPaused = false, bool ControlEnabled = false, ulong Epoch = 0, long ResumedAtTickMs = 0)
{
    public static SimulationActivityObservation Unknown(string reason) => new("unknown", reason);
    public SimulationActivityObservation Fresh(long now) => PublisherTickMs is { } tick &&
        (now < tick || now - tick > SimulationActivityReader.MaximumAgeMilliseconds)
        ? new("unknown", "observer-stale", PublisherTickMs) : this;
    public ActivitySnapshot Envelope(long sequence, string session, DateTimeOffset utc, long now)
    {
        var fresh = Fresh(now);
        return new("1.0", sequence, utc, session, fresh.State, "simulation-tick",
            fresh.PublisherTickMs is { } tick && now >= tick ? now - tick : null,
            fresh.AcquisitionPaused, fresh.Epoch, fresh.Reason);
    }
}

/// <summary>Read-only, PID/start-bound 128-byte observer. No game-build label gate.</summary>
public sealed class SimulationActivityReader(int pid, long processStartFileTime) : IDisposable
{
    public const int Bytes = 128;
    public const long MaximumAgeMilliseconds = 350;
    private MemoryMappedFile? _mapping;
    private MemoryMappedViewAccessor? _view;
    private long _retryAfter;
    public static string MappingName(int processId) => $"Local\\CrimsonDesertTelemetry.SimulationTicks.{processId}";
    public SimulationActivityObservation Capture()
    {
        try
        {
            if (_view is null)
            {
                if (Environment.TickCount64 < _retryAfter) return SimulationActivityObservation.Unknown("bridge-missing");
                _mapping = MemoryMappedFile.OpenExisting(MappingName(pid), MemoryMappedFileRights.Read);
                _view = _mapping.CreateViewAccessor(0, Bytes, MemoryMappedFileAccess.Read);
            }
            for (var attempt = 0; attempt < 3; attempt++)
            {
                var before = _view.ReadUInt64(16);
                if (before == 0 || (before & 1) != 0) continue;
                Thread.MemoryBarrier();
                var bytes = new byte[Bytes];
                if (_view.ReadArray(0, bytes, 0, Bytes) != Bytes) throw new InvalidDataException("Short activity bridge.");
                Thread.MemoryBarrier();
                var after = _view.ReadUInt64(16);
                if (before != after || BitConverter.ToUInt64(bytes, 16) != after || (after & 1) != 0) continue;
                return Decode(bytes, pid, processStartFileTime, Environment.TickCount64);
            }
            return SimulationActivityObservation.Unknown("bridge-changing");
        }
        catch (FileNotFoundException) { Reset(); return SimulationActivityObservation.Unknown("bridge-missing"); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or InvalidDataException or OverflowException)
        { Reset(); return SimulationActivityObservation.Unknown("bridge-invalid"); }
    }
    public static SimulationActivityObservation Decode(byte[] bytes, int pid, long born, long now)
    {
        if (bytes.Length != Bytes || BitConverter.ToUInt32(bytes, 0) != 0x54445443 ||
            BitConverter.ToUInt32(bytes, 4) is not (1 or 2) || BitConverter.ToUInt32(bytes, 8) != Bytes ||
            BitConverter.ToUInt32(bytes, 12) != Bytes || BitConverter.ToUInt64(bytes, 16) == 0 ||
            (BitConverter.ToUInt64(bytes, 16) & 1) != 0 || BitConverter.ToUInt32(bytes, 24) != pid ||
            BitConverter.ToInt64(bytes, 32) != born)
            throw new InvalidDataException("Activity bridge identity, version or publication disagrees.");
        var version = BitConverter.ToUInt32(bytes, 4);
        var status = BitConverter.ToUInt32(bytes, 28);
        var state = BitConverter.ToUInt32(bytes, 72);
        var flags = BitConverter.ToUInt32(bytes, 84);
        var observed = BitConverter.ToInt64(bytes, 48);
        var lastProgress = BitConverter.ToInt64(bytes, 56);
        var progressAge = BitConverter.ToUInt64(bytes, 64);
        if (status is < 1 or > 4 || state > 2 || (flags & ~(version == 1 ? 3u : 15u)) != 0 ||
            observed <= 0 || observed > now || lastProgress < 0 || lastProgress > observed)
            throw new InvalidDataException("Invalid activity status, flags or clock.");
        var epoch = version == 2 ? BitConverter.ToUInt64(bytes, 96) : 0;
        var resumed = version == 2 ? BitConverter.ToInt64(bytes, 104) : 0;
        var control = version == 2 && (flags & 8) != 0;
        var paused = version == 2 && (flags & 4) != 0;
        if (resumed < 0 || resumed > observed || (paused && (!control || state != 2)) ||
            (control && epoch == 0) || (state == 0 && (lastProgress != 0 || progressAge != ulong.MaxValue)) ||
            (state != 0 && (lastProgress == 0 || progressAge == ulong.MaxValue ||
                (state == 1 && progressAge >= 500) || (state == 2 && progressAge < 500))))
            throw new InvalidDataException("Inconsistent activity policy fields.");
        if (status != 2 || BitConverter.ToUInt32(bytes, 80) != 0 || (flags & 3) != 3)
            return new("unknown", status == 3 ? "observer-stopped" : status == 4 ? "observer-failed" : "observer-starting", observed);
        return new SimulationActivityObservation(state switch { 1 => "running", 2 => "paused", _ => "unknown" },
            state switch { 1 => "simulation-progressing", 2 => "simulation-idle", _ => "waiting-for-progress" },
            observed, paused, control, epoch, resumed).Fresh(now);
    }
    private void Reset()
    {
        _view?.Dispose(); _view = null; _mapping?.Dispose(); _mapping = null;
        _retryAfter = Environment.TickCount64 + 500;
    }
    public void Dispose() => Reset();
}

/// <summary>Retains the last confirmed resume boundary through unknown samples.</summary>
public sealed class ActivityCaptureBoundary
{
    public ulong Epoch { get; private set; }
    public long MinimumCaptureTickMs { get; private set; }
    public bool Observe(SimulationActivityObservation observation)
    {
        // A validated observer can be UnknownUntilProgress while control and its
        // epoch already exist. Accept that boundary without treating it as pause.
        if (!observation.ControlEnabled) return false;
        var changed = Epoch != observation.Epoch;
        Epoch = observation.Epoch;
        MinimumCaptureTickMs = Math.Max(MinimumCaptureTickMs, observation.ResumedAtTickMs);
        return changed;
    }
}
