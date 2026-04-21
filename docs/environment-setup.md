# DeltaV — Environment Setup (US-001)

Canonical reference for setting up the Windows 11 Pro dev workstation for DeltaV (UE 5.7+ C++). This document doubles as the audit trail for **US-001 — Environnement de dev Windows installé et validé**.

Re-run validation at any time:

```powershell
pwsh -NoProfile -File scripts/validate-env.ps1
```

The script prints a PASS / FAIL / BLOCKED line per acceptance criterion and exits `0` iff all criteria pass.

## Target stack

| Component | Required version | Notes |
|---|---|---|
| Windows | 11 Pro (10.0.22000+) | 10.0.26200 verified on this workstation |
| Visual Studio | 2022 **17.10+** | Community / Professional / Enterprise all fine |
| VS C++ workloads | `Game development with C++`, `Desktop development with C++` | Installed via Visual Studio Installer |
| Windows SDK | Windows 11 SDK **10.0.26100+** (preferred) or **10.0.22621**, or legacy Windows 10 SDK 10.0.20348 | Bundled with VS Game Dev / Desktop C++ workloads. The PRD originally targeted Win10 SDK 20348; Win11 SDK 26100 is the 2026 successor and is what Epic recommends for UE 5.5+/5.7+. |
| Git | 2.40+ | 2.53 verified |
| Git LFS | **3.5+** | 3.7.1 verified |
| NVIDIA driver | **560.x+** (Studio) | 595.97 verified on RTX 4070 Ti SUPER |
| Unreal Engine | **5.7+** (tested: 5.7.4) | Installed via Epic Games Launcher. Always-latest policy — minor upgrades (5.8, 5.9…) acceptables post-MVP, déclenchent re-run full Automation suite. |
| Epic Games Launcher | Latest | Required to install + maintain UE 5.7+ |

## Current validation snapshot

Captured from `scripts/validate-env.ps1` on this workstation:

| AC | Component | Status |
|---|---|---|
| AC1 | Visual Studio 2022 17.10+ with C++ workloads + Win10 SDK | ❌ **FAIL** — not installed |
| AC2 | UE 5.7+ test project compiles | ⏸ **BLOCKED** — UE 5.7+ not installed (requires Epic Launcher install first) |
| AC3 | Git LFS 3.5+ | ✅ **PASS** — 3.7.1 |
| AC4 | NVIDIA Studio driver ≥ 560.x | ✅ **PASS** — 595.97 on RTX 4070 Ti SUPER |

Re-run `scripts/validate-env.ps1` after the installs below to get an updated snapshot.

## AC1 — Visual Studio 2022 17.10+ with C++ workloads + Windows 10 SDK

**Install steps:**

1. Download the Visual Studio 2022 installer from https://visualstudio.microsoft.com/vs/ (Community is sufficient).
2. Launch the installer, then in the **Workloads** tab tick:
   - **Game development with C++**
   - **Desktop development with C++**
3. In the **Individual components** tab, confirm one of the following Windows SDKs is ticked (the Game Dev C++ workload ticks the latest Windows 11 SDK by default):
   - **Windows 11 SDK (10.0.26100.x)** — preferred as of 2026, default from the VS Installer.
   - **Windows 11 SDK (10.0.22621.x)** — acceptable.
   - **Windows 10 SDK (10.0.20348.0)** — legacy; Microsoft no longer bundles this in the VS Installer in 2026 but UE 5.7+ still accepts it if present.
   - ⚠️ Do NOT rely on **Windows 10 SDK (10.0.19041.0)** — VS Installer marks it as "no longer supported".
4. Install. Reboot if prompted.

**Validation (AC1):**

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -products * -property installationVersion
```

Expected: a line like `17.10.x` or newer.

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -products * -requires Microsoft.VisualStudio.Workload.NativeGame `
    -property displayName
```

Expected: non-empty (the display name of the VS 2022 install).

Then run `scripts/validate-env.ps1` — AC1 sub-rows should all be PASS.

## AC2 — UE C++ project compiles

**Install steps:**

1. Install the **Epic Games Launcher** from https://store.epicgames.com/en-US/download.
2. In the launcher, go to **Unreal Engine → Library → Engine Versions**, add **5.7.x** (or the latest 5.7+), wait for the download (~80–100 GB).
3. Create a throwaway C++ project to validate the toolchain:
   - Launch **UE 5.7+** from the Epic Launcher.
   - **New Project** → **Games** → **Blank** → **C++** → **Starter Content OFF** → name it `SmokeTest` in a scratch folder.
   - Let UE regenerate the VS solution and open it in VS 2022.
   - Build in **Development Editor** (`Ctrl+Shift+B`). The build must complete with **no errors**.
4. Delete the `SmokeTest` project once the build succeeds (not part of the repo).

**Validation (AC2):**

UE compile cannot be automated by this repo's scripts (requires a live UE install + a generated `.sln`). Record the result manually:

- Write the date and the build status in the **[Manual compile log](#manual-compile-log)** section below.
- Re-run `scripts/validate-env.ps1` — once UE 5.7+ is installed it flips AC2 from BLOCKED to PASS (with a reminder that the manual compile is still required).

## AC3 — Git LFS 3.5+

Already installed on this workstation (3.7.1).

**Install / upgrade path** if validation starts failing in the future:

1. Download the latest Git LFS Windows installer from https://git-lfs.com.
2. Run the installer, then in any shell: `git lfs install`.
3. Verify: `git lfs version`.

**Validation (AC3):**

```powershell
git lfs version
```

Expected: `git-lfs/3.5.x` or newer. `scripts/validate-env.ps1` parses this automatically.

### One-time hook wiring (US-003)

The repo ships a committed pre-commit hook at `scripts/hooks/pre-commit` that rejects any staged binary > 100 KB not tracked by Git LFS. After each fresh clone, run once:

```powershell
git config core.hooksPath scripts/hooks
```

This tells Git to look for hooks in `scripts/hooks/` instead of the default `.git/hooks/` (which is per-clone, not committed). Verify with:

```powershell
pwsh -NoProfile -File scripts/validate-us003.ps1
```

Expected: all automatable AC pass (exit 0). Push remains a manual verification against github.com (LFS pointer icon).

## AC4 — NVIDIA Studio driver ≥ 560.x

Already installed on this workstation (595.97 on RTX 4070 Ti SUPER).

**Upgrade path:**

1. Download the latest **Studio driver** (not Game Ready) for your GPU from https://www.nvidia.com/Download/index.aspx.
2. Install with **Custom → Clean install** ticked.
3. Reboot.

**Validation (AC4):**

```powershell
nvidia-smi --query-gpu=name,driver_version --format=csv
```

Expected: driver version `>= 560.x`. `scripts/validate-env.ps1` enforces the floor automatically.

## Unhappy path — Epic Games Launcher refuses to install UE 5.7+

Per AC5 in the PRD:

1. Attempt the install via the Epic Games Launcher as described above.
2. If the install fails, the launcher writes logs under:

   ```
   %LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs
   ```

3. Capture the most recent error log:

   ```powershell
   $src = "$env:LOCALAPPDATA\EpicGamesLauncher\Saved\Logs"
   if (Test-Path $src) {
       $latest = Get-ChildItem $src -Filter '*.log' |
                 Sort-Object LastWriteTime -Descending |
                 Select-Object -First 1
       Copy-Item $latest.FullName "docs/epic-launcher-error-$(Get-Date -Format 'yyyyMMdd-HHmm').log"
   }
   ```

4. Append a short incident entry to the **[Incident log](#incident-log)** section below (date, symptom, log filename, resolution path).
5. Commit both the log file and the updated `environment-setup.md`.

Common failure modes worth noting in the incident log: disk out of space (UE needs >100 GB free on the install drive), corrupted cache (fix: `%LOCALAPPDATA%\EpicGamesLauncher\Saved\webcache*` purge), firewall / proxy issues, antivirus quarantining Launcher files.

## Manual compile log

| Date | UE version | VS version | Build config | Result | Notes |
|---|---|---|---|---|---|
| _pending_ | _e.g. 5.7.4_ | _e.g. 17.14.30_ | Development Editor | _PASS / FAIL_ | _link or paste summary_ |

## Incident log

_None._
