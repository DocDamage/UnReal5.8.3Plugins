<#
.SYNOPSIS
  Run every M1.1 (DocModularCore) verification step and keep going after failures,
  so one run produces evidence for all checks. Each step writes its own
  Scripts/Output/<run-id>/summary.json; this script prints a table at the end.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\Verify-M1.1.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
#>
[CmdletBinding()]
param([Parameter(Mandatory)] [string]$EngineRoot)
$ErrorActionPreference = 'Continue'

$steps = @(
    @{ Name = 'Editor build (unity)';        Script = 'Build-Host.ps1';     Args = @{ Target = 'DocModularDevEditor'; Configuration = 'Development' } },
    @{ Name = 'Editor build (non-unity)';    Script = 'Build-Host.ps1';     Args = @{ Target = 'DocModularDevEditor'; Configuration = 'Development'; NoUnity = $true } },
    @{ Name = 'Game Development build';      Script = 'Build-Host.ps1';     Args = @{ Target = 'DocModularDev'; Configuration = 'Development' } },
    @{ Name = 'Game Shipping build';         Script = 'Build-Host.ps1';     Args = @{ Target = 'DocModularDev'; Configuration = 'Shipping' } },
    @{ Name = 'Automation Doc.Core (NullRHI)'; Script = 'Run-Automation.ps1'; Args = @{ Filter = 'Doc.Core'; ExpectedMinimumTests = 12 } }
)

$rows = @()
foreach ($s in $steps) {
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
    $rows += [pscustomobject]@{ Step = $s.Name; ExitCode = $code }
}
Write-Host "`n=== Summary (details in Scripts\Output\*\summary.json) ===" -ForegroundColor Cyan
$rows | Format-Table -AutoSize
