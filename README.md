# DeltaV

> Space agency immersive-sim — you *are* the commander, you *build* the agency, you *fly* the rockets.

[![Status: Pre-Production](https://img.shields.io/badge/status-pre--production-orange)](docs/PRD.md)
[![Engine: Unreal Engine 5.5](https://img.shields.io/badge/engine-UE%205.5-black)](https://www.unrealengine.com/)
[![Language: C++](https://img.shields.io/badge/language-C%2B%2B-blue)](https://isocpp.org/)
[![Platform: Windows / Steam](https://img.shields.io/badge/platform-Windows%20%7C%20Steam-lightgrey)](https://store.steampowered.com/)

## What is this

**DeltaV** is a browser-of-one solo-dev game project that combines three genres rarely seen together:

- **Immersive-sim** — you walk around your space agency base in third-person, talk to your MetaHuman crew, interact with consoles. Ambience inspired by *Observation* and *Tacoma*.
- **Management game** — you grow your agency mission by mission. XP and scaling come exclusively from loot brought back (resources, scientific data, artifacts). No grind, no microtransactions.
- **Action flight-sim** — you pilot the rocket you just prepared. Launch sequence *à la* SpaceX livestream (ignition, max-Q, MECO, staging, fairing jettison), orbital piloting with burns and trajectory prediction, reentry with plasma ablation shader, propulsive or parachute landing. Orbital math inspired by *Kerbal Space Program* (patched conics, f64 throughout).

The gameplay loop chains four playable phases (no cutscenes): **Base (TPV walking) → Launch (multi-cam broadcast) → Orbital (piloting) → Reentry & Landing**.

## Current status

Pre-production. The [Product Requirements Document](docs/PRD.md) is finalized and ready for implementation. **67 user stories organized into 12 phased releases**, estimated 30–40 calendar weeks of solo development.

- ✅ Engine & stack decisions locked (UE 5.5 C++, Chaos + custom Kepler rail dual-tier physics, Lumen HW RT, Nanite, MetaHumans, Mountea Dialogue, GenericGraph tech tree)
- ✅ Architecture documented
- ✅ Top 3 technical risks identified with mitigation strategies
- ⏳ Phase 0 (project setup + first passing Kepler Automation test) — **next**

## Tech stack

| Layer | Choice |
|---|---|
| Engine | Unreal Engine 5.5.x |
| Language | C++ (Blueprints for wiring only) |
| Rendering | Lumen Hardware RT, Nanite, Volumetric Clouds |
| Physics | Chaos rigid body (local) + custom Kepler f64 (long-range) with dual-tier switching |
| Orbital math | Patched conics, Newton-Raphson, Leapfrog symplectic fallback |
| VFX | Niagara GPU (exhaust, plasma, smoke) |
| Audio | MetaSounds |
| Characters | MetaHumans (crew) + native UE character (commander) |
| Dialogue | [Mountea Dialogue System](https://github.com/Mountea-Framework/MounteaDialogueSystem) |
| Tech tree | [GenericGraph](https://github.com/jinyuliao/GenericGraph) + custom UDataAsset |
| Tests | UE Automation Framework (headless CLI) |
| Distribution | Steam (Windows) — no web, no Mac/Linux in MVP |
| VCS | Git + LFS |

## Repository layout

```
delta-v/
├── docs/
│   ├── PRD.md                  # Full Product Requirements Document
│   └── prd-status.json         # Machine-readable story tracker
├── README.md                   # You are here
└── (UE5 project files land here on first Windows session — Source/, Content/, Config/, etc.)
```

## Why this exists

Developed solo by [@ArthurDEV44](https://github.com/ArthurDEV44) — a web developer diving into UE5 C++ for the first time. Shipping-focused, parental constraints real, no deadline, quality over speed.

The project is **public** for transparency, portfolio visibility, and to share the devlog. Contributions are not currently accepted (solo-dev discipline), but feedback and discussion are welcome via Issues.

## Inspirations

*Kerbal Space Program* (simulation), *Observation* & *Tacoma* (immersive TPV), *Everspace 2* (juice & game feel), SpaceX livestreams (cinematic clean camera work). Visual direction: **clean PBR realism** — hero asset quality à la *Star Citizen* / *Arena Breakout Infinite* / *PUBG Black Budget*, not gritty photoréalisme.

## License

TBD. Codebase will be proprietary (Steam commercial release). Documentation in `docs/` is © Arthur Jean, all rights reserved until further notice.
