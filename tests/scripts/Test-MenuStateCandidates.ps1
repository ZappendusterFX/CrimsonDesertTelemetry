#requires -Version 7.4
# Synthetic byte-addressed fixtures only. Never opens a process.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../../scripts/MenuStateCandidates.psm1') -Force
function Assert($value, [string]$message) { if (-not $value) { throw $message } }
function PutQ([byte[]]$bytes, [int]$offset, [uint64]$value) { [BitConverter]::GetBytes($value).CopyTo($bytes, $offset) }
function PutD([byte[]]$bytes, [int]$offset, [uint32]$value) { [BitConverter]::GetBytes($value).CopyTo($bytes, $offset) }
$menuProfile = Get-MenuCandidateProfile
[uint64]$testBase = 0x140000000
$memory = [Collections.Generic.Dictionary[uint64,byte[]]]::new()
$reads = [Collections.Generic.List[object]]::new()
$controls = @{ HeaderReads = 0; ChangeHeader = $false; OwnerReads = 0; ChangeOwner = $false; HolderReads = 0; ChangeHolder = $false }
$memory[$testBase + $menuProfile.GlobalRva] = [BitConverter]::GetBytes([uint64]0x20000)
$memory[0x200B0] = [BitConverter]::GetBytes([uint64]0x30000)
$memory[0x30A38] = [BitConverter]::GetBytes([uint64]0x40000)
$header = [byte[]]::new(16); PutQ $header 0 0x50000; PutD $header 8 2; PutD $header 12 2
$memory[0x40040] = $header
$entries = [byte[]]::new(64)
PutQ $entries 0 0x60000; PutQ $entries 8 ($testBase + $menuProfile.CallbackRva)
PutQ $entries 32 0x70000; PutQ $entries 40 ($testBase + $menuProfile.CallbackRva)
$memory[0x50000] = $entries
$root = [byte[]]::new(0x132); PutQ $root 0 ($testBase + $menuProfile.Types[2].Vtable)
PutQ $root 8 0x80000; PutD $root 0x20 0x12345678; PutD $root 0xA4 7; PutD $root 0xA8 9; $root[0xB9] = 1
$root[0x130] = 1; $root[0x131] = 2
$memory[0x60000] = $root
$memory[0x70000] = [BitConverter]::GetBytes([uint64]0x140012340)
$memory[0x80028] = [BitConverter]::GetBytes([uint64]0x90000)
$memory[0x90010] = [BitConverter]::GetBytes([uint64]0xA0000)
$memory[0xA004E] = [byte[]]@(1)
$memory[0x8008A] = [BitConverter]::GetBytes([uint16]1)
$memory[0x80058] = [BitConverter]::GetBytes([uint64]0xB0000)
$memory[0xB00B0] = [BitConverter]::GetBytes([uint64]0xC0000)
$memory[0xC00B8] = [BitConverter]::GetBytes([uint64]0xD0000)
$indexHeader = [byte[]]::new(12); PutQ $indexHeader 0 0xE0000; PutD $indexHeader 8 2
$memory[0xD0000] = $indexHeader
$memory[0xE0008] = [BitConverter]::GetBytes([uint64]0xF0000)
$memory[0xF00A0] = [BitConverter]::GetBytes([uint64]0x100000)
$memory[0x100028] = [BitConverter]::GetBytes([uint64]0x110000)
$memory[0x11027C] = [byte[]]@(1, 2)
$memory[0x110120] = [BitConverter]::GetBytes([uint64]0xF0000)
$fakeRead = {
    param([uint64]$address, [int]$length)
    $reads.Add(@{ address = $address; length = $length })
    if ($address -eq 0xF00A0) {
        $controls.HolderReads++
        if ($controls.ChangeHolder -and $controls.HolderReads -gt 1) { $memory[0xF00A0] = [BitConverter]::GetBytes([uint64]0) }
    }
    if ($address -eq 0x40040) {
        $controls.HeaderReads++
        if ($controls.ChangeHeader -and $controls.HeaderReads -gt 1) { $memory[0x40040][8] = 1 }
    }
    if ($address -eq 0x80028) {
        $controls.OwnerReads++
        if ($controls.ChangeOwner -and $controls.OwnerReads -gt 1) { $memory[0x80028] = [BitConverter]::GetBytes([uint64]0x90010) }
    }
    foreach ($addressKey in $memory.Keys) {
        if ($address -ge $addressKey -and $address + $length -le $addressKey + $memory[$addressKey].Length) {
            $result = [byte[]]::new($length)
            [Array]::Copy($memory[$addressKey], [long]($address - $addressKey), $result, 0, $length)
            return ,$result
        }
    }
    throw 'Fixture address unmapped.'
}.GetNewClosure()
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert $sample.stableRegistry 'Stable fixture was rejected.'
Assert ($sample.candidateRoots.Count -eq 1 -and $sample.candidateRoots[0].type -eq 'SkillTree') 'Unrelated root was accepted or matching root lost.'
Assert ($sample.candidateRoots[0].stateA4 -eq 7 -and $sample.candidateRoots[0].stateA8 -eq 9 -and $sample.candidateRoots[0].header20To23 -eq '78563412') 'Opaque fields lost or misdecoded.'
Assert ($sample.candidateRoots[0].opaque130To131 -eq '0102' -and $sample.candidateRoots[0].controlCandidate.status -eq 'read' -and $sample.candidateRoots[0].controlCandidate.opaque4E -eq 1) 'Extended root/control fields lost or misdecoded.'
Assert ($sample.candidateRoots[0].indexedControlCandidate.status -eq 'present' -and $sample.candidateRoots[0].indexedControlCandidate.index -eq 1) 'Indexed control lookup lost the matching entry.'
Assert ($sample.candidateRoots[0].indexedControlCandidate.controlFlags.status -eq 'read' -and $sample.candidateRoots[0].indexedControlCandidate.controlFlags.opaque27CTo27D -eq '0102') 'Indexed control flags lost or misdecoded.'
Assert (-not $sample.Contains('menuOpen')) 'Diagnostic invented an open/closed verdict.'
Assert ($sample.readBytes -lt 1024) 'Small fixture unexpectedly read unrelated memory.'
$memory[0xF00A0] = [BitConverter]::GetBytes([uint64]0); $reads.Clear()
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.controlFlags.status -eq 'absent-holder' -and @($reads | Where-Object address -eq 0x100028).Count -eq 0) 'Absent holder was dereferenced.'
$memory[0xF00A0] = [BitConverter]::GetBytes([uint64]0x100000)
$memory[0x100028] = [BitConverter]::GetBytes([uint64]0)
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.controlFlags.status -eq 'absent-control') 'Absent indexed control fabricated flags.'
$memory[0x100028] = [BitConverter]::GetBytes([uint64]0x110000)
$controls.HolderReads = 0; $controls.ChangeHolder = $true
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.controlFlags.status -eq 'changed' -and -not $sample.candidateRoots[0].indexedControlCandidate.controlFlags.stableChain) 'Changed indexed control accepted as stable.'
$controls.ChangeHolder = $false; $memory[0xF00A0] = [BitConverter]::GetBytes([uint64]0x100000)
$memory[0x8008A] = [BitConverter]::GetBytes([uint16]0xFFFF); $reads.Clear()
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.status -eq 'absent-index' -and @($reads | Where-Object address -eq 0x80058).Count -eq 0) 'Sentinel index followed the downstream chain.'
$memory[0x8008A] = [BitConverter]::GetBytes([uint16]2); $reads.Clear()
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.status -eq 'index-outside-vector' -and @($reads | Where-Object address -eq 0xE0010).Count -eq 0) 'Out-of-range index was dereferenced.'
$memory[0x8008A] = [BitConverter]::GetBytes([uint16]1)
$memory[0xE0008] = [BitConverter]::GetBytes([uint64]0)
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].indexedControlCandidate.status -eq 'absent-entry') 'Null indexed entry was accepted as present.'
$memory[0xE0008] = [BitConverter]::GetBytes([uint64]0xF0000)
$memory[0x90010] = [BitConverter]::GetBytes([uint64]0)
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots.Count -eq 1 -and $sample.candidateRoots[0].controlCandidate.status -eq 'absent-control') 'Absent optional control discarded the valid root or fabricated a field.'
$memory[0x90010] = [BitConverter]::GetBytes([uint64]0xA0000)
$controls.OwnerReads = 0; $controls.ChangeOwner = $true
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert ($sample.candidateRoots[0].controlCandidate.status -eq 'changed' -and -not $sample.candidateRoots[0].controlCandidate.stableChain) 'Changed control chain was accepted as stable.'
$controls.ChangeOwner = $false; $memory[0x80028] = [BitConverter]::GetBytes([uint64]0x90000)
$controls.HeaderReads = 0; $controls.ChangeHeader = $true
$sample = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase
Assert (-not $sample.stableRegistry) 'Changing registry was accepted as stable.'
$controls.ChangeHeader = $false
PutD $header 8 1025; PutD $header 12 1025; $reads.Clear(); $rejected = $false
try { $null = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase } catch { $rejected = $true }
Assert $rejected 'Oversized vector was accepted.'
Assert (@($reads | Where-Object address -eq 0x50000).Count -eq 0) 'Oversized vector was read before its bounds check.'
$memory[$testBase + $menuProfile.GlobalRva] = [BitConverter]::GetBytes([uint64]0); $rejected = $false
try { $null = Read-MenuCandidateSample -ReadMemory $fakeRead -ImageBase $testBase } catch { $rejected = $true }
Assert $rejected 'Null root chain was accepted.'
Write-Output 'PASS synthetic matching/foreign roots, extended root/control fields, indexed entry/sentinel/bounds/null checks, absent/changed optional controls, registry races, oversized-vector refusal and null-chain refusal. No process opened.'
