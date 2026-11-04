# Diagnostic only: class identity and opaque candidate fields, never a menu verdict.
Set-StrictMode -Version Latest

function Get-MenuCandidateProfile {
    @{
        Sha256 = 'DC4237F21D6CE1E49F8E5D6FA89BF803FBBE6549123078EFDC393265C7BF6841'
        GlobalRva = [uint64]0x71BDF48
        CallbackRva = [uint64]0x1022660
        Guards = @(
            @{ Rva = [uint64]0xFF93DF; Hex = '4C8BE1488B055F4B1C064C8BB8B00000004C897D40498BBF380A000033DB84D2' },
            @{ Rva = [uint64]0xFF9513; Hex = '8B474848C1E00548034740C5FC1045C0C5FC1100FF4748' },
            @{ Rva = [uint64]0xFF98B2; Hex = '8B47488BF385C0746C4C8B4740488D159A8D020066660F1F840000000000448BF649C1E6054B8D0C304C3B217506483B51087408' },
            @{ Rva = [uint64]0x1022660; Hex = 'E9BB81FDFF' }
        )
        Types = @(
            @{ Name = 'GamePause'; Vtable = [uint64]0x59F4458; FirstMethod = [uint64]0xEDD4B0 },
            @{ Name = 'SkillList'; Vtable = [uint64]0x5A6D5D0; FirstMethod = [uint64]0x11C7B00 },
            @{ Name = 'SkillTree'; Vtable = [uint64]0x5A6D068; FirstMethod = [uint64]0x11B40C0 },
            @{ Name = 'InventoryEquipment'; Vtable = [uint64]0x59F9138; FirstMethod = [uint64]0xEEC1C0 },
            @{ Name = 'WorldMap'; Vtable = [uint64]0x5A053E8; FirstMethod = [uint64]0xF2CBA0 },
            @{ Name = 'MainMenu'; Vtable = [uint64]0x5A012F0; FirstMethod = [uint64]0xF18360 }
        )
    }
}

function Read-MenuCandidateSample {
    [CmdletBinding()]
    param([Parameter(Mandatory)][scriptblock]$ReadMemory,
        [Parameter(Mandatory)][uint64]$ImageBase,
        [ValidateRange(1, 1024)][int]$MaximumEntries = 1024)
    $menuProfile = Get-MenuCandidateProfile
    $budget = @{ Bytes = 0; Watch = [Diagnostics.Stopwatch]::StartNew() }
    function Bytes([uint64]$address, [int]$length) {
        if ($address -lt 0x10000 -or $address -gt 0x7FFFFFFFFFFF -or $length -le 0 -or
            $address + [uint64]$length -gt 0x800000000000 -or
            $budget.Bytes + $length -gt 1048576 -or $budget.Watch.Elapsed.TotalSeconds -gt 1) {
            throw 'Read address, byte or time budget rejected.'
        }
        $budget.Bytes += $length
        [byte[]]$value = & $ReadMemory $address $length
        if ($value.Length -ne $length) { throw 'Incomplete memory read.' }
        return ,$value
    }
    function Q([byte[]]$bytes, [int]$offset = 0) { [BitConverter]::ToUInt64($bytes, $offset) }
    function D([byte[]]$bytes, [int]$offset) { [BitConverter]::ToUInt32($bytes, $offset) }
    function Pointer([uint64]$address) { Q (Bytes $address 8) }
    function Hex([uint64]$value) { '0x{0:X}' -f $value }

    $application = Pointer ($ImageBase + $menuProfile.GlobalRva)
    $manager = Pointer ($application + 0xB0)
    $subscription = Pointer ($manager + 0xA38)
    $header = Bytes ($subscription + 0x40) 16
    $array = Q $header
    $count = D $header 8
    $capacity = D $header 12
    if ($count -gt $MaximumEntries -or $capacity -gt 4096 -or $count -gt $capacity) {
        throw 'Candidate subscription-vector bounds rejected.'
    }
    $entries = if ($count) { Bytes $array ([int]$count * 32) } else { [byte[]]@() }
    $roots = [Collections.Generic.List[object]]::new()
    $failures = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[uint64]]::new()
    for ($i = 0; $i -lt $count; $i++) {
        $root = Q $entries ($i * 32)
        $callback = Q $entries ($i * 32 + 8)
        if ($callback -ne $ImageBase + $menuProfile.CallbackRva -or -not $seen.Add($root)) { continue }
        try {
            $vtable = Pointer $root
            $type = @($menuProfile.Types | Where-Object { $vtable -eq $ImageBase + $_.Vtable })
            if ($type.Count -ne 1) { continue }
            if ($roots.Count -ge 64) { throw 'Too many matching menu roots.' }
            $bytes = Bytes $root 0xC2
            if ((Q $bytes) -ne $vtable -or (Pointer $root) -ne $vtable) { throw 'Root identity changed during sample.' }
            $roots.Add([ordered]@{
                type = $type[0].Name; address = Hex $root; vtable = Hex $vtable
                owner = Hex (Q $bytes 8); parent = Hex (Q $bytes 0x28)
                header20To23 = [Convert]::ToHexString($bytes, 0x20, 4)
                stateA4 = D $bytes 0xA4; stateA8 = D $bytes 0xA8
                transitionAC = D $bytes 0xAC; transitionB0 = D $bytes 0xB0; transitionB4 = D $bytes 0xB4
                opaqueB8ToC1 = [Convert]::ToHexString($bytes, 0xB8, 10)
            })
        } catch {
            $failures.Add(@{ address = Hex $root; error = 'Root unreadable, changed, or bounded read rejected.' })
        }
    }
    $headerAfter = Bytes ($subscription + 0x40) 16
    $chainStable = (Pointer ($ImageBase + $menuProfile.GlobalRva)) -eq $application -and
        (Pointer ($application + 0xB0)) -eq $manager -and (Pointer ($manager + 0xA38)) -eq $subscription
    $vectorStable = [Convert]::ToHexString($headerAfter) -eq [Convert]::ToHexString($header)
    if ($vectorStable -and $count) {
        $vectorStable = [Convert]::ToHexString((Bytes $array ([int]$count * 32))) -eq [Convert]::ToHexString($entries)
    }
    [ordered]@{
        utc = [DateTimeOffset]::UtcNow.ToString('o'); elapsedMs = $budget.Watch.Elapsed.TotalMilliseconds
        readBytes = $budget.Bytes; stableRegistry = $chainStable -and $vectorStable
        application = Hex $application; manager = Hex $manager; event = Hex $subscription
        entries = $count; capacity = $capacity; candidateRoots = @($roots.ToArray()); rejectedRoots = @($failures.ToArray())
        interpretation = 'Opaque candidate fields only. Presence, B9=1, or any other field is NOT a verified opened-menu state. Registry consistency is best effort, not an atomic snapshot. The one-second budget is checked between reads, not an OS read timeout.'
    }
}
Export-ModuleMember -Function Get-MenuCandidateProfile, Read-MenuCandidateSample
