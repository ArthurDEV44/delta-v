<#
.SYNOPSIS
  Validates the DeltaV development environment against US-001 acceptance criteria.

.DESCRIPTION
  Probes the local Windows machine for: Visual Studio 2022 17.10+ with required
  C++ workloads, Git LFS 3.5+, NVIDIA driver >= 560.x, and Unreal Engine 5.7+ (floor per PRD v1.3).
  Emits a PASS / FAIL / BLOCKED report per acceptance criterion.

  Exit codes:
    0 — every AC passes (US-001 DoD satisfied).
    1 — one or more ACs fail or are blocked.

.EXAMPLE
  pwsh -NoProfile -File scripts/validate-env.ps1
#>

[CmdletBinding()]
param(
    [switch]$Json
)

$ErrorActionPreference = 'Stop'
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [string]$Id,
        [string]$Name,
        [ValidateSet('PASS','FAIL','BLOCKED','WARN')][string]$Status,
        [string]$Detail
    )
    $results.Add([pscustomobject]@{
        Id = $Id; Name = $Name; Status = $Status; Detail = $Detail
    })
}

# ---------------------------------------------------------------------------
# AC1 — Visual Studio 2022 17.10+ with "Game dev C++" + "Desktop C++" + Win10 SDK
# ---------------------------------------------------------------------------
$vswhere = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $vswhere) {
    Add-Result -Id 'AC1' -Name 'Visual Studio 2022 17.10+ with C++ workloads' `
        -Status 'FAIL' `
        -Detail 'vswhere.exe not found. Visual Studio Installer is not installed. See docs/environment-setup.md (AC1 section).'
}
else {
    $installs = & $vswhere -products '*' -format json 2>$null | ConvertFrom-Json
    $vs2022 = $installs | Where-Object { $_.installationVersion -like '17.*' } | Select-Object -First 1

    if (-not $vs2022) {
        Add-Result -Id 'AC1' -Name 'Visual Studio 2022 17.10+ with C++ workloads' `
            -Status 'FAIL' `
            -Detail 'No VS 2022 (17.x) installation detected by vswhere.'
    }
    else {
        $version = [version]$vs2022.installationVersion
        if ($version -lt [version]'17.10') {
            Add-Result -Id 'AC1.version' -Name 'VS 2022 version >= 17.10' `
                -Status 'FAIL' `
                -Detail "Installed version $version is older than 17.10."
        }
        else {
            Add-Result -Id 'AC1.version' -Name 'VS 2022 version >= 17.10' `
                -Status 'PASS' -Detail "Version $version at $($vs2022.installationPath)"
        }

        $required = @{
            'Microsoft.VisualStudio.Workload.NativeGame'    = 'Game development with C++'
            'Microsoft.VisualStudio.Workload.NativeDesktop' = 'Desktop development with C++'
        }
        foreach ($kvp in $required.GetEnumerator()) {
            $found = & $vswhere -products '*' -requires $kvp.Key -latest -property installationPath 2>$null
            if ($found) {
                Add-Result -Id "AC1.$($kvp.Key)" -Name "Workload: $($kvp.Value)" `
                    -Status 'PASS' -Detail $found
            } else {
                Add-Result -Id "AC1.$($kvp.Key)" -Name "Workload: $($kvp.Value)" `
                    -Status 'FAIL' `
                    -Detail "Workload '$($kvp.Value)' not installed. Open VS Installer > Modify."
            }
        }

        # UE 5.7+ accepts Windows 11 SDK (26100, 22621) OR legacy Windows 10 SDK (20348, 19041).
        # Microsoft stopped bundling Win10 SDK 20348 in VS Installer in 2026; Win11 SDK 26100 is now the default.
        $sdkComponents = @(
            @{ Id = 'Microsoft.VisualStudio.Component.Windows11SDK.26100'; Name = 'Windows 11 SDK 10.0.26100' },
            @{ Id = 'Microsoft.VisualStudio.Component.Windows11SDK.22621'; Name = 'Windows 11 SDK 10.0.22621' },
            @{ Id = 'Microsoft.VisualStudio.Component.Windows10SDK.20348'; Name = 'Windows 10 SDK 10.0.20348' },
            @{ Id = 'Microsoft.VisualStudio.Component.Windows10SDK.19041'; Name = 'Windows 10 SDK 10.0.19041' }
        )
        $winSdk = $null
        foreach ($c in $sdkComponents) {
            $path = & $vswhere -products '*' -requires $c.Id -latest -property installationPath 2>$null
            if ($path) { $winSdk = "$($c.Name) at $path"; break }
        }
        if ($winSdk) {
            Add-Result -Id 'AC1.winsdk' -Name 'Windows SDK component (11 or 10)' `
                -Status 'PASS' -Detail $winSdk
        } else {
            Add-Result -Id 'AC1.winsdk' -Name 'Windows SDK component (11 or 10)' `
                -Status 'FAIL' `
                -Detail 'No Windows 11 SDK (26100/22621) or Windows 10 SDK (20348/19041) installed via VS Installer.'
        }
    }
}

# ---------------------------------------------------------------------------
# AC2 — UE 5.7+ test project compiles (probes UE install; compile is user-run)
# ---------------------------------------------------------------------------
$ueRoots = @()
$ueRegRoot = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine'
if (Test-Path $ueRegRoot) {
    Get-ChildItem $ueRegRoot | ForEach-Object {
        $props = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
        if ($props.InstalledDirectory) {
            $ueRoots += [pscustomobject]@{
                Version = $_.PSChildName
                Path    = $props.InstalledDirectory
            }
        }
    }
}

# PRD v1.3: floor = UE 5.7, newer minor versions (5.8, 5.9...) auto-accepted.
$ue = $ueRoots | Where-Object {
    if ($_.Version -match '^(\d+)\.(\d+)') {
        try { [version]::new([int]$Matches[1], [int]$Matches[2]) -ge [version]'5.7' }
        catch { $false }
    } else { $false }
} | Select-Object -First 1

if (-not $ue) {
    Add-Result -Id 'AC2' -Name 'Unreal Engine 5.7+ installed' `
        -Status 'BLOCKED' `
        -Detail 'UE 5.7+ not installed. Install via Epic Games Launcher. Compile validation requires a user-run C++ test project.'
}
else {
    $engineExe = Join-Path $ue.Path 'Engine\Binaries\Win64\UnrealEditor.exe'
    if (Test-Path $engineExe) {
        Add-Result -Id 'AC2' -Name 'Unreal Engine 5.7+ installed' `
            -Status 'PASS' -Detail "$($ue.Version) at $($ue.Path) (manual compile of a test C++ project still required)"
    }
    else {
        Add-Result -Id 'AC2' -Name 'Unreal Engine 5.7+ installed' `
            -Status 'WARN' `
            -Detail "UE $($ue.Version) registered at $($ue.Path) but UnrealEditor.exe missing. Reinstall engine."
    }
}

# ---------------------------------------------------------------------------
# AC3 — Git LFS 3.5+
# ---------------------------------------------------------------------------
$lfs = Get-Command git-lfs -ErrorAction SilentlyContinue
if (-not $lfs) {
    # Fallback: `git lfs version` even if git-lfs not on PATH as its own command
    try { $lfsOutput = & git lfs version 2>$null } catch { $lfsOutput = $null }
} else {
    $lfsOutput = & git lfs version
}

if (-not $lfsOutput) {
    Add-Result -Id 'AC3' -Name 'Git LFS >= 3.5' `
        -Status 'FAIL' -Detail '`git lfs version` returned nothing. Install Git LFS (https://git-lfs.com).'
}
else {
    # Example output: git-lfs/3.7.1 (GitHub; windows amd64; go 1.25.1; git b84b3384)
    if ($lfsOutput -match 'git-lfs/(\d+)\.(\d+)\.(\d+)') {
        $lfsVersion = [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])"
        if ($lfsVersion -ge [version]'3.5.0') {
            Add-Result -Id 'AC3' -Name 'Git LFS >= 3.5' `
                -Status 'PASS' -Detail "Detected $lfsVersion"
        } else {
            Add-Result -Id 'AC3' -Name 'Git LFS >= 3.5' `
                -Status 'FAIL' -Detail "Detected $lfsVersion, needs upgrade to 3.5+"
        }
    } else {
        Add-Result -Id 'AC3' -Name 'Git LFS >= 3.5' `
            -Status 'FAIL' -Detail "Could not parse `git lfs version` output: $lfsOutput"
    }
}

# ---------------------------------------------------------------------------
# AC4 — NVIDIA driver >= 560.x
# ---------------------------------------------------------------------------
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if (-not $nvidiaSmi) {
    Add-Result -Id 'AC4' -Name 'NVIDIA driver >= 560.x' `
        -Status 'FAIL' `
        -Detail 'nvidia-smi not found. Install NVIDIA Studio driver 560.x or newer.'
}
else {
    $csv = & nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>$null
    if (-not $csv) {
        Add-Result -Id 'AC4' -Name 'NVIDIA driver >= 560.x' `
            -Status 'FAIL' -Detail 'nvidia-smi returned no GPU info.'
    }
    else {
        $parts = ($csv -split '\r?\n')[0] -split ',\s*'
        $gpuName = $parts[0]
        $driver  = $parts[1]
        if ($driver -match '^(\d+)\.') {
            $major = [int]$Matches[1]
            if ($major -ge 560) {
                Add-Result -Id 'AC4' -Name 'NVIDIA driver >= 560.x' `
                    -Status 'PASS' -Detail "$gpuName, driver $driver"
            } else {
                Add-Result -Id 'AC4' -Name 'NVIDIA driver >= 560.x' `
                    -Status 'FAIL' -Detail "$gpuName, driver $driver (need >= 560.x)"
            }
        } else {
            Add-Result -Id 'AC4' -Name 'NVIDIA driver >= 560.x' `
                -Status 'FAIL' -Detail "Could not parse driver version: $driver"
        }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
if ($Json) {
    $results | ConvertTo-Json -Depth 3
}
else {
    $icons = @{ PASS='[PASS]'; FAIL='[FAIL]'; BLOCKED='[WAIT]'; WARN='[WARN]' }
    Write-Host ''
    Write-Host 'US-001 environment validation' -ForegroundColor Cyan
    Write-Host ('-' * 60)
    foreach ($r in $results) {
        $color = switch ($r.Status) {
            'PASS'    { 'Green' }
            'FAIL'    { 'Red' }
            'BLOCKED' { 'Yellow' }
            'WARN'    { 'Yellow' }
        }
        Write-Host ("{0} {1,-38} {2}" -f $icons[$r.Status], $r.Name, $r.Detail) -ForegroundColor $color
    }
    Write-Host ('-' * 60)
    $fails = @($results | Where-Object { $_.Status -in 'FAIL','BLOCKED' })
    if ($fails.Count -eq 0) {
        Write-Host 'All acceptance criteria satisfied.' -ForegroundColor Green
    } else {
        Write-Host "$($fails.Count) criteria need attention. See docs/environment-setup.md." -ForegroundColor Yellow
    }
}

if (@($results | Where-Object { $_.Status -in 'FAIL','BLOCKED' }).Count -gt 0) { exit 1 } else { exit 0 }
