using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CrimsonDesertTelemetry.Core;

public sealed record RecoveryProgress(string Phase, int Completed, int Total, string Detail);
public sealed record RecoveryReadiness(BuildDefinition Definition, string ExecutableSha256,
    string TemplateFingerprint, string ExecutablePath, bool CacheHit,
    ulong StaticPositionXyRva, ulong StaticPositionZRva, ulong PlayerWorldRva,
    IReadOnlyList<UpdateAnchor> Anchors)
{
    internal RecoverySession Session { get; init; } = null!;
}
public sealed record RecoveryResult(BuildDefinition Definition, string ExecutableSha256,
    string TemplateFingerprint, bool CacheHit, IReadOnlyList<UpdateAnchor> Anchors)
{
    public IReadOnlyList<RecoveryInstructionGuard> InstructionGuards { get; init; } = [];
}
public sealed record RecoveryInstructionGuard(string Name, ulong Rva, string Bytes);

internal sealed record RecoveryAddress(string Name, ulong Rva, ulong? Target, string Bytes);
internal sealed record RecoveryCache(int Version, string ExecutableSha256, string TemplateFingerprint,
    List<RecoveryAddress> Addresses);
internal sealed class RecoverySession
{
    public required CompatibilityImage Image { get; init; }
    public required Dictionary<string, RecoveryAddress> Addresses { get; init; }
    public Dictionary<string, RecoveryAddress>? Cached { get; set; }
}

/// <summary>
/// Conditional recovery of the existing, unchanged acquisition contract. This
/// resolver only reads the executable. The host must establish a progressing
/// playable scene between ResolveReadiness and ResolveNative; native activation
/// must independently validate the proposed contract against live image bytes.
/// No layout guessing, collision-mask guessing, heap scan or game call occurs.
/// </summary>
public static class RuntimeRecovery
{
    private const int CacheVersion = 1;
    private const int Total = 30;
    private const string RayCallerPrefix = "48 8B 01 4D 8B C7 49 8B D4 FF 90 E8 01 00 00";
    private const string ShapeCallerPrefix = "48 8B 01 4C 8B 90 F0 01 00 00 48 8B 85 B0 00 00 00 48 89 44 24 20 4D 8B CF 4D 8B C4 49 8B D5 41 FF D2";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        MaxDepth = 12
    };

    public static RecoveryReadiness ResolveReadiness(string executable, BuildDefinition reference,
        string? cacheDirectory = null, Action<RecoveryProgress>? progress = null,
        CancellationToken cancellation = default)
    {
        ValidateReference(reference);
        cancellation.ThrowIfCancellationRequested();
        progress?.Invoke(new("identifying", 0, Total, "Checking the game executable and recovery cache."));
        var hash = GameDiscovery.ComputeSha256(executable);
        var fingerprint = Fingerprint(reference);
        var image = CompatibilityImage.Load(executable);
        var cached = ReadCache(cacheDirectory, hash, fingerprint);
        try { return ResolveReadinessCore(executable, reference, image, hash, fingerprint, cached, progress, cancellation); }
        catch (InvalidDataException) when (cached is not null)
        {
            progress?.Invoke(new("cache-rejected", 0, Total, "Cached addresses failed validation; checking current game addresses."));
            return ResolveReadinessCore(executable, reference, image, hash, fingerprint, null, progress, cancellation);
        }
    }

    private static RecoveryReadiness ResolveReadinessCore(string executable, BuildDefinition reference,
        CompatibilityImage image, string hash, string fingerprint, Dictionary<string, RecoveryAddress>? cached,
        Action<RecoveryProgress>? progress, CancellationToken cancellation)
    {
        var definition = Clone(reference);
        definition.ExecutableSha256 = hash;
        definition.ExecutableVersion = FileVersionInfo.GetVersionInfo(executable).FileVersion ?? reference.ExecutableVersion;
        definition.AllowAutomaticCompatibility = false;
        definition.EngineLights = null;
        definition.NativeCapture = null;
        var session = new RecoverySession { Image = image, Cached = cached, Addresses = [] };
        var camera = definition.EngineCamera!;
        ulong Data(string name, PatternDefinition pattern, int size)
        {
            cancellation.ThrowIfCancellationRequested();
            var pair = cached is not null
                ? image.CheckDataReferenceAt(Cached(cached, name).Rva, pattern, size)
                : image.ResolveDataReference(pattern, size);
            if (cached is not null && pair.Target != Cached(cached, name).Target)
                throw new InvalidDataException("Cached global target differs from the current instruction.");
            Add(session, name, pair.Instruction, pair.Target, Code(image, pair.Instruction, pattern.Pattern));
            Tick("preparing", session, progress, "Checking player and camera addresses.");
            return pair.Target;
        }
        var xy = Data("player-xy", definition.Patterns.Single(p => p.Purpose == "static-position-xy"), 8);
        var z = Data("player-z", definition.Patterns.Single(p => p.Purpose == "static-position-z"), 4);
        var world = Data("player-world", definition.PlayerRoot!.WorldSystemPattern, 8);
        for (var index = 0; index < definition.PlayerRoot.ExpectedTypeNames.Count; index++)
        {
            cancellation.ThrowIfCancellationRequested();
            var name = "player-rtti-" + index;
            var type = definition.PlayerRoot.ExpectedTypeNames[index];
            var rva = cached is null ? image.FindRttiName(type) : Cached(cached, name).Rva;
            if (!image.MatchesRttiAt(rva, type)) throw new InvalidDataException("Player RTTI guard is missing.");
            Add(session, name, rva, null, "");
            Tick("preparing", session, progress, "Checking player object types.");
        }
        camera.CameraGlobalRva = Data("camera-reference", new PatternDefinition
            { Name = "camera scene global", Pattern = camera.CameraReferencePattern, RipOffset = 3, RipEnd = 7 }, 8);
        camera.CameraReferenceRva = session.Addresses["camera-reference"].Rva;
        cancellation.ThrowIfCancellationRequested();
        camera.CameraVtableRva = cached is null ? image.ResolveVtable(camera.CameraVtableFingerprints, "scene camera")
            : Cached(cached, "camera-vtable").Rva;
        if (!image.MatchesVtableAt(camera.CameraVtableRva, camera.CameraVtableFingerprints))
            throw new InvalidDataException("Camera multi-slot vtable guard failed.");
        Add(session, "camera-vtable", camera.CameraVtableRva, null, "");
        BuildProfileValidation.Validate(definition);
        Tick("waiting-for-world", session, progress, "Waiting for a loaded game and progressing player/camera data.");
        return new(definition, hash, fingerprint, executable, cached is not null, xy, z, world, Report(session)) { Session = session };
    }

    public static RecoveryResult ResolveNative(RecoveryReadiness readiness, BuildDefinition reference,
        string? cacheDirectory = null, Action<RecoveryProgress>? progress = null,
        CancellationToken cancellation = default)
    {
        ValidateReference(reference);
        if (readiness.Session is null || readiness.TemplateFingerprint != Fingerprint(reference))
            throw new InvalidDataException("Recovery readiness belongs to another template.");
        cancellation.ThrowIfCancellationRequested();
        // The loaded file cannot silently change between readiness and recovery.
        if (!string.Equals(GameDiscovery.ComputeSha256(readiness.ExecutablePath), readiness.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Game executable changed during recovery; restart is required.");
        var session = readiness.Session;
        try { return ResolveNativeCore(readiness, reference, cacheDirectory, progress, cancellation); }
        catch (InvalidDataException) when (session.Cached is not null)
        {
            session.Cached = null;
            foreach (var name in session.Addresses.Keys.Where(n => n.StartsWith("native-", StringComparison.Ordinal)).ToArray())
                session.Addresses.Remove(name);
            Tick("cache-rejected", session, progress, "Cached acquisition addresses failed validation; searching once.");
            return ResolveNativeCore(readiness, reference, cacheDirectory, progress, cancellation);
        }
    }

    private static RecoveryResult ResolveNativeCore(RecoveryReadiness readiness, BuildDefinition reference,
        string? cacheDirectory, Action<RecoveryProgress>? progress, CancellationToken cancellation)
    {
        var session = readiness.Session;
        var image = session.Image;
        var cached = session.Cached;
        var definition = Clone(reference);
        definition.ExecutableSha256 = readiness.ExecutableSha256;
        definition.ExecutableVersion = readiness.Definition.ExecutableVersion;
        definition.AllowAutomaticCompatibility = false;
        definition.EngineCamera = Clone(readiness.Definition).EngineCamera;
        definition.EngineLights!.SceneGlobalRva = definition.EngineCamera!.CameraGlobalRva;
        definition.EngineLights.SceneVtableRva = definition.EngineCamera.CameraVtableRva;
        var native = definition.NativeCapture!;
        var original = reference.NativeCapture!;
        var support = native.Support!;
        var originalSupport = original.Support!;
        ulong Find(string name, string pattern, Func<ulong> resolve, Func<ulong, bool>? role = null)
        {
            cancellation.ThrowIfCancellationRequested();
            name = "native-" + name;
            var rva = cached is null ? resolve() : Cached(cached, name).Rva;
            if (!image.MatchesFunctionAt(rva, pattern) || (role is not null && !role(rva)))
                throw new InvalidDataException("Acquisition role/context mismatch: " + name);
            var bytes = Code(image, rva, pattern);
            if (cached is not null && Cached(cached, name).Bytes != bytes)
                throw new InvalidDataException("Cached code bytes changed: " + name);
            Add(session, name, rva, null, bytes);
            Tick("searching", session, progress, "Checking " + name[7..] + ".");
            return rva;
        }
        ulong Relative(ulong source, ulong referenceSource, ulong target, int bound = 0x10000)
        {
            var delta = checked((long)target - (long)referenceSource);
            if (Math.Abs(delta) > bound) throw new InvalidDataException("Recovery relation exceeds its bound.");
            return checked((ulong)((long)source + delta));
        }
        // ProcessManyLights is the unique root. All its intra-function contexts
        // retain their offsets, register lifetimes and exact non-address bytes.
        var entry = Find("context-0", original.ContextSignatures[0].Bytes,
            () => image.FindFunction(original.ContextSignatures[0].Bytes, "ProcessManyLights"),
            rva => image.MatchesNamedFunctionAt(rva, original.ContextSignatures[0].Bytes, "ProcessManyLights", atEntry: false));
        for (var index = 0; index < native.ContextSignatures.Count; index++)
        {
            var context = native.ContextSignatures[index];
            var expected = original.ContextSignatures[index];
            var name = "context-" + index;
            context.Rva = index == 0 ? entry : Find(name, expected.Bytes,
                () => index == 4 ? image.FindFunction(expected.Bytes, expected.Purpose)
                    : Relative(entry, original.ContextSignatures[0].Rva, expected.Rva, 0x4000),
                rva => index == 4 || (rva == Relative(entry, original.ContextSignatures[0].Rva, expected.Rva, 0x4000) &&
                    image.SameUnwindFunction(entry, rva, firstIsEntry: true)));
            context.Bytes = session.Addresses["native-" + name].Bytes;
        }
        var hookPattern = HookPattern(original.HookSignature);
        native.HookRva = Find("hook", hookPattern,
            () => Relative(entry, original.ContextSignatures[0].Rva, original.HookRva, 0x4000),
            rva => rva == Relative(entry, original.ContextSignatures[0].Rva, original.HookRva, 0x4000) &&
                image.SameUnwindFunction(entry, rva, firstIsEntry: true));
        native.HookSignature = session.Addresses["native-hook"].Bytes;
        NativeSupportAnchor Anchor(string name) => support.Anchors.Single(a => a.Name == name);
        NativeSupportAnchor Source(string name) => originalSupport.Anchors.Single(a => a.Name == name);
        ulong Support(string name, Func<ulong> resolve, Func<ulong, bool>? role = null)
        {
            var anchor = Anchor(name);
            anchor.Rva = Find(name, Source(name).RelocationPattern, resolve, role);
            anchor.Bytes = session.Addresses["native-" + name].Bytes;
            return anchor.Rva;
        }
        var sourceA = Support("AmbientSourceA", () => image.FindFunction(Source("AmbientSourceA").RelocationPattern, "ambient source A"),
            rva => image.MatchesNamedFunctionAt(rva, Source("AmbientSourceA").RelocationPattern, "PrecomputeAmbient", atEntry: false));
        var hookA = Support("AmbientHookA", () => Relative(sourceA, Source("AmbientSourceA").Rva, Source("AmbientHookA").Rva),
            rva => rva == Relative(sourceA, Source("AmbientSourceA").Rva, Source("AmbientHookA").Rva) && image.SameUnwindFunction(sourceA, rva));
        var sourceB = Support("AmbientSourceB", () => Relative(sourceA, Source("AmbientSourceA").Rva, Source("AmbientSourceB").Rva),
            rva => rva == Relative(sourceA, Source("AmbientSourceA").Rva, Source("AmbientSourceB").Rva) &&
                image.MatchesNamedFunctionAt(rva, Source("AmbientSourceB").RelocationPattern,
                    "GenerateAmbientFromEnvironmentAtmosphericScattering", atEntry: false));
        Support("AmbientHookB", () => Relative(sourceB, Source("AmbientSourceB").Rva, Source("AmbientHookB").Rva),
            rva => rva == Relative(sourceB, Source("AmbientSourceB").Rva, Source("AmbientHookB").Rva) && image.SameUnwindFunction(sourceB, rva));
        Support("SpatialDispatch", () => Relative(native.ContextSignatures[4].Rva, original.ContextSignatures[4].Rva, Source("SpatialDispatch").Rva),
            rva => rva == Relative(native.ContextSignatures[4].Rva, original.ContextSignatures[4].Rva, Source("SpatialDispatch").Rva) &&
                image.SameUnwindFunction(rva, native.ContextSignatures[4].Rva, firstIsEntry: true));
        Support("PhysicsShape", () => image.FindNamedFunction(Source("PhysicsShape").RelocationPattern, "TtWorldCastShape"),
            rva => image.MatchesNamedFunctionAt(rva, Source("PhysicsShape").RelocationPattern, "TtWorldCastShape"));
        Support("PhysicsRay", () => image.FindNamedFunction(Source("PhysicsRay").RelocationPattern, "TtWorldCastRay"),
            rva => image.MatchesNamedFunctionAt(rva, Source("PhysicsRay").RelocationPattern, "TtWorldCastRay"));
        var collector = Support("PhysicsCollectorMethod", () => image.FindFunction(Source("PhysicsCollectorMethod").RelocationPattern, "collector method"));
        Support("ExposureDispatch", () => image.FindNamedFunction(Source("ExposureDispatch").RelocationPattern, "AdaptExposure", atEntry: false),
            rva => image.MatchesNamedFunctionAt(rva, Source("ExposureDispatch").RelocationPattern, "AdaptExposure", atEntry: false));
        ulong CallTarget(ulong rva, int instructionOffset)
        {
            var call = image.ReadCode(rva + (ulong)instructionOffset, 5);
            var target = checked((long)rva + instructionOffset + 5 + BitConverter.ToInt32(call, 1));
            if (call[0] != 0xE8 || target <= 0 || !image.SameUnwindFunction((ulong)target, (ulong)target, firstIsEntry: true))
                throw new InvalidDataException("Post-dispatch helper is not a guarded unwind entry.");
            _ = image.ReadCode((ulong)target, 1);
            return (ulong)target;
        }
        // All four production paths call the same command helper after Dispatch.
        // Masking a rel32 address must not permit them to drift to unrelated code.
        var helper = CallTarget(native.HookRva, 8);
        if (CallTarget(Anchor("AmbientHookA").Rva, 3) != helper ||
            CallTarget(Anchor("AmbientHookB").Rva, 3) != helper ||
            CallTarget(Anchor("ExposureDispatch").Rva, 21) != helper)
            throw new InvalidDataException("Post-dispatch helper targets disagree across the four acquisition paths.");
        var shapeCaller = Support("PhysicsShapeCaller", () => image.FindFunction(Source("PhysicsShapeCaller").RelocationPattern, "natural shape caller"));
        var rayCaller = Support("PhysicsRayCaller", () => Relative(shapeCaller, Source("PhysicsShapeCaller").Rva, Source("PhysicsRayCaller").Rva, 0x4000),
            rva => rva == Relative(shapeCaller, Source("PhysicsShapeCaller").Rva, Source("PhysicsRayCaller").Rva, 0x4000));
        Find("PhysicsShapeCall", ShapeCallerPrefix, () => shapeCaller - 34,
            rva => rva == shapeCaller - 34 && image.SameUnwindFunction(rva, shapeCaller));
        Find("PhysicsRayCall", RayCallerPrefix, () => rayCaller - 15,
            rva => rva == rayCaller - 15 && image.SameUnwindFunction(rva, rayCaller));
        cancellation.ThrowIfCancellationRequested();
        var table = cached is null ? image.ResolveCollectorVtable(Source("PhysicsCollectorMethod").RelocationPattern)
            : Cached(cached, "native-PhysicsCollectorVtable").Rva;
        if (!image.MatchesCollectorVtableAt(table, collector)) throw new InvalidDataException("Physics collector vtable shape changed.");
        Add(session, "native-PhysicsCollectorVtable", table, null, "");
        native.SkyProducerRva = checked((uint)hookA);
        support.Values["PhysicsCollectorVtableRva"] = checked((uint)table);
        support.Values["UpstreamInputRva"] = checked((uint)native.ContextSignatures[^2].Rva);
        support.Values["UpstreamBinderRva"] = checked((uint)native.ContextSignatures[^1].Rva);
        support.Values["ExposureReturnRva"] = checked((uint)(Anchor("ExposureDispatch").Rva + 18));
        BuildProfileValidation.Validate(definition);
        if (session.Addresses.Count != Total) throw new InvalidDataException("Recovery did not cover the complete contract.");
        // Persist only bounded RVAs/instruction guards, never process pointers.
        // A cache hit reruns all role/byte checks above and still needs live gates.
        WriteCache(cacheDirectory, readiness.ExecutableSha256, readiness.TemplateFingerprint, session.Addresses.Values.ToList());
        Tick("addresses-validated", session, progress, "Addresses verified; validating live acquisition before enabling telemetry.");
        return new(definition, readiness.ExecutableSha256, readiness.TemplateFingerprint, cached is not null, Report(session))
        {
            InstructionGuards = session.Addresses.Values.Where(a => a.Bytes.Length != 0)
                .Select(a => new RecoveryInstructionGuard(a.Name, a.Rva, a.Bytes)).ToArray()
        };
    }

    public static string Fingerprint(BuildDefinition reference) => Convert.ToHexString(SHA256.HashData(
        Encoding.UTF8.GetBytes("cdt-runtime-recovery-v" + CacheVersion + "\n" + JsonSerializer.Serialize(reference))));

    private static void ValidateReference(BuildDefinition reference)
    {
        BuildProfileValidation.Validate(reference);
        if (reference.NativeCapture?.Support is null || reference.NativeCapture.ContextSignatures.Count != 7 ||
            reference.EngineCamera?.Layout != "renderer-camera-direct-v1" || reference.EngineCamera.CameraVtableFingerprints.Count < 2)
            throw new InvalidDataException("This profile has no complete recoverable acquisition contract.");
    }
    private static BuildDefinition Clone(BuildDefinition definition) => JsonSerializer.Deserialize<BuildDefinition>(JsonSerializer.Serialize(definition))!;
    private static RecoveryAddress Cached(Dictionary<string, RecoveryAddress> cache, string name) =>
        cache.TryGetValue(name, out var entry) ? entry : throw new InvalidDataException("Incomplete recovery cache: " + name);
    private static string Code(CompatibilityImage image, ulong rva, string pattern) =>
        string.Join(' ', image.ReadCode(rva, SignaturePattern.Parse(pattern).Length).Select(b => b.ToString("X2")));
    private static void Add(RecoverySession session, string name, ulong rva, ulong? target, string bytes) =>
        session.Addresses[name] = new(name, rva, target, bytes);
    private static IReadOnlyList<UpdateAnchor> Report(RecoverySession session) => session.Addresses.Values.Select(a =>
        new UpdateAnchor(a.Name, "recovered-address", null, a.Rva, a.Target,
            "Validated against the current file and unchanged acquisition roles; live activation remains a separate check.")).ToArray();
    private static void Tick(string phase, RecoverySession session, Action<RecoveryProgress>? progress, string detail) =>
        progress?.Invoke(new(phase, session.Addresses.Count, Total, detail));
    private static string HookPattern(string signature)
    {
        var tokens = signature.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length != 25 || tokens[8] != "E8") throw new InvalidDataException("Unsupported native hook instruction shape.");
        for (var index = 9; index <= 12; index++) tokens[index] = "??";
        return string.Join(' ', tokens);
    }
    private static string CachePath(string directory, string hash, string fingerprint) =>
        Path.Combine(Path.GetFullPath(directory), hash + "-" + fingerprint + ".json");
    private static Dictionary<string, RecoveryAddress>? ReadCache(string? directory, string hash, string fingerprint)
    {
        if (directory is null) return null;
        try
        {
            var path = CachePath(directory, hash, fingerprint);
            if (!File.Exists(path) || new FileInfo(path).Length is < 2 or > 128 * 1024) return null;
            using var stream = File.OpenRead(path);
            using var document = JsonDocument.Parse(stream, new JsonDocumentOptions { MaxDepth = 12 });
            RejectDuplicateProperties(document.RootElement);
            var cache = document.RootElement.Deserialize<RecoveryCache>(JsonOptions);
            if (cache is null || cache.Version != CacheVersion || cache.ExecutableSha256 != hash ||
                cache.TemplateFingerprint != fingerprint || cache.Addresses is null || cache.Addresses.Count != Total ||
                cache.Addresses.Any(a => a is null || string.IsNullOrEmpty(a.Name) || a.Name.Length > 80 ||
                    a.Rva is 0 or > int.MaxValue || a.Target > int.MaxValue || a.Bytes is null || a.Bytes.Length > 128 * 3) ||
                cache.Addresses.Select(a => a.Name).Distinct(StringComparer.Ordinal).Count() != Total) return null;
            return cache.Addresses.ToDictionary(a => a.Name, StringComparer.Ordinal);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or ArgumentException) { return null; }
    }
    private static void RejectDuplicateProperties(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var property in value.EnumerateObject())
            {
                if (!names.Add(property.Name)) throw new JsonException("Duplicate recovery-cache property.");
                RejectDuplicateProperties(property.Value);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array)
            foreach (var child in value.EnumerateArray()) RejectDuplicateProperties(child);
    }
    private static void WriteCache(string? directory, string hash, string fingerprint, List<RecoveryAddress> addresses)
    {
        if (directory is null) return;
        string? temporary = null;
        try
        {
            Directory.CreateDirectory(directory);
            var path = CachePath(directory, hash, fingerprint);
            temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                JsonSerializer.Serialize(stream, new RecoveryCache(CacheVersion, hash, fingerprint, addresses), JsonOptions);
            File.Move(temporary, path, overwrite: true);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { /* Cache is optional, never activation authority. */ }
        finally
        {
            if (temporary is not null && File.Exists(temporary))
                try { File.Delete(temporary); }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
    }
}
