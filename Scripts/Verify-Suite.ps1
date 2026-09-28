<#
.SYNOPSIS
  Build the development host with every DocModular plugin enabled and run the whole
  Doc.* automation suite. Keeps going after failures so one run produces evidence for
  every check. Each step writes Scripts/Output/<run-id>/summary.json.
.DESCRIPTION
  Test counts are the tests authored in source (never run so far). Per-plugin filters
  are listed so a failing area can be re-run alone with Run-Automation.ps1.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\Verify-Suite.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\Verify-Suite.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -SkipBuilds -Only Doc.UI
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$EngineRoot,
    [switch]$SkipBuilds,
    [string]$Only
)
$ErrorActionPreference = 'Continue'

# Filter -> authored test count (source inspection).
$suites = [ordered]@{
    'Doc.Core'         = 12
    'Doc.Events'       = 8
    'Doc.Interaction'  = 7
    'Doc.Regions'      = 5
    'Doc.Time'         = 6
    'Doc.Streaming'    = 6
    'Doc.Save'         = 12
    'Doc.Audio'        = 8
    'Doc.Sequences'    = 5
    'Doc.Inspection'   = 5
    'Doc.Activation'   = 5
    'Doc.Surface'      = 11
    'Doc.Map'          = 10
    'Doc.Weather'      = 9
    'Doc.Schedule'     = 9
    'Doc.Dialogue'     = 13
    'Doc.Quest'        = 13
    'Doc.Knowledge'    = 11
    'Doc.Unlock'       = 12
    'Doc.Inventory'    = 13
    'Doc.UI'           = 11
    'Doc.Puzzle'       = 10
    'Doc.Queue'        = 10
    'Doc.Power'        = 10
    'Doc.Evidence'     = 10
    'Doc.Rhythm'       = 10
    'Doc.Optics'       = 10
    'Doc.Acoustics'    = 10
    'Doc.Paint'        = 10
    'Doc.Material'     = 10
    'Doc.Photo'        = 10
    'Doc.Broadcast'    = 10
    'Doc.Terminal'     = 10
    'Doc.Gesture'      = 10
    'Doc.Fluid'        = 10
    'Doc.Mechanical'   = 10
    'Doc.Assembly'     = 10
    'Doc.Ghost'        = 10
    'Doc.Race'         = 10
    'Doc.Mod'          = 10
    'Doc.Playtest'     = 10
}
$total = ($suites.Values | Measure-Object -Sum).Sum

$steps = @()
if (-not $SkipBuilds) {
    $steps += @{ Name = 'Editor build (unity)';     Script = 'Build-Host.ps1'; Args = @{ Target = 'DocModularDevEditor'; Configuration = 'Development' } }
    $steps += @{ Name = 'Editor build (non-unity)'; Script = 'Build-Host.ps1'; Args = @{ Target = 'DocModularDevEditor'; Configuration = 'Development'; NoUnity = $true } }
    $steps += @{ Name = 'Game Development build';   Script = 'Build-Host.ps1'; Args = @{ Target = 'DocModularDev'; Configuration = 'Development' } }
    $steps += @{ Name = 'Game Shipping build';      Script = 'Build-Host.ps1'; Args = @{ Target = 'DocModularDev'; Configuration = 'Shipping' } }
}
if ($Only) {
    $steps += @{ Name = "Automation $Only"; Script = 'Run-Automation.ps1'; Args = @{ Filter = $Only; ExpectedMinimumTests = [int]$suites[$Only] } }
}
else {
    $steps += @{ Name = "Automation Doc. (all $total)"; Script = 'Run-Automation.ps1'; Args = @{ Filter = 'Doc.'; ExpectedMinimumTests = $total } }
}

$rows = @()
$buildFailed = $false
foreach ($s in $steps) {
    if ($buildFailed -and $s.Script -eq 'Run-Automation.ps1') {
        Write-Host "`n=== $($s.Name) === SKIPPED: a build failed, so tests would run against stale binaries." -ForegroundColor Yellow
        $rows += [pscustomobject]@{ Step = $s.Name; ExitCode = 'skipped (build failed)' }
        continue
    }
    Write-Host "`n=== $($s.Name) ===" -ForegroundColor Cyan
    $a = $s.Args.Clone(); $a.EngineRoot = $EngineRoot
    $code = $null
    try {
        & (Join-Path $PSScriptRoot $s.Script) @a
        $code = $LASTEXITCODE
    }
    catch {
        Write-Host "Step threw: $_" -ForegroundColor Red
        $code = 'threw'
    }
    if ($s.Script -eq 'Build-Host.ps1' -and $code -ne 0) { $buildFailed = $true }
    $rows += [pscustomobject]@{ Step = $s.Name; ExitCode = $code }
}
Write-Host "`n=== Summary (details in Scripts\Output\*\summary.json) ===" -ForegroundColor Cyan
$rows | Format-Table -AutoSize
Write-Host "Per-area re-run: .\Scripts\Verify-Suite.ps1 -EngineRoot '$EngineRoot' -SkipBuilds -Only <filter>  (filters: $($suites.Keys -join ', '))"
