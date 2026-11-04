using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;
using CrimsonDesertTelemetry.Cli;

internal static class SkyAmbientTests
{
    private static void Check(bool ok, string message) { if (!ok) throw new InvalidOperationException(message); }
    private static void Near(double a, double b) => Check(Math.Abs(a - b) < 1e-6 * Math.Max(1, Math.Abs(b)), $"{a} != {b}");
    private static void Invalid(Action action)
    {
        try { action(); } catch (InvalidDataException) { return; }
        throw new InvalidOperationException("Malformed sky accepted.");
    }
    private static void U32(byte[] b, int o, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(o), v);
    private static void U64(byte[] b, int o, ulong v) => BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(o), v);
    private static void F(byte[] b, int o, float v) => BinaryPrimitives.WriteSingleLittleEndian(b.AsSpan(o), v);
    private static byte[] Payload()
    {
        var b = new byte[1024];
        for (var c = 0; c < 3; c++)
        {
            F(b, c * 32, 2 * .282095f * (c + 1));
            F(b, c * 32 + 4, -.488603f * (c + 1) * 1.00390625f);
            F(b, c * 32 + 8, -.031f); F(b, 96 + c * 4, .03f);
        }
        F(b, 108, 12345); // row6.w is NOT color or brightness.
        return b;
    }
    private static byte[] Bridge()
    {
        var b = new byte[SkyAmbientReader.TotalBytes];
        U32(b, 0, 0x53445443); U32(b, 4, SkyAmbientReader.BridgeVersion); U32(b, 8, 128); U32(b, 12, (uint)b.Length);
        U64(b, 16, 2); U32(b, 24, 42); U32(b, 28, 1); U64(b, 32, 123); U64(b, 40, 7);
        U64(b, 48, 1000); U64(b, 56, 1010); U32(b, 64, 42); U32(b, 68, 2816);
        U32(b, 72, 1024); U32(b, 76, SkyAmbientReader.SkyProducerRva); U32(b, 84, 7); U64(b, 88, 999);
        EngineCameraTests.SceneBytes().CopyTo(b, 128); Payload().CopyTo(b, 128 + 2816);
        return b;
    }
    public static void MathAndRaw()
    {
        var b = Payload(); var before = b.ToArray(); var v = SkyAmbientReader.DecodePayload(b);
        for (var c = 0; c < 3; c++)
        {
            Near(v.UpperHemisphereMeanWorking[c], c + 1);
            Near(v.UpwardIrradianceOverPiWorking[c], (c + 1) * 1.00390625);
            Near(v.CoefficientsWorking[c][2], -.031f); Near(v.CoefficientsWorking[c][8], .03f);
        }
        Check(b.SequenceEqual(before), "Decoder mutated raw data.");
        // Known exact float32 matrix, including its non-unit row sums.
        double[,] m = {{.61312f,.33951f,.04737f},{.07020f,.91636f,.01345f},{.02062f,.10958f,.86980f}};
        for (var r = 0; r < 3; r++)
            Near(Enumerable.Range(0, 3).Sum(c => m[r,c] * v.InverseMatrixMean[c]), v.UpperHemisphereMeanWorking[r]);
        Near(v.Rec709MeanLuminanceEstimate, v.InverseMatrixMean[0]*.2126+v.InverseMatrixMean[1]*.7152+v.InverseMatrixMean[2]*.0722);
        Array.Clear(b); F(b, 0, 2*.282095f);
        Check(SkyAmbientReader.DecodePayload(b).InverseMatrixMean[1] < 0, "Matrix reversal was clamped.");
        foreach (var bad in new[]{float.NaN,float.PositiveInfinity,float.NegativeInfinity})
        { var bytes = Payload(); F(bytes, 96, bad); Invalid(() => SkyAmbientReader.DecodePayload(bytes)); }
        var negative = Payload(); F(negative, 0, -1); Invalid(() => SkyAmbientReader.DecodePayload(negative));
        var wrongY = Payload(); F(wrongY, 4, 1); Invalid(() => SkyAmbientReader.DecodePayload(wrongY));
        Invalid(() => SkyAmbientReader.DecodePayload(new byte[1023]));
    }
    public static void Protocol()
    {
        SkyAmbientSnapshot Read(byte[] b, long now = 1100) => SkyAmbientReader.Decode(b, 42, 123, now);
        Check(Read(Bridge()).Status == "available", "Valid sky rejected.");
        var relocated = Bridge(); U32(relocated,76,0x3931317);
        Check(SkyAmbientReader.Decode(relocated,42,123,1100,0x3931317).Status == "available",
            "Build-specific sky producer was rejected.");
        Invalid(() => SkyAmbientReader.Decode(relocated,42,123,1100));
        foreach (var offset in new[]{0,4,8,12,24,32,40,68,72,76,84,88})
        { var b=Bridge(); U32(b,offset,0); Invalid(()=>Read(b)); }
        var odd=Bridge(); U64(odd,16,3); Invalid(()=>Read(odd));
        var mismatch=Bridge(); U32(mismatch,64,41); Invalid(()=>Read(mismatch));
        // The second ambient path must never be accepted as the sky producer.
        var otherPath=Bridge(); U32(otherPath,76,0x384ED63); Invalid(()=>Read(otherPath));
        Invalid(()=>Read(Bridge(),1009));
        var times=Bridge(); U64(times,56,999); Invalid(()=>Read(times));
        Check(Read(Bridge(),2500).Status=="available", "Freshness boundary wrong.");
        Check(Read(Bridge(),2501).Sky is null, "Stale sky survived.");
        for(uint state=0;state<6;state++) if(state!=1)
        { var b=Bridge(); U32(b,28,state); Check(Read(b).Sky is null, "Unavailable state retained sky."); }
        var invalid=Bridge(); U32(invalid,28,6); Invalid(()=>Read(invalid));
    }
    public static void Visibility()
    {
        SkyAmbientSnapshot Read(byte[] b, long now = 1100) => SkyAmbientReader.Decode(b, 42, 123, now);
        static byte[] WithVisibility(double value, uint state, uint frame, ulong tick)
        {
            var b = Bridge();
            BinaryPrimitives.WriteDoubleLittleEndian(b.AsSpan(96), value);
            U32(b, 104, state); U32(b, 108, frame); U64(b, 112, tick);
            return b;
        }
        // No block at all: sky still available, estimate simply absent.
        var bare = Read(Bridge());
        Check(bare.Visibility is null, "Absent visibility invented a value.");
        Check(bare.LocalEnvironmentAmbientEstimateWorking is { Available: false, Stale: false, RgbWorking: null },
            "Absent visibility produced an estimate.");

        var valid = Read(WithVisibility(0.25, 1, 77, 1000));
        Check(valid.Visibility is { ValueWorking: 0.25, FrameNumber: 77 }, "Valid visibility lost.");
        Check(valid.Visibility!.AgeMilliseconds == 100, "Visibility age uses the wrong base.");
        // Its own frame, never the sky sample's.
        Check(valid.FrameNumber == 42 && valid.Visibility.FrameNumber == 77, "Frames were merged.");
        var estimate = valid.LocalEnvironmentAmbientEstimateWorking!;
        Check(estimate.Available && !estimate.Stale, "Valid visibility did not produce an estimate.");
        for (var c = 0; c < 3; c++) Near(estimate.RgbWorking![c], (c + 1) * 0.25);
        // Raw inputs survive: the scaling question stays answerable from the payload.
        Near(valid.Sky!.UpperHemisphereMeanWorking[0], 1);

        // The fallback branch is one BY DEFINITION. Accepting it would restore full
        // ambient exactly where the local measurement is missing, which is indoors.
        var fallback = Read(WithVisibility(1.0, 2, 77, 1000));
        Check(fallback.Visibility is null, "Fallback was published as a measurement.");
        Check(fallback.LocalEnvironmentAmbientEstimateWorking is { Available: false },
            "Fallback produced an estimate.");
        Check(Read(WithVisibility(1.0, 0, 77, 1000)).Visibility is null, "Unavailable state retained.");

        // Stale visibility must say so rather than pass off a fresh-looking ambient.
        // The two ages are independent, so the sky is held fresh while only the
        // visibility expires; sharing one clock would hide exactly this case.
        var staleBridge = WithVisibility(0.25, 1, 77, 3000);
        U64(staleBridge, 48, 5000); U64(staleBridge, 56, 5010);
        var stale = Read(staleBridge, 5100);
        Check(stale.Status == "available" && stale.AgeMilliseconds == 100, "Sky was not held fresh.");
        Check(stale.Visibility is not null, "Stale visibility was hidden instead of flagged.");
        Check(stale.Visibility!.AgeMilliseconds > SkyAmbientReader.MaximumAgeMilliseconds, "Wrong age.");
        Check(stale.LocalEnvironmentAmbientEstimateWorking is { Available: false, Stale: true, RgbWorking: null },
            "Stale visibility was not flagged.");

        // Out-of-contract values are refused rather than propagated.
        foreach (var bad in new[] { -0.01, 1.01, double.NaN })
            Check(Read(WithVisibility(bad, 1, 77, 1000)).Visibility is null, "Out-of-range visibility accepted.");
        Check(Read(WithVisibility(0.25, 1, 77, 0)).Visibility is null, "Zero tick accepted.");
        Check(Read(WithVisibility(0.25, 1, 77, 2000)).Visibility is null, "Future tick accepted.");

        // Version 1 has no block, so the whole bridge must be refused, not misread.
        var old = WithVisibility(0.25, 1, 77, 1000); U32(old, 4, 1); Invalid(() => Read(old));
    }
    public static void ResumeBoundary()
    {
        var bytes = Bridge();
        Check(SkyAmbientReader.Decode(bytes,42,123,1100,minimumCaptureTickMs:1000).Reason ==
            "awaiting-post-resume-capture", "An old sky capture was relabelled on resume.");
        BinaryPrimitives.WriteDoubleLittleEndian(bytes.AsSpan(96), .25);
        U32(bytes,104,1); U32(bytes,108,77); U64(bytes,112,999);
        var fresh = SkyAmbientReader.Decode(bytes,42,123,1100,minimumCaptureTickMs:999);
        Check(fresh.Status == "available" && fresh.Visibility is null,
            "A new sky frame borrowed pre-resume visibility.");
        U64(bytes,112,1000);
        Check(SkyAmbientReader.Decode(bytes,42,123,1100,minimumCaptureTickMs:999).Visibility is not null,
            "A genuinely newer visibility measurement was rejected.");
    }
    public static void Mapping()
    {
        using var m=MemoryMappedFile.CreateNew($"Local\\CrimsonDesertTelemetry.Sky.{Environment.ProcessId}",SkyAmbientReader.TotalBytes);
        using var w=m.CreateViewAccessor(); var b=Bridge(); U32(b,24,(uint)Environment.ProcessId);
        var now=(ulong)Environment.TickCount64; U64(b,48,now); U64(b,56,now);
        w.WriteArray(0,b,0,b.Length);
        using var reader=new SkyAmbientReader(Environment.ProcessId,123);
        Check(reader.Capture().Status=="available", "Mapping unavailable.");
        U64(b,16,4); U32(b,28,3); w.WriteArray(0,b,0,b.Length);
        Check(reader.Capture().Reason=="native-fault", "Cached result survived native fault.");
        U64(b,16,6); U32(b,28,1); U32(b,0,0); w.WriteArray(0,b,0,b.Length);
        Check(reader.Capture().Reason=="bridge-invalid", "Malformed mapping did not fail closed.");
        var relocated=Bridge(); U32(relocated,24,(uint)Environment.ProcessId);
        U32(relocated,76,0x3931317); U64(relocated,48,now); U64(relocated,56,now);
        w.WriteArray(0,relocated,0,relocated.Length);
        using var updated=new SkyAmbientReader(Environment.ProcessId,123,0x3931317);
        Check(updated.Capture().Status=="available" && updated.Capture().Status=="available",
            "A cached sample lost its build-specific sky producer identity.");
    }
    public static void Transport()
    {
        var options=new JsonSerializerOptions(JsonSerializerDefaults.Web);
        var state=new TelemetryServerState(options,60,"1.4");
        using (var waiting=JsonDocument.Parse(state.LatestSkyBytes))
            Check(waiting.RootElement.GetProperty("status").GetString()=="unavailable" &&
                waiting.RootElement.GetProperty("localEnvironmentAmbientEstimateWorking").ValueKind==JsonValueKind.Null,
                "Unavailable sky must serialize a null local estimate, not a fabricated estimate object.");
        using var raw=state.Subscribe(); using var smooth=state.Subscribe(true); using var sky=state.SubscribeSky();
        state.SetHealth("playing",true,true,"25116796",0,null,null);
        var sample=SkyAmbientReader.Decode(Bridge(),42,123,1100) with {CapturedAt=DateTimeOffset.UtcNow,AgeMilliseconds=0};
        state.PublishSky(sample);
        Check(sky.Reader.TryRead(out var bytes) && !raw.Reader.TryRead(out _) && !smooth.Reader.TryRead(out _),"Sky leaked into another feed.");
        using var json=JsonDocument.Parse(bytes!);
        Check(json.RootElement.GetProperty("scope").GetString()=="global-upper-hemisphere-sky", "Scope omitted.");
        Check(!json.RootElement.GetProperty("exposureNormalized").GetBoolean(), "False normalization claim.");
        Check(json.RootElement.GetProperty("localEnvironmentAmbientEstimateWorking").ValueKind==JsonValueKind.Object &&
            !json.RootElement.GetProperty("localEnvironmentAmbientEstimateWorking").GetProperty("available").GetBoolean(),
            "Available sky without local visibility must preserve the estimate status object.");
        state.SetHealth("loading",true,true,"25116796",0,null,null);
        Check(state.LatestSky.Sky is null && sky.Reader.TryRead(out _), "Loading retained sky.");
        using (var loading=JsonDocument.Parse(state.LatestSkyBytes))
            Check(loading.RootElement.GetProperty("localEnvironmentAmbientEstimateWorking").ValueKind==JsonValueKind.Null,
                "Loading retained a local estimate in the unavailable envelope.");
        state.PublishSky(sample); Check(state.LatestSky.Sky is null,"Loading accepted available data.");
        state.SetHealth("playing",true,true,"25116796",0,null,null);
        state.PublishSky(sample with {CapturedAt=DateTimeOffset.UtcNow.AddSeconds(-2)});
        Check(state.LatestSky.Reason=="source-stale", "HTTP cache never expired.");
    }
    public static int Replay(string path)
    {
        using var doc=JsonDocument.Parse(File.ReadAllText(path));
        var original=File.ReadAllBytes(doc.RootElement.GetProperty("source").GetString()!);
        var recordBytes=BitConverter.ToInt32(original,8); var index=0;
        foreach(var expected in doc.RootElement.GetProperty("samples").EnumerateArray())
        {
            var actual=SkyAmbientReader.DecodePayload(original.AsSpan(index*recordBytes+64+2816,1024));
            var values=expected.GetProperty("values");
            foreach(var (name,array) in new[]{("upperHemisphereMeanWorking",actual.UpperHemisphereMeanWorking),
                ("upwardIrradianceOverPiWorking",actual.UpwardIrradianceOverPiWorking),("inverseMatrixMean",actual.InverseMatrixMean),
                ("inverseMatrixUpwardIrradianceOverPi",actual.InverseMatrixUpwardIrradianceOverPi)})
                for(var c=0;c<3;c++) Near(array[c],values.GetProperty(name)[c].GetDouble());
            Near(actual.Rec709MeanLuminanceEstimate,values.GetProperty("rec709MeanLuminanceEstimate").GetDouble());
            index++;
        }
        Check(index>0 && index*recordBytes==original.Length,"Replay sample bounds disagree.");
        Console.WriteLine($"Sky C# decoder matches independent PowerShell results for {index} captured samples.");
        return 0;
    }
}
