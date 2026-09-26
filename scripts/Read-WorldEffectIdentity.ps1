#requires -Version 7.4
<# Read-only exact-build effect-handle lookup, reconstructed from 1430EC910,
1403D1E80 and 1430EC820. Never calls game code. Handles are NOT persistent IDs
and this is NOT yet a GPU ManyLights join. Inputs must come from a fresh chain.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ChainPath,
      [Parameter(Mandatory)][string]$Component, [string]$OutPath, [switch]$CaptureApi)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$chain = Get-Content -LiteralPath $ChainPath -Raw | ConvertFrom-Json
$expected = @($chain.records.components | Where-Object address -EQ $Component)
if ($expected.Count -ne 1 -or -not $expected[0].sourceUuid) { throw 'Expected one captured effect component.' }
$game = Get-Process CrimsonDesert
if ($game.Id -ne $chain.pid -or $game.StartTime.ToUniversalTime() -gt ([DateTimeOffset]$chain.utc).UtcDateTime) { throw 'Process changed.' }
$hash = (Get-FileHash -LiteralPath $game.MainModule.FileName -Algorithm SHA256).Hash
if ($hash -ne '57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7') { throw 'Unsupported executable.' }
if (-not $OutPath) { $OutPath = Join-Path $repo ('artifacts/light-research/world-effect-identity-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json') }
if (Test-Path -LiteralPath $OutPath) { throw 'Refusing overwrite.' }
Add-Type -Path (Join-Path $repo 'src/CrimsonDesertTelemetry.Core/bin/Release/net8.0-windows/CrimsonDesertTelemetry.Core.dll')
$reader = [CrimsonDesertTelemetry.Core.ReadOnlyProcess]::new($game)
function Bytes([uint64]$a,[int]$n) { ,$reader.Read([IntPtr]$a,$n) }
function Q([byte[]]$b,[int]$o) { [BitConverter]::ToUInt64($b,$o) }
function D([byte[]]$b,[int]$o) { [BitConverter]::ToUInt32($b,$o) }
function HexAddress([uint64]$a) { '0x{0:X}' -f $a }
function ApiSnapshot {
    if (-not $CaptureApi) { return $null }
    $s = Invoke-RestMethod 'http://127.0.0.1:27311/v1/snapshot' -TimeoutSec 2
    @{utc=[DateTime]::UtcNow.ToString('o'); player=$s.player; camera=$s.camera;
        rendered=$s.lights.rendered; upstream=$s.lights.upstream}
}
$clock = [Diagnostics.Stopwatch]::StartNew()
try {
    $apiBefore = ApiSnapshot
    $clock.Restart()
    $base = [uint64]$game.MainModule.BaseAddress.ToInt64()
    $componentAddress = [Convert]::ToUInt64($Component.Substring(2),16)
    $cb = Bytes $componentAddress 0x2B0
    if ((Q $cb 0) -ne $base+0x5B969F8) { throw 'Effect component vtable changed.' }
    $wrapper = Q $cb 0x258
    if (-not $wrapper) { throw 'Effect source detached.' }
    $source = $reader.ReadPointer($wrapper+8)-0x28
    $sb = Bytes $source 0x210
    $uuid = [Convert]::ToHexString($sb,0x200,16)
    if ((Q $sb 0) -ne $base+0x557E120 -or $uuid -ne $expected[0].sourceUuid) { throw 'Source identity changed.' }
    $key = Q $cb 0x288
    $handle = Q $cb 0x270
    if ($key -eq [uint64]::MaxValue -or $handle -eq [uint64]::MaxValue) { throw 'No registered world-effect handle.' }
    $global = $reader.ReadPointer($base+0x6C8D018)
    $renderer = $reader.ReadPointer($global+0x14818)
    $manager = $reader.ReadPointer($renderer+0x106248)
    if ($reader.ReadPointer($manager) -ne $base+0x5BB7978) { throw 'Manager vtable mismatch.' }
    $matches = [Collections.Generic.List[object]]::new()
    $maps = [Collections.Generic.List[object]]::new()
    foreach ($spec in @(@(0xF4168,0x20),@(0xF41B0,0x10))) {
        $map = $manager+$spec[0]
        $h = Bytes $map 0x20
        $bucketCount = D $h 0; $entryCount = D $h 4
        if ($bucketCount -gt 4096 -or $entryCount -gt 50000) { throw 'Table bounds rejected.' }
        $buckets = Q $h 0x10; $nodes = Q $h 0x18
        $bb = if ($bucketCount) { Bytes $buckets ($bucketCount*0x100) } else { [byte[]]@() }
        $keyFound = $false
        for ($i=0; $i -lt $bucketCount; $i++) {
            $count = D $bb ($i*0x100)
            if ($count -gt 31) { throw 'Bucket count rejected.' }
            for ($j=0; $j -lt $count; $j++) {
                if ($clock.Elapsed.TotalSeconds -gt 5) { throw 'Lookup time budget exceeded; not absence evidence.' }
                $index = D $bb ($i*0x100+12+$j*8)
                if ($index -gt 100000) { throw 'Node index rejected.' }
                $node = $reader.ReadPointer($nodes+$index*8)
                $nb = Bytes $node 0x40
                if ((Q $nb 8) -ne $key) { continue }
                $keyFound = $true
                $vectorOffset = $spec[1]
                $array = Q $nb $vectorOffset; $length = D $nb ($vectorOffset+8)
                if ($length -gt 8192) { throw 'Instance vector count rejected.' }
                $vb = if ($length) { Bytes $array ($length*0x68) } else { [byte[]]@() }
                for ($k=0; $k -lt $length; $k++) {
                    if ((Q $vb ($k*0x68)) -ne $handle) { continue }
                    $address = $array+$k*0x68
                    if ($reader.ReadPointer($node+8) -ne $key -or $reader.ReadPointer($address) -ne $handle -or
                        [Convert]::ToHexString((Bytes ($node+$vectorOffset) 12)) -ne [Convert]::ToHexString($nb,$vectorOffset,12)) { throw 'Instance changed during lookup.' }
                    $matches.Add(@{map=HexAddress $map; node=HexAddress $node; index=$k; count=$length;
                        address=HexAddress $address; bytes=[Convert]::ToHexString($vb,$k*0x68,0x68)})
                }
            }
        }
        $maps.Add(@{address=HexAddress $map; entries=$entryCount; keyFound=$keyFound;
            headerStable=([Convert]::ToHexString((Bytes $map 0x20)) -eq [Convert]::ToHexString($h))})
    }
    $after = Bytes $componentAddress 0x2B0
    if ((Q $after 0x258) -ne $wrapper -or (Q $after 0x288) -ne $key -or (Q $after 0x270) -ne $handle -or
        $reader.ReadPointer($wrapper+8) -ne $source+0x28 -or [Convert]::ToHexString((Bytes ($source+0x200) 16)) -ne $uuid) { throw 'Effect changed during lookup.' }
    $report = [ordered]@{scope='Read-only effect UUID -> keyed 0x68 world instance, NOT a GPU light-ID join';
        utc=[DateTime]::UtcNow.ToString('o'); pid=$game.Id; exeSha256=$hash; chain=$ChainPath;
        component=$Component; source=HexAddress $source; uuidBytes=$uuid; effectKey=HexAddress $key; instanceHandle=$handle;
        manager=HexAddress $manager; componentBytes=[Convert]::ToHexString($cb); maps=$maps.ToArray(); matches=$matches.ToArray(); seconds=$clock.Elapsed.TotalSeconds;
        apiBefore=$apiBefore; apiAfter=(ApiSnapshot); apiTiming='Bracketing API snapshots, not atomic CPU/GPU pairing'}
    $json = [Text.Encoding]::UTF8.GetBytes(($report | ConvertTo-Json -Depth 12))
    $file = [IO.File]::Open($OutPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $file.Write($json) } finally { $file.Dispose() }
    [pscustomobject]@{output=$OutPath; uuid=$uuid; key=HexAddress $key; handle=$handle; matches=$matches.Count; seconds=$report.seconds} | ConvertTo-Json
} finally { $reader.Dispose() }
