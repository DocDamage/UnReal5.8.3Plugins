<#
.SYNOPSIS
  Run DocModular automation tests headless and record evidence.
.DESCRIPTION
  Uses -NullRHI, so it is valid for logic tests only (Core, Events, pure algorithms).
  Presentation/audio suites must not be run through this wrapper and claimed as
  visual/audio verification. Fails when no tests were discovered or executed, or
  when any test failed, regardless of process exit code.
.EXAMPLE
  .\Scripts\Run-Automation.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -Filter 'Doc.Core'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [string]$ProjectFile,
    [string]$Filter = 'Doc.',
    [int]$ExpectedMinimumTests = 1,
    [int]$TimeoutMinutes = 30,
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\DocModularDev.uproject' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$engine = Resolve-DocEngine -EngineRoot $EngineRoot
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$repoRoot = Split-Path -Parent $ProjectFile
$editorCmd = Join-Path $engine.Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorCmd)) { throw "UnrealEditor-Cmd.exe not found: $editorCmd" }

$runDir = New-DocRunDirectory -OutputRoot $OutputRoot -Label ('Automation-' + ($Filter.TrimEnd('.')))
$reportDir = Join-Path $runDir 'Report'
$editorLog = Join-Path $runDir 'editor.log'
$stdout = Join-Path $runDir 'stdout.log'

$argString = @(
    "`"$ProjectFile`"",
    '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound', '-stdout', '-FullStdOutLogOutput',
    "-ExecCmds=`"Automation RunTests $Filter`"",
    "-TestExit=`"Automation Test Queue Empty`"",
    "-ReportExportPath=`"$reportDir`"",
    "-abslog=`"$editorLog`""
) -join ' '

$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$start = Get-Date
Write-Host "Engine $($engine.Version) CL $($engine.Changelist) | $editorCmd $argString"
$exit = Invoke-DocProcess -FilePath $editorCmd -ArgumentString $argString -StdOutPath $stdout -TimeoutMinutes $TimeoutMinutes
$end = Get-Date

$discovered = 0; $passed = 0; $failed = 0; $other = 0; $failedNames = @()
$index = Join-Path $reportDir 'index.json'
if (Test-Path -LiteralPath $index) {
    # The report JSON may carry a UTF-8 BOM; Get-Content handles it.
    $report = Get-Content -LiteralPath $index -Raw | ConvertFrom-Json
    $tests = if ($report.PSObject.Properties.Name -contains 'tests') { @($report.tests) } else { @() }
    foreach ($t in $tests) {
        $discovered++
        switch ([string]$t.state) {
            'Success' { $passed++ }
            'Fail'    { $failed++; $failedNames += [string]$t.fullTestPath }
            default   { $other++ }
        }
    }
}

$result =
    if ($null -eq $exit) { 'TimedOut' }
    elseif (-not (Test-Path -LiteralPath $index)) { 'Failed (no report)' }
    elseif ($discovered -lt $ExpectedMinimumTests) { 'Failed (too few tests discovered)' }
    elseif ($failed -gt 0 -or $other -gt 0) { 'Failed' }
    elseif ($exit -ne 0) { 'Failed (nonzero exit)' }
    else { 'Passed' }

$summary = [ordered]@{
    check         = 'Automation'
    result        = $result
    exitCode      = $exit
    filter        = $Filter
    discovered    = $discovered
    passed        = $passed
    failed        = $failed
    notPassedOrFailed = $other
    failedTests   = $failedNames
    command       = "`"$editorCmd`" $argString"
    engineVersion = $engine.Version
    changelist    = $engine.Changelist
    rhi           = 'NullRHI (logic-only)'
    sourceSha256  = $sourceHash
    start         = $start.ToString('o')
    end           = $end.ToString('o')
    report        = $reportDir
    log           = $editorLog
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'summary.json') -Encoding UTF8
Write-Host "Result: $result | discovered=$discovered passed=$passed failed=$failed other=$other (exit $exit). Evidence: $runDir"
if ($result -ne 'Passed') { exit 1 }
exit 0
