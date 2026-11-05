# Diagnostic only: class identity and opaque candidate fields, never a menu verdict.
Set-StrictMode -Version Latest

function Get-MenuCandidateProfile([Parameter(Mandatory)][string]$Identity) {
    $previousSha = 'DC4237F21D6CE1E49F8E5D6FA89BF803FBBE6549123078EFDC393265C7BF6841'
    $currentSha = '25F24E06A1F886E9310FD290F9462F66B480A5E7763D1FE156BFDDAA8D1992B9'
    switch ($Identity) {
        { $_ -in '26423880', $previousSha } {
            $profile = @{
                BuildId = '26423880'
                Sha256 = $previousSha
                PreservedExe = 'artifacts/recovery/20261104-093224-build-26423880/CrimsonDesert.exe'
                Guards = @(
                    @{ Rva = [uint64]0xEE43C0; Hex = 'E9CB4B2A09' },
                    @{ Rva = [uint64]0xFFBCE0; Hex = 'E9BB183D09' },
                    @{ Rva = [uint64]0xA3CD697; Hex = 'C6873001000001' },
                    @{ Rva = [uint64]0xA3CD6D5; Hex = 'C6813001000000' },
                    @{ Rva = [uint64]0xA3CD743; Hex = '488B89200100004889CA488B4958E85A3F4EF9' },
                    @{ Rva = [uint64]0xA3CD715; Hex = '488B88A00000004885C97406488B4928EB034889D980A17D020000FEF6817D02000002741C80B97C020000007413' },
                    @{ Rva = [uint64]0xA188F90; Hex = '488B41084989C94885C0743E440FB7808A000000B9FFFF0000664139C8742B488B4058488B90B00000004C8B92B8000000453B42087313498B024A833CC0007409410FB68130010000C330C0C3' }
                )
            }
            break
        }
        { $_ -in '26448710', $currentSha } {
            $profile = @{
                BuildId = '26448710'
                Sha256 = $currentSha
                PreservedExe = 'artifacts/recovery/20261105-193913-build-26448710/CrimsonDesert.exe'
                Guards = @(
                    @{ Rva = [uint64]0xEE43C0; Hex = 'E9CB782509' },
                    @{ Rva = [uint64]0xFFBCE0; Hex = 'E90B533709' },
                    @{ Rva = [uint64]0xA3710E7; Hex = 'C6873001000001' },
                    @{ Rva = [uint64]0xA371129; Hex = 'C6813001000000' },
                    @{ Rva = [uint64]0xA371197; Hex = '488B89200100004889CA488B4958E8060554F9' },
                    @{ Rva = [uint64]0xA371169; Hex = '488B88A00000004885C97406488B4928EB034889D980A17D020000FEF6817D02000002741C80B97C020000007413' },
                    @{ Rva = [uint64]0xA13BC90; Hex = '488B41084989C94885C0743E440FB7808A000000B9FFFF0000664139C8742B488B4058488B90B00000004C8B92B8000000453B42087313498B024A833CC0007409410FB68130010000C330C0C3' }
                )
            }
            break
        }
        default { throw 'Unsupported EXE profile.' }
    }

    # Both builds retain the subscription path and the six RTTI-identified vtables.
    $profile.GlobalRva = [uint64]0x71BDF48
    $profile.CallbackRva = [uint64]0x1022660
    $profile.Guards += @(
        @{ Rva = [uint64]0xFF93DF; Hex = '4C8BE1488B055F4B1C064C8BB8B00000004C897D40498BBF380A000033DB84D2' },
        @{ Rva = [uint64]0xFF9405; Hex = '48894DC0488D1550920200488955C848895DD066895DD8' },
        @{ Rva = [uint64]0xFF9513; Hex = '8B474848C1E00548034740C5FC1045C0C5FC1100FF4748' },
        @{ Rva = [uint64]0xFF98B2; Hex = '8B47488BF385C0746C4C8B4740488D159A8D020066660F1F840000000000448BF649C1E6054B8D0C304C3B217506483B51087408' },
        @{ Rva = [uint64]0x1022660; Hex = 'E9BB81FDFF' },
        @{ Rva = [uint64]0xDEF90D; Hex = '488B41084C8B4028498B40104885C0741D80784E00' }
    )
    $profile.Types = @(
        @{ Name = 'GamePause'; Vtable = [uint64]0x59F4458; FirstMethod = [uint64]0xEDD4B0 },
        @{ Name = 'SkillList'; Vtable = [uint64]0x5A6D5D0; FirstMethod = [uint64]0x11C7B00 },
        @{ Name = 'SkillTree'; Vtable = [uint64]0x5A6D068; FirstMethod = [uint64]0x11B40C0 },
        @{ Name = 'InventoryEquipment'; Vtable = [uint64]0x59F9138; FirstMethod = [uint64]0xEEC1C0 },
        @{ Name = 'WorldMap'; Vtable = [uint64]0x5A053E8; FirstMethod = [uint64]0xF2CBA0 },
        @{ Name = 'MainMenu'; Vtable = [uint64]0x5A012F0; FirstMethod = [uint64]0xF18360 }
    )
    $profile
}

function Read-MenuCandidateSample {
    [CmdletBinding()]
    param([Parameter(Mandatory)][scriptblock]$ReadMemory,
        [Parameter(Mandatory)][uint64]$ImageBase,
        [Parameter(Mandatory)][hashtable]$MenuProfile,
        [ValidateRange(1, 1024)][int]$MaximumEntries = 1024)
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
    function IndexedControlFlags([uint64]$entry) {
        # The guarded event handler follows this entry to the raw control flags.
        # These bytes and the backlink have no verified menu semantics.
        try {
            $holder = Pointer ($entry + 0xA0)
            if ($holder -eq 0) { return @{ status = 'absent-holder' } }
            $control = Pointer ($holder + 0x28)
            if ($control -eq 0) { return @{ status = 'absent-control' } }
            $flags = Bytes ($control + 0x27C) 2
            $backlink = Pointer ($control + 0x120)
            $stable = (Pointer ($entry + 0xA0)) -eq $holder -and
                (Pointer ($holder + 0x28)) -eq $control -and
                (Pointer ($control + 0x120)) -eq $backlink
            return @{ status = $(if ($stable) { 'read' } else { 'changed' }); stableChain = $stable
                holder = Hex $holder; control = Hex $control; opaque27CTo27D = [Convert]::ToHexString($flags)
                opaqueBacklink120 = Hex $backlink }
        } catch { return @{ status = 'unreadable'; error = 'Indexed control flags unreadable or bounded read rejected.' } }
    }
    function IndexedControlCandidate([uint64]$root, [uint64]$owner) {
        # Read the prerequisites used by the game's guarded common getter.
        # No game function is called; an existing entry is not a visibility verdict.
        try {
            if ($owner -eq 0) { return @{ status = 'absent-owner' } }
            $index = [BitConverter]::ToUInt16((Bytes ($owner + 0x8A) 2), 0)
            if ($index -eq 0xFFFF) { return @{ status = 'absent-index'; index = $index } }
            $context = Pointer ($owner + 0x58)
            $manager = Pointer ($context + 0xB0)
            $vector = Pointer ($manager + 0xB8)
            $header = Bytes $vector 12
            $data = Q $header; $size = D $header 8
            if ($size -gt 65536) { throw 'Indexed control count rejected.' }
            if ($index -ge $size) { return @{ status = 'index-outside-vector'; index = $index; count = $size } }
            $entry = Pointer ($data + [uint64]$index * 8)
            $controlFlags = if ($entry -ne 0) { IndexedControlFlags $entry } else { @{ status = 'absent-entry' } }
            $stable = (Pointer ($root + 8)) -eq $owner -and
                [BitConverter]::ToUInt16((Bytes ($owner + 0x8A) 2), 0) -eq $index -and
                (Pointer ($owner + 0x58)) -eq $context -and (Pointer ($context + 0xB0)) -eq $manager -and
                (Pointer ($manager + 0xB8)) -eq $vector -and
                [Convert]::ToHexString((Bytes $vector 12)) -eq [Convert]::ToHexString($header) -and
                (Pointer ($data + [uint64]$index * 8)) -eq $entry
            return @{ status = $(if (-not $stable) { 'changed' } elseif ($entry -eq 0) { 'absent-entry' } else { 'present' })
                index = $index; count = $size; entry = Hex $entry; stableChain = $stable; controlFlags = $controlFlags }
        } catch { return @{ status = 'unreadable'; error = 'Indexed control chain unreadable or bounded read rejected.' } }
    }
    function ControlCandidate([uint64]$root, [uint64]$owner) {
        # The shared callback at DEF900 follows these pointers before reading4E.
        # Capture the raw value only: visibility/active semantics are not proven.
        try {
            if ($owner -eq 0) { return @{ status = 'absent-owner' } }
            $node = Pointer ($owner + 0x28)
            if ($node -eq 0) { return @{ status = 'absent-node' } }
            $control = Pointer ($node + 0x10)
            if ($control -eq 0) { return @{ status = 'absent-control' } }
            $value = (Bytes ($control + 0x4E) 1)[0]
            $stable = (Pointer ($root + 8)) -eq $owner -and
                (Pointer ($owner + 0x28)) -eq $node -and (Pointer ($node + 0x10)) -eq $control
            return @{ status = $(if ($stable) { 'read' } else { 'changed' }); stableChain = $stable
                node = Hex $node; control = Hex $control; opaque4E = $value }
        } catch { return @{ status = 'unreadable'; error = 'Optional control chain unreadable or bounded read rejected.' } }
    }

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
            $bytes = Bytes $root 0x132
            $controlCandidate = ControlCandidate $root (Q $bytes 8)
            $indexedControl = IndexedControlCandidate $root (Q $bytes 8)
            if ((Q $bytes) -ne $vtable -or (Pointer $root) -ne $vtable) { throw 'Root identity changed during sample.' }
            $roots.Add([ordered]@{
                type = $type[0].Name; address = Hex $root; vtable = Hex $vtable
                owner = Hex (Q $bytes 8); parent = Hex (Q $bytes 0x28)
                header20To23 = [Convert]::ToHexString($bytes, 0x20, 4)
                stateA4 = D $bytes 0xA4; stateA8 = D $bytes 0xA8
                transitionAC = D $bytes 0xAC; transitionB0 = D $bytes 0xB0; transitionB4 = D $bytes 0xB4
                opaqueB8ToC1 = [Convert]::ToHexString($bytes, 0xB8, 10)
                opaque128To12F = [Convert]::ToHexString($bytes, 0x128, 8)
                opaque130To131 = [Convert]::ToHexString($bytes, 0x130, 2)
                controlCandidate = $controlCandidate
                indexedControlCandidate = $indexedControl
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
