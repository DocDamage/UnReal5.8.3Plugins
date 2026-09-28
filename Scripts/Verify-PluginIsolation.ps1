<#
.SYNOPSIS
  Build Core alone and Core plus one DocModular feature with every sibling absent.
.DESCRIPTION
  Creates a clean consumer project for each selected plugin under Scripts/Output.
  Only Core and the target feature are copied into the host's DocModular directory.
  The generated consumer compiles each public header in its own translation unit,
  using a non-unity Editor build. This proves compile-time isolation only; it does
  not prove startup, cooked behavior, or second-host portability.
.EXAMPLE
  .\Scripts\Verify-PluginIsolation.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -PluginName DocPuzzleMechanisms
.EXAMPLE
  .\Scripts\Verify-PluginIsolation.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [string]$PluginName,
    [int]$TimeoutMinutes = 90
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$engine = Resolve-DocEngine -EngineRoot $EngineRoot
if ($engine.Version -ne '5.8.3' -or $engine.Changelist -ne 58210709) {
    throw "Expected Unreal Engine 5.8.3 CL 58210709; found $($engine.Version) CL $($engine.Changelist)."
}

$pluginGroupingRoot = Join-Path $repoRoot 'Plugins\DocModular'
$coreRoot = Join-Path $pluginGroupingRoot 'DocModularCore'
$uprojectTemplate = Get-Content -LiteralPath (Join-Path $repoRoot 'DocModularDev.uproject') -Raw | ConvertFrom-Json
$buildBat = Join-Path $engine.Root 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildBat)) { throw "Build.bat not found: $buildBat" }

function Read-PluginMetadata {
    param([Parameter(Mandatory)] [System.IO.DirectoryInfo]$PluginDirectory)

    $descriptors = @(Get-ChildItem -LiteralPath $PluginDirectory.FullName -Filter '*.uplugin' -File)
    if ($descriptors.Count -ne 1) {
        throw "Expected one .uplugin in $($PluginDirectory.FullName); found $($descriptors.Count)."
    }

    $descriptor = Get-Content -LiteralPath $descriptors[0].FullName -Raw | ConvertFrom-Json
    $runtimeModules = @($descriptor.Modules | Where-Object { $_.Type -eq 'Runtime' })
    if ($runtimeModules.Count -ne 1) {
        throw "Expected one Runtime module in $($descriptors[0].Name); found $($runtimeModules.Count)."
    }

    $moduleName = [string]$runtimeModules[0].Name
    $publicRoot = Join-Path $PluginDirectory.FullName (Join-Path 'Source' (Join-Path $moduleName 'Public'))
    if (-not (Test-Path -LiteralPath $publicRoot -PathType Container)) {
        throw "Runtime public header directory is missing: $publicRoot"
    }
    $publicHeaders = @(Get-ChildItem -LiteralPath $publicRoot -Filter '*.h' -File -Recurse | Sort-Object FullName)
    if ($publicHeaders.Count -eq 0) { throw "No public headers found for $moduleName." }

    [pscustomobject]@{
        Name            = $descriptors[0].BaseName
        Root            = $PluginDirectory.FullName
        DescriptorPath  = $descriptors[0].FullName
        Descriptor      = $descriptor
        RuntimeModule   = $moduleName
        PublicRoot      = $publicRoot
        PublicHeaders   = $publicHeaders
    }
}

function Copy-PluginSourceClean {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $generatedPath = '\\(Binaries|Intermediate|Saved|DerivedDataCache|\.git)\\'
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File -Force) {
        if ($file.FullName -match $generatedPath) { continue }
        $relative = $file.FullName.Substring($Source.Length).TrimStart('\')
        $target = Join-Path $Destination $relative
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

function Write-HostFile {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Content
    )
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Add-UniquePluginName {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [System.Collections.Generic.List[string]]$List,
        [Parameter(Mandatory)] [string]$Name
    )
    if (-not $List.Contains($Name)) { $List.Add($Name) }
}

$coreMetadata = Read-PluginMetadata -PluginDirectory (Get-Item -LiteralPath $coreRoot)
$featureMetadata = @(
    Get-ChildItem -LiteralPath $pluginGroupingRoot -Directory |
        Where-Object { $_.Name -ne 'DocModularCore' } |
        Sort-Object Name |
        ForEach-Object { Read-PluginMetadata -PluginDirectory $_ }
)
if ($featureMetadata.Count -ne 40) {
    throw "Expected 40 feature plugins under $pluginGroupingRoot; found $($featureMetadata.Count)."
}

$allTargets = @($coreMetadata) + $featureMetadata
if ($PluginName) {
    $selected = @($allTargets | Where-Object { $_.Name -eq $PluginName })
    if ($selected.Count -ne 1) {
        throw "Unknown plugin '$PluginName'. Choose DocModularCore or one of the 40 feature plugin folder names."
    }
    $targets = $selected
}
else {
    $targets = $allTargets
}

$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$verifierHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$runRoot = New-DocRunDirectory -OutputRoot (Join-Path $PSScriptRoot 'Output') -Label 'PluginIsolation'
$summaryPath = Join-Path $runRoot 'summary.json'
$start = Get-Date
$end = $null
$results = [System.Collections.Generic.List[object]]::new()
$enginePluginsRoot = Join-Path $engine.Root 'Engine\Plugins'

function Save-IsolationSummary {
    param([Parameter(Mandatory)] [string]$RunResult)

    $passedCount = @($results | Where-Object { $_.result -eq 'Passed' }).Count
    $summaryRecord = [ordered]@{
        check = 'PluginIsolation'
        result = $RunResult
        sourceSha256 = $sourceHash
        verifierSha256 = $verifierHash
        engineVersion = $engine.Version
        changelist = $engine.Changelist
        fullSuite = (-not [bool]$PluginName)
        expectedPlugins = $targets.Count
        completedPlugins = $results.Count
        passed = $passedCount
        failed = ($results.Count - $passedCount)
        start = $start.ToString('o')
        end = if ($null -eq $end) { $null } else { $end.ToString('o') }
        runDirectory = $runRoot
        results = @($results.ToArray())
    }
    $summaryJson = $summaryRecord | ConvertTo-Json -Depth 14
    [System.IO.File]::WriteAllText($summaryPath, $summaryJson, [System.Text.UTF8Encoding]::new($false))
}

Save-IsolationSummary -RunResult 'InProgress'

$mappedDrive = $null
$availableDriveLetters = @('Q', 'P', 'O', 'N', 'M', 'L', 'K', 'J', 'I', 'H', 'G', 'E', 'D', 'C', 'Z', 'Y', 'X', 'W', 'V', 'U', 'T', 'S', 'R')
$usedDriveLetters = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Name.ToUpperInvariant() })
$mappedDrive = $availableDriveLetters | Where-Object { $_ -notin $usedDriveLetters } | Select-Object -First 1
if (-not $mappedDrive) { throw 'No free drive letter is available for a short-path isolation build.' }
& (Join-Path $env:SystemRoot 'System32\subst.exe') ($mappedDrive + ':') $repoRoot
if ($LASTEXITCODE -ne 0) { throw "Failed to create temporary $mappedDrive`: mapping for the repository root." }
$mappedRepoRoot = $mappedDrive + ':\'

try {
foreach ($targetPlugin in $targets) {
    $pluginStart = Get-Date
    $stageRoot = Join-Path $runRoot $targetPlugin.Name
    $buildStageRoot = Join-Path (Join-Path $mappedRepoRoot 'Scripts\Output') (Join-Path (Split-Path -Leaf $runRoot) $targetPlugin.Name)
    $stageProject = Join-Path $buildStageRoot 'DocIsolationHost.uproject'
    $stagePluginRoot = Join-Path $buildStageRoot 'Plugins\DocModular'
    $buildLog = Join-Path $stageRoot 'build.log'
    $exitCode = $null
    $result = 'Failed'
    $reason = $null
    $command = $null
    $enabledPluginNames = [System.Collections.Generic.List[string]]::new()
    $siblingDependencies = [System.Collections.Generic.List[string]]::new()
    $stagedPluginNames = @('DocModularCore')
    $consumerHeaders = [System.Collections.Generic.List[string]]::new()

    try {
        $corePluginRefs = @()
        if ($coreMetadata.Descriptor.PSObject.Properties['Plugins']) {
            $corePluginRefs = @($coreMetadata.Descriptor.Plugins)
        }
        $targetPluginRefs = @()
        if ($targetPlugin.Descriptor.PSObject.Properties['Plugins']) {
            $targetPluginRefs = @($targetPlugin.Descriptor.Plugins)
        }
        $descriptorPluginRefs = @($corePluginRefs) + @($targetPluginRefs)
        foreach ($pluginRef in $descriptorPluginRefs) {
            if ($null -eq $pluginRef) { continue }
            if ($pluginRef.PSObject.Properties['Enabled'] -and $pluginRef.Enabled -eq $false) { continue }
            $dependencyName = [string]$pluginRef.Name
            if ([string]::IsNullOrWhiteSpace($dependencyName)) { continue }
            Add-UniquePluginName -List $enabledPluginNames -Name $dependencyName

            if ($dependencyName -like 'Doc*' -and $dependencyName -ne 'DocModularCore' -and $dependencyName -ne $targetPlugin.Name) {
                Add-UniquePluginName -List $siblingDependencies -Name $dependencyName
                continue
            }

            if ($dependencyName -ne 'DocModularCore' -and $dependencyName -ne $targetPlugin.Name) {
                $enginePluginPath = Join-Path $enginePluginsRoot ($dependencyName + '.uplugin')
                if (-not (Test-Path -LiteralPath $enginePluginPath -PathType Leaf)) {
                    $enginePluginPath = Get-ChildItem -LiteralPath $enginePluginsRoot -Filter ($dependencyName + '.uplugin') -File -Recurse -ErrorAction SilentlyContinue |
                        Select-Object -First 1 -ExpandProperty FullName
                }
                if (-not $enginePluginPath) {
                    throw "Declared non-sibling plugin dependency '$dependencyName' is not installed under the engine."
                }
            }
        }

        Add-UniquePluginName -List $enabledPluginNames -Name 'DocModularCore'
        if ($targetPlugin.Name -ne 'DocModularCore') {
            Add-UniquePluginName -List $enabledPluginNames -Name $targetPlugin.Name
            $stagedPluginNames += $targetPlugin.Name
        }

        New-Item -ItemType Directory -Path $stagePluginRoot -Force | Out-Null
        Copy-PluginSourceClean -Source $coreMetadata.Root -Destination (Join-Path $stagePluginRoot 'DocModularCore')
        if ($targetPlugin.Name -ne 'DocModularCore') {
            Copy-PluginSourceClean -Source $targetPlugin.Root -Destination (Join-Path $stagePluginRoot $targetPlugin.Name)
        }

        $actualStagedNames = @(Get-ChildItem -LiteralPath $stagePluginRoot -Directory | Sort-Object Name | Select-Object -ExpandProperty Name)
        $expectedStagedNames = @($stagedPluginNames | Sort-Object -Unique)
        if (($actualStagedNames -join '|') -ne ($expectedStagedNames -join '|')) {
            throw "Isolation stage has plugin directories [$($actualStagedNames -join ', ')]; expected only [$($expectedStagedNames -join ', ')]."
        }

        $sourceRoot = Join-Path $buildStageRoot 'Source\DocIsolationHost'
        $publicHeadersToCompile = @($coreMetadata.PublicHeaders)
        if ($targetPlugin.Name -ne 'DocModularCore') {
            $publicHeadersToCompile += @($targetPlugin.PublicHeaders)
        }

        $headerIndex = 0
        foreach ($header in $publicHeadersToCompile) {
            $moduleMetadata = if ($header.FullName.StartsWith($coreMetadata.PublicRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                $coreMetadata
            }
            else {
                $targetPlugin
            }
            $relativeHeader = $header.FullName.Substring($moduleMetadata.PublicRoot.Length).TrimStart('\').Replace('\', '/')
            $consumerHeaders.Add(($moduleMetadata.Name + '/' + $relativeHeader))
            $consumerName = 'DocIsolationHeader_{0:D4}.cpp' -f $headerIndex
            Write-HostFile -Path (Join-Path $sourceRoot ('Private\' + $consumerName)) -Content ("// Generated external consumer for $($moduleMetadata.Name).`r`n#include `"$relativeHeader`"`r`n")
            $headerIndex++
        }

        $privateDependencies = @('GameplayTags', $coreMetadata.RuntimeModule)
        if ($targetPlugin.Name -ne 'DocModularCore') {
            $privateDependencies += $targetPlugin.RuntimeModule
        }
        $privateDependencyLines = ($privateDependencies | ForEach-Object { '            "' + $_ + '"' }) -join ",`r`n"
        $buildCs = @"
using UnrealBuildTool;

public class DocIsolationHost : ModuleRules
{
    public DocIsolationHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
$privateDependencyLines
        });
    }
}
"@
        Write-HostFile -Path (Join-Path $sourceRoot 'DocIsolationHost.Build.cs') -Content $buildCs
        Write-HostFile -Path (Join-Path $sourceRoot 'Private\DocIsolationHost.cpp') -Content "#include `"Modules/ModuleManager.h`"`r`n`r`nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, DocIsolationHost, `"DocIsolationHost`");`r`n"

        $targetDir = Join-Path $buildStageRoot 'Source'
        $editorTargetCs = @'
using UnrealBuildTool;

public class DocIsolationHostEditorTarget : TargetRules
{
    public DocIsolationHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("DocIsolationHost");
    }
}
'@
        Write-HostFile -Path (Join-Path $targetDir 'DocIsolationHostEditor.Target.cs') -Content $editorTargetCs

        $projectPlugins = [System.Collections.Generic.List[object]]::new()
        foreach ($enabledName in $enabledPluginNames) {
            $projectPlugins.Add([ordered]@{ Name = $enabledName; Enabled = $true })
        }
        $hostProjectData = [ordered]@{
            FileVersion = $uprojectTemplate.FileVersion
            EngineAssociation = $uprojectTemplate.EngineAssociation
            Category = 'DocModular Isolation Verification'
            Description = "Generated isolated consumer for $($targetPlugin.Name)."
            Modules = @([ordered]@{ Name = 'DocIsolationHost'; Type = 'Runtime'; LoadingPhase = 'Default' })
            Plugins = @($projectPlugins.ToArray())
        }
        $hostProjectJson = $hostProjectData | ConvertTo-Json -Depth 12
        Write-HostFile -Path $stageProject -Content ($hostProjectJson + "`r`n")

        $argumentList = @('DocIsolationHostEditor', 'Win64', 'Development', "-Project=`"$stageProject`"", '-WaitMutex', '-DisableUnity')
        $command = "`"$buildBat`" " + ($argumentList -join ' ')
        Write-Host "`n=== Isolation build: $($targetPlugin.Name) ===" -ForegroundColor Cyan
        Write-Host "Present DocModular plugins: $($actualStagedNames -join ', '); external public headers: $($consumerHeaders.Count)"
        $exitCode = Invoke-DocProcess -FilePath $buildBat -ArgumentString ($argumentList -join ' ') -StdOutPath $buildLog -TimeoutMinutes $TimeoutMinutes
        if ($null -eq $exitCode) {
            $result = 'TimedOut'
            $reason = "Build timed out after $TimeoutMinutes minutes."
        }
        elseif ($exitCode -eq 0) {
            $result = if ($siblingDependencies.Count -eq 0) { 'Passed' } else { 'Failed' }
            if ($siblingDependencies.Count -gt 0) { $reason = 'Descriptor declares sibling dependencies: ' + ($siblingDependencies -join ', ') }
        }
        else {
            $result = 'Failed'
        }
    }
    catch {
        $reason = $_.Exception.Message
        $result = 'Failed'
    }

    $pluginEnd = Get-Date
    $results.Add([ordered]@{
        plugin = $targetPlugin.Name
        runtimeModule = $targetPlugin.RuntimeModule
        result = $result
        exitCode = $exitCode
        reason = $reason
        command = $command
        sourceSha256 = $sourceHash
        verifierSha256 = $verifierHash
        engineVersion = $engine.Version
        changelist = $engine.Changelist
        target = 'DocIsolationHostEditor'
        configuration = 'Development'
        unity = $false
        buildProject = $stageProject
        siblingDescriptorDependencies = @($siblingDependencies.ToArray())
        presentDocModularPlugins = @($stagedPluginNames)
        projectEnabledPlugins = @($enabledPluginNames.ToArray())
        consumerHeaderCount = $consumerHeaders.Count
        consumerHeaders = @($consumerHeaders.ToArray())
        start = $pluginStart.ToString('o')
        end = $pluginEnd.ToString('o')
        log = $buildLog
        stagedHost = $stageRoot
    })
    Save-IsolationSummary -RunResult 'InProgress'
    $exitDisplay = if ($null -eq $exitCode) { 'not run' } else { $exitCode }
    Write-Host "Result: $result (exit $exitDisplay). Evidence will be in $runRoot\summary.json"
}

$end = Get-Date
$passed = @($results | Where-Object { $_.result -eq 'Passed' }).Count
$failed = $results.Count - $passed
$overall = if ($failed -eq 0 -and $results.Count -eq $targets.Count) { 'Passed' } else { 'Failed' }
$summaryPath = Join-Path $runRoot 'summary.json'
Save-IsolationSummary -RunResult $overall
Write-Host "`n=== Isolation summary ===" -ForegroundColor Cyan
Write-Host "${overall}: $passed/$($targets.Count) builds passed; evidence: $summaryPath"
if ($overall -ne 'Passed') { $global:LASTEXITCODE = 1 }
else { $global:LASTEXITCODE = 0 }
}
finally {
    if ($mappedDrive) {
        & (Join-Path $env:SystemRoot 'System32\subst.exe') ($mappedDrive + ':') '/D' | Out-Null
    }
}
exit $global:LASTEXITCODE
