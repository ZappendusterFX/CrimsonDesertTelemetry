using System.IO.MemoryMappedFiles;
using System.Text;
using System.Text.Json;

namespace CrimsonDesertTelemetry.Core;

public sealed record NativeRecoveryRequest(uint State, string TemplateSha256, string ExecutableSha256,
    string ContractId, uint RequestedFeatures, string? DiagnosticToken = null)
{
    public bool DiagnosticTest => DiagnosticToken is not null;
}

/// <summary>Per-process, one-shot handoff to the ASI. This never writes game process memory.</summary>
public sealed class NativeRecoveryBridge : IDisposable
{
    public const int Capacity = 65536, HeaderBytes = 256;
    public const uint Magic = 0x52544443, Version = 1;
    public const uint DiagnosticVersion = 2;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private readonly int _pid;
    private readonly long _start;

    private NativeRecoveryBridge(MemoryMappedFile mapping, int pid, long start)
    {
        _mapping = mapping;
        _view = mapping.CreateViewAccessor(0, Capacity, MemoryMappedFileAccess.ReadWrite);
        _pid = pid; _start = start;
    }

    public static string Name(int processId) => $"Local\\CrimsonDesertTelemetry.Recovery.{processId}";

    public static NativeRecoveryBridge? TryOpen(int processId, long processStartFileTime)
    {
        MemoryMappedFile? mapping = null;
        try
        {
            mapping = MemoryMappedFile.OpenExisting(Name(processId), MemoryMappedFileRights.ReadWrite);
            return new NativeRecoveryBridge(mapping, processId, processStartFileTime);
        }
        catch (Exception e) when (e is FileNotFoundException or UnauthorizedAccessException or IOException or ArgumentException)
        { mapping?.Dispose(); return null; }
    }

    public NativeRecoveryRequest Read()
    {
        var bytes = new byte[HeaderBytes];
        var second = new byte[HeaderBytes];
        for (var attempt = 0; attempt < 3; attempt++)
        {
            _view.ReadArray(0, bytes, 0, bytes.Length);
            Thread.MemoryBarrier();
            _view.ReadArray(0, second, 0, second.Length);
            if (bytes.AsSpan().SequenceEqual(second)) return DecodeHeader(bytes, _pid, _start);
        }
        throw new InvalidDataException("Recovery handshake changed during all three bounded reads.");
    }

    public static NativeRecoveryRequest DecodeHeader(byte[] bytes, int pid, long start)
    {
        if (bytes.Length != HeaderBytes || BitConverter.ToUInt32(bytes, 0) != Magic ||
            BitConverter.ToUInt32(bytes, 4) is not (Version or DiagnosticVersion) || BitConverter.ToUInt32(bytes, 8) != Capacity ||
            BitConverter.ToUInt32(bytes, 20) != (uint)pid || BitConverter.ToInt64(bytes, 24) != start ||
            BitConverter.ToUInt32(bytes, 16) > Capacity - HeaderBytes)
            throw new InvalidDataException("Recovery handshake identity or version disagrees.");
        var state = BitConverter.ToUInt32(bytes, 12);
        if (state > 5) throw new InvalidDataException("Unknown recovery handshake state.");
        var features = BitConverter.ToUInt32(bytes, 224);
        if ((features & ~15u) != 0) throw new InvalidDataException("Unknown recovery feature flags.");
        var diagnostic = BitConverter.ToUInt32(bytes, 232);
        var tokenBytes = bytes.AsSpan(240, 16);
        var hasToken = tokenBytes.ContainsAnyExcept((byte)0);
        var version = BitConverter.ToUInt32(bytes, 4);
        if (diagnostic > 1 || diagnostic == 0 && (hasToken || version != Version) ||
            diagnostic == 1 && (!hasToken || version != DiagnosticVersion || state == 5))
            throw new InvalidDataException("Recovery test flag, token and protocol version disagree.");
        string Hash(int offset)
        {
            var value = Encoding.ASCII.GetString(bytes, offset, 64);
            if (value.Any(c => !Uri.IsHexDigit(c))) throw new InvalidDataException("Invalid recovery executable hash.");
            return value;
        }
        var contractBytes = bytes.AsSpan(160, 64);
        var zero = contractBytes.IndexOf((byte)0);
        if (zero <= 0 || contractBytes[..zero].ContainsAnyExceptInRange((byte)32, (byte)126))
            throw new InvalidDataException("Invalid recovery contract identity.");
        return new(state, Hash(32), Hash(96), Encoding.ASCII.GetString(contractBytes[..zero]), features,
            diagnostic == 1 ? Convert.ToHexString(tokenBytes) : null);
    }

    public void Submit(BuildDefinition definition, NativeRecoveryRequest request)
    {
        var native = definition.NativeCapture ?? throw new InvalidDataException("Missing native recovery contract.");
        var support = native.Support ?? throw new InvalidDataException("Missing full-product recovery anchors.");
        if (!string.Equals(definition.ExecutableSha256, request.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Recovered file identity differs from the loaded game.");
        var payload = JsonSerializer.SerializeToUtf8Bytes(new
        {
            processId = _pid, processStartFileTime = _start,
            executableSha256 = definition.ExecutableSha256, templateSha256 = request.TemplateSha256,
            contractId = request.ContractId,
            sceneGlobalRva = definition.EngineCamera!.CameraGlobalRva,
            sceneVtableRva = definition.EngineCamera.CameraVtableRva,
            hookRva = native.HookRva, hookSignature = native.HookSignature,
            skyProducerRva = native.SkyProducerRva,
            contexts = native.ContextSignatures.Select(c => new { rva = c.Rva, bytes = c.Bytes }),
            support = new { values = support.Values,
                anchors = support.Anchors.Select(a => new { name = a.Name, rva = a.Rva, bytes = a.Bytes }) }
        });
        Publish(payload, 2);
    }

    public void Acknowledge()
    {
        if (Read().State is not (1 or 2 or 3)) return;
        Thread.MemoryBarrier();
        _view.Write(228, 1u);
    }

    public void Fail(string reason)
    {
        if (Read().State != 1) return;
        Publish(JsonSerializer.SerializeToUtf8Bytes(new { error = reason[..Math.Min(reason.Length, 768)] }), 4);
    }

    private void Publish(byte[] payload, uint state)
    {
        if (payload.Length is 0 or > Capacity - HeaderBytes || Read().State != 1)
            throw new InvalidDataException("Recovery handoff is oversized or no longer pending.");
        _view.WriteArray(HeaderBytes, payload, 0, payload.Length);
        _view.Write(16, (uint)payload.Length);
        Thread.MemoryBarrier();
        _view.Write(12, state);
    }

    public void Dispose() { _view.Dispose(); _mapping.Dispose(); }
}
