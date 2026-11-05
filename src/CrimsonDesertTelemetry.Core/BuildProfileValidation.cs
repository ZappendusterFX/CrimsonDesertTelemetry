namespace CrimsonDesertTelemetry.Core;

/// <summary>Reject malformed profiles before either exact-hash trust or diagnostic scanning.</summary>
public static class BuildProfileValidation
{
    // Candidate anchors are executable only in the caller's explicit private,
    // exact-hash research mode. Profile validation alone never promotes them.
    public static void ValidateRuntimeAnchor(PatternDefinition pattern, BuildDefinition definition,
        bool allowResearchCandidate, string failure)
    {
        if (pattern.Confidence == "locally-validated" ||
            (allowResearchCandidate && definition.Status == "research" && pattern.Confidence == "candidate"))
            return;
        Fail(failure);
    }

    public static void ValidateAll(IReadOnlyList<BuildDefinition> definitions)
    {
        if (definitions.Count is 0 or > 64) Fail("Expected 1..64 build profiles.");
        var hashes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var definition in definitions)
        {
            Validate(definition);
            if (!hashes.Add(definition.ExecutableSha256)) Fail("Duplicate executable hash in build profiles.");
        }
    }

    public static void Validate(BuildDefinition definition)
    {
        if (definition is null || definition.SchemaVersion != 1) Fail("Unsupported build-profile schema.");
        if (!Hex(definition.ExecutableSha256, 64) || definition.ExecutableSha256.All(c => c == '0'))
            Fail("Build profile requires a complete nonzero executable SHA-256.");
        if (definition.SteamAppId != "3321460" || !ulong.TryParse(definition.SteamBuildId, out var build) || build == 0 ||
            !Version.TryParse(definition.ExecutableVersion, out _)) Fail("Invalid game/build/version identity.");
        if (definition.Status is not ("locally-validated" or "research")) Fail("Unknown profile validation status.");
        if (definition.Patterns is null || definition.Patterns.Count != 2) Fail("Exactly two player-position patterns are required.");
        foreach (var purpose in new[] { "static-position-xy", "static-position-z" })
        {
            var patterns = definition.Patterns.Where(p => p is not null && p.Purpose == purpose).ToArray();
            if (patterns.Length != 1) Fail($"Missing/ambiguous {purpose} profile.");
            ValidatePattern(patterns[0], definition.Status == "locally-validated");
        }
        ValidatePlayerRoot(definition.PlayerRoot ?? throw new InvalidDataException("Missing player-root profile."),
            definition.Status == "locally-validated");

        var camera = definition.EngineCamera ?? throw new InvalidDataException("Missing camera profile.");
        if (camera.Layout is not ("renderer-camera-v1" or "renderer-camera-direct-v1")) Fail("Unknown camera layout.");
        Rva(camera.CameraReferenceRva); Rva(camera.CameraGlobalRva, 8); Rva(camera.CameraVtableRva, 8);
        Signature(camera.CameraReferencePattern, 12, 64);
        CameraInstruction(camera.CameraReferencePattern, "05");
        var currentSceneLayout = UsesCurrentSceneLayout(definition);
        if (camera.SourcePointerOffset != (currentSceneLayout ? 0x430 : 0x428) ||
            (currentSceneLayout && camera.Layout != "renderer-camera-direct-v1"))
            Fail("Unsupported camera source-pointer layout.");
        Fingerprints(camera.CameraVtableFingerprints);
        Fingerprints(camera.ContextVtableFingerprints);
        if (camera.Layout == "renderer-camera-v1")
        {
            Rva(camera.MainRootReferenceRva); Rva(camera.MainRootGlobalRva, 8); Rva(camera.ContextVtableRva, 8);
            CameraInstruction(camera.MainRootReferencePattern, "0D");
            if (camera.MainRootGlobalRva == camera.CameraGlobalRva || camera.ContextVtableRva == camera.CameraVtableRva)
                Fail("Camera roots and vtables must be independent.");
        }
        else
        {
            Offset(camera.FrameCounterOffset, 4);
            if (camera.MainRootReferenceRva != 0 || camera.MainRootGlobalRva != 0 || camera.ContextVtableRva != 0)
                Fail("Direct-camera profile must not declare a legacy context chain.");
        }
        if (definition.AllowAutomaticCompatibility && (definition.Status != "locally-validated" ||
                camera.CameraVtableFingerprints.Count < 2 ||
                (camera.Layout == "renderer-camera-v1" && camera.ContextVtableFingerprints.Count < 2)))
            Fail("Automatic compatibility requires multi-slot fingerprints for every object its camera layout reads.");
        if (definition.EngineLights is { } lights)
        {
            Offset(lights.ArrayPointerOffset, 8);
            if (lights.Layout == "light-source-array-v1") Rva(lights.RootGlobalRva, 8);
            else if (lights.Layout == "light-source-array-scene-global-v1")
            {
                Rva(lights.SceneGlobalRva, 8); Rva(lights.SceneVtableRva, 8);
                if (camera.Layout != "renderer-camera-direct-v1" || lights.SceneGlobalRva != camera.CameraGlobalRva ||
                    lights.SceneVtableRva != camera.CameraVtableRva) Fail("Direct camera and light scene identities disagree.");
            }
            else Fail("Unknown engine-light layout.");
        }
        if (definition.NativeCapture is { } native) ValidateNative(definition, native);
    }

    public static void ValidatePlayerRoot(PlayerRootDefinition root, bool trusted = true)
    {
        if (root is null || root.TransformLayout is not ("basis-v1" or "sqt-v1") || root.WorldSystemPattern is null ||
            root.WorldSystemPattern.Purpose != "world-system") Fail("Unknown player-root layout or anchor.");
        ValidatePattern(root.WorldSystemPattern, trusted);
        foreach (var offset in new[] { root.WorldSystemToActorManagerOffset, root.ActorManagerToPlayerActorOffset,
                     root.PlayerActorToIntermediateOffset, root.IntermediateToControlOffset,
                     root.ControlToOwnerOffset, root.OwnerToPhysicsOffset }) Offset(offset, 8);
        foreach (var offset in new[] { root.PositionOffset, root.BasisXOffset, root.BasisYOffset, root.BasisZOffset })
            Offset(offset, 4);
        if (root.TransformLayout == "sqt-v1") Offset(root.QuaternionOffset, 4);
        var fields = root.TransformLayout == "sqt-v1"
            ? new[] { (root.PositionOffset, 12), (root.QuaternionOffset, 16) }
            : new[] { (root.PositionOffset, 12), (root.BasisXOffset, 12), (root.BasisYOffset, 12), (root.BasisZOffset, 12) };
        for (var index = 0; index < fields.Length; index++)
            for (var other = index + 1; other < fields.Length; other++)
                if (fields[index].Item1 < fields[other].Item1 + fields[other].Item2 &&
                    fields[other].Item1 < fields[index].Item1 + fields[index].Item2) Fail("Player transform fields overlap.");
        if (root.ExpectedTypeNames is null || root.ExpectedTypeNames.Count != 3 ||
            root.ExpectedTypeNames.Any(n => string.IsNullOrWhiteSpace(n) || n.Length > 150) ||
            root.ExpectedTypeNames.Distinct(StringComparer.Ordinal).Count() != 3) Fail("Three distinct player RTTI guards are required.");
    }

    private static void ValidateNative(BuildDefinition definition, NativeCaptureDefinition native)
    {
        var expectedContractId = $"manylights-filter-{definition.SteamBuildId}-v1";
        // A profile may also serve as an automatic camera template: BuildCompatibility.Relocate
        // drops NativeCapture and EngineLights, so only the exact executable ever runs this contract.
        if (native.SchemaVersion != 1 || native.ContractId != expectedContractId ||
            native.Status != definition.Status || native.Status is not ("locally-validated" or "research") ||
            !native.RequiresExactExecutable ||
            definition.EngineCamera?.Layout != "renderer-camera-direct-v1" ||
            definition.EngineLights?.Layout != "light-source-array-scene-global-v1")
            Fail("Native capture must use its supported exact-executable contract and coherent direct scene profile.");
        Rva(native.HookRva); Signature(native.HookSignature, 25, 25, exact: true);
        Rva(native.SkyProducerRva);
        if (native.ContextSignatures is null || native.ContextSignatures.Count is < 1 or > 8 ||
            native.ContextSignatures.Any(c => c is null) ||
            native.ContextSignatures.Select(c => c.Rva).Distinct().Count() != native.ContextSignatures.Count)
            Fail("Native dispatch contexts must be distinct and bounded.");
        foreach (var context in native.ContextSignatures)
        {
            Rva(context.Rva); Signature(context.Bytes, 8, 128, exact: true);
            if (string.IsNullOrWhiteSpace(context.Purpose) || context.Purpose.Length > 160) Fail("Missing native context purpose.");
        }
        var currentSceneLayout = UsesCurrentSceneLayout(definition);
        if (native.Registers is not { Output: "r12", Command: "rbx", Counter: "r15", Owner: "r13" } ||
            native.Wrapper is not { InnerOffset: 48, StrideOffset: 192, CountOffset: 196, ResourceOffset: 360,
                NativeListOffset: 8 } ||
            native.Wrapper.CommandHolderOffset != (currentSceneLayout ? 0x8C0 : 0x800) ||
            native.Wrapper.OwnerBankIndexOffset != (currentSceneLayout ? 0x8E0 : 0x8F8) ||
            native.Scene is not { ByteCount: 2816, FrameOffset: 32, ScreenOffset: 48,
                PositionOffset: 128, DirectionOffset: 144, ViewRelativeOffset: 160, EarthRadiusOffset: 2752, EarthRadius: 6360000f } ||
            native.Scene.RootPointerOffset != definition.EngineCamera.SourcePointerOffset ||
            native.Gpu is not { RecordCount: 32768, RecordStride: 48, CounterBytes: 256, ValidCounterByteOffset: 4,
                ResourceState: "unordered-access" }) Fail("Unsupported native capture ABI, register or layout contract.");
        if (native.ShaderEvidence is { } evidence && (!Hex(evidence.ProducerEntry, 8) ||
            evidence.ConsumerEntries is null || evidence.ConsumerEntries.Count is < 1 or > 16 ||
            evidence.ConsumerEntries.Any(e => !Hex(e, 8)) || !Hex(evidence.ProducerDxilHash, 32) ||
            string.IsNullOrWhiteSpace(evidence.Source) || evidence.Source.Length > 512 ||
            evidence.Validation != "archived-one-variant-not-live-pso")) Fail("Invalid or overstated archived shader evidence.");
        if ((definition.SteamBuildId is "26423880" or "26448710") && native.Support is null)
            Fail("Current full-product profile requires its acquisition support anchors.");
        if (native.Support is { } support) ValidateSupport(native, support);
    }

    private static bool UsesCurrentSceneLayout(BuildDefinition definition) =>
        definition.SteamBuildId is "26361232" or "26423880" or "26448710";

    private static void ValidateSupport(NativeCaptureDefinition native, NativeSupportDefinition support)
    {
        string[] keys = ["AmbientSourceOffset", "RendererGiOffset", "RendererSkyOffset", "RendererExposureOffset",
            "PhysicsCollectorVtableRva", "PhysicsQueryProfile", "UpstreamInputRva", "UpstreamBinderRva",
            "ExposureReturnRva"];
        string[] names = ["AmbientHookA", "AmbientHookB", "AmbientSourceA", "AmbientSourceB", "SpatialDispatch",
            "PhysicsShape", "PhysicsRay", "PhysicsCollectorMethod", "ExposureDispatch", "PhysicsShapeCaller", "PhysicsRayCaller"];
        if (native.ContextSignatures.Count != 7 || support.Values is null || !keys.Order().SequenceEqual(support.Values.Keys.Order()) ||
            support.Anchors is null || support.Anchors.Any(a => a is null) ||
            !names.Order().SequenceEqual(support.Anchors.Select(a => a.Name).Order()))
            Fail("Incomplete or unknown full-product acquisition anchors.");
        foreach (var anchor in support.Anchors)
        {
            Rva(anchor.Rva); Signature(anchor.Bytes, 12, 128, exact: true);
            Signature(anchor.RelocationPattern, 12, 128);
            var exact = SignaturePattern.Parse(anchor.Bytes);
            var masked = SignaturePattern.Parse(anchor.RelocationPattern);
            if (exact.Length != masked.Length || !masked.FindAll(Convert.FromHexString(
                    new string(anchor.Bytes.Where(c => !char.IsWhiteSpace(c)).ToArray()))).SequenceEqual([0]))
                Fail("Recovery fingerprint does not describe the exact guarded instruction bytes.");
        }
        foreach (var key in keys.Where(k => k.EndsWith("Rva", StringComparison.Ordinal))) Rva(support.Values[key]);
        if ((support.Values["PhysicsCollectorVtableRva"] & 7) != 0 || support.Values["PhysicsQueryProfile"] != 0x40000017 ||
            support.Values["AmbientSourceOffset"] != 0xA8 || support.Values["RendererGiOffset"] != 0x688 ||
            support.Values["RendererSkyOffset"] != 0x690 || support.Values["RendererExposureOffset"] != 0x6B8 ||
            support.Anchors.Single(a => a.Name == "AmbientHookA").Rva != native.SkyProducerRva ||
            support.Values["UpstreamInputRva"] != native.ContextSignatures[^2].Rva ||
            support.Values["UpstreamBinderRva"] != native.ContextSignatures[^1].Rva ||
            support.Values["ExposureReturnRva"] != support.Anchors.Single(a => a.Name == "ExposureDispatch").Rva + 18)
            Fail("Unsupported or incoherent acquisition layout.");
    }

    public static void ValidatePattern(PatternDefinition pattern, bool trusted = true)
    {
        if (pattern is null || string.IsNullOrWhiteSpace(pattern.Name) || pattern.Name.Length > 128 ||
            pattern.Section != ".text" || pattern.Confidence is not ("locally-validated" or "candidate") ||
            (trusted && pattern.Confidence != "locally-validated")) Fail("Invalid pattern identity/validation status.");
        var length = Signature(pattern.Pattern, 12, 256);
        var offset = pattern.RipOffset ?? -1;
        var end = pattern.RipEnd ?? -1;
        if (offset < 0 ||
            offset > length - 4 || end < offset + 4 || end > length) Fail("Invalid RIP displacement boundaries.");
        var tokens = pattern.Pattern.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (tokens.Skip(offset).Take(4).Any(t => t is not ("?" or "??")))
            Fail("RIP displacement must be the declared four wildcard bytes.");
    }

    private static void CameraInstruction(string pattern, string operand)
    {
        Signature(pattern, 12, 64);
        var bytes = pattern.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (bytes[0] != "48" || bytes[1].ToUpperInvariant() != "8B" || bytes[2].ToUpperInvariant() != operand)
            Fail("Camera anchor is not the supported RIP-relative MOV instruction.");
        if (bytes.Skip(3).Take(4).Any(t => t is not ("?" or "??"))) Fail("Camera RIP displacement must be wildcarded.");
    }
    private static void Fingerprints(List<VtableSlotFingerprint> fingerprints)
    {
        if (fingerprints is null || fingerprints.Count > 16 || fingerprints.Any(f => f is null) ||
            fingerprints.Select(f => f.Slot).Distinct().Count() != fingerprints.Count)
            Fail("Invalid vtable fingerprint list.");
        foreach (var fingerprint in fingerprints)
        {
            if (fingerprint.Slot is < 0 or > 64) Fail("Vtable slot is out of bounds.");
            Signature(fingerprint.Pattern, 12, 256);
        }
    }
    private static int Signature(string text, int minimum, int maximum, bool exact = false)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(text) || text.Length > maximum * 3) Fail("Missing/oversize signature.");
            var parsed = SignaturePattern.Parse(text);
            var tokens = text.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (parsed.Length < minimum || parsed.Length > maximum || tokens.Count(t => t is not ("?" or "??")) < 7 ||
                (exact && tokens.Any(t => t is "?" or "??"))) Fail("Signature is too weak or not exact where required.");
            return parsed.Length;
        }
        catch (FormatException exception) { throw new InvalidDataException("Malformed profile signature.", exception); }
    }
    private static bool Hex(string value, int length) => value is not null && value.Length == length && value.All(Uri.IsHexDigit);
    private static void Rva(ulong value, int alignment = 1)
    { if (value is 0 or > int.MaxValue || value % (ulong)alignment != 0) Fail("Invalid or unaligned profile RVA."); }
    private static void Offset(int value, int alignment)
    { if (value is <= 0 or > 0x10000 || value % alignment != 0) Fail("Invalid or unaligned object-field offset."); }
    [System.Diagnostics.CodeAnalysis.DoesNotReturn]
    private static void Fail(string message) => throw new InvalidDataException(message);
}
