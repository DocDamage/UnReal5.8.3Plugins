<#
.SYNOPSIS
  Record the result of a manual (cooked/packaged/audible/rendered) gate as evidence.
.DESCRIPTION
  A person runs the check described in Docs/MANUAL_GATES.md and records what they saw.
  The record is tied to the current source hash and to the package run it used, so a
  traceability row can cite it the same way it cites an automation run. Attach proof
  (screenshot, capture, audio/timing log) with -Attachment; files are copied into the
  run directory. A gate is Verified only with Result Passed and at least one attachment.
  Status: authored 2026-09-28, not yet run.
.EXAMPLE
  .\Scripts\Record-ManualGate.ps1 -Gate BRC-10 -Result Passed -PackageRun 'Scripts\Output\20261001-101500-Package-Win64-Development-abc123' `
      -Observer 'DocDamage' -Notes 'Bundled clip audible; drift 4 ms over 60 s' -Attachment .\drift.csv,.\capture.mp4
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidatePattern('^[A-Z]{2,4}-\d{2}$')] [string]$Gate,
    [Parameter(Mandatory)] [ValidateSet('Passed', 'Failed', 'Blocked')] [string]$Result,
    [Parameter(Mandatory)] [string]$PackageRun,
    [Parameter(Mandatory)] [string]$Observer,
    [Parameter(Mandatory)] [string]$Notes,
    [string[]]$Attachment = @(),
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot 'Output' }
. (Join-Path $PSScriptRoot 'DocRunCommon.ps1')

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$packageSummaryPath = Join-Path (Join-Path $repoRoot $PackageRun) 'summary.json'
if (-not (Test-Path -LiteralPath $packageSummaryPath)) { throw "Package run summary not found: $packageSummaryPath" }
$package = Get-Content -LiteralPath $packageSummaryPath -Raw | ConvertFrom-Json
if ($package.check -ne 'Package' -or $package.result -ne 'Passed') { throw "Cited run is not a passed Package run: $PackageRun" }

$sourceHash = Get-DocSourceHash -RepoRoot $repoRoot
if ($package.sourceSha256 -ne $sourceHash) {
    Write-Warning "Source changed since the package run ($($package.sourceSha256) -> $sourceHash). The record will say so."
}

$runDir = New-DocRunDirectory -OutputRoot $OutputRoot -Label "ManualGate-$Gate"
$copied = @()
foreach ($a in $Attachment) {
    $src = (Resolve-Path -LiteralPath $a).Path
    $dst = Join-Path $runDir (Split-Path -Leaf $src)
    Copy-Item -LiteralPath $src -Destination $dst
    $copied += (Split-Path -Leaf $src)
}
$effective = if ($Result -eq 'Passed' -and $copied.Count -eq 0) { 'PassedWithoutEvidence' } else { $Result }

$summary = [ordered]@{
    check                = 'ManualGate'
    gate                 = $Gate
    result               = $effective
    observer             = $Observer
    notes                = $Notes
    attachments          = $copied
    packageRun           = $PackageRun
    packageSourceSha256  = $package.sourceSha256
    sourceSha256         = $sourceHash
    sourceMatchesPackage = ($package.sourceSha256 -eq $sourceHash)
    recorded             = (Get-Date).ToString('o')
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'summary.json') -Encoding UTF8
Write-Host "Recorded $Gate as $effective. Evidence: $runDir"
if ($effective -ne 'Passed') { exit 1 }
exit 0
