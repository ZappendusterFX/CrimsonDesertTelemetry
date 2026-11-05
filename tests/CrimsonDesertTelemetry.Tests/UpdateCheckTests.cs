using System.Text;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;

internal static class UpdateCheckTests
{
    public static BuildDefinition Current() => BuildDefinition.LoadAll("not-a-profile-directory")
        .Single(d => d.SteamBuildId == "25116796");

    public static void Profiles()
    {
        BuildProfileValidation.ValidateAll(BuildDefinition.LoadAll("not-a-profile-directory"));
        Action<BuildDefinition>[] corruptions =
        [
            d => d.SchemaVersion = 2, d => d.ExecutableSha256 = "1234", d => d.SteamAppId = "1",
            d => d.EngineCamera!.Layout = "guessed-layout", d => d.Patterns[0].RipOffset = int.MaxValue,
            d => d.Patterns[0].RipEnd = 10000, d => d.Patterns[0].RipOffset = 0,
            d => d.Patterns[0].Pattern = "?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??",
            d => d.PlayerRoot!.OwnerToPhysicsOffset = -8, d => d.PlayerRoot!.ExpectedTypeNames.Clear(),
            d => d.NativeCapture!.RequiresExactExecutable = false, d => d.NativeCapture!.ContractId = "unknown-v2",
            d => d.NativeCapture!.Gpu.ValidCounterByteOffset = 0, d => d.NativeCapture!.Registers.Counter = "r14",
            d => d.NativeCapture!.ContextSignatures.Clear(), d => d.EngineLights!.SceneGlobalRva += 8,
            d => d.AllowAutomaticCompatibility = true, d => d.EngineCamera!.CameraVtableFingerprints = null!,
            d => d.NativeCapture!.Scene.RootPointerOffset = 0x430, d => d.PlayerRoot!.BasisXOffset = d.PlayerRoot.PositionOffset,
            d => d.EngineCamera!.SourcePointerOffset = 0x430, d => d.NativeCapture!.Scene = null!,
            d => d.NativeCapture!.Wrapper.OwnerBankIndexOffset = 0x8E0,
            d => d.NativeCapture!.Wrapper.CommandHolderOffset = 0x8C0
        ];
        foreach (var corrupt in corruptions)
        {
            var profile = Current(); corrupt(profile);
            Reject(() => BuildProfileValidation.Validate(profile), "Malformed profile accepted.");
            Reject(() => BuildCompatibility.Resolve("file-must-not-be-read.exe", [profile]), "Profile was used before validation.");
        }
        var updated = BuildDefinition.LoadAll("not-a-profile-directory").Single(d => d.SteamBuildId == "26361232");
        updated.NativeCapture!.Wrapper.CommandHolderOffset = 0x800;
        Reject(() => BuildProfileValidation.Validate(updated), "New build retained the obsolete command-list holder.");
        var current = BuildDefinition.LoadAll("not-a-profile-directory").Single(d => d.SteamBuildId == "26448710");
        Check(current.Status == "locally-validated" && current.NativeCapture!.Status == "locally-validated" &&
            !current.AllowAutomaticCompatibility && current.NativeCapture.Support?.Anchors.Count == 11,
            "Live-validated release reference lost its paired status or guarded full-product coverage.");
        Action<BuildDefinition>[] supportCorruptions = [
            d => d.NativeCapture!.Support = null,
            d => d.NativeCapture!.Support!.Values.Remove("PhysicsQueryProfile"),
            d => d.NativeCapture!.Support!.Values["UnreviewedOffset"] = 16,
            d => d.NativeCapture!.Support!.Values["PhysicsQueryProfile"] = 0x40000016,
            d => d.NativeCapture!.Support!.Values["PhysicsCollectorVtableRva"] += 1,
            d => d.NativeCapture!.Support!.Anchors.RemoveAt(0),
            d => d.NativeCapture!.Support!.Anchors[0].RelocationPattern = new string('?', 30),
            d => d.NativeCapture!.Support!.Values["ExposureReturnRva"] += 1,
            d => { d.EngineCamera!.SourcePointerOffset = 0x428; d.NativeCapture!.Scene.RootPointerOffset = 0x428; }
        ];
        foreach (var corrupt in supportCorruptions)
        {
            var candidate = BuildDefinition.LoadAll("not-a-profile-directory").Single(d => d.SteamBuildId == current.SteamBuildId);
            corrupt(candidate);
            Reject(() => BuildProfileValidation.Validate(candidate), "Malformed or legacy current-build acquisition support accepted.");
        }
        var valid = Current();
        Reject(() => BuildProfileValidation.ValidateAll([valid, valid]), "Duplicate identity accepted.");
        var research = Current();
        research.Status = "research";
        research.NativeCapture!.Status = "research";
        BuildProfileValidation.Validate(research);
        research.NativeCapture.Status = "locally-validated";
        Reject(() => BuildProfileValidation.Validate(research), "Mixed research/validated native status accepted.");
    }

    public static void JsonGuards()
    {
        var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "cdt-profiles-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var path = System.IO.Path.Combine(directory, "build-fixture.json");
        try
        {
            var json = JsonSerializer.Serialize(Current());
            File.WriteAllText(path, json.Insert(1, "\"typoNativeGate\":true,"));
            Reject(() => BuildDefinition.LoadAll(directory), "Unknown profile property accepted.");
            File.WriteAllText(path, json.Insert(1, "\"schemaVersion\":1,"));
            Reject(() => BuildDefinition.LoadAll(directory), "Duplicate case-insensitive schema property accepted.");
            var node = System.Text.Json.Nodes.JsonNode.Parse(json)!;
            node["PlayerRoot"]!.AsObject().Remove("OwnerToPhysicsOffset");
            File.WriteAllText(path, node.ToJsonString());
            Reject(() => BuildDefinition.LoadAll(directory), "Missing pointer-chain offset silently inherited a default.");
        }
        finally { File.Delete(path); Directory.Delete(directory); }
    }

    public static void DirectRelocation()
    {
        foreach (var shift in new[] { 0u, 0x10000u })
        {
            using var fixture = DirectFixture(shift);
            var originalHash = fixture.Definition.ExecutableSha256;
            var result = UpdateCheck.Run(fixture.Path, [fixture.Definition]);
            Check(result.Status == "candidate-layout-unverified" && !result.ExactExecutable && !result.ActivatesAnything,
                "Unknown EXE was promoted or diagnostic failed: " + Summary(result));
            var scene = result.Anchors.Single(a => a.Name == "camera-scene-global");
            Check(scene.FoundRva == fixture.Code + 0x180 && scene.TargetRva == fixture.Data + 0x108,
                "Direct scene did not relocate.");
            Check(result.Anchors.Single(a => a.Name == "native-filter-callsite").FoundRva == fixture.Code + 0xA00,
                "Native candidate did not relocate.");
            Check(result.Anchors.Single(a => a.Name == "camera-scene-vtable").Status == "unverified",
                "Unknown camera table reused a fixed RVA without fingerprints.");
            Check(fixture.Definition.ExecutableSha256 == originalHash && fixture.Definition.NativeCapture is not null,
                "Diagnostic mutated the trusted profile.");
        }
        using var movedCall = DirectFixture();
        movedCall.Bytes[0xE09] ^= 0x5A;
        movedCall.Save();
        var movedResult = UpdateCheck.Run(movedCall.Path, [movedCall.Definition]);
        Check(movedResult.Anchors.Single(a => a.Name == "native-filter-callsite").FoundRva == movedCall.Code + 0xA00,
            "Offline update check treated a relocated call displacement as instruction identity.");
    }

    public static void ExactAndAmbiguous()
    {
        using var fixture = DirectFixture();
        fixture.Definition.ExecutableSha256 = GameDiscovery.ComputeSha256(fixture.Path);
        var result = UpdateCheck.Run(fixture.Path, [fixture.Definition]);
        Check(result.Status == "exact-profile-anchors-checked" && result.ExactExecutable && !result.ActivatesAnything,
            "Exact guarded fixture rejected: " + Summary(result));
        fixture.Bytes.AsSpan(0xE00, 25).CopyTo(fixture.Bytes.AsSpan(0x1800));
        fixture.Save();
        result = UpdateCheck.Run(fixture.Path, [fixture.Definition]);
        Check(result.Status == "candidate-layout-unverified" &&
            result.Anchors.Single(a => a.Name == "native-filter-callsite").FoundRva == fixture.Code + 0xA00,
            "The unique dispatch context did not disambiguate a repeated hook instruction sequence.");
        using var ambiguousContext = DirectFixture();
        var firstContext = ambiguousContext.Definition.NativeCapture!.ContextSignatures[0];
        PutSignature(ambiguousContext, 0x1800, firstContext.Bytes);
        ambiguousContext.Save();
        result = UpdateCheck.Run(ambiguousContext.Path, [ambiguousContext.Definition]);
        Check(result.Status == "anchor-check-failed" && result.Anchors.Single(a =>
                a.Name == "native-context-" + firstContext.Purpose).Status == "ambiguous",
            "Ambiguous native dispatch context accepted.");
        using var missing = DirectFixture();
        missing.Bytes[0x580] ^= 1; missing.Save();
        result = UpdateCheck.Run(missing.Path, [missing.Definition]);
        Check(result.Anchors.Single(a => a.Name == "camera-scene-global").Status == "missing" && !result.ActivatesAnything,
            "Missing direct-camera anchor accepted.");
        using var mismatch = DirectFixture();
        mismatch.Definition.NativeCapture!.HookRva += 1;
        mismatch.Definition.ExecutableSha256 = GameDiscovery.ComputeSha256(mismatch.Path);
        result = UpdateCheck.Run(mismatch.Path, [mismatch.Definition]);
        Check(result.Anchors.Single(a => a.Name == "native-filter-callsite").Status == "missing",
            "Exact hash masked native gate RVA mismatch.");
    }

    private static BuildCompatibilityTests.Fixture DirectFixture(uint shift = 0)
    {
        var fixture = new BuildCompatibilityTests.Fixture(shift);
        var definition = fixture.Definition;
        definition.AllowAutomaticCompatibility = false;
        var camera = definition.EngineCamera!;
        camera.Layout = "renderer-camera-direct-v1"; camera.FrameCounterOffset = 0x2C8;
        camera.MainRootReferenceRva = camera.MainRootGlobalRva = camera.ContextVtableRva = 0;
        camera.ContextVtableFingerprints.Clear(); camera.CameraVtableFingerprints.Clear();
        camera.CameraReferenceRva = 0x1180; camera.CameraGlobalRva = 0x7108; camera.CameraVtableRva = 0x5200;
        definition.EngineLights = new() { Layout = "light-source-array-scene-global-v1", SceneGlobalRva = 0x7108, SceneVtableRva = 0x5200 };
        definition.NativeCapture = Current().NativeCapture;
        var native = definition.NativeCapture!;
        native.ContractId = $"manylights-filter-{definition.SteamBuildId}-v1";
        native.HookRva = 0x1A00;
        PutSignature(fixture, 0xE00, native.HookSignature);
        for (var index = 0; index < native.ContextSignatures.Count; index++)
        {
            native.ContextSignatures[index].Rva = (ulong)(0x1B00 + index * 0x100);
            PutSignature(fixture, 0xF00 + index * 0x100, native.ContextSignatures[index].Bytes);
        }
        // Actual MSVC-style COL and writable TypeDescriptor, no process/heap needed.
        fixture.Q(0x25F8, fixture.ImageBase + fixture.Tables + 0x500);
        fixture.U32(0x2900, 1); fixture.U32(0x290C, fixture.Data + 0x600);
        fixture.U32(0x2914, fixture.Tables + 0x500);
        Encoding.ASCII.GetBytes(".?AVSceneFixture@@\0").CopyTo(fixture.Bytes, 0x3A10);
        for (var index = 0; index < 3; index++)
            Encoding.ASCII.GetBytes(definition.PlayerRoot!.ExpectedTypeNames[index] + '\0')
                .CopyTo(fixture.Bytes, 0x3C00 + index * 0x200);
        fixture.Save();
        return fixture;
    }

    private static void PutSignature(BuildCompatibilityTests.Fixture fixture, int offset, string signature) =>
        signature.Split(' ').Select(s => Convert.ToByte(s, 16)).ToArray().CopyTo(fixture.Bytes, offset);
    private static string Summary(UpdateCheckReport report) => string.Join(", ", report.Anchors.Select(a => a.Name + ":" + a.Status));
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Reject(Action action, string message)
    { try { action(); } catch (InvalidDataException) { return; } throw new InvalidOperationException(message); }
}
