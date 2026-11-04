param(
    [string]$PackageDirectory,
    [string]$ArchivePath,
    [ValidateSet('production', 'research')][string]$Profile = 'production',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$expectedNames = @(
    'CrimsonDesertTelemetry.asi',
    'CrimsonDesertTelemetry.ini',
    'CrimsonDesertTelemetry.Core.dll',
    'crimson-desert-telemetry.dll',
    'crimson-desert-telemetry.deps.cfg',
    'crimson-desert-telemetry.runtimeconfig.cfg',
    'README.txt',
    'THIRD-PARTY-NOTICES.txt',
    'LICENSE.txt'
)

function Assert-PackageNames([string[]]$Names) {
    if (@($Names | Where-Object { $_ -match '(?i)\.json$' }).Count) {
        throw 'Loose JSON is forbidden: mod managers interpret it as game-patch data.'
    }
    if ($Names.Count -ne $expectedNames.Count -or @(Compare-Object $expectedNames $Names).Count) {
        throw 'Package contents differ from the ASI companion-file contract.'
    }
}

function Get-ProfileRules([ValidateSet('production', 'research')][string]$ProfileName) {
    # Complete offered configuration, with ranges taken from the native readers.
    # This validates a package's requested settings, not runtime availability.
    $rules = @{
        Compatibility = @{ RecoveryTestOnce = 'recovery-token' }
        Diagnostics = @{ SimulationTicks = 'simulation-ticks' }
        Server = @{ Enabled = 'bool'; Port = 'int:1024:65535'; SampleRateHz = 'int:1:240' }
        Notifications = @{ Enabled = 'bool'; DurationMilliseconds = 'int:5000:10000' }
        CrimsonHue = @{ Enabled = 'bool' }
        Lights = @{ Enabled = 'bool'; NearbyRadius = 'int:1:100000'; ManyLights = 'bool'; ManyLightsSampleRateHz = 'int:1:60'; Upstream = 'bool' }
        Ambient = @{ Enabled = 'bool' }
        SourceVisibility = @{ Enabled = 'bool' }
        LightSmoothing = @{ TimeConstantMilliseconds = 'int:0:2000'; GroupRadius = 'number:0.01:1' }
        Overlay = @{
            Enabled = 'bool'; InitiallyVisible = 'bool'; ShowDetails = 'bool'; ShowAmbient = 'bool'; Radar3D = 'bool'
            ToggleKey = 'int:0:255'; DetailsKey = 'int:0:255'; Corner = 'int:0:3'; AutoScale = 'bool'
            Scale = 'number:0.5:3'; Opacity = 'number:0.2:1'; HdrPaperWhiteNits = 'number:80:500'
            StaleMilliseconds = 'int:100:10000'
        }
        LightOverlay = @{
            Enabled = 'bool'; InitiallyVisible = 'bool'; ToggleKey = 'int:0:255'; HideOccluded = 'bool'
            OcclusionToggleKey = 'int:0:255'
            Radius = 'number:1:500'; MaxMarkers = 'int:1:2048'; MaxLabels = 'int:0:16'
        }
    }
    if ($ProfileName -eq 'research') {
        $rules.Experimental = @{ PhysicsVisibility = 'bool' }
        $rules.Research = @{
            PhysicsProbe = 'bool'; ManyLightsPair = 'bool'
            SignedDistanceReadback = 'bool'; SignedDistanceReadbackCount = 'int:1:120'; SignedDistanceReadbackIntervalMs = 'int:250:10000'
            SpatialProbe = 'bool'; SpatialReadback = 'bool'; SpatialReadbackCount = 'int:1:8'
            SpatialReadbackIntervalMs = 'int:250:10000'; SpatialVisibilitySeconds = 'int:0:4294967295'; AmbientProbe = 'bool'
        }
        $rules.Console = @{
            EnableConsole = 'bool'; PatchGates = 'bool'; PatchDevFlags = 'bool'; EarlyBreakpoint = 'bool'
            HideNotification = 'bool'; VerboseDiscovery = 'bool'; InitDelayMs = 'int:1:30000'
        }
        $rules.Explorer = @{
            EnableExplorer = 'bool'; AllowWrite = 'bool'; AllowCall = 'bool'; AllowDebugCommands = 'bool'
            AllowBreakpoints = 'bool'; AllowManyLights = 'bool'; PollIntervalMs = 'int:1:4294967294'
            CmdFile = 'filename'; ResultFile = 'filename'
        }
        $rules.LightOverlay.OcclusionTest = 'bool'
    }
    return $rules
}

function Assert-IniConfiguration([string]$Ini, [ValidateSet('production', 'research')][string]$ProfileName) {
    $rules = Get-ProfileRules $ProfileName
    $sections = @{}
    $section = $null
    $lineNumber = 0
    foreach ($line in ($Ini.TrimStart([char]0xFEFF) -split '\r?\n')) {
        ++$lineNumber
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith(';') -or $text.StartsWith('#')) { continue }
        if ($text -match '^\[([^\[\]]+)\]$') {
            $section = $Matches[1].Trim()
            if (-not $rules.ContainsKey($section)) { throw "Section [$section] is not supported by the $ProfileName profile." }
            if ($sections.ContainsKey($section)) { throw "Duplicate INI section [$section]." }
            $sections[$section] = @{}
            continue
        }
        if (-not $section -or $text -notmatch '^([^=]+)=(.*)$') { throw "Malformed INI entry at line $lineNumber." }
        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $rules[$section].ContainsKey($key)) { throw "Setting $section.$key is not supported by the $ProfileName profile." }
        if ($sections[$section].ContainsKey($key)) { throw "Duplicate INI setting $section.$key." }
        $sections[$section][$key] = $value
        $rule = $rules[$section][$key] -split ':'
        if ($rule[0] -eq 'bool') {
            if ($value -cnotmatch '^[01]$') { throw "$section.$key must be 0 or 1." }
        }
        elseif ($rule[0] -eq 'simulation-ticks') {
            if ($value -cnotmatch '^[01]$' -or ($ProfileName -eq 'research' -and $value -ne '0')) {
                throw "$section.$key must be 0 or 1 in production code and 0 in research builds."
            }
        }
        elseif ($rule[0] -eq 'filename') {
            # Explorer joins these names to the game directory, deletes its command
            # file and replaces its result file. Require distinct local filenames.
            if (-not $value -or $value -in @('.', '..') -or $value -match '[\\/:*?"<>|\x00-\x1f]' -or $value -match '[. ]$') {
                throw "$section.$key must be a local filename."
            }
        }
        elseif ($rule[0] -eq 'recovery-token') {
            if ($value -and ($value -cnotmatch '^[0-9A-Fa-f]{32}$' -or $value -eq ('0' * 32))) {
                throw "$section.$key must be empty or a nonzero 32-digit hexadecimal token."
            }
            if ($ProfileName -eq 'research' -and $value) {
                throw 'A forced recovery test requires a production-code diagnostic package.'
            }
        }
        else {
            if ($rule[0] -eq 'int' -and $value -cnotmatch '^[0-9]+$') { throw "$section.$key must be an unsigned decimal integer." }
            if ($rule[0] -eq 'number' -and $value.StartsWith('+')) { throw "$section.$key must not have a leading plus sign." }
            $number = 0.0
            $culture = [Globalization.CultureInfo]::InvariantCulture
            if (-not [double]::TryParse($value, [Globalization.NumberStyles]::Float, $culture, [ref]$number) -or
                [double]::IsNaN($number) -or [double]::IsInfinity($number) -or
                $number -lt [double]::Parse($rule[1], $culture) -or $number -gt [double]::Parse($rule[2], $culture)) {
                throw "$section.$key must be within $($rule[1])..$($rule[2]) (decimal point required)."
            }
        }
    }
    foreach ($requiredSection in $rules.Keys) {
        # Added after 2.2.8-recovery.1. Old packages/INIs retain default disabled.
        if ($requiredSection -eq 'Compatibility' -and -not $sections.ContainsKey($requiredSection)) { continue }
        # Private monitor opt-in added after 2.2.8; absent in old packages = OFF.
        if ($requiredSection -eq 'Diagnostics' -and -not $sections.ContainsKey($requiredSection)) { continue }
        if ($requiredSection -eq 'CrimsonHue' -and -not $sections.ContainsKey($requiredSection)) { continue } # old INIs use optional discovery
        # New private opt-in; old research packages remain valid with default OFF.
        if ($requiredSection -eq 'Experimental' -and -not $sections.ContainsKey($requiredSection)) { continue }
        if (-not $sections.ContainsKey($requiredSection)) { throw "Missing INI section [$requiredSection]." }
        foreach ($requiredKey in $rules[$requiredSection].Keys) {
            # Added in the physics diagnostic. Older research INIs default to OFF.
            if ($requiredSection -eq 'Research' -and $requiredKey -in @('PhysicsProbe','ManyLightsPair')) { continue }
            # Added with the all-around input stream; older INIs use the native default.
            if ($requiredSection -eq 'Lights' -and $requiredKey -eq 'Upstream') { continue }
            if (-not $sections[$requiredSection].ContainsKey($requiredKey)) { throw "Missing INI setting $requiredSection.$requiredKey." }
        }
    }
    if ($ProfileName -eq 'research' -and $sections.Explorer.CmdFile -ieq $sections.Explorer.ResultFile) {
        throw 'Explorer command and result filenames must differ.'
    }
    # Deliberately no all-on claim or dependency prohibition: requested features
    # may be unavailable while their upstream feed is disabled. Research includes
    # exclusive diagnostic modes whose runtime effects are documented separately.
}

function Assert-Payload([hashtable]$Files, [ValidateSet('production', 'research')][string]$ProfileName = $Profile) {
    Assert-PackageNames @($Files.Keys)
    $deps = [Text.Encoding]::UTF8.GetString($Files['crimson-desert-telemetry.deps.cfg']) | ConvertFrom-Json
    $runtime = [Text.Encoding]::UTF8.GetString($Files['crimson-desert-telemetry.runtimeconfig.cfg']) | ConvertFrom-Json
    if (-not $deps.runtimeTarget.name -or $null -eq $deps.targets) { throw 'Invalid .NET dependency metadata.' }
    if ($null -eq $runtime.runtimeOptions) { throw 'Invalid .NET runtime configuration.' }
    $ini = [Text.Encoding]::UTF8.GetString($Files['CrimsonDesertTelemetry.ini'])
    Assert-IniConfiguration $ini $ProfileName
    $notices = [Text.Encoding]::UTF8.GetString($Files['THIRD-PARTY-NOTICES.txt'])
    foreach ($dependency in @('Dear ImGui', 'MinHook', 'JSON for Modern C++', 'Tristan Grimmer', 'Sean Barrett')) {
        if (-not $notices.Contains($dependency)) { throw "Missing third-party attribution: $dependency" }
    }
    foreach ($name in @('CrimsonDesertTelemetry.asi', 'CrimsonDesertTelemetry.Core.dll', 'crimson-desert-telemetry.dll')) {
        $bytes = $Files[$name]
        if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
            throw "Not a Windows PE binary: $name"
        }
    }
}

function Assert-Rejected([scriptblock]$Test, [string]$Message) {
    $rejected = $false
    try { & $Test } catch { $rejected = $true }
    if (-not $rejected) { throw $Message }
}

if ($SelfTest) {
    Assert-PackageNames $expectedNames
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'crimson-desert-telemetry.deps.json') } 'Old dependency JSON was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'crimson-desert-telemetry.runtimeconfig.json') } 'Old runtime JSON was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames | Where-Object { $_ -ne 'crimson-desert-telemetry.deps.cfg' }) } 'Missing dependency metadata was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'unexpected.exe') } 'A loose EXE was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'CrimsonHueConsole.asi') } 'A second console ASI was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + $expectedNames[0]) } 'A duplicate companion filename was accepted.'

    # Independent complete fixture: disabled UI/upstream feeds, remapped/disabled
    # keys and supported range boundaries must not be mistaken for bad defaults.
    $productionIni = @'
[Server]
Enabled=0
Port=65535
SampleRateHz=240
[Notifications]
Enabled=0
DurationMilliseconds=10000
[Lights]
Enabled=0
NearbyRadius=1
ManyLights=0
ManyLightsSampleRateHz=1
Upstream=0
[Ambient]
Enabled=1
[SourceVisibility]
Enabled=1
[LightSmoothing]
TimeConstantMilliseconds=0
GroupRadius=0.01
[Overlay]
Enabled=0
InitiallyVisible=0
ShowDetails=1
ShowAmbient=0
Radar3D=0
ToggleKey=0
DetailsKey=255
Corner=3
AutoScale=0
Scale=0.5
Opacity=0.2
HdrPaperWhiteNits=500
StaleMilliseconds=100
[LightOverlay]
Enabled=0
InitiallyVisible=0
ToggleKey=65
HideOccluded=1
OcclusionToggleKey=0
Radius=500
MaxMarkers=1
MaxLabels=0
'@
    Assert-IniConfiguration $productionIni 'production'
    $testToken = '1234567890ABCDEF1234567890ABCDEF'
    $withCompatibility = $productionIni + "`n[Compatibility]`nRecoveryTestOnce=`n"
    Assert-IniConfiguration $withCompatibility 'production'
    Assert-IniConfiguration ($withCompatibility.Replace('RecoveryTestOnce=', "RecoveryTestOnce=$testToken")) 'production'
    foreach ($invalidToken in @(('0' * 32), '1', ('A' * 31), ('A' * 33), ('G' * 32), '../test', '12345678-90AB-CDEF-1234-567890ABCDEF')) {
        Assert-Rejected { Assert-IniConfiguration ($withCompatibility.Replace('RecoveryTestOnce=', "RecoveryTestOnce=$invalidToken")) 'production' } 'An invalid recovery test token was accepted.'
    }
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`n[Compatibility]`n") 'production' } 'An incomplete compatibility section was accepted.'
    $otherBounds = $productionIni.Replace('Port=65535', 'Port=1024').Replace('SampleRateHz=240', 'SampleRateHz=1').
        Replace('DurationMilliseconds=10000', 'DurationMilliseconds=5000').Replace('NearbyRadius=1', 'NearbyRadius=100000').
        Replace('ManyLightsSampleRateHz=1', 'ManyLightsSampleRateHz=60').Replace('TimeConstantMilliseconds=0', 'TimeConstantMilliseconds=2000').
        Replace('GroupRadius=0.01', 'GroupRadius=1').Replace('DetailsKey=255', 'DetailsKey=0').Replace('Corner=3', 'Corner=0').
        Replace('Scale=0.5', 'Scale=3').Replace('Opacity=0.2', 'Opacity=1').Replace('HdrPaperWhiteNits=500', 'HdrPaperWhiteNits=80').
        Replace('StaleMilliseconds=100', 'StaleMilliseconds=10000').Replace('Radius=500', 'Radius=1').
        Replace('MaxMarkers=1', 'MaxMarkers=2048').Replace('MaxLabels=0', 'MaxLabels=16')
    Assert-IniConfiguration $otherBounds 'production'
    $researchTail = @'
OcclusionTest=0
[Research]
SignedDistanceReadback=0
SignedDistanceReadbackCount=120
SignedDistanceReadbackIntervalMs=2000
SpatialProbe=0
SpatialReadback=0
SpatialReadbackCount=1
SpatialReadbackIntervalMs=1000
SpatialVisibilitySeconds=0
AmbientProbe=0
[Console]
EnableConsole=0
PatchGates=1
PatchDevFlags=1
EarlyBreakpoint=1
HideNotification=1
VerboseDiscovery=0
InitDelayMs=5000
[Explorer]
EnableExplorer=0
AllowWrite=0
AllowCall=0
AllowDebugCommands=0
AllowBreakpoints=0
AllowManyLights=0
PollIntervalMs=100
CmdFile=inject_cmd.txt
ResultFile=inject_result.txt
'@
    $researchIni = $productionIni + "`n" + $researchTail
    Assert-IniConfiguration $researchIni 'research'
    $diagnosticSection = "`n[Diagnostics]`nSimulationTicks=0`n"
    Assert-IniConfiguration ($productionIni + $diagnosticSection) 'production'
    Assert-IniConfiguration ($productionIni + $diagnosticSection.Replace('SimulationTicks=0', 'SimulationTicks=1')) 'production'
    Assert-IniConfiguration ($researchIni + $diagnosticSection) 'research'
    Assert-Rejected { Assert-IniConfiguration ($researchIni + $diagnosticSection.Replace('SimulationTicks=0', 'SimulationTicks=1')) 'research' } 'A research package enabled the simulation tick hook.'
    foreach ($invalidTickValue in @('2', '-1', 'true', '01', '')) {
        Assert-Rejected { Assert-IniConfiguration ($productionIni + $diagnosticSection.Replace('SimulationTicks=0', "SimulationTicks=$invalidTickValue")) 'production' } 'An invalid simulation tick switch was accepted.'
    }
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`n[Diagnostics]`n") 'production' } 'An incomplete diagnostics section was accepted.'
    Assert-IniConfiguration ($researchIni + "`n[Compatibility]`nRecoveryTestOnce=`n") 'research'
    Assert-Rejected { Assert-IniConfiguration ($researchIni + "`n[Compatibility]`nRecoveryTestOnce=$testToken`n") 'research' } 'A forced production recovery test was accepted in research instrumentation.'
    Assert-IniConfiguration ($researchIni.Replace('EnableConsole=0', 'EnableConsole=1').Replace('SpatialProbe=0', 'SpatialProbe=1')) 'research'
    Assert-IniConfiguration ($researchIni.Replace('[Research]', "[Research]`nPhysicsProbe=1")) 'research'
    Assert-IniConfiguration ($researchIni.Replace('[Research]', "[Research]`nManyLightsPair=1")) 'research'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('[Research]', "[Research]`nManyLightsPair=2")) 'research' } 'Invalid ManyLightsPair boolean was accepted.'
    Assert-IniConfiguration ($researchIni + "`n[Experimental]`nPhysicsVisibility=1`n") 'research'
    Assert-Rejected { Assert-IniConfiguration ($researchIni + "`n[Experimental]`nPhysicsVisibility=2`n") 'research' } 'Invalid PhysicsVisibility boolean was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('[Research]', "[Research]`nPhysicsProbe=2")) 'research' } 'Invalid PhysicsProbe boolean was accepted.'
    Assert-Rejected { Assert-IniConfiguration $productionIni 'unsupported' } 'An unsupported profile was accepted.'
    Assert-Rejected { Assert-IniConfiguration $researchIni 'production' } 'Research settings were accepted in production.'
    Assert-Rejected { Assert-IniConfiguration $productionIni 'research' } 'An incomplete research profile was accepted.'
    foreach ($forbidden in @('Research', 'Console', 'Explorer', 'Experimental')) {
        Assert-Rejected { Assert-IniConfiguration ($productionIni + "`n[$forbidden]`n") 'production' } "Forbidden section [$forbidden] was accepted."
    }
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`nOcclusionTest=0`n") 'production' } 'The legacy research key was accepted in production.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`nUnknownSetting=1`n") 'production' } 'An unknown key was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni -replace '(?m)^DetailsKey=255\r?\n', '') 'production' } 'A missing offered shortcut was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`nMaxLabels=1`n") 'production' } 'A duplicate key was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni + "`n[Server]`n") 'production' } 'A duplicate section was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('Enabled=0', 'Enabled=true')) 'production' } 'A nonnumeric boolean was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('Upstream=0', 'Upstream=2')) 'production' } 'An invalid Upstream boolean was accepted.'
    Assert-IniConfiguration ($productionIni -replace '(?m)^Upstream=0\r?\n', '') 'production'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('Port=65535', 'Port=1023')) 'production' } 'An unsupported port was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('DetailsKey=255', 'DetailsKey=256')) 'production' } 'An unsupported keycode was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('MaxLabels=0', 'MaxLabels=-1')) 'production' } 'A negative unsigned setting was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('GroupRadius=0.01', 'GroupRadius=0,15')) 'production' } 'A locale-dependent decimal was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('Scale=0.5', 'Scale=NaN')) 'production' } 'A nonfinite number was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($productionIni.Replace('Scale=0.5', 'Scale=+2')) 'production' } 'A leading plus sign rejected by the native UI reader was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('SpatialReadbackCount=1', 'SpatialReadbackCount=9')) 'research' } 'An unsupported research count was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('PollIntervalMs=100', 'PollIntervalMs=0')) 'research' } 'A busy-loop Explorer interval was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('PollIntervalMs=100', 'PollIntervalMs=4294967295')) 'research' } 'An infinite Explorer interval was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('inject_cmd.txt', '..\outside.txt')) 'research' } 'An Explorer path escape was accepted.'
    Assert-Rejected { Assert-IniConfiguration ($researchIni.Replace('inject_result.txt', 'inject_cmd.txt')) 'research' } 'Overlapping Explorer command/result files were accepted.'

    $fixture = @{}
    foreach ($name in $expectedNames) { $fixture[$name] = [Text.Encoding]::UTF8.GetBytes('fixture') }
    foreach ($name in @('CrimsonDesertTelemetry.asi', 'CrimsonDesertTelemetry.Core.dll', 'crimson-desert-telemetry.dll')) {
        $fixture[$name] = [byte[]](0x4D, 0x5A)
    }
    $fixture['CrimsonDesertTelemetry.ini'] = [Text.Encoding]::UTF8.GetBytes($productionIni)
    $fixture['crimson-desert-telemetry.deps.cfg'] = [Text.Encoding]::UTF8.GetBytes('{"runtimeTarget":{"name":"test"},"targets":{}}')
    $fixture['crimson-desert-telemetry.runtimeconfig.cfg'] = [Text.Encoding]::UTF8.GetBytes('{"runtimeOptions":{}}')
    $fixture['THIRD-PARTY-NOTICES.txt'] = [Text.Encoding]::UTF8.GetBytes('Dear ImGui; MinHook; JSON for Modern C++; Tristan Grimmer; Sean Barrett')
    Assert-Payload $fixture 'production'
    $missing = $fixture.Clone(); $missing.Remove('crimson-desert-telemetry.runtimeconfig.cfg')
    Assert-Rejected { Assert-Payload $missing 'production' } 'Missing runtime companion was accepted.'
    $badMetadata = $fixture.Clone(); $badMetadata['crimson-desert-telemetry.deps.cfg'] = [Text.Encoding]::UTF8.GetBytes('{}')
    Assert-Rejected { Assert-Payload $badMetadata 'production' } 'Malformed dependency metadata was accepted.'
    $badPe = $fixture.Clone(); $badPe['CrimsonDesertTelemetry.asi'] = [byte[]](0, 0)
    Assert-Rejected { Assert-Payload $badPe 'production' } 'A non-PE ASI was accepted.'
    $badNotices = $fixture.Clone(); $badNotices['THIRD-PARTY-NOTICES.txt'] = [Text.Encoding]::UTF8.GetBytes('Dear ImGui')
    Assert-Rejected { Assert-Payload $badNotices 'production' } 'Missing dependency attribution was accepted.'

    # Exercise the actual builder gate without invoking publish/build/archive
    # operations. Only its named validation function is loaded from the AST.
    $builderTokens = $null; $builderErrors = $null
    $builderAst = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot '..\scripts\Build-ModManagerPackage.ps1'), [ref]$builderTokens, [ref]$builderErrors)
    if ($builderErrors.Count) { throw 'The package builder contains syntax errors.' }
    $builderFunction = $builderAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Resolve-RecoveryTestToken'
    }, $false)
    if ($null -eq $builderFunction) { throw 'Recovery test package guard is missing.' }
    . ([scriptblock]::Create($builderFunction.Extent.Text))
    $recoveryDefinition = [pscustomobject]@{ nativeCapture = [pscustomobject]@{
        support = [pscustomobject]@{ anchors = @(1..11) }; contextSignatures = @(1..7)
    } }
    if ((Resolve-RecoveryTestToken '' '2.2.7' $false 'auto' $null $null) -cne '') {
        throw 'Normal packages unexpectedly enable a recovery test.'
    }
    if ((Resolve-RecoveryTestToken $testToken.ToLowerInvariant() '2.2.8-recovery.2' $true 'off' $recoveryDefinition $null) -cne $testToken) {
        throw 'A valid private diagnostic token was not normalized.'
    }
    $matchingOverride = @{ 'Compatibility.RecoveryTestOnce' = $testToken.ToLowerInvariant() }
    if ((Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'off' $recoveryDefinition $matchingOverride) -cne $testToken) {
        throw 'A matching qualified token override was rejected.'
    }
    Assert-Rejected { Resolve-RecoveryTestToken ('0' * 32) '2.2.8-recovery.2' $true 'off' $recoveryDefinition $null } 'The builder accepted an all-zero token.'
    Assert-Rejected { Resolve-RecoveryTestToken '../test' '2.2.8-recovery.2' $true 'off' $recoveryDefinition $null } 'The builder accepted a malformed token.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8' $true 'off' $recoveryDefinition $null } 'A public package enabled the forced diagnostic.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $false 'off' $recoveryDefinition $null } 'A non-integrated package enabled the forced diagnostic.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'on' $recoveryDefinition $null } 'A research build enabled the forced diagnostic.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'auto' $recoveryDefinition $null } 'An implicit research build enabled the forced diagnostic.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'off' $null $null } 'A profile without recovery metadata enabled the test.'
    Assert-Rejected { Resolve-RecoveryTestToken '' '2.2.8-recovery.2' $true 'off' $recoveryDefinition $matchingOverride } 'An INI-only override bypassed the explicit diagnostic parameter.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'off' $recoveryDefinition @{ 'Compatibility.RecoveryTestOnce' = 'ABCDEF1234567890ABCDEF1234567890' } } 'A conflicting diagnostic token override was accepted.'
    Assert-Rejected { Resolve-RecoveryTestToken $testToken '2.2.8-recovery.2' $true 'off' $recoveryDefinition @{ 'RecoveryTestOnce' = $testToken } } 'An unqualified token override was accepted.'
    $simulationFunction = $builderAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Resolve-SimulationTickDiagnostic'
    }, $false)
    if ($null -eq $simulationFunction) { throw 'Simulation tick package guard is missing.' }
    . ([scriptblock]::Create($simulationFunction.Extent.Text))
    $simulationDefinition = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\definitions\build-26423880.json') -Raw | ConvertFrom-Json
    if (Resolve-SimulationTickDiagnostic $false '2.2.8' 'auto' '26423880' $null $null) {
        throw 'A normal public package unexpectedly enables simulation tick monitoring.'
    }
    if (Resolve-SimulationTickDiagnostic $false '2.2.8-private.1' 'off' '26423880' $null @{ 'Diagnostics.SimulationTicks' = '0' }) {
        throw 'An explicit OFF override unexpectedly enables simulation tick monitoring.'
    }
    if (-not (Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $simulationDefinition $null)) {
        throw 'The supported private exact-build simulation tick diagnostic was rejected.'
    }
    if (-not (Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $simulationDefinition @{ 'Diagnostics.SimulationTicks' = '1' })) {
        throw 'A matching qualified simulation tick override was rejected.'
    }
    Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8' 'off' '26423880' $simulationDefinition $null } 'A public package enabled simulation tick monitoring.'
    foreach ($mode in @('auto', 'on')) {
        Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' $mode '26423880' $simulationDefinition $null } 'Implicit/research instrumentation enabled simulation tick monitoring.'
    }
    foreach ($packageVersion in @('2.2.8', '2.2.8-simulation.1')) {
        Assert-Rejected { Resolve-SimulationTickDiagnostic $false $packageVersion 'off' '26423880' $simulationDefinition @{ 'Diagnostics.SimulationTicks' = '1' } } 'A raw INI override bypassed the explicit simulation tick switch.'
    }
    Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $simulationDefinition @{ 'SimulationTicks' = '1' } } 'An unqualified simulation tick override was accepted.'
    Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $simulationDefinition @{ 'Diagnostics.SimulationTicks' = '0' } } 'A conflicting simulation tick override was accepted.'
    Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26361232' $simulationDefinition $null } 'A different native build enabled the measured simulation tick diagnostic.'
    Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $null $null } 'A missing native definition enabled simulation tick monitoring.'
    foreach ($changedField in @('build', 'hash', 'status', 'native-status', 'exact')) {
        $changedDefinition = $simulationDefinition | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        switch ($changedField) {
            'build' { $changedDefinition.steamBuildId = '26361232' }
            'hash' { $changedDefinition.executableSha256 = 'A' * 64 }
            'status' { $changedDefinition.status = 'research' }
            'native-status' { $changedDefinition.nativeCapture.status = 'research' }
            'exact' { $changedDefinition.nativeCapture.requiresExactExecutable = $false }
        }
        Assert-Rejected { Resolve-SimulationTickDiagnostic $true '2.2.8-simulation.1' 'off' '26423880' $changedDefinition $null } "A mismatched diagnostic definition ($changedField) was accepted."
    }
    foreach ($template in @('CrimsonDesertTelemetry.ini', 'CrimsonDesertTelemetry.research.ini')) {
        $templateIni = Get-Content -LiteralPath (Join-Path $PSScriptRoot "..\packaging\mod-manager\$template") -Raw
        if ($templateIni -notmatch '(?m)^RecoveryTestOnce=\r?$') { throw 'A default INI does not leave the recovery test disabled.' }
        if ($templateIni -notmatch '(?m)^SimulationTicks=0\r?$') { throw 'A default INI does not leave simulation tick monitoring disabled.' }
        Assert-IniConfiguration $templateIni $(if ($template -like '*.research.ini') { 'research' } else { 'production' })
    }
    Write-Output 'PASS package profiles, nondefault settings, bounds and companion-file regression cases (in-memory only)'
}

if (-not $PackageDirectory -and -not $ArchivePath -and $SelfTest) { return }
if (-not $PackageDirectory -or -not $ArchivePath) { throw 'PackageDirectory and ArchivePath are both required for package validation.' }

$directoryPath = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\') + '\'
$directoryFiles = @{}
foreach ($file in Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File -Force) {
    $relative = $file.FullName.Substring($directoryPath.Length).Replace('\', '/')
    $directoryFiles[$relative] = [IO.File]::ReadAllBytes($file.FullName)
}
Assert-Payload $directoryFiles
Write-Output "PASS expanded $Profile ASI package (no loose JSON or EXE)"

$archiveFiles = @{}
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).Path)
try {
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ($name.EndsWith('/')) { continue }
        $prefix = 'CrimsonDesertTelemetry/'
        if (-not $name.StartsWith($prefix, [StringComparison]::Ordinal)) { throw "Unexpected ZIP root: $name" }
        $relative = $name.Substring($prefix.Length)
        if ($archiveFiles.ContainsKey($relative)) { throw "Duplicate ZIP entry: $name" }
        $stream = $entry.Open()
        $buffer = New-Object IO.MemoryStream
        try { $stream.CopyTo($buffer); $archiveFiles[$relative] = $buffer.ToArray() }
        finally { $stream.Dispose(); $buffer.Dispose() }
    }
} finally { $zip.Dispose() }
Assert-Payload $archiveFiles
foreach ($name in $expectedNames) {
    if ([Convert]::ToBase64String($directoryFiles[$name]) -ne [Convert]::ToBase64String($archiveFiles[$name])) {
        throw "ZIP and expanded payload differ: $name"
    }
}
Write-Output 'PASS ZIP payload matches the validated expanded package'
