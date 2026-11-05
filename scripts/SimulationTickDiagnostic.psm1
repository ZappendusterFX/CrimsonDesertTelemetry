# Read-only decoder for the optional native tick diagnostic. No game-memory access.
Set-StrictMode -Version Latest

function ConvertFrom-SimulationTickDiagnostic {
    param([Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][uint32]$ExpectedProcessId,
        [Parameter(Mandatory)][uint64]$ExpectedProcessStart,
        [Parameter(Mandatory)][uint64]$NowMilliseconds)
    if ($Bytes.Length -ne 128) { throw 'Unexpected diagnostic mapping size.' }
    function U32([int]$offset) { [BitConverter]::ToUInt32($Bytes, $offset) }
    function U64([int]$offset) { [BitConverter]::ToUInt64($Bytes, $offset) }
    $version = U32 4
    if ((U32 0) -ne 0x54445443 -or $version -notin @(1, 2, 3) -or (U32 8) -ne 128 -or (U32 12) -ne 128) {
        throw 'Unknown diagnostic mapping schema.'
    }
    $sequence = U64 16
    if ($sequence -eq 0 -or ($sequence -band 1) -ne 0) { throw 'Diagnostic mapping is not a completed publication.' }
    if ((U32 24) -ne $ExpectedProcessId -or (U64 32) -ne $ExpectedProcessStart) {
        throw 'Diagnostic process identity mismatch.'
    }
    $status = U32 28; $state = U32 72; $flags = U32 84
    $unknownFlagMask = if ($version -eq 1) { 0xFFFFFFFC } else { 0xFFFFFFF0 }
    if ($status -lt 1 -or $status -gt 4 -or $state -gt 2 -or ($flags -band $unknownFlagMask) -ne 0) {
        throw 'Unknown diagnostic state or flags.'
    }
    $observed = U64 48
    $fresh = $observed -le $NowMilliseconds -and $NowMilliseconds - $observed -le 1000
    $usable = $fresh -and $status -eq 2 -and ($flags -band 2) -ne 0 -and (U32 80) -eq 0
    $menu = if ($version -eq 3) { U32 88 } else { 0 }
    $control = $version -ge 2 -and ($flags -band 8) -ne 0
    $paused = ($flags -band 4) -ne 0
    if ($version -eq 3 -and (!$control -or $menu -gt 2 -or $paused -ne ($menu -eq 2))) {
        throw 'Inconsistent menu activity or pause flags.'
    }
    $menuUsable = $version -eq 3 -and $fresh -and $status -eq 2 -and ($flags -band 1) -ne 0
    $stateName = if (-not $usable) { 'unknown' } else { @('unknown', 'running', 'no-tick-500ms')[$state] }
    [ordered]@{
        schemaVersion = $version; sequence = $sequence; processId = U32 24; processStartFileTime = U64 32
        status = @('', 'starting', 'observing', 'stopped', 'failed')[$status]
        rawState = $state; state = $stateName; observerFresh = $fresh
        counter = U64 40; observerTickMs = $observed; lastProgressTickMs = U64 56
        progressAgeMs = $(if ((U64 64) -eq [uint64]::MaxValue) { $null } else { U64 64 })
        hookRva = '0x{0:X}' -f (U32 76); error = U32 80; flags = $flags
        pauseControlEnabled = $control
        menuState = $(if ($menuUsable) { @('unknown', 'closed', 'open')[$menu] } else { 'unknown' })
        acquisitionPaused = $paused -and $control -and ($menuUsable -or ($version -eq 2 -and $usable -and $state -eq 2))
        captureEpoch = $(if ($version -ge 2) { U64 96 } else { $null })
        resumedAtTickMs = $(if ($version -ge 2) { U64 104 } else { $null })
        interpretation = 'Function entry progress; pause control is reported separately. Reader never changes acquisition or lighting.'
    }
}
Export-ModuleMember -Function ConvertFrom-SimulationTickDiagnostic
