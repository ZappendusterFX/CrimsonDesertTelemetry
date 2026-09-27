param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+-]*$')]
    [string]$Version = '2.2.1',
    [ValidatePattern('^[0-9]+$')]
    [string]$NativeBuildId = '25477059',
    # Private diagnostic builds ship ready to run instead of forcing a hand edit
    # after every install, which has already cost one wasted game start. Keys must
    # already exist in the template. A version without a prerelease suffix is
    # treated as a release and refuses overrides outright.
    [hashtable]$IniOverrides,
    # The general research instrumentation is separate from the narrow production
    # ambient acquisition. 'auto' includes research in prerelease versions;
    # pass 'off' explicitly when validating a production implementation privately.
    # All research sources and their direct tests remain preserved.
    [ValidateSet('auto', 'on', 'off')]
    [string]$Research = 'auto',
    # Explicit exact-hash-only native capture on a research profile. This is a
    # private ManyLights diagnostic, never a supported/public package.
    [switch]$UnvalidatedDiagnostic,
    # One private, exact-hash integrated test of every current product feature.
    # The embedded profile remains research and the archive is never releasable.
    [switch]$IntegratedDiagnostic
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $repoRoot 'artifacts\mod-manager'
$archive = Join-Path $artifactRoot "CrimsonDesertTelemetry-v$Version-ModManagers.zip"
if (Test-Path -LiteralPath $archive) {
    throw "Release already exists; choose a new version instead of replacing it: $archive"
}
$buildStamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$stagingRoot = Join-Path $artifactRoot "v$Version-$buildStamp"
$packageRoot = Join-Path $stagingRoot 'CrimsonDesertTelemetry'
$researchEnabled = if ($Research -eq 'auto') { [bool]($Version -match '-') } else { $Research -eq 'on' }
$packageProfile = if ($researchEnabled) { 'research' } else { 'production' }
$privateDiagnostic = $UnvalidatedDiagnostic -or $IntegratedDiagnostic
if ($UnvalidatedDiagnostic -and $IntegratedDiagnostic) {
    throw 'Choose exactly one private diagnostic mode.'
}
if ($privateDiagnostic) {
    if ($Version -notmatch '-' -or $Research -ne 'off' -or $researchEnabled) {
        throw 'Private exact-build diagnostics require a prerelease version and -Research off.'
    }
    $definitionPath = Join-Path $repoRoot "definitions\build-$NativeBuildId.json"
    if (-not (Test-Path -LiteralPath $definitionPath)) { throw "Missing diagnostic profile: $definitionPath" }
    $definition = Get-Content -LiteralPath $definitionPath -Raw | ConvertFrom-Json
    if ($definition.status -ne 'research' -or $definition.nativeCapture.status -ne 'research') {
        throw 'Private diagnostics require a paired research-status exact-build profile.'
    }
    $enabledValue = if ($IntegratedDiagnostic) { '1' } else { '0' }
    $forced = @{
        'Server.Enabled' = $enabledValue; 'Notifications.Enabled' = $enabledValue
        'Lights.Enabled' = '1'; 'Lights.ManyLights' = '1'
        'Ambient.Enabled' = $enabledValue; 'SourceVisibility.Enabled' = $enabledValue
        'Overlay.Enabled' = $enabledValue; 'LightOverlay.Enabled' = $enabledValue
    }
    if (-not $IniOverrides) { $IniOverrides = @{} }
    foreach ($key in $forced.Keys) {
        if ($IniOverrides.ContainsKey($key) -and [string]$IniOverrides[$key] -ne $forced[$key]) {
            throw "Diagnostic safety setting cannot be overridden: $key"
        }
        $IniOverrides[$key] = $forced[$key]
    }
}
$iniTemplate = Join-Path $repoRoot $(if ($researchEnabled) {
    'packaging\mod-manager\CrimsonDesertTelemetry.research.ini'
} else { 'packaging\mod-manager\CrimsonDesertTelemetry.ini' })
$readmeTemplate = Join-Path $repoRoot $(if ($IntegratedDiagnostic) {
    'packaging\mod-manager\README.integrated-diagnostic.txt'
} elseif ($UnvalidatedDiagnostic) {
    'packaging\mod-manager\README.diagnostic.txt'
} elseif ($researchEnabled) {
    'packaging\mod-manager\README.research.txt'
} else { 'packaging\mod-manager\README.txt' })
$configuredIni = Get-Content -LiteralPath $iniTemplate -Raw

# Resolve overrides before any build or package mutation. A production template
# has no research keys; an old/unsupported override therefore fails explicitly.
if ($IniOverrides -and $IniOverrides.Count -gt 0) {
    if ($Version -notmatch '-') {
        throw "Refusing INI overrides for release version '$Version'; a public package ships the clean template."
    }
    foreach ($qualifiedKey in $IniOverrides.Keys) {
        $parts = $qualifiedKey -split '\.', 2
        $key = $parts[-1]
        $pattern = if ($parts.Count -eq 2) {
            $section = [regex]::Escape($parts[0])
            "(?m)(^\[$section\]\s*\r?\n(?:(?!^\[)[^\r\n]*(?:\r?\n|$))*?^$([regex]::Escape($key))=)[^\r\n]*"
        } else {
            "(?m)^$([regex]::Escape($key))=[^\r\n]*$"
        }
        $matches = [regex]::Matches($configuredIni, $pattern)
        if ($matches.Count -ne 1) {
            throw "INI override must identify exactly one supported $packageProfile template key: $qualifiedKey (found $($matches.Count))"
        }
        $value = [string]$IniOverrides[$qualifiedKey]
        if ($value -match '[\r\n]') { throw "INI override must be one value without newlines: $qualifiedKey" }
        $configuredIni = [regex]::Replace($configuredIni, $pattern, {
            param($match)
            if ($parts.Count -eq 2) { return $match.Groups[1].Value + $value }
            return $key + '=' + $value
        })
    }
}
$cmake = $null
$cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        # Require the C++ CMake component so unrelated installer products are ignored.
        # Do not pin a Visual Studio generation: GitHub's windows-latest image advances.
        $cmake = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
    }
}
if (-not $cmake -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found. Install Visual Studio 2022 with C++ and CMake tools, or add CMake to PATH.'
}

$nativeSource = Join-Path $repoRoot 'native\CrimsonDesertTelemetry.Asi'
# Separate trees per setting so a release build never inherits a research CMake cache.
$nativeBuild = Join-Path $repoRoot $(if ($researchEnabled) { 'build\native-package' } else { 'build\native-package-release' })
$managedPublish = Join-Path $repoRoot "build\managed-package\v$Version-$buildStamp"

function Assert-WithinRepo([string]$Path) {
    $resolvedRepo = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedRepo, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing filesystem mutation outside repository: $resolvedPath"
    }
}
Assert-WithinRepo $packageRoot
Assert-WithinRepo $archive

& dotnet publish (Join-Path $repoRoot 'src\CrimsonDesertTelemetry.Cli\CrimsonDesertTelemetry.Cli.csproj') `
    -c Release -r win-x64 --self-contained false -p:UseAppHost=false "-p:Version=$Version" -o $managedPublish
if ($LASTEXITCODE -ne 0) { throw 'Managed host publish failed.' }

Write-Output "Research instrumentation: $(if ($researchEnabled) { 'compiled in' } else { 'COMPILED OUT' }) (-Research $Research)"
& $cmake -S $nativeSource -B $nativeBuild -A x64 "-DCDT_NATIVE_BUILD_ID=$NativeBuildId" "-DCDT_RESEARCH=$(if ($researchEnabled) { 'ON' } else { 'OFF' })" "-DCDT_ALLOW_RESEARCH_NATIVE=$(if ($privateDiagnostic) { 'ON' } else { 'OFF' })"
if ($LASTEXITCODE -ne 0) { throw 'Native ASI configure failed.' }
& $cmake --build $nativeBuild --config Release
if ($LASTEXITCODE -ne 0) { throw 'Native ASI build failed.' }

New-Item -ItemType Directory -Path $packageRoot | Out-Null

$files = @(
    @{ Source = (Join-Path $nativeBuild 'Release\CrimsonDesertTelemetry.asi'); Name = 'CrimsonDesertTelemetry.asi' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.dll'); Name = 'crimson-desert-telemetry.dll' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.deps.json'); Name = 'crimson-desert-telemetry.deps.cfg' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.runtimeconfig.json'); Name = 'crimson-desert-telemetry.runtimeconfig.cfg' },
    @{ Source = (Join-Path $managedPublish 'CrimsonDesertTelemetry.Core.dll'); Name = 'CrimsonDesertTelemetry.Core.dll' },
    @{ Source = $iniTemplate; Name = 'CrimsonDesertTelemetry.ini' },
    @{ Source = $readmeTemplate; Name = 'README.txt' },
    @{ Source = (Join-Path $nativeBuild 'THIRD-PARTY-NOTICES.txt'); Name = 'THIRD-PARTY-NOTICES.txt' },
    @{ Source = (Join-Path $repoRoot 'LICENSE'); Name = 'LICENSE.txt' }
)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.Source)) { throw "Missing package input: $($file.Source)" }
    Copy-Item -LiteralPath $file.Source -Destination (Join-Path $packageRoot $file.Name)
}

if ($IniOverrides -and $IniOverrides.Count -gt 0) {
    $iniPath = Join-Path $packageRoot 'CrimsonDesertTelemetry.ini'
    Set-Content -LiteralPath $iniPath -Value $configuredIni -NoNewline
    Write-Output "INI overrides applied (private build): $(($IniOverrides.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
}

# No -Force: also refuse a release created by another build while this one ran.
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -CompressionLevel Optimal

& (Join-Path $repoRoot 'tests\Test-ModManagerPackage.ps1') -PackageDirectory $packageRoot -ArchivePath $archive -Profile $packageProfile -SelfTest

$hash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
Write-Output "Package: $archive"
Write-Output "Expanded package: $packageRoot"
Write-Output "SHA-256: $($hash.Hash)"
