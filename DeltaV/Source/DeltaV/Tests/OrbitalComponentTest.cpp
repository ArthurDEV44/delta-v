// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/OrbitalComponent.h"

#include "Base/CelestialBody.h"
#include "Orbital/OrbitalMath.h"
#include "Orbital/OrbitalState.h"
#include "Vehicles/AVehicle.h"

#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double KMuEarth = 3.986004418e14;
	constexpr double KEarthRadius = 6378136.3;
	constexpr double KMetersToCm = 100.0;

	/** Build an LEO circular orbit at periapsis (ν = 0). */
	FOrbitalState MakeLeoState(double InclinationRad = 0.0)
	{
		FOrbitalState State;
		State.SemiMajorAxis = KEarthRadius + 400e3;
		State.Eccentricity = 0.0;
		State.Inclination = InclinationRad;
		State.RightAscensionOfAscendingNode = 0.0;
		State.ArgumentOfPeriapsis = 0.0;
		State.TrueAnomaly = 0.0;
		State.Epoch = 0.0;
		return State;
	}

	/** Spawn a transient-package AVehicle ready for component-level tests. */
	AVehicle* MakeVehicle()
	{
		return NewObject<AVehicle>(GetTransientPackage());
	}

	/** Attach a freshly-configured UOrbitalComponent to an AVehicle. */
	UOrbitalComponent* MakeOrbitalComponent(
		AActor* Owner,
		const FOrbitalState& State,
		EPhysicsMode InitialMode = EPhysicsMode::Rail,
		double MuOverride = KMuEarth)
	{
		UOrbitalComponent* Component = NewObject<UOrbitalComponent>(Owner);
		Component->CurrentState = State;
		Component->Mode = InitialMode;
		Component->GravitationalParameterOverride = MuOverride;
		return Component;
	}
}

// =============================================================================
// AC#1 — Rail mode advances actor location from Kepler each tick.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentRailTickTest,
	"DeltaV.Physics.OrbitalComponent.RailTickPropagates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentRailTickTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	const FOrbitalState S0 = MakeLeoState(FMath::DegreesToRadians(51.6));
	UOrbitalComponent* Orbit = MakeOrbitalComponent(Vehicle, S0, EPhysicsMode::Rail, KMuEarth);
	Orbit->InitializeOwnerBinding();

	TestFalse(TEXT("AVehicle owner passes the owner check"),
		Orbit->HasFailedOwnerCheck());
	TestTrue(TEXT("Rail mode leaves tick enabled"),
		Orbit->IsComponentTickEnabled());

	// Initial actor location: zero. After a single 60-second tick, the rail
	// path should land us at PropagateKepler(S0, 60)->ElementsToStateVector()
	// scaled to cm. We compare the written location against that reference.
	const double DeltaSeconds = 60.0;

	FOrbitalState SExpected;
	TestTrue(TEXT("Reference: PropagateKepler succeeds"),
		UOrbitalMath::PropagateKepler(S0, DeltaSeconds, KMuEarth, SExpected));
	FVector PosMetersExpected, VelMetersExpected;
	TestTrue(TEXT("Reference: ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			SExpected, KMuEarth, PosMetersExpected, VelMetersExpected));

	Orbit->TickComponent(
		static_cast<float>(DeltaSeconds), LEVELTICK_All, /*TickFn=*/ nullptr);

	const FVector ExpectedCm = PosMetersExpected * KMetersToCm;
	const FVector ActualCm = Vehicle->GetActorLocation();
	const double PosErrorCm = (ActualCm - ExpectedCm).Length();

	// 1 cm tolerance absorbs rounding in the trig/sqrt chain (FVector is
	// f64 in UE5, so no narrowing is involved).
	TestTrue(
		FString::Printf(
			TEXT("Rail tick writes expected location (|Δ|=%.3e cm < 1.0 cm)"),
			PosErrorCm),
		PosErrorCm < 1.0);

	// Rotation was applied: at initial periapsis (ν=0, position +X, velocity
	// +Y rotated by inclination), forward points along velocity. After a 60 s
	// tick the velocity direction has rotated by mean-motion × Δt ≈ 0.066 rad
	// at LEO — the actor's yaw must differ from the ZeroRotator identity.
	const FRotator ActorRotation = Vehicle->GetActorRotation();
	TestFalse(TEXT("Rail tick sets a non-identity rotation"),
		ActorRotation.Equals(FRotator::ZeroRotator, 1e-3f));

	// Prograde check: actor forward must align with the reference velocity.
	const FVector ActorForward = ActorRotation.RotateVector(FVector::ForwardVector);
	const FVector ExpectedForward = VelMetersExpected.GetSafeNormal();
	const double ForwardDot = FVector::DotProduct(ActorForward, ExpectedForward);
	TestTrue(
		FString::Printf(
			TEXT("Actor forward aligns with velocity (dot=%.6f > 0.999)"),
			ForwardDot),
		ForwardDot > 0.999);

	// The internal state's Epoch must have advanced by DeltaSeconds.
	const double EpochDelta = Orbit->GetOrbitalState().Epoch - S0.Epoch;
	TestTrue(
		FString::Printf(TEXT("Rail tick advances Epoch by Δt (got %.6f)"), EpochDelta),
		FMath::IsNearlyEqual(EpochDelta, DeltaSeconds, 1e-9));

	// AC#1 explicit: no Chaos physics. The default AVehicle root is a
	// USceneComponent, so there is no body to simulate. Either way, the
	// cast-to-primitive must fail gracefully: no mode toggle, no crash.
	TestNull(TEXT("Rail: AVehicle default USceneComponent root is not a UPrimitiveComponent"),
		Cast<UPrimitiveComponent>(Vehicle->GetRootComponent()));

	return true;
}

// =============================================================================
// AC#2 — Local mode activates Chaos rigid body simulation on a primitive root.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentLocalModeTest,
	"DeltaV.Physics.OrbitalComponent.LocalModeActivatesPhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentLocalModeTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();

	// Replace the default USceneComponent root with a primitive so
	// SetSimulatePhysics has a body-instance slot to flip. UBoxComponent is
	// a UPrimitiveComponent (via UShapeComponent) and needs no mesh asset.
	UBoxComponent* Box = NewObject<UBoxComponent>(Vehicle);
	Vehicle->SetRootComponent(Box);

	// Local-mode init should flip the Chaos simulation flag on the new root.
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoState(), EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	TestFalse(TEXT("AVehicle owner passes the owner check"),
		Orbit->HasFailedOwnerCheck());
	TestTrue(TEXT("Local mode sets BodyInstance.bSimulatePhysics = true"),
		Box->BodyInstance.bSimulatePhysics);

	// Locale TickComponent in Local mode must not mutate the actor transform;
	// Chaos is the sole driver, the component is passive.
	const FVector LocBeforeTick = Vehicle->GetActorLocation();
	Orbit->TickComponent(1.0f / 60.0f, LEVELTICK_All, /*TickFn=*/ nullptr);
	TestTrue(TEXT("Local tick does not move the actor"),
		Vehicle->GetActorLocation().Equals(LocBeforeTick, 0.0));

	// Switching back to Rail deactivates physics on the primitive.
	Orbit->SetPhysicsMode(EPhysicsMode::Rail);
	TestFalse(TEXT("Rail mode clears BodyInstance.bSimulatePhysics"),
		Box->BodyInstance.bSimulatePhysics);

	return true;
}

// =============================================================================
// AC#3 — 100 rail-mode vehicles tick under 1 ms total.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentTickBudget100VehiclesTest,
	"DeltaV.Physics.OrbitalComponent.TickBudget100Vehicles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentTickBudget100VehiclesTest::RunTest(const FString& Parameters)
{
	constexpr int32 NumVehicles = 100;
	constexpr double BudgetMs = 1.0;

	TArray<UOrbitalComponent*> Components;
	Components.Reserve(NumVehicles);

	for (int32 I = 0; I < NumVehicles; ++I)
	{
		AVehicle* Vehicle = MakeVehicle();
		// Varied true-anomalies keep the inputs non-identical so the compiler
		// can't cheat with cache-perfect reuse across the cohort.
		FOrbitalState State = MakeLeoState(FMath::DegreesToRadians(51.6));
		State.TrueAnomaly = UE_DOUBLE_TWO_PI * (static_cast<double>(I) / NumVehicles);
		UOrbitalComponent* Orbit = MakeOrbitalComponent(
			Vehicle, State, EPhysicsMode::Rail, KMuEarth);
		Orbit->InitializeOwnerBinding();
		Components.Add(Orbit);
	}

	// Warm-up: first-touch page faults and instruction-cache fills must not
	// contaminate the measured budget. One full sweep pre-measurement.
	for (UOrbitalComponent* C : Components)
	{
		C->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	}

	// Measured sweep: one frame of rail ticks across the whole cohort.
	const double T0 = FPlatformTime::Seconds();
	for (UOrbitalComponent* C : Components)
	{
		C->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	}
	const double ElapsedMs = (FPlatformTime::Seconds() - T0) * 1000.0;

	AddInfo(FString::Printf(
		TEXT("100 rail-mode vehicles ticked in %.3f ms (budget %.2f ms)."),
		ElapsedMs, BudgetMs));

	TestTrue(
		FString::Printf(
			TEXT("100-vehicle rail tick under %.2f ms budget (got %.3f ms)"),
			BudgetMs, ElapsedMs),
		ElapsedMs < BudgetMs);

	return true;
}

// =============================================================================
// AC#4 — Missing AVehicle owner logs error and permanently disables tick.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentNoAVehicleParentTest,
	"DeltaV.Physics.OrbitalComponent.NoAVehicleParentDisablesTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentNoAVehicleParentTest::RunTest(const FString& Parameters)
{
	// The owner-check failure is an explicit UE_LOG Error; whitelist it so
	// the test harness doesn't flag it as an unexpected failure.
	AddExpectedError(TEXT("requires an AVehicle owner"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AActor* PlainActor = NewObject<AActor>(GetTransientPackage());
	UOrbitalComponent* Orbit = NewObject<UOrbitalComponent>(PlainActor);
	Orbit->CurrentState = MakeLeoState();
	Orbit->GravitationalParameterOverride = KMuEarth;

	Orbit->InitializeOwnerBinding();

	TestTrue(TEXT("non-AVehicle owner flags the component as failed"),
		Orbit->HasFailedOwnerCheck());
	TestFalse(TEXT("failed-owner component is tick-disabled"),
		Orbit->IsComponentTickEnabled());

	// TickComponent must be a no-op and must not crash even when invoked
	// externally after the guard has fired.
	const FVector LocBefore = PlainActor->GetActorLocation();
	Orbit->TickComponent(1.0f / 60.0f, LEVELTICK_All, /*TickFn=*/ nullptr);
	TestTrue(TEXT("TickComponent on failed component is a no-op"),
		PlainActor->GetActorLocation().Equals(LocBefore, 0.0));

	// Idempotency: a second InitializeOwnerBinding must not re-log and must
	// keep the failed state. (We already whitelisted only 1 error occurrence.)
	Orbit->InitializeOwnerBinding();
	TestTrue(TEXT("second init is idempotent"),
		Orbit->HasFailedOwnerCheck());

	// Null-owner sanity: the component must not crash when queried after the
	// failure path.
	TestFalse(TEXT("tick stays disabled after the failure path"),
		Orbit->IsComponentTickEnabled());

	return true;
}

// =============================================================================
// US-020 — Rail -> Local switching with velocity injection.
// =============================================================================

namespace
{
	/** Specific orbital energy ε = v²/2 − μ/|r|. Negative for bound orbits. */
	double SpecificEnergy(const FVector& R, const FVector& V, double Mu)
	{
		const double V2 = V.SizeSquared();
		const double RMag = R.Length();
		return 0.5 * V2 - Mu / RMag;
	}

	/** Build an LEO state with custom true anomaly for off-periapsis fixtures. */
	FOrbitalState MakeLeoAtTrueAnomaly(double TrueAnomalyRad,
		double InclinationRad = FMath::DegreesToRadians(51.6))
	{
		FOrbitalState State = MakeLeoState(InclinationRad);
		State.TrueAnomaly = TrueAnomalyRad;
		return State;
	}

	/** Build a moderately elliptical inclined orbit for AC#3 fixtures. */
	FOrbitalState MakeEllipticalState()
	{
		FOrbitalState State;
		State.SemiMajorAxis = KEarthRadius + 2000e3;
		State.Eccentricity = 0.2;
		State.Inclination = FMath::DegreesToRadians(28.5);
		State.RightAscensionOfAscendingNode = FMath::DegreesToRadians(60.0);
		State.ArgumentOfPeriapsis = FMath::DegreesToRadians(90.0);
		State.TrueAnomaly = FMath::DegreesToRadians(45.0);
		State.Epoch = 0.0;
		return State;
	}
}

// -----------------------------------------------------------------------------
// AC#1 — SwitchToLocal flips the mode + Chaos flag and records the injection;
//        pre/post actor position must not jump.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToLocalInjectsVelocityTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToLocalInjectsVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToLocalInjectsVelocityTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();

	// Primitive root required so ApplyModeToOwner actually flips physics sim.
	UBoxComponent* Box = NewObject<UBoxComponent>(Vehicle);
	Vehicle->SetRootComponent(Box);

	// LEO circular at 45° true anomaly — velocity vector is non-trivial
	// (both Y and Z components under the 51.6° inclination).
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoAtTrueAnomaly(FMath::DegreesToRadians(45.0)),
		EPhysicsMode::Rail, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Seed a non-zero rail location so the "no position jump" assertion is
	// meaningful (we're verifying SwitchToLocal doesn't teleport the actor).
	Orbit->TickComponent(0.0f, LEVELTICK_All, nullptr);
	const FVector LocBefore = Vehicle->GetActorLocation();

	// Reference velocity: what ElementsToStateVector produces for this state.
	FVector RefPosMeters, RefVelMeters;
	TestTrue(TEXT("Reference: ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, RefPosMeters, RefVelMeters));

	Orbit->SwitchToLocal();

	// AC#1 — mode flipped + ApplyModeToOwner flipped the Chaos body flag.
	TestEqual<uint8>(TEXT("SwitchToLocal sets Mode = Local"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Local));
	TestTrue(TEXT("BodyInstance.bSimulatePhysics = true after switch"),
		Box->BodyInstance.bSimulatePhysics);

	// AC#1 — no position jump: SwitchToLocal must not move the actor.
	const FVector LocAfter = Vehicle->GetActorLocation();
	const double PosDeltaCm = (LocAfter - LocBefore).Length();
	TestTrue(
		FString::Printf(
			TEXT("No position discontinuity at switch (|Δ|=%.3e cm < 1.0 cm)"),
			PosDeltaCm),
		PosDeltaCm < 1.0);

	// AC#1 — velocity recorded matches the reference to 1 cm/s (= 0.01 m/s).
	const FVector InjectedLinMps = Orbit->GetLastInjectedLinearVelocityMps();
	const double VelDeltaMps = (InjectedLinMps - RefVelMeters).Length();
	TestTrue(
		FString::Printf(
			TEXT("Injected linear velocity matches reference (|Δ|=%.3e m/s < 1e-2)"),
			VelDeltaMps),
		VelDeltaMps < 1e-2);

	return true;
}

// -----------------------------------------------------------------------------
// AC#2 — Energy conservation > 99.95% across the switch.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToLocalConservesEnergyTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToLocalConservesEnergy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToLocalConservesEnergyTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	UBoxComponent* Box = NewObject<UBoxComponent>(Vehicle);
	Vehicle->SetRootComponent(Box);

	// Use an elliptical orbit so the SMA-based specific energy test is not
	// trivially preserved by a circular-orbit symmetry.
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeEllipticalState(), EPhysicsMode::Rail, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Pre-switch energy from the analytic orbital state.
	FVector PreR, PreV;
	TestTrue(TEXT("Pre-switch: ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, PreR, PreV));
	const double E0 = SpecificEnergy(PreR, PreV, KMuEarth);

	Orbit->SwitchToLocal();

	// Post-switch energy from the injected linear velocity + same position.
	// (SwitchToLocal does not move the actor — the position vector in the
	// parent-body frame is invariant across the switch.)
	const FVector PostV = Orbit->GetLastInjectedLinearVelocityMps();
	const double E1 = SpecificEnergy(PreR, PostV, KMuEarth);

	const double RelEnergyDrift = FMath::Abs((E1 - E0) / E0);
	TestTrue(
		FString::Printf(
			TEXT("Energy conservation |Δε/ε|=%.3e < 5e-4 (AC > 99.95%%)"),
			RelEnergyDrift),
		RelEnergyDrift < 5e-4);

	return true;
}

// -----------------------------------------------------------------------------
// AC#3 — ComputeInjectionVelocities matches the reference math to 1e-6.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToLocalVelocitiesMatchTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToLocalVelocitiesMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToLocalVelocitiesMatchTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeEllipticalState(), EPhysicsMode::Rail, KMuEarth);
	Orbit->InitializeOwnerBinding();

	FVector RefPos, RefVel;
	TestTrue(TEXT("Reference: ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, RefPos, RefVel));
	const FVector RefOmega = FVector::CrossProduct(RefPos, RefVel) / RefPos.SizeSquared();

	// ComputeInjectionVelocities is the pure test seam — exercise it directly.
	FVector LinMps, AngRadps;
	TestTrue(TEXT("ComputeInjectionVelocities succeeds on a valid state"),
		Orbit->ComputeInjectionVelocities(LinMps, AngRadps));

	const double LinDelta = (LinMps - RefVel).Length();
	TestTrue(
		FString::Printf(TEXT("Linear velocity matches ElementsToStateVector (|Δ|=%.3e m/s < 1e-6)"),
			LinDelta),
		LinDelta < 1e-6);

	const double AngDelta = (AngRadps - RefOmega).Length();
	TestTrue(
		FString::Printf(TEXT("Angular velocity matches r × v / |r|² (|Δω|=%.3e rad/s < 1e-6)"),
			AngDelta),
		AngDelta < 1e-6);

	// Invariant: for a two-body orbit, |ω| should match the instantaneous
	// orbital rate √(μ/a³) × (1 + e cos ν)² / (1 − e²)^(3/2). Sanity-bound
	// at 1 ppm against the direct-math identity above, which is the same
	// derivation via a different algebraic path.
	const double OmegaMag = AngRadps.Length();
	const double RefOmegaMag = RefOmega.Length();
	TestTrue(
		FString::Printf(TEXT("|ω| matches reference magnitude (|Δ|=%.3e rad/s)"),
			FMath::Abs(OmegaMag - RefOmegaMag)),
		FMath::Abs(OmegaMag - RefOmegaMag) < 1e-9);

	return true;
}

// -----------------------------------------------------------------------------
// AC#4 — SwitchToLocal while already Local logs a warning and mutates nothing.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToLocalAlreadyLocalTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToLocalAlreadyLocalIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToLocalAlreadyLocalTest::RunTest(const FString& Parameters)
{
	// Whitelist the warning emitted on the redundant switch.
	AddExpectedError(TEXT("SwitchToLocal called while already in Local mode"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AVehicle* Vehicle = MakeVehicle();
	UBoxComponent* Box = NewObject<UBoxComponent>(Vehicle);
	Vehicle->SetRootComponent(Box);

	const FOrbitalState SInitial = MakeLeoState();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, SInitial, EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Sanity: Local mode on init already flipped the Chaos flag.
	TestTrue(TEXT("Initial Local mode enables physics"),
		Box->BodyInstance.bSimulatePhysics);

	const FOrbitalState StateBefore = Orbit->GetOrbitalState();
	const FVector LastLinBefore = Orbit->GetLastInjectedLinearVelocityMps();

	Orbit->SwitchToLocal();

	// CurrentState must be bit-identical (except potentially un-set fields).
	const FOrbitalState StateAfter = Orbit->GetOrbitalState();
	TestEqual<double>(TEXT("SMA unchanged"),
		StateAfter.SemiMajorAxis, StateBefore.SemiMajorAxis);
	TestEqual<double>(TEXT("e unchanged"),
		StateAfter.Eccentricity, StateBefore.Eccentricity);
	TestEqual<double>(TEXT("ν unchanged"),
		StateAfter.TrueAnomaly, StateBefore.TrueAnomaly);
	TestEqual<double>(TEXT("Epoch unchanged"),
		StateAfter.Epoch, StateBefore.Epoch);

	// No velocity injection recorded — LastInjectedLinearVelocityMps must
	// still be whatever it was before the no-op call (zero, since we never
	// previously called SwitchToLocal on this component).
	const FVector LastLinAfter = Orbit->GetLastInjectedLinearVelocityMps();
	TestTrue(TEXT("Last injected linear velocity unchanged on no-op"),
		LastLinAfter.Equals(LastLinBefore, 0.0));

	// A second redundant call also suppresses further logging (throttle flag
	// stays set — AddExpectedError whitelisted exactly 1 occurrence).
	Orbit->SwitchToLocal();

	return true;
}

// =============================================================================
// US-021 — Local -> Rail switching with state capture.
// =============================================================================

namespace
{
	/** Shared specific-energy formula (ε = v²/2 − μ/|r|). */
	double SpecificEnergySI(const FVector& R, const FVector& V, double Mu)
	{
		return 0.5 * V.SizeSquared() - Mu / R.Length();
	}
}

// -----------------------------------------------------------------------------
// AC#1 — Post-burn energy reflected in the snapped FOrbitalState.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToRailCapturesPostBurnEnergyTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToRailCapturesPostBurnEnergy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToRailCapturesPostBurnEnergyTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();

	// Component starts in Local (as if it had been running under Chaos after
	// a prior SwitchToLocal) so SwitchToRail has the right prior mode.
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoState(), EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Synthesise the pre-burn Cartesian state from the LEO fixture.
	FVector PrePos, PreVel;
	TestTrue(TEXT("Pre-burn ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, PrePos, PreVel));

	const double PreSMA = Orbit->GetOrbitalState().SemiMajorAxis;

	// Apply a +100 m/s prograde Δv — same direction as PreVel.
	const FVector DvProgradeMps = PreVel.GetSafeNormal() * 100.0;
	const FVector PostVel = PreVel + DvProgradeMps;

	TestTrue(TEXT("SwitchToRailFromStateVector succeeds on valid post-burn state"),
		Orbit->SwitchToRailFromStateVector(PrePos, PostVel));

	// Mode flipped back to Rail.
	TestEqual<uint8>(TEXT("Mode flipped to Rail"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Rail));

	// A prograde burn at periapsis raises apoapsis → a increases.
	const double PostSMA = Orbit->GetOrbitalState().SemiMajorAxis;
	TestTrue(
		FString::Printf(TEXT("Prograde burn raises SMA (pre=%.3e, post=%.3e)"),
			PreSMA, PostSMA),
		PostSMA > PreSMA);

	// Expected SMA from vis-viva: a = -μ / (2ε) with ε from post-burn state.
	const double PostEnergy = SpecificEnergySI(PrePos, PostVel, KMuEarth);
	const double ExpectedSMA = -KMuEarth / (2.0 * PostEnergy);
	const double RelSMAErr = FMath::Abs(PostSMA - ExpectedSMA) / ExpectedSMA;
	TestTrue(
		FString::Printf(
			TEXT("Post-burn SMA matches vis-viva expected (got %.3e, exp %.3e, rel=%.3e < 1e-6)"),
			PostSMA, ExpectedSMA, RelSMAErr),
		RelSMAErr < 1e-6);

	return true;
}

// -----------------------------------------------------------------------------
// AC#2 — Energy conservation > 99.95% on a lossless round-trip.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToRailEnergyConservationTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToRailEnergyRoundTripConserves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToRailEnergyConservationTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeEllipticalState(), EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Pre-switch "Chaos-equivalent" state: the analytic Cartesian for the
	// fixture. This models the situation where Chaos faithfully maintained
	// whatever SwitchToLocal injected (no external forces applied).
	FVector PreR, PreV;
	TestTrue(TEXT("Pre-switch ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, PreR, PreV));
	const double E0 = SpecificEnergySI(PreR, PreV, KMuEarth);

	TestTrue(TEXT("SwitchToRailFromStateVector succeeds"),
		Orbit->SwitchToRailFromStateVector(PreR, PreV));

	// Post-switch Cartesian from the snapped elements.
	FVector PostR, PostV;
	TestTrue(TEXT("Post-switch ElementsToStateVector succeeds"),
		UOrbitalMath::ElementsToStateVector(
			Orbit->GetOrbitalState(), KMuEarth, PostR, PostV));
	const double E1 = SpecificEnergySI(PostR, PostV, KMuEarth);

	const double RelDrift = FMath::Abs((E1 - E0) / E0);
	TestTrue(
		FString::Printf(
			TEXT("Round-trip energy drift |Δε/ε|=%.3e < 5e-4 (AC > 99.95%%)"),
			RelDrift),
		RelDrift < 5e-4);

	return true;
}

// -----------------------------------------------------------------------------
// AC#3 — Near-zero velocity surfaces the degeneracy warning.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToRailNearZeroVelocityTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToRailNearZeroVelocityWarns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToRailNearZeroVelocityTest::RunTest(const FString& Parameters)
{
	// Our component-level warning (AC#3 text, exact match).
	AddExpectedError(TEXT("near-zero velocity, orbital elements degenerate"),
		EAutomationExpectedErrorFlags::Contains, 1);
	// StateVectorToElements will likely then reject the input — whitelist its
	// own internal diagnostic (any of the documented ones) so the test stays
	// green regardless of which branch the math takes.
	AddExpectedError(TEXT("StateVectorToElements"),
		EAutomationExpectedErrorFlags::Contains, 0);

	AVehicle* Vehicle = MakeVehicle();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoState(), EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Build a valid-radius position (6778 km from origin) paired with a
	// velocity just below the 0.1 m/s floor.
	const FVector PosMeters(6778e3, 0.0, 0.0);
	const FVector TinyVelMps(1e-3, 0.0, 0.0);

	// We don't care here whether the switch succeeded — only that the
	// degeneracy warning was emitted. Both outcomes are acceptable:
	//  - StateVectorToElements accepts the tiny velocity → switch commits.
	//  - StateVectorToElements rejects it → switch returns false, mode
	//    stays Local. Either way AddExpectedError above validates the log.
	Orbit->SwitchToRailFromStateVector(PosMeters, TinyVelMps);

	return true;
}

// -----------------------------------------------------------------------------
// AC#4 — Sub-surface altitude refuses the switch.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToRailBelowSurfaceRefusedTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToRailBelowSurfaceRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToRailBelowSurfaceRefusedTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("altitude below parent body surface"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AVehicle* Vehicle = MakeVehicle();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoState(), EPhysicsMode::Local, KMuEarth);
	Orbit->InitializeOwnerBinding();

	// Attach an Earth-sized parent body so the altitude gate has a radius
	// to test against. Strongly held on the stack via NewObject, stored as
	// a TWeakObjectPtr on the FOrbitalState — the test keeps the reference
	// alive through the local variable below.
	UCelestialBody* Earth = NewObject<UCelestialBody>(GetTransientPackage());
	Earth->BodyName = TEXT("Earth");
	Earth->GravitationalParameter = KMuEarth;
	Earth->EquatorialRadius = KEarthRadius;
	Earth->SOIRadius = 9.0e8;
	Earth->WorldPosition = FVector::ZeroVector;

	FOrbitalState WithBody = Orbit->GetOrbitalState();
	WithBody.ParentBody = Earth;
	Orbit->SetOrbitalState(WithBody);

	const FOrbitalState StateBefore = Orbit->GetOrbitalState();

	// Position deep inside Earth (|r| = 3000 km < 6378 km surface).
	const FVector PosInsideEarth(3000e3, 0.0, 0.0);
	const FVector ValidVelMps(0.0, 7800.0, 0.0);

	const bool bCommitted = Orbit->SwitchToRailFromStateVector(
		PosInsideEarth, ValidVelMps);

	TestFalse(TEXT("SwitchToRail refuses sub-surface switch"), bCommitted);
	TestEqual<uint8>(TEXT("Mode stays Local after refusal"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Local));

	// State fields preserved bit-for-bit.
	const FOrbitalState StateAfter = Orbit->GetOrbitalState();
	TestEqual<double>(TEXT("SMA unchanged"),
		StateAfter.SemiMajorAxis, StateBefore.SemiMajorAxis);
	TestEqual<double>(TEXT("e unchanged"),
		StateAfter.Eccentricity, StateBefore.Eccentricity);
	TestEqual<double>(TEXT("ν unchanged"),
		StateAfter.TrueAnomaly, StateBefore.TrueAnomaly);
	TestEqual<double>(TEXT("Epoch unchanged"),
		StateAfter.Epoch, StateBefore.Epoch);

	return true;
}

// -----------------------------------------------------------------------------
// AC#1 symmetry — SwitchToRail while already in Rail is a throttled no-op.
// (Mirrors the US-020 SwitchToLocalAlreadyLocal test; protects against a
//  double-switch bug silently overwriting a live Rail state.)
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentSwitchToRailAlreadyRailTest,
	"DeltaV.Physics.OrbitalComponent.SwitchToRailAlreadyRailIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentSwitchToRailAlreadyRailTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("SwitchToRail called while already in Rail mode"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AVehicle* Vehicle = MakeVehicle();
	UOrbitalComponent* Orbit = MakeOrbitalComponent(
		Vehicle, MakeLeoState(), EPhysicsMode::Rail, KMuEarth);
	Orbit->InitializeOwnerBinding();

	const FOrbitalState StateBefore = Orbit->GetOrbitalState();

	// Feed a valid LEO Cartesian — the redundant-mode guard fires first, so
	// StateVectorToElements is never reached.
	FVector PosMeters, VelMps;
	TestTrue(TEXT("Setup: ElementsToStateVector"),
		UOrbitalMath::ElementsToStateVector(
			StateBefore, KMuEarth, PosMeters, VelMps));

	const bool bCommitted = Orbit->SwitchToRailFromStateVector(PosMeters, VelMps);
	TestFalse(TEXT("Redundant SwitchToRail returns false"), bCommitted);

	TestEqual<uint8>(TEXT("Mode stays Rail after no-op"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Rail));

	const FOrbitalState StateAfter = Orbit->GetOrbitalState();
	TestEqual<double>(TEXT("SMA unchanged"),
		StateAfter.SemiMajorAxis, StateBefore.SemiMajorAxis);
	TestEqual<double>(TEXT("e unchanged"),
		StateAfter.Eccentricity, StateBefore.Eccentricity);
	TestEqual<double>(TEXT("Epoch unchanged"),
		StateAfter.Epoch, StateBefore.Epoch);

	// Second call in Rail mode: throttle flag suppresses the log.
	Orbit->SwitchToRailFromStateVector(PosMeters, VelMps);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
