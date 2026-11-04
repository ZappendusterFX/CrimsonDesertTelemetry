using System.Text;
using System.Text.Json;

namespace CrimsonDesertTelemetry.Core;

/// <summary>Consumes an explicit test request, never a build/profile authorization.</summary>
public static class RecoveryDiagnosticTest
{
    public static string NormalizeToken(string token)
    {
        if (token.Length != 32 || token.Any(c => !char.IsAsciiHexDigit(c)) || token.All(c => c == '0'))
            throw new InvalidDataException("Recovery test token must contain 32 hexadecimal digits and cannot be all zero.");
        return token.ToUpperInvariant();
    }

    public static string MarkerPath(string token, string? localAppData = null)
    {
        // Match native GetEnvironmentVariableW exactly, including a redirected
        // profile. Both processes must inspect/consume the same one-shot marker.
        var root = localAppData ?? Environment.GetEnvironmentVariable("LOCALAPPDATA");
        if (string.IsNullOrWhiteSpace(root) || !Path.IsPathFullyQualified(root))
            throw new InvalidDataException("Local application data is unavailable; the one-time recovery test cannot be recorded.");
        return Path.Combine(root, "CrimsonDesertTelemetry", "RecoveryTests", NormalizeToken(token) + ".done");
    }

    /// <summary>Call after world readiness and immediately before the full cold search.</summary>
    public static void Consume(string token, int processId, long processStartFileTime,
        string executableSha256, string? localAppData = null)
    {
        var path = MarkerPath(token, localAppData);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        // CREATE_NEW is the authority: a second host/process cannot consume the
        // same request. Even a later scan failure/crash must not arm it again.
        using var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        var receipt = JsonSerializer.Serialize(new
        {
            state = "attempt-started", token = NormalizeToken(token), processId, processStartFileTime,
            executableSha256, startedAt = DateTimeOffset.UtcNow,
            detail = "Consumed before full search; this marker does not claim successful recovery."
        });
        stream.Write(Encoding.UTF8.GetBytes(receipt));
        stream.Flush(flushToDisk: true);
    }
}
