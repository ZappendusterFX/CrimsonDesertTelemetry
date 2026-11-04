using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;

internal static class RenderLightReaderTests
{
    private const int ProcessId = 1234;
    private const long ProcessStart = 133000000000000000;
    private const long CapturedTick = 10000;
    private const int HeaderBytes = 256;
    private const int SceneBytes = 2816;
    private const int RecordBytes = 48;
    private const int RecordCount = 32768;
    private const int CounterOffset = HeaderBytes + SceneBytes + RecordCount * RecordBytes;
    private const int TotalBytes = CounterOffset + 256;

    public static void DecodeFields()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 2, 3), (4, 2, 1), -1, (0, -1, 0));
        Record(bytes, 4, (2, 2, 3), (.5f, .25f, .125f), MathF.PI / 6, (0, -.6f, .8f));
        // The fourth colour component is not a second intensity multiplier.
        F(bytes, RecordOffset(0) + 28, 123);
        var result = Decode(bytes, CapturedTick + 100, (15, 20, 35), 10);
        Check(result.Status == "available" && result.Source == "filtered-manylights" &&
              result.CaptureSequence == 7 && result.FrameNumber == 42 && result.AgeMilliseconds == 100 &&
              result.CapturedAt is not null && result.UnavailableReason is null,
            "Fresh capture metadata was not preserved.");
        Check(result.Camera?.Position == new CameraVector3(10, 20, 30),
            "The light frame lost its paired camera.");
        var sources = result.Sources ?? throw new InvalidOperationException("Available capture has no sources.");
        Check(sources.Count == 2 && sources[0].SampleIndex == 0 && sources[1].SampleIndex == 4,
            "Inactive entries were published or capture-local record indices changed.");
        Check(sources[0].Position == new CameraVector3(11, 22, 33) &&
              sources[1].Position == new CameraVector3(12, 22, 33),
            "Relative light positions were not converted using the paired scene origin.");
        Check(sources[0].ColorLinear == new CameraVector3(4, 2, 1) &&
              Near(sources[0].LuminanceLinear, 4 * .212671f + 2 * .71516f + .07216f),
            "Current HDR colour/luminance was clamped or scaled by an unrelated component.");
        Check(sources[0].Kind == "point" && sources[0].Direction is null &&
              sources[0].ConeHalfAngleDegrees is null,
            "Point light was given a spot direction or cone.");
        Check(sources[1].Kind == "spot" && sources[1].Direction is CameraVector3 direction &&
              Near(direction.X, 0) && Near(direction.Y, -.6f, .001f) && Near(direction.Z, .8f, .001f) &&
              Near(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z, 1) &&
              sources[1].ConeHalfAngleDegrees is float cone && Near(cone, 30, .05f),
            "Packed half-precision spotlight direction/cone was not decoded and normalized.");
        Check(result.Diagnostics.ActiveRecords == 2 && result.Diagnostics.PublishedRecords == 2 &&
              result.Diagnostics.Malformed == 0 && result.Diagnostics.OutsideRadius == 0,
            "Clean capture diagnostics mismatch.");

        using var json = JsonDocument.Parse(JsonSerializer.Serialize(result, JsonOptions));
        var jsonSources = json.RootElement.GetProperty("sources");
        Check(!jsonSources[0].TryGetProperty("direction", out _) &&
              !jsonSources[0].TryGetProperty("coneHalfAngleDegrees", out _) &&
              jsonSources[1].TryGetProperty("direction", out _) &&
              jsonSources[1].TryGetProperty("coneHalfAngleDegrees", out _),
            "Rendered-light JSON did not omit inapplicable point fields.");
    }

    public static void Validation()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 0, 0), (1, .5f, .25f), -1, (0, 0, 1));
        Record(bytes, 1, (float.NaN, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 2, (1, 0, 0), (-1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 3, (1, 0, 0), (float.PositiveInfinity, 1, 1), -1, (0, 0, 1));
        Record(bytes, 4, (100, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 5, (2, 0, 0), (1, 1, 1), float.NaN, (0, 0, 1));
        Record(bytes, 6, (3, 0, 0), (1, 1, 1), .4f, (0, 0, 0));
        Record(bytes, 7, (float.NaN, 0, 0), (float.NaN, 1, 1), -1, (0, 0, 1));
        F(bytes, RecordOffset(7) + 12, 0);
        Record(bytes, 8, (4, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        F(bytes, RecordOffset(8) + 12, 1);
        var result = Decode(bytes);
        var sources = result.Sources ?? throw new InvalidOperationException("Valid records were all discarded.");
        Check(result.Status == "available" && sources.Count == 3 &&
              result.Diagnostics.PublishedRecords == 3 && result.Diagnostics.Malformed >= 3 &&
              result.Diagnostics.OutsideRadius == 1,
            "Invalid fields, inactive markers and radius filtering were not separated.");
        Check(sources.Select(source => source.SampleIndex).SequenceEqual([0, 5, 6]),
            "A malformed/inactive record leaked into the published light list.");
        Check(sources[1].Kind is null && sources[1].Direction is null &&
              sources[1].ConeHalfAngleDegrees is null,
            "Unknown packed cone was assigned a light type or direction.");
        Check(sources[2].Kind == "spot" && sources[2].Direction is null &&
              sources[2].ConeHalfAngleDegrees is not null,
            "Invalid look vector discarded an otherwise usable spotlight.");

        var radiusBytes = Snapshot();
        Record(radiusBytes, 0, (10, 0, 0), (0, 0, 0), -1, (0, 0, 1));
        Record(radiusBytes, 1, (10.01f, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        var radius = Decode(radiusBytes, nearbyRadius: 10);
        Check(radius.Sources is { Count: 1 } && radius.Sources[0].SampleIndex == 0 &&
              radius.Sources[0].LuminanceLinear == 0 && radius.Diagnostics.OutsideRadius == 1,
            "Radius boundary or a valid zero-intensity active record was mishandled.");
    }

    public static void ProtocolAndFreshness()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Check(Decode(bytes, CapturedTick + 500).Status == "available", "Exactly 500ms was rejected as stale.");
        AssertUnavailable(Decode(bytes, CapturedTick + 501), "A stale render sample was published.");
        ExpectInvalid(() => Decode(bytes, CapturedTick - 1), "A future capture timestamp was accepted.");
        var republished = (byte[])bytes.Clone();
        U64(republished, 56, CapturedTick + 5000);
        AssertUnavailable(Decode(republished, CapturedTick + 5000),
            "A recent publication timestamp made an old GPU capture appear fresh.");

        foreach (var state in new uint[] { 0, 2, 3, 4, 5 })
        {
            var inactive = (byte[])bytes.Clone();
            U32(inactive, 28, state);
            AssertUnavailable(Decode(inactive), $"Native state {state} exposed a light list.");
        }

        foreach (var (offset, value, description) in new (int Offset, uint Value, string Description)[]
        {
            (0, 0, "magic"), (4, 1, "old counterless version"), (8, 255, "header length"),
            (12, TotalBytes - 1, "total length"), (24, ProcessId + 1, "PID"),
            (28, 6, "unknown native state"), (68, SceneBytes - 1, "scene length"),
            (72, RecordCount - 1, "record count"), (76, RecordBytes - 1, "record stride"),
            (84, 7, "missing paired counter flag"), (84, 31, "unknown coherence flags"),
            (88, 0, "missing light resource"), (96, 0, "missing counter resource"),
            (96, 1000, "aliased light and counter resource"), (116, 0, "missing counter bytes")
        })
        {
            var corrupt = (byte[])bytes.Clone();
            U32(corrupt, offset, value);
            ExpectInvalid(() => Decode(corrupt), $"Malformed {description} was accepted.");
        }
        var differentProcess = (byte[])bytes.Clone();
        U64(differentProcess, 32, ProcessStart + 1);
        ExpectInvalid(() => Decode(differentProcess), "A recycled PID's mapping was accepted.");
        var torn = (byte[])bytes.Clone();
        U64(torn, 16, 3);
        ExpectInvalid(() => Decode(torn), "Odd/in-progress seqlock was accepted.");
        ExpectInvalid(() => Decode(bytes[..^1]), "Truncated mapping was accepted.");
        ExpectInvalid(() => Decode([.. bytes, 0]), "Oversized mapping was accepted.");

        var mismatchedFrame = (byte[])bytes.Clone();
        U32(mismatchedFrame, 64, 43);
        ExpectInvalid(() => Decode(mismatchedFrame), "Different scene/header frames were paired.");
        MappingFailureIsolation();
    }

    public static void ValidPrefix()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 0, 0), (1, .5f, .25f), -1, (0, 0, 1));
        Record(bytes, 1, (2, 0, 0), (.2f, .4f, 1), -1, (0, 0, 1));
        Record(bytes, 2, (3, 0, 0), (2, .5f, .1f), -1, (0, 0, 1));
        Record(bytes, 200, (4, 0, 0), (3, .5f, .1f), -1, (0, 0, 1));
        // A complete, plausible old tail survives the GPU compaction pass.
        // Neither input-count DWORD[0] nor downstream DWORD[2] is its valid length.
        U32(bytes, CounterOffset, 2236); U32(bytes, CounterOffset + 8, 777);
        U32(bytes, CounterOffset + 4, 2);
        var first = Decode(bytes);
        Check(first.Sources is { Count: 2 } && first.Diagnostics.ActiveRecords == 2 &&
              first.Diagnostics.Malformed == 0 && first.Diagnostics.OutsideRadius == 0,
            "The reader scanned a plausible retained tail or used the wrong GPU counter.");

        // Camera motion used to turn that stale tail into camera-attached ghosts.
        var moved = (byte[])bytes.Clone();
        F(moved, HeaderBytes + 0x80, 15);
        F(moved, HeaderBytes + 0x410, -15); // Matching world-view translation.
        var movingResult = Decode(moved, player: (15, 20, 30));
        Check(movingResult.Sources is { Count: 2 } && movingResult.Sources.All(light => light.SampleIndex < 2),
            "Camera movement resurrected retained tail records.");

        // Another resource bank may have a different prefix and equally old tail.
        U64(moved, 88, 3000); U64(moved, 96, 4000);
        U32(moved, CounterOffset + 4, 1);
        Check(Decode(moved, player: (15, 20, 30)).Sources is { Count: 1 },
            "Alternating resources reused the previous bank's valid length.");

        U32(bytes, CounterOffset + 4, 0);
        Check(Decode(bytes).Sources is { Count: 0 } && Decode(bytes).Diagnostics.ActiveRecords == 0,
            "An empty current prefix exposed old lights.");
        // Becoming valid must publish a record even with completely unchanged RGB.
        U32(bytes, CounterOffset + 4, 3);
        Check(Decode(bytes).Sources is { Count: 3 }, "A newly valid constant-color light was suppressed.");
        U32(bytes, CounterOffset + 4, RecordCount);
        Record(bytes, RecordCount - 1, (5, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Check(Decode(bytes).Sources is { Count: 5 }, "The maximum valid capacity was truncated.");
        U32(bytes, CounterOffset + 4, RecordCount + 1);
        ExpectInvalid(() => Decode(bytes), "An overflowing GPU count was clamped or accepted.");
        U32(bytes, CounterOffset + 4, uint.MaxValue);
        ExpectInvalid(() => Decode(bytes), "A negative/wrapped GPU count was accepted.");
    }

    public static void VisibilityMetadata()
    {
        var legacy = Snapshot();
        Record(legacy, 0, (1, 0, 0), (4, 2, 1), -1, (0, 0, 1));
        var oldLight = Decode(legacy).Sources!.Single();
        Check(oldLight.SourceVisibility is null, "Legacy v2 invented visibility metadata.");
        using (var json = JsonDocument.Parse(JsonSerializer.Serialize(oldLight, JsonOptions)))
            Check(!json.RootElement.TryGetProperty("sourceVisibility", out _), "Legacy JSON acquired a visibility field.");

        var bytes = VisibilitySnapshot();
        Record(bytes, 0, (1, 0, 0), (4, 2, 1), -1, (0, 0, 1));
        Record(bytes, 1, (2, 0, 0), (3, 1, 2), -1, (0, 0, 1));
        U32(bytes, TotalBytes, 1); F(bytes, TotalBytes + 4, .25f);
        U32(bytes, TotalBytes + 8, 2); F(bytes, TotalBytes + 12, -.03f);
        var result = Decode(bytes);
        var visible = result.Sources![0];
        var blocked = result.Sources[1];
        Check(visible with { SourceVisibility = null } == oldLight, "Visibility changed raw light data.");
        Check(visible.SourceVisibility is { Status: "clear", AttenuationFactor: 1, Reason: null,
                LightCaptureSequence: 7, VolumeSequence: 12, ContextFrame: 41,
                VolumeAgeMillisecondsAtCapture: 110, ClosestApproach: .25 } &&
            visible.SourceVisibility.ReferencePosition == result.Camera!.Position,
            "Clear metadata lost its paired camera, source capture or volume provenance.");
        Check(blocked.SourceVisibility is { Status: "blocked", AttenuationFactor: 0 } &&
            blocked.ColorLinear == new CameraVector3(3, 1, 2), "Blocked source was removed or its RGB was attenuated.");
        Check(Decode(bytes, CapturedTick + 200).Sources!.SequenceEqual(result.Sources),
            "A repeated GPU capture changed its visibility metadata while aging.");

        void Unknown(byte[] candidate, string? expectedReason = null)
        {
            var decoded = Decode(candidate);
            var actual = decoded.Sources![0];
            Check(decoded.Status == "available" && decoded.Sources.Count == 2 &&
                actual with { SourceVisibility = null } == oldLight,
                "Malformed optional visibility damaged healthy raw records.");
            Check(actual.SourceVisibility is { Status: "unknown", AttenuationFactor: null, ClosestApproach: null } &&
                (expectedReason is null || actual.SourceVisibility.Reason == expectedReason),
                "Unknown visibility supplied a false factor or lost its reason.");
        }
        foreach (var code in new uint[] { 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, uint.MaxValue })
        {
            var changed = (byte[])bytes.Clone(); U32(changed, TotalBytes, code); Unknown(changed);
        }
        foreach (var (offset, value) in new (int, uint)[] { (120, 2), (124, 4), (148, 8000), (152, 257), (156, 1) })
        {
            var changed = (byte[])bytes.Clone(); U32(changed, offset, value); Unknown(changed, "invalid-metadata");
        }
        foreach (var bad in new[] { float.NaN, float.PositiveInfinity, 0f, -.1f })
        {
            var changed = (byte[])bytes.Clone(); F(changed, TotalBytes + 4, bad); Unknown(changed, "invalid-metadata");
        }
        var wrongBlockedSign = (byte[])bytes.Clone();
        U32(wrongBlockedSign, TotalBytes, 2); F(wrongBlockedSign, TotalBytes + 4, .1f);
        Unknown(wrongBlockedSign, "invalid-metadata");
        var stale = (byte[])bytes.Clone(); U64(stale, 136, CapturedTick + 10 - 1501); Unknown(stale, "stale-volume");
        var boundary = (byte[])bytes.Clone(); U64(boundary, 136, CapturedTick + 10 - 1500);
        Check(Decode(boundary).Sources![0].SourceVisibility!.Status == "clear", "Exactly 1500 ms volume rejected.");
        var future = (byte[])bytes.Clone(); U64(future, 136, CapturedTick + 11); Unknown(future, "invalid-metadata");
        var absent = (byte[])bytes.Clone(); U64(absent, 128, 0); U64(absent, 136, 0); U32(absent, 144, 0);
        Unknown(absent, "invalid-metadata");
        U32(absent, TotalBytes, 11); Unknown(absent, "disabled");
        Check(Decode(absent).Sources![0].SourceVisibility is { VolumeSequence: null, ContextFrame: null,
            VolumeAgeMillisecondsAtCapture: null }, "Missing volume invented provenance.");
        var partial = (byte[])bytes.Clone(); U64(partial, 128, 0); Unknown(partial, "invalid-metadata");
        ExpectInvalid(() => Decode(bytes[..^1]), "Truncated v3 mapping accepted.");
        ExpectInvalid(() => Decode([.. bytes, 0]), "Oversized v3 mapping accepted.");
        var wrongLayout = (byte[])bytes.Clone(); U32(wrongLayout, 4, 2);
        ExpectInvalid(() => Decode(wrongLayout), "V2 header accepted with v3 payload size.");
    }

    public static void VisibilityMappingAndSmoothing()
    {
        var bytes = VisibilitySnapshot();
        Record(bytes, 0, (1, 0, 0), (4, 2, 1), -1, (0, 0, 1));
        Record(bytes, 1, (1.04f, 0, 0), (3, 1, 2), -1, (0, 0, 1));
        U32(bytes, TotalBytes, 1); F(bytes, TotalBytes + 4, .25f);
        U32(bytes, TotalBytes + 8, 2); F(bytes, TotalBytes + 12, -.03f);
        var timestamp = DateTimeOffset.UtcNow.AddMilliseconds(-100);
        var first = Decode(bytes) with { CapturedAt = timestamp };
        var later = Decode(bytes, CapturedTick + 200) with { CapturedAt = timestamp };
        var rawBytes = JsonSerializer.SerializeToUtf8Bytes(first, JsonOptions);
        var smoother = new SmoothedLightProcessor();
        var smooth = smoother.Process(first, timestamp.AddMilliseconds(100));
        var repeated = smoother.Process(later, timestamp.AddMilliseconds(200));
        Check(smooth.Status == "available" && repeated.Status == "available" && smooth.Sources!.Count == 1 &&
            repeated.Sources!.Single().ColorLinear == new CameraVector3(7, 3, 3),
            "Visibility changed smoothing or repeated capture validity.");
        Check(smooth.Sources![0].Contributions.SequenceEqual(first.Sources!) &&
            repeated.Sources![0].Contributions.SequenceEqual(first.Sources!), "Smoothed contributions lost per-source visibility.");
        Check(rawBytes.SequenceEqual(JsonSerializer.SerializeToUtf8Bytes(first, JsonOptions)), "Smoothing mutated the raw payload.");
        using (var json = JsonDocument.Parse(JsonSerializer.Serialize(repeated, JsonOptions)))
            Check(json.RootElement.GetProperty("sources")[0].GetProperty("contributions")[1]
                .GetProperty("sourceVisibility").GetProperty("status").GetString() == "blocked",
                "Smoothed JSON omitted source visibility.");

        U32(bytes, 24, (uint)Environment.ProcessId);
        var now = (ulong)Environment.TickCount64;
        U64(bytes, 48, now); U64(bytes, 56, now); U64(bytes, 136, now - 100);
        using var mapping = MemoryMappedFile.CreateNew($"Local\\CrimsonDesertTelemetry.Render.{Environment.ProcessId}",
            bytes.Length, MemoryMappedFileAccess.ReadWrite);
        using var writer = mapping.CreateViewAccessor(); writer.WriteArray(0, bytes, 0, bytes.Length);
        using var reader = new RenderLightReader(Environment.ProcessId, ProcessStart);
        var live = reader.Capture((10, 20, 30), 10);
        Check(live.Status == "available" && live.Sources![0].SourceVisibility?.Status == "clear",
            "Reader did not open the bounded v3 mapping from its header.");
        Check(reader.Capture((10, 20, 30), 10).Sources!.SequenceEqual(live.Sources!), "V3 mapping cache changed metadata.");
    }

    public static void PublicationRaces()
    {
        var bytes = VisibilitySnapshot();
        U32(bytes, 24, (uint)Environment.ProcessId);
        Record(bytes, 0, (1, 0, 0), (4, 2, 1), -1, (0, 0, 1));
        U32(bytes, TotalBytes, 2); F(bytes, TotalBytes + 4, -.03f);
        var now = (ulong)Environment.TickCount64;
        U64(bytes, 48, now); U64(bytes, 56, now); U64(bytes, 136, now - 100);
        using var mapping = MemoryMappedFile.CreateNew($"Local\\CrimsonDesertTelemetry.Render.{Environment.ProcessId}",
            bytes.Length, MemoryMappedFileAccess.ReadWrite);
        using var writer = mapping.CreateViewAccessor();
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using var reader = new RenderLightReader(Environment.ProcessId, ProcessStart);
        var first = reader.Capture((10, 20, 30), 10);
        Check(first.Status == "available" && first.Sources?.Count == 1, "Race fixture has no fresh capture.");

        // Leave the publication deliberately incomplete with changed RGB/geometry.
        U64(bytes, 16, 3); U64(bytes, 40, 8);
        F(bytes, RecordOffset(0) + 16, 99); U32(bytes, TotalBytes, 1); F(bytes, TotalBytes + 4, .25f);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using (var coldReader = new RenderLightReader(Environment.ProcessId, ProcessStart))
            Check(coldReader.Capture((10, 20, 30), 10).UnavailableReason == "bridge-changing",
                "A cold reader invented a capture from an unfinished publication.");
        var racing = reader.Capture((10, 20, 30), 10);
        Check(racing.Status == "available" && racing.CaptureSequence == first.CaptureSequence &&
              racing.Sources!.SequenceEqual(first.Sources!) && racing.AgeMilliseconds >= first.AgeMilliseconds,
            "A publication race dropped fresh lights, mixed metadata/RGB or refreshed their age.");
        U64(bytes, 16, 4); writer.WriteArray(0, bytes, 0, bytes.Length);
        var next = reader.Capture((10, 20, 30), 10);
        Check(next.CaptureSequence == 8 && next.Sources![0].ColorLinear.X == 99 &&
              next.Sources[0].SourceVisibility?.Status == "clear",
            "A completed new capture was hidden by the previous cache.");

        // A completed fault must replace the active cache even during a later race.
        U64(bytes, 16, 6); U32(bytes, 28, 3); writer.WriteArray(0, bytes, 0, bytes.Length);
        Check(reader.Capture((10, 20, 30), 10).UnavailableReason == "native-fault", "A stable fault was masked.");
        writer.Write(16, 7UL);
        Check(reader.Capture((10, 20, 30), 10).UnavailableReason == "native-fault", "An older active cache revived after fault.");

        U64(bytes, 16, 8); U32(bytes, 28, 1);
        now = (ulong)Environment.TickCount64;
        U64(bytes, 48, now); U64(bytes, 56, now); U64(bytes, 136, now - 100);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        Check(reader.Capture((10, 20, 30), 10).Status == "available", "New valid publication did not recover.");
        writer.Write(16, 9UL);
        Thread.Sleep((int)RenderLightReader.MaximumAgeMilliseconds + 25);
        Check(reader.Capture((10, 20, 30), 10).UnavailableReason == "bridge-stale",
            "A prolonged publication race extended capture freshness.");
    }

    public static void ResumeBoundary()
    {
        var bytes = Snapshot();
        var old = RenderLightReader.DecodeAll(bytes, ProcessId, ProcessStart, CapturedTick + 100,
            (10, 20, 30), 10, CapturedTick);
        Check(old.Rendered.UnavailableReason == "awaiting-post-resume-capture", "An equal/old tick passed resume.");
        var fresh = RenderLightReader.DecodeAll(bytes, ProcessId, ProcessStart, CapturedTick + 100,
            (10, 20, 30), 10, CapturedTick - 1);
        Check(fresh.Rendered.Status == "available", "A genuinely newer native capture was rejected.");
    }

    private static byte[] VisibilitySnapshot()
    {
        var bytes = new byte[RenderLightReader.VisibilityTotalBytes];
        Snapshot().CopyTo(bytes, 0); U32(bytes, 4, 3); U32(bytes, 12, (uint)bytes.Length);
        U32(bytes, 120, 1); U32(bytes, 124, 8); U64(bytes, 128, 12);
        U64(bytes, 136, CapturedTick - 100); U32(bytes, 144, 41); U32(bytes, 148, 1500); U32(bytes, 152, 256);
        return bytes;
    }

    private static RenderLightsSnapshot Decode(byte[] bytes, long now = CapturedTick + 100,
        (float X, float Y, float Z)? player = null, float nearbyRadius = 10) =>
        RenderLightReader.Decode(bytes, ProcessId, ProcessStart, now, player ?? (10, 20, 30), nearbyRadius);

    private static byte[] Snapshot()
    {
        var bytes = new byte[TotalBytes];
        U32(bytes, 0, 0x52445443); U32(bytes, 4, 2);
        U32(bytes, 8, HeaderBytes); U32(bytes, 12, TotalBytes);
        U64(bytes, 16, 2); U32(bytes, 24, ProcessId); U32(bytes, 28, 1);
        U64(bytes, 32, ProcessStart); U64(bytes, 40, 7);
        U64(bytes, 48, CapturedTick); U64(bytes, 56, CapturedTick + 10);
        U32(bytes, 64, 42); U32(bytes, 68, SceneBytes);
        U32(bytes, 72, RecordCount); U32(bytes, 76, RecordBytes); U32(bytes, 84, 15);
        U64(bytes, 88, 1000); U64(bytes, 96, 2000); U32(bytes, 116, 256);
        EngineCameraTests.SceneBytes().CopyTo(bytes, HeaderBytes);
        return bytes;
    }

    private static void Record(byte[] bytes, int index, (float X, float Y, float Z) position,
        (float R, float G, float B) colour, float cone, (float X, float Y, float Z) look)
    {
        var offset = RecordOffset(index);
        F(bytes, offset, position.X); F(bytes, offset + 4, position.Y); F(bytes, offset + 8, position.Z);
        F(bytes, offset + 12, MathF.PI);
        F(bytes, offset + 16, colour.R); F(bytes, offset + 20, colour.G); F(bytes, offset + 24, colour.B);
        F(bytes, offset + 28, .005f);
        H(bytes, offset + 32, 0); H(bytes, offset + 34, 1); H(bytes, offset + 36, 0);
        H(bytes, offset + 38, cone);
        H(bytes, offset + 40, look.X); H(bytes, offset + 42, look.Y); H(bytes, offset + 44, look.Z);
        U32(bytes, CounterOffset + 4, Math.Max(BitConverter.ToUInt32(bytes, CounterOffset + 4), (uint)index + 1));
    }

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    private static void MappingFailureIsolation()
    {
        var bytes = Snapshot();
        U32(bytes, 24, (uint)Environment.ProcessId);
        U32(bytes, 0, 0);
        using var mapping = MemoryMappedFile.CreateNew(
            $"Local\\CrimsonDesertTelemetry.Render.{Environment.ProcessId}", TotalBytes,
            MemoryMappedFileAccess.ReadWrite);
        using var writer = mapping.CreateViewAccessor();
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using (var malformedReader = new RenderLightReader(Environment.ProcessId, ProcessStart))
        {
            var result = malformedReader.Capture((10, 20, 30), 10);
            AssertUnavailable(result, "Malformed bridge prevented graceful optional-light failure.");
            Check(result.UnavailableReason == "bridge-invalid", "Malformed mapping lost its failure reason.");
        }

        U32(bytes, 0, 0x52445443);
        U64(bytes, 16, 4);
        var now = (ulong)Environment.TickCount64;
        U64(bytes, 48, now); U64(bytes, 56, now);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using var reader = new RenderLightReader(Environment.ProcessId, ProcessStart);
        Check(reader.Capture((10, 20, 30), 10).Status == "available", "Reader could not recover a valid mapping.");
        U64(bytes, 16, 6); U32(bytes, 28, 4); U32(bytes, 84, 0);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        var conflict = reader.Capture((10, 20, 30), 10);
        AssertUnavailable(conflict, "A status update reused the previous cached active capture.");
        Check(conflict.UnavailableReason == "legacy-plugin-conflict", "Native conflict status was lost.");
    }

    private static int RecordOffset(int index) => HeaderBytes + SceneBytes + index * RecordBytes;
    private static void U32(byte[] bytes, int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
    private static void U64(byte[] bytes, int offset, ulong value) => BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(offset), value);
    private static void F(byte[] bytes, int offset, float value) => BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(offset), value);
    private static void H(byte[] bytes, int offset, float value) => BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(offset), BitConverter.HalfToUInt16Bits((Half)value));
    private static bool Near(float actual, float expected, float tolerance = .0001f) => Math.Abs(actual - expected) < tolerance;
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void ExpectInvalid(Action action, string message)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
    private static void AssertUnavailable(RenderLightsSnapshot result, string message)
    {
        Check(result.Status == "unavailable" && result.Sources is null &&
              !string.IsNullOrWhiteSpace(result.UnavailableReason), message);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(result, JsonOptions));
        Check(!json.RootElement.TryGetProperty("sources", out _),
            "Unavailable render capture serialized sources as a plausible empty light list.");
    }
}
