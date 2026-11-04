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
    private readonly uint _exceptionRva, _exceptionSize;
    private byte[]? _runtimeFunctions;

    private CompatibilityImage(string path, IReadOnlyList<PeSectionInfo> sections, ulong imageBase,
        uint exceptionRva, uint exceptionSize)
    { _path = path; _sections = sections; _imageBase = imageBase; _exceptionRva = exceptionRva; _exceptionSize = exceptionSize; }

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
            uint exceptionRva = 0, exceptionSize = 0;
            if (optionalSize >= 112 + 4 * 8)
            {
                stream.Position = pe + 24 + 108;
                if (reader.ReadUInt32() >= 4)
                {
                    stream.Position = pe + 24 + 112 + 3 * 8;
                    exceptionRva = reader.ReadUInt32(); exceptionSize = reader.ReadUInt32();
                }
            }
            return new CompatibilityImage(path, sections, imageBase, exceptionRva, exceptionSize);
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

    internal byte[] ReadCode(ulong rva, int size)
    {
        if (size is < 1 or > 32768 || !_sections.Any(s => s.Executable && rva >= s.VirtualAddress &&
                rva + (ulong)size <= (ulong)s.VirtualAddress + s.RawSize))
            throw new InvalidDataException("Code range is outside the executable image.");
        return ReadRva(rva, size);
    }

    internal (ulong Instruction, ulong Target) CheckDataReferenceAt(ulong rva, PatternDefinition pattern, int size)
    {
        if (!MatchesFunctionAt(rva, pattern.Pattern) || pattern.RipOffset is not int displacement ||
            pattern.RipEnd is not int end || displacement < 0 || displacement + 4 > SignaturePattern.Parse(pattern.Pattern).Length ||
            end < displacement + 4 || end > SignaturePattern.Parse(pattern.Pattern).Length)
            throw new InvalidDataException("Cached data-reference instruction does not match its template.");
        var target = checked((long)rva + end + BitConverter.ToInt32(ReadRva(rva + (ulong)displacement, 4)));
        if (target < 0 || (target & 3) != 0 || !IsData((ulong)target, size, writable: true))
            throw new InvalidDataException("Cached RIP target is not writable game data.");
        return (rva, (ulong)target);
    }

    internal bool MatchesVtableAt(ulong rva, IReadOnlyList<VtableSlotFingerprint> fingerprints)
    {
        if (fingerprints.Count < 2 || (rva & 7) != 0 ||
            !IsData(rva, checked((fingerprints.Max(f => f.Slot) + 1) * 8), writable: false)) return false;
        foreach (var fingerprint in fingerprints)
        {
            var address = BitConverter.ToUInt64(ReadRva(rva + (ulong)fingerprint.Slot * 8, 8));
            if (address < _imageBase || !MatchesFunctionAt(address - _imageBase, fingerprint.Pattern)) return false;
        }
        return true;
    }

    internal bool MatchesRttiAt(ulong rva, string name)
    {
        var bytes = System.Text.Encoding.ASCII.GetBytes(name + '\0');
        if (!_sections.Any(s => s.Readable && !s.Executable && rva >= s.VirtualAddress &&
                rva + (ulong)bytes.Length <= (ulong)s.VirtualAddress + s.RawSize)) return false;
        return ReadRva(rva, bytes.Length).AsSpan().SequenceEqual(bytes);
    }

    // Natural ray/shape prologues occur in several functions. Their bounded
    // unwind function and the engine's own profiler label establish the role.
    // A first matching prologue must never select a physics entry point.
    internal ulong FindNamedFunction(string pattern, string name, bool atEntry = true)
    {
        var matches = new List<ulong>();
        foreach (var section in _sections.Where(s => s.Executable && s.RawSize != 0))
            foreach (var offset in SignaturePattern.Parse(pattern).FindAll(Bytes(section)))
            {
                var rva = section.VirtualAddress + (ulong)offset;
                if (MatchesNamedFunctionAt(rva, pattern, name, atEntry)) matches.Add(rva);
                if (matches.Count > 1) throw new InvalidDataException("Ambiguous named physics function: " + name);
            }
        return matches.Count == 1 ? matches[0] : throw new InvalidDataException("Missing named physics function: " + name);
    }

    internal bool MatchesNamedFunctionAt(ulong rva, string pattern, string name, bool atEntry = true)
    {
        if (!MatchesFunctionAt(rva, pattern)) return false;
        var entries = RuntimeFunctions();
        for (var at = 0; at + 12 <= entries.Length; at += 12)
        {
            var start = BitConverter.ToUInt32(entries, at);
            var end = BitConverter.ToUInt32(entries, at + 4);
            if (atEntry ? start != rva : start > rva || rva >= end) continue;
            if (end <= start || end - start > 32768) return false;
            var code = ReadCode(start, checked((int)(end - start)));
            // Entry-point roles have their profiler reference near entry. The
            // ambient/exposure context roles may register it later in the same
            // bounded unwind function. Neither route executes candidate code.
            for (var index = 0; index + 7 <= (atEntry ? Math.Min(code.Length, 512) : code.Length); index++)
            {
                if (code[index] is < 0x48 or > 0x4F || code[index + 1] != 0x8D || (code[index + 2] & 0xC7) != 0x05) continue;
                var target = checked((long)start + index + 7 + BitConverter.ToInt32(code, index + 3));
                if (target >= 0 && MatchesRttiAt((ulong)target, name)) return true;
            }
            return false;
        }
        return false;
    }

    internal bool SameUnwindFunction(ulong first, ulong second, bool firstIsEntry = false)
    {
        var entries = RuntimeFunctions();
        for (var at = 0; at + 12 <= entries.Length; at += 12)
        {
            var start = BitConverter.ToUInt32(entries, at);
            var end = BitConverter.ToUInt32(entries, at + 4);
            if (start <= first && first < end && start <= second && second < end &&
                end - start <= 32768 && (!firstIsEntry || start == first)) return true;
        }
        return false;
    }

    private byte[] RuntimeFunctions()
    {
        // Packed game images rename sections; use PE's exception directory, not
        // the conventional .pdata name. The table itself must be read-only data.
        if (_runtimeFunctions is not null) return _runtimeFunctions;
        if (_exceptionRva == 0 || _exceptionSize is < 12 or > 16 * 1024 * 1024 || _exceptionSize % 12 != 0 ||
            !IsData(_exceptionRva, checked((int)_exceptionSize), writable: false))
            throw new InvalidDataException("Missing or malformed AMD64 exception directory.");
        return _runtimeFunctions = ReadRva(_exceptionRva, checked((int)_exceptionSize));
    }

    internal bool MatchesCollectorVtableAt(ulong rva, ulong method)
    {
        if ((rva & 7) != 0 || !IsData(rva, 96, writable: false)) return false;
        var bytes = ReadRva(rva, 96);
        var slots = Enumerable.Range(0, 12).Select(i => BitConverter.ToUInt64(bytes, i * 8)).ToArray();
        return slots[5] == _imageBase + method && slots[0] == slots[9] && slots[3] == slots[7] && slots[6] == slots[8] &&
            slots.All(p => p >= _imageBase && IsExecutable(p - _imageBase));
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
