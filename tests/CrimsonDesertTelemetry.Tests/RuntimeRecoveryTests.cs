using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text;
using System.Diagnostics;
using CrimsonDesertTelemetry.Core;

internal static class RuntimeRecoveryTests
{
    private static BuildDefinition Template() => BuildDefinition.LoadAll("not-a-profile-directory")
        .Single(d => d.SteamBuildId == "26448710");

    public static int Replay(string executable, string cacheDirectory)
    {
        var reference = Template();
        var messages = new List<RecoveryProgress>();
        var firstWatch = Stopwatch.StartNew();
        var readiness = RuntimeRecovery.ResolveReadiness(executable, reference, cacheDirectory, p =>
        { messages.Add(p); Console.WriteLine($"{p.Completed}/{p.Total} {p.Phase}: {p.Detail}"); });
        Check(readiness.Definition.NativeCapture is null && readiness.Definition.EngineLights is null,
            "Readiness phase exposed native acquisition before the live gate.");
        var result = RuntimeRecovery.ResolveNative(readiness, reference, cacheDirectory, p =>
        { messages.Add(p); Console.WriteLine($"{p.Completed}/{p.Total} {p.Phase}: {p.Detail}"); });
        firstWatch.Stop();
        Check(result.Anchors.Count == 30 && !result.Definition.AllowAutomaticCompatibility,
            "Recovery lost guards or promoted the reference allowlist.");
        Check(result.Definition.Status == reference.Status && reference.ExecutableSha256 == Template().ExecutableSha256,
            "Recovery promoted or mutated reference profile.");
        var cachedWatch = Stopwatch.StartNew();
        var next = RuntimeRecovery.ResolveReadiness(executable, reference, cacheDirectory);
        var second = RuntimeRecovery.ResolveNative(next, reference, cacheDirectory);
        cachedWatch.Stop();
        Check(next.CacheHit && second.CacheHit, "Verified per-EXE cache was not reused.");
        Check(JsonSerializer.Serialize(result.Definition) == JsonSerializer.Serialize(second.Definition),
            "Cache changed the recovered contract.");
        var cache = Path.Combine(cacheDirectory, result.ExecutableSha256 + "-" + result.TemplateFingerprint + ".json");
        var original = File.ReadAllText(cache);
        try
        {
            // A syntactically valid cache cannot redirect a hook or change the
            // role simply because its executable hash still agrees.
            var node = JsonNode.Parse(original)!;
            var entry = node["Addresses"]!.AsArray().Single(a => a!["Name"]!.GetValue<string>() == "native-PhysicsRay")!;
            entry["Rva"] = result.Definition.NativeCapture!.Support!.Anchors.Single(a => a.Name == "PhysicsShape").Rva;
            File.WriteAllText(cache, node.ToJsonString());
            var stale = RuntimeRecovery.ResolveReadiness(executable, reference, cacheDirectory);
            var repaired = RuntimeRecovery.ResolveNative(stale, reference, cacheDirectory);
            Check(!repaired.CacheHit && JsonSerializer.Serialize(repaired.Definition) == JsonSerializer.Serialize(result.Definition),
                "Corrupted cached physics role was trusted or did not trigger one fresh resolution.");
            File.WriteAllText(cache, "{ invalid json");
            Check(!RuntimeRecovery.ResolveReadiness(executable, reference, cacheDirectory).CacheHit,
                "Malformed cache was trusted.");
        }
        finally { File.WriteAllText(cache, original); }
        Console.WriteLine(JsonSerializer.Serialize(new { result.ExecutableSha256, result.TemplateFingerprint,
            Timing = new { FirstMilliseconds = firstWatch.Elapsed.TotalMilliseconds, FirstUsedCache = result.CacheHit,
                CachedMilliseconds = cachedWatch.Elapsed.TotalMilliseconds },
            result.Anchors, Native = result.Definition.NativeCapture, Camera = result.Definition.EngineCamera }));
        Console.WriteLine("PASS full runtime recovery, cached guard replay and corrupted-cache fallback; file-only, no live activation.");
        return 0;
    }

    public static void ContractGuards()
    {
        var noSupport = Template(); noSupport.NativeCapture!.Support = null;
        Reject(() => RuntimeRecovery.ResolveReadiness("must-not-be-read.exe", noSupport), "Incomplete contract accepted.");
        using var canceled = new CancellationTokenSource(); canceled.Cancel();
        try { RuntimeRecovery.ResolveReadiness("must-not-be-read.exe", Template(), cancellation: canceled.Token); }
        catch (OperationCanceledException) { return; }
        throw new InvalidOperationException("Canceled recovery read executable bytes.");
    }

    public static void SyntheticRecovery()
    {
        using (var fixture = new Fixture())
        {
            var readiness = RuntimeRecovery.ResolveReadiness(fixture.Path, fixture.Reference, fixture.Cache);
            Check(readiness.StaticPositionXyRva == 0x40000 + 0x100 && readiness.PlayerWorldRva == 0x40000 + 0x200,
                "Player globals did not relocate.");
            var result = RuntimeRecovery.ResolveNative(readiness, fixture.Reference, fixture.Cache);
            Check(result.Definition.NativeCapture!.HookRva == 0x10000 + 0x1603 &&
                result.Definition.NativeCapture.Support!.Anchors.Single(a => a.Name == "PhysicsRay").Rva == 0x2100 &&
                result.Definition.NativeCapture.Support.Values["PhysicsCollectorVtableRva"] == 0x30200,
                "Complete acquisition did not relocate.");
            Check(result.InstructionGuards.Any(g => g.Name == "native-PhysicsRayCall" && g.Bytes.EndsWith("FF 90 E8 01 00 00", StringComparison.Ordinal)),
                "Natural-call virtual slot guard was lost.");
            var cached = RuntimeRecovery.ResolveReadiness(fixture.Path, fixture.Reference, fixture.Cache);
            Check(cached.CacheHit && RuntimeRecovery.ResolveNative(cached, fixture.Reference, fixture.Cache).CacheHit,
                "Complete synthetic cache did not replay.");
            // A forced in-game diagnostic must actually search, even when a valid
            // result already exists. It can still refresh that cache afterwards.
            var cold = RuntimeRecovery.ResolveReadiness(fixture.Path, fixture.Reference, cacheDirectory: null);
            var searched = RuntimeRecovery.ResolveNative(cold, fixture.Reference, fixture.Cache);
            Check(!cold.CacheHit && !searched.CacheHit && searched.Anchors.Count == 30 &&
                JsonSerializer.Serialize(searched.Definition) == JsonSerializer.Serialize(result.Definition),
                "Forced cold search reused cached addresses or changed the guarded contract.");
            var path = Directory.GetFiles(fixture.Cache, "*.json").Single();
            File.WriteAllText(path, File.ReadAllText(path).Insert(1, "\"Version\":1,"));
            Check(!RuntimeRecovery.ResolveReadiness(fixture.Path, fixture.Reference, fixture.Cache).CacheHit,
                "Duplicate cache properties accepted.");
        }
        using (var missing = new Fixture())
        {
            missing.Put(0x10000, [0]); missing.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(missing.Path, missing.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, missing.Reference), "Missing native entry accepted.");
        }
        using (var ambiguous = new Fixture())
        {
            ambiguous.Put(0x8000, ambiguous.Reference.NativeCapture!.ContextSignatures[0].Bytes); ambiguous.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(ambiguous.Path, ambiguous.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, ambiguous.Reference), "Ambiguous ManyLights entry accepted.");
        }
        using (var changedLayout = new Fixture())
        {
            var context = changedLayout.Reference.NativeCapture!.ContextSignatures[3];
            var actual = 0x10000 + context.Rva - changedLayout.Reference.NativeCapture.ContextSignatures[0].Rva;
            changedLayout.Put(actual + 3, [0xF8]); changedLayout.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(changedLayout.Path, changedLayout.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, changedLayout.Reference), "Changed bank field offset accepted.");
        }
        using (var unnamedRay = new Fixture())
        {
            unnamedRay.Put(0x2180, [0]); unnamedRay.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(unnamedRay.Path, unnamedRay.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, unnamedRay.Reference), "Unnamed ray prologue accepted.");
        }
        using (var changedCaller = new Fixture())
        {
            changedCaller.Put(0x6000 - 843 - 3, [0xF0]); changedCaller.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(changedCaller.Path, changedCaller.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, changedCaller.Reference), "Changed physics virtual slot accepted.");
        }
        using (var wrongCollector = new Fixture())
        {
            wrongCollector.Put(0x7117, [0]); wrongCollector.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(wrongCollector.Path, wrongCollector.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, wrongCollector.Reference), "Collector storing a different table accepted.");
        }
        using (var changedTarget = new Fixture())
        {
            changedTarget.Call(0x10000 + 0x1603 + 8, 0x2500); changedTarget.Save();
            var readiness = RuntimeRecovery.ResolveReadiness(changedTarget.Path, changedTarget.Reference);
            Reject(() => RuntimeRecovery.ResolveNative(readiness, changedTarget.Reference), "Unrelated post-dispatch callee accepted.");
        }
    }

    // Small PE with real production guard bytes, unrelated section names and an
    // AMD64 exception directory. No copied game executable/assets are required.
    private sealed class Fixture : IDisposable
    {
        public string DirectoryPath { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "cdt-recovery-" + Guid.NewGuid().ToString("N"));
        public string Path => System.IO.Path.Combine(DirectoryPath, "fixture.exe");
        public string Cache => System.IO.Path.Combine(DirectoryPath, "cache");
        public BuildDefinition Reference { get; } = Template();
        private readonly byte[] bytes = new byte[0x25400];
        private const ulong ImageBase = 0x140000000;
        private readonly List<(uint Start, uint End)> functions = [];
        private uint nextString = 0x31000;

        public Fixture()
        {
            Directory.CreateDirectory(DirectoryPath);
            U16(0, 0x5A4D); U32(0x3C, 0x80); U32(0x80, 0x4550); U16(0x84, 0x8664);
            U16(0x86, 3); U16(0x94, 0xF0); U16(0x98, 0x20B);
            Q(0x98 + 24, ImageBase); U32(0x98 + 56, 0x41000); U32(0x98 + 108, 16);
            Section(0, ".renamed", 0x1000, 0x20000, 0x400, 0x60000020);
            Section(1, ".tables", 0x30000, 0x4000, 0x20400, 0x40000040);
            Section(2, ".data", 0x40000, 0x1000, 0x24400, 0xC0000040);
            DataReference(0x1100, Reference.Patterns[0], 0x40100);
            DataReference(0x1100, Reference.Patterns[1], 0x40108, overwrite: false);
            DataReference(0x1200, Reference.PlayerRoot!.WorldSystemPattern, 0x40200);
            DataReference(0x1300, new PatternDefinition { Pattern = Reference.EngineCamera!.CameraReferencePattern, RipOffset = 3, RipEnd = 7 }, 0x40300);
            foreach (var name in Reference.PlayerRoot.ExpectedTypeNames) String(name);
            var slots = Reference.EngineCamera.CameraVtableFingerprints;
            for (var index = 0; index < slots.Count; index++)
            {
                var method = 0x1800u + (uint)index * 0x100;
                Put(method, slots[index].Pattern);
                PutQ(0x30000 + (ulong)slots[index].Slot * 8, ImageBase + method);
            }
            PutQ(0x30000, ImageBase + 0x1800);

            var native = Reference.NativeCapture!;
            for (var index = 0; index < native.ContextSignatures.Count; index++)
            {
                var rva = index == 4 ? 0x140FAUL : 0x10000 + native.ContextSignatures[index].Rva - native.ContextSignatures[0].Rva;
                Put(rva, native.ContextSignatures[index].Bytes);
            }
            Put(0x10000 + native.HookRva - native.ContextSignatures[0].Rva, native.HookSignature);
            Function(0x10000, 0x11800, "ProcessManyLights", 0x10800);
            Function(0x14000, 0x14300);
            var anchors = native.Support!.Anchors.ToDictionary(a => a.Name);
            void Anchor(string name, ulong rva) => Put(rva, anchors[name].Bytes);
            const ulong sourceA = 0x16F7F;
            var sourceB = sourceA + anchors["AmbientSourceB"].Rva - anchors["AmbientSourceA"].Rva;
            Anchor("AmbientSourceA", sourceA);
            Anchor("AmbientHookA", sourceA + anchors["AmbientHookA"].Rva - anchors["AmbientSourceA"].Rva);
            Anchor("AmbientSourceB", sourceB);
            Anchor("AmbientHookB", sourceB + anchors["AmbientHookB"].Rva - anchors["AmbientSourceB"].Rva);
            Function(0x16000, 0x18000, "PrecomputeAmbient", 0x16100);
            Function(0x19000, 0x1C000, "GenerateAmbientFromEnvironmentAtmosphericScattering", 0x19100);
            Anchor("SpatialDispatch", 0x14000);
            Anchor("PhysicsRay", 0x2100); Function(0x2100, 0x2300, "TtWorldCastRay", 0x2180);
            Anchor("PhysicsShape", 0x2300); Function(0x2300, 0x2500, "TtWorldCastShape", 0x2380);
            Anchor("PhysicsCollectorMethod", 0x2500); Function(0x2500, 0x2700);
            Anchor("ExposureDispatch", 0x3200); Function(0x3000, 0x3400, "AdaptExposure", 0x3080);
            Function(0x7000, 0x7100); Put(0x7000, [0xC3]);
            Call(0x10000 + native.HookRva - native.ContextSignatures[0].Rva + 8, 0x7000);
            Call(sourceA + anchors["AmbientHookA"].Rva - anchors["AmbientSourceA"].Rva + 3, 0x7000);
            Call(sourceB + anchors["AmbientHookB"].Rva - anchors["AmbientSourceB"].Rva + 3, 0x7000);
            Call(0x3200 + 21, 0x7000);
            Anchor("PhysicsShapeCaller", 0x6000); Function(0x5F00, 0x6200);
            Anchor("PhysicsRayCaller", 0x6000 - 843); Function(0x5B00, 0x5E00);
            Put(0x6000 - 34, "48 8B 01 4C 8B 90 F0 01 00 00 48 8B 85 B0 00 00 00 48 89 44 24 20 4D 8B CF 4D 8B C4 49 8B D5 41 FF D2");
            Put(0x6000 - 843 - 15, "48 8B 01 4D 8B C7 49 8B D4 FF 90 E8 01 00 00");
            for (var index = 0; index < 9; index++) PutQ(0x30200 + (ulong)index * 8, ImageBase + 0x1800UL);
            PutQ(0x30208, ImageBase + 0x7100);
            PutQ(0x30228, ImageBase + 0x2500);
            PutQ(0x30248, ImageBase + 0x7000); // Neighboring table.
            Put(0x7114, "48 8D 05 ?? ?? ?? ?? 48 89 01 E8 ?? ?? ?? ??");
            U32(At(0x7117), 0x30200 - 0x711B);
            var ordered = functions.OrderBy(f => f.Start).ToArray();
            for (var index = 0; index < ordered.Length; index++)
            {
                var address = At(0x32000 + (ulong)index * 12);
                U32(address, ordered[index].Start); U32(address + 4, ordered[index].End); U32(address + 8, 0x33000);
            }
            U32(0x98 + 112 + 3 * 8, 0x32000); U32(0x98 + 112 + 3 * 8 + 4, (uint)(ordered.Length * 12));
            Save();
        }
        private void Function(uint start, uint end, string? name = null, uint reference = 0)
        {
            functions.Add((start, end));
            if (name is null) return;
            var target = String(name);
            Put(reference, [0x48, 0x8D, 0x15, 0, 0, 0, 0]);
            U32(At(reference + 3), unchecked((uint)((int)target - (int)(reference + 7))));
        }
        private uint String(string value)
        {
            var at = nextString;
            var text = Encoding.ASCII.GetBytes(value + '\0'); Put(at, text);
            nextString += (uint)text.Length + 8; return at;
        }
        private void DataReference(uint rva, PatternDefinition definition, uint target, bool overwrite = true)
        {
            if (overwrite) Put(rva, definition.Pattern);
            U32(At(rva + (ulong)definition.RipOffset!.Value), unchecked(target - (rva + (uint)definition.RipEnd!.Value)));
        }
        public void Put(ulong rva, string pattern) => Put(rva, pattern.Split(' ').Select(t => t is "?" or "??" ? (byte)0x11 : Convert.ToByte(t, 16)).ToArray());
        public void Put(ulong rva, byte[] value) => value.CopyTo(bytes, At(rva));
        public void Call(ulong rva, ulong target)
        {
            Put(rva, [0xE8]);
            U32(At(rva + 1), unchecked((uint)((long)target - (long)(rva + 5))));
        }
        private void PutQ(ulong rva, ulong value) => Q(At(rva), value);
        private static int At(ulong rva) => checked((int)(rva < 0x21000 ? rva - 0x1000 + 0x400 : rva < 0x34000 ? rva - 0x30000 + 0x20400 : rva - 0x40000 + 0x24400));
        private void Section(int index, string name, uint rva, uint size, uint raw, uint flags)
        {
            var at = 0x188 + index * 40;
            Encoding.ASCII.GetBytes(name).CopyTo(bytes, at);
            U32(at + 8, size); U32(at + 12, rva); U32(at + 16, size); U32(at + 20, raw); U32(at + 36, flags);
        }
        private void U16(int at, ushort value) => BitConverter.GetBytes(value).CopyTo(bytes, at);
        private void U32(int at, uint value) => BitConverter.GetBytes(value).CopyTo(bytes, at);
        private void Q(int at, ulong value) => BitConverter.GetBytes(value).CopyTo(bytes, at);
        public void Save() => File.WriteAllBytes(Path, bytes);
        public void Dispose() => Directory.Delete(DirectoryPath, recursive: true);
    }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
