<#
.SYNOPSIS
  Validates Git + LFS + .gitattributes configuration against US-003 acceptance criteria.

.DESCRIPTION
  AC1: .gitignore excludes Binaries/, DerivedDataCache/, Intermediate/, Saved/, *.sln, .vs/
  AC2: git check-attr filter -- Content/Meshes/test.uasset prints "filter: lfs"
  AC3: commit + push shows LFS pointer icons on GitHub (MANUAL — reminder only)
  AC4: pre-commit hook rejects a non-LFS binary > 100 KB with an explicit error message
       pointing to .gitattributes

  This script is fully idempotent — it never mutates the real index or working
  tree. AC4 is verified by invoking the hook script directly with a temporary
  GIT_INDEX_FILE, so the real staging area is untouched.

  Exit codes:
    0 — all automatable ACs pass.
    1 — one or more ACs fail.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [string]$Id,
        [string]$Name,
        [ValidateSet('PASS','FAIL','MANUAL','WARN')][string]$Status,
        [string]$Detail
    )
    $results.Add([pscustomobject]@{ Id=$Id; Name=$Name; Status=$Status; Detail=$Detail })
}

# ---------------------------------------------------------------------------
# AC1 — .gitignore covers the required exclusion patterns
# ---------------------------------------------------------------------------
$gitignore = Join-Path $repo '.gitignore'
if (-not (Test-Path $gitignore)) {
    Add-Result -Id 'AC1' -Name '.gitignore exists and covers UE5 patterns' `
        -Status 'FAIL' -Detail '.gitignore file missing at repo root.'
}
else {
    $content = Get-Content $gitignore -Raw
    $required = @(
        @{ Pattern = 'Binaries';         Check = '(^|\n)\s*Binaries/' },
        @{ Pattern = 'DerivedDataCache'; Check = '(^|\n)\s*DerivedDataCache/' },
        @{ Pattern = 'Intermediate';     Check = '(^|\n)\s*Intermediate/' },
        @{ Pattern = 'Saved';            Check = '(^|\n)\s*Saved/' },
        @{ Pattern = '*.sln';            Check = '(^|\n)\s*\*\.sln(\s|$)' },
        @{ Pattern = '.vs/';             Check = '(^|\n)\s*\.vs/' }
    )
    $missing = @()
    foreach ($r in $required) {
        if ($content -notmatch $r.Check) {
            $missing += $r.Pattern
        }
    }
    if ($missing.Count -eq 0) {
        Add-Result -Id 'AC1' -Name '.gitignore excludes UE5 artifacts' -Status 'PASS' `
            -Detail "All 6 required patterns present: Binaries/, DerivedDataCache/, Intermediate/, Saved/, *.sln, .vs/"
    } else {
        Add-Result -Id 'AC1' -Name '.gitignore excludes UE5 artifacts' -Status 'FAIL' `
            -Detail "Missing patterns: $($missing -join ', ')"
    }
}

# ---------------------------------------------------------------------------
# AC2 — git check-attr filter -- Content/Meshes/test.uasset prints "filter: lfs"
# ---------------------------------------------------------------------------
$probePath = 'Content/Meshes/test.uasset'
$checkAttr = & git -C $repo check-attr filter -- $probePath 2>&1
if ($checkAttr -match ':\s*filter:\s*lfs\s*$') {
    Add-Result -Id 'AC2' -Name '.gitattributes routes *.uasset to LFS' -Status 'PASS' `
        -Detail "`"git check-attr filter -- $probePath`" => $checkAttr"
} else {
    Add-Result -Id 'AC2' -Name '.gitattributes routes *.uasset to LFS' -Status 'FAIL' `
        -Detail "Expected 'filter: lfs', got: $checkAttr"
}

# ---------------------------------------------------------------------------
# AC3 — GitHub LFS pointer icons (MANUAL verification)
# ---------------------------------------------------------------------------
Add-Result -Id 'AC3' -Name 'GitHub shows LFS pointer icons (manual)' -Status 'MANUAL' `
    -Detail 'After first push: open the repo on github.com, navigate to a .uasset/.umap/.png file, verify the "Stored with Git LFS" badge appears.'

# ---------------------------------------------------------------------------
# AC4 — pre-commit hook rejects non-LFS binary > 100 KB
# ---------------------------------------------------------------------------
$hook = Join-Path $repo 'scripts\hooks\pre-commit'
if (-not (Test-Path $hook)) {
    Add-Result -Id 'AC4.hook' -Name 'pre-commit hook present' -Status 'FAIL' `
        -Detail "Hook missing at $hook. Run US-003 implementation."
}
else {
    Add-Result -Id 'AC4.hook' -Name 'pre-commit hook present' -Status 'PASS' -Detail $hook

    # Simulate a commit that stages a 200 KB .dat file (not LFS-tracked) in a
    # disposable index file. The real .git/index is never touched.
    $tmpRoot     = Join-Path $env:TEMP "deltav-us003-$([guid]::NewGuid().ToString('N'))"
    $tmpIndex    = Join-Path $tmpRoot 'index.bin'
    $tmpFile     = Join-Path $repo 'us003-smoke-test.dat'
    $hookOutFile = Join-Path $tmpRoot 'hook.out'
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    try {
        # Create a 200 KB binary payload (random bytes to defeat any heuristic).
        $bytes = [byte[]]::new(200 * 1024)
        [System.Random]::new(42).NextBytes($bytes)
        [System.IO.File]::WriteAllBytes($tmpFile, $bytes)

        # Build a standalone index containing just our sacrificial file.
        $env:GIT_INDEX_FILE = $tmpIndex
        $null = & git -C $repo read-tree --empty 2>&1
        $null = & git -C $repo add --force -- (Split-Path $tmpFile -Leaf) 2>&1

        # Locate Git-Bash explicitly — on Windows, `bash` on PATH often resolves to
        # WSL, not Git-Bash. Derive the correct bash.exe from the git.exe location.
        $bashExe = $null
        $git = Get-Command git -ErrorAction SilentlyContinue
        if ($git) {
            $gitRoot = Split-Path -Parent (Split-Path -Parent $git.Source)  # e.g. C:\Program Files\Git
            $candidates = @(
                (Join-Path $gitRoot 'bin\bash.exe'),
                (Join-Path $gitRoot 'usr\bin\bash.exe')
            )
            $bashExe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        }

        if (-not $bashExe) {
            Add-Result -Id 'AC4.reject' -Name 'pre-commit hook rejects > 100 KB non-LFS binary' -Status 'WARN' `
                -Detail 'Git-Bash not found (derived from git.exe location). Hook cannot be exercised from PowerShell; manual test required.'
        }
        else {
            # Run the hook with the tmp index in scope.
            $hookPosix = ($hook -replace '\\','/')
            & $bashExe -c "cd '$($repo -replace '\\','/')' && GIT_INDEX_FILE='$($tmpIndex -replace '\\','/')' '$hookPosix'" *> $hookOutFile
            $hookExit = $LASTEXITCODE
            $hookOut  = if (Test-Path $hookOutFile) { Get-Content $hookOutFile -Raw } else { '' }

            $mentionsSize       = $hookOut -match '100 KB'
            $mentionsAttributes = $hookOut -match '\.gitattributes'
            $mentionsLfs        = $hookOut -match 'filter=lfs'

            if ($hookExit -ne 0 -and $mentionsSize -and $mentionsAttributes -and $mentionsLfs) {
                Add-Result -Id 'AC4.reject' -Name 'pre-commit hook rejects > 100 KB non-LFS binary' -Status 'PASS' `
                    -Detail "Exit $hookExit, error message mentions '100 KB' + '.gitattributes' + 'filter=lfs' pattern."
            } else {
                Add-Result -Id 'AC4.reject' -Name 'pre-commit hook rejects > 100 KB non-LFS binary' -Status 'FAIL' `
                    -Detail "Exit=$hookExit, sizeMsg=$mentionsSize, attrMsg=$mentionsAttributes, lfsMsg=$mentionsLfs. Output: $($hookOut.Trim())"
            }
        }
    }
    finally {
        # Always clean up — tmp index, tmp file, tmp dir. The real .git/index is
        # untouched because we used a scoped GIT_INDEX_FILE.
        Remove-Item Env:\GIT_INDEX_FILE -ErrorAction SilentlyContinue
        if (Test-Path $tmpFile) { Remove-Item $tmpFile -Force }
        if (Test-Path $tmpRoot) { Remove-Item $tmpRoot -Recurse -Force }
    }
}

# ---------------------------------------------------------------------------
# AC4 bonus — hook is wired via core.hooksPath so it runs on real commits
# ---------------------------------------------------------------------------
$hooksPath = & git -C $repo config --get core.hooksPath 2>$null
$expected  = 'scripts/hooks'
if ($hooksPath -eq $expected) {
    Add-Result -Id 'AC4.config' -Name 'git core.hooksPath = scripts/hooks' -Status 'PASS' -Detail "Configured to $hooksPath"
} else {
    Add-Result -Id 'AC4.config' -Name 'git core.hooksPath = scripts/hooks' -Status 'FAIL' `
        -Detail "Expected 'scripts/hooks', got '$hooksPath'. Run: git config core.hooksPath scripts/hooks"
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$icons = @{ PASS='[PASS]'; FAIL='[FAIL]'; MANUAL='[USER]'; WARN='[WARN]' }
Write-Host ''
Write-Host 'US-003 Git + LFS validation' -ForegroundColor Cyan
Write-Host ('-' * 70)
foreach ($r in $results) {
    $color = switch ($r.Status) {
        'PASS'   { 'Green' }
        'FAIL'   { 'Red' }
        'MANUAL' { 'Cyan' }
        'WARN'   { 'Yellow' }
    }
    Write-Host ("{0} {1,-45} {2}" -f $icons[$r.Status], $r.Name, $r.Detail) -ForegroundColor $color
}
Write-Host ('-' * 70)
$blocking = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($blocking.Count -eq 0) {
    Write-Host 'All automatable criteria satisfied. Complete the MANUAL push-to-GitHub check to close US-003.' -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($blocking.Count) criteria need attention." -ForegroundColor Yellow
    exit 1
}
