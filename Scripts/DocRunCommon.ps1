# Shared helpers for DocModular build/test wrappers. Dot-source; do not run directly.
Set-StrictMode -Version Latest

function Resolve-DocEngine {
    param([Parameter(Mandatory)] [string]$EngineRoot)
    $root = (Resolve-Path -LiteralPath $EngineRoot).Path
    $versionFile = Join-Path $root 'Engine\Build\Build.version'
    if (-not (Test-Path -LiteralPath $versionFile)) { throw "Not an Unreal Engine root (missing $versionFile)" }
    $v = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
    [pscustomobject]@{
        Root        = $root
        Version     = "$($v.MajorVersion).$($v.MinorVersion).$($v.PatchVersion)"
        Changelist  = $v.Changelist
        BranchName  = $v.BranchName
    }
}

function New-DocRunDirectory {
    param([Parameter(Mandatory)] [string]$OutputRoot, [Parameter(Mandatory)] [string]$Label)
    $id = '{0}-{1}-{2}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Label, ([guid]::NewGuid().ToString('N').Substring(0, 6))
    $dir = Join-Path $OutputRoot $id
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

# Hash source, config, and specification files so a result is tied to exact inputs
# even without version control. Run/status/decision markdown is excluded because it
# is updated after a run and must not change the identity of the code under test.
function Get-DocSourceHash {
    param([Parameter(Mandatory)] [string]$RepoRoot)
    $exclude = '\\(Binaries|Intermediate|Saved|DerivedDataCache|Build|\.vs|\.git|Scripts\\Output)\\'
    $evidenceDocs = '\\Docs\\(DECISIONS|DEVELOPMENT_STATUS|HANDOFF|REQUIREMENTS_TRACEABILITY|EXPANSION_TRACEABILITY|MODULES_21_40_TRACEABILITY|RELEASE_CHECKLIST|TEST_MATRIX)\.md$'
    $files = Get-ChildItem -LiteralPath $RepoRoot -Recurse -File |
        Where-Object { $_.FullName -notmatch $exclude -and $_.FullName -notmatch $evidenceDocs } |
        Sort-Object FullName
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($f in $files) {
            $rel = $f.FullName.Substring($RepoRoot.Length).TrimStart('\').Replace('\', '/')
            $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($rel + "`n")
            [void]$sha.TransformBlock($nameBytes, 0, $nameBytes.Length, $null, 0)
            $content = [System.IO.File]::ReadAllBytes($f.FullName)
            [void]$sha.TransformBlock($content, 0, $content.Length, $null, 0)
        }
        [void]$sha.TransformFinalBlock([byte[]]::new(0), 0, 0)
        return ([System.BitConverter]::ToString($sha.Hash)).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

# Starts a process, enforces a timeout, kills only its own process tree on timeout,
# and returns the exit code (or $null on timeout).
function Invoke-DocProcess {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string]$ArgumentString,
        [Parameter(Mandatory)] [string]$StdOutPath,
        [Parameter(Mandatory)] [int]$TimeoutMinutes
    )
    $p = Start-Process -FilePath $FilePath -ArgumentList $ArgumentString -NoNewWindow -PassThru `
        -RedirectStandardOutput $StdOutPath -RedirectStandardError "$StdOutPath.stderr"
    $null = $p.Handle  # cache handle so ExitCode is available (Windows PowerShell quirk)
    if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        & taskkill.exe /PID $p.Id /T /F | Out-Null
        return $null
    }
    $p.WaitForExit()
    return $p.ExitCode
}
