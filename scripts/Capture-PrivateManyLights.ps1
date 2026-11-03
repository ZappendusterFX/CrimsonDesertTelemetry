# Read-only, exact-build private diagnostic. Capture one progressing paired
# ManyLights/scene sample per owner-reported fire state; never modifies the game.
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][ValidateSet('AN','AUS')][string]$FireState,
    [Parameter(Mandatory)][string]$OutFile,
    [ValidatePattern('^[0-9]+$')][string]$BuildId = '25477059',
    [float]$PlayerX = -10530.632,
    [float]$PlayerY = 609.1567,
    [float]$PlayerZ = -4419.819,
    [ValidateRange(1,100)][float]$Radius = 12
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath((Join-Path $root 'artifacts')) + [IO.Path]::DirectorySeparatorChar
$output = [IO.Path]::GetFullPath($OutFile)
if (-not $output.StartsWith($artifacts,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $output)) { throw 'Choose a new output file below artifacts/.' }
$game = Get-Process -Id $ProcessId -ErrorAction Stop
if ($game.ProcessName -ne 'CrimsonDesert') { throw 'Target process is not Crimson Desert.' }
$profile = Get-Content -LiteralPath (Join-Path $root "definitions\build-$BuildId.json") -Raw | ConvertFrom-Json
$hash = (Get-FileHash -LiteralPath $game.Path -Algorithm SHA256).Hash
if ($hash -ne $profile.executableSha256) { throw 'Game executable differs from the exact diagnostic build.' }
$core = Join-Path $root 'src\CrimsonDesertTelemetry.Core\bin\Release\net8.0-windows\CrimsonDesertTelemetry.Core.dll'
if (-not (Test-Path -LiteralPath $core)) { throw 'Build the managed Core in Release first.' }
Add-Type -Path $core
# Mirror the existing host's playable-world gate: validate current player and camera
# before arming native GPU capture. The diagnostic package deliberately has no host.
$definitions = [CrimsonDesertTelemetry.Core.BuildDefinition]::LoadAll((Join-Path $root 'definitions'))
$resolved = [CrimsonDesertTelemetry.Core.BuildCompatibility]::Resolve($game.Path, $definitions, $true)
if ($resolved.GameBuild -ne $BuildId) { throw 'Resolved diagnostic build differs from the requested build.' }
$memory = [CrimsonDesertTelemetry.Core.ReadOnlyProcess]::new($game)
try {
    $addresses = [CrimsonDesertTelemetry.Core.StaticPositionProbe]::Resolve($game, $game.Path, $resolved.Definition, $true)
    $currentPlayer = [CrimsonDesertTelemetry.Core.StaticPositionProbe]::Read($memory, $addresses)
    $camera = [CrimsonDesertTelemetry.Core.EngineCameraReader]::new($memory,
        [ulong]$game.MainModule.BaseAddress.ToInt64(), $resolved.Definition.EngineCamera)
    $null = $camera.Capture($currentPlayer)
} finally { $memory.Dispose() }
if (-not [CrimsonDesertTelemetry.Core.NativeCaptureReadySignal]::TrySet($ProcessId)) {
    throw 'The private native capture ready gate is unavailable; no capture was armed.'
}
$player = [ValueTuple[float,float,float]]::new($PlayerX,$PlayerY,$PlayerZ)
$reader = [CrimsonDesertTelemetry.Core.RenderLightReader]::new($ProcessId,$game.StartTime.ToFileTimeUtc())
try {
    $firstSequence = $null
    $sample = $null
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $candidate = $reader.Capture($player,$Radius)
        if ($candidate.Status -eq 'available') {
            if ($null -ne $firstSequence -and $candidate.CaptureSequence -gt $firstSequence) {
                $sample = $candidate
                break
            }
            $firstSequence = $candidate.CaptureSequence
        }
        Start-Sleep -Milliseconds 100
    }
    if ($null -eq $sample) {
        throw "No progressing, fresh native capture in four seconds (last status: $($candidate.Status); $($candidate.UnavailableReason))."
    }
    $record = [ordered]@{
        exactBuild = $profile.steamBuildId
        executableSha256 = $hash
        processId = $ProcessId
        fireStateReportedByOwner = $FireState
        capturedAtUtc = [DateTimeOffset]::UtcNow
        player = @{ x = $PlayerX; y = $PlayerY; z = $PlayerZ }
        radius = $Radius
        sample = $sample
    }
    $parent = Split-Path -Parent $output
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
    $record | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $output -NoNewline
    Write-Output "Saved ${FireState}: sequence $($sample.CaptureSequence), frame $($sample.FrameNumber), nearby sources $(@($sample.Sources).Count): $output"
} finally { $reader.Dispose() }
