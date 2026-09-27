<#
.SYNOPSIS
Controls and modulates Crimson Desert in-game lights in real-time.

.DESCRIPTION
Interacts with the CDT GPU ManyLights DMA Modulator bridge (Local\CrimsonDesertTelemetry.Modulator.<pid>)
and the Render telemetry stream (Local\CrimsonDesertTelemetry.Render.<pid>).
Allows turning specific lights ON or OFF, changing brightness/color, or pulsing them to music.

.EXAMPLE
# List active in-game lights near the player
.\scripts\Set-LightSwitch.ps1 -List

.EXAMPLE
# Turn off the nearest light
.\scripts\Set-LightSwitch.ps1 -Nearest -Off

.EXAMPLE
# Turn on the nearest light (restore engine default)
.\scripts\Set-LightSwitch.ps1 -Nearest -On

.EXAMPLE
# Set nearest light to pure Red
.\scripts\Set-LightSwitch.ps1 -Nearest -Color 3.0, 0.0, 0.0

.EXAMPLE
# Pulse the nearest light for 5 seconds
.\scripts\Set-LightSwitch.ps1 -Pulse -Seconds 5
#>

[CmdletBinding()]
param(
    [int]$ProcessId = 0,
    [switch]$List,
    [switch]$Nearest,
    [switch]$All,
    [switch]$Off,
    [switch]$On,
    [int]$LightIndex = -1,
    [float[]]$Color,
    [switch]$Bypass,
    [switch]$Clear,
    [switch]$Pulse,
    [int]$Seconds = 5
)

$ErrorActionPreference = 'Stop'

if ($ProcessId -eq 0) {
    $game = Get-Process -Name 'CrimsonDesert' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $game) {
        throw "CrimsonDesert process not found. Is the game running?"
    }
    $ProcessId = $game.Id
}

$modulatorName = "Local\CrimsonDesertTelemetry.Modulator.$ProcessId"
$renderName = "Local\CrimsonDesertTelemetry.Render.$ProcessId"

# Open Modulator MMF
try {
    $modMmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($modulatorName, [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::ReadWrite)
} catch {
    throw "Could not open Modulator bridge ($modulatorName). Make sure CrimsonDesertTelemetry ASI is loaded and active."
}
$modView = $modMmf.CreateViewAccessor(0, 2112, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)

function Set-LightOverride {
    param(
        [int]$Slot,
        [int]$TargetIndex,
        [float]$R, [float]$G, [float]$B,
        [float]$Intensity = 1.0,
        [bool]$Enabled = $true
    )
    $seq = $modView.ReadInt64(8)
    $modView.Write(8, [int64]($seq + 1))
    [System.Threading.Thread]::MemoryBarrier()

    $modView.Write(0, [uint32]0x4D4C4443) # Magic 'CDLM'
    $modView.Write(4, [uint32]2)          # Version 2

    $curOverrides = $modView.ReadUInt32(20)
    if ($Slot -ge $curOverrides) {
        $modView.Write(20, [uint32]($Slot + 1))
    }
    $modView.Write(24, [uint32]1)         # Master enable

    $offset = 576 + ($Slot * 24)
    $modView.Write($offset + 0, [uint32]$TargetIndex)
    $modView.Write($offset + 4, [float]$R)
    $modView.Write($offset + 8, [float]$G)
    $modView.Write($offset + 12, [float]$B)
    $modView.Write($offset + 16, [float]$Intensity)
    $enabledVal = if ($Enabled) { [uint32]1 } else { [uint32]0 }
    $modView.Write($offset + 20, $enabledVal)

    [System.Threading.Thread]::MemoryBarrier()
    $modView.Write(8, [int64]($seq + 2))
}

function Clear-All {
    $seq = $modView.ReadInt64(8)
    $modView.Write(8, [int64]($seq + 1))
    [System.Threading.Thread]::MemoryBarrier()

    $modView.Write(16, [uint32]0) # ZoneCount = 0
    $modView.Write(20, [uint32]0) # OverrideCount = 0
    for ($i = 0; $i -lt 16; $i++) {
        $offset = 64 + ($i * 32)
        for ($b = 0; $b -lt 32; $b += 4) { $modView.Write($offset + $b, [float]0.0) }
    }
    for ($i = 0; $i -lt 64; $i++) {
        $offset = 576 + ($i * 24)
        for ($b = 0; $b -lt 24; $b += 4) { $modView.Write($offset + $b, [uint32]0) }
    }

    [System.Threading.Thread]::MemoryBarrier()
    $modView.Write(8, [int64]($seq + 2))
}

function Set-MasterBypass {
    param([bool]$Enable)
    $seq = $modView.ReadInt64(8)
    $modView.Write(8, [int64]($seq + 1))
    [System.Threading.Thread]::MemoryBarrier()
    $enableVal = if ($Enable) { [uint32]1 } else { [uint32]0 }
    $modView.Write(24, $enableVal)
    [System.Threading.Thread]::MemoryBarrier()
    $modView.Write(8, [int64]($seq + 2))
}

# Helper to read lights and camera from Render bridge
function Get-RenderSnapshot {
    try {
        $renderMmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($renderName, [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
    } catch {
        return $null
    }
    $view = $renderMmf.CreateViewAccessor(0, 1576192, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)

    # Read camera pos: offset 256 + 128 = 384
    $camX = $view.ReadSingle(384)
    $camY = $view.ReadSingle(388)
    $camZ = $view.ReadSingle(392)

    # Counters: DWORD 1 = valid count (offset + 4 = 1575940)
    $validCount = $view.ReadUInt32(1575940)
    if ($validCount -gt 2048) { $validCount = 0 }

    # Lights start at 256 + 2816 = 3072, stride 48
    $lights = [System.Collections.Generic.List[psobject]]::new()
    for ($i = 0; $i -lt $validCount; $i++) {
        $lightOffset = 3072 + ($i * 48)
        $relX = $view.ReadSingle($lightOffset + 0)
        $relY = $view.ReadSingle($lightOffset + 4)
        $relZ = $view.ReadSingle($lightOffset + 8)
        $r    = $view.ReadSingle($lightOffset + 16)
        $g    = $view.ReadSingle($lightOffset + 20)
        $b    = $view.ReadSingle($lightOffset + 24)

        $worldX = $camX + $relX
        $worldY = $camY + $relY
        $worldZ = $camZ + $relZ
        $dist = [Math]::Sqrt($relX * $relX + $relY * $relY + $relZ * $relZ)

        $lights.Add([PSCustomObject]@{
            Index    = $i
            WorldX   = [Math]::Round($worldX, 2)
            WorldY   = [Math]::Round($worldY, 2)
            WorldZ   = [Math]::Round($worldZ, 2)
            Distance = [Math]::Round($dist, 2)
            RGB      = "$([Math]::Round($r, 2)), $([Math]::Round($g, 2)), $([Math]::Round($b, 2))"
            RawX     = $worldX
            RawY     = $worldY
            RawZ     = $worldZ
            RawR     = $r
            RawG     = $g
            RawB     = $b
        })
    }

    $view.Dispose()
    $renderMmf.Dispose()

    return [PSCustomObject]@{
        CameraPos  = @($camX, $camY, $camZ)
        ValidCount = $validCount
        Lights     = $lights
    }
}

# ----------------- Command Handling -----------------

if ($Clear) {
    Clear-All
    Write-Host "[OK] All light overrides cleared. Lights returned to authentic engine defaults." -ForegroundColor Green
    exit 0
}

if ($Bypass) {
    Set-MasterBypass -Enable $false
    Write-Host "[OK] Modulator master bypass enabled. All modifications suspended." -ForegroundColor Yellow
    exit 0
}

if ($List) {
    $snap = Get-RenderSnapshot
    if (-not $snap) {
        Write-Error "Render bridge telemetry not active."
        exit 1
    }
    Write-Host "Camera Position: X=$($snap.CameraPos[0]) Y=$($snap.CameraPos[1]) Z=$($snap.CameraPos[2])" -ForegroundColor Cyan
    Write-Host "Active GPU ManyLights: $($snap.ValidCount)" -ForegroundColor Cyan
    $snap.Lights | Sort-Object Distance | Select-Object -First 25 | Format-Table -AutoSize
    exit 0
}

if ($Nearest) {
    $snap = Get-RenderSnapshot
    if (-not $snap -or $snap.Lights.Count -eq 0) {
        Write-Error "No active lights detected near camera."
        exit 1
    }
    $target = $snap.Lights | Sort-Object Distance | Select-Object -First 1
    Write-Host "Nearest Light Selected: Index $($target.Index) at Distance $($target.Distance)m (Current RGB: $($target.RGB))" -ForegroundColor Cyan
    Write-Host "World Position: ($($target.WorldX), $($target.WorldY), $($target.WorldZ))" -ForegroundColor Cyan

    if ($Off) {
        Set-LightOverride -Slot 0 -TargetIndex $target.Index -R 0.0 -G 0.0 -B 0.0 -Intensity 0.0 -Enabled $true
        Write-Host "[SWITCH -> AUS] Light #$($target.Index) turned OFF (RGB = 0, 0, 0)." -ForegroundColor Red
    } elseif ($On) {
        Clear-All
        Write-Host "[SWITCH -> AN] Light #$($target.Index) restored to authentic engine default." -ForegroundColor Green
    } elseif ($Color) {
        Set-LightOverride -Slot 0 -TargetIndex $target.Index -R $Color[0] -G $Color[1] -B $Color[2] -Intensity 1.0 -Enabled $true
        Write-Host "[COLOR APPLIED] Light #$($target.Index) RGB set to $($Color[0]), $($Color[1]), $($Color[2])" -ForegroundColor Magenta
    }
    exit 0
}

if ($All) {
    $snap = Get-RenderSnapshot
    if (-not $snap -or $snap.Lights.Count -eq 0) {
        Write-Error "No active lights detected near camera."
        exit 1
    }
    $count = [Math]::Min(64, $snap.Lights.Count)
    Write-Host "Modulating ALL $count nearby lights..." -ForegroundColor Cyan

    if ($Off) {
        for ($i = 0; $i -lt $count; $i++) {
            $t = $snap.Lights[$i]
            Set-LightOverride -Slot $i -TargetIndex $t.Index -R 0.0 -G 0.0 -B 0.0 -Intensity 0.0 -Enabled $true
        }
        Write-Host "[SWITCH -> AUS] All $count lights turned OFF (RGB = 0, 0, 0)." -ForegroundColor Red
    } elseif ($On) {
        Clear-All
        Write-Host "[SWITCH -> AN] All lights restored to authentic engine defaults." -ForegroundColor Green
    } elseif ($Color) {
        for ($i = 0; $i -lt $count; $i++) {
            $t = $snap.Lights[$i]
            Set-LightOverride -Slot $i -TargetIndex $t.Index -R $Color[0] -G $Color[1] -B $Color[2] -Intensity 1.0 -Enabled $true
        }
        Write-Host "[COLOR APPLIED] All $count lights RGB set to $($Color[0]), $($Color[1]), $($Color[2])" -ForegroundColor Magenta
    }
    exit 0
}

if ($LightIndex -ge 0) {
    if ($Off) {
        Set-LightOverride -Slot 0 -TargetIndex $LightIndex -R 0.0 -G 0.0 -B 0.0 -Intensity 0.0 -Enabled $true
        Write-Host "[SWITCH -> AUS] Light #$LightIndex turned OFF (RGB = 0, 0, 0)." -ForegroundColor Red
    } elseif ($On) {
        Clear-All
        Write-Host "[SWITCH -> AN] Light #$LightIndex restored to authentic engine default." -ForegroundColor Green
    } elseif ($Color) {
        Set-LightOverride -Slot 0 -TargetIndex $LightIndex -R $Color[0] -G $Color[1] -B $Color[2] -Intensity 1.0 -Enabled $true
        Write-Host "[COLOR APPLIED] Light #$LightIndex RGB set to $($Color[0]), $($Color[1]), $($Color[2])" -ForegroundColor Magenta
    }
    exit 0
}

if ($Pulse) {
    $snap = Get-RenderSnapshot
    if (-not $snap -or $snap.Lights.Count -eq 0) {
        Write-Error "No active lights detected."
        exit 1
    }
    $target = $snap.Lights | Sort-Object Distance | Select-Object -First 1
    Write-Host "Pulsing nearest light #$($target.Index) (X=$($target.WorldX), Y=$($target.WorldY), Z=$($target.WorldZ)) for $Seconds seconds..." -ForegroundColor Magenta

    $endTime = [DateTime]::UtcNow.AddSeconds($Seconds)
    $t = 0.0
    while ([DateTime]::UtcNow -lt $endTime) {
        $scale = [Math]::Max(0.0, [Math]::Sin($t * [Math]::PI * 2.0 * 2.0))
        $r = [float]($scale * 3.5)
        $g = [float]((1.0 - $scale) * 0.4)
        $b = [float]($scale * 2.0)

        Set-LightOverride -Slot 0 -TargetIndex $target.Index -R $r -G $g -B $b -Intensity 1.0 -Enabled $true
        [System.Threading.Thread]::Sleep(16)
        $t += 0.016
    }

    Clear-All
    Write-Host "[PULSE DEMO COMPLETE] Overrides cleared." -ForegroundColor Green
    exit 0
}

Write-Host "No action specified. Run with -Help, -List, -Nearest -Off, or -Pulse."
