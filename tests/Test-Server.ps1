param(
    [string]$Executable,
    [int]$Port = 27312
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $PSScriptRoot '..\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.exe'
}
if (-not (Test-Path -LiteralPath $Executable)) { throw "Server executable is missing: $Executable" }

$server = Start-Process -FilePath $Executable -ArgumentList 'serve', $Port, 120 -PassThru -WindowStyle Hidden
try {
    $health = $null
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/health" -TimeoutSec 1
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if ($null -eq $health) { throw 'Health endpoint did not become ready.' }
    if ($health.status -notin @('waiting-for-game', 'loading', 'discovering', 'playing') -or $health.sampleRateHz -ne 120) {
        throw "Unexpected health response: $($health | ConvertTo-Json -Compress)"
    }
    if (-not $health.gameRunning -and $null -ne $health.supportedBuild) {
        throw 'Build support must be unknown while the game is absent.'
    }
    if (-not ($health.PSObject.Properties.Name -contains 'compatibility')) {
        throw 'Health response is missing compatibility metadata.'
    }
    if (-not $health.gameRunning -and $null -ne $health.compatibility) {
        throw 'Compatibility must be unknown while the game is absent.'
    }

    $localOrigin = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/v1/health" -Headers @{ Origin = 'http://127.0.0.1:8080' }
    if ($localOrigin.Headers['Access-Control-Allow-Origin'] -ne 'http://127.0.0.1:8080') {
        throw 'Loopback browser origin was not allowed.'
    }
    $remoteOrigin = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/v1/health" -Headers @{ Origin = 'https://example.com' }
    if ($remoteOrigin.Headers.ContainsKey('Access-Control-Allow-Origin')) {
        throw 'Remote browser origin was allowed.'
    }

    $schema = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/schema"
    if ($schema.title -ne 'Crimson Desert Telemetry snapshot v1') { throw 'Schema endpoint mismatch.' }

    $smoothed = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/lights/smoothed"
    if ($smoothed.schemaVersion -ne '1.0' -or $smoothed.status -ne 'unavailable' -or
        $null -ne $smoothed.sources -or $smoothed.settings.timeConstantMilliseconds -ne 200) {
        throw 'Server without --lights must expose unavailable derived data, not fabricate lights.'
    }
    $visible = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/lights/visible"
    if ($visible.schemaVersion -ne '1.0' -or $visible.status -ne 'unavailable' -or
        $null -ne $visible.sources -or $visible.coverage -notlike '*visibility-clear-only') {
        throw 'Visible endpoint must fail closed without light/visibility data.'
    }
    $derivedSocket = [System.Net.WebSockets.ClientWebSocket]::new()
    $derivedTimeout = [Threading.CancellationTokenSource]::new(5000)
    try {
        $null = $derivedSocket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/lights/smoothed/stream",$derivedTimeout.Token).GetAwaiter().GetResult()
        $derivedBuffer = [byte[]]::new(4096)
        $derivedRead = $derivedSocket.ReceiveAsync([ArraySegment[byte]]::new($derivedBuffer),$derivedTimeout.Token).GetAwaiter().GetResult()
        if (-not $derivedRead.EndOfMessage) { throw 'Unexpectedly large unavailable derived message' }
        $derivedMessage = [Text.Encoding]::UTF8.GetString($derivedBuffer,0,$derivedRead.Count) | ConvertFrom-Json
        if ($derivedMessage.status -ne 'unavailable' -or $derivedMessage.schemaVersion -ne '1.0') { throw 'Derived WebSocket payload mismatch' }
        $null = $derivedSocket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'test complete',$derivedTimeout.Token).GetAwaiter().GetResult()
    } finally { $derivedSocket.Dispose(); $derivedTimeout.Dispose() }

    $visibleSocket = [System.Net.WebSockets.ClientWebSocket]::new()
    $visibleTimeout = [Threading.CancellationTokenSource]::new(5000)
    try {
        $null = $visibleSocket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/lights/visible/stream",$visibleTimeout.Token).GetAwaiter().GetResult()
        $buffer = [byte[]]::new(4096)
        $received = $visibleSocket.ReceiveAsync([ArraySegment[byte]]::new($buffer),$visibleTimeout.Token).GetAwaiter().GetResult()
        if (-not $received.EndOfMessage) { throw 'Unexpectedly large unavailable visible message' }
        $message = [Text.Encoding]::UTF8.GetString($buffer,0,$received.Count) | ConvertFrom-Json
        if ($message.status -ne 'unavailable' -or $message.coverage -notlike '*visibility-clear-only') {
            throw 'Visible WebSocket payload mismatch'
        }
        $null = $visibleSocket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'test complete',$visibleTimeout.Token).GetAwaiter().GetResult()
    } finally { $visibleSocket.Dispose(); $visibleTimeout.Dispose() }

    $sky = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/ambient"
    if ($sky.schemaVersion -ne '1.0' -or $sky.scope -ne 'global-upper-hemisphere-sky' -or
        $sky.localOcclusionIncluded -ne $false -or $sky.exposureNormalized -ne $false) { throw 'Sky scope mismatch.' }
    $skySchema = Invoke-WebRequest -Uri "http://127.0.0.1:$port/v1/ambient/schema"
    $skySchemaText = if ($skySchema.Content -is [byte[]]) { [Text.Encoding]::UTF8.GetString($skySchema.Content) } else { [string]$skySchema.Content }
    if (-not ($sky | ConvertTo-Json -Depth 8 | Test-Json -Schema $skySchemaText)) { throw 'Sky schema mismatch.' }
    # Exercise both producer branches. Unavailable carries null; a valid sky
    # capture carries an estimate object even when local visibility is missing.
    $unavailableSky = $sky | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable
    $unavailableSky.status = 'unavailable'; $unavailableSky.reason = 'test-unavailable'
    foreach ($field in @('captureSequence','frameNumber','capturedAt','ageMilliseconds','sky','visibility','localEnvironmentAmbientEstimateWorking')) {
        $unavailableSky[$field] = $null
    }
    if (-not (Test-Json -Json ($unavailableSky | ConvertTo-Json -Depth 8) -Schema $skySchemaText)) {
        throw 'Unavailable sky with a null local estimate violates schema.'
    }
    $estimate = @{ rgbWorking = $null; available = $false; stale = $false; basis = 'upperHemisphereMeanWorking * cameraSkyVisibilityWorking, both raw' }
    $unavailableSky.localEnvironmentAmbientEstimateWorking = $estimate
    if (Test-Json -Json ($unavailableSky | ConvertTo-Json -Depth 8) -Schema $skySchemaText -ErrorAction SilentlyContinue) {
        throw 'Unavailable sky incorrectly accepts a local estimate object.'
    }
    $availableSky = $unavailableSky | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable
    $availableSky.status = 'available'; $availableSky.reason = $null
    $availableSky.captureSequence = 1; $availableSky.frameNumber = 42
    $availableSky.capturedAt = [DateTimeOffset]::UtcNow.ToString('O'); $availableSky.ageMilliseconds = 0
    $availableSky.sky = @{
        coefficientsWorking = @([double[]]::new(9), [double[]]::new(9), [double[]]::new(9))
        upperHemisphereMeanWorking = @(0,0,0); upwardIrradianceOverPiWorking = @(0,0,0)
        inverseMatrixMean = @(0,0,0); inverseMatrixUpwardIrradianceOverPi = @(0,0,0)
        rec709MeanLuminanceEstimate = 0
    }
    if (-not (Test-Json -Json ($availableSky | ConvertTo-Json -Depth 8) -Schema $skySchemaText)) {
        throw 'Available sky without local visibility must retain its estimate status object.'
    }
    $availableSky.localEnvironmentAmbientEstimateWorking = $null
    if (Test-Json -Json ($availableSky | ConvertTo-Json -Depth 8) -Schema $skySchemaText -ErrorAction SilentlyContinue) {
        throw 'Available sky incorrectly accepts a null local estimate.'
    }
    $skySocket = [System.Net.WebSockets.ClientWebSocket]::new()
    $skyTimeout = [Threading.CancellationTokenSource]::new(5000)
    try {
        $null = $skySocket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/ambient/stream",$skyTimeout.Token).GetAwaiter().GetResult()
        $buffer = [byte[]]::new(8192)
        $received = $skySocket.ReceiveAsync([ArraySegment[byte]]::new($buffer),$skyTimeout.Token).GetAwaiter().GetResult()
        if (-not $received.EndOfMessage) { throw 'Unexpectedly large sky envelope.' }
        $message = [Text.Encoding]::UTF8.GetString($buffer,0,$received.Count)
        if (-not (Test-Json -Json $message -Schema $skySchemaText)) { throw 'Sky stream schema mismatch.' }
        $null = $skySocket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'test complete',$skyTimeout.Token).GetAwaiter().GetResult()
    } finally { $skySocket.Dispose(); $skyTimeout.Dispose() }

    if (-not $health.gameRunning -or $health.status -ne 'playing') {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/v1/snapshot" | Out-Null
            throw 'Snapshot unexpectedly succeeded before telemetry was ready.'
        } catch {
            if ([int]$_.Exception.Response.StatusCode -ne 503) { throw }
        }
    }

    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    try {
        $null = $socket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/stream",
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($socket.State -ne [System.Net.WebSockets.WebSocketState]::Open) {
            throw 'WebSocket did not open.'
        }
        $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
            'test complete', [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    } finally {
        $socket.Dispose()
    }

    foreach ($streamPath in @('/v1/stream','/v1/lights/smoothed/stream','/v1/lights/visible/stream','/v1/ambient/stream')) {
    $remoteSocket = [System.Net.WebSockets.ClientWebSocket]::new()
    try {
        $remoteSocket.Options.SetRequestHeader('Origin', 'https://example.com')
        try {
            $null = $remoteSocket.ConnectAsync([Uri]"ws://127.0.0.1:$port$streamPath",
                [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            throw 'Remote WebSocket browser origin was allowed.'
        } catch [System.Net.WebSockets.WebSocketException] {
            # Expected: only loopback browser origins are accepted.
        }
    } finally {
        $remoteSocket.Dispose()
    }
    }

    Start-Sleep -Milliseconds 100
    $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/health"
    if ($health.connectedClients -ne 0) { throw 'WebSocket subscriber was not removed.' }
    Write-Output 'PASS server HTTP and WebSocket smoke test'
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
}
