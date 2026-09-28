<#
.SYNOPSIS
  Validate the DocModular workspace structure and Unreal Engine prerequisites.
.DESCRIPTION
  Performs read-only preflight checks for the repository host, the 41 base plugin
  descriptors, the base dependency boundary, required local scripts, and the
  installed Unreal Engine version. Writes one summary.json under Scripts/Output.
  This does not build, run automation, validate authored assets, or prove runtime,
  cooked, networking, presentation, or performance behavior.
.EXAMPLE
  .\Scripts\Validate-Workspace.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [string]$ProjectFile,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $ProjectFile) { $ProjectFile = Join-Path $repoRoot 'DocModularDev.uproject' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$runRoot = New-DocRunDirectory -OutputRoot $OutputRoot -Label 'WorkspaceValidation'
$summaryPath = Join-Path $runRoot 'summary.json'
$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$verifierHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$start = Get-Date
$checks = [System.Collections.Generic.List[object]]::new()
$engine = $null

function Add-WorkspaceCheck {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [bool]$Passed,
        [Parameter(Mandatory)] [string]$Details
    )
    $script:checks.Add([ordered]@{
        name = $Name
        result = if ($Passed) { 'Passed' } else { 'Failed' }
        details = $Details
    })
}

try {
    $engine = Resolve-DocEngine -EngineRoot $EngineRoot
    $enginePassed = $engine.Version -eq '5.8.3' -and $engine.Changelist -eq 58210709
    Add-WorkspaceCheck -Name 'EngineIdentity' -Passed $enginePassed -Details ("Found Unreal Engine {0}, CL {1}, branch {2}; expected 5.8.3 CL 58210709." -f $engine.Version, $engine.Changelist, $engine.BranchName)

    $engineTools = @(
        (Join-Path $engine.Root 'Engine\Build\BatchFiles\Build.bat'),
        (Join-Path $engine.Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'),
        (Join-Path $engine.Root 'Engine\Build\BatchFiles\RunUAT.bat')
    )
    $missingTools = @($engineTools | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    Add-WorkspaceCheck -Name 'EngineTools' -Passed ($missingTools.Count -eq 0) -Details $(if ($missingTools.Count -eq 0) { 'Build.bat, UnrealEditor-Cmd.exe and RunUAT.bat are present.' } else { 'Missing: ' + ($missingTools -join ', ') })
}
catch {
    Add-WorkspaceCheck -Name 'EngineIdentity' -Passed $false -Details $_.Exception.Message
    Add-WorkspaceCheck -Name 'EngineTools' -Passed $false -Details 'Skipped because the engine root could not be resolved.'
}

$project = $null
try {
    $project = Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json
    $projectValid = $project.FileVersion -ge 3 -and @($project.Modules).Count -gt 0
    $associationValid = [string]$project.EngineAssociation -eq '5.8'
    Add-WorkspaceCheck -Name 'HostProject' -Passed ($projectValid -and $associationValid) -Details ("Parsed $ProjectFile; EngineAssociation=$($project.EngineAssociation), modules=$(@($project.Modules).Count). Expected association 5.8 and at least one module.")
}
catch {
    Add-WorkspaceCheck -Name 'HostProject' -Passed $false -Details $_.Exception.Message
}

$pluginRoot = Join-Path $repoRoot 'Plugins\DocModular'
$pluginMetadata = [System.Collections.Generic.List[object]]::new()
$descriptorErrors = [System.Collections.Generic.List[string]]::new()
if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
    $descriptorErrors.Add("Missing plugin grouping directory: $pluginRoot")
}
else {
    $rootDescriptors = @(Get-ChildItem -LiteralPath $pluginRoot -Filter '*.uplugin' -File)
    if ($rootDescriptors.Count -ne 0) { $descriptorErrors.Add('Plugins/DocModular must remain a descriptor-free grouping directory.') }
    foreach ($directory in (Get-ChildItem -LiteralPath $pluginRoot -Directory | Sort-Object Name)) {
        $descriptors = @(Get-ChildItem -LiteralPath $directory.FullName -Filter '*.uplugin' -File)
        if ($descriptors.Count -ne 1) {
            $descriptorErrors.Add("$($directory.Name): expected one .uplugin, found $($descriptors.Count).")
            continue
        }
        try {
            $descriptor = Get-Content -LiteralPath $descriptors[0].FullName -Raw | ConvertFrom-Json
            $runtimeModules = @($descriptor.Modules | Where-Object { $_.Type -eq 'Runtime' })
            if ($descriptors[0].BaseName -ne $directory.Name) {
                $descriptorErrors.Add("$($directory.Name): descriptor filename does not match its plugin directory.")
            }
            if ($runtimeModules.Count -ne 1) {
                $descriptorErrors.Add("$($directory.Name): expected one Runtime module, found $($runtimeModules.Count).")
                continue
            }
            $pluginMetadata.Add([pscustomobject]@{
                Name = $descriptors[0].BaseName
                Directory = $directory.FullName
                RuntimeModule = [string]$runtimeModules[0].Name
                Descriptor = $descriptor
            })
        }
        catch {
            $descriptorErrors.Add("$($directory.Name): invalid plugin descriptor: $($_.Exception.Message)")
        }
    }
}

$expectedPluginCount = 41
$pluginCountPassed = $pluginMetadata.Count -eq $expectedPluginCount -and $descriptorErrors.Count -eq 0
Add-WorkspaceCheck -Name 'PluginDescriptors' -Passed $pluginCountPassed -Details $(if ($pluginCountPassed) { "Found $expectedPluginCount plugins, each with one descriptor and one Runtime module; grouping directory is descriptor-free." } else { 'Validated ' + $pluginMetadata.Count + ' plugin descriptors; ' + ($descriptorErrors -join ' ') })

$dependencyErrors = [System.Collections.Generic.List[string]]::new()
$core = $pluginMetadata | Where-Object Name -eq 'DocModularCore' | Select-Object -First 1
if (-not $core) { $dependencyErrors.Add('DocModularCore is missing.') }
else {
    $featureModules = @($pluginMetadata | Where-Object Name -ne 'DocModularCore')
    foreach ($feature in $featureModules) {
        $declared = @()
        if ($feature.Descriptor.PSObject.Properties['Plugins']) { $declared = @($feature.Descriptor.Plugins) }
        foreach ($reference in $declared) {
            if ($reference.PSObject.Properties['Enabled'] -and $reference.Enabled -eq $false) { continue }
            $name = [string]$reference.Name
            if ($name -like 'Doc*' -and $name -ne 'DocModularCore' -and $name -ne $feature.Name) {
                $dependencyErrors.Add("$($feature.Name) descriptor depends on sibling plugin $name.")
            }
        }
        $buildFiles = @(Get-ChildItem -LiteralPath $feature.Directory -Filter '*.Build.cs' -File -Recurse)
        foreach ($buildFile in $buildFiles) {
            $buildText = Get-Content -LiteralPath $buildFile.FullName -Raw
            foreach ($sibling in ($pluginMetadata | Where-Object { $_.Name -ne 'DocModularCore' -and $_.Name -ne $feature.Name })) {
                if ($buildText.Contains($sibling.RuntimeModule, [System.StringComparison]::Ordinal)) {
                    $dependencyErrors.Add("$($feature.Name) Build.cs references sibling module $($sibling.RuntimeModule) in $($buildFile.Name).")
                }
            }
        }
    }
}
$dependencyPassed = $dependencyErrors.Count -eq 0 -and $null -ne $core
Add-WorkspaceCheck -Name 'BaseDependencyBoundary' -Passed $dependencyPassed -Details $(if ($dependencyPassed) { 'No feature descriptor or Build.cs declares another DocModular feature; Core is present.' } else { $dependencyErrors -join ' ' })

if ($project) {
    $expectedNames = @($pluginMetadata | Select-Object -ExpandProperty Name | Sort-Object -Unique)
    $enabledNames = @($project.Plugins | Where-Object Enabled -eq $true | Select-Object -ExpandProperty Name | Sort-Object -Unique)
    $missingNames = @($expectedNames | Where-Object { $_ -notin $enabledNames })
    $unknownNames = @($enabledNames | Where-Object { $_ -like 'Doc*' -and $_ -notin $expectedNames })
    $pluginsValid = $expectedNames.Count -eq $expectedPluginCount -and $missingNames.Count -eq 0 -and $unknownNames.Count -eq 0
    $projectDetails = if ($pluginsValid) { "Host enables all $expectedPluginCount repository plugins." } else { 'Missing DocModular plugins: ' + ($missingNames -join ', ') + '; unknown enabled Doc plugins: ' + ($unknownNames -join ', ') }
    Add-WorkspaceCheck -Name 'HostPluginSet' -Passed $pluginsValid -Details $projectDetails
}
else {
    Add-WorkspaceCheck -Name 'HostPluginSet' -Passed $false -Details 'Skipped because the host project could not be parsed.'
}

$configPath = Join-Path $repoRoot 'Config\DefaultEngine.ini'
if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    $configText = Get-Content -LiteralPath $configPath -Raw
    $securityTokenMatch = [regex]::Match($configText, '(?im)^[ \t]*SecurityToken[ \t]*=[ \t]*(?<value>[^\r\n]*)')
    $connectionMatch = [regex]::Match($configText, '(?im)^[ \t]*bAllowNetworkConnection[ \t]*=[ \t]*(?<value>[^\r\n]*)')
    $tokenEmpty = $securityTokenMatch.Success -and [string]::IsNullOrWhiteSpace($securityTokenMatch.Groups['value'].Value)
    $connectionsDisabled = $connectionMatch.Success -and $connectionMatch.Groups['value'].Value.Trim() -eq 'False'
    Add-WorkspaceCheck -Name 'AndroidFileServerConfig' -Passed ($tokenEmpty -and $connectionsDisabled) -Details $(if ($tokenEmpty -and $connectionsDisabled) { 'SecurityToken is empty and bAllowNetworkConnection=False.' } else { 'Expected an empty SecurityToken and bAllowNetworkConnection=False in Config/DefaultEngine.ini.' })
}
else {
    Add-WorkspaceCheck -Name 'AndroidFileServerConfig' -Passed $false -Details "Missing $configPath"
}

$requiredScripts = @('Build-Host.ps1', 'Run-Automation.ps1', 'Verify-Suite.ps1', 'Package-Host.ps1', 'Verify-PluginIsolation.ps1', 'Verify-IsolatedStartup.ps1', 'Record-ManualGate.ps1')
$missingScripts = @($requiredScripts | Where-Object { -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $_) -PathType Leaf) })
Add-WorkspaceCheck -Name 'VerificationWrappers' -Passed ($missingScripts.Count -eq 0) -Details $(if ($missingScripts.Count -eq 0) { 'Build, automation, suite, packaging, isolation, startup, and manual-gate wrappers are present.' } else { 'Missing: ' + ($missingScripts -join ', ') })

$end = Get-Date
$failedCount = @($checks | Where-Object { $_.result -eq 'Failed' }).Count
$overall = if ($failedCount -eq 0) { 'Passed' } else { 'Failed' }
$exitCode = if ($overall -eq 'Passed') { 0 } else { 1 }
$summary = [ordered]@{
    check = 'WorkspaceValidation'
    result = $overall
    exitCode = $exitCode
    command = $MyInvocation.Line
    sourceSha256 = $sourceHash
    verifierSha256 = $verifierHash
    project = $ProjectFile
    engineVersion = if ($engine) { $engine.Version } else { $null }
    changelist = if ($engine) { $engine.Changelist } else { $null }
    engineBranch = if ($engine) { $engine.BranchName } else { $null }
    start = $start.ToString('o')
    end = $end.ToString('o')
    passed = $checks.Count - $failedCount
    failed = $failedCount
    checks = @($checks.ToArray())
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

foreach ($check in $checks) {
    $color = if ($check.result -eq 'Passed') { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1}: {2}" -f $check.result, $check.name, $check.details) -ForegroundColor $color
}
Write-Host "Workspace validation $overall; summary: $summaryPath"
exit $exitCode
