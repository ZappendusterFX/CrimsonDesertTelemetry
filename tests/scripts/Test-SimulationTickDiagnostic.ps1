#requires -Version 7.4
# Synthetic mapping bytes only; no running game or mapping is opened.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../../scripts/SimulationTickDiagnostic.psm1') -Force
function Assert($condition, [string]$message) { if (-not $condition) { throw $message } }
function Put32([int]$offset, [uint32]$value) { [BitConverter]::GetBytes($value).CopyTo($bytes, $offset) }
function Put64([int]$offset, [uint64]$value) { [BitConverter]::GetBytes($value).CopyTo($bytes, $offset) }
function Decode { ConvertFrom-SimulationTickDiagnostic -Bytes $bytes -ExpectedProcessId 1234 -ExpectedProcessStart 5678 -NowMilliseconds 2000 }
function Reject([scriptblock]$action) { $rejected = $false; try { & $action | Out-Null } catch { $rejected = $true }; Assert $rejected 'Invalid mapping accepted.' }
$bytes = [byte[]]::new(128)
Put32 0 0x54445443; Put32 4 1; Put32 8 128; Put32 12 128; Put64 16 2
Put32 24 1234; Put32 28 2; Put64 32 5678; Put64 40 10; Put64 48 2000
Put64 56 1400; Put64 64 600; Put32 72 2; Put32 76 0x4585370; Put32 84 3
$parsed = Decode
Assert ($parsed.state -eq 'no-tick-500ms' -and $parsed.counter -eq 10 -and $parsed.observerFresh) 'Valid paused observation lost.'
Put32 72 1; Assert ((Decode).state -eq 'running') 'Running observation lost.'
Put32 72 0; Put64 64 ([uint64]::MaxValue)
Assert ((Decode).state -eq 'unknown' -and $null -eq (Decode).progressAgeMs) 'Startup promoted to paused.'
Put32 72 2; Put64 48 999; Assert ((Decode).state -eq 'unknown') 'Stale paused observation accepted.'
Put64 48 2001; Assert ((Decode).state -eq 'unknown') 'Future publication accepted.'
Put64 48 2000; Put32 28 4; Assert ((Decode).state -eq 'unknown') 'Hook failure accepted as paused.'
Put32 28 2; Put32 84 1; Assert ((Decode).state -eq 'unknown') 'Disabled hook accepted as paused.'
Put32 84 3; Put32 24 4321; Reject { Decode }; Put32 24 1234
Put64 32 8765; Reject { Decode }; Put64 32 5678
Put64 16 3; Reject { Decode }; Put64 16 2
Put32 4 4; Reject { Decode }; Put32 4 1
Put32 12 256; Reject { Decode }; Put32 12 128
Put32 84 7; Reject { Decode }; Put32 84 3
Put32 72 3; Reject { Decode }
Put32 72 2; Put32 4 2; Put32 84 15; Put64 96 4; Put64 104 1500
Assert ((Decode).acquisitionPaused -and (Decode).captureEpoch -eq 4 -and (Decode).resumedAtTickMs -eq 1500) 'V2 pause control lost.'
Put64 48 999; Assert (-not (Decode).acquisitionPaused) 'Stale V2 pause control accepted.'
Put64 48 2000; Put32 80 1; Assert (-not (Decode).acquisitionPaused) 'Faulted V2 pause control accepted.'
Put32 80 0; Put32 84 3; Assert (-not (Decode).acquisitionPaused) 'Monitor-only V2 treated as acquisition pause.'
Put32 4 3; Put32 84 11; Put32 88 1
Assert ((Decode).state -eq 'no-tick-500ms' -and (Decode).menuState -eq 'closed' -and !(Decode).acquisitionPaused) 'V3 cutscene observation drove pause.'
Put32 84 13; Put32 80 4; Put32 88 2; Put32 72 0
Assert ((Decode).state -eq 'unknown' -and (Decode).menuState -eq 'open' -and (Decode).acquisitionPaused) 'V3 menu could not pause without the hook.'
Put32 88 1; Reject { Decode }
Write-Output 'PASS tick diagnostic decoder: identity, schema, publication, startup, freshness, hook failure and known states. No process opened.'
