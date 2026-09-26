using System.Diagnostics;
using System.IO.Compression;
using System.Net.WebSockets;
using System.Text.Json;

// Read-only client. Compile BEFORE the owner's go; no HTTP preflight or second feed.
// Every received snapshot is retained, but authored lights/ambient/RGB are omitted.
// Usage: VisibilityRecorder.exe [seconds=20] [--trace]
// --trace signals the matching plugin's bounded native context trace, no added rays.
// Offline check: VisibilityRecorder.exe --replay existing-raw.jsonl
internal static class Program
{
    private static readonly string[] RootFields =
        ["schemaVersion", "sequence", "capturedAt", "game", "player", "camera", "quality"];
    private static readonly string[] LightFields =
        ["sampleIndex", "position", "kind", "rendererSelected", "renderedSampleIndex", "groupMemberCount", "sourceVisibility"];

    public static async Task<int> Main(string[] args)
    {
        var nativeTrace = args.Contains("--trace");
        args = args.Where(a => a != "--trace").ToArray();
        var replay = args.Length == 2 && args[0] == "--replay";
        if (nativeTrace && replay) throw new ArgumentException("Offline replay must not trigger the game");
        var seconds = args.Length == 0 || replay ? 20 : int.Parse(args[0]);
        if (!replay && (args.Length > 1 || seconds is < 1 or > 30))
            throw new ArgumentException("Expected duration 1..30 seconds, or --replay raw.jsonl");
        var directory = Path.GetFullPath(Path.Combine("artifacts", "light-research",
            $"visibility-{(replay ? "offline" : "motion")}-{DateTime.Now:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}"));
        Directory.CreateDirectory(directory);
        var timer = Stopwatch.StartNew();
        long frames = 0, inputBytes = 0;
        string? error = null;
        string result = replay ? "offline-replay" : "duration-complete";
        DateTimeOffset? connectedAt = null, firstFrameAt = null, lastFrameAt = null;
        int? nativeTracePid = null;
        try
        {
            using var file = new FileStream(Path.Combine(directory, "visibility.jsonl.gz"), FileMode.CreateNew);
            using var gzip = new GZipStream(file, CompressionLevel.Fastest);
            void Save(ReadOnlyMemory<byte> bytes)
            {
                var received = DateTimeOffset.UtcNow;
                using var doc = JsonDocument.Parse(bytes);
                using (var writer = new Utf8JsonWriter(gzip))
                {
                    writer.WriteStartObject();
                    writer.WriteString("receivedAt", received);
                    writer.WriteNumber("elapsedMs", timer.Elapsed.TotalMilliseconds);
                    writer.WriteString("evidence", replay ? "offline-replay" : "live");
                    foreach (var key in RootFields) Copy(writer, doc.RootElement, key);
                    if (doc.RootElement.TryGetProperty("lights", out var lights))
                    {
                        writer.WriteStartObject("lights");
                        foreach (var name in new[] { "upstream", "rendered" })
                        {
                            if (!lights.TryGetProperty(name, out var stream)) continue;
                            writer.WritePropertyName(name);
                            if (stream.ValueKind != JsonValueKind.Object) { stream.WriteTo(writer); continue; }
                            writer.WriteStartObject();
                            foreach (var property in stream.EnumerateObject())
                            {
                                if (property.Name != "sources" || property.Value.ValueKind != JsonValueKind.Array)
                                { property.WriteTo(writer); continue; }
                                writer.WriteStartArray("sources");
                                foreach (var source in property.Value.EnumerateArray())
                                {
                                    writer.WriteStartObject();
                                    foreach (var key in LightFields) Copy(writer, source, key);
                                    writer.WriteEndObject();
                                }
                                writer.WriteEndArray();
                            }
                            writer.WriteEndObject();
                        }
                        writer.WriteEndObject();
                    }
                    writer.WriteEndObject();
                }
                gzip.WriteByte((byte)'\n');
                firstFrameAt ??= received; lastFrameAt = received; frames++;
            }
            if (replay)
            {
                foreach (var line in File.ReadLines(args[1])) Save(System.Text.Encoding.UTF8.GetBytes(line));
            }
            else
            {
                using var socket = new ClientWebSocket();
                using var connectTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                await socket.ConnectAsync(new Uri("ws://127.0.0.1:27311/v1/stream"), connectTimeout.Token);
                connectedAt = DateTimeOffset.UtcNow;
                if (nativeTrace)
                {
                    if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
                    var games = Process.GetProcessesByName("CrimsonDesert");
                    try
                    {
                        if (games.Length != 1) throw new IOException("Exactly one game process is required for native trace");
                        using var trigger = EventWaitHandle.OpenExisting(
                            $"Local\\CrimsonDesertTelemetry.VisibilityContextTrace.{games[0].Id}");
                        if (!trigger.Set()) throw new IOException("Could not signal native trace");
                        nativeTracePid = games[0].Id;
                    }
                    finally { foreach (var game in games) game.Dispose(); }
                }
                Console.WriteLine($"RECORDING {seconds}s -> {directory}");
                using var duration = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
                var buffer = new byte[65536];
                try
                {
                    while (!duration.IsCancellationRequested)
                    {
                        using var message = new MemoryStream();
                        WebSocketReceiveResult part;
                        do
                        {
                            part = await socket.ReceiveAsync(new ArraySegment<byte>(buffer), duration.Token);
                            if (part.MessageType != WebSocketMessageType.Text)
                                throw new IOException($"Stream ended or sent non-text: {part.MessageType}");
                            message.Write(buffer, 0, part.Count); inputBytes += part.Count;
                            if (message.Length > 8 * 1024 * 1024 || inputBytes > 1024L * 1024 * 1024)
                                throw new IOException("Recorder safety bound exceeded");
                        } while (!part.EndOfMessage);
                        Save(message.GetBuffer().AsMemory(0, (int)message.Length));
                    }
                }
                catch (OperationCanceledException) when (duration.IsCancellationRequested) { }
                finally { socket.Abort(); }
            }
        }
        catch (Exception exception) { result = "failed"; error = exception.Message; }
        var summary = JsonSerializer.Serialize(new { result, error, directory, seconds, frames, inputBytes,
            connectedAt, firstFrameAt, lastFrameAt, nativeTracePid, elapsedMs = timer.Elapsed.TotalMilliseconds });
        File.WriteAllText(Path.Combine(directory, "summary.json"), summary);
        Console.WriteLine(summary);
        return error is null && frames > 0 ? 0 : 1;
    }

    private static void Copy(Utf8JsonWriter writer, JsonElement source, string key)
    {
        if (!source.TryGetProperty(key, out var value)) return;
        writer.WritePropertyName(key); value.WriteTo(writer);
    }
}
