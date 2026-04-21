# US-002 — Scaffold UE 5.7+ C++ Third Person project `DeltaV`

Runbook for creating the `DeltaV` UE 5.7+ C++ project (tested on 5.7.4) from the **Third Person** template and validating it against US-002 acceptance criteria.

> **Prerequisite:** US-001 must be `DONE` — Visual Studio 2022 17.10+ (with *Game dev C++* + *Desktop C++* + Windows 11 SDK 10.0.26100 or fallback) and Unreal Engine 5.7+ must both be installed. Run `pwsh -NoProfile -File scripts/validate-env.ps1` first; it must exit 0.

## Step-by-step — New Project wizard

UE 5.7 does not expose a public CLI for template instantiation (same as 5.5; Epic hasn't shipped a `CreateProjectFromTemplate` commandlet), so this step is a one-shot GUI workflow.

1. Launch the **Epic Games Launcher** → **Unreal Engine** tab → **Library** → click the **5.7.x** launch icon (or whatever latest 5.7+ you installed).
2. In the UE **Project Browser**:
   - Category: **Games**
   - Template: **Third Person**
   - Project Defaults:
     - **C++** (not Blueprint)
     - **Target Platform:** Desktop
     - **Quality Preset:** Maximum
     - **Starter Content:** **OFF**
     - **Raytracing:** OFF (can be flipped later; keeps the first build small)
   - Location: `C:\dev\`
   - Project Name: **`DeltaV`** — must match exactly (the AC mandates this name; it drives module + target names).
3. Click **Create**. UE will scaffold the project, generate the VS 2022 solution, and open the editor once the first compile completes.
4. Close the editor once you see the `ThirdPersonMap` load — the wizard job is done.

Expected on-disk layout after the wizard (rooted at `C:\dev\delta-v\`):

```
DeltaV.uproject
DeltaV.sln
Source/
  DeltaV.Target.cs
  DeltaVEditor.Target.cs
  DeltaV/
    DeltaV.Build.cs
    DeltaV.cpp
    DeltaV.h
    DeltaVCharacter.{h,cpp}
    DeltaVGameMode.{h,cpp}
    DeltaVPlayerController.{h,cpp}
Content/
  ThirdPerson/**
  Characters/**
  LevelPrototyping/**
Config/
  DefaultEditor.ini
  DefaultEngine.ini
  DefaultGame.ini
  DefaultInput.ini
```

## AC1 — Project compiles in Development Editor

After the wizard closes:

```powershell
pwsh -NoProfile -File scripts/validate-us002.ps1
```

The script locates `MSBuild.exe` via `vswhere`, runs `MSBuild DeltaV.sln /p:Configuration="Development Editor" /p:Platform=Win64 /m /v:minimal`, and reports PASS/FAIL.

## AC2 — PIE: WASD + mouse drive the mannequin

Manual verification:

1. Open `DeltaV.uproject` in UE 5.7+ (double-click, or via Epic Launcher).
2. Press **Alt+P** (or the ▶ toolbar button) to launch **PIE**.
3. Walk around with **W/A/S/D** and look with the mouse.
4. Confirm: no crash, no hang, character responds. Press **Esc** to stop PIE.

Record the outcome in the [Manual playtest log](#manual-playtest-log) section below.

## AC3 — `Saved/Logs/DeltaV.log` has no fatal errors

`scripts/validate-us002.ps1` scans the most recent `Saved/Logs/DeltaV.log` for lines matching `Fatal error:` or `LogOutputDevice: Error:`. Re-run after a PIE session to re-check.

## Unhappy path — compile fails

If MSBuild fails during AC1 validation:

1. The script writes the full build output (`stdout` + `stderr`) to `Saved/Logs/compile-errors.log` automatically.
2. Commit that log alongside this runbook so the incident is tracked:

   ```powershell
   git add Saved/Logs/compile-errors.log docs/us-002-scaffold.md
   git commit -m "incident(US-002): compile failure — see Saved/Logs/compile-errors.log"
   ```

3. Append an entry to the [Incident log](#incident-log) with a one-line summary (date, top error message, fix applied).

Common first-build failure modes: missing Windows 10 SDK (rerun VS Installer, add component), corrupted `Intermediate/` (delete `Binaries/`, `Intermediate/`, `DerivedDataCache/`, right-click `.uproject` → *Generate Visual Studio project files*, retry build), VS 2022 version below 17.10 (upgrade VS).

## Manual playtest log

| Date | UE version | WASD | Mouse | Crash? | Notes |
|---|---|---|---|---|---|
| 2026-04-21 | 5.7.4 | ✅ | ✅ | no | User-confirmed via Claude Code session 2026-04-21. Project scaffolded at `C:\dev\delta-v\DeltaV\`. `Build.bat DeltaVEditor Win64 Development` exit 0. `Saved/Logs/DeltaV.log` no fatal entries. |

## Incident log

_None._
