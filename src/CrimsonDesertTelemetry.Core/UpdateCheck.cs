using System.Diagnostics;

namespace CrimsonDesertTelemetry.Core;

public sealed record UpdateAnchor(string Name, string Status, ulong? ReferenceRva, ulong? FoundRva,
    ulong? TargetRva, string Detail);
public sealed record UpdateCheckReport(int SchemaVersion, string Status, string ExecutableSha256,
    string? ExecutableVersion, string? InstalledBuild, string ReferenceBuild, bool ExactExecutable,
    bool ActivatesAnything, string LayoutValidation, string ShaderValidation, IReadOnlyList<UpdateAnchor> Anchors);

/// <summary>Offline recovery evidence only. Never returns a runtime definition or arms capture.</summary>
public static class UpdateCheck
{
    public static UpdateCheckReport Run(string executable, IReadOnlyList<BuildDefinition> definitions)
    {
        BuildProfileValidation.ValidateAll(definitions);
        var hash = GameDiscovery.ComputeSha256(executable);
        var reference = definitions.SingleOrDefault(d =>
                string.Equals(d.ExecutableSha256, hash, StringComparison.OrdinalIgnoreCase)) ??
            definitions.Where(d => d.Status == "locally-validated").OrderByDescending(d => ulong.Parse(d.SteamBuildId)).FirstOrDefault()
            ?? throw new InvalidDataException("No validated reference profile for update diagnostics.");
        var image = CompatibilityImage.Load(executable);
        var exact = string.Equals(hash, reference.ExecutableSha256, StringComparison.OrdinalIgnoreCase);
        var anchors = new List<UpdateAnchor>();
        var camera = reference.EngineCamera!;

        void Check(string name, ulong? expected, Func<(ulong Found, ulong? Target, string Detail)> resolve)
        {
            try
            {
                var found = resolve();
                var status = exact ? (expected is not null && expected != found.Found ? "mismatch" : "matched") : "candidate";
                anchors.Add(new(name, status, expected, found.Found, found.Target, found.Detail));
            }
            catch (InvalidDataException exception)
            {
                var status = exception.Message.Contains("Ambiguous", StringComparison.OrdinalIgnoreCase) ? "ambiguous" :
                    exception.Message.Contains("Missing", StringComparison.OrdinalIgnoreCase) ? "missing" : "rejected";
                anchors.Add(new(name, status, expected, null, null, exception.Message));
            }
        }
        void Reference(string name, PatternDefinition pattern, int width, ulong? instruction = null, ulong? target = null)
        {
            Check(name, instruction, () =>
            {
                var result = image.ResolveDataReference(pattern, width);
                if (exact && target is not null && target != result.Target)
                    throw new InvalidDataException($"Resolved data target disagrees with exact profile ({target:X}).");
                return (result.Instruction, result.Target, "Unique executable-code anchor and aligned writable-data RIP target; not runtime data validation.");
            });
        }
        foreach (var pattern in reference.Patterns)
            Reference(pattern.Purpose, pattern, pattern.Purpose == "static-position-xy" ? 8 : 4);
        Reference("player-world-system", reference.PlayerRoot!.WorldSystemPattern, 8);
        for (var index = 0; index < reference.PlayerRoot.ExpectedTypeNames.Count; index++)
        {
            var type = reference.PlayerRoot.ExpectedTypeNames[index];
            Check($"player-rtti-{index}", null, () => (image.FindRttiName(type), null,
                $"Unique RTTI name {type}; this does not validate pointer-chain offsets or object identity."));
        }
        Reference("camera-scene-global", new PatternDefinition { Name = "camera scene global",
            Pattern = camera.CameraReferencePattern, RipOffset = 3, RipEnd = 7 }, 8,
            camera.CameraReferenceRva, camera.CameraGlobalRva);
        if (camera.Layout == "renderer-camera-v1")
            Reference("camera-main-global", new PatternDefinition { Name = "camera main global",
                Pattern = camera.MainRootReferencePattern, RipOffset = 3, RipEnd = 7 }, 8,
                camera.MainRootReferenceRva, camera.MainRootGlobalRva);
        void Table(string name, ulong expected, IReadOnlyList<VtableSlotFingerprint> fingerprints)
        {
            if (fingerprints.Count >= 2)
                Check(name, expected, () => (image.ResolveVtable(fingerprints, name), null, "Unique multi-slot vtable fingerprint."));
            else if (exact)
            {
                var before = anchors.Count;
                Check(name, expected, () => (expected, null, image.InspectExactVtable(expected) +
                    " Exact-profile location only; no relocatable fingerprint or runtime layout validation."));
                if (anchors[before].Status == "matched") anchors[before] = anchors[before] with { Status = "unverified" };
            }
            else
                anchors.Add(new(name, "unverified", expected, null, null,
                    "No relocatable multi-slot fingerprint exists. Reference RVA is not reused on this executable."));
        }
        Table("camera-scene-vtable", camera.CameraVtableRva, camera.CameraVtableFingerprints);
        if (camera.Layout == "renderer-camera-v1") Table("camera-context-vtable", camera.ContextVtableRva, camera.ContextVtableFingerprints);
        if (reference.EngineLights is { } lights)
            anchors.Add(new("authored-light-layout", "unverified", lights.SceneGlobalRva == 0 ? lights.RootGlobalRva : lights.SceneGlobalRva,
                null, null, "No heap read: CPU array offset, stride and field meanings require runtime validation even when the scene anchor matches."));
        if (reference.NativeCapture is { } native)
        {
            var relocatedContexts = new List<(NativeContextSignature Context, ulong Found)>();
            foreach (var context in native.ContextSignatures)
                Check("native-context-" + context.Purpose, context.Rva, () =>
                {
                    ulong found;
                    var detail = "Unique bounded dispatch-context instruction guard.";
                    if (exact)
                    {
                        if (!image.MatchesFunctionAt(context.Rva, context.Bytes))
                            throw new InvalidDataException("Missing exact-profile instruction bytes at the guarded context RVA.");
                        found = context.Rva;
                        detail = "Byte-exact context at its exact-EXE profile location; no image-wide uniqueness assumption.";
                    }
                    else
                    {
                        try { found = image.FindFunction(context.Bytes, context.Purpose); }
                        catch (InvalidDataException e) when (e.Message.Contains("Ambiguous", StringComparison.OrdinalIgnoreCase) && relocatedContexts.Count > 0)
                        {
                            var nearest = relocatedContexts.MinBy(c => Distance(c.Context.Rva, context.Rva));
                            var distance = checked((long)context.Rva - (long)nearest.Context.Rva);
                            var candidate = checked((long)nearest.Found + distance);
                            if (Math.Abs(distance) > 0x4000 || candidate < 0) throw;
                            found = (ulong)candidate;
                            if (!image.MatchesFunctionAt(found, context.Bytes)) throw;
                            detail = "Repeated fingerprint verified byte-exact at the bounded relative position from a unique dispatch context; offline candidate only.";
                        }
                    }
                    relocatedContexts.Add((context, found));
                    return (found, null, detail);
                });
            Check("native-filter-callsite", native.HookRva, () =>
            {
                if (exact)
                {
                    if (!image.MatchesFunctionAt(native.HookRva, native.HookSignature))
                        throw new InvalidDataException("Missing exact hook bytes at the guarded profile RVA.");
                    return (native.HookRva, null, "Byte-exact instruction guard at the exact executable profile location.");
                }
                var pattern = HookRelocationPattern(native.HookSignature);
                var nearest = relocatedContexts.MinBy(candidate => Distance(candidate.Context.Rva, native.HookRva));
                if (nearest.Context is null)
                    return (image.FindFunction(pattern, "native filter callsite"), null,
                        "Unique instruction guard with its rel32 call displacement masked for offline relocation only; runtime preflight remains byte-exact.");
                var delta = checked((long)native.HookRva - (long)nearest.Context.Rva);
                var candidate = checked((ulong)((long)nearest.Found + delta));
                if (!image.MatchesFunctionAt(candidate, pattern))
                    throw new InvalidDataException("Missing code fingerprint at the position established by the nearest native dispatch context.");
                return (candidate, null, "Instruction guard located relative to the unique nearest dispatch context; its rel32 call displacement is masked for offline relocation only. Runtime preflight remains byte-exact.");
            });
            if (native.Support is { } support)
            {
                foreach (var anchor in support.Anchors)
                    Check("native-support-" + anchor.Name, anchor.Rva, () =>
                    {
                        if (!exact) return (image.FindFunction(anchor.RelocationPattern, anchor.Name), null,
                            "Offline acquisition candidate; only relative address operands are masked. Field/layout semantics remain unverified.");
                        if (!image.MatchesFunctionAt(anchor.Rva, anchor.Bytes))
                            throw new InvalidDataException("Missing exact acquisition context bytes.");
                        return (anchor.Rva, null, "Byte-exact acquisition context from the shared build definition; no live execution claim.");
                    });
                Check("native-support-PhysicsCollectorVtable", support.Values["PhysicsCollectorVtableRva"], () =>
                    (image.ResolveCollectorVtable(support.Anchors.Single(a => a.Name == "PhysicsCollectorMethod").RelocationPattern), null,
                        "Unique read-only 12-slot collector shape and guarded method; natural query/profile control remains a live check."));
            }
        }
        else anchors.Add(new("native-filter-callsite", "unverified", null, null, null, "This reference has no native capture contract."));
        var failures = anchors.Any(a => a.Status is "missing" or "ambiguous" or "rejected" or "mismatch");
        return new(1, failures ? "anchor-check-failed" : exact ?
                reference.Status == "research" ? "exact-research-anchors-checked" : "exact-profile-anchors-checked" : "candidate-layout-unverified",
            hash, FileVersionInfo.GetVersionInfo(executable).FileVersion, GameDiscovery.ReadSteamBuildIdForExecutable(executable),
            reference.SteamBuildId, exact, false,
            "Offline anchors are recovery evidence, not proof of runtime layouts. No profile is promoted and no lights/hooks are enabled.",
            "Not checked: EXE identity and archived shader notes do not verify live PSO/shader-archive semantics.", anchors);
    }

    private static ulong Distance(ulong first, ulong second) => first >= second ? first - second : second - first;

    private static string HookRelocationPattern(string signature)
    {
        var tokens = signature.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length != 25 || !tokens[8].Equals("E8", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Unsupported native hook signature shape for relocation.");
        for (var displacement = 9; displacement <= 12; ++displacement)
            tokens[displacement] = "??";
        return string.Join(' ', tokens);
    }
}
