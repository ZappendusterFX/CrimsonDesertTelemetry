#requires -Version 7.4
<# Exact-build, bounded READ-ONLY menu-state research. No game calls, writes,
hooks, heap scan or package installation. Owner label is context, not detection.
ValidateOnly reads the preserved executable and never opens a game process. #>
[CmdletBinding()]
param(
    [ValidateSet('gameplay', 'skills', 'paused', 'inventory', 'map', 'cutscene', 'unknown')][string]$Label = 'unknown',
    [string]$OutPath,
    [ValidateRange(1, 3)][int]$Samples = 3,
    [switch]$WithActivity,
    [switch]$ValidateOnly,
    [ValidateSet('26423880', '26448710')][string]$BuildId = '26448710'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'MenuStateCandidates.psm1') -Force
if ($OutPath -and (Test-Path -LiteralPath $OutPath)) { throw 'Refusing existing output.' }
if ($ValidateOnly) {
    $menuProfile = Get-MenuCandidateProfile -Identity $BuildId
    $preserved = Join-Path $repo $menuProfile.PreservedExe
    if ((Get-FileHash -LiteralPath $preserved -Algorithm SHA256).Hash -ne $menuProfile.Sha256) { throw 'Preserved EXE identity mismatch.' }
    $file = [IO.File]::ReadAllBytes($preserved)
    $pe = [BitConverter]::ToInt32($file, 0x3C)
    $sections = [BitConverter]::ToUInt16($file, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($file, $pe + 20)
    $imageBase = [BitConverter]::ToUInt64($file, $pe + 24 + 24)
    function DiskBytes([uint64]$rva, [int]$length) {
        for ($s = 0; $s -lt $sections; $s++) {
            $p = $pe + 24 + $optionalSize + 40 * $s
            $virtual = [BitConverter]::ToUInt32($file, $p + 12)
            $size = [BitConverter]::ToUInt32($file, $p + 16)
            $raw = [BitConverter]::ToUInt32($file, $p + 20)
            if ($rva -ge $virtual -and $rva + $length -le [uint64]$virtual + $size) {
                [byte[]]$result = [byte[]]::new($length)
                [Array]::Copy($file, [long]($raw + $rva - $virtual), $result, 0, $length)
                return ,$result
            }
        }
        throw 'Guard RVA is not backed by the preserved PE.'
    }
    foreach ($guard in $menuProfile.Guards) {
        if ([Convert]::ToHexString((DiskBytes $guard.Rva ($guard.Hex.Length / 2))) -cne $guard.Hex) { throw 'Offline code guard mismatch.' }
    }
    foreach ($type in $menuProfile.Types) {
        if ([BitConverter]::ToUInt64((DiskBytes $type.Vtable 8), 0) -ne $imageBase + $type.FirstMethod) { throw 'Offline vtable guard mismatch.' }
    }
    Write-Output "PASS preserved build $BuildId EXE, $($menuProfile.Guards.Count) code guards and six vtable anchors. No process opened."
    return
}
if ($PSBoundParameters.ContainsKey('BuildId')) { throw '-BuildId requires -ValidateOnly; live capture selects by EXE hash.' }
$assembly = Join-Path $repo 'src/CrimsonDesertTelemetry.Core/bin/Release/net8.0-windows/CrimsonDesertTelemetry.Core.dll'
if (-not (Test-Path -LiteralPath $assembly)) { throw 'Build the existing Core Release assembly before capture.' }
if (-not $OutPath) {
    $OutPath = Join-Path $repo ('artifacts/menu-state-' + $Label + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N') + '.json')
}
$OutPath = [IO.Path]::GetFullPath($OutPath)
$output = [IO.File]::Open($OutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$reader = $null
$activityClient = $null
$activityBefore = $null
$activityUri = 'http://127.0.0.1:27311/v1/activity'
function Receive-Activity([Threading.Tasks.Task[string]]$Request) {
    try {
        $Request.GetAwaiter().GetResult() | ConvertFrom-Json -AsHashtable -ErrorAction Stop
    } catch {
        @{ error = $_.Exception.Message }
    }
}
$report = [ordered]@{ observerVersion = 4; scope = 'Read-only opaque menu candidates; no verified open/closed detection.'; label = $Label
    labelSource = 'Caller/owner supplied, not automatically detected'; requestedSamples = $Samples; samples = @(); complete = $false }
try {
    $games = @(Get-Process CrimsonDesert -ErrorAction Stop)
    if ($games.Count -ne 1) { throw 'Exactly one CrimsonDesert process required.' }
    $game = $games[0]
    $report.pid = $game.Id
    $report.processStartUtc = $game.StartTime.ToUniversalTime().ToString('o')
    $sha = (Get-FileHash -LiteralPath $game.MainModule.FileName -Algorithm SHA256).Hash
    $report.exeSha256 = $sha
    $menuProfile = Get-MenuCandidateProfile -Identity $sha
    $report.steamBuildId = $menuProfile.BuildId
    $imageBase = [uint64]$game.MainModule.BaseAddress.ToInt64()
    Add-Type -Path $assembly
    $reader = [CrimsonDesertTelemetry.Core.ReadOnlyProcess]::new($game)
    foreach ($guard in $menuProfile.Guards) {
        $live = $reader.Read([IntPtr]($imageBase + $guard.Rva), $guard.Hex.Length / 2)
        if ([Convert]::ToHexString($live) -cne $guard.Hex) { throw 'Live subscription code guard mismatch.' }
    }
    foreach ($type in $menuProfile.Types) {
        if ($reader.ReadPointer($imageBase + $type.Vtable) -ne $imageBase + $type.FirstMethod) { throw 'Live vtable guard mismatch.' }
    }
    $read = { param([uint64]$a, [int]$n) ,$reader.Read([IntPtr]$a, $n) }.GetNewClosure()
    if ($WithActivity) {
        $handler = [Net.Http.HttpClientHandler]::new()
        $handler.UseProxy = $false
        $handler.AllowAutoRedirect = $false
        $activityClient = [Net.Http.HttpClient]::new($handler)
        $activityClient.Timeout = [TimeSpan]::FromSeconds(1)
        $activityClient.MaxResponseContentBufferSize = 65536
        # Start the request without delaying the first UI sample.
        $activityBefore = $activityClient.GetStringAsync($activityUri)
    }
    for ($i = 0; $i -lt $Samples; $i++) {
        $report.samples += Read-MenuCandidateSample -ReadMemory $read -ImageBase $imageBase -MenuProfile $menuProfile
        if ($i + 1 -lt $Samples) { Start-Sleep -Milliseconds 100 }
    }
    $report.complete = $true
} catch {
    $report.error = $_.Exception.Message
} finally {
    if ($reader) { $reader.Dispose() }
    if ($activityBefore) {
        $activityAfter = $activityClient.GetStringAsync($activityUri)
        $report.activityBefore = Receive-Activity $activityBefore
        $report.activityAfter = Receive-Activity $activityAfter
    }
    if ($activityClient) { $activityClient.Dispose() }
    $json = $report | ConvertTo-Json -Depth 12
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $output.Write($bytes, 0, $bytes.Length); $output.Dispose()
}
Write-Output $OutPath
if (-not $report.complete) { throw ('Capture failed; partial evidence preserved. ' + $report.error) }
