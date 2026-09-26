#requires -Version 7.4
<# Read-only, exact-build follow-up to Read-SceneObjectIdentity.ps1.
Follows bounded scene child/component arrays and the relocated renderer queue
back through WorkItem -> Owner -> source wrapper -> SceneObjectClient UUID.
These are scene/effect identities, NOT yet verified ManyLights contribution IDs.
Does not call game code or interpret an empty Owner pointer as an absent light.
JSON snapshots are best-effort, not atomic; the traversal budget is 10 seconds.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SnapshotPath, [string]$OutPath, [switch]$CaptureApi)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$inputSnapshot = Get-Content -LiteralPath $SnapshotPath -Raw | ConvertFrom-Json
$game = Get-Process CrimsonDesert
if ($game.Id -ne $inputSnapshot.pid) { throw 'Snapshot PID is not current.' }
if ($game.StartTime.ToUniversalTime() -gt ([DateTimeOffset]$inputSnapshot.utc).UtcDateTime) {
    throw 'Snapshot predates this process (possible PID reuse). Capture a fresh registry snapshot.'
}
$hash = (Get-FileHash -LiteralPath $game.MainModule.FileName -Algorithm SHA256).Hash
if ($hash -ne '57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7') { throw 'Unsupported EXE.' }
if (-not $OutPath) { $OutPath = Join-Path $repo ('artifacts/light-research/light-identity-chain-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json') }
if (Test-Path -LiteralPath $OutPath) { throw 'Refusing overwrite.' }
Add-Type -Path (Join-Path $repo 'src/CrimsonDesertTelemetry.Core/bin/Release/net8.0-windows/CrimsonDesertTelemetry.Core.dll')
$reader = [CrimsonDesertTelemetry.Core.ReadOnlyProcess]::new($game)
function ReadBytes([uint64]$a, [int]$n) { ,$reader.Read([IntPtr]$a, $n) }
function Q([byte[]]$b, [int]$o) { [BitConverter]::ToUInt64($b, $o) }
function D([byte[]]$b, [int]$o) { [BitConverter]::ToUInt32($b, $o) }
function HexAddress([uint64]$a) { '0x{0:X}' -f $a }
function ApiSnapshot {
    if (-not $CaptureApi) { return $null }
    $s = Invoke-RestMethod 'http://127.0.0.1:27311/v1/snapshot' -TimeoutSec 2
    @{utc=[DateTime]::UtcNow.ToString('o'); player=$s.player; camera=$s.camera;
        rendered=$s.lights.rendered; upstream=$s.lights.upstream}
}
$records = [Collections.Generic.List[object]]::new()
$workRecords = [Collections.Generic.List[object]]::new()
$queueHeaders = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[object]]::new()
$seen = [Collections.Generic.HashSet[uint64]]::new()
$queue = [Collections.Generic.Queue[object]]::new()
$clock = [Diagnostics.Stopwatch]::StartNew()
try {
    $apiBefore = ApiSnapshot
    $clock.Restart()
    $base = [uint64]$game.MainModule.BaseAddress.ToInt64()
    $guard = '48895C241848896C24204889542410565741564883EC30498BE8488BFA488BF145'
    if ([Convert]::ToHexString((ReadBytes ($base + 0x1851010) ($guard.Length / 2))) -ne $guard) { throw 'Live code guard mismatch.' }
    foreach ($root in ($inputSnapshot.records | Where-Object { $_.prefab -match 'lamp|torch|candle|brazier|fire_spark' } | Select-Object -First 256)) {
        $queue.Enqueue(@{address = [Convert]::ToUInt64($root.source.Substring(2), 16); expectedUuid = $root.uuidBytes; depth = 0; parent = $null; prefab = $root.prefab})
    }
    while ($queue.Count -and $seen.Count -lt 768 -and $clock.Elapsed.TotalSeconds -lt 10) {
        $task = $queue.Dequeue()
        $address = [uint64]$task.address
        if (-not $seen.Add($address)) { continue }
        try {
            $scene = ReadBytes $address 0x2B8
            if ((Q $scene 0) -ne ($base + 0x557E120)) { throw 'Client vtable mismatch.' }
            $uuid = [Convert]::ToHexString($scene, 0x200, 16)
            if ($task.expectedUuid -and $task.expectedUuid -ne $uuid) { throw 'Root UUID changed.' }
            $components = [Collections.Generic.List[object]]::new()
            $componentArray = Q $scene 0x210
            $componentCount = D $scene 0x218
            if ($componentCount -gt 64) { throw 'Component count bound.' }
            if ($componentCount) {
                $cb = ReadBytes $componentArray ($componentCount * 8)
                for ($i = 0; $i -lt $componentCount; $i++) {
                    $component = Q $cb ($i * 8)
                    $vt = $reader.ReadPointer($component)
                    $item = [ordered]@{ address = HexAddress $component; vtable = HexAddress $vt }
                    if ($vt -eq ($base + 0x5B969F8)) {
                        $parent = ReadBytes $component 0x2B0
                        $sourceWrapper = Q $parent 0x258
                        $associated = $reader.ReadPointer($sourceWrapper + 8) - 0x28
                        $associatedBytes = ReadBytes $associated 0x210
                        if ((Q $associatedBytes 0) -ne ($base + 0x557E120)) { throw 'Associated client vtable mismatch.' }
                        $item.source = HexAddress $associated
                        $item.sourceUuid = [Convert]::ToHexString($associatedBytes, 0x200, 16)
                        $item.parentBytes = [Convert]::ToHexString($parent)
                        $owner = Q $parent 0x250
                        $item.owner = HexAddress $owner
                        if ($owner) {
                            $ownerBytes = ReadBytes $owner 0x2D8
                            $ownerWrapper = Q $ownerBytes 0x1B0
                            $item.ownerBytes = [Convert]::ToHexString($ownerBytes)
                            $item.ownerVtable = HexAddress (Q $ownerBytes 0)
                            $item.sameSourceWrapper = $ownerWrapper -eq $sourceWrapper
                            $item.workArray = HexAddress (Q $ownerBytes 0x2B0)
                            $count = D $ownerBytes 0x2B8
                            $item.workCount = $count
                            if ($count -gt 0 -and $count -le 64 -and $item.sameSourceWrapper) {
                                $workBytes = ReadBytes (Q $ownerBytes 0x2B0) ($count * 0x2F8)
                                $item.workBytes = [Convert]::ToHexString($workBytes)
                            }
                        }
                    }
                    $components.Add($item)
                }
            }
            $childArray = Q $scene 0x230
            $childCount = D $scene 0x238
            if ($childCount -gt 32) { throw 'Child count bound.' }
            if ($childCount -and $task.depth -lt 3) {
                $children = ReadBytes $childArray ($childCount * 8)
                for ($i = 0; $i -lt $childCount; $i++) {
                    $queue.Enqueue(@{address = (Q $children ($i * 8)); expectedUuid = $null; depth = $task.depth + 1; parent = HexAddress $address; prefab = $null})
                }
            }
            if ([Convert]::ToHexString((ReadBytes ($address + 0x200) 16)) -ne $uuid) { throw 'UUID changed during read.' }
            $records.Add([ordered]@{ address = HexAddress $address; uuidBytes = $uuid; parent = $task.parent;
                prefab = $task.prefab; sceneBytes = [Convert]::ToHexString($scene);
                components = $components.ToArray(); childCount = $childCount })
        } catch { $failures.Add(@{address = HexAddress $address; reason = $_.Exception.Message}) }
    }
    # Independent reverse route, relocated from old function0x143010BB0.
    # Live0x1430E5770 obtains global -> renderer+14818/14820; the queue consumer
    # at0x15180446E reads renderer+6100 and indexes it by WorkItem+2B8.
    # That helper REMOVES entries; it does not establish the insertion path.
    $queueGuard = '488BC4574881EC800000004883794800488BF90F84'
    if ([Convert]::ToHexString((ReadBytes ($base + 0x30E5770) ($queueGuard.Length / 2))) -ne $queueGuard) {
        throw 'Work queue function guard mismatch.'
    }
    $engineGlobal = $reader.ReadPointer($base + 0x6C8D058)
    foreach ($rendererOffset in @(0x14818, 0x14820)) {
        $renderer = $reader.ReadPointer($engineGlobal + $rendererOffset)
        if (-not $renderer) { $queueHeaders.Add(@{offset = $rendererOffset; renderer = '0x0'}); continue }
        $workQueue = $reader.ReadPointer($renderer + 0x6100)
        $queueBytes = ReadBytes $workQueue (2048 * 8)
        $occupied = 0
        $inspected = 0
        for ($slot = 0; $slot -lt 2048; $slot++) {
            $work = Q $queueBytes ($slot * 8)
            if (-not $work) { continue }
            $occupied++
            if ($clock.Elapsed.TotalSeconds -gt 10) { continue }
            $inspected++
            try {
                $wb = ReadBytes $work 0x2F8
                if ((D $wb 0x2B8) -ne $slot) { throw 'WorkItem slot changed.' }
                $owner = Q $wb 0
                $ob = ReadBytes $owner 0x2C0
                if ((Q $ob 0) -ne ($base + 0x5BB77A8)) { throw 'Owner vtable mismatch.' }
                $sourceWrapper = Q $ob 0x1B0
                $source = $reader.ReadPointer($sourceWrapper + 8) - 0x28
                $sb = ReadBytes $source 0x2B8
                if ((Q $sb 0) -ne ($base + 0x557E120)) { throw 'Queue source vtable mismatch.' }
                $uuid = [Convert]::ToHexString($sb, 0x200, 16)
                $workArray = Q $ob 0x2B0
                $workCount = D $ob 0x2B8
                $delta = [long]$work - [long]$workArray
                if ($workCount -gt 2048 -or $delta -lt 0 -or $delta % 0x2F8 -ne 0 -or
                    $delta / 0x2F8 -ge $workCount) { throw 'WorkItem not in owner array.' }
                if ($reader.ReadPointer($workQueue + $slot * 8) -ne $work -or
                    $reader.ReadPointer($work) -ne $owner -or
                    $reader.ReadPointer($owner + 0x1B0) -ne $sourceWrapper -or
                    $reader.ReadPointer($sourceWrapper + 8) -ne $source + 0x28 -or
                    [Convert]::ToHexString((ReadBytes ($source + 0x200) 16)) -ne $uuid) { throw 'Queue chain changed during read.' }
                $prefab = $null
                try {
                    $descriptor = Q $sb 0x60
                    $characters = $reader.ReadPointer($descriptor)
                    $pb = ReadBytes $characters 256
                    $zero = [Array]::IndexOf($pb, [byte]0)
                    if ($zero -gt 0) {
                        $value = [Text.Encoding]::UTF8.GetString($pb, 0, $zero)
                        if ($value -match '^/object/[^\x00-\x1f]+\.prefab$') { $prefab = $value }
                    }
                } catch { }
                $workRecords.Add([ordered]@{ renderer = HexAddress $renderer; slot = $slot;
                    work = HexAddress $work; owner = HexAddress $owner; source = HexAddress $source;
                    uuidBytes = $uuid; prefab = $prefab; workIndexInOwner = $delta / 0x2F8;
                    workCount = $workCount; workBytes = [Convert]::ToHexString($wb);
                    ownerBytes = [Convert]::ToHexString($ob); sceneBytes = [Convert]::ToHexString($sb) })
            } catch { $failures.Add(@{queueSlot = $slot; reason = $_.Exception.Message}) }
        }
        $queueHeaders.Add(@{offset = $rendererOffset; renderer = HexAddress $renderer;
            queue = HexAddress $workQueue; occupied = $occupied; inspected = $inspected})
    }
    $report = [ordered]@{ scope = 'Read-only bounded scene/effect and renderer WorkItem-to-UUID associations; not yet GPU ManyLights contribution IDs';
        utc = [DateTime]::UtcNow.ToString('o'); pid = $game.Id; exeSha256 = $hash;
        inputSnapshot = $SnapshotPath; seconds = $clock.Elapsed.TotalSeconds;
        remaining = $queue.Count; visited = $seen.Count; failures = $failures.ToArray(); records = $records.ToArray();
        workQueues = $queueHeaders.ToArray(); workRecords = $workRecords.ToArray();
        apiBefore = $apiBefore; apiAfter = (ApiSnapshot);
        apiTiming = 'Bracketing API snapshots, not atomic CPU/GPU pairing' }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($report | ConvertTo-Json -Depth 12))
    $file = [IO.File]::Open($OutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $file.Write($bytes) } finally { $file.Dispose() }
    [pscustomobject]@{ output = $OutPath; records = $records.Count; workRecords = $workRecords.Count;
        failures = $failures.Count; remaining = $queue.Count; seconds = $report.seconds } | ConvertTo-Json
} finally { $reader.Dispose() }
