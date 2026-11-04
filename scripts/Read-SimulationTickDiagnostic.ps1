#requires -Version 7.4
# Reads only CDT's optional per-process diagnostic mapping, never game memory.
[CmdletBinding()]
param(
    [ValidateSet('gameplay', 'skills', 'inventory', 'map', 'paused', 'unknown')][string]$Label = 'unknown',
    [ValidateRange(1, 30)][int]$Seconds = 3,
    [string]$OutPath
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'SimulationTickDiagnostic.psm1') -Force
$games = @(Get-Process CrimsonDesert -ErrorAction Stop)
if ($games.Count -ne 1) { throw 'Exactly one game process is required.' }
$game = $games[0]
$processStart = [uint64]$game.StartTime.ToUniversalTime().ToFileTimeUtc()
$name = 'Local\CrimsonDesertTelemetry.SimulationTicks.' + $game.Id
# OpenExisting+Read never creates or writes a mapping. Missing means this diagnostic is unavailable.
$mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($name, [IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
$view = $null; $output = $null
try {
    $view = $mapping.CreateViewAccessor(0, 128, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
    if (-not $OutPath) {
        $OutPath = Join-Path (Split-Path -Parent $PSScriptRoot) ('artifacts/simulation-ticks-' + $Label + '-' +
            (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N') + '.json')
    }
    $output = [IO.File]::Open([IO.Path]::GetFullPath($OutPath), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $report = [ordered]@{ observerVersion = 1; label = $Label; labelSource = 'Owner/caller supplied, not detected'
        processId = $game.Id; processStartFileTime = $processStart; seconds = $Seconds; samples = @(); complete = $false }
    try {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
            $bytes = [byte[]]::new(128); $accepted = $false
            for ($attempt = 0; $attempt -lt 4; ++$attempt) {
                $before = $view.ReadInt64(16)
                if ($before -le 0 -or ($before -band 1) -ne 0) { continue }
                $null = $view.ReadArray(0, $bytes, 0, $bytes.Length)
                $after = $view.ReadInt64(16)
                if ($before -eq $after -and $after -eq [BitConverter]::ToInt64($bytes, 16)) { $accepted = $true; break }
            }
            if ($accepted) {
                $sample = ConvertFrom-SimulationTickDiagnostic -Bytes $bytes -ExpectedProcessId $game.Id `
                    -ExpectedProcessStart $processStart -NowMilliseconds ([uint64][Environment]::TickCount64)
                $sample['utc'] = [DateTimeOffset]::UtcNow.ToString('o')
                $report.samples += $sample
            } else { $report.samples += @{ state = 'unknown'; error = 'Concurrent or incomplete publication'; utc = [DateTimeOffset]::UtcNow.ToString('o') } }
            Start-Sleep -Milliseconds 50
        }
        $report.complete = $true
    } catch { $report.error = $_.Exception.Message }
    $json = [Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 7))
    $output.Write($json, 0, $json.Length)
    Write-Output ([IO.Path]::GetFullPath($OutPath))
    if (-not $report.complete) { throw ('Diagnostic capture failed: ' + $report.error) }
} finally {
    if ($output) { $output.Dispose() }
    if ($view) { $view.Dispose() }
    $mapping.Dispose()
}
