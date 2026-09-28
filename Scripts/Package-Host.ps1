<#
.SYNOPSIS
  Cook, stage and package the DocModular development host for Win64 and record evidence.
.DESCRIPTION
  Wraps RunUAT BuildCookRun. The result proves only that the host packages; it does not
  verify any manual gate by itself. Use Record-ManualGate.ps1 to record what a person
  observed in the packaged build, citing this run's directory.
  Status: authored 2026-09-28, not yet run.
.EXAMPLE
  .\Scripts\Package-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
  .\Scripts\Package-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -Configuration Shipping
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [string]$ProjectFile,
    [ValidateSet('Win64')] [string]$Platform = 'Win64',
    [ValidateSet('Development', 'Shipping')] [string]$Configuration = 'Development',
    [int]$TimeoutMinutes = 180,
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\DocModularDev.uproject' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$engine = Resolve-DocEngine -EngineRoot $EngineRoot
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$repoRoot = Split-Path -Parent $ProjectFile
$uat = Join-Path $engine.Root 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat)) { throw "RunUAT.bat not found: $uat" }

$runDir = New-DocRunDirectory -OutputRoot $OutputRoot -Label "Package-$Platform-$Configuration"
$log = Join-Path $runDir 'package.log'
$archive = Join-Path $runDir 'Archive'

$argList = @(
    'BuildCookRun',
    "-project=`"$ProjectFile`"",
    "-platform=$Platform",
    "-clientconfig=$Configuration",
    '-build', '-cook', '-stage', '-pak', '-archive',
    "-archivedirectory=`"$archive`"",
    '-noP4', '-utf8output', '-unattended'
)
$argString = $argList -join ' '

$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$start = Get-Date
Write-Host "Engine $($engine.Version) CL $($engine.Changelist) | $uat $argString"
$exit = Invoke-DocProcess -FilePath $uat -ArgumentString $argString -StdOutPath $log -TimeoutMinutes $TimeoutMinutes
$end = Get-Date

$result = if ($null -eq $exit) { 'TimedOut' } elseif ($exit -eq 0) { 'Passed' } else { 'Failed' }
$exe = if ($result -eq 'Passed') { Get-ChildItem -LiteralPath $archive -Recurse -Filter '*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 } else { $null }
$summary = [ordered]@{
    check         = 'Package'
    result        = $result
    exitCode      = $exit
    command       = "`"$uat`" $argString"
    engineVersion = $engine.Version
    changelist    = $engine.Changelist
    platform      = $Platform
    configuration = $Configuration
    sourceSha256  = $sourceHash
    archive       = $archive
    executable    = $(if ($exe) { $exe.FullName } else { $null })
    start         = $start.ToString('o')
    end           = $end.ToString('o')
    log           = $log
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'summary.json') -Encoding UTF8
Write-Host "Result: $result (exit $exit). Evidence: $runDir"
if ($result -ne 'Passed') { exit 1 }
exit 0
