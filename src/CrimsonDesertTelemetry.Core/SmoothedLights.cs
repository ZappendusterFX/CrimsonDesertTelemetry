using System.Text.Json.Serialization;

namespace CrimsonDesertTelemetry.Core;

public sealed record LightSmoothingOptions(double TimeConstantMilliseconds = 200, float GroupRadius = .15f)
{
    public void Validate()
    {
        if (!double.IsFinite(TimeConstantMilliseconds) || TimeConstantMilliseconds is < 0 or > 2000 ||
            !float.IsFinite(GroupRadius) || GroupRadius is < .01f or > 1f)
            throw new ArgumentOutOfRangeException(nameof(LightSmoothingOptions), "Smoothing: 0..2000 ms; grouping: 0.01..1 game units.");
    }
}

public sealed record SmoothedLightGroup(
    string TrackingId, CameraVector3 Position, CameraVector3 ColorLinear, float LuminanceLinear,
    CameraVector3 RawSumColorLinear, IReadOnlyList<RenderedLightSnapshot> Contributions);

public sealed record SmoothedLightsSnapshot(
    string SchemaVersion, string Status, string Source, string Coverage, DateTimeOffset PublishedAt,
    LightSmoothingOptions Settings,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] ulong? SourceCaptureSequence,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] uint? SourceFrameNumber,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] DateTimeOffset? CapturedAt,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] long? AgeMilliseconds,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyList<SmoothedLightGroup>? Sources,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? UnavailableReason = null);

/// <summary>
/// Derived local-light signal, not object identity or rendered pixel color.
/// Spatially bounded groups sum linear RGB, then apply time-based EMA once per
/// new GPU capture. Never mix authored records with ManyLights (double counting).
/// Not thread-safe: the host serializes calls with its publication lock.
/// </summary>
public sealed class SmoothedLightProcessor
{
    private readonly LightSmoothingOptions _options;
    private readonly string _session = Guid.NewGuid().ToString("N");
    private long _nextId;
    private ulong? _sequence;
    private DateTimeOffset _sourceTime;
    private RenderedLightSnapshot[] _previousInput = [];
    private SmoothedLightGroup[] _previous = [];
    private bool _upstream;
    private string Source => _upstream ? "spatially-grouped-manylights-input" : "spatially-grouped-filtered-manylights";
    private string Coverage => _upstream ? "current-engine-lights-before-view-selection" : "view-filtered-not-complete-360";

    public SmoothedLightProcessor(LightSmoothingOptions? options = null)
    {
        _options = options ?? new();
        _options.Validate();
    }

    public SmoothedLightsSnapshot Unavailable(string reason, DateTimeOffset now)
    {
        _sequence = null; _previous = []; _previousInput = [];
        return Envelope(now, null, null, null, null, null, reason);
    }

    /// <summary>
    /// Current pre-selection engine lights, including sources behind the camera. A
    /// configured input stream is used exclusively: never a silent per-sample fallback
    /// to the view-filtered output, whose different coverage would break tracking.
    /// </summary>
    public SmoothedLightsSnapshot ProcessUpstream(UpstreamLightsSnapshot input, DateTimeOffset now)
    {
        SelectSource(true);
        return Process(UpstreamLightDecoder.AsRendered(input, null), now, keepSource: true);
    }

    public SmoothedLightsSnapshot Process(RenderLightsSnapshot? input, DateTimeOffset now) =>
        Process(input, now, keepSource: false);

    private void SelectSource(bool upstream)
    {
        if (_upstream == upstream) return;
        _upstream = upstream; _sequence = null; _previous = []; _previousInput = [];
    }

    private SmoothedLightsSnapshot Process(RenderLightsSnapshot? input, DateTimeOffset now, bool keepSource)
    {
        if (!keepSource) SelectSource(false);
        if (input?.Status != "available" || input.Sources is null)
            return Unavailable(input?.UnavailableReason ?? "rendered-lights-unavailable", now);
        if (input.CaptureSequence is not > 0 || input.FrameNumber is null || input.CapturedAt is not { } timestamp ||
            input.AgeMilliseconds is null or < 0 or > RenderLightReader.MaximumAgeMilliseconds ||
            (now - timestamp).TotalMilliseconds is < 0 or > RenderLightReader.MaximumAgeMilliseconds ||
            input.Sources.Count > RenderLightReader.RawCount)
            return Unavailable("source-stale-or-invalid", now);
        if (input.Sources.Any(light => !Valid(light))) return Unavailable("source-invalid", now);
        var ordered = input.Sources.OrderBy(light => light.Position.X).ThenBy(light => light.Position.Y)
            .ThenBy(light => light.Position.Z).ThenBy(light => light.SampleIndex).ToArray();
        if (ordered.Select(light => light.SampleIndex).Distinct().Count() != ordered.Length)
            return Unavailable("source-duplicate-index", now);
        var age = Math.Max(input.AgeMilliseconds.Value, (long)(now - timestamp).TotalMilliseconds);
        if (_sequence == input.CaptureSequence)
        {
            // HTTP polls/60Hz host ticks must not smooth a repeated 20Hz sample
            // again. A radius-filter change on that same sample is not fresh GPU data.
            if (!_previousInput.SequenceEqual(ordered))
            {
                // Physics verdicts/ages advance independently of GPU captures.
                // Refresh that metadata without rejecting valid light data or
                // applying the RGB EMA again at the host publication rate.
                if (_previousInput.Length != ordered.Length ||
                    _previousInput.Where((before, index) => !SameLightPayload(before, ordered[index])).Any())
                    return Unavailable("repeated-source-changed", now);
                var current = ordered.ToDictionary(light => light.SampleIndex);
                _previous = _previous.Select(group => group with
                {
                    Contributions = group.Contributions.Select(light => current[light.SampleIndex]).ToArray()
                }).ToArray();
                _previousInput = ordered;
            }
            return Envelope(now, _sequence, input.FrameNumber, _sourceTime,
                Math.Max(age, (long)(now - _sourceTime).TotalMilliseconds), _previous);
        }
        if (_sequence is not null && (input.CaptureSequence < _sequence || timestamp <= _sourceTime ||
            (timestamp - _sourceTime).TotalMilliseconds > RenderLightReader.MaximumAgeMilliseconds))
            Unavailable("source-discontinuity", now); // New epoch: no interpolation from stale state.

        var radius = _options.GroupRadius;
        var groups = new List<Group>();
        var grid = new Dictionary<Cell, List<int>>();
        foreach (var light in ordered)
        {
            var cell = Cell.From(light.Position, radius);
            Group? selected = null;
            foreach (var index in Neighbors(grid, cell))
            {
                var candidate = groups[index];
                if (candidate.Fits(light.Position, radius) && (selected is null || candidate.Order < selected.Order))
                    selected = candidate;
            }
            if (selected is null)
            {
                selected = new Group(groups.Count, light.Position);
                Add(grid, cell, groups.Count); groups.Add(selected);
            }
            selected.Add(light);
        }

        // Spatial tracking only, never SampleIndex identity. One-to-one matching
        // is bounded to the immediately preceding capture; absent groups vanish.
        var matchRadius = Math.Max(.5f, radius * 2);
        var tracks = new Dictionary<Cell, List<int>>();
        for (var index = 0; index < _previous.Length; index++)
            Add(tracks, Cell.From(_previous[index].Position, matchRadius), index);
        var claimed = new bool[_previous.Length];
        var result = new List<SmoothedLightGroup>(groups.Count);
        var alpha = _sequence is null || _options.TimeConstantMilliseconds == 0 ? 1 :
            1 - Math.Exp(-(timestamp - _sourceTime).TotalMilliseconds / _options.TimeConstantMilliseconds);
        foreach (var group in groups)
        {
            var position = group.Center;
            var nearest = -1;
            var distance = (double)matchRadius * matchRadius;
            foreach (var index in Neighbors(tracks, Cell.From(position, matchRadius)))
            {
                var candidateDistance = DistanceSquared(position, _previous[index].Position);
                if (!claimed[index] && candidateDistance < distance)
                { nearest = index; distance = candidateDistance; }
            }
            var sum = group.Sum;
            var rgb = sum;
            string id;
            if (nearest >= 0)
            {
                claimed[nearest] = true;
                id = _previous[nearest].TrackingId;
                var before = _previous[nearest].ColorLinear;
                rgb = new((float)(before.X + alpha * (sum.X - before.X)),
                    (float)(before.Y + alpha * (sum.Y - before.Y)), (float)(before.Z + alpha * (sum.Z - before.Z)));
            }
            else id = $"{_session}:{++_nextId}";
            result.Add(new(id, position, rgb, Luminance(rgb), sum, group.Members.ToArray()));
        }
        _sequence = input.CaptureSequence; _sourceTime = timestamp;
        _previousInput = ordered; _previous = result.ToArray();
        return Envelope(now, _sequence, input.FrameNumber, timestamp, age, _previous);
    }

    private SmoothedLightsSnapshot Envelope(DateTimeOffset now, ulong? sequence, uint? frame,
        DateTimeOffset? captured, long? age, IReadOnlyList<SmoothedLightGroup>? groups, string? reason = null) =>
        new("1.0", reason is null ? "available" : "unavailable", Source, Coverage,
            now, _options, sequence, frame, captured, age, groups, reason);
    private static bool SameLightPayload(RenderedLightSnapshot a, RenderedLightSnapshot b) =>
        a.SampleIndex == b.SampleIndex && a.Position == b.Position && a.ColorLinear == b.ColorLinear &&
        a.LuminanceLinear == b.LuminanceLinear && a.Kind == b.Kind && a.Direction == b.Direction &&
        a.ConeHalfAngleDegrees == b.ConeHalfAngleDegrees;

    private static bool Valid(RenderedLightSnapshot light) => light.SampleIndex is >= 0 and < RenderLightReader.RawCount &&
        Finite(light.Position) && Finite(light.ColorLinear) && light.ColorLinear.X >= 0 && light.ColorLinear.Y >= 0 &&
        light.ColorLinear.Z >= 0 && float.IsFinite(light.LuminanceLinear) && light.LuminanceLinear >= 0 &&
        (light.Direction is not { } d || Finite(d)) &&
        (light.ConeHalfAngleDegrees is not { } cone || float.IsFinite(cone) && cone is >= 0 and <= 90);
    private static bool Finite(CameraVector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) &&
        float.IsFinite(value.Z) && Math.Abs(value.X) <= 10_000_000 && Math.Abs(value.Y) <= 10_000_000 && Math.Abs(value.Z) <= 10_000_000;
    private static float Luminance(CameraVector3 rgb) => rgb.X * .212671f + rgb.Y * .71516f + rgb.Z * .07216f;
    private static double DistanceSquared(CameraVector3 a, CameraVector3 b) =>
        Math.Pow((double)a.X - b.X, 2) + Math.Pow((double)a.Y - b.Y, 2) + Math.Pow((double)a.Z - b.Z, 2);

    private readonly record struct Cell(int X, int Y, int Z)
    {
        public static Cell From(CameraVector3 point, float scale) => new(
            (int)Math.Floor(point.X / (double)scale), (int)Math.Floor(point.Y / (double)scale), (int)Math.Floor(point.Z / (double)scale));
    }
    private static void Add(Dictionary<Cell, List<int>> grid, Cell cell, int index)
    {
        if (!grid.TryGetValue(cell, out var bucket)) grid[cell] = bucket = [];
        bucket.Add(index);
    }
    private static IEnumerable<int> Neighbors(Dictionary<Cell, List<int>> grid, Cell cell)
    {
        for (var x = -1; x <= 1; x++) for (var y = -1; y <= 1; y++) for (var z = -1; z <= 1; z++)
            if (grid.TryGetValue(new(cell.X + x, cell.Y + y, cell.Z + z), out var bucket))
                foreach (var index in bucket) yield return index;
    }
    private sealed class Group(int order, CameraVector3 seed)
    {
        public int Order { get; } = order;
        public List<RenderedLightSnapshot> Members { get; } = [];
        private CameraVector3 _min = seed, _max = seed;
        private double _x, _y, _z, _r, _g, _b;
        // Reuses HUD's .15gu proximity idea, but conservatively bounds the whole
        // group's box diagonal. No transitive chains; O(1) fit even for dense pools.
        public bool Fits(CameraVector3 p, float radius) => DistanceSquared(
            new(Math.Min(_min.X, p.X), Math.Min(_min.Y, p.Y), Math.Min(_min.Z, p.Z)),
            new(Math.Max(_max.X, p.X), Math.Max(_max.Y, p.Y), Math.Max(_max.Z, p.Z))) <= (double)radius * radius;
        public void Add(RenderedLightSnapshot light)
        {
            Members.Add(light); var p = light.Position;
            _min = new(Math.Min(_min.X, p.X), Math.Min(_min.Y, p.Y), Math.Min(_min.Z, p.Z));
            _max = new(Math.Max(_max.X, p.X), Math.Max(_max.Y, p.Y), Math.Max(_max.Z, p.Z));
            _x += p.X; _y += p.Y; _z += p.Z; _r += light.ColorLinear.X; _g += light.ColorLinear.Y; _b += light.ColorLinear.Z;
        }
        public CameraVector3 Center => new((float)(_x / Members.Count), (float)(_y / Members.Count), (float)(_z / Members.Count));
        public CameraVector3 Sum => new((float)_r, (float)_g, (float)_b);
    }
}
