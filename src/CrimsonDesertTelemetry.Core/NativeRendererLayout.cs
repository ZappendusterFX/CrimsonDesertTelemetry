namespace CrimsonDesertTelemetry.Core;

internal static class NativeRendererLayout
{
    private const string BankPattern = "41 8B 85 ?? ?? ?? ?? 4D 8B BC C5 ?? ?? ?? ?? 4C 89 7C 24 58 49 8B 8C C5 ?? ?? ?? ??";
    private const string InputPattern = "49 8B B5 ?? ?? ?? ?? 48 8B 5C 24 50";

    internal static string ContextPattern(int index, string bytes)
    {
        if (index is not (3 or 5)) return bytes;
        var tokens = bytes.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        foreach (var start in index == 3 ? new[] { 3, 11, 24 } : new[] { 3 })
            for (var at = start; at < start + 4; at++) tokens[at] = "??";
        return string.Join(' ', tokens);
    }

    internal static int Offset(string bytes, int at) =>
        BitConverter.ToInt32(Convert.FromHexString(bytes.Replace(" ", "")), at);

    internal static int Validate(NativeCaptureDefinition native)
    {
        if (native.ContextSignatures.Count != 7)
            throw new InvalidDataException("Renderer layout needs the complete context family.");
        var bankBytes = Convert.FromHexString(native.ContextSignatures[3].Bytes.Replace(" ", ""));
        var inputBytes = Convert.FromHexString(native.ContextSignatures[5].Bytes.Replace(" ", ""));
        if (bankBytes.Length != 28 || inputBytes.Length != 12)
            throw new InvalidDataException("Renderer field contexts have unexpected lengths.");
        var bank = Offset(native.ContextSignatures[3].Bytes, 3);
        var input = Offset(native.ContextSignatures[5].Bytes, 3);
        var shift = (long)bank - 0x8E0;
        if (!SignaturePattern.Parse(BankPattern).FindAll(bankBytes).SequenceEqual([0]) ||
            !SignaturePattern.Parse(InputPattern).FindAll(inputBytes).SequenceEqual([0]) ||
            Math.Abs(shift) > 0x100 || shift % 8 != 0 ||
            native.Wrapper.OwnerBankIndexOffset != bank || bank - input != 0x2B8 ||
            Offset(native.ContextSignatures[3].Bytes, 11) != input + 0x10 ||
            Offset(native.ContextSignatures[3].Bytes, 24) != input + 0x20)
            throw new InvalidDataException("Renderer buffer fields do not form the guarded layout.");
        return checked((int)shift);
    }
}
