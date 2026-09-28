using System.Collections.Concurrent;
using System.Text.Json;
using System.Threading.Channels;
using CrimsonDesertTelemetry.Core;

namespace CrimsonDesertTelemetry.Cli;

internal sealed record TelemetryHealth(
    string SchemaVersion,
    string Status,
    bool GameRunning,
    bool? SupportedBuild,
    string? GameBuild,
    int SampleRateHz,
    long? LastSequence,
    DateTimeOffset? LastCapture,
    int ConnectedClients,
    int DiscoveredCopies,
    double? DiscoveryMilliseconds,
    string? Error,
    CompatibilityInfo? Compatibility = null);

internal sealed class TelemetryServerState(JsonSerializerOptions jsonOptions, int sampleRateHz, string schemaVersion,
    LightSmoothingOptions? smoothingOptions = null)
{
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<Guid, (Channel<byte[]> Channel, int Feed)> _subscribers = new();
    private SkyAmbientSnapshot _sky = SkyAmbientReader.Unavailable("waiting-for-game");
    private readonly SmoothedLightProcessor _smoother = new(smoothingOptions);
    private readonly SmoothedLightProcessor _visibleSmoother = new(smoothingOptions);
    private SmoothedLightsSnapshot? _smoothed;
    private SmoothedLightsSnapshot? _visible;
    private ulong? _visibleInputSequence;
    private bool _visibleInputUpstream;
    private int[] _visibleAcceptedIndices = [];
    private TelemetrySnapshot? _latest;
    private byte[]? _latestBytes;
    private TelemetryHealth _health = new(schemaVersion, "waiting-for-game", false, null, null,
        sampleRateHz, null, null, 0, 0, null, null);

    public TelemetrySnapshot? Latest
    {
        get { lock (_gate) return _latest; }
    }

    public byte[]? LatestBytes
    {
        get { lock (_gate) return _latestBytes; }
    }

    public SmoothedLightsSnapshot LatestSmoothed
    {
        get
        {
            lock (_gate)
            {
                var now = DateTimeOffset.UtcNow;
                if (_smoothed?.CapturedAt is { } captured)
                {
                    var age = (now - captured).TotalMilliseconds;
                    if (age is < 0 or > RenderLightReader.MaximumAgeMilliseconds)
                        _smoothed = _smoother.Unavailable("source-stale", now);
                    else _smoothed = _smoothed with { AgeMilliseconds = Math.Max(_smoothed.AgeMilliseconds ?? 0, (long)age) };
                }
                return _smoothed ??= _smoother.Unavailable("waiting-for-game", now);
            }
        }
    }
    public byte[] LatestSmoothedBytes => JsonSerializer.SerializeToUtf8Bytes(LatestSmoothed, jsonOptions);

    public SmoothedLightsSnapshot LatestVisible
    {
        get
        {
            lock (_gate)
            {
                var now = DateTimeOffset.UtcNow;
                if (_visible?.CapturedAt is { } captured)
                {
                    var age = (now - captured).TotalMilliseconds;
                    if (age is < 0 or > RenderLightReader.MaximumAgeMilliseconds)
                    {
                        _visibleInputSequence = null;
                        _visibleAcceptedIndices = [];
                        _visible = VisibleEnvelope(_visibleSmoother.Unavailable("source-stale", now));
                    }
                    else _visible = _visible with { AgeMilliseconds = Math.Max(_visible.AgeMilliseconds ?? 0, (long)age) };
                }
                return _visible ??= VisibleEnvelope(_visibleSmoother.Unavailable("waiting-for-game", now));
            }
        }
    }
    public byte[] LatestVisibleBytes => JsonSerializer.SerializeToUtf8Bytes(LatestVisible, jsonOptions);

    public SkyAmbientSnapshot LatestSky
    {
        get
        {
            lock (_gate)
            {
                if (_sky.CapturedAt is { } captured)
                {
                    var age = (DateTimeOffset.UtcNow - captured).TotalMilliseconds;
                    _sky = age is < 0 or > SkyAmbientReader.MaximumAgeMilliseconds
                        ? SkyAmbientReader.Unavailable("source-stale")
                        : _sky with { AgeMilliseconds = Math.Max(_sky.AgeMilliseconds ?? 0, (long)age) };
                }
                return _sky;
            }
        }
    }
    public byte[] LatestSkyBytes => JsonSerializer.SerializeToUtf8Bytes(LatestSky, jsonOptions);
    public void PublishSky(SkyAmbientSnapshot snapshot)
    {
        lock (_gate)
        {
            _sky = _health.Status == "playing" ? snapshot : SkyAmbientReader.Unavailable(_health.Status);
            SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
        }
    }

    public TelemetryHealth Health
    {
        get
        {
            lock (_gate) return _health with { ConnectedClients = _subscribers.Count };
        }
    }

    public void SetHealth(string status, bool gameRunning, bool? supportedBuild, string? gameBuild,
        int discoveredCopies, double? discoveryMilliseconds, string? error)
    {
        lock (_gate)
        {
            _health = _health with
            {
                Status = status,
                GameRunning = gameRunning,
                SupportedBuild = supportedBuild,
                GameBuild = gameBuild,
                DiscoveredCopies = discoveredCopies,
                DiscoveryMilliseconds = discoveryMilliseconds,
                Error = error,
                Compatibility = !gameRunning || supportedBuild == false ? null : _health.Compatibility
            };
            if (status != "playing")
            {
                _smoothed = _smoother.Unavailable(status, DateTimeOffset.UtcNow);
                Send(JsonSerializer.SerializeToUtf8Bytes(_smoothed, jsonOptions), true);
                _visibleInputSequence = null;
                _visibleAcceptedIndices = [];
                _visible = VisibleEnvelope(_visibleSmoother.Unavailable(status, DateTimeOffset.UtcNow));
                SendFeed(JsonSerializer.SerializeToUtf8Bytes(_visible, jsonOptions), 3);
                _sky = SkyAmbientReader.Unavailable(status);
                SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
            }
        }
    }

    public void SetCompatibility(CompatibilityInfo compatibility)
    {
        lock (_gate) _health = _health with { Compatibility = compatibility };
    }

    public void Publish(TelemetrySnapshot snapshot, int discoveredCopies, double? discoveryMilliseconds)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(snapshot, jsonOptions);
        var gameRunning = snapshot.Game.State != "stopped";
        lock (_gate)
        {
            _latest = snapshot;
            _latestBytes = bytes;
            // Present Upstream means the input stream is configured; it then owns the
            // smoothed feed, including when a sample is unavailable.
            _smoothed = snapshot.Game.State != "playing"
                ? _smoother.Unavailable(snapshot.Game.State, DateTimeOffset.UtcNow)
                : snapshot.Lights?.Upstream is { } upstream
                    ? _smoother.ProcessUpstream(upstream, DateTimeOffset.UtcNow)
                    : _smoother.Process(snapshot.Lights?.Rendered, DateTimeOffset.UtcNow);
            _visible = ProcessVisible(snapshot, DateTimeOffset.UtcNow);
            _health = _health with
            {
                Status = snapshot.Game.State,
                GameRunning = gameRunning,
                // Private exact-hash diagnostics publish data but are not a
                // declaration that the game build is publicly supported.
                SupportedBuild = _health.Compatibility?.Mode == "research-exact" ? null : true,
                GameBuild = snapshot.Game.Build,
                LastSequence = snapshot.Sequence,
                LastCapture = snapshot.CapturedAt,
                DiscoveredCopies = discoveredCopies,
                DiscoveryMilliseconds = discoveryMilliseconds,
                Error = null
            };
            Send(JsonSerializer.SerializeToUtf8Bytes(_smoothed, jsonOptions), true);
            SendFeed(JsonSerializer.SerializeToUtf8Bytes(_visible, jsonOptions), 3);
            if (snapshot.Game.State != "playing")
            {
                _sky = SkyAmbientReader.Unavailable(snapshot.Game.State);
                SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
            }
        }
        Send(bytes, false);
    }

    private void Send(byte[] bytes, bool smoothed)
        => SendFeed(bytes, smoothed ? 1 : 0);

    // This is a *filtered* feed, not a fabricated physics verdict: unknown,
    // blocked and absent measurements contribute no RGB and cannot be tracked.
    private SmoothedLightsSnapshot ProcessVisible(TelemetrySnapshot snapshot, DateTimeOffset now)
    {
        if (snapshot.Game.State != "playing") return VisibleUnavailable(snapshot.Game.State, now);
        if (snapshot.Lights?.Upstream is { } upstream)
        {
            if (upstream.Status != "available" || upstream.Sources is null)
                return VisibleUnavailable(upstream.UnavailableReason ?? "upstream-lights-unavailable", now);
            if (upstream.Sources.Count > 0 && upstream.Sources.All(s => s.SourceVisibility is null))
                return VisibleUnavailable("visibility-unavailable", now);
            var selected = upstream with
            {
                Sources = upstream.Sources.Where(s => s.SourceVisibility?.Status == "clear").ToArray()
            };
            var accepted = selected.Sources.Select(s => s.SampleIndex).Order().ToArray();
            if (SameVisibleCapture(upstream.CaptureSequence, true, accepted, now)) return _visible!;
            ResetVisibleOnSelectionChange(accepted, now);
            var result = _visibleSmoother.ProcessUpstream(selected, now);
            _visibleInputSequence = result.Status == "available" ? upstream.CaptureSequence : null;
            _visibleInputUpstream = true;
            _visibleAcceptedIndices = accepted;
            return VisibleEnvelope(result);
        }
        var rendered = snapshot.Lights?.Rendered;
        if (rendered?.Status != "available" || rendered.Sources is null)
            return VisibleUnavailable(rendered?.UnavailableReason ?? "rendered-lights-unavailable", now);
        if (rendered.Sources.Count > 0 && rendered.Sources.All(s => s.SourceVisibility is null))
            return VisibleUnavailable("visibility-unavailable", now);
        var filtered = rendered with
        {
            Sources = rendered.Sources.Where(s => s.SourceVisibility?.Status == "clear").ToArray()
        };
        var renderedAccepted = filtered.Sources.Select(s => s.SampleIndex).Order().ToArray();
        if (SameVisibleCapture(rendered.CaptureSequence, false, renderedAccepted, now)) return _visible!;
        ResetVisibleOnSelectionChange(renderedAccepted, now);
        var output = _visibleSmoother.Process(filtered, now);
        _visibleInputSequence = output.Status == "available" ? rendered.CaptureSequence : null;
        _visibleInputUpstream = false;
        _visibleAcceptedIndices = renderedAccepted;
        return VisibleEnvelope(output);
    }

    private bool SameVisibleCapture(ulong? sequence, bool upstream, int[] accepted, DateTimeOffset now)
    {
        if (sequence is not > 0 || _visibleInputSequence != sequence || _visibleInputUpstream != upstream ||
            !_visibleAcceptedIndices.SequenceEqual(accepted) || _visible?.Status != "available" ||
            _visible.CapturedAt is not { } captured) return false;
        var age = (now - captured).TotalMilliseconds;
        if (age is < 0 or > RenderLightReader.MaximumAgeMilliseconds) return false;
        // The same GPU capture must not apply EMA again at the host's 60 Hz rate.
        _visible = _visible with { PublishedAt = now, AgeMilliseconds = Math.Max(_visible.AgeMilliseconds ?? 0, (long)age) };
        return true;
    }

    private void ResetVisibleOnSelectionChange(int[] accepted, DateTimeOffset now)
    {
        // Physics can flip clear <-> unknown between host publications of one
        // GPU capture. Rebuild immediately from the eligible contributions;
        // otherwise an EMA tail could retain RGB from a newly hidden light.
        if (_visible is { Status: "available" } && !_visibleAcceptedIndices.SequenceEqual(accepted))
            _visibleSmoother.Unavailable("visibility-selection-changed", now);
    }

    private SmoothedLightsSnapshot VisibleUnavailable(string reason, DateTimeOffset now)
    {
        _visibleInputSequence = null;
        _visibleAcceptedIndices = [];
        return VisibleEnvelope(_visibleSmoother.Unavailable(reason, now));
    }

    private static SmoothedLightsSnapshot VisibleEnvelope(SmoothedLightsSnapshot snapshot) => snapshot with
    {
        Source = snapshot.Source + "-clear-only",
        Coverage = snapshot.Coverage + ";visibility-clear-only"
    };

    private void SendFeed(byte[] bytes, int feed)
    {
        foreach (var entry in _subscribers.Values)
            if (entry.Feed == feed) entry.Channel.Writer.TryWrite(bytes);
    }

    public TelemetrySubscription Subscribe(bool smoothed = false)
        => SubscribeFeed(smoothed ? 1 : 0);
    public TelemetrySubscription SubscribeSky() => SubscribeFeed(2);
    public TelemetrySubscription SubscribeVisible() => SubscribeFeed(3);
    private TelemetrySubscription SubscribeFeed(int feed)
    {
        var id = Guid.NewGuid();
        var channel = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(1)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.DropOldest
        });
        if (!_subscribers.TryAdd(id, (channel, feed))) throw new InvalidOperationException("Could not add subscriber.");
        return new TelemetrySubscription(id, channel.Reader, this);
    }

    private void Unsubscribe(Guid id)
    {
        if (_subscribers.TryRemove(id, out var entry)) entry.Channel.Writer.TryComplete();
    }

    internal sealed class TelemetrySubscription(
        Guid id, ChannelReader<byte[]> reader, TelemetryServerState owner) : IDisposable
    {
        public ChannelReader<byte[]> Reader { get; } = reader;
        public void Dispose() => owner.Unsubscribe(id);
    }
}
