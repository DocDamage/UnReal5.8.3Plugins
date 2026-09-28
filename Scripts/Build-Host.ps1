<#
.SYNOPSIS
  Build a DocModular host target with UnrealBuildTool and record evidence.
.EXAMPLE
  .\Scripts\Build-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
  .\Scripts\Build-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -NoUnity
  .\Scripts\Build-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -Target DocModularDev -Configuration Shipping
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [string]$ProjectFile,
    [string]$Target = 'DocModularDevEditor',
    [ValidateSet('Win64')] [string]$Platform = 'Win64',
    [ValidateSet('Development', 'DebugGame', 'Shipping')] [string]$Configuration = 'Development',
    [switch]$NoUnity,
    [int]$TimeoutMinutes = 90,
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\DocModularDev.uproject' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$engine = Resolve-DocEngine -EngineRoot $EngineRoot
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$repoRoot = Split-Path -Parent $ProjectFile
$buildBat = Join-Path $engine.Root 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildBat)) { throw "Build.bat not found: $buildBat" }

$label = "$Target-$Configuration" + $(if ($NoUnity) { '-NoUnity' } else { '' })
$runDir = New-DocRunDirectory -OutputRoot $OutputRoot -Label $label
$log = Join-Path $runDir 'build.log'

$argList = @($Target, $Platform, $Configuration, "-Project=`"$ProjectFile`"", '-WaitMutex')
if ($NoUnity) { $argList += '-DisableUnity' }
$argString = $argList -join ' '

$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
$start = Get-Date
Write-Host "Engine $($engine.Version) CL $($engine.Changelist) | $buildBat $argString"
$exit = Invoke-DocProcess -FilePath $buildBat -ArgumentString $argString -StdOutPath $log -TimeoutMinutes $TimeoutMinutes
$end = Get-Date

$result = if ($null -eq $exit) { 'TimedOut' } elseif ($exit -eq 0) { 'Passed' } else { 'Failed' }
$summary = [ordered]@{
    check         = 'Build'
    result        = $result
    exitCode      = $exit
    command       = "`"$buildBat`" $argString"
    engineVersion = $engine.Version
    changelist    = $engine.Changelist
    target        = $Target
    configuration = $Configuration
    unity         = -not $NoUnity
    sourceSha256  = $sourceHash
    start         = $start.ToString('o')
    end           = $end.ToString('o')
    log           = $log
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'summary.json') -Encoding UTF8
Write-Host "Result: $result (exit $exit). Evidence: $runDir"
if ($result -ne 'Passed') { exit 1 }
exit 0
