[PRD]
# PRD: DeltaV — Space Agency Immersive-Sim

## Changelog

| Version | Date | Author | Summary |
|---------|------|--------|---------|
| 1.0 | 2026-04-20 | Arthur Jean + Claude Code | Initial draft — MVP vertical slice, 12 phases, 67 stories |

## Problem Statement

1. **Aucun jeu de gestion d'agence spatiale en 2026 ne combine simulation orbitale crédible, narration immersive à la 3ème personne et pilotage direct de véhicules.** Kerbal Space Program maîtrise la simulation mais a un rendu daté et une narration inexistante. Observation et Tacoma maîtrisent l'immersion TPV mais sont des expériences courtes sans simulation technique. Les jeux de gestion (Mars Horizon, Kerbal Space Academy) restreignent le joueur à une UI cliquable. Le gap : un jeu où le joueur *est* le commandant (TPV, walking-sim dans sa base), puis *pilote* réellement le véhicule qu'il vient de préparer, avec une physique orbitale plausible.

2. **Le développement solo d'un jeu photoréaliste en UE5 C++ est un champ miné de décisions techniques qui tuent les projets.** Précision orbitale long-terme malgré LWC (solver Chaos reste f32), couplage entre physique rigide et orbital propagator, scope explosion sur les VFX Niagara, switching de mode physique cohérent en énergie. Sans PRD explicite et sans tests Automation dès le jour 1, le projet dérive rapidement en démo non-livrable.

3. **Le profil solopreneur avec contraintes parentales réelles exige un plan phasé avec jalons jouables.** Sans découpage explicite, les 30-40 semaines de MVP se transforment en 18 mois de refactoring. Chaque phase doit produire un livrable démo-able, testable, commit-able, shippable sur une branche feature.

**Why now** : UE 5.5 consolide les technologies qui rendent ce projet jouable en solo (Lumen HW RT mature, Nanite stable, LWC industrialisé, MetaHumans accessibles, Niagara GPU puissant). Hardware consumer 2026 (Ryzen 7 7800X3D + RTX 4070 Ti Super + 32 GB DDR5) soutient sans compromis Lumen + Nanite + MetaHumans + Volumetric Clouds. La fenêtre technique est ouverte, mais sans PRD structuré le temps solo du dev se disperse.

## Overview

DeltaV est un jeu vidéo immersive-sim + management + action-sim développé sur Unreal Engine 5.5 en C++, distribué en exécutable Steam pour Windows. Le joueur incarne le commandant d'une agence spatiale type NASA/SpaceX en vue à la 3ème personne. Il fait croître son agence au fil des missions, exclusivement par butin récolté (ressources, données scientifiques, artefacts) — pas de grind, pas de monnaie IAP.

La boucle de gameplay enchaîne quatre phases distinctes toutes jouables (aucune cinématique) : (1) **Base TPV** — le joueur marche dans sa base peuplée de MetaHumans crew, interagit avec consoles, dialogue avec l'équipage, configure la mission ; (2) **Lancement épique** — compte à rebours, staging réaliste (ignition, max-Q, MECO, séparation, fairing), cameras multi façon retransmission SpaceX live, shaders de flamme Niagara et volumétriques de fumée ; (3) **Pilotage orbital** — bascule TPV sur le véhicule possédé, burns manuels prograde/retrograde, trajectoire projetée, scan/récolte de cible (astéroïde MVP), évitement débris ; (4) **Rentrée & atterrissage** — physique atmosphérique, plasma ablation via shader HLSL, posé propulsif ou parachute.

L'architecture technique repose sur quatre décisions structurantes : (a) **dual-tier physics** via `UOrbitalComponent` qui switch entre mode `Rail` (Kepler f64 déterministe, longues trajectoires) et mode `Local` (Chaos rigid body activé pour burns/collisions/landing) — non négociable vu que le solver Chaos 5.5 reste f32 en interne ; (b) **orbital core en f64 strict** avec patched conics, Newton-Raphson pour Kepler, intégrateur symplectique Leapfrog en fallback anti-drift ; (c) **Tests Automation Framework obligatoires** sur tout le noyau orbital, SOI, économie, switching rail↔Chaos, avec quality gate CLI headless ; (d) **développement phasé strict** en 12 phases (Phase 0 → Phase 11), chaque phase produit un livrable démo-able et commit-able.

## Goals

| Goal | Month-3 Target | Month-6 Target | Month-12 Target |
|------|---------------|----------------|------------------|
| Noyau orbital testé et stable | Kepler solver + SOI switching, 100% tests Automation passent | Dual-tier physics validé, 200+ tests Automation | Drift orbital < 10 m après 100 révolutions LEO en continu |
| Vertical slice jouable bout-en-bout | Phase 0-4 complètes (orbital core + vehicles + physique + base 3D v1) | Phase 0-7 complètes (jusqu'au gameplay orbital avec target astéroïde) | Phase 0-9 complètes — une mission complète jouable, ressources converties en tech tree |
| Performance runtime | 60 fps en PIE sur tests synthétiques orbital | 60 fps constant en base TPV avec 3 MetaHumans crew + Lumen HW RT, 1440p | 60 fps min pendant séquence lancement avec Niagara exhaust + volumetric smoke, 1440p |
| Tests Automation coverage | Kepler + SOI + conversion state-vector | + Economy ledger + dialog flow + event resolution | + Regression suite complète run en < 10 min CLI headless |

## Target Users

### Joueur principal — "Le passionné d'espace avec peu de temps"

- **Role** : joueur PC, 28-45 ans, fan de simulation space + jeux narratifs immersifs. A joué à KSP / Observation / Tacoma / Mass Effect / Death Stranding.
- **Behaviors** : sessions de 60-90 minutes en soirée ou weekend. Préfère les jeux à forte identité visuelle et narrative plutôt que les grinders. Lit les patch notes, regarde les lancements SpaceX en live. Commente sur Reddit r/KerbalSpaceProgram et r/spacex.
- **Pain points** : KSP est techniquement brillant mais graphiquement daté et sans narration ; les jeux narratifs spatiaux (Observation) durent 4h et sont rejouabilité zéro ; les jeux de gestion spatiale (Mars Horizon) restreignent à une UI sans incarnation. Aucun jeu ne lui donne *à la fois* l'immersion TPV et la sim orbitale crédible.
- **Current workaround** : alterne entre KSP (sim pure), Everspace 2 (pilotage juteux mais pas de gestion), et films documentaires / livestreams SpaceX (consommation passive).
- **Success looks like** : peut raconter à un ami "j'ai préparé ma mission, briefé mon équipe, regardé ma fusée décoller sous tous les angles, piloté jusqu'à un astéroïde, ramené des échantillons, et débloqué un nouveau moteur" — en une session de 90 minutes et en photoréalisme.

### Joueur secondaire — "Le streamer tech / science"

- **Role** : streamer Twitch/YouTube content space ou sim games (~1k à 100k followers). Diffuse des sessions de 2-4h.
- **Behaviors** : cherche des jeux visuellement spectaculaires et narrativement riches qui génèrent des moments de streaming (le lancement qui explose, la rentrée serrée, le dialogue crew poignant). Partage des clips courts sur TikTok/Twitter.
- **Pain points** : KSP stream très niche ; les jeux de gestion pure manquent de moments vidéogéniques ; les jeux ultra-narratifs sont courts et non-rejouables.
- **Current workaround** : stream KSP modé avec reshade + fake RTX, commentaire tech ; rarely ressort des jeux de gestion space.
- **Success looks like** : un lancement SpaceX-grade fait le clip TikTok de la semaine. La rentrée atmosphérique avec plasma shader génère des "oh putain" spontanés sur le chat.

### Dev (Arthur) — "Solopreneur père de jumelles qui doit shipper"

- **Role** : développeur solo, background web (TypeScript, R3F, GLSL), débutant C++ / UE5. Travaille par blocs de 2-4h, parfois interrompu.
- **Behaviors** : itère par phases, chaque phase produit un commit démo-able. Valide chaque décision technique avant de coder, déteste les refactors lourds.
- **Pain points** : scope explosion (piège solo KSP-like), perte de flow parental, décisions techniques non-documentées qui reviennent hanter 3 mois plus tard, bugs de précision orbital silencieux.
- **Current workaround** : PRD explicite + phases courtes + tests Automation + Claude Code en assistant code (Arthur valide et commit).
- **Success looks like** : chaque fin de phase, ouverture UE5, PIE → quelque chose de nouveau fonctionne. Zéro régression grâce aux tests. Zéro décision technique "on verra plus tard" non-tracée.

## Research Findings

### Competitive Context

- **Kerbal Space Program 1 & 2** : référence simulation orbitale patched conics, tech tree, base building basique. KSP1 graphiquement daté, KSP2 en development hell. **DeltaV se différencie par** : TPV walking-sim dans la base + MetaHumans crew + photoréalisme UE5 natif + narration environnementale.
- **Observation / Tacoma** : référence ambiance TPV station spatiale, interactions consoles, storytelling environnemental. Pas de simulation technique. **DeltaV étend** : le walking-sim débouche sur du pilotage réel de fusée/satellite avec physique orbitale crédible.
- **Mars Horizon** : gestion d'agence spatiale en 2D UI cliquable. **DeltaV remplace** la UI par un niveau 3D walkable + dialogue crew MetaHuman.
- **Everspace 2** : référence juice / game feel pilotage spatial. Pas de sim orbital, pas de base. **DeltaV emprunte** le juice (camera shake, impact feedback, HDR bloom) pour les phases orbital et reentry.
- **SpaceX livestreams officiels** : référence esthétique lancement (cameras multi, overlay télémétrie, échelle dramatique). **DeltaV reproduit** cette esthétique en temps réel jouable.

**Market gap** : aucun jeu ne combine simulation orbitale crédible + narration immersive TPV + pilotage direct photoréaliste. DeltaV occupe ce créneau.

### Best Practices Applied

- **Patched conics avec Kepler f64 + SOI switching** (référence : KSP postmortem, papier Battin "An Introduction to the Mathematics and Methods of Astrodynamics") — aligné.
- **Floating origin / origin rebasing** même avec LWC UE5 activé (confirmé par forums Epic 5.5 : solver Chaos reste f32 en interne, jitter apparaît > 10 km) — critical.
- **Automation Framework obligatoire** dès le jour 1 avec `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + flags `ApplicationContextMask|ProductFilter` + CLI headless `-NullRHI` (docs Epic UE 5.5) — adopté.
- **Online Subsystem Steam builtin** suffit pour solo single-player (Epic docs, community tutorials). Gotcha confirmé : `steam_appid.txt` à côté de l'exe en Shipping, Steam doit tourner en dev.
- **MetaHuman Animator runtime audio** pour lipsync in-game (Epic docs, Epic Community tutorial 2025), fallback Runtime MetaHuman Lip Sync (georgy.dev) si perf.
- **Mountea Dialogue System** (open-source, maintenu, graph editor dans UE editor, Decorators conditions gameplay) pour le dialog data-driven.
- **GenericGraph** (jinyuliao, open-source) pour l'éditeur tech tree graph custom + `UDataAsset` subclass par nœud.

*Full research sources available in project documentation (Phase 2 research brief).*

## Assumptions & Constraints

### Assumptions (to validate)

- **A1** : Les 200 calculs Kepler par frame (60 vehicles simulés en rail) tiennent en moins de 2 ms CPU sur Ryzen 7 7800X3D. À valider dès Phase 1.
- **A2** : Lumen Hardware RT + Nanite + 3 MetaHumans crew en base tiennent 60 fps en 1440p sur RTX 4070 Ti Super. À valider fin Phase 5.
- **A3** : L'intégrateur Newton-Raphson suffit pour des orbites courtes (< 10 révolutions LEO) ; Leapfrog symplectique sera adopté pour les orbites longues si drift > 10 m. À valider Phase 1.
- **A4** : Chaos Physics 5.5 restore proprement via snapshot position + rotation + velocities seulement (sans solver internal state). Workaround documenté mais à tester. À valider Phase 3.
- **A5** : Le switching rail↔Chaos prend < 1 frame et ne perd pas > 0.1% d'énergie cinétique. À valider Phase 3.
- **A6** : Mountea Dialogue System reste maintenu jusqu'en 2027 et supporte UE 5.5+ nativement. À vérifier au moment de l'adoption Phase 5.

### Hard Constraints

- **HC1** : UE 5.5.x uniquement, pas de port vers 5.6+ avant MVP stable.
- **HC2** : Développement sur Windows 11 Pro exclusivement. Linux Fedora = OS personnel, aucun build UE5 dessus.
- **HC3** : C++ pour toute la logique métier. Blueprints tolérés uniquement pour : UI widgets (UMG), level scripting non-critique, Niagara triggers, configuration data-driven de variantes de vehicles.
- **HC4** : Pas de multijoueur, pas de serveur backend, pas d'analytics tierces (RGPD simple pour éviter un PRD juridique).
- **HC5** : Distribution Windows Steam uniquement pour le MVP. Mac / Linux = post-MVP si demande utilisateur.
- **HC6** : Pas de save manuel arbitraire. Uniquement save auto post-mission (snapshot d'état haut-niveau, pas de serialization Chaos).
- **HC7** : Pas de monétisation IAP, pas de battle pass, pas de loot box. Un seul prix d'achat Steam.
- **HC8** : Tous les `.uasset`, `.umap`, `.fbx`, `.wav`, `.png`, `.tga`, `.exr` doivent être trackés par Git LFS. Aucun binaire source dans le repo hors LFS.

## Quality Gates

These commands must pass for every user story:

- `DeltaV.sln` build en Development Editor config sans warnings C++ → via Visual Studio 2022 ou `MSBuild.exe DeltaV.sln /p:Configuration=Development_Editor /p:Platform=Win64`
- `UnrealEditor-Cmd.exe DeltaV.uproject -execcmds="Automation RunTests DeltaV.;Quit" -NullRHI -unattended -stdout` — tous les tests Automation du projet passent (zéro failure, zéro warning)
- Lancement PIE en Development Editor charge le level par défaut sans erreur fatale dans `Saved/Logs/DeltaV.log`
- Aucun fichier binaire > 100 KB non trackés par Git LFS (`git lfs ls-files` vérifié avant commit)
- Pas de logique métier déplacée en Blueprint qui existait en C++ (diff review manuel)

For gameplay / UI stories, additional gates:

- Visual verification dans PIE : reproduire le flow décrit dans l'acceptance criteria sans crash, avec FPS >= 60 en 1440p sur hardware cible (RTX 4070 Ti Super)
- Screenshot du résultat attaché au commit pour les stories visuelles (VFX, UI, base 3D, launch sequence)

## Epics & User Stories

### EP-001: Setup & Foundations (Phase 0)

Créer le socle projet : UE 5.5 C++, Git + LFS, structure Source, premier test Automation qui passe, commandant TPV qui marche dans un level vide.

**Definition of Done** : le dev ouvre `DeltaV.uproject` sur Windows 11 Pro, appuie PIE, incarne le commandant dans un level vide, un test Automation Kepler passe en CLI headless, commit initial propre.

#### US-001: Environnement de dev Windows installé et validé
**Description** : As a dev, I want Visual Studio 2022 + UE 5.5.x + Git LFS installés et fonctionnels sur le SSD Windows 11 Pro so that je peux démarrer le développement UE5 C++ sans bloquer.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : None

**Acceptance Criteria** :
- [ ] Given Windows 11 Pro fraîchement installé, when je lance `devenv.exe /?`, then Visual Studio 2022 17.10+ est installé avec workloads "Game development with C++" et "Desktop C++" et Windows 10 SDK
- [ ] Given VS 2022 installé, when je crée un projet UE C++ test, then la compile passe sans erreur toolchain
- [ ] Given Git for Windows installé, when je tape `git lfs install` puis `git lfs version`, then Git LFS 3.5+ répond
- [ ] Given driver NVIDIA Studio, when je regarde le panneau NVIDIA, then version driver >= 560.x (support UE 5.5 Lumen HW RT)
- [ ] **Unhappy path** : si Epic Games Launcher refuse d'installer UE 5.5, capturer le log d'erreur `%LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs` et le commit dans `docs/environment-setup.md`

#### US-002: Projet UE5 C++ créé avec template Third Person
**Description** : As a dev, I want un projet UE 5.5.x C++ nommé `DeltaV` créé depuis le template Third Person so that je bénéficie de `ACharacter` + Enhanced Input gratuits au lieu de repartir de zéro.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-001

**Acceptance Criteria** :
- [ ] Given UE 5.5.x installé, when je crée un nouveau projet C++ Third Person nommé `DeltaV`, then le projet compile en Development Editor et PIE charge le level template
- [ ] Given le projet créé, when je lance PIE, then je peux déplacer le mannequin avec WASD + souris sans crash
- [ ] Given PIE actif, when je check `Saved/Logs/DeltaV.log`, then aucune erreur fatale n'est loggée
- [ ] **Unhappy path** : si la compile échoue, le log complet MSBuild est capturé dans `Saved/Logs/compile-errors.log` et committé

#### US-003: Git + LFS configuré avec `.gitattributes` UE5
**Description** : As a dev, I want un repo Git privé GitHub `delta-v` avec `.gitignore` UE standard et `.gitattributes` LFS pour les binaires so that je versionne proprement sans polluer le repo avec des assets multi-GB.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-002

**Acceptance Criteria** :
- [ ] Given le projet UE créé, when je crée `.gitignore`, then il exclut `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`, `*.sln`, `*.vs/`
- [ ] Given `.gitattributes` écrit, when je tape `git check-attr filter -- Content/Meshes/test.uasset`, then le résultat indique `filter: lfs`
- [ ] Given LFS configuré pour `*.uasset *.umap *.fbx *.wav *.png *.tga *.exr *.hdr *.psd`, when je pousse un commit initial vers GitHub, then les `.uasset` apparaissent comme pointeurs LFS sur GitHub (icône LFS visible)
- [ ] **Unhappy path** : un binaire > 100 KB non tracké LFS est bloqué par un pre-commit hook ; la commande `git commit` échoue avec message explicite pointant vers `.gitattributes`

#### US-004: Structure Source/DeltaV/ créée avec tous les sous-dossiers
**Description** : As a dev, I want la structure `Source/DeltaV/{Core,Player,Vehicles,Orbital,Physics,Interaction,Dialog,Events,Missions,Base,Economy,UI,VFX,Audio,Tests}` créée avec `.gitkeep` dans chaque dossier vide so that l'architecture est visible dans l'IDE dès le jour 1.

**Priority** : P0
**Size** : XS (1 pt)
**Dependencies** : Blocked by US-002

**Acceptance Criteria** :
- [ ] Given le projet UE, when je liste `Source/DeltaV/`, then les 15 sous-dossiers existent
- [ ] Given `DeltaV.Build.cs`, when je l'ouvre, then les dépendances incluent `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `UMG`, `Slate`, `SlateCore`, `Niagara`, `Chaos`, `GameplayTags`
- [ ] Given la structure, when je build en Development Editor, then compile passe sans nouvelle warning
- [ ] **Unhappy path** : si `DeltaV.Build.cs` a une dépendance manquante détectée lors d'un include, la compile échoue explicitement avec ligne/fichier ; fixer la dépendance et re-build

#### US-005: `ACommanderCharacter` remplace le mannequin template
**Description** : As a dev, I want la classe `ADeltaVCharacter` du template renommée et déplacée en `Source/DeltaV/Player/ACommanderCharacter.h/.cpp` so that le commandant devient la classe centrale du joueur en base.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-004

**Acceptance Criteria** :
- [ ] Given la classe renommée, when je build en Development Editor, then la compile passe et le Blueprint `BP_ThirdPersonCharacter` hérite désormais de `ACommanderCharacter`
- [ ] Given PIE actif, when je regarde l'outliner, then le pawn actif est de classe `BP_ThirdPersonCharacter` : `ACommanderCharacter`
- [ ] Given `APlayerController`, when le level commence, then il possède automatiquement une instance `ACommanderCharacter` via `ADeltaVGameMode::DefaultPawnClass`
- [ ] **Unhappy path** : si le renaming casse les références Blueprint, utiliser le "Fix Up Redirectors in Folder" de l'éditeur + tests PIE jusqu'à stabilité

#### US-006: Premier test Automation `FOrbitalState` + Kepler solver qui passe
**Description** : As a dev, I want un test Automation `DeltaV.Orbital.KeplerSolver.Converges` qui valide la convergence du solveur Newton-Raphson sur plusieurs excentricités (0, 0.1, 0.5, 0.9, 0.99) so that le socle de tests est en place avant toute logique gameplay.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-004

**Acceptance Criteria** :
- [ ] Given `Source/DeltaV/Orbital/OrbitalState.h`, when j'inclus le header, then `FOrbitalState` est défini comme struct USTRUCT avec 7 `double` (a, e, i, raan, argp, ta, epoch) + `UPROPERTY` pour chaque + `TWeakObjectPtr<UCelestialBody> ParentBody`
- [ ] Given `Source/DeltaV/Orbital/KeplerSolver.cpp`, when j'appelle `UKeplerSolver::SolveEquation(MeanAnomaly, Eccentricity, Tolerance=1e-12, MaxIterations=50)`, then la fonction retourne l'anomalie excentrique avec erreur < tolerance pour e ∈ [0, 0.99]
- [ ] Given `Source/DeltaV/Tests/KeplerSolverTest.cpp`, when je run `UnrealEditor-Cmd.exe DeltaV.uproject -execcmds="Automation RunTests DeltaV.Orbital.KeplerSolver;Quit" -NullRHI -unattended -stdout`, then le test passe avec exit code 0 et produit un rapport XML dans `Saved/Automation/`
- [ ] Given le solveur, when j'appelle avec excentricité hyperbolique (e >= 1.0), then la fonction retourne un code d'erreur explicite (pas de boucle infinie, pas de NaN silencieux)
- [ ] **Unhappy path** : si le solver ne converge pas en 50 itérations pour une entrée pathologique (M proche de 0 et e proche de 1), le test échoue explicitement avec le triplet (M, e, dernière itération)

---

### EP-002: Orbital Mechanics Core (Phase 1)

Implémenter le noyau de mécanique orbitale complet en f64 : `FOrbitalState`, Kepler solver, SOI manager, propagator, conversions state-vector ↔ elements. Tests Automation exhaustifs (propagation LEO × 100 révolutions, transfert Hohmann Terre→Lune, SOI transition).

**Definition of Done** : je peux propager un vaisseau en LEO pendant 100 révolutions avec drift position < 10 m, drift énergie < 0.01%. Un transfert Hohmann Terre→Lune atterrit dans la SOI lunaire avec erreur < 100 km. Tous ces résultats sont vérifiés par tests Automation passant en CLI headless.

#### US-007: Conversion state-vector ↔ éléments orbitaux (f64 strict)
**Description** : As a dev, I want des fonctions `UOrbitalMath::StateVectorToElements(FVector3d Pos, FVector3d Vel, double Mu)` et inverse so that je peux convertir entre représentations sans perdre de précision.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-006

**Acceptance Criteria** :
- [ ] Given une position + vélocité connues (ex : ISS 400 km LEO), when je convertis en `FOrbitalState`, then a ≈ 6778 km ± 1 m, e ≈ 0.0003 ± 1e-5
- [ ] Given un `FOrbitalState` d'ISS, when je reconvertis en state vector, then la différence avec les vecteurs d'origine est < 1 mm en position, < 1 µm/s en vélocité
- [ ] Given orbite quasi-circulaire (e < 1e-4), when je convertis, then les éléments angulaires dégénérés (argp, ta) utilisent les fallbacks RAAN + longitude vraie sans produire NaN
- [ ] **Unhappy path** : input invalide (Pos=0 ou Vel=0) → return false + log explicite, pas de crash

#### US-008: Propagateur Kepler analytique (2-body)
**Description** : As a dev, I want une fonction `UOrbitalMath::PropagateKepler(FOrbitalState InState, double DeltaSeconds)` qui avance l'état orbital dans le temps so that je peux simuler des orbites longues sans intégration numérique.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-007

**Acceptance Criteria** :
- [ ] Given un état initial LEO circulaire, when je propage de 90 min, then la nouvelle position est à < 10 m de la position théorique
- [ ] Given un état GTO (très excentrique), when je propage de 10h45min (demi-période), then la position atteint l'apogée avec erreur < 50 m
- [ ] Given un input `DeltaSeconds = 0`, when je propage, then l'état retourné est bit-equal à l'entrée
- [ ] **Unhappy path** : propagation négative `DeltaSeconds < 0` → comportement défini : propage rétroactivement (utile pour trajectoires passées)

#### US-009: Intégrateur Leapfrog symplectique fallback
**Description** : As a dev, I want une fonction `UOrbitalMath::PropagateLeapfrog(FOrbitalState InState, double DeltaSeconds, double StepHz)` so that les orbites longues où Kepler analytique dérive soient propagées par intégration symplectique conservative.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-008

**Acceptance Criteria** :
- [ ] Given orbite LEO circulaire, when je propage 100 révolutions avec Leapfrog à 100 Hz, then drift énergie cumulée < 0.01%
- [ ] Given orbite LEO circulaire, when je propage 100 révolutions avec Kepler analytique vs Leapfrog, then les deux arrivent dans un cercle de 10 m l'une de l'autre
- [ ] Given un state propagé par Leapfrog, when je reconvertis en elements, then les 7 valeurs dérivent de < 1 ppm par révolution
- [ ] **Unhappy path** : `StepHz < 10` → warning log + force à 60 Hz min (évite instabilité)

#### US-010: `USOIManager` avec body tree et transitions
**Description** : As a dev, I want une classe `USOIManager` (UGameInstanceSubsystem) qui maintient l'arbre des corps célestes (Terre, Lune, astéroïde) avec leurs SOI radius et détecte les transitions d'un vaisseau so that le mode rail puisse patcher les coniques.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-008

**Acceptance Criteria** :
- [ ] Given le subsystem, when je charge un level avec Earth + Moon + 1 astéroïde, then `USOIManager::GetAllBodies()` retourne 3 entries avec SOI radius calculés
- [ ] Given un vaisseau à 300 km altitude Terre, when je query `USOIManager::GetCurrentSOI(Vessel)`, then retourne `Earth`
- [ ] Given un vaisseau qui franchit la SOI Earth→Moon, when je check une frame après la traversée, then `USOIManager::GetCurrentSOI` retourne `Moon` et un event `OnSOITransitionEnter` est broadcast
- [ ] Given chattering SOI boundary (oscillation rapide entrée/sortie), when le flag hystérésis activé (± 0.5% SOI radius), then pas plus d'une transition par 60 s n'est loggée
- [ ] **Unhappy path** : un vaisseau hors de toute SOI (erreur logique) → event `OnSOIOrphan` broadcast + log error, pas de crash

#### US-011: Test Automation "propagation LEO × 100 révolutions sans drift"
**Description** : As a dev, I want un test Automation `DeltaV.Orbital.Propagator.LEO100Revolutions` qui propage un vaisseau LEO circulaire sur 100 révolutions et assert drift < 10 m so that la précision long-terme est garantie par CI.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-008, US-009

**Acceptance Criteria** :
- [ ] Given un `FOrbitalState` LEO circulaire, when je propage 100 × 5400 s (= 100 révolutions), then drift position < 10 m
- [ ] Given même setup, when je mesure énergie orbitale au début et à la fin, then delta_energy / initial_energy < 1e-4
- [ ] Given test en mode release, when je run CLI headless, then temps total < 2 secondes
- [ ] **Unhappy path** : si drift > 10 m, fail le test avec message "Drift {X}m exceeds 10m threshold — consider Leapfrog fallback"

#### US-012: Test Automation "Hohmann transfer Earth → Moon"
**Description** : As a dev, I want un test Automation `DeltaV.Orbital.Propagator.HohmannEarthMoon` qui simule un transfert Hohmann de LEO vers une orbite qui intercepte la SOI lunaire so that la cohérence patched-conics multi-body est validée.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-010

**Acceptance Criteria** :
- [ ] Given état initial LEO + delta-V prograde calculé pour Hohmann vers distance lunaire, when je propage en rail avec SOI switching, then le vaisseau entre dans la SOI Moon entre 4j18h et 5j06h après TLI (plage Hohmann réaliste)
- [ ] Given entrée SOI Moon, when je vérifie la position, then erreur < 100 km par rapport à la trajectoire théorique 2-body
- [ ] **Unhappy path** : si le vaisseau rate la SOI Moon (trajectoire trop basse ou trop haute), fail avec la distance de passage minimale et le delta-V recommandé

---

### EP-003: Vehicles & Payloads (Phase 2)

Modéliser les véhicules comme actors hiérarchiques data-driven : `AVehicle` base, `ARocket` multi-stage avec courbes de poussée, `ASatellite`. Placeholders visuels acceptables, la data-structure est le livrable.

**Definition of Done** : je peux spawner via `UVehicleFactory::Spawn(FVehicleDef)` une fusée 2-étages avec sa charge utile satellite, toutes les masses/poussées/fuel exposées dans le HUD debug.

#### US-013: Classe `AVehicle` base avec masse, centre de masse, MoI
**Description** : As a dev, I want une classe `AVehicle : AActor` avec propriétés `TotalMass`, `CenterOfMass`, `MomentOfInertia` calculées depuis ses `UVehiclePartComponent` so that tout véhicule expose son état inertiel cohérent.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-006

**Acceptance Criteria** :
- [ ] Given un `AVehicle` avec 3 `UVehiclePartComponent`, when je tape `show debug Vehicle` dans PIE, then le HUD affiche masse totale, CoM, MoI avec precision 3 décimales
- [ ] Given un fuel tank qui se vide, when le mass delta atteint 10% du total, then `AVehicle::OnInertialPropertiesChanged` est broadcast
- [ ] Given 1000 `AVehicle` spawnés en même temps dans un stress test, when je mesure avec Unreal Insights, then le recompute des inertial properties prend < 5 ms par frame (amorti sur dirty flag)
- [ ] **Unhappy path** : un vehicle sans aucun part → masse = 0, CoM = origine locale, log warning mais pas de crash

#### US-014: Classe `ARocket : AVehicle` multi-stage avec courbes thrust
**Description** : As a dev, I want une classe `ARocket` avec `TArray<UStageComponent>` où chaque stage porte thrust curve, fuel mass, dry mass, specific impulse (Isp) so that une fusée réaliste type Falcon 9 puisse être modélisée data-driven.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-013

**Acceptance Criteria** :
- [ ] Given `URocketDef` DataAsset avec 2 stages Falcon 9-like, when je spawn une `ARocket` from def, then les 2 stages sont présents, total mass 549t, delta-V théorique calculé ≈ 9400 m/s (tolérance ±5%)
- [ ] Given la rocket allumée, when le stage 1 consomme du fuel selon sa thrust curve pendant 2.5 min, then la masse diminue linéairement de 411t à 27t stage 1 dry
- [ ] Given stage 1 empty, when `UStageComponent::Separate()` appelée, then stage 1 est spawné comme nouvel `AActor` détaché et rocket mass diminue de 27t
- [ ] **Unhappy path** : spawn d'un def invalide (stages.Num() == 0) → spawn échoue avec log error explicite, pas d'actor partiel dans le world

#### US-015: Classe `ASatellite : AVehicle` avec instruments et power
**Description** : As a dev, I want une classe `ASatellite` avec `UPowerComponent` (batteries, panneaux solaires) + `TArray<UInstrumentComponent>` (scanners, antennes) so that le satellite puisse scanner les cibles et gérer son budget énergétique.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-013

**Acceptance Criteria** :
- [ ] Given un satellite avec 100 Wh batterie + 50 W panneaux solaires + 1 scanner consommant 30 W, when le scanner tourne à l'ombre pendant 20 min, then batterie chute de 10 Wh
- [ ] Given le scanner activé face à un astéroïde à 50 km, when 30 s d'exposition, then un `FScanResult` est produit avec données typées (masse estimée, composition)
- [ ] Given satellite en ombre Terre pendant 45 min, when la batterie atteint 0 Wh, then le satellite passe en mode "safe" (instruments OFF, comms OFF sauf beacon minimal)
- [ ] **Unhappy path** : instrument activé sans énergie → retourne `FScanResult::Invalid` + log warning, pas de crash

#### US-016: `UVehicleDef` DataAsset + factory spawn
**Description** : As a dev, I want un `UVehicleDef : UDataAsset` avec toutes les propriétés configurables dans l'éditeur UE + `UVehicleFactory::Spawn(FVehicleDef)` so that ajouter un nouveau vehicle ne nécessite pas de recompile C++.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-014, US-015

**Acceptance Criteria** :
- [ ] Given un `UVehicleDef` créé dans l'éditeur avec 2 stages + 1 satellite payload, when je tape `spawn.vehicle MyFalcon9` dans console PIE, then la rocket est spawnée au player start avec toutes les propriétés du def
- [ ] Given 5 VehicleDefs différents (sonde, satellite, rocket small, rocket medium, navette), when je les spawn séquentiellement, then aucun asset leak dans Unreal Insights (detection GC)
- [ ] Given un def modifié dans l'éditeur, when je re-spawn le vehicle, then les nouvelles valeurs sont prises en compte sans reload project
- [ ] **Unhappy path** : VehicleDef manquant un required part → spawn retourne nullptr + log error

#### US-017: HUD debug véhicule (masse, thrust, fuel, delta-V)
**Description** : As a dev, I want un widget UMG `WBP_VehicleDebug` qui affiche en temps réel masse totale, thrust actuel, fuel restant, delta-V restant du vehicle actif so that je puisse debugger visuellement les calculs physiques.

**Priority** : P1
**Size** : S (2 pts)
**Dependencies** : Blocked by US-014

**Acceptance Criteria** :
- [ ] Given le widget activé par `show debug VehicleHUD`, when je regarde, then masse / thrust / fuel / deltaV sont affichés avec 3 décimales et mis à jour à 10 Hz
- [ ] Given la rocket en combustion, when je regarde, then fuel décroit en temps réel et deltaV restant diminue proportionnellement
- [ ] Given changement de vehicle possédé, when je switch, then le widget s'attache automatiquement au nouveau vehicle
- [ ] **Unhappy path** : aucun vehicle actif → widget affiche "No active vehicle" sans crash

#### US-018: Test Automation "Rocket deltaV calculation matches Tsiolkovsky"
**Description** : As a dev, I want un test Automation `DeltaV.Vehicles.Rocket.TsiolkovskyEquation` qui vérifie que le deltaV calculé d'une rocket match l'équation de Tsiolkovsky à ±1% so that la physique des fusées est conforme.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-014

**Acceptance Criteria** :
- [ ] Given `ARocket` Falcon 9-like (masse totale 549t, masse dry 22.2t + 4.5t, Isp moy 300s), when je calcule deltaV, then le résultat ≈ 9420 m/s avec tolérance ±1%
- [ ] Given 5 rocket configs différentes (petit, moyen, gros, SSTO, TSTO), when je calcule deltaV, then chaque résultat match Tsiolkovsky ±1%
- [ ] **Unhappy path** : rocket avec Isp = 0 → deltaV = 0 + log warning

---

### EP-004: Dual-Tier Physics Coupling (Phase 3)

Cœur technique du projet. `UOrbitalComponent` attachable à tout `AVehicle` qui gère deux modes : `Rail` (Kepler propagation, kinematic) et `Local` (Chaos rigid body activé). Switching automatique avec conservation d'énergie testée.

**Definition of Done** : je peux switcher un vehicle de Rail à Local et vice-versa avec conservation d'énergie cinétique > 99.9%, aucun saut de position > 10 cm, tests Automation dédiés passent.

#### US-019: `UOrbitalComponent` avec deux modes
**Description** : As a dev, I want une `UOrbitalComponent : UActorComponent` avec `EPhysicsMode::Rail | Local` + `FOrbitalState` interne en f64 so that tout vehicle puisse être propagé orbital ou simulé physique.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-013, US-008

**Acceptance Criteria** :
- [ ] Given un vehicle avec `UOrbitalComponent` en mode `Rail`, when la frame tourne, then position / rotation sont update depuis Kepler propagation sans physique Chaos
- [ ] Given même vehicle passé en mode `Local`, when la frame tourne, then Chaos rigid body activé et les forces externes (gravité, drag) s'appliquent
- [ ] Given un level avec 100 vehicles tous en mode Rail, when je profile, then le tick `UOrbitalComponent::TickComponent` total reste < 1 ms/frame sur CPU cible
- [ ] **Unhappy path** : composant sans parent `AVehicle` → log error + désactive le tick, pas de crash

#### US-020: Switching Rail → Local avec injection d'état cohérent
**Description** : As a dev, I want `UOrbitalComponent::SwitchToLocal()` qui arrête le Kepler propagator, active `SetSimulatePhysics(true)` sur le root, et injecte les velocities calculés depuis `FOrbitalState` so that la continuité physique est préservée.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-019

**Acceptance Criteria** :
- [ ] Given un vehicle en Rail avec state LEO circulaire, when je call `SwitchToLocal()`, then le vehicle continue sa trajectoire visuellement sans discontinuité > 1 cm / 1 cm/s
- [ ] Given état avant/après switch, when je mesure énergie cinétique + potentielle, then conservation > 99.95%
- [ ] Given switch pendant un burn, when je re-inject velocity, then linear + angular velocity matchent `FOrbitalState::ToStateVector()` à 1e-6 près
- [ ] **Unhappy path** : switch appelé alors que déjà en Local → no-op + log warning

#### US-021: Switching Local → Rail avec state capture et snap
**Description** : As a dev, I want `UOrbitalComponent::SwitchToRail()` qui capture position + velocity du rigid body, convertit en `FOrbitalState` via `StateVectorToElements`, désactive physique Chaos, et bascule en Kepler so that un vehicle post-burn retourne en rail sans dérive.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-020

**Acceptance Criteria** :
- [ ] Given un vehicle en Local à l'issue d'un burn, when je call `SwitchToRail()`, then la trajectoire projetée est cohérente avec l'énergie post-burn (orbit plus haute / plus basse selon direction)
- [ ] Given switch Local→Rail, when je mesure énergie avant/après, then conservation > 99.95%
- [ ] Given velocity négligeable (< 0.1 m/s dans le référentiel SOI), when je switch, then warning "near-zero velocity, orbital elements degenerate" est loggé
- [ ] **Unhappy path** : vehicle crashé au sol (altitude < SOI body surface) → SwitchToRail refusé + log error

#### US-022: Test Automation "switching round-trip conserve énergie"
**Description** : As a dev, I want un test Automation `DeltaV.Physics.Dual.RoundTripConservation` qui Rail→Local→Rail un vehicle et vérifie conservation énergie + position so that le switching est garanti stable par CI.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-021

**Acceptance Criteria** :
- [ ] Given LEO circulaire, when je fais Rail → Local → Rail (sans force appliquée en Local pendant 1s), then position drift < 10 cm, energy drift < 0.1%
- [ ] Given 10 itérations successives du round-trip, when je mesure, then cumulative energy drift < 1%
- [ ] **Unhappy path** : si drift > seuil, log l'état complet (pos, vel, energy) avant et après chaque switch pour debug

#### US-023: Origin rebasing runtime pour éviter jitter > 10 km
**Description** : As a dev, I want un système `UOriginRebasingSubsystem` qui recenter le world origin sur le vehicle actif quand sa position absolute dépasse 10 km so that les précisions f32 du solver Chaos ne produisent pas de jitter visible.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-019

**Acceptance Criteria** :
- [ ] Given un vehicle actif à 15 km de l'origine world, when le subsystem détecte le dépassement, then world origin shifté via `SetWorldOrigin` et tous les actors re-positionnés (Unreal built-in)
- [ ] Given rebasing actif, when je check un vehicle en mode Local après rebasing, then la physique Chaos continue sans jitter visible (vibration position < 1 mm)
- [ ] Given le HUD debug, when le rebasing se déclenche, then un event est loggé avec l'offset appliqué
- [ ] **Unhappy path** : rebasing pendant un burn → warning "rebasing during active burn may affect physics" mais pas de bug gameplay

---

### EP-005: Base 3D Walkable v1 (Phase 4)

Premier level de la base spatiale avec 4 salles (control room, crew quarters, launch console, payload bay). Interactions `IInteractable` sur consoles. Commandant TPV marche, saute, interagit. Niveau shell visuellement brut (BSP + textures génériques), les MetaHumans viendront en Phase 5.

**Definition of Done** : je démarre dans la base, je marche de la control room jusqu'à la launch console, j'interagis (E), un widget UI s'ouvre avec "mission briefing stub".

#### US-024: Level `L_Base` avec 4 salles BSP + éclairage Lumen
**Description** : As a dev, I want un level `L_Base.umap` avec 4 salles connectées par couloirs, éclairage Lumen Hardware RT activé so that j'ai un environnement walkable minimal pour tester les interactions.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-005

**Acceptance Criteria** :
- [ ] Given PIE sur `L_Base`, when je spawn, then je suis dans la control room avec vue sur 3 écrans placeholder
- [ ] Given je marche, when je traverse les 4 salles via couloirs, then pas de "seam" visible, Lumen HW RT éclaire correctement ombres + reflets
- [ ] Given mesurer FPS avec stat fps, when je me déplace, then > 60 fps en 1440p sur RTX 4070 Ti Super
- [ ] Given stat GPU, when je profile, then GPU < 12 ms / frame
- [ ] **Unhappy path** : shader compile en cours → affichage d'un écran "Compiling shaders..." pendant le premier PIE, pas de freeze

#### US-025: Interface `IInteractable` + `UInteractionComponent`
**Description** : As a dev, I want une interface UInterface `IInteractable` implémentée par tout actor interactable + un `UInteractionComponent` sur le commandant qui traceAhead et détecte les IInteractable so that "Press E" fonctionne partout de façon uniforme.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-024

**Acceptance Criteria** :
- [ ] Given `IInteractable` interface, when un actor l'implémente avec `GetInteractionText() → "Open Console"`, then il est détectable par le trace
- [ ] Given commandant à moins de 2m d'un interactable, when je regarde dans la direction de l'actor, then un prompt "E — Open Console" s'affiche à l'écran
- [ ] Given prompt visible, when j'appuie E, then `IInteractable::OnInteract(PlayerController)` est appelé
- [ ] Given multiple interactables à portée, when j'appuie E, then seul le plus proche aligné avec le crosshair est déclenché
- [ ] **Unhappy path** : interactable marqué disabled → prompt affiché en gris "Console offline", appuyer E ne déclenche rien + feedback sonore négatif

#### US-026: Console `ALaunchConsole` avec interaction qui ouvre widget stub
**Description** : As a dev, I want un actor `ALaunchConsole : AActor` qui implémente `IInteractable` et ouvre un widget UMG `WBP_LaunchConsoleStub` so that la future mission briefing ait une porte d'entrée.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-025

**Acceptance Criteria** :
- [ ] Given `ALaunchConsole` placé en control room, when j'interact, then `WBP_LaunchConsoleStub` s'ouvre en plein écran avec titre "Launch Console — Coming Soon"
- [ ] Given widget ouvert, when j'appuie Escape, then widget se ferme et contrôle joueur rendu
- [ ] Given widget ouvert, when je clique sur le bouton "Launch mission stub", then un log "Mission briefing requested" est émis et widget fermé
- [ ] **Unhappy path** : widget déjà ouvert + autre interact déclenché → 2ème interact ignoré + log warning

#### US-027: Enhanced Input mappings pour commandant TPV
**Description** : As a dev, I want les Input Actions + Input Mapping Context pour WASD + jump + sprint + interact + pause via Enhanced Input so that les contrôles sont data-driven et remappable plus tard.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-005

**Acceptance Criteria** :
- [ ] Given `IMC_Commander` assigné au `APlayerController`, when j'appuie WASD, then `IA_Move` déclenche le mouvement
- [ ] Given Shift pressé pendant WASD, when je check velocity, then sprint x1.5 appliqué
- [ ] Given espace pressé, when au sol, then jump déclenché, sinon no-op
- [ ] Given E pressé near interactable, when je check l'input, then `IA_Interact` déclenché
- [ ] **Unhappy path** : mapping context non-assigné → player pawn inerte, log warning explicite au BeginPlay

#### US-028: Camera TPV avec spring arm et collision
**Description** : As a dev, I want le commandant avec une `USpringArmComponent` + `UCameraComponent` en vue à la 3ème personne, collision activée (évite caméra qui passe à travers murs) so that la vue soit stable dans les couloirs serrés.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-005

**Acceptance Criteria** :
- [ ] Given commandant spawn, when je regarde le PIE, then camera placée ~3m derrière et 1m au-dessus
- [ ] Given commandant dans un couloir étroit, when je recule vers un mur, then camera se rapproche automatiquement sans clip à travers le mur
- [ ] Given souris input, when je bouge, then camera orbite autour du commandant avec rotation fluide
- [ ] **Unhappy path** : spring arm longueur < 0 (erreur config) → force à 100 cm + log warning

#### US-029: `UPossessionManager` subsystem pour switch commandant ↔ vehicle
**Description** : As a dev, I want un `UPossessionManager : UGameInstanceSubsystem` qui expose `RequestPossession(APlayerController, APawn NewTarget)` et gère le switch smooth commandant → vehicle et inverse so that les transitions phases base ↔ mission soient orchestrées proprement.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-028, US-013

**Acceptance Criteria** :
- [ ] Given commandant possédé, when je call `RequestPossession(PC, MyRocket)`, then le PC possède la rocket et la camera bascule sur la camera par défaut du rocket (chase cam)
- [ ] Given vehicle possédé, when je call avec target null, then le PC repossess le commandant à sa dernière position
- [ ] Given possession en cours pendant un fade (0.5s noir), when la transition finit, then input mapping context switch automatiquement
- [ ] **Unhappy path** : possession sur un actor non-pawn → retourne false + log error, pas de crash

#### US-030: Test Automation "L_Base level loads and basic nav works"
**Description** : As a dev, I want un test Automation `DeltaV.Base.L_Base.LoadsAndNavigation` qui charge `L_Base` et simule 5 secondes de mouvement du commandant so that les régressions de level setup soient détectées tôt.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-024, US-027

**Acceptance Criteria** :
- [ ] Given CI headless, when je run le test, then level load sans fatal error
- [ ] Given level loaded, when j'inject 5 secondes d'input "forward", then commandant se déplace effectivement de > 1 m
- [ ] Given 5 secondes, when le test finit, then FPS moyen > 30 (headless relaxed threshold)
- [ ] **Unhappy path** : level load échoue → test fail avec log complet

---

### EP-006: MetaHuman Crew & Dialogue System (Phase 5)

Intégrer 2-3 MetaHumans crew dans la base avec animations idle/walk placeholder + système de dialogue Mountea Dialogue System + lipsync runtime. Un dialogue "briefing mission" déclenche la prochaine phase (lancement).

**Definition of Done** : je m'approche d'un crew MetaHuman dans la control room, j'interact, dialogue branchant 3-4 répliques + choix joueur s'affiche avec lipsync synchronisé, la dernière branche active "lancer la mission → Phase lancement".

#### US-031: Import 2 MetaHumans (Chief Engineer + Mission Director)
**Description** : As a dev, I want 2 MetaHumans importés via Quixel Bridge dans `Content/Art/Characters/MetaHumans/` avec LOD configurés so that le crew ait une représentation physique dans la base.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-024

**Acceptance Criteria** :
- [ ] Given Quixel Bridge linké, when je sync 2 MetaHumans (MH_Chief + MH_Director), then les assets apparaissent dans `Content/MetaHumans/`
- [ ] Given les 2 MH placés dans `L_Base`, when je mesure FPS en les regardant, then > 60 fps 1440p avec Lumen HW RT + Nanite
- [ ] Given distance > 15 m, when je regarde, then LOD 3 actif (perf acceptable)
- [ ] **Unhappy path** : Quixel Bridge disconnecté → log error + instructions dans `docs/metahuman-setup.md`

#### US-032: `ACrewMember : ACharacter` avec `UCrewDataComponent`
**Description** : As a dev, I want une classe `ACrewMember` héritant de `ACharacter` avec un `UCrewDataComponent` portant nom, rôle, compétences, loyalty so that chaque crew MetaHuman soit un actor jouable scriptable.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-031

**Acceptance Criteria** :
- [ ] Given un `ACrewMember` spawn avec `UCrewDataComponent` rempli, when je query le data component, then nom/rôle/compétences accessibles via getters `BlueprintCallable`
- [ ] Given 3 crew dans `L_Base`, when je spawn, then chacun a une idle animation différente (placeholder animation set)
- [ ] Given un crew spawn sans data component assigné, when je check, then défaut "Unknown Crew Member" + warning loggé
- [ ] **Unhappy path** : skeletal mesh invalide → spawn avec placeholder T-pose + log error

#### US-033: Plugin Mountea Dialogue System intégré
**Description** : As a dev, I want Mountea Dialogue System installé comme plugin dans `Plugins/MounteaDialogueSystem/` avec `DeltaV.Build.cs` référençant le module so that j'ai un graph editor de dialogue dans l'éditeur UE.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-032

**Acceptance Criteria** :
- [ ] Given plugin installed, when je rebuild project, then compile passe et "Mountea Dialogue" apparaît dans l'éditeur (Content Browser filter)
- [ ] Given un `UMounteaDialogueGraph` créé, when je double-click, then l'éditeur graph custom s'ouvre
- [ ] Given graph avec 3 nodes (Start → Reply → End), when je valide, then le graph est valide (pas d'erreurs de validation)
- [ ] **Unhappy path** : version plugin incompatible avec UE 5.5 → log error explicite avec version attendue dans `docs/plugin-versions.md`

#### US-034: Arbre de dialogue "Mission briefing" Chief Engineer
**Description** : As a dev, I want un `UMounteaDialogueGraph_Briefing` de 8-10 nodes avec 2-3 branches joueur (questions techniques, skip direct, confirmation) so that le joueur ait une interaction narrative avec le chief engineer avant mission.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-033

**Acceptance Criteria** :
- [ ] Given j'interact avec le Chief Engineer, when dialogue démarre, then first reply "Commander, we're ready for the asteroid mission." s'affiche avec 3 choix joueur
- [ ] Given chaque branche joueur, when je choisis, then dialogue progresse vers node différent (choix "tell me about the rocket" → 2 replies techniques)
- [ ] Given fin de dialogue, when node "End" atteint, then event `OnBriefingCompleted` broadcast avec mission params sélectionnés
- [ ] **Unhappy path** : joueur quitte dialogue mid-branche (Escape) → state dialog reset, re-interacting redémarre au début

#### US-035: Runtime MetaHuman lipsync intégré
**Description** : As a dev, I want MetaHuman Animator runtime audio (native Epic) activé sur les MetaHumans + hook sur Mountea Dialogue Node start event so that les MH ouvrent la bouche synchrone avec l'audio pré-enregistré.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-034

**Acceptance Criteria** :
- [ ] Given audio `SFX_Briefing_Line01.wav` 5s, when dialogue node declenche lipsync, then visuellement les lips du MetaHuman bougent avec désync < 40 ms
- [ ] Given 3 MetaHumans simultanés qui parlent tour à tour (pas simultanément), when je mesure FPS, then > 60 fps 1440p
- [ ] Given audio file manquant, when dialogue node démarre, then fallback à no-lipsync + affiche sous-titre uniquement + log warning
- [ ] **Unhappy path** : MH skeletal mesh sans les ARKit blendshapes → log error + désactive lipsync sur ce MH, pas de crash

#### US-036: Test Automation "Dialog flow end-to-end completion"
**Description** : As a dev, I want un test Automation `DeltaV.Dialog.BriefingFlow.Completion` qui joue automatiquement l'arbre de dialogue briefing jusqu'à "End" et assert l'event broadcast so that les régressions de dialog flow soient détectées.

**Priority** : P1
**Size** : S (2 pts)
**Dependencies** : Blocked by US-034

**Acceptance Criteria** :
- [ ] Given le test, when je run, then le dialogue démarre, auto-selects la branche directe, atteint End, et `OnBriefingCompleted` est broadcast avec mission params non-null
- [ ] Given même test 100 fois de suite, when je check, then 100% pass (déterministe)
- [ ] **Unhappy path** : dialog stuck dans une branche infinie → timeout 30s + test fail avec last node visité

---

### EP-007: Launch Sequence (Phase 6)

Implémenter la séquence de lancement cinématique-mais-jouable : compte à rebours, ignition, liftoff, gravity turn, max-Q, MECO, stage separation, fairing jettison, orbit insertion. Cameras multi façon retransmission SpaceX. Niagara exhaust v1 (placeholder).

**Definition of Done** : depuis la launch console, je lance la séquence, je regarde ma rocket décoller sous 4 cameras (pad, tracking, chase, stage-sep), elle atteint LEO avec stage 1 retombant proprement.

#### US-037: `APad` launch pad actor + rocket attachment
**Description** : As a dev, I want `APad : AActor` avec socket rocket attachment point + `APad::PrepareRocket(ARocket)` qui attach la rocket verticalement so that le setup visuel de lancement soit propre.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-014

**Acceptance Criteria** :
- [ ] Given `APad` placé dans `L_LaunchPad`, when j'appelle `PrepareRocket(MyFalcon9)`, then rocket attachée verticalement au socket avec pieds au sol
- [ ] Given rocket attachée en mode Rail, when je check velocity, then velocity absolute = rotation Earth (1670 km/h au sol, pas 0)
- [ ] **Unhappy path** : pad already has a rocket → log warning + remplace (avec log)

#### US-038: State machine `ULaunchSequenceComponent` avec 8 phases
**Description** : As a dev, I want `ULaunchSequenceComponent` avec enum `ELaunchPhase::{Countdown, Ignition, Liftoff, GravityTurn, MaxQ, MECO, StageSeparation, FairingJettison, OrbitInsertion}` + transitions automatiques so that la séquence soit scriptable data-driven.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-014, US-037

**Acceptance Criteria** :
- [ ] Given séquence démarrée, when `Countdown` finit (T=0), then `Ignition` déclenche + thrust à 100%
- [ ] Given liftoff à T+5s, when rocket quitte le pad, then detach automatique et gravity turn programmé à T+15s
- [ ] Given max-Q à ~T+75s, when reached, then throttle down à 80% pendant 20s (realistic)
- [ ] Given MECO à T+~155s (fuel stage 1 ~empty), when reached, then stage 1 thrust cut + countdown 5s avant separation
- [ ] Given staging, when stage 1 separation, then stage 1 devient actor détaché avec physique Chaos locale, stage 2 poursuit en rail
- [ ] **Unhappy path** : abort conditions (high acceleration > 4g, trajectory off-nominal > 30°) → event `OnLaunchAborted` broadcast, stage 1 détache, crew safe (même si mission perdue)

#### US-039: 4 cinecams multi-angles façon retransmission
**Description** : As a dev, I want 4 `UCineCameraComponent` (PadCam fixe, TrackingCam qui suit vertical, ChaseCam attachée rocket, StageSepCam activée à staging) + switch logic automatique so that le lancement soit visuellement dramatique.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-038

**Acceptance Criteria** :
- [ ] Given séquence démarrée, when T < 0, then PadCam active avec framing bas du pad
- [ ] Given liftoff, when T=0+5s, then switch auto TrackingCam (follow vertical rocket, framing dynamique avec easing)
- [ ] Given altitude > 10 km, when atteint, then switch ChaseCam (attachée rocket, vue derrière)
- [ ] Given staging, when stage sep, then StageSepCam activée 8 secondes (plan large des 2 stages qui se séparent), puis retour ChaseCam
- [ ] Given input joueur "V" à tout moment, when pressé, then cycle manuel entre les 4 cameras
- [ ] **Unhappy path** : camera target détruite pendant son usage → fallback PadCam

#### US-040: Niagara exhaust plume v1 (placeholder, GPU)
**Description** : As a dev, I want un `NS_RocketExhaust` Niagara GPU system avec particules + glow HDR attaché au stage moteur pendant thrust > 0 so that la rocket ait un feu visible.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-038

**Acceptance Criteria** :
- [ ] Given thrust actif, when je regarde le moteur, then plume de particules visible avec emissive HDR orange/bleu
- [ ] Given thrust = 0 (MECO), when je check, then plume disparaît en 0.5s avec trail
- [ ] Given plume rendering, when je profile, then GPU cost < 1.5 ms/frame sur RTX 4070 Ti Super 1440p
- [ ] **Unhappy path** : Niagara system manquant → moteur sans plume + warning log (pas de crash)

#### US-041: Camera shake + HDR bloom pendant ignition
**Description** : As a dev, I want un `UCameraShakeBase` déclenché à ignition + intensité proportionnelle au thrust so that le joueur ressente la puissance.

**Priority** : P1
**Size** : S (2 pts)
**Dependencies** : Blocked by US-039

**Acceptance Criteria** :
- [ ] Given ignition, when je regarde PadCam, then shake intense pendant 3s (oscillation 5-8 Hz)
- [ ] Given TrackingCam active, when je regarde, then shake atténué (la camera "s'éloigne" de l'event)
- [ ] Given bloom active, when thrust max, then emissive plume produit halo HDR visible
- [ ] **Unhappy path** : shake appliqué à cinecam détruite → no-op + warning

#### US-042: Orbit insertion stable en Rail à T+~540s
**Description** : As a dev, I want la séquence se termine avec stage 2 qui switch en mode Rail quand burn final cut à T+~540s avec orbital velocity LEO so that le vehicle entre Phase 7 (orbital gameplay) proprement.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-038, US-021

**Acceptance Criteria** :
- [ ] Given stage 2 burn cut à T+540s, when je check state, then orbital elements cohérents LEO ~200x300 km altitude, e < 0.01
- [ ] Given switch to Rail, when je propage 1 révolution, then drift < 1 km
- [ ] **Unhappy path** : stage 2 burn insuffisant → `FLaunchResult::SubOrbitalFailure` broadcast, rocket retombe, mission perdue avec narration stub

---

### EP-008: Orbital Gameplay (Phase 7)

Le joueur possède le vehicle, effectue des burns manuels prograde/retrograde, voit sa trajectoire projetée en temps réel, cible un astéroïde coorbital, scan/récolte, évite un débris.

**Definition of Done** : je suis en LEO stable, je plan un transfert vers un astéroïde placé à 500 km, j'exécute 2 burns, j'arrive à 10 km de l'astéroïde, je scan 30 secondes → `FScanResult` obtenu avec ressources.

#### US-043: HUD orbital (altitude, vitesse, delta-V restant, periapsis, apoapsis)
**Description** : As a dev, I want un widget `WBP_OrbitalHUD` qui affiche altitude, orbital velocity, delta-V restant, periapsis, apoapsis en temps réel so that le joueur ait l'info pour planifier ses burns.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-017, US-042

**Acceptance Criteria** :
- [ ] Given vehicle en LEO, when je check HUD, then altitude, velocity affichés avec precision 1 décimale, mis à jour 10 Hz
- [ ] Given orbital state updated, when orbit change (burn), then periapsis/apoapsis refresh dans la seconde
- [ ] Given delta-V = 0, when je check, then HUD affiche "FUEL EMPTY" en rouge clignotant
- [ ] **Unhappy path** : vehicle sans OrbitalComponent → HUD affiche "NO ORBITAL DATA"

#### US-044: Trajectory prediction line (spline rendering)
**Description** : As a dev, I want une ligne projetée de la trajectoire orbitale future (1 révolution) rendue en HUD 3D via `USplineMeshComponent` so that le joueur voie où son vaisseau va.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-043

**Acceptance Criteria** :
- [ ] Given vehicle en LEO, when je check le rendu, then ligne verte semi-transparente trace l'orbite complète (~180 points interpolés)
- [ ] Given burn en cours, when thrust appliqué, then la ligne se déforme en temps réel pour refléter la nouvelle orbite
- [ ] Given orbite avec SOI switch (ex: vers Lune), when trajectoire franchit SOI, then changement de couleur (vert Earth → gris Moon)
- [ ] Given 100 vehicles tous avec trajectoire affichée, when je profile, then GPU cost total < 3 ms (culling activé pour non-visibles)
- [ ] **Unhappy path** : trajectoire hyperbolique (escape) → ligne coupée à 10^6 km + indicateur "ESCAPE TRAJECTORY"

#### US-045: Burns manuels prograde / retrograde / radial
**Description** : As a dev, I want inputs joueur Shift (prograde), Ctrl (retrograde), W/S (radial up/down) qui appliquent thrust dans le référentiel orbital so that le joueur puisse modifier son orbite.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-043

**Acceptance Criteria** :
- [ ] Given Shift pressé, when thrust > 0, then le vehicle passe en mode Local, thrust appliqué prograde, orbite change en live
- [ ] Given Shift relâché, when thrust = 0, then le vehicle revient en mode Rail avec nouvel orbital state
- [ ] Given Ctrl pressé, when thrust > 0, then thrust retrograde, orbite descend
- [ ] Given fuel consommé, when fuel = 0, then burns cessent + HUD flag "FUEL OUT"
- [ ] **Unhappy path** : burn pendant collision avec débris → collision win over (staggered events)

#### US-046: `AAsteroid` cible coorbital avec orbital state
**Description** : As a dev, I want `AAsteroid : AActor` avec `UOrbitalComponent` en mode Rail permanent (non-interactive physique) placé sur orbite coorbital 500 km altitude so that le joueur ait une cible à rejoindre.

**Priority** : P0
**Size** : S (2 pts)
**Dependencies** : Blocked by US-019

**Acceptance Criteria** :
- [ ] Given level loaded, when je spawn `AAsteroid`, then il orbite Earth à 500 km altitude, inclinaison 51.6° (compatible LEO), e < 0.01
- [ ] Given je check sur 1 révolution, when je mesure, then drift < 10 m
- [ ] Given asteroid mesh Nanite 100k triangles, when je profile à 100 m distance, then Nanite culling actif, 100% perf
- [ ] **Unhappy path** : asteroid spawn sans orbital state initialisé → spawn at Earth origin + warning log

#### US-047: Scan/récolte astéroïde via `UInstrumentComponent::Scan`
**Description** : As a dev, I want l'interaction scan (hold F pendant 30s à < 5 km de l'astéroïde) qui produit un `FScanResult` avec ressources simulées so that le joueur ait un objectif measurable.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-015, US-046

**Acceptance Criteria** :
- [ ] Given vehicle à < 5 km asteroid, when j'hold F pendant 30s, then scan progress bar dans HUD + `FScanResult` généré
- [ ] Given scan complete, when event broadcast, then 5 resources ajoutés à l'inventaire vehicle (types : alliage_metal, eau, scan_data, artifact_rock, chunk_ice)
- [ ] Given je bouge > 5 km pendant le scan, when distance dépassée, then scan reset + message "Too far, return within 5 km"
- [ ] **Unhappy path** : power component insufficient → scan refusé + HUD "INSUFFICIENT POWER"

#### US-048: `ADebrisField` avec 10-20 débris à éviter
**Description** : As a dev, I want un champ de débris placés aléatoirement sur l'orbite cible avec collision Chaos en mode Local proche so that le joueur ait un challenge skill-based.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-019

**Acceptance Criteria** :
- [ ] Given level, when je spawn `ADebrisField`, then 15 débris créés avec orbital states légèrement bruités
- [ ] Given vehicle à < 2 km d'un débris, when proximity triggered, then le débris + vehicle passent en mode Local (Chaos activé)
- [ ] Given collision, when impact, then HP du vehicle diminue + visual impact sparks
- [ ] Given vehicle HP = 0, when event, then mission fail
- [ ] **Unhappy path** : débris spawn inside another → overlap resolved via slight random offset + log warning

---

### EP-009: Reentry & Landing (Phase 8)

Rentrée atmosphérique avec plasma ablation via shader HLSL, drag atmosphérique custom, atterrissage par parachute OU propulsif.

**Definition of Done** : je désorbite depuis LEO, plasma shader s'active visuellement pendant 2 minutes à ~70 km altitude, atterrissage réussi avec vitesse finale < 10 m/s.

#### US-049: Atmospheric drag force custom
**Description** : As a dev, I want un `UAtmosphericComponent` qui applique force drag = 0.5 × ρ(alt) × v² × Cd × A sur le vehicle en mode Local so that la rentrée soit physiquement plausible.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-019

**Acceptance Criteria** :
- [ ] Given altitude > 100 km, when je check drag, then 0 (above Kármán line simplifié)
- [ ] Given altitude 80 km à 0 km, when je check, then drag follows US Standard Atmosphere density curve
- [ ] Given vehicle en rentrée, when altitude < 100 km, then vehicle switch Local + drag appliqué chaque frame
- [ ] **Unhappy path** : altitude négative (under ground) → drag force capée + vehicle crash event

#### US-050: Shader HLSL plasma ablation
**Description** : As a dev, I want un `M_PlasmaAblation` material HLSL custom (Fresnel + animated noise + emissive temperature-based) appliqué au nosecone/heat shield du vehicle pendant rentrée so that l'effet visuel soit spectaculaire.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-049

**Acceptance Criteria** :
- [ ] Given velocity > 3 km/s + altitude entre 60-80 km, when je regarde le vehicle, then glow plasma orange/bleu visible sur le heat shield
- [ ] Given dynamic parameter `Temperature` 0-3000 K, when temperature monte, then couleur shift vers blanc-bleu HDR
- [ ] Given heat shield ablation, when temperature > 2500 K trop longtemps, then warning "hull breach imminent" + HP du vehicle diminue
- [ ] Given plasma active, when je profile, then shader cost < 0.8 ms/frame 1440p
- [ ] **Unhappy path** : material manquant → vehicle rendu sans plasma + log warning (pas de crash)

#### US-051: `UParachuteComponent` avec déploiement
**Description** : As a dev, I want `UParachuteComponent` avec déploiement manuel (touche P) qui spawn un parachute mesh + multiplie drag × 10 so that le vehicle puisse atterrir en douceur.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-049

**Acceptance Criteria** :
- [ ] Given altitude < 10 km + velocity < 300 m/s, when j'appuie P, then parachute déployé, drag multiplié ×10
- [ ] Given altitude > 10 km OR velocity > 300 m/s, when j'appuie P, then warning "Conditions non safe for deployment" + refuse
- [ ] Given parachute déployé, when vehicle atteint sol, then atterrissage réussi si velocity finale < 10 m/s
- [ ] **Unhappy path** : déploiement at > 300 m/s accidentellement → parachute ripped + drag multiplié x2 seulement + log "parachute damaged"

#### US-052: `UPropulsiveLandingComponent` style Falcon 9
**Description** : As a dev, I want un mode landing propulsif alternatif avec burn automatique pour ramener velocity verticale à 0 au sol so that stage 1 recovery soit possible.

**Priority** : P1
**Size** : L (5 pts)
**Dependencies** : Blocked by US-045

**Acceptance Criteria** :
- [ ] Given stage 1 post-staging en chute, when altitude < 30 km, then boost-back burn calculé + appliqué si fuel suffisant
- [ ] Given final landing, when altitude < 5 km + velocity > 100 m/s, then landing burn déclenché, throttle modulé pour velocity = 0 à altitude = 0
- [ ] Given fuel insuffisant, when calculé, then warning "landing burn will fail" + vehicle crash
- [ ] **Unhappy path** : wind / perturbation perturbe le landing spot → vehicle atterrit off-target, mission partiellement réussie

#### US-053: Test Automation "full reentry LEO to ground"
**Description** : As a dev, I want un test `DeltaV.Reentry.LEOToGround.Success` qui simule une rentrée complète depuis LEO jusqu'au sol so that la cohérence drag + plasma + landing soit validée.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-049, US-051

**Acceptance Criteria** :
- [ ] Given setup LEO 400 km + deorbit burn -100 m/s, when je simule 30 minutes, then vehicle atterrit au sol avec velocity < 10 m/s
- [ ] Given même test, when je regarde les logs, then temperature max < 3000 K (heat shield tient), HP vehicle > 50%
- [ ] **Unhappy path** : test fail → dump complet (alt/vel/temp/HP) par seconde dans `Saved/Logs/reentry-test.csv`

---

### EP-010: Mission Loop Closure & Economy (Phase 9)

Fermer la boucle 4-phases : mission result screen, `UResourceLedger` qui ajoute les resources récoltées, premier nœud tech tree débloquable, save auto post-mission, retour base.

**Definition of Done** : après atterrissage, un écran "Mission Complete" s'affiche avec résumé + resources gagnées ajoutées au ledger + 1 tech node débloquable. Je clique "Return to Base", load `L_Base`, commandant respawn, resources visibles dans un terminal.

#### US-054: `UResourceLedger : UGameInstanceSubsystem` avec 5 types de ressources
**Description** : As a dev, I want un `UResourceLedger` persistant qui maintient les quantities de 5 types de ressources (`Metal_Alloy`, `Fuel`, `Electronic_Components`, `Scientific_Data`, `Artifact`) avec events on change so that l'économie soit centralisée.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-047

**Acceptance Criteria** :
- [ ] Given ledger initialisé à zéro, when je call `AddResource(EResourceType::Metal_Alloy, 50)`, then inventory montre 50 Metal_Alloy + event `OnResourceChanged` broadcast
- [ ] Given 1000 transactions en stress test, when je profile, then total time < 50 ms (operations O(1))
- [ ] Given `RemoveResource` appelé avec quantity > current, when je check, then retourne false + pas de mutation négative
- [ ] **Unhappy path** : type de ressource invalide → log error + no-op

#### US-055: `UTechTreeManager` + GenericGraph plugin + 1 node débloquable
**Description** : As a dev, I want `UTechTreeManager` basé sur GenericGraph plugin avec au moins 1 `UTechNode` "Advanced Scanners" débloquable via 50 Scientific_Data + 10 Electronic_Components so that le tech tree soit en place même minimal.

**Priority** : P0
**Size** : L (5 pts)
**Dependencies** : Blocked by US-054

**Acceptance Criteria** :
- [ ] Given GenericGraph plugin installed, when je crée un `UTechTreeAsset`, then graph editor custom ouvrable
- [ ] Given graph avec 3 nodes (Root + Advanced Scanners + Propulsion Level 2), when je play, then "Advanced Scanners" affiché comme débloquable si ressources ok
- [ ] Given ressources suffisantes, when je call `UnlockNode("Advanced_Scanners")`, then ressources déduites + node.bIsUnlocked = true + event broadcast
- [ ] Given ressources insuffisantes, when je tente unlock, then refuse + HUD "Insufficient resources"
- [ ] **Unhappy path** : tech tree graph vide → warning + node par défaut "Starter kit unlocked"

#### US-056: Save auto post-mission (haut-niveau, pas Chaos state)
**Description** : As a dev, I want un `USaveGameSubsystem` qui sérialise sur disque : `UResourceLedger` + `UTechTreeManager` state + progression missions complétées, déclenché automatiquement à la fin de chaque mission so that le progress soit persistant.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-055

**Acceptance Criteria** :
- [ ] Given mission complete event, when je check, then save file `Saved/SaveGames/PlayerProgression.sav` écrit avec resources + tech tree + missions
- [ ] Given launch PIE avec save existant, when je check au boot, then resources restaurées + tech tree restauré
- [ ] Given save corruption simulée (file tronqué), when load, then fallback à new game + log error + popup "Save corrupted, starting new"
- [ ] **Unhappy path** : disk full pendant write → retry 1 fois + log error + fallback write vers temp

#### US-057: Widget `WBP_MissionComplete` avec résumé
**Description** : As a dev, I want un widget affiché fin de mission avec : mission name, duration, resources gagnées, tech nodes débloquables, bouton "Return to Base" so that le feedback du joueur soit clair.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-054

**Acceptance Criteria** :
- [ ] Given mission completed, when je check, then widget s'affiche avec résumé cohérent
- [ ] Given widget, when je clique "Return to Base", then fade out + load `L_Base` + commandant spawn en control room
- [ ] Given bouton "View Resources" cliqué, when activé, then breakdown détaillé des 5 types resources affichés
- [ ] **Unhappy path** : resources = 0 (mission aborted/failed) → widget variant avec "Mission failed" + encouragement + bouton "Return"

#### US-058: Test Automation "full mission loop end-to-end"
**Description** : As a dev, I want un test `DeltaV.Mission.FullLoop.SuccessfulMission` qui simule automatiquement toutes les 4 phases (base briefing → launch → orbital scan → reentry → return to base) et vérifie resources ajoutées au ledger so that la boucle soit garantie stable.

**Priority** : P1
**Size** : L (5 pts)
**Dependencies** : Blocked by US-053, US-056

**Acceptance Criteria** :
- [ ] Given test setup : full mission scripted inputs, when je run, then test complete en < 5 min CLI headless avec succès
- [ ] Given fin de mission, when je query ledger, then 5 resources ajoutées
- [ ] Given save fichier, when reload, then state restauré
- [ ] **Unhappy path** : test stuck dans une phase (ex: atterrissage échoue) → fail + log la phase bloquante

---

### EP-011: Dynamic Events v1 (Phase 10)

Introduire 3 events aléatoires smart que le joueur peut affronter : micrometeoroid shower, panne moteur partiel, tempête solaire. Chaque event a une `UPlayerResponseWindow` de quelques secondes pour que le joueur puisse agir.

**Definition of Done** : pendant une mission orbital, 1 event aléatoire se déclenche (proba 30%), HUD warn visible 5-10s, joueur peut appuyer une touche d'action pour s'en sortir. Event résolu avec feedback clair (succès/échec partiel/échec total).

#### US-059: `UDynamicEventManager` + `UEventTrigger` base framework
**Description** : As a dev, I want `UDynamicEventManager : UGameInstanceSubsystem` qui tick périodiquement (1 Hz) et évalue des `UEventTrigger` data-driven so that les events random soient injectables sans hard-coding.

**Priority** : P1
**Size** : L (5 pts)
**Dependencies** : Blocked by US-042

**Acceptance Criteria** :
- [ ] Given `UEventTrigger` DataAsset avec conditions + probability, when manager tick, then trigger évalué correctement
- [ ] Given trigger passes, when évalué, then event spawn via `UEventInstance` + broadcast `OnEventTriggered`
- [ ] Given 10 triggers actifs, when profile, then tick < 0.5 ms/frame
- [ ] **Unhappy path** : trigger avec probability > 1 → capé à 1 + warning log

#### US-060: Event "Micrometeoroid shower" avec player response window
**Description** : As a dev, I want un event `UEventMicrometeoroidShower` qui spawn 20 petits débris rapides vers le vehicle + HUD warning + window 5s pour burn évasion so that le joueur ait une option de skill.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-059, US-048

**Acceptance Criteria** :
- [ ] Given event triggered, when time T, then HUD affiche "MICROMETEOROID SHOWER — 5 seconds to respond"
- [ ] Given window active, when joueur appuie Space (boost évasion), then burn -200 m/s prograde appliqué + shower miss
- [ ] Given window expire, when time T+5s, then 20 débris collision-tested contre vehicle + HP damage proportionnel
- [ ] Given burn évasion insufficient (fuel empty), when trigger, then "Fuel insufficient — bracing for impact"
- [ ] **Unhappy path** : event triggered pendant dialogue base (hors space) → warning "event skipped, not in orbital phase"

#### US-061: Event "Engine partial failure" pendant burn
**Description** : As a dev, I want un event `UEventEngineFailure` qui coupe 1 moteur sur 3 pendant un burn + HUD warning + option "compensate thrust" so that le joueur puisse réagir.

**Priority** : P1
**Size** : M (3 pts)
**Dependencies** : Blocked by US-059

**Acceptance Criteria** :
- [ ] Given burn actif, when event triggered, then 33% thrust perdu immédiatement + HUD alert
- [ ] Given window 8s, when joueur hold Shift (thrust max compensatoire), then 33% perdu compensé par 2 moteurs à 150%
- [ ] Given window expire sans action, when time T+8s, then orbit underperforms mais pas fatal
- [ ] **Unhappy path** : 2/3 engines failed → fatal "mission abort imminent" + force abort séquence

#### US-062: Event "Solar storm" radiation damage
**Description** : As a dev, I want un event `UEventSolarStorm` qui applique radiation damage lent + HUD warning + option "deploy shielding" (si tech node débloqué) so that le joueur ressente le besoin du tech tree.

**Priority** : P2
**Size** : M (3 pts)
**Dependencies** : Blocked by US-059, US-055

**Acceptance Criteria** :
- [ ] Given event triggered, when time T, then radiation tick 1 HP/s pendant 30s
- [ ] Given tech node "Radiation Shielding" débloqué + window 10s, when joueur appuie R (deploy shield), then radiation cap à 0.2 HP/s
- [ ] Given tech node non-débloqué, when window affichée, then HUD grey-out option + hint "research Radiation Shielding tech to unlock"
- [ ] **Unhappy path** : event triggered dans la base → warning + skip

---

### EP-012: Polish & Shipping Prep (Phase 11)

Polish VFX v2 (exhaust advanced, volumetric smoke, stage separation debris), MetaSounds (ignition rumble, max-Q buffet, comm chatter), camera shake tuning, Steamworks OSS integration, packaging Windows Shipping config.

**Definition of Done** : je package en Shipping Win64, j'installe sur un laptop tiers Windows, je lance, le jeu tourne avec Steam overlay + 1 achievement "first successful mission" déclenché. VFX et audio passent le "wow" test.

#### US-063: Niagara exhaust v2 (volumetric smoke + heat haze)
**Description** : As a dev, I want le `NS_RocketExhaust` upgradé avec volumetric smoke + heat distortion post-process so that le lancement ait un feeling dramatic.

**Priority** : P1
**Size** : L (5 pts)
**Dependencies** : Blocked by US-040

**Acceptance Criteria** :
- [ ] Given lancement, when je regarde exhaust, then smoke volumetric visible autour du pad pendant ignition (3s)
- [ ] Given thrust continu, when je regarde, then plume longue, heat haze derrière la rocket
- [ ] Given profile, when mesuré, then total exhaust GPU cost < 3 ms/frame 1440p
- [ ] **Unhappy path** : volumetric clouds désactivés (low-end detection) → fallback à exhaust v1 + log info

#### US-064: MetaSounds ignition + rumble + max-Q buffet + comm chatter
**Description** : As a dev, I want des MetaSound sources attachées à la rocket + PlayerController pour ignition bass drop + continuous rumble + max-Q high-frequency buffet + comm chatter voice samples so that l'audio soit immersif.

**Priority** : P1
**Size** : L (5 pts)
**Dependencies** : Blocked by US-038

**Acceptance Criteria** :
- [ ] Given ignition, when je regarde/écoute, then bass drop profond + rumble LFE 20-60 Hz
- [ ] Given max-Q, when je regarde/écoute, then buffet high-freq (200-800 Hz) pendant 15s
- [ ] Given each launch phase transition, when triggered, then comm chatter "Throttle down", "Max-Q", "MECO", "Stage separation nominal"
- [ ] **Unhappy path** : audio muté system-wide → pas de crash + continue en muet

#### US-065: Steamworks Online Subsystem intégration
**Description** : As a dev, I want Online Subsystem Steam builtin activé avec AppID temporaire (480 SpaceWar en dev, réel en shipping) + 1 achievement "First Successful Mission" so that le packaging Steam fonctionne.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-056

**Acceptance Criteria** :
- [ ] Given `DefaultEngine.ini` configuré avec OSS Steam + AppID 480, when je lance PIE avec Steam ouvert, then Steam overlay accessible (Shift+Tab)
- [ ] Given mission completed, when event broadcast, then achievement `ACH_FIRST_MISSION` unlocked via Steam API + popup Steam
- [ ] Given package Shipping Win64, when je lance l'exe avec `steam_appid.txt` à côté, then Steam init OK
- [ ] **Unhappy path** : Steam non lancé → OSS Steam fail init + fallback offline mode + log warning

#### US-066: Packaging Windows Shipping config
**Description** : As a dev, I want configuration de packaging Windows Shipping validée + script `scripts/package-shipping.bat` qui produit un installable so that le build final soit reproductible.

**Priority** : P0
**Size** : M (3 pts)
**Dependencies** : Blocked by US-065

**Acceptance Criteria** :
- [ ] Given `RunUAT.bat BuildCookRun -project=DeltaV.uproject -build -cook -stage -archive -platform=Win64 -configuration=Shipping`, when je run, then build réussit et produit `Binaries/Win64/DeltaV.exe` + assets cooked
- [ ] Given installer zip, when je le dézip sur un laptop Windows 11 tiers, then le jeu lance et boot menu principal
- [ ] Given laptop, when je lance le jeu, then PIE-equivalent performance > 30 fps sur GPU milieu-gamme (GTX 1660 ref)
- [ ] **Unhappy path** : packaging échoue → log UAT complet capturé + fix blocker avant proceed

#### US-067: Test Automation CLI headless complet en CI
**Description** : As a dev, I want un script `scripts/ci-automation-full.bat` qui run tous les tests Automation `DeltaV.*` en < 10 min sans GPU so that la CI puisse tourner sur un agent headless Windows.

**Priority** : P1
**Size** : S (2 pts)
**Dependencies** : Blocked by US-058

**Acceptance Criteria** :
- [ ] Given le script run sur Windows 11 avec UE 5.5 installé, when je le lance, then 100% des tests pass + exit code 0 + rapport JUnit XML produit
- [ ] Given test run, when je chronomètre, then temps total < 10 min
- [ ] **Unhappy path** : un test fail → script exit code 1 + log complet du test failing extrait + exit proprement

---

## Functional Requirements

- **FR-01** : Le système doit propager des orbites Kepler en f64 avec drift < 10 m sur 100 révolutions LEO.
- **FR-02** : Le système doit switcher entre mode Rail (kinematic, Kepler) et mode Local (Chaos rigid body) avec conservation d'énergie > 99.9%.
- **FR-03** : Le système doit rebase le world origin sur le vehicle actif quand sa position absolute dépasse 10 km.
- **FR-04** : Le système doit permettre au joueur d'incarner un commandant TPV dans une base 3D walkable avec interactions consoles.
- **FR-05** : Le système doit permettre au joueur de dialoguer avec 2+ MetaHumans crew via un dialogue branchant avec lipsync.
- **FR-06** : Le système doit exécuter une séquence de lancement complète avec 9 phases (countdown, ignition, liftoff, gravity turn, max-Q, MECO, stage separation, fairing jettison, orbit insertion).
- **FR-07** : Le système doit permettre le pilotage direct d'un véhicule en orbite avec burns prograde/retrograde/radial et trajectoire projetée visible.
- **FR-08** : Le système doit permettre le scan d'un astéroïde cible à < 5 km pendant 30 s pour produire un `FScanResult`.
- **FR-09** : Le système doit gérer une rentrée atmosphérique avec drag custom, plasma shader, et atterrissage par parachute ou propulsif.
- **FR-10** : Le système doit maintenir un inventaire multi-ressources (5 types min) et un tech tree à branches avec au moins 1 nœud débloquable au MVP.
- **FR-11** : Le système doit sauvegarder automatiquement la progression (ressources + tech tree) post-mission sans possibilité de save manuel.
- **FR-12** : Le système doit déclencher 3 types d'events dynamiques pendant mission (micrometeoroid, engine failure, solar storm) avec window de réponse joueur.
- **FR-13** : Le système doit packager en Windows Shipping Steam avec Online Subsystem Steam builtin + 1 achievement minimum.
- **FR-14** : Le système doit exécuter l'intégralité de sa suite de tests Automation en CLI headless en < 10 minutes.

## Non-Functional Requirements

- **Performance** :
  - 60 fps minimum en 1440p sur RTX 4070 Ti Super en base TPV avec 3 MetaHumans + Lumen HW RT + Nanite
  - 60 fps minimum pendant séquence lancement avec Niagara GPU exhaust + volumetric smoke
  - Kepler propagator : < 1 ms/frame pour 100 vehicles simultanés en mode Rail
  - Dual-tier switching : < 1 frame (< 16.6 ms) sans saut position > 10 cm
  - Shader compilation : < 30 s au premier boot PIE après fresh project
- **Reliability** :
  - 0 crash sur une mission complète end-to-end (test CI)
  - Test Automation 100% pass rate obligatoire pour merge
  - Drift orbital < 10 m sur 100 révolutions LEO
  - Conservation d'énergie > 99.9% sur 10 round-trips rail↔local
- **Usability** :
  - First-time boot → premier lancement réussi en < 15 minutes gameplay (tutorial implicite via dialogue crew)
  - HUD altitude / velocity / delta-V latence de refresh < 100 ms (10 Hz minimum)
- **Compatibility** :
  - Windows 10 build 1909+ / Windows 11 officially supported
  - GPU : min GTX 1660 (6 GB VRAM), recommandé RTX 3060 12 GB, target RTX 4070 Ti Super
  - CPU : min Intel i5-9600K / Ryzen 5 3600, recommandé Ryzen 7 5800X / i7-11700K
  - RAM : min 16 GB, recommandé 32 GB
  - Storage : 25 GB libres SSD (NVMe recommandé)
- **Maintainability** :
  - Zéro fichier C++ > 400 lignes (hors headers)
  - Pas de logique métier en Blueprint (BP wiring only)
  - Chaque story livre : code + test Automation (si applicable) + commit propre signed-off
- **Security** :
  - Pas d'analytics tierces (RGPD by design)
  - Save files localement dans `%LOCALAPPDATA%\DeltaV\Saved\SaveGames\` — pas de cloud sync non-Steam
  - Pas de code exécuté hors sandbox UE5 (pas de downloaded content runtime)

## Edge Cases & Error States

| # | Scenario | Trigger | Expected Behavior | User Message |
|---|----------|---------|-------------------|--------------|
| 1 | Empty state — no save file | First boot ever | Show "Start new campaign" flow, skip load option | "Welcome, Commander. Start your first campaign?" |
| 2 | Loading state — shader compilation | First PIE after fresh install | Show "Compiling shaders..." overlay non-blocking PIE | "Optimizing visuals for your system (one-time, ~30s)" |
| 3 | Error state — save file corrupted | Load fails checksum | Fallback to new game + popup + log | "Save data appears corrupted. Starting a new campaign." |
| 4 | Boundary — fuel exhausted mid-burn | `Fuel == 0` during thrust | Thrust cut + HUD "FUEL EMPTY" red flashing | "FUEL EMPTY — thrust unavailable" |
| 5 | Boundary — HP = 0 mid-mission | Vehicle destroyed | Mission fail + respawn at base | "Vehicle lost. Mission aborted." |
| 6 | Interrupted — alt-tab during burn | Window loses focus | Pause auto + HUD "GAME PAUSED" | "Paused — click to resume" |
| 7 | External — Steam not running | OSS Steam init fails | Fallback to offline mode, disable achievements | "Offline mode — achievements disabled" |
| 8 | Extreme precision — vehicle at > 10M km from origin | Post-MVP, but architectural guard now | Warn + refuse spawn | "Object too far from world origin for current MVP scope" |
| 9 | MetaHuman import failure — Quixel Bridge offline | Asset sync fails | Log + instructions | "MetaHuman sync failed. See docs/metahuman-setup.md" |
| 10 | Plugin version mismatch (Mountea, GenericGraph) | UE 5.5 incompatibility | Compile fail with explicit message | "Plugin Mountea requires UE 5.5.x exactly — see docs/plugin-versions.md" |

## Risks & Mitigations

| # | Risk | Probability | Impact | Mitigation |
|---|------|------------|--------|------------|
| 1 | **Précision orbitale long-terme** malgré LWC (solver Chaos reste f32 interne en 5.5) | High | High | Dual-tier enforcé : rail f64 exclusif pour trajectoires longues, Chaos uniquement local. Test Automation `LEO100Revolutions` obligatoire CI. Leapfrog fallback prêt si Newton dérive. Origin rebasing > 10 km. |
| 2 | **Scope explosion Niagara VFX** (exhaust + plasma + volumetric smoke) | High | High | Time-box 3 jours par VFX v1. Polish reporté Phase 11. Fab/Quixel baseline autorisée. Règle : pas de VFX pass 2 tant que mission ne boucle pas end-to-end. |
| 3 | **Switching Chaos↔rail cohérent en énergie** (source de bugs silencieux) | Med | High | Source de vérité unique : `UOrbitalComponent`. Tests Automation dédiés `RoundTripConservation`. HUD dev mode affiche mode courant + velocity + énergie. 1 semaine Phase 3 dédiée. |
| 4 | **MetaHuman perf** avec 3 crew simultanés + Lumen HW RT | Med | Med | Benchmark dès Phase 5. LOD agressifs > 10 m. Grooms désactivables via setting. Fallback à Nanite meshes simples si perf insuffisante. |
| 5 | **Plugin compatibility** (Mountea, GenericGraph) avec UE 5.5+ | Med | Med | Pin versions exactes dans `docs/plugin-versions.md`. Fork les plugins si abandonnés. Fallback custom si maintainer disparait. |
| 6 | **Parentalité** (jumelles) interrompt phases critiques | High | Med | Chaque story indépendamment complétable. Tests Automation préservent contexte. Git commits signés à chaque fin de session. Aucun WIP > 48h. |
| 7 | **C++ learning curve** sur UE5 idioms (GC, UObject, Reflection) | Med | Med | Claude Code écrit le code, Arthur review ligne à ligne et commit. Lecture `docs/` Epic au besoin. Pas de code généré non-compris. |
| 8 | **Packaging Shipping Steam** gotchas (steam_appid.txt, DLLs, achievements) | Med | Low | Intégration OSS Steam dès Phase 11 (pas en toute fin). Test sur laptop tiers Windows avant freeze. |

## Non-Goals

Explicit boundaries — what DeltaV **does NOT** include in the MVP scope :

- **Multijoueur** — pas de coop, pas de PvP, pas de leaderboards online (hors Steam achievements). Post-MVP uniquement si demande utilisateur.
- **Missions multiples simultanées** — 1 mission à la fois, série linéaire. Pas de gestion concurrentielle de plusieurs rockets en orbite.
- **Interplanétaire** — Terre + LEO + Lune + 1 astéroïde coorbital uniquement. Mars, Jupiter, et au-delà = post-MVP (nécessite World Partition + level streaming Solar System).
- **Économie complexe** — 5 ressources simples, pas de trading, pas de marché dynamique, pas de staff hiring/firing.
- **Save manuel** — save auto uniquement post-mission. Pas de quick-save pendant un burn.
- **N-body** — patched conics (2-body + SOI switching) suffisant. Pas de Lagrange points, pas de trajectoires n-corps.
- **Customization cosmetics** — pas de skins, pas de nommage de rockets, pas de tuning visuel de la base au-delà du level fixé.
- **Historiques de missions / tech tree branches complètes** — 1 node débloquable dans le MVP, le reste (progression complète propulsion/payload/crew/research/landing) en post-MVP.
- **MetaHuman dialog multi-arbres** — 1 arbre "briefing mission" dans le MVP. Relations crew, intrigues, conversations libres = post-MVP.
- **Version Mac / Linux** — Windows 10/11 uniquement au MVP.
- **Micro-transactions** — jamais. Un seul prix d'achat Steam, point.
- **Crafting / base building** — pas de construction libre de la base. Le level `L_Base` est prédéfini.
- **Combat orbital / armement** — DeltaV n'est pas un jeu de combat. Pas de missiles, pas de lasers, pas de dogfight.

## Files NOT to Modify

(N/A — greenfield project, no existing codebase. Once project bootstraps, this section should list :)

- `Source/DeltaV/Orbital/OrbitalState.h` — canonical f64 orbital struct, modifier ici impacte tout le noyau
- `Source/DeltaV/Physics/UOrbitalComponent.h` — single source of truth for physics mode, modifier sans review = bugs silencieux
- `Plugins/MounteaDialogueSystem/**` — vendor plugin, ne pas modifier sauf fork explicite
- `Plugins/GenericGraph/**` — vendor plugin, ne pas modifier sauf fork explicite
- `Config/DefaultEngine.ini` flags Lumen HW RT / Nanite / LWC — ne désactiver que pour bench temporaire

## Technical Considerations

Framed as questions for engineering confirmation (Arthur) before we commit to architecture :

- **Architecture** : `UOrbitalComponent` comme single source of truth pour physics mode — confirmé. Alternative : composant split Rail + Local séparés. Trade-off : split ajoute complexité d'ownership, monolithe concentre le risque mais simplifie le switching. **Recommandation** : monolithe.
- **Data Model** : `FOrbitalState` struct avec 7 `double` + `TWeakObjectPtr<UCelestialBody>` — confirmé. Alternative : matrice de rotation + position vector + velocity vector (state vector direct, pas d'elements). Trade-off : state vector plus simple mais moins utile pour rendering trajectory predicted. **Recommandation** : elements + conversions à la demande.
- **API Design** : `UGameInstanceSubsystem` pour `USOIManager`, `UResourceLedger`, `UPossessionManager`, `USaveGameSubsystem`, `UDynamicEventManager` — subsystems singletons par GameInstance, cycle de vie propre, facilement accessibles. Alternatives : `UWorldSubsystem` (meurent au level change, bad). **Recommandation** : GameInstanceSubsystem pour tout ce qui est cross-level.
- **Dependencies** :
  - Mountea Dialogue System (v2.x compatible UE 5.5) — confirmé
  - GenericGraph (jinyuliao, open-source) — confirmé
  - Runtime MetaHuman Lip Sync (georgy.dev) comme fallback Phase 5 si perf insuffisant
  - MetaHuman Animator (builtin Epic) — primary choice
  - Online Subsystem Steam (builtin) — confirmé pour Phase 11
- **Migration** : N/A — greenfield, pas de data existante à migrer. Post-MVP : plan de migration save format v1 → v2 à prévoir si tech tree évolue.

## Success Metrics

| Metric | Baseline | Target | Timeframe | How Measured |
|--------|----------|--------|-----------|-------------|
| Tests Automation pass rate | N/A (new) | 100% | Phase 0 → ongoing | CI script `scripts/ci-automation-full.bat` |
| Drift orbital LEO × 100 révolutions | N/A | < 10 m | Phase 1 (Month 2) | Test `LEO100Revolutions` |
| Conservation d'énergie round-trip Rail↔Local | N/A | > 99.95% | Phase 3 (Month 3) | Test `RoundTripConservation` |
| FPS base TPV 1440p RTX 4070 Ti Super | N/A | > 60 | Phase 4 (Month 4) | `stat fps` PIE |
| FPS séquence lancement 1440p | N/A | > 60 | Phase 6 (Month 5-6) | `stat fps` PIE |
| Mission complete end-to-end sans crash | N/A | 100% | Phase 9 (Month 8) | Test `FullLoop.SuccessfulMission` |
| MetaHumans 3 simultanés 1440p | N/A | > 60 fps | Phase 5 (Month 5) | `stat fps` + `stat GPU` PIE |
| Package Shipping Win64 boot time | N/A | < 20 s | Phase 11 (Month 10+) | Stopwatch laptop tiers |
| Phases MVP complétées | 0/12 | 12/12 | Month 10-12 | PRD status JSON |
| Stories completed | 0/67 | 67/67 | Month 10-12 | `prd-status.json` `stories_done` |

## Open Questions

- **Q1** : Thème musical / direction audio globale — ambient atmosphérique (Interstellar-like) vs rock épique (Chris Hadfield guitare) vs silence immersif (Gravity-like, silence absolu en orbite) ? Décision Arthur avant Phase 11, idéalement Phase 5-6 pour avoir le temps de prototyper l'audio ignition.
- **Q2** : Multilingue — le MVP est en anglais ou en français (localizable ensuite) ? Décision avant Phase 5 (dialogue system charge la langue par défaut).
- **Q3** : Difficulté — un seul mode "realistic" ou modes "easy/normal/hard" avec toggles physique (deltaV infini / navigation simplifiée) ? Décision avant Phase 10 (événements calibration). **Recommandation initiale** : un seul mode "realistic" pour le MVP, difficulté via progression tech tree uniquement.
- **Q4** : Sons de voix crew MetaHuman — enregistrements voice actors pro (payant, ~500-2000€ par personnage) ou voix synthétique (ElevenLabs, ~100€/mois) ou placeholders text-only ? Décision Phase 5.
- **Q5** : Cible Steam release — Early Access ou release directe ? Décision vers Month 8-10 selon feedback phases 0-9.
- **Q6** : Steam store assets (screenshots, trailer, description) — planifier une mini-phase dédiée post-MVP ou intégrer dans Phase 11 ? **Recommandation** : phase 12 Early Access prep, hors MVP scope.
[/PRD]
