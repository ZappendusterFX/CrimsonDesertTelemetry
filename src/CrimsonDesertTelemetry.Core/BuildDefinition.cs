using System.Text.Json;
using System.Text.Json.Serialization;

namespace CrimsonDesertTelemetry.Core;

public sealed class BuildDefinition
{
    public int SchemaVersion { get; set; }
    public string SteamAppId { get; set; } = "";
    public string SteamBuildId { get; set; } = "";
    public string ExecutableVersion { get; set; } = "";
    public string ExecutableSha256 { get; set; } = "";
    public string Status { get; set; } = "research";
    public List<PatternDefinition> Patterns { get; set; } = [];
    public PlayerRootDefinition? PlayerRoot { get; set; }
    public EngineCameraDefinition? EngineCamera { get; set; }
    public EngineLightsDefinition? EngineLights { get; set; }
    public NativeCaptureDefinition? NativeCapture { get; set; }
    public bool AllowAutomaticCompatibility { get; set; }

    public static IReadOnlyList<BuildDefinition> LoadAll(string directory)
    {
        var options = new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true,
            UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
            MaxDepth = 24
        };
        var definitions = new Dictionary<string, BuildDefinition>(StringComparer.OrdinalIgnoreCase);
        var assembly = typeof(BuildDefinition).Assembly;
        foreach (var resourceName in assembly.GetManifestResourceNames()
                     .Where(static name => name.Contains(".definitions.build-", StringComparison.OrdinalIgnoreCase) &&
                                           name.EndsWith(".json", StringComparison.OrdinalIgnoreCase)))
        {
            using var stream = assembly.GetManifestResourceStream(resourceName)
                               ?? throw new InvalidDataException($"Missing embedded build definition: {resourceName}");
            var definition = ReadDefinition(stream, resourceName, options);
            BuildProfileValidation.Validate(definition);
            if (definitions.ContainsKey(definition.ExecutableSha256))
                throw new InvalidDataException($"Duplicate embedded build hash: {resourceName}");
            definitions[definition.ExecutableSha256] = definition;
        }

        if (Directory.Exists(directory))
        {
            var externalHashes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var path in Directory.EnumerateFiles(directory, "build-*.json"))
            {
                if (new FileInfo(path).Length > 256 * 1024)
                    throw new InvalidDataException($"Build definition is too large: {path}");
                using var stream = File.OpenRead(path);
                var definition = ReadDefinition(stream, path, options);
                BuildProfileValidation.Validate(definition);
                if (!externalHashes.Add(definition.ExecutableSha256))
                    throw new InvalidDataException($"Duplicate external build hash: {path}");
                definitions[definition.ExecutableSha256] = definition;
            }
        }
        var result = definitions.Values.ToList();
        BuildProfileValidation.ValidateAll(result);
        return result;
    }

    private static BuildDefinition ReadDefinition(Stream stream, string source, JsonSerializerOptions options)
    {
        try
        {
            using var document = JsonDocument.Parse(stream, new JsonDocumentOptions { MaxDepth = 24 });
            RejectDuplicateProperties(document.RootElement);
            return document.RootElement.Deserialize<BuildDefinition>(options)
                ?? throw new InvalidDataException($"Empty build definition: {source}");
        }
        catch (JsonException exception) { throw new InvalidDataException($"Malformed build definition: {source}", exception); }
    }

    private static void RejectDuplicateProperties(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var property in value.EnumerateObject())
            {
                if (!names.Add(property.Name)) throw new InvalidDataException("Duplicate build-profile property: " + property.Name);
                RejectDuplicateProperties(property.Value);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array)
            foreach (var child in value.EnumerateArray()) RejectDuplicateProperties(child);
    }
}

public sealed class EngineLightsDefinition
{
    public string Layout { get; set; } = "";
    public ulong RootGlobalRva { get; set; }
    public ulong SceneGlobalRva { get; set; }
    public ulong SceneVtableRva { get; set; }
    public int ArrayPointerOffset { get; set; } = 0xF08;
}

public sealed class EngineCameraDefinition
{
    public string Layout { get; set; } = "";
    public ulong MainRootReferenceRva { get; set; }
    public string MainRootReferencePattern { get; set; } = "";
    public ulong MainRootGlobalRva { get; set; }
    public ulong CameraReferenceRva { get; set; }
    public string CameraReferencePattern { get; set; } = "";
    public ulong CameraGlobalRva { get; set; }
    public ulong ContextVtableRva { get; set; }
    public ulong CameraVtableRva { get; set; }
    public int FrameCounterOffset { get; set; }
    public int SourcePointerOffset { get; set; } = 0x428;
    public List<VtableSlotFingerprint> ContextVtableFingerprints { get; set; } = [];
    public List<VtableSlotFingerprint> CameraVtableFingerprints { get; set; } = [];
}

public sealed class VtableSlotFingerprint
{
    public int Slot { get; set; }
    public string Pattern { get; set; } = "";
}

public sealed class PlayerRootDefinition
{
    public string TransformLayout { get; set; } = "basis-v1";
    public PatternDefinition WorldSystemPattern { get; set; } = new();
    [JsonRequired] public int WorldSystemToActorManagerOffset { get; set; } = 0x30;
    [JsonRequired] public int ActorManagerToPlayerActorOffset { get; set; } = 0x50;
    [JsonRequired] public int PlayerActorToIntermediateOffset { get; set; } = 0x68;
    [JsonRequired] public int IntermediateToControlOffset { get; set; } = 0x40;
    [JsonRequired] public int ControlToOwnerOffset { get; set; } = 0x140;
    [JsonRequired] public int OwnerToPhysicsOffset { get; set; } = 0x298;
    [JsonRequired] public int BasisXOffset { get; set; } = 0x60;
    [JsonRequired] public int BasisYOffset { get; set; } = 0x70;
    [JsonRequired] public int BasisZOffset { get; set; } = 0x80;
    [JsonRequired] public int PositionOffset { get; set; } = 0x90;
    public int QuaternionOffset { get; set; }
    public List<string> ExpectedTypeNames { get; set; } = [];
}

public sealed class PatternDefinition
{
    public string Name { get; set; } = "";
    public string Purpose { get; set; } = "";
    public string Section { get; set; } = ".text";
    public string Pattern { get; set; } = "";
    public int? RipOffset { get; set; }
    public int? RipEnd { get; set; }
    public string Confidence { get; set; } = "candidate";
    public string? Source { get; set; }
}

public sealed class NativeCaptureDefinition
{
    public int SchemaVersion { get; set; }
    public string ContractId { get; set; } = "";
    public string Status { get; set; } = "";
    public bool RequiresExactExecutable { get; set; }
    public uint SkyProducerRva { get; set; } = 0x384BD77;
    public ulong HookRva { get; set; }
    public string HookSignature { get; set; } = "";
    public List<NativeContextSignature> ContextSignatures { get; set; } = [];
    public NativeCaptureRegisters Registers { get; set; } = new();
    public NativeCaptureWrapper Wrapper { get; set; } = new();
    public NativeCaptureScene Scene { get; set; } = new();
    public NativeCaptureGpu Gpu { get; set; } = new();
    public NativeShaderEvidence? ShaderEvidence { get; set; }
}
public sealed class NativeContextSignature
{
    public ulong Rva { get; set; }
    public string Bytes { get; set; } = "";
    public string Purpose { get; set; } = "";
}
public sealed class NativeCaptureRegisters
{
    public string Output { get; set; } = "";
    public string Command { get; set; } = "";
    public string Counter { get; set; } = "";
    public string Owner { get; set; } = "";
}
public sealed class NativeCaptureWrapper
{
    public int InnerOffset { get; set; }
    public int StrideOffset { get; set; }
    public int CountOffset { get; set; }
    public int ResourceOffset { get; set; }
    public int CommandHolderOffset { get; set; }
    public int NativeListOffset { get; set; }
    public int OwnerBankIndexOffset { get; set; }
}
public sealed class NativeCaptureScene
{
    public int RootPointerOffset { get; set; }
    public int ByteCount { get; set; }
    public int FrameOffset { get; set; }
    public int ScreenOffset { get; set; }
    public int PositionOffset { get; set; }
    public int DirectionOffset { get; set; }
    public int ViewRelativeOffset { get; set; }
    public int EarthRadiusOffset { get; set; }
    public float EarthRadius { get; set; }
}
public sealed class NativeCaptureGpu
{
    public int RecordCount { get; set; }
    public int RecordStride { get; set; }
    public int CounterBytes { get; set; }
    public int ValidCounterByteOffset { get; set; }
    public string ResourceState { get; set; } = "";
}
public sealed class NativeShaderEvidence
{
    public string ProducerEntry { get; set; } = "";
    public List<string> ConsumerEntries { get; set; } = [];
    public string ProducerDxilHash { get; set; } = "";
    public string Source { get; set; } = "";
    public string Validation { get; set; } = "";
}
