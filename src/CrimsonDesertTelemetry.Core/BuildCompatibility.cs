using System.Diagnostics;
using System.Text.Json;

namespace CrimsonDesertTelemetry.Core;

/// <summary>Identification is not a guarantee of correctness on an untested engine build.</summary>
public sealed record CompatibilityInfo(string Mode, string ExecutableSha256,
    string? ExecutableVersion, string ReferenceBuild);

public sealed record ResolvedBuild(BuildDefinition Definition, string GameBuild, CompatibilityInfo Compatibility);

public static class BuildCompatibility
{
    public static ResolvedBuild Resolve(string executable, IReadOnlyList<BuildDefinition> definitions,
        bool allowPrivateResearchExact = false)
    {
        BuildProfileValidation.ValidateAll(definitions);
        var hash = GameDiscovery.ComputeSha256(executable);
        var known = definitions.SingleOrDefault(definition =>
            string.Equals(definition.ExecutableSha256, hash, StringComparison.OrdinalIgnoreCase));
        if (known is not null && known.Status == "locally-validated")
            return Result(executable, known, known, hash, "tested");
        if (allowPrivateResearchExact && known is not null && known.Status == "research" &&
            !known.AllowAutomaticCompatibility)
            return Result(executable, known, known, hash, "research-exact");
        return ResolveAutomatic(executable, definitions, hash);
    }

    // Offline diagnostics use this even on a known EXE. It is NOT a runtime bypass:
    // every signature and table guard is still required.
    public static ResolvedBuild CheckAutomatic(string executable, IReadOnlyList<BuildDefinition> definitions) =>
        ResolveAutomatic(executable, definitions, GameDiscovery.ComputeSha256(executable));

    private static ResolvedBuild ResolveAutomatic(string executable, IReadOnlyList<BuildDefinition> definitions,
        string hash)
    {
        BuildProfileValidation.ValidateAll(definitions);
        var image = CompatibilityImage.Load(executable);
        var matches = new List<(BuildDefinition Reference, BuildDefinition Resolved)>();
        var failures = new List<string>();
        foreach (var reference in definitions.Where(static definition =>
                     definition.AllowAutomaticCompatibility && definition.Status == "locally-validated"))
        {
            try { matches.Add((reference, Relocate(image, reference))); }
            catch (InvalidDataException exception) { failures.Add($"{reference.SteamBuildId}: {exception.Message}"); }
        }
        if (matches.Count != 1)
            throw new InvalidDataException($"Executable not accepted (SHA-256 {hash}): automatic compatibility " +
                $"matched {matches.Count} layouts; exactly one is required. {string.Join("; ", failures)}");
        var match = matches[0];
        return Result(executable, match.Resolved, match.Reference, hash, "automatic");
    }

    private static ResolvedBuild Result(string executable, BuildDefinition resolved, BuildDefinition reference,
        string hash, string mode) => new(resolved,
        mode is "tested" or "research-exact"
            ? reference.SteamBuildId
            : GameDiscovery.ReadSteamBuildIdForExecutable(executable) ?? "unknown",
        new CompatibilityInfo(mode, hash, FileVersionInfo.GetVersionInfo(executable).FileVersion,
            reference.SteamBuildId));

    private static BuildDefinition Relocate(CompatibilityImage image, BuildDefinition reference)
    {
        // Never mutate or promote an untested EXE into the persisted allowlist.
        var definition = JsonSerializer.Deserialize<BuildDefinition>(JsonSerializer.Serialize(reference))!;
        // Light offsets are never carried onto a merely pattern-compatible executable.
        // They are enabled only by an exact, locally validated executable hash.
        definition.EngineLights = null;
        definition.NativeCapture = null;
        var camera = definition.EngineCamera
            ?? throw new InvalidDataException("No native camera layout.");
        if (camera.Layout is not ("renderer-camera-v1" or "renderer-camera-direct-v1"))
            throw new InvalidDataException("Unknown camera layout.");
        var direct = camera.Layout == "renderer-camera-direct-v1";
        var root = definition.PlayerRoot ?? throw new InvalidDataException("No player-root layout.");
        if (root.ExpectedTypeNames.Count != 3 || root.ExpectedTypeNames.Any(string.IsNullOrWhiteSpace))
            throw new InvalidDataException("Player-root type guards are missing.");
        foreach (var purpose in new[] { "static-position-xy", "static-position-z" })
        {
            var patterns = definition.Patterns.Where(pattern => pattern.Purpose == purpose).ToArray();
            if (patterns.Length != 1) throw new InvalidDataException($"Missing/ambiguous {purpose} definition.");
            image.ResolveDataReference(patterns[0], purpose == "static-position-xy" ? 8 : 4);
        }
        image.ResolveDataReference(root.WorldSystemPattern, 8);

        (camera.CameraReferenceRva, camera.CameraGlobalRva) = image.ResolveDataReference(
            new PatternDefinition { Pattern = camera.CameraReferencePattern, RipOffset = 3, RipEnd = 7,
                Name = "camera global" }, 8);
        // The renderer camera class has no MSVC RTTI locator (checked on 1.0.0.2976), so its
        // type guard is the multi-slot vtable fingerprint; the player chain keeps its RTTI guards.
        camera.CameraVtableRva = image.ResolveVtable(camera.CameraVtableFingerprints, "camera");
        if (direct)
        {
            // The direct layout reads the camera global only; its profile offsets (source pointer,
            // frame counter) are carried unchanged and remain subject to the live decode checks.
            if (camera.MainRootReferenceRva != 0 || camera.MainRootGlobalRva != 0 || camera.ContextVtableRva != 0)
                throw new InvalidDataException("Direct-camera template declares a legacy context chain.");
            return definition;
        }
        (camera.MainRootReferenceRva, camera.MainRootGlobalRva) = image.ResolveDataReference(
            new PatternDefinition { Pattern = camera.MainRootReferencePattern, RipOffset = 3, RipEnd = 7,
                Name = "camera main root" }, 8);
        if (camera.MainRootGlobalRva == camera.CameraGlobalRva)
            throw new InvalidDataException("Camera roots must be independent.");
        camera.ContextVtableRva = image.ResolveVtable(camera.ContextVtableFingerprints, "context");
        if (camera.ContextVtableRva == camera.CameraVtableRva)
            throw new InvalidDataException("Camera and context tables must differ.");
        return definition;
    }
}

/// <summary>Bounded, file-backed scanning. Never scans game heaps or executes candidate code.</summary>
internal sealed class CompatibilityImage
{
    private readonly string _path;
    private readonly IReadOnlyList<PeSectionInfo> _sections;
    private readonly Dictionary<PeSectionInfo, byte[]> _bytes = [];
    private readonly Dictionary<string, ulong> _functions = [];
    private readonly ulong _imageBase;

    private CompatibilityImage(string path, IReadOnlyList<PeSectionInfo> sections, ulong imageBase)
    { _path = path; _sections = sections; _imageBase = imageBase; }

    public static CompatibilityImage Load(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            using var reader = new BinaryReader(stream);
            if (stream.Length < 64 || reader.ReadUInt16() != 0x5A4D)
                throw new InvalidDataException("Invalid DOS header.");
            stream.Position = 0x3C;
            var pe = reader.ReadUInt32();
            if ((ulong)pe + 24 + 112 > (ulong)stream.Length)
                throw new InvalidDataException("Truncated PE header.");
            stream.Position = pe;
            if (reader.ReadUInt32() != 0x4550 || reader.ReadUInt16() != 0x8664)
                throw new InvalidDataException("An AMD64 PE executable is required.");
            var count = reader.ReadUInt16();
            stream.Position = pe + 20;
            var optionalSize = reader.ReadUInt16();
            if (count is 0 or > 96 || optionalSize < 112 ||
                (ulong)pe + 24 + optionalSize + (ulong)count * 40 > (ulong)stream.Length)
                throw new InvalidDataException("Invalid PE section table.");
            stream.Position = pe + 24;
            if (reader.ReadUInt16() != 0x20B) throw new InvalidDataException("PE32+ is required.");
            stream.Position = pe + 24 + 24;
            var imageBase = reader.ReadUInt64();
            stream.Position = pe + 24 + 56;
            var imageSize = reader.ReadUInt32();
            if (imageBase < 0x10000 || imageBase > 0x00007FFFFFFFFFFFUL - imageSize || imageSize == 0)
                throw new InvalidDataException("Invalid PE image range.");
            var sections = PortableExecutable.ReadSectionHeaders(path);
            ulong previousEnd = 0;
            foreach (var section in sections.OrderBy(section => section.VirtualAddress))
            {
                var end = (ulong)section.VirtualAddress + Math.Max(section.VirtualSize, section.RawSize);
                if (section.VirtualAddress < previousEnd || end > imageSize ||
                    section.RawSize > int.MaxValue ||
                    (section.RawSize != 0 && (ulong)section.RawOffset + section.RawSize > (ulong)stream.Length))
                    throw new InvalidDataException("Invalid, overlapping or truncated PE section.");
                previousEnd = end;
            }
            return new CompatibilityImage(path, sections, imageBase);
        }
        catch (EndOfStreamException exception) { throw new InvalidDataException("Truncated PE file.", exception); }
    }

    private byte[] Bytes(PeSectionInfo section)
    {
        if (!_bytes.TryGetValue(section, out var bytes))
            _bytes[section] = bytes = PortableExecutable.ReadSection(_path, section).Bytes;
        return bytes;
    }

    public ulong FindFunction(string text, string label)
    {
        if (_functions.TryGetValue(text, out var cached)) return cached;
        var pattern = SignaturePattern.Parse(text);
        if (pattern.Length < 12) throw new InvalidDataException($"Fingerprint too short: {label}.");
        var matches = new List<ulong>();
        foreach (var section in _sections.Where(section => section.Executable && section.RawSize > 0))
        {
            foreach (var offset in pattern.FindAll(Bytes(section)))
            {
                matches.Add(section.VirtualAddress + (ulong)offset);
                if (matches.Count > 1) throw new InvalidDataException($"Ambiguous code fingerprint: {label}.");
            }
        }
        if (matches.Count != 1) throw new InvalidDataException($"Missing code fingerprint: {label}.");
        return _functions[text] = matches[0];
    }

    internal bool MatchesFunctionAt(ulong rva, string text)
    {
        var pattern = SignaturePattern.Parse(text);
        if (pattern.Length < 12) throw new InvalidDataException("Fingerprint too short.");
        var section = _sections.SingleOrDefault(candidate => candidate.Executable &&
            rva >= candidate.VirtualAddress && rva + (ulong)pattern.Length <=
            (ulong)candidate.VirtualAddress + candidate.RawSize);
        if (section is null) return false;
        var offset = checked((int)(rva - section.VirtualAddress));
        return pattern.FindAll(Bytes(section).AsSpan(offset, pattern.Length)).SequenceEqual([0]);
    }

    internal ulong ResolveCollectorVtable(string methodPattern)
    {
        var method = _imageBase + FindFunction(methodPattern, "physics collector method");
        var matches = new List<ulong>();
        foreach (var section in _sections.Where(s => s.Readable && !s.Executable && !s.Writable))
        {
            var bytes = Bytes(section);
            for (var start = 0; start + 12 * 8 <= bytes.Length; start += 8)
            {
                if (((section.VirtualAddress + (ulong)start) & 7) != 0 ||
                    BitConverter.ToUInt64(bytes, start + 40) != method) continue;
                var slots = Enumerable.Range(0, 12).Select(i => BitConverter.ToUInt64(bytes, start + i * 8)).ToArray();
                if (slots[0] != slots[9] || slots[3] != slots[7] || slots[6] != slots[8] ||
                    slots.Any(p => p < _imageBase || !IsExecutable(p - _imageBase))) continue;
                matches.Add(section.VirtualAddress + (ulong)start);
            }
        }
        if (matches.Count != 1) throw new InvalidDataException(matches.Count == 0 ?
            "Missing physics collector vtable shape." : "Ambiguous physics collector vtable shape.");
        return matches[0];
    }

    public (ulong Instruction, ulong Target) ResolveDataReference(PatternDefinition pattern, int size)
    {
        var length = SignaturePattern.Parse(pattern.Pattern).Length;
        if (pattern.RipOffset is not int displacement || pattern.RipEnd is not int end ||
            displacement < 0 || displacement + 4 > length || end < displacement + 4 || end > length)
            throw new InvalidDataException("Invalid RIP-reference definition.");
        var instruction = FindFunction(pattern.Pattern, pattern.Name);
        var relative = BitConverter.ToInt32(ReadRva(instruction + (ulong)displacement, 4));
        var target = (long)instruction + end + relative;
        if (target < 0 || (target & 3) != 0 || !IsData((ulong)target, size, writable: true))
            throw new InvalidDataException($"RIP target is not writable game data: {pattern.Name}.");
        return (instruction, (ulong)target);
    }

    public ulong ResolveVtable(IReadOnlyList<VtableSlotFingerprint> fingerprints, string label)
    {
        if (fingerprints.Count < 2 || fingerprints.Select(fingerprint => fingerprint.Slot).Distinct().Count() != fingerprints.Count ||
            fingerprints.Any(fingerprint => fingerprint.Slot is < 0 or > 64))
            throw new InvalidDataException($"At least two distinct bounded vtable slots are required: {label}.");
        var slots = fingerprints.Select(fingerprint => (fingerprint.Slot,
            Address: _imageBase + FindFunction(fingerprint.Pattern, label))).ToArray();
        var maxSlot = slots.Max(slot => slot.Slot);
        var candidates = new List<ulong>();
        var pattern = SignaturePattern.Parse(string.Join(" ", BitConverter.GetBytes(slots[0].Address)
            .Select(value => value.ToString("X2"))));
        foreach (var section in _sections.Where(section => section.Readable && !section.Executable && !section.Writable))
        {
            var bytes = Bytes(section);
            foreach (var hit in pattern.FindAll(bytes))
            {
                var start = hit - slots[0].Slot * 8;
                if (start < 0 || start + (maxSlot + 1) * 8 > bytes.Length || ((section.VirtualAddress + start) & 7) != 0)
                    continue;
                if (!slots.All(slot => BitConverter.ToUInt64(bytes, start + slot.Slot * 8) == slot.Address)) continue;
                var first = BitConverter.ToUInt64(bytes, start);
                if (first < _imageBase || !_sections.Any(code => code.Executable && first - _imageBase >= code.VirtualAddress &&
                        first - _imageBase < (ulong)code.VirtualAddress + code.RawSize)) continue;
                candidates.Add(section.VirtualAddress + (ulong)start);
                if (candidates.Count > 1) throw new InvalidDataException($"Ambiguous vtable fingerprint: {label}.");
            }
        }
        if (candidates.Count != 1) throw new InvalidDataException($"Missing vtable fingerprint: {label}.");
        return candidates[0];
    }

    public ulong FindRttiName(string name)
    {
        var pattern = SignaturePattern.Parse(string.Join(" ", System.Text.Encoding.ASCII.GetBytes(name + '\0')
            .Select(value => value.ToString("X2"))));
        var matches = new List<ulong>();
        // MSVC stores TypeDescriptor names in writable .data on the current build.
        foreach (var section in _sections.Where(s => s.Readable && !s.Executable))
            foreach (var offset in pattern.FindAll(Bytes(section)))
            {
                matches.Add(section.VirtualAddress + (ulong)offset);
                if (matches.Count > 1) throw new InvalidDataException($"Ambiguous RTTI name: {name}.");
            }
        if (matches.Count != 1) throw new InvalidDataException($"Missing RTTI name: {name}.");
        return matches[0];
    }

    public string InspectExactVtable(ulong rva)
    {
        if (rva < 8 || (rva & 7) != 0 || !IsData(rva - 8, 16, writable: false))
            throw new InvalidDataException("Vtable is not aligned read-only image data.");
        var first = BitConverter.ToUInt64(ReadRva(rva, 8));
        if (first < _imageBase || !_sections.Any(s => s.Executable && first - _imageBase >= s.VirtualAddress &&
                first - _imageBase < (ulong)s.VirtualAddress + s.RawSize))
            throw new InvalidDataException("Vtable first slot is not executable image code.");
        var locatorVa = BitConverter.ToUInt64(ReadRva(rva - 8, 8));
        if (locatorVa < _imageBase || !IsData(locatorVa - _imageBase, 24, writable: false))
            return "Aligned read-only table with an executable first slot; no usable MSVC RTTI locator.";
        var locator = ReadRva(locatorVa - _imageBase, 24);
        if (BitConverter.ToUInt32(locator, 0) != 1 || BitConverter.ToUInt32(locator, 20) != locatorVa - _imageBase)
            throw new InvalidDataException("Vtable RTTI locator self-reference disagrees.");
        var typeRva = (ulong)BitConverter.ToUInt32(locator, 12);
        // MSVC TypeDescriptors may be in writable .data, unlike the locator/table.
        var typeName = ReadRva(typeRva + 16, 160);
        var end = Array.IndexOf(typeName, (byte)0);
        if (end is < 5 or > 150 || typeName.Take(end).Any(b => b is < 32 or > 126))
            throw new InvalidDataException("Vtable RTTI type name is invalid.");
        return "Aligned read-only table with an executable first slot and RTTI " + System.Text.Encoding.ASCII.GetString(typeName, 0, end);
    }

    private byte[] ReadRva(ulong rva, int size)
    {
        var section = _sections.SingleOrDefault(section => rva >= section.VirtualAddress &&
            rva + (ulong)size <= (ulong)section.VirtualAddress + section.RawSize)
            ?? throw new InvalidDataException("RVA is not file-backed.");
        return Bytes(section).AsSpan(checked((int)(rva - section.VirtualAddress)), size).ToArray();
    }

    private bool IsExecutable(ulong rva) => _sections.Any(s => s.Executable &&
        rva >= s.VirtualAddress && rva < (ulong)s.VirtualAddress + s.RawSize);

    private bool IsData(ulong rva, int size, bool writable) => _sections.Any(section =>
        section.Readable && !section.Executable && section.Writable == writable &&
        rva >= section.VirtualAddress && rva + (ulong)size <= (ulong)section.VirtualAddress + Math.Max(section.VirtualSize, section.RawSize));
}
