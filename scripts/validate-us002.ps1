<#
.SYNOPSIS
  Validates the DeltaV UE 5.7+ C++ project against US-002 acceptance criteria.

.DESCRIPTION
  Must be run AFTER the user has scaffolded the project via the UE 5.7+ Editor
  (see docs/us-002-scaffold.md). This script:
    - AC1: Verifies project files exist and builds in Development Editor config.
    - AC3: Scans Saved/Logs/DeltaV.log for fatal errors.
    - AC2 (PIE WASD + mouse): cannot be automated; reported as MANUAL.
    - Unhappy path: MSBuild failures are captured to Saved/Logs/compile-errors.log.

  Exit codes:
    0 — every automatable AC passes.
    1 — one or more ACs fail or are blocked.
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild  # useful for quick log-only re-checks
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# UE project is scaffolded in a nested folder to keep the repo root clean (docs/, scripts/, PRD).
$projectDir = Join-Path $repo 'DeltaV'
$uproject   = Join-Path $projectDir 'DeltaV.uproject'
$solution   = Join-Path $projectDir 'DeltaV.sln'
$buildCs    = Join-Path $projectDir 'Source\DeltaV\DeltaV.Build.cs'
$logDir     = Join-Path $projectDir 'Saved\Logs'
$results    = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [string]$Id,
        [string]$Name,
        [ValidateSet('PASS','FAIL','BLOCKED','MANUAL','WARN')][string]$Status,
        [string]$Detail
    )
    $results.Add([pscustomobject]@{ Id=$Id; Name=$Name; Status=$Status; Detail=$Detail })
}

# ---------------------------------------------------------------------------
# Pre-req: project scaffold exists
# ---------------------------------------------------------------------------
$scaffolded = (Test-Path $uproject) -and (Test-Path $buildCs)
if (-not $scaffolded) {
    Add-Result -Id 'AC1.scaffold' -Name 'UE project scaffolded' -Status 'BLOCKED' `
        -Detail "Missing $uproject or $buildCs. Follow docs/us-002-scaffold.md to run the New Project wizard."
    # Downstream checks cannot proceed
    Add-Result -Id 'AC1.build'    -Name 'Development Editor build' -Status 'BLOCKED' -Detail 'Scaffold not found.'
    Add-Result -Id 'AC2.pie'      -Name 'PIE WASD + mouse (manual)' -Status 'BLOCKED' -Detail 'Scaffold not found.'
    Add-Result -Id 'AC3.log'      -Name 'No fatal errors in DeltaV.log' -Status 'BLOCKED' -Detail 'Scaffold not found.'
}
else {
    Add-Result -Id 'AC1.scaffold' -Name 'UE project scaffolded' -Status 'PASS' `
        -Detail "Found DeltaV.uproject and Source/DeltaV/DeltaV.Build.cs"

    # -----------------------------------------------------------------------
    # AC1 — Compile in Development Editor
    # -----------------------------------------------------------------------
    if ($SkipBuild) {
        Add-Result -Id 'AC1.build' -Name 'Development Editor build' -Status 'WARN' -Detail 'Skipped via -SkipBuild.'
    }
    else {
        # Build via UE's Build.bat (wraps UnrealBuildTool + bundled dotnet SDK) rather than
        # MSBuild.exe on the .sln. MSBuild would try to resolve the .csproj files for
        # AutomationTool / EpicGames.* which require Microsoft.NET.Sdk — not present in the
        # Game Dev / Desktop C++ workloads. Build.bat only builds the game target and is the
        # canonical Epic-supported build entry point.
        $ueRoot = $null
        $ueRegRoot = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine'
        if (Test-Path $ueRegRoot) {
            $ueCandidates = Get-ChildItem $ueRegRoot | ForEach-Object {
                $props = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($props.InstalledDirectory) {
                    [pscustomobject]@{ Version = $_.PSChildName; Path = $props.InstalledDirectory }
                }
            }
            $ueRoot = ($ueCandidates | Where-Object {
                if ($_.Version -match '^(\d+)\.(\d+)') {
                    try { [version]::new([int]$Matches[1], [int]$Matches[2]) -ge [version]'5.7' }
                    catch { $false }
                } else { $false }
            } | Select-Object -First 1).Path
        }

        if (-not $ueRoot) {
            Add-Result -Id 'AC1.build' -Name 'Development Editor build' -Status 'FAIL' `
                -Detail 'No UE 5.7+ install found in registry. Run US-001 workflow first.'
        }
        else {
            $buildBat = Join-Path $ueRoot 'Engine\Build\BatchFiles\Build.bat'
            if (-not (Test-Path $buildBat)) {
                Add-Result -Id 'AC1.build' -Name 'Development Editor build' -Status 'FAIL' `
                    -Detail "Build.bat not found at $buildBat. UE install appears incomplete."
            }
            else {
                New-Item -ItemType Directory -Force -Path $logDir | Out-Null
                $buildLog = Join-Path $logDir 'compile-errors.log'
                if (Test-Path $buildLog) { Remove-Item $buildLog -Force }

                Write-Host "Running UE Build.bat (DeltaVEditor Win64 Development)..." -ForegroundColor Cyan
                $buildOutput = & $buildBat DeltaVEditor Win64 Development "-Project=$uproject" -WaitMutex 2>&1
                $buildExit = $LASTEXITCODE
                $buildOutput | Out-File -FilePath $buildLog -Encoding utf8

                if ($buildExit -eq 0) {
                    Add-Result -Id 'AC1.build' -Name 'Development Editor build' -Status 'PASS' `
                        -Detail 'UE Build.bat exit 0 (UnrealBuildTool succeeded for DeltaVEditor Win64 Development).'
                    if (Test-Path $buildLog) { Remove-Item $buildLog -Force }
                } else {
                    Add-Result -Id 'AC1.build' -Name 'Development Editor build' -Status 'FAIL' `
                        -Detail "Build.bat exit $buildExit. Full log at $buildLog (unhappy path)."
                }
            }
        }
    }

    # -----------------------------------------------------------------------
    # AC2 — PIE WASD + mouse (manual only)
    # -----------------------------------------------------------------------
    Add-Result -Id 'AC2.pie' -Name 'PIE WASD + mouse (manual)' -Status 'MANUAL' `
        -Detail 'Launch PIE in UE Editor, drive the mannequin, record in docs/us-002-scaffold.md > Manual playtest log.'

    # -----------------------------------------------------------------------
    # AC3 — DeltaV.log: no fatal/critical errors
    # -----------------------------------------------------------------------
    $gameLog = Join-Path $logDir 'DeltaV.log'
    if (-not (Test-Path $gameLog)) {
        Add-Result -Id 'AC3.log' -Name 'No fatal errors in DeltaV.log' -Status 'BLOCKED' `
            -Detail 'DeltaV.log not yet created. Launch PIE once, then re-run this validator.'
    }
    else {
        $fatal = Select-String -Path $gameLog -Pattern 'Fatal error:', 'LogOutputDevice: Error:', 'assert failed' `
                                -SimpleMatch:$false -CaseSensitive:$false
        if ($fatal) {
            $excerpt = ($fatal | Select-Object -First 3 | ForEach-Object { "L$($_.LineNumber): $($_.Line.Trim())" }) -join ' | '
            Add-Result -Id 'AC3.log' -Name 'No fatal errors in DeltaV.log' -Status 'FAIL' `
                -Detail "$($fatal.Count) fatal/critical entries. Excerpt: $excerpt"
        } else {
            Add-Result -Id 'AC3.log' -Name 'No fatal errors in DeltaV.log' -Status 'PASS' `
                -Detail "Scanned $gameLog — no fatal entries."
        }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$icons = @{ PASS='[PASS]'; FAIL='[FAIL]'; BLOCKED='[WAIT]'; MANUAL='[USER]'; WARN='[WARN]' }
Write-Host ''
Write-Host 'US-002 project validation' -ForegroundColor Cyan
Write-Host ('-' * 60)
foreach ($r in $results) {
    $color = switch ($r.Status) {
        'PASS'    { 'Green' }
        'FAIL'    { 'Red' }
        'BLOCKED' { 'Yellow' }
        'MANUAL'  { 'Cyan' }
        'WARN'    { 'Yellow' }
    }
    Write-Host ("{0} {1,-38} {2}" -f $icons[$r.Status], $r.Name, $r.Detail) -ForegroundColor $color
}
Write-Host ('-' * 60)

$blocking = @($results | Where-Object { $_.Status -in 'FAIL','BLOCKED' })
if ($blocking.Count -eq 0) {
    Write-Host 'All automatable criteria satisfied. Complete the MANUAL playtest to close US-002.' -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($blocking.Count) criteria need attention. See docs/us-002-scaffold.md." -ForegroundColor Yellow
    exit 1
}
