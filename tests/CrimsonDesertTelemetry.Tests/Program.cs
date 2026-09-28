using CrimsonDesertTelemetry.Core;
using System.Text.Json;

if (args.Length != 0)
{
    if (args.Length == 1 && args[0] == "--physics-visibility")
    {
        PhysicsVisibilityTests.Exchange();
        Console.WriteLine("PASS raw physics measurements, rapid camera orbit, provenance, budget and expiry");
        return 0;
    }
    if (args.Length == 2 && args[0] == "--replay-sky") return SkyAmbientTests.Replay(args[1]);
    if (args.Length == 2 && args[0] == "--replay-camera-right") return CameraCopyReplay.Run(args[1]);
    if (args.Length is 6 or 7 && args[0] == "--replay-manylights-pair")
        return UpstreamLightTests.Replay(args[1], args[2], args[3], args[4], args[5], args.ElementAtOrDefault(6));
    Console.Error.WriteLine("Usage: tests [--physics-visibility | --replay-camera-right <private-trace.jsonl>]");
    return 2;
}

var tests = new (string Name, Action Run)[]
{
    ("sky SH math, color inverse and raw preservation", SkyAmbientTests.MathAndRaw),
    ("sky bridge protocol, producer identity and freshness", SkyAmbientTests.Protocol),
    ("camera visibility block, fallback rule and estimate staleness", SkyAmbientTests.Visibility),
    ("sky mapping cache and fault isolation", SkyAmbientTests.Mapping),
    ("sky HTTP/WS state isolation and loading invalidation", SkyAmbientTests.Transport),
    ("signature exact", SignatureExact),
    ("signature wildcard", SignatureWildcard),
    ("signature rejects invalid input", SignatureRejectsInvalidInput),
    ("camera constants", CameraConstants),
    ("camera rejects invalid basis", CameraRejectsInvalidBasis),
    ("camera rejects wrong sentinel", CameraRejectsWrongSentinel),
    ("camera rejects distant position", CameraRejectsDistantPosition),
    ("camera consensus", CameraConsensus),
    ("camera temporal bootstrap", CameraSelectionTests.Bootstrap),
    ("camera rejects historical majority", CameraSelectionTests.HistoricalMajority),
    ("camera follows rotating buffers", CameraSelectionTests.RotatingBuffers),
    ("camera preserves reverse turns and wrap", CameraSelectionTests.ReversalAndWrap),
    ("camera tracks position and projection", CameraSelectionTests.PositionAndProjection),
    ("camera retains stationary source", CameraSelectionTests.Stationary),
    ("camera rediscovers lost learned sources", CameraSelectionTests.LostSource),
    ("camera switches away from frozen source", CameraSelectionTests.FrozenSource),
    ("camera temporal sampling at 1 Hz", CameraSelectionTests.LowRate),
    ("tracker rediscovers camera", TrackerRediscoversCamera),
    ("native camera mapping and unknown far plane", EngineCameraTests.Mapping),
    ("native camera rebased cold start", EngineCameraTests.RebasedColdStart),
    ("native camera instruction guards", EngineCameraTests.InstructionGuards),
    ("native camera roots and vtables", EngineCameraTests.RootsAndTypes),
    ("native camera rejects invalid data", EngineCameraTests.InvalidData),
    ("native camera concurrent read budget", EngineCameraTests.ConcurrentUpdates),
    ("native camera loading and relocation", EngineCameraTests.LoadingAndRelocation),
    ("native camera freshness and counter wrap", EngineCameraTests.Freshness),
    ("native camera direct layout", EngineCameraTests.DirectCameraLayout),
    ("scene constants validate full camera", EngineCameraTests.SceneConstantsValidation),
    ("scene constants own frame freshness", EngineCameraTests.SceneConstantsFreshness),
    ("render lights decode paired world-space data", RenderLightReaderTests.DecodeFields),
    ("render lights reject malformed data", RenderLightReaderTests.Validation),
    ("render lights exclude retained tails using paired GPU count", RenderLightReaderTests.ValidPrefix),
    ("render bridge protocol and freshness", RenderLightReaderTests.ProtocolAndFreshness),
    ("source visibility metadata preserves raw records and fails unknown", RenderLightReaderTests.VisibilityMetadata),
    ("source visibility mapping and smoothed capture preservation", RenderLightReaderTests.VisibilityMappingAndSmoothing),
    ("player-to-all-known-source visibility exchange", SourceVisibilityClientTests.ExchangeAndAttach),
    ("raw physics visibility, rapid camera orbit, provenance, freshness and raw preservation", PhysicsVisibilityTests.Exchange),
    ("render publication races preserve only the fresh complete capture", RenderLightReaderTests.PublicationRaces),
    ("ManyLights input follows ProcessManyLightsCS group/skip/special/colour rules and pairs output", UpstreamLightTests.DecodeRules),
    ("v4 bridge pairs input with its own sample and isolates input failures", UpstreamLightTests.BridgeProtocol),
    ("input lights including behind-camera sources drive physics targets", UpstreamLightTests.PhysicsTargets),
    ("configured input stream owns smoothing with honest coverage labels", UpstreamLightTests.Smoothing),
    ("smoothed local lights group/sum without mutating raw data", SmoothedLightTests.Grouping),
    ("smoothed local lights rate-independent EMA and spatial tracking", SmoothedLightTests.Smoothing),
    ("smoothed local lights fail closed and remove missing groups", SmoothedLightTests.Freshness),
    ("smoothed local lights validate bounds and dense pools", SmoothedLightTests.ValidationAndBounds),
    ("smoothed local lights transport isolation and health invalidation", SmoothedLightTests.TransportIsolation),
    ("clear-only visible transport hides unknown/blocked while raw remains complete", SmoothedLightTests.VisibleTransport),
    ("engine lights decode verified fields", EngineLightReaderTests.DecodeFields),
    ("engine lights reject and diagnose fields", EngineLightReaderTests.Validation),
    ("engine lights retry a changed walk", EngineLightReaderTests.WalkRetry),
    ("engine lights fail closed after walk races", EngineLightReaderTests.WalkUnavailable),
    ("engine lights direct scene-global walk", EngineLightReaderTests.DirectSceneWalk),
    ("engine lights JSON omits unavailable fields", EngineLightReaderTests.JsonContract),
    ("player position rejects uninitialized state", PlayerPositionRejectsUninitializedState),
    ("player orientation serializes validated basis", PlayerOrientationSerializesValidatedBasis),
    ("player orientation walks the actual guarded pointer chain", PlayerOrientationReaderTests.RealChain),
    ("player orientation rejects wrong identity and replaced physics", PlayerOrientationReaderTests.IdentityAndRaces),
    ("private exact-build candidate anchors stay gated", PlayerOrientationReaderTests.PrivateCandidateGate),
    ("build profiles reject malformed and mismatched native contracts", UpdateCheckTests.Profiles),
    ("build profile JSON rejects typos, duplicates and omitted offsets", UpdateCheckTests.JsonGuards),
    ("update check relocates direct-camera and native candidates without promotion", UpdateCheckTests.DirectRelocation),
    ("update check distinguishes exact, missing and ambiguous anchors", UpdateCheckTests.ExactAndAmbiguous),
    ("embedded build definition", EmbeddedBuildDefinition),
    ("automatic compatibility relocates code, globals and tables", BuildCompatibilityTests.Relocation),
    ("automatic compatibility relocates the direct camera layout", BuildCompatibilityTests.DirectLayoutRelocation),
    ("compatibility distinguishes exact executable identity", BuildCompatibilityTests.KnownHash),
    ("compatibility rejects missing and ambiguous code", BuildCompatibilityTests.MissingAndAmbiguousCode),
    ("compatibility validates RIP target sections", BuildCompatibilityTests.DataTargets),
    ("compatibility requires unique multi-slot vtable fingerprints", BuildCompatibilityTests.VtableGuards),
    ("compatibility rejects malformed PE images", BuildCompatibilityTests.MalformedImages),
    ("compatibility requires one opted-in guarded layout", BuildCompatibilityTests.TemplateGuards),
    ("native capture waits for and consumes the playable-world signal", NativeCaptureReadyGate),
    ("telemetry JSON contract", TelemetryJsonContract)
};
var failures = 0;
foreach (var test in tests)
{
    try { test.Run(); Console.WriteLine($"PASS {test.Name}"); }
    catch (Exception exception) { failures++; Console.Error.WriteLine($"FAIL {test.Name}: {exception.Message}"); }
}
return failures == 0 ? 0 : 1;

static void SignatureExact() => Assert(
    SignaturePattern.Parse("48 8B 01 FF").FindAll([0, 0x48, 0x8B, 0x01, 0xFF, 0]).SequenceEqual([1]),
    "Exact signature mismatch.");

static void NativeCaptureReadyGate()
{
    var processId = Environment.ProcessId;
    Assert(!NativeCaptureReadySignal.TrySet(processId), "A missing native capture gate was reported as signalled.");
    using var gate = new EventWaitHandle(false, EventResetMode.AutoReset, NativeCaptureReadySignal.Name(processId));
    Assert(NativeCaptureReadySignal.TrySet(processId), "The existing native capture gate was not signalled.");
    Assert(gate.WaitOne(0), "The native capture gate did not receive the signal.");
    Assert(!gate.WaitOne(0), "The one-shot native capture signal was not consumed.");
}

static void SignatureWildcard() => Assert(
    SignaturePattern.Parse("48 8B ?? FF").FindAll([0x48, 0x8B, 0xAC, 0xFF]).SequenceEqual([0]),
    "Wildcard signature mismatch.");

static void SignatureRejectsInvalidInput()
{
    AssertThrows<FormatException>(() => SignaturePattern.Parse("GG"), "Invalid hex was accepted.");
    AssertThrows<InvalidOperationException>(() => SignaturePattern.Parse("?? ??").FindAll([0, 0]),
        "All-wildcard signature was accepted.");
}

static void CameraConstants()
{
    var results = RenderCameraConstantsScanner.FindInBuffer(CameraBuffer(), 0x100000, (10f, 20f, 30f));
    Assert(results is [{ Address: 0x100028 }], "Camera constants were not uniquely recognized.");
    Assert(Math.Abs(results[0].FieldOfViewRadians - MathF.PI / 3) < 0.0001f, "Camera FOV mismatch.");
}

static void CameraRejectsInvalidBasis()
{
    var bytes = CameraBuffer();
    WriteVector(bytes, 0x40, 0, 1, 0);
    Assert(RenderCameraConstantsScanner.FindInBuffer(bytes, 0x100000, (10f, 20f, 30f)).Count == 0,
        "Non-orthogonal basis was accepted.");
}

static void CameraRejectsWrongSentinel()
{
    var bytes = CameraBuffer();
    WriteSingle(bytes, 0x24, 0);
    Assert(RenderCameraConstantsScanner.FindInBuffer(bytes, 0x100000, (10f, 20f, 30f)).Count == 0,
        "Invalid sentinel was accepted.");
}

static void CameraRejectsDistantPosition() => Assert(
    RenderCameraConstantsScanner.FindInBuffer(CameraBuffer(), 0x100000, (1000f, 2000f, 3000f)).Count == 0,
    "A camera far away from the player was accepted.");

static void CameraConsensus()
{
    RenderCameraConstantsCandidate Candidate(ulong address, float x) => new(address,
        new CameraVector3(x, 20, 30), new CameraVector3(0, 1, 0), new CameraVector3(1, 0, 0),
        new CameraVector3(0, 0, 1), 0.2f, float.MaxValue, MathF.PI / 3, 16f / 9f, 4);
    var consensus = RenderCameraConstantsScanner.SelectConsensus([
        Candidate(0x1000, 10.001f), Candidate(0x2000, 10.004f), Candidate(0x3000, 12f)
    ]);
    Assert(consensus is { CopyCount: 2, ValidCopyCount: 3, DistinctStateCount: 2 },
        "Camera consensus mismatch.");
}

static void TrackerRediscoversCamera()
{
    const ulong regionBase = 0x200000;
    var memory = new BufferMemory(regionBase, CameraBuffer());
    var stale = new RenderCameraConstantsCandidate(0x300028,
        new CameraVector3(10, 20, 30), new CameraVector3(0, 1, 0), new CameraVector3(1, 0, 0),
        new CameraVector3(0, 0, 1), 0.2f, float.MaxValue, MathF.PI / 3, 16f / 9f, 4);
    var tracker = new RenderCameraTracker(memory, [stale]);
    var frame = tracker.Capture((10f, 20f, 30f));
    Assert(frame.Rediscovered && tracker.RediscoveryCount == 1 && frame.Camera.Address == regionBase + 0x28,
        "Tracker did not rediscover the relocated camera record.");
}

static void PlayerPositionRejectsUninitializedState()
{
    const ulong baseAddress = 0x400000;
    var zeroMemory = new BufferMemory(baseAddress, new byte[12]);
    AssertThrows<InvalidDataException>(() => StaticPositionProbe.Read(zeroMemory,
        new StaticPositionAddresses(baseAddress, baseAddress + 8)),
        "An uninitialized zero player position was accepted.");

    var loadingBytes = new byte[12];
    WriteSingle(loadingBytes, 4, 1000);
    var loadingMemory = new BufferMemory(baseAddress, loadingBytes);
    AssertThrows<InvalidDataException>(() => StaticPositionProbe.Read(loadingMemory,
        new StaticPositionAddresses(baseAddress, baseAddress + 8)),
        "The observed 0,1000,0 loading sentinel was accepted.");

    WriteSingle(loadingBytes, 0, -0.000002394615f);
    WriteSingle(loadingBytes, 4, 1000.15f);
    WriteSingle(loadingBytes, 8, -0.000032010845f);
    var noisyLoadingMemory = new BufferMemory(baseAddress, loadingBytes);
    AssertThrows<InvalidDataException>(() => StaticPositionProbe.Read(noisyLoadingMemory,
        new StaticPositionAddresses(baseAddress, baseAddress + 8)),
        "The observed noisy loading sentinel was accepted.");

    WriteSingle(loadingBytes, 0, -0.104707725f);
    WriteSingle(loadingBytes, 4, 1000.15f);
    WriteSingle(loadingBytes, 8, -0.0047603063f);
    var driftingLoadingMemory = new BufferMemory(baseAddress, loadingBytes);
    AssertThrows<InvalidDataException>(() => StaticPositionProbe.Read(driftingLoadingMemory,
        new StaticPositionAddresses(baseAddress, baseAddress + 8)),
        "The observed drifting height-1000 loading sentinel was accepted.");
}

static void EmbeddedBuildDefinition()
{
    var definitions = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")));
    Assert(definitions.Any(definition => definition.SteamBuildId == "24994088" &&
                                         definition.Status == "locally-validated" &&
                                         definition.AllowAutomaticCompatibility &&
                                         definition.EngineCamera?.ContextVtableFingerprints.Count >= 2 &&
                                         definition.EngineCamera.CameraVtableFingerprints.Count >= 2),
        "The validated build definition is not embedded in the core assembly.");
    Assert(definitions.Any(definition => definition.SteamBuildId == "25050808" &&
                                         definition.ExecutableVersion == "1.0.0.2692" &&
                                         definition.Status == "locally-validated" &&
                                         !definition.AllowAutomaticCompatibility &&
                                         definition.EngineLights is { Layout: "light-source-array-v1", RootGlobalRva: 0x637A4D8 }),
        "The automatically recognized and live-validated update is not embedded.");
    Assert(definitions.Single(definition => definition.SteamBuildId == "24994088").EngineLights is null,
        "Unvalidated light offsets leaked into the automatic reference build.");
    Assert(definitions.Where(definition => definition.AllowAutomaticCompatibility)
            .GroupBy(definition => definition.EngineCamera!.Layout).All(layout => layout.Count() == 1),
        "Automatic compatibility must have at most one reference profile per camera layout.");
    Assert(definitions.Any(definition => definition.SteamBuildId == "25477059" &&
                                         definition.AllowAutomaticCompatibility &&
                                         definition.EngineCamera is { Layout: "renderer-camera-direct-v1" } camera &&
                                         camera.CameraVtableFingerprints.Count >= 2),
        "The current direct-camera profile is not an automatic compatibility template.");
    Assert(definitions.Any(definition => definition.SteamBuildId == "25116796" &&
                                         definition.ExecutableVersion == "1.0.0.2760" &&
                                         definition.Status == "locally-validated" &&
                                         !definition.AllowAutomaticCompatibility &&
                                         definition.EngineCamera is
                                         {
                                             Layout: "renderer-camera-direct-v1",
                                             FrameCounterOffset: 0x2C8
                                         } &&
                                         definition.EngineLights is
                                         {
                                             Layout: "light-source-array-scene-global-v1",
                                             SceneGlobalRva: 0x6B4EFB8,
                                             SceneVtableRva: 0x5C01A20
                                         } &&
                                         definition.PlayerRoot is
                                         {
                                             TransformLayout: "basis-v1",
                                             OwnerToPhysicsOffset: 0x2B8,
                                             BasisXOffset: 0x60,
                                             BasisYOffset: 0x70,
                                             BasisZOffset: 0x80,
                                             PositionOffset: 0x90
                                         }),
        "The previous direct-camera build definition is not embedded.");
    Assert(definitions.Any(definition => definition.SteamBuildId == "25246367" &&
                                         definition.ExecutableVersion == "1.0.0.2850" &&
                                         definition.ExecutableSha256 == "BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E" &&
                                         definition.EngineCamera is
                                         {
                                             Layout: "renderer-camera-direct-v1",
                                             CameraReferenceRva: 46413159,
                                             CameraGlobalRva: 112537704,
                                             CameraVtableRva: 96487272
                                         } &&
                                         definition.NativeCapture is
                                         {
                                             ContractId: "manylights-filter-25246367-v1",
                                             HookRva: 63670746
                                         }),
        "The current exact-build recovery definition is not embedded.");
}

static void TelemetryJsonContract()
{
    var vector = new CameraVector3(1, 2, 3);
    var snapshot = new TelemetrySnapshot("1.1", 42, DateTimeOffset.UnixEpoch,
        new GameSnapshot("24994088", "playing"),
        new CoordinateSystemSnapshot("game-unit", "right", "y"),
        ["player.position", "camera.transform", "camera.projection"],
        new PlayerSnapshot(vector),
        new CameraSnapshot(vector, vector, vector, vector, 0.2f, null, 50, 1.775f),
        new QualitySnapshot(48, 100, 7, false, 510));
    var json = JsonSerializer.Serialize(snapshot, new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    });
    using var document = JsonDocument.Parse(json);
    var root = document.RootElement;
    Assert(root.GetProperty("schemaVersion").GetString() == "1.1", "Schema version is missing.");
    Assert(root.GetProperty("capturedAt").ValueKind == JsonValueKind.String, "Capture time is missing.");
    Assert(root.GetProperty("camera").GetProperty("farPlane").ValueKind == JsonValueKind.Null,
        "Infinite far plane is not represented as null.");
    Assert(root.GetProperty("camera").GetProperty("verticalFovDegrees").GetSingle() == 50,
        "Vertical FOV field is missing.");
    Assert(root.GetProperty("quality").GetProperty("captureDurationMicroseconds").GetInt64() == 510,
        "Capture duration is missing.");
    Assert(root.GetProperty("player").GetProperty("orientation").ValueKind == JsonValueKind.Null,
        "Orientation should be explicitly null when this fixture has no orientation.");
    Assert(!root.TryGetProperty("lights", out _),
        "Disabled light telemetry changed the schema 1.1 payload.");

    var unavailable = snapshot with
    {
        Game = new GameSnapshot("24994088", "loading"),
        Player = null,
        Camera = null,
        Quality = null
    };
    using var unavailableDocument = JsonDocument.Parse(JsonSerializer.Serialize(unavailable,
        new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
    Assert(unavailableDocument.RootElement.GetProperty("camera").ValueKind == JsonValueKind.Null,
        "Unavailable camera state is not represented as null.");
}

static void PlayerOrientationSerializesValidatedBasis()
{
    var orientation = new PlayerOrientationSnapshot("player-physics-root",
        new CameraVector3(1, 0, 0), new CameraVector3(0, 1, 0), 90);
    using var document = JsonDocument.Parse(JsonSerializer.Serialize(orientation,
        new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
    Assert(document.RootElement.GetProperty("source").GetString() == "player-physics-root",
        "Orientation source is missing.");
    Assert(document.RootElement.GetProperty("headingDegrees").GetSingle() == 90,
        "Heading was not serialized.");
}

static byte[] CameraBuffer()
{
    var bytes = new byte[0x100];
    const int sentinel = 0x20;
    WriteSingle(bytes, sentinel, float.MaxValue);
    WriteSingle(bytes, sentinel + 4, float.MaxValue);
    var record = sentinel + 8;
    WriteVector(bytes, record, 10, 20, 30);
    WriteVector(bytes, record + 0x0C, 0, 1, 0);
    WriteVector(bytes, record + 0x18, 1, 0, 0);
    WriteVector(bytes, record + 0x24, 0, 0, 1);
    WriteSingle(bytes, record + 0x30, 0.2f);
    WriteSingle(bytes, record + 0x34, float.MaxValue);
    WriteSingle(bytes, record + 0x38, MathF.PI / 3);
    WriteSingle(bytes, record + 0x3C, 16f / 9f);
    WriteSingle(bytes, record + 0x40, float.MaxValue);
    return bytes;
}

static void WriteVector(byte[] bytes, int offset, float x, float y, float z)
{
    WriteSingle(bytes, offset, x);
    WriteSingle(bytes, offset + 4, y);
    WriteSingle(bytes, offset + 8, z);
}

static void WriteSingle(byte[] bytes, int offset, float value) =>
    BitConverter.GetBytes(value).CopyTo(bytes, offset);

static void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void AssertThrows<T>(Action action, string message) where T : Exception
{
    try { action(); }
    catch (T) { return; }
    throw new InvalidOperationException(message);
}

sealed class BufferMemory(ulong baseAddress, byte[] bytes) : IReadOnlyProcessMemory
{
    public byte[] Read(IntPtr address, int length)
    {
        var offset = checked((long)address - (long)baseAddress);
        if (offset < 0 || offset + length > bytes.Length) throw new InvalidDataException("Outside test memory.");
        return bytes.AsSpan(checked((int)offset), length).ToArray();
    }

    public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(new IntPtr((long)address), sizeof(float)));
    public IReadOnlyList<MemoryRegion> GetWritableRegions() => [new(baseAddress, (ulong)bytes.Length)];
}
