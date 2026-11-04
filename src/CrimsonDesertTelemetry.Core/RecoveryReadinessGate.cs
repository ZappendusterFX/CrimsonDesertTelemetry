namespace CrimsonDesertTelemetry.Core;

/// <summary>A stationary player is sufficient; initialization, stale frames and gaps are not.</summary>
public sealed class RecoveryReadinessGate
{
    private uint? _lastFrame;
    private TimeSpan? _first, _last;
    private int _frames;

    public int Progress => Math.Min(100, _frames * 100 / 8);

    public bool Observe(uint? frame, bool validPose, TimeSpan now)
    {
        if (!validPose || frame is null || (_last is { } last &&
            (now < last || now - last > TimeSpan.FromMilliseconds(500))))
        {
            Reset();
            return false;
        }
        if (_lastFrame == frame)
        {
            if (_last is { } previous && now - previous > TimeSpan.FromMilliseconds(500)) Reset();
            return false;
        }
        if (_lastFrame is { } previousFrame && unchecked(frame.Value - previousFrame) >= 0x80000000u)
        {
            Reset();
            return false;
        }
        _first ??= now;
        _last = now;
        _lastFrame = frame;
        _frames++;
        return _frames >= 8 && now - _first.Value >= TimeSpan.FromSeconds(1);
    }

    public void Reset() { _lastFrame = null; _first = _last = null; _frames = 0; }
}
