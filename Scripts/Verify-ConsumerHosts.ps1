<#
.SYNOPSIS
  Build and launch two structurally different external DocModular consumer projects.
.DESCRIPTION
  Checks plugin discovery for a broad suite consumer and a minimal Core + Interaction
  consumer. It builds both as non-unity Editor targets. The minimal host is staged with
  clean local copies of only Core and Interaction, then launched headlessly; it must
  complete an instant interaction between plain AActor subclasses.
  A summary.json records source identity, discovered plugins, module loads, and run results.
.EXAMPLE
  .\Scripts\Verify-ConsumerHosts.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [ValidateRange(1, 180)] [int]$BuildTimeoutMinutes = 90,
    [ValidateRange(1, 30)] [int]$LaunchTimeoutMinutes = 5,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$engine = Resolve-DocEngine -EngineRoot $EngineRoot
if ($engine.Version -ne '5.8.3' -or $engine.Changelist -ne 58210709) {
    throw "Expected Unreal Engine 5.8.3 CL 58210709; found $($engine.Version) CL $($engine.Changelist)."
}
$buildBat = Join-Path $engine.Root 'Engine\Build\BatchFiles\Build.bat'
$editorCmd = Join-Path $engine.Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
foreach ($tool in @($buildBat, $editorCmd)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required engine tool is missing: $tool" }
}
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
$runRoot = New-DocRunDirectory -OutputRoot $OutputRoot -Label 'CH'
$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$verifierHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$summaryPath = Join-Path $runRoot 'summary.json'
$started = Get-Date
$results = [System.Collections.Generic.List[object]]::new()

function Save-ConsumerSummary {
    param([Parameter(Mandatory)] [string]$Result)
    $passed = @($results | Where-Object result -eq 'Passed').Count
    $summary = [ordered]@{
        check = 'ConsumerHosts'
        result = $Result
        sourceSha256 = $sourceHash
        verifierSha256 = $verifierHash
        engineVersion = $engine.Version
        changelist = $engine.Changelist
        engineBranch = $engine.BranchName
        buildTimeoutMinutes = $BuildTimeoutMinutes
        launchTimeoutMinutes = $LaunchTimeoutMinutes
        expectedHosts = 2
        completedHosts = $results.Count
        passed = $passed
        failed = $results.Count - $passed
        start = $started.ToString('o')
        end = if ($Result -in @('Passed', 'Failed')) { (Get-Date).ToString('o') } else { $null }
        runDirectory = $runRoot
        results = @($results.ToArray())
    }
    [System.IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 12), [System.Text.UTF8Encoding]::new($false))
}

function Get-EnabledDocPlugins {
    param([Parameter(Mandatory)] $Project)
    @($Project.Plugins | Where-Object { $_.Enabled -eq $true -and $_.Name -like 'Doc*' } | Select-Object -ExpandProperty Name | Sort-Object -Unique)
}

function Get-PluginInventory {
    param([Parameter(Mandatory)] [string[]]$Roots)
    $descriptors = foreach ($root in $Roots) {
        Get-ChildItem -LiteralPath $root -Filter '*.uplugin' -File -Recurse
    }
    @($descriptors | ForEach-Object { $_.BaseName } | Sort-Object -Unique)
}

function Copy-SourceTree {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Destination
    )
    $sourceRoot = (Resolve-Path -LiteralPath $Source).Path
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $excludedPath = '\\(Binaries|Intermediate|Saved|DerivedDataCache|\.git|\.vs)\\'
    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Force) {
        if ($file.FullName -match $excludedPath) { continue }
        $relativePath = $file.FullName.Substring($sourceRoot.Length).TrimStart('\')
        $destinationFile = Join-Path $Destination $relativePath
        $destinationDirectory = Split-Path -Parent $destinationFile
        if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destinationFile
    }
}

function Get-TreeFingerprint {
    param([Parameter(Mandatory)] [string]$Root)
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $excludedPath = '\\(Binaries|Intermediate|Saved|DerivedDataCache|\.git|\.vs)\\'
    $files = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force |
        Where-Object { $_.FullName -notmatch $excludedPath } |
        Sort-Object FullName)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in $files) {
            $relativePath = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\').Replace('\', '/')
            $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($relativePath + "`n")
            [void]$sha.TransformBlock($nameBytes, 0, $nameBytes.Length, $null, 0)
            $contents = [System.IO.File]::ReadAllBytes($file.FullName)
            [void]$sha.TransformBlock($contents, 0, $contents.Length, $null, 0)
        }
        [void]$sha.TransformFinalBlock([byte[]]::new(0), 0, 0)
        return ([System.BitConverter]::ToString($sha.Hash)).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

$pluginRoot = Join-Path $repoRoot 'Plugins\DocModular'
$allPluginNames = @(Get-ChildItem -LiteralPath $pluginRoot -Directory | Sort-Object Name | Select-Object -ExpandProperty Name)
$hosts = @(
    [pscustomobject]@{
        Name = 'CppConsumer'
        Project = Join-Path $repoRoot 'Samples\CppConsumer\DocCppConsumer.uproject'
        ProjectTemplate = $null
        Target = 'DocCppConsumerEditor'
        ExpectedPlugins = $allPluginNames
        StageLocalPlugins = $false
        RunStartup = $false
        InteractionMarker = $null
    },
    [pscustomobject]@{
        Name = 'NonCharacterInteraction'
        Project = $null
        ProjectTemplate = Join-Path $repoRoot 'Samples\NonCharacterInteraction\Project'
        Target = 'NonCharacterInteractionEditor'
        ExpectedPlugins = @('DocModularCore', 'DocInteraction')
        StageLocalPlugins = $true
        RunStartup = $true
        InteractionMarker = 'DOC_NONCHARACTER_INTERACTION_PASSED:'
    }
)

Save-ConsumerSummary -Result 'InProgress'
foreach ($hostSpec in $hosts) {
    $hostResult = 'Failed'
    $failureReason = $null
    $projectJson = $null
    $resolvedPluginRoots = @()
    $pluginSearchMode = $null
    $discoveredPlugins = @()
    $pluginSourceMatches = @()
    $runtimeModules = @()
    $loadedModules = @()
    $buildExitCode = $null
    $launchExitCode = $null
    $worldReachedPlay = $null
    $cleanShutdown = $null
    $interactionPassed = ($null -eq $hostSpec.InteractionMarker)
    $hostRunRoot = Join-Path $runRoot $hostSpec.Name
    New-Item -ItemType Directory -Path $hostRunRoot -Force | Out-Null
    $buildLog = Join-Path $hostRunRoot 'build.log'
    $startupLog = Join-Path $hostRunRoot 'startup.log'
    $buildCommand = $null
    $launchCommand = $null
    $projectFile = $null

    try {
        if ($hostSpec.StageLocalPlugins) {
            if (-not (Test-Path -LiteralPath $hostSpec.ProjectTemplate -PathType Container)) { throw "Project template is missing: $($hostSpec.ProjectTemplate)" }
            $projectDirectory = $hostRunRoot
            Copy-SourceTree -Source $hostSpec.ProjectTemplate -Destination $projectDirectory
            $projectFile = Join-Path $projectDirectory 'NonCharacterInteraction.uproject'
            $localPluginRoot = Join-Path $projectDirectory 'Plugins\DocModular'
            $pluginSourceMatches = foreach ($pluginName in $hostSpec.ExpectedPlugins) {
                $sourcePlugin = Join-Path $pluginRoot $pluginName
                $stagedPlugin = Join-Path $localPluginRoot $pluginName
                Copy-SourceTree -Source $sourcePlugin -Destination $stagedPlugin
                [pscustomobject]@{
                    plugin = $pluginName
                    source = $sourcePlugin
                    staged = $stagedPlugin
                    sourceSha256 = Get-TreeFingerprint -Root $sourcePlugin
                    stagedSha256 = Get-TreeFingerprint -Root $stagedPlugin
                    matches = (Get-TreeFingerprint -Root $sourcePlugin) -eq (Get-TreeFingerprint -Root $stagedPlugin)
                }
            }
            if (@($pluginSourceMatches | Where-Object { -not $_.matches }).Count -gt 0) {
                throw 'One or more locally staged plugin trees differ from the current source.'
            }
        }
        else {
            $projectFile = $hostSpec.Project
            $projectDirectory = Split-Path -Parent $projectFile
        }

        if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Project is missing: $projectFile" }
        $projectJson = Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json
        $additionalPluginDirectoryValues = @()
        if ($projectJson.PSObject.Properties['AdditionalPluginDirectories']) {
            $additionalPluginDirectoryValues = @($projectJson.AdditionalPluginDirectories)
        }
        if ($additionalPluginDirectoryValues.Count -gt 0) {
            $pluginSearchMode = 'AdditionalPluginDirectories'
            $resolvedPluginRoots = @($additionalPluginDirectoryValues | ForEach-Object {
                (Resolve-Path -LiteralPath (Join-Path $projectDirectory $_)).Path
            })
        }
        else {
            $pluginSearchMode = 'ProjectPlugins'
            $projectPluginRoot = Join-Path $projectDirectory 'Plugins'
            if (-not (Test-Path -LiteralPath $projectPluginRoot -PathType Container)) { throw "Project plugin root is missing: $projectPluginRoot" }
            $resolvedPluginRoots = @((Resolve-Path -LiteralPath $projectPluginRoot).Path)
        }

        $discoveredPlugins = Get-PluginInventory -Roots $resolvedPluginRoots
        $expectedPlugins = @($hostSpec.ExpectedPlugins | Sort-Object -Unique)
        if (($discoveredPlugins -join '|') -ne ($expectedPlugins -join '|')) {
            throw "Plugin discovery found [$($discoveredPlugins -join ', ')], expected [$($expectedPlugins -join ', ')]."
        }
        $enabledPlugins = Get-EnabledDocPlugins -Project $projectJson
        if (($enabledPlugins -join '|') -ne ($expectedPlugins -join '|')) {
            throw "Enabled project plugins [$($enabledPlugins -join ', ')] differ from expected [$($expectedPlugins -join ', ')]."
        }

        foreach ($pluginName in $discoveredPlugins) {
            $descriptorFile = Get-ChildItem -LiteralPath $resolvedPluginRoots -Filter ($pluginName + '.uplugin') -File -Recurse | Select-Object -First 1
            if (-not $descriptorFile) { throw "Missing descriptor for discovered plugin $pluginName." }
            $descriptor = Get-Content -LiteralPath $descriptorFile.FullName -Raw | ConvertFrom-Json
            $runtimeModules += @($descriptor.Modules | Where-Object Type -eq 'Runtime' | ForEach-Object { [string]$_.Name })
        }
        $runtimeModules = @($runtimeModules | Sort-Object -Unique)

        $buildArgs = "$($hostSpec.Target) Win64 Development -Project=`"$projectFile`" -WaitMutex -DisableUnity"
        $buildCommand = "`"$buildBat`" $buildArgs"
        $buildExitCode = Invoke-DocProcess -FilePath $buildBat -ArgumentString $buildArgs -StdOutPath $buildLog -TimeoutMinutes $BuildTimeoutMinutes
        if ($null -eq $buildExitCode -or $buildExitCode -ne 0) {
            throw "Non-unity Editor build failed or timed out (exit $buildExitCode)."
        }

        if ($hostSpec.RunStartup) {
            $worldReachedPlay = $false
            $cleanShutdown = $false
            $launchArgs = @(
                "-project=`"$projectFile`"",
                '-game', '-nullrhi', '-nosound', '-unattended', '-nop4', '-nosplash',
                '-NoHotReload', '-NoLiveCoding', '-stdout', '-FullStdOutLogOutput', '-ExecCmds=Quit'
            )
            $launchCommand = "`"$editorCmd`" " + ($launchArgs -join ' ')
            $launchExitCode = Invoke-DocProcess -FilePath $editorCmd -ArgumentString ($launchArgs -join ' ') -StdOutPath $startupLog -TimeoutMinutes $LaunchTimeoutMinutes
            if ($null -eq $launchExitCode) { throw "Headless launch timed out after $LaunchTimeoutMinutes minute(s)." }
            if (-not (Test-Path -LiteralPath $startupLog -PathType Leaf)) { throw 'Headless launch produced no captured log.' }

            $logText = Get-Content -LiteralPath $startupLog -Raw
            $loadedModules = @($runtimeModules | Where-Object { $logText.Contains("InternalLoadLibrary: '$_'", [System.StringComparison]::Ordinal) })
            $worldReachedPlay = $logText -match 'LogWorld: Bringing World .* up for play'
            $cleanShutdown = $logText -match 'LogExit: Exiting\.' -and $logText -match 'FPlatformMisc::RequestExit\(0'
            $fatal = $logText -match 'Fatal error:|Unhandled Exception'
            if ($hostSpec.InteractionMarker) { $interactionPassed = $logText.Contains($hostSpec.InteractionMarker, [System.StringComparison]::Ordinal) }
            if ($launchExitCode -ne 0 -or $loadedModules.Count -ne $runtimeModules.Count -or -not $worldReachedPlay -or -not $cleanShutdown -or $fatal -or -not $interactionPassed) {
                $missing = @($runtimeModules | Where-Object { $_ -notin $loadedModules })
                throw ('Startup checks failed: ' + (@(
                    if ($launchExitCode -ne 0) { "exit code $launchExitCode" }
                    if ($missing.Count -gt 0) { 'modules not loaded: ' + ($missing -join ', ') }
                    if (-not $worldReachedPlay) { 'world did not reach play' }
                    if (-not $cleanShutdown) { 'clean shutdown marker missing' }
                    if ($fatal) { 'fatal error marker present' }
                    if (-not $interactionPassed) { 'non-Character interaction marker missing' }
                ) -join '; '))
            }
        }
        $hostResult = 'Passed'
    }
    catch {
        $failureReason = $_.Exception.Message
    }

    $results.Add([ordered]@{
        host = $hostSpec.Name
        result = $hostResult
        reason = $failureReason
        project = $projectFile
        projectTemplate = $hostSpec.ProjectTemplate
        target = $hostSpec.Target
        pluginSearchMode = $pluginSearchMode
        pluginSearchRoots = @($resolvedPluginRoots)
        additionalPluginDirectories = @($additionalPluginDirectoryValues)
        pluginSourceSnapshots = @($pluginSourceMatches)
        discoveredPlugins = @($discoveredPlugins)
        enabledPlugins = if ($projectJson) { @(Get-EnabledDocPlugins -Project $projectJson) } else { @() }
        runtimeModules = @($runtimeModules)
        loadedRuntimeModules = @($loadedModules)
        sourceSha256 = $sourceHash
        buildExitCode = $buildExitCode
        buildCommand = $buildCommand
        buildLog = $buildLog
        launchExitCode = $launchExitCode
        launchCommand = $launchCommand
        worldReachedPlay = $worldReachedPlay
        cleanShutdown = $cleanShutdown
        runtimeCheck = if ($hostSpec.RunStartup) { 'Executed' } else { 'BuildOnly' }
        nonCharacterInteractionPassed = if ($hostSpec.InteractionMarker) { $interactionPassed } else { $null }
        startupLog = $startupLog
    })
    Save-ConsumerSummary -Result 'InProgress'
    Write-Host ("{0}: {1} (build {2}, launch {3})" -f $hostSpec.Name, $hostResult, $buildExitCode, $launchExitCode)
    if ($failureReason) { Write-Host "  $failureReason" -ForegroundColor Yellow }
}

$passedCount = @($results | Where-Object result -eq 'Passed').Count
$overall = if ($passedCount -eq $hosts.Count) { 'Passed' } else { 'Failed' }
Save-ConsumerSummary -Result $overall
Write-Host "Consumer host verification ${overall}: $passedCount/$($hosts.Count); summary: $summaryPath"
if ($overall -ne 'Passed') { exit 1 }
exit 0
