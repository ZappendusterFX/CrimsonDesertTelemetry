#requires -Version 7.4
<#
Bounded READ-ONLY research snapshot, exact game build 25477059 only.
No game function calls, writes, hooks, heap scan or atomic-snapshot guarantee.
SceneObject identity is NOT yet a ManyLights contribution ID.
#>
[CmdletBinding()]
param(
    [string]$OutPath,
    [Parameter(Mandatory)][double[]]$Center,
    [ValidateRange(1, 500)][double]$Radius = 100,
    [ValidateRange(1, 15)][int]$MaxSeconds = 10
)
$ErrorActionPreference = 'Stop'
if ($Center.Count -ne 3 -or @($Center | Where-Object { -not [double]::IsFinite($_) }).Count) { throw 'Center needs finite X Y Z.' }
$repo = Split-Path $PSScriptRoot -Parent
if (-not $OutPath) {
    $OutPath = Join-Path $repo ('artifacts/light-research/scene-identity-' +
        (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json')
}
if (Test-Path -LiteralPath $OutPath) { throw 'Output already exists.' }
Add-Type -Path (Join-Path $repo 'src/CrimsonDesertTelemetry.Core/bin/Release/net8.0-windows/CrimsonDesertTelemetry.Core.dll')
$processes = @(Get-Process CrimsonDesert -ErrorAction Stop)
if ($processes.Count -ne 1) { throw 'Exactly one CrimsonDesert process required.' }
$game = $processes[0]
$base = [uint64]$game.MainModule.BaseAddress.ToInt64()
$sha = (Get-FileHash -LiteralPath $game.MainModule.FileName -Algorithm SHA256).Hash
if ($sha -ne '57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7') {
    throw 'Unsupported executable; no object reads.'
}
$reader = [CrimsonDesertTelemetry.Core.ReadOnlyProcess]::new($game)
function Bytes([uint64]$address, [int]$length) { ,$reader.Read([IntPtr]$address, $length) }
function U32([byte[]]$bytes, [int]$offset) { [BitConverter]::ToUInt32($bytes, $offset) }
function U64([byte[]]$bytes, [int]$offset) { [BitConverter]::ToUInt64($bytes, $offset) }
function Hex([uint64]$value) { '0x{0:X}' -f $value }
function Uuid([byte[]]$bytes, [int]$offset) { [Convert]::ToHexString($bytes, $offset, 16) }
try {
    # Length is derived, not an unrelated hardcoded 32-byte read (prefix is 33).
    $prefix = [Convert]::FromHexString('48895C241848896C24204889542410565741564883EC30498BE8488BFA488BF145')
    if ([Convert]::ToHexString((Bytes ($base + 0x1851010) $prefix.Length)) -ne
        [Convert]::ToHexString($prefix)) { throw 'Live UUID lookup guard mismatch.' }
    $world = $reader.ReadPointer($base + 0x6D69190)
    $container = $reader.ReadPointer($world + 0xC8)
    $manager = $reader.ReadPointer($container)
    $registry = $reader.ReadPointer($manager + 0x80)
    $header = Bytes $registry 0x80
    $bucketCount = U32 $header 0x60
    $count = U32 $header 0x64
    $bucketArray = U64 $header 0x70
    $nodeArray = U64 $header 0x78
    if ($bucketCount -lt 1 -or $bucketCount -gt 4096 -or $count -gt 50000) {
        throw 'Registry bounds rejected.'
    }
    $buckets = Bytes $bucketArray ($bucketCount * 0x100)
    $records = [Collections.Generic.List[object]]::new()
    $vtables = @{}
    $pathCache = @{}
    $stats = [ordered]@{ entries = 0; failures = 0; changed = 0; badBuckets = 0;
        rejectedIndex = 0; detached = 0; matchingClientVtable = 0; matchingUuid = 0; near = 0 }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $timedOut = $false
    :scan for ($b = 0; $b -lt $bucketCount; $b++) {
        $n = U32 $buckets ($b * 0x100)
        if ($n -gt 31) { $stats.badBuckets++; continue }
        for ($i = 0; $i -lt $n; $i++) {
            if ($clock.Elapsed.TotalSeconds -ge $MaxSeconds) { $timedOut = $true; break scan }
            $stats.entries++
            $index = U32 $buckets ($b * 0x100 + 12 + $i * 8)
            if ($index -gt 262143) { $stats.rejectedIndex++; continue }
            try {
                $node = $reader.ReadPointer($nodeArray + [uint64]$index * 8)
                $nodeBytes = Bytes $node 0x20
                $uuid = Uuid $nodeBytes 8
                # Lookup returns ClientSyncSceneObjectData, NOT a source wrapper.
                # Verified caller 0x140743267..2CE uses data+0x58 -> source+0x28.
                $syncData = U64 $nodeBytes 0x18
                if ($reader.ReadPointer($syncData) -ne ($base + 0x559F9C0)) { throw 'Unexpected sync-data class.' }
                $interior = $reader.ReadPointer($syncData + 0x58)
                if ($interior -eq 0) { $stats.detached++; continue }
                if ($interior -lt 0x10028) { throw 'Invalid source pointee.' }
                $source = $interior - 0x28
                $object = Bytes $source 0x210
                $vtable = U64 $object 0
                $vkey = Hex $vtable
                $vtables[$vkey] = 1 + $vtables[$vkey]
                if (($vtable - $base) -notin @(0x557E0F8, 0x557E110, 0x557E120)) { continue }
                $stats.matchingClientVtable++
                if ((Uuid $object 0x200) -ne $uuid -or $uuid -eq ('0' * 32)) { continue }
                if ($reader.ReadPointer($nodeArray + [uint64]$index * 8) -ne $node -or
                    $reader.ReadPointer($node + 0x18) -ne $syncData -or
                    $reader.ReadPointer($syncData + 0x58) -ne $interior -or
                    (Uuid (Bytes ($source + 0x200) 16) 0) -ne $uuid -or
                    (Uuid (Bytes ($node + 8) 16) 0) -ne $uuid) { $stats.changed++; continue }
                $stats.matchingUuid++
                $pos = @(0xA0, 0xA4, 0xA8 | ForEach-Object { [BitConverter]::ToSingle($object, $_) })
                if (@($pos | Where-Object { -not [float]::IsFinite($_) -or [Math]::Abs($_) -gt 1000000 }).Count) { continue }
                $distance = [Math]::Sqrt([Math]::Pow($pos[0] - $Center[0], 2) +
                    [Math]::Pow($pos[1] - $Center[1], 2) + [Math]::Pow($pos[2] - $Center[2], 2))
                if ($distance -gt $Radius) { continue }
                $stats.near++
                $prefab = $null
                try {
                    # +60 -> string descriptor -> character data, not direct UTF-8.
                    $pathDescriptor = U64 $object 0x60
                    if ($pathCache.ContainsKey($pathDescriptor)) { $prefab = $pathCache[$pathDescriptor] }
                    else {
                        $pathPointer = $reader.ReadPointer($pathDescriptor)
                        $pathRegion = $reader.QueryRegion($pathPointer)
                        $length = [int][Math]::Min(384, $pathRegion.BaseAddress + $pathRegion.Size - $pathPointer)
                        $pathBytes = Bytes $pathPointer $length
                        $zero = [Array]::IndexOf($pathBytes, [byte]0)
                        if ($zero -gt 0) {
                            $candidate = [Text.Encoding]::UTF8.GetString($pathBytes, 0, $zero)
                            if ($candidate -match '^/object/[^\x00-\x1f]+\.prefab$') { $prefab = $candidate }
                        }
                        $pathCache[$pathDescriptor] = $prefab
                    }
                } catch { } # Optional label; never identity evidence.
                $records.Add([ordered]@{ uuidBytes = $uuid; source = Hex $source; vtable = $vkey;
                    position = $pos; distance = $distance; prefab = $prefab;
                    node = Hex $node; syncData = Hex $syncData; nodeIndex = $index })
            } catch { $stats.failures++ }
        }
    }
    $after = Bytes ($registry + 0x60) 0x20
    $headerStable = [Convert]::ToHexString($header, 0x60, 0x20) -eq [Convert]::ToHexString($after)
    $report = [ordered]@{ scope = 'READ-ONLY, unlocked best-effort registry snapshot; not an atomic scene or ManyLights identity map';
        utc = [DateTime]::UtcNow.ToString('o'); pid = $game.Id; exeSha256 = $sha;
        base = Hex $base; registry = Hex $registry; bucketCount = $bucketCount; declaredCount = $count;
        headerStable = $headerStable; timedOut = $timedOut; seconds = $clock.Elapsed.TotalSeconds;
        center = $Center; radius = $Radius; stats = $stats; observedVtables = $vtables;
        records = $records.ToArray() }
    $json = $report | ConvertTo-Json -Depth 8
    $file = [IO.File]::Open($OutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $encoded = [Text.Encoding]::UTF8.GetBytes($json); $file.Write($encoded) } finally { $file.Dispose() }
    [pscustomobject]@{ output = $OutPath; headerStable = $headerStable; timedOut = $timedOut;
        seconds = $report.seconds; declaredCount = $count; stats = $stats } | ConvertTo-Json -Depth 4
} finally { $reader.Dispose() }
