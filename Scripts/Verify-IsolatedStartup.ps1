<#
.SYNOPSIS
  Launch already-built physical-absence hosts and verify runtime module startup.
.DESCRIPTION
  Reads a passed Verify-PluginIsolation summary, confirms each staged plugin source
  still matches the repository, then launches each host with UnrealEditor-Cmd in
  headless game mode. A pass requires expected modules loaded, an Unreal world
  reaching play, clean engine shutdown, and process exit code zero. This is a
  startup smoke check, not feature-behavior, cooked-content, or packaging evidence.
.EXAMPLE
  .\Scripts\Verify-IsolatedStartup.ps1 -EngineRoot 'C:\Program Files\UE_5.8' `
      -IsolationSummary '.\Scripts\Output\<run>\summary.json'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [Parameter(Mandatory)] [string]$IsolationSummary,
    [string]$PluginName,
    [ValidateRange(1, 30)] [int]$TimeoutMinutes = 3,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$engine = Resolve-DocEngine -EngineRoot $EngineRoot
if ($engine.Version -ne '5.8.3' -or $engine.Changelist -ne 58210709) {
    throw "Expected Unreal Engine 5.8.3 CL 58210709; found $($engine.Version) CL $($engine.Changelist)."
}
$editorCmd = Join-Path $engine.Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorCmd -PathType Leaf)) { throw "UnrealEditor-Cmd.exe not found: $editorCmd" }
$IsolationSummary = (Resolve-Path -LiteralPath $IsolationSummary).Path
$isolation = Get-Content -LiteralPath $IsolationSummary -Raw | ConvertFrom-Json
if ($isolation.check -ne 'PluginIsolation' -or $isolation.result -ne 'Passed') {
    throw "Isolation summary must be a passed PluginIsolation run: $IsolationSummary"
}
if ($isolation.completedPlugins -ne $isolation.expectedPlugins -or $isolation.failed -ne 0) {
    throw "Isolation summary is incomplete or contains failures: $IsolationSummary"
}

$targets = @($isolation.results | Where-Object { $_.result -eq 'Passed' })
if ($PluginName) {
    $targets = @($targets | Where-Object plugin -eq $PluginName)
    if ($targets.Count -ne 1) { throw "Plugin '$PluginName' is absent from the passed isolation summary." }
}
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
$runRoot = New-DocRunDirectory -OutputRoot $OutputRoot -Label 'IsolatedStartup'
$summaryPath = Join-Path $runRoot 'summary.json'
$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$verifierHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$invocationLine = $MyInvocation.Line
$start = Get-Date
$results = [System.Collections.Generic.List[object]]::new()

function Get-PluginTreeFingerprint {
    param([Parameter(Mandatory)] [string]$Root)
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $exclude = '\\(Binaries|Intermediate|Saved|DerivedDataCache|\.git)\\'
    $files = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force |
        Where-Object { $_.FullName -notmatch $exclude } |
        Sort-Object FullName)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\').Replace('\', '/')
            $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($relative + "`n")
            [void]$sha.TransformBlock($nameBytes, 0, $nameBytes.Length, $null, 0)
            $content = [System.IO.File]::ReadAllBytes($file.FullName)
            [void]$sha.TransformBlock($content, 0, $content.Length, $null, 0)
        }
        [void]$sha.TransformFinalBlock([byte[]]::new(0), 0, 0)
        return ([System.BitConverter]::ToString($sha.Hash)).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

function Test-HostPluginSnapshot {
    param(
        [Parameter(Mandatory)] $Target,
        [Parameter(Mandatory)] [string]$BuildProject
    )
    $hostRoot = Split-Path -Parent $BuildProject
    $hostPluginRoot = Join-Path $hostRoot 'Plugins\DocModular'
    if (-not (Test-Path -LiteralPath $BuildProject -PathType Leaf)) {
        throw "Isolated host project is missing: $BuildProject"
    }
    if (-not (Test-Path -LiteralPath $hostPluginRoot -PathType Container)) {
        throw "Staged plugin directory is missing: $hostPluginRoot"
    }
    $actualNames = @(Get-ChildItem -LiteralPath $hostPluginRoot -Directory | Select-Object -ExpandProperty Name | Sort-Object -Unique)
    $expectedNames = @($Target.presentDocModularPlugins | Sort-Object -Unique)
    if (($actualNames -join '|') -ne ($expectedNames -join '|')) {
        throw "Staged plugin set [$($actualNames -join ', ')] differs from isolation evidence [$($expectedNames -join ', ')]."
    }
    foreach ($name in $expectedNames) {
        $sourcePlugin = Join-Path $repoRoot ("Plugins\DocModular\$name")
        $stagedPlugin = Join-Path $hostPluginRoot $name
        if ((Get-PluginTreeFingerprint -Root $sourcePlugin) -ne (Get-PluginTreeFingerprint -Root $stagedPlugin)) {
            throw "Staged plugin source differs from current repository source: $name"
        }
    }
    $hostProject = Get-Content -LiteralPath $BuildProject -Raw | ConvertFrom-Json
    $enabledDocPlugins = @($hostProject.Plugins | Where-Object { $_.Enabled -eq $true -and $_.Name -like 'Doc*' } | Select-Object -ExpandProperty Name | Sort-Object -Unique)
    if (($enabledDocPlugins -join '|') -ne ($expectedNames -join '|')) {
        throw "Enabled Doc plugins [$($enabledDocPlugins -join ', ')] do not match staged plugins [$($expectedNames -join ', ')]."
    }
    return $hostPluginRoot
}

function Save-StartupSummary {
    param([Parameter(Mandatory)] [string]$Result)
    $passed = @($results | Where-Object { $_.result -eq 'Passed' }).Count
    $summary = [ordered]@{
        check = 'IsolatedRuntimeStartup'
        result = $Result
        exitCode = if ($Result -eq 'InProgress') { $null } elseif ($Result -eq 'Passed') { 0 } else { 1 }
        command = $invocationLine
        sourceSha256 = $sourceHash
        isolationSourceSha256 = $isolation.sourceSha256
        verifierSha256 = $verifierHash
        engineVersion = $engine.Version
        changelist = $engine.Changelist
        engineBranch = $engine.BranchName
        isolationSummary = $IsolationSummary
        fullSuite = (-not [bool]$PluginName)
        expectedPlugins = $targets.Count
        completedPlugins = $results.Count
        passed = $passed
        failed = ($results.Count - $passed)
        start = $start.ToString('o')
        end = if ($Result -in @('Passed', 'Failed')) { (Get-Date).ToString('o') } else { $null }
        runDirectory = $runRoot
        results = @($results.ToArray())
    }
    $json = $summary | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($summaryPath, $json, [System.Text.UTF8Encoding]::new($false))
}

Save-StartupSummary -Result 'InProgress'
foreach ($target in $targets) {
    $hostStart = Get-Date
    $result = 'Failed'
    $exitCode = $null
    $reason = $null
    $moduleChecks = @()
    $worldStarted = $false
    $cleanShutdown = $false
    $hostSourceMatch = $false
    $hostPluginRoot = $null
    $buildProject = Join-Path (Join-Path $isolation.runDirectory $target.plugin) 'DocIsolationHost.uproject'
    $command = $null
    $hostDir = Join-Path $runRoot $target.plugin
    New-Item -ItemType Directory -Path $hostDir -Force | Out-Null
    $stdout = Join-Path $hostDir 'stdout.log'

    try {
        $hostPluginRoot = Test-HostPluginSnapshot -Target $target -BuildProject $buildProject
        $hostSourceMatch = $true
        $requiredModules = if ($target.plugin -eq 'DocModularCore') { @($target.runtimeModule) } else { @('DocModularCoreRuntime', $target.runtimeModule) }
        $argumentList = @(
            "-project=`"$buildProject`"",
            '-game', '-nullrhi', '-nosound', '-unattended', '-nop4', '-nosplash',
            '-stdout', '-FullStdOutLogOutput', '-ExecCmds=Quit'
        )
        $command = "`"$editorCmd`" " + ($argumentList -join ' ')
        $exitCode = Invoke-DocProcess -FilePath $editorCmd -ArgumentString ($argumentList -join ' ') -StdOutPath $stdout -TimeoutMinutes $TimeoutMinutes
        if ($null -eq $exitCode) {
            $result = 'TimedOut'
            $reason = "Startup timed out after $TimeoutMinutes minute(s)."
        }
        elseif (-not (Test-Path -LiteralPath $stdout -PathType Leaf)) {
            $reason = 'The startup process produced no captured log.'
        }
        else {
            $logText = Get-Content -LiteralPath $stdout -Raw
            $moduleChecks = @($requiredModules | ForEach-Object {
                [ordered]@{
                    name = $_
                    loaded = $logText.Contains("InternalLoadLibrary: '$_'", [System.StringComparison]::Ordinal)
                }
            })
            $worldStarted = $logText -match 'LogWorld: Bringing World .* up for play'
            $cleanShutdown = $logText -match 'LogExit: Exiting\.' -and $logText -match 'FPlatformMisc::RequestExit\(0'
            $fatal = $logText -match 'Fatal error:|Unhandled Exception'
            if ($exitCode -eq 0 -and @($moduleChecks | Where-Object { -not $_.loaded }).Count -eq 0 -and $worldStarted -and $cleanShutdown -and -not $fatal) {
                $result = 'Passed'
            }
            else {
                $reason = 'One or more startup gates failed: ' + (@(
                    if ($exitCode -ne 0) { "process exit code $exitCode" }
                    if (@($moduleChecks | Where-Object { -not $_.loaded }).Count -gt 0) { 'expected runtime module missing from log' }
                    if (-not $worldStarted) { 'no world reached play' }
                    if (-not $cleanShutdown) { 'clean shutdown marker missing' }
                    if ($fatal) { 'fatal error marker present' }
                ) -join '; ')
            }
        }
    }
    catch {
        $reason = $_.Exception.Message
    }

    $results.Add([ordered]@{
        plugin = $target.plugin
        runtimeModule = $target.runtimeModule
        result = $result
        exitCode = $exitCode
        reason = $reason
        sourceSnapshotMatchesCurrent = $hostSourceMatch
        presentDocModularPlugins = @($target.presentDocModularPlugins)
        loadedRuntimeModules = @($moduleChecks)
        worldReachedPlay = $worldStarted
        cleanShutdown = $cleanShutdown
        command = $command
        buildProject = $buildProject
        stagedPluginRoot = $hostPluginRoot
        start = $hostStart.ToString('o')
        end = (Get-Date).ToString('o')
        log = $stdout
    })
    Save-StartupSummary -Result 'InProgress'
    Write-Host ("{0}: {1} (exit {2})" -f $target.plugin, $result, $(if ($null -eq $exitCode) { 'not run' } else { $exitCode }))
    if ($reason) { Write-Host "  $reason" -ForegroundColor Yellow }
}

$passedCount = @($results | Where-Object { $_.result -eq 'Passed' }).Count
$overall = if ($passedCount -eq $targets.Count) { 'Passed' } else { 'Failed' }
Save-StartupSummary -Result $overall
Write-Host "Isolated startup ${overall}: $passedCount/$($targets.Count); summary: $summaryPath"
if ($overall -ne 'Passed') { exit 1 }
exit 0
