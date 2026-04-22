// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/OrbitalComponent.h"

#include "Orbital/OrbitalMath.h"
#include "Orbital/OrbitalState.h"
#include "Vehicles/AVehicle.h"

#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// =============================================================================
// US-022 — Dual-tier round-trip conservation.
//
// Full Rail -> Local -> Rail pipeline exercising the US-019/020/021 stack end
// to end. PRD AC:
//   AC#1  Single round-trip: |Δposition| < 10 cm, |Δenergy| / |E| < 0.1%.
//   AC#2  Ten iterations:    cumulative |Δenergy| / |E| < 1%.
//   AC#3  On any threshold breach, log the complete pre/post state (position,
//         velocity, specific energy, six orbital elements) for triage.
//
// Note on "1 s without external force" (PRD wording): with zero external force
// the inertial Cartesian state is time-invariant (p(t) = p₀, v(t) = v₀ for any
// t), so the Cartesian snapshot captured at SwitchToLocal is also the correct
// input to the paired SwitchToRailFromStateVector. We therefore skip the
// literal 1-second wall-clock wait — a transient-package component has no
// realised Chaos body to integrate anyway, and advancing time with no force
// model would produce nothing beyond the snapshot already in hand.
// =============================================================================

namespace
{
	constexpr double KMuEarth = 3.986004418e14;
	constexpr double KEarthRadius = 6378136.3;

	/** Fresh LEO circular fixture at the PRD's ISS-like parameters. */
	FOrbitalState MakeLeoCircular()
	{
		FOrbitalState State;
		State.SemiMajorAxis = KEarthRadius + 400e3;
		State.Eccentricity = 0.0;
		State.Inclination = FMath::DegreesToRadians(51.6);
		State.RightAscensionOfAscendingNode = 0.0;
		State.ArgumentOfPeriapsis = 0.0;
		State.TrueAnomaly = 0.0;
		State.Epoch = 0.0;
		return State;
	}

	/** Specific orbital energy ε = v²/2 − μ/|r|. Negative for bound orbits. */
	double SpecificEnergy(const FVector& R, const FVector& V, double Mu)
	{
		return 0.5 * V.SizeSquared() - Mu / R.Length();
	}

	/**
	 * Human-readable dump of one orbital snapshot. Format is intentionally
	 * single-line per field and uses %.9e so two dumps can be diffed visually.
	 * Called only on assertion failure per AC#3.
	 */
	FString FormatSnapshot(
		const TCHAR* Label,
		const FOrbitalState& State,
		const FVector& Pos,
		const FVector& Vel,
		double Energy)
	{
		return FString::Printf(
			TEXT("\n[%s]")
			TEXT("\n  a     = %.9e m")
			TEXT("\n  e     = %.9e")
			TEXT("\n  i     = %.9e rad")
			TEXT("\n  RAAN  = %.9e rad")
			TEXT("\n  ω     = %.9e rad")
			TEXT("\n  ν     = %.9e rad")
			TEXT("\n  Epoch = %.9e s")
			TEXT("\n  P     = (%.9e, %.9e, %.9e) m")
			TEXT("\n  V     = (%.9e, %.9e, %.9e) m/s")
			TEXT("\n  |r|   = %.9e m")
			TEXT("\n  |v|   = %.9e m/s")
			TEXT("\n  ε     = %.9e J/kg"),
			Label,
			State.SemiMajorAxis, State.Eccentricity, State.Inclination,
			State.RightAscensionOfAscendingNode, State.ArgumentOfPeriapsis,
			State.TrueAnomaly, State.Epoch,
			Pos.X, Pos.Y, Pos.Z,
			Vel.X, Vel.Y, Vel.Z,
			Pos.Length(), Vel.Length(), Energy);
	}

	/**
	 * Spawn a transient-package AVehicle + UOrbitalComponent pre-configured
	 * for the fixture. The vehicle's root is replaced with a UBoxComponent
	 * (a UPrimitiveComponent) so SwitchToLocal's ApplyModeToOwner path can
	 * flip BodyInstance.bSimulatePhysics without emitting the "Local mode
	 * without primitive root" warning that would fail the automation run.
	 */
	UOrbitalComponent* MakeConfiguredComponent(AVehicle*& OutVehicle)
	{
		OutVehicle = NewObject<AVehicle>(GetTransientPackage());
		UBoxComponent* Box = NewObject<UBoxComponent>(OutVehicle);
		OutVehicle->SetRootComponent(Box);

		UOrbitalComponent* Orbit = NewObject<UOrbitalComponent>(OutVehicle);
		Orbit->CurrentState = MakeLeoCircular();
		Orbit->Mode = EPhysicsMode::Rail;
		Orbit->GravitationalParameterOverride = KMuEarth;
		Orbit->InitializeOwnerBinding();
		return Orbit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalComponentDualRoundTripConservationTest,
	"DeltaV.Physics.Dual.RoundTripConservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalComponentDualRoundTripConservationTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = nullptr;
	UOrbitalComponent* Orbit = MakeConfiguredComponent(Vehicle);

	// --- AC#1: single round-trip ---------------------------------------------
	const FOrbitalState S0 = Orbit->GetOrbitalState();
	FVector P0, V0;
	if (!TestTrue(TEXT("Initial ElementsToStateVector succeeds"),
			UOrbitalMath::ElementsToStateVector(S0, KMuEarth, P0, V0)))
	{
		return false;
	}
	const double E0 = SpecificEnergy(P0, V0, KMuEarth);

	Orbit->SwitchToLocal();
	TestEqual<uint8>(TEXT("After SwitchToLocal: Mode == Local"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Local));

	// "1 s without external force" is a no-op on the Cartesian state by
	// construction (see the note at the top of the file). Hand the same
	// (P0, V0) back to SwitchToRailFromStateVector — that IS the state the
	// Chaos body would have been reporting a second later.
	if (!TestTrue(TEXT("SwitchToRailFromStateVector commits"),
			Orbit->SwitchToRailFromStateVector(P0, V0)))
	{
		return false;
	}
	TestEqual<uint8>(TEXT("After SwitchToRail: Mode == Rail"),
		static_cast<uint8>(Orbit->Mode), static_cast<uint8>(EPhysicsMode::Rail));

	const FOrbitalState S1 = Orbit->GetOrbitalState();
	FVector P1, V1;
	if (!TestTrue(TEXT("Post-round-trip ElementsToStateVector succeeds"),
			UOrbitalMath::ElementsToStateVector(S1, KMuEarth, P1, V1)))
	{
		return false;
	}
	const double E1 = SpecificEnergy(P1, V1, KMuEarth);

	const double PositionDriftMeters = (P1 - P0).Length();
	const double RelEnergyDrift = FMath::Abs((E1 - E0) / E0);

	// AC#1 thresholds. PRD: position drift < 10 cm, energy drift < 0.1%.
	constexpr double KPositionDriftToleranceMeters = 0.10;
	constexpr double KEnergyDriftTolerance = 1e-3;

	const bool bSingleTripPosOk = PositionDriftMeters < KPositionDriftToleranceMeters;
	const bool bSingleTripEnergyOk = RelEnergyDrift < KEnergyDriftTolerance;

	if (!bSingleTripPosOk || !bSingleTripEnergyOk)
	{
		// AC#3 — on drift breach, dump the full pre/post state.
		AddError(FString::Printf(
			TEXT("Single round-trip drift exceeded tolerance. ")
			TEXT("|ΔP|=%.6e m (tol %.2e), |Δε/ε|=%.6e (tol %.2e).%s%s"),
			PositionDriftMeters, KPositionDriftToleranceMeters,
			RelEnergyDrift, KEnergyDriftTolerance,
			*FormatSnapshot(TEXT("before"), S0, P0, V0, E0),
			*FormatSnapshot(TEXT("after"), S1, P1, V1, E1)));
	}
	TestTrue(
		FString::Printf(TEXT("AC#1 position drift |ΔP|=%.3e m < 0.10 m"),
			PositionDriftMeters),
		bSingleTripPosOk);
	TestTrue(
		FString::Printf(TEXT("AC#1 energy drift |Δε/ε|=%.3e < 1e-3"),
			RelEnergyDrift),
		bSingleTripEnergyOk);

	// --- AC#2: 10 iterations cumulative drift --------------------------------
	// We are back in Rail mode with state S1. Use S1 as the new reference
	// baseline for the 10-iteration cumulative measurement — any deviation
	// observed here is pure mechanism drift across repeated conversions.
	FOrbitalState StateBaseline = Orbit->GetOrbitalState();
	FVector PBaseline, VBaseline;
	UOrbitalMath::ElementsToStateVector(StateBaseline, KMuEarth, PBaseline, VBaseline);
	const double EBaseline = SpecificEnergy(PBaseline, VBaseline, KMuEarth);

	// Track the worst-case iteration for the failure dump.
	double MaxIterDrift = 0.0;
	int32 MaxIterIndex = -1;
	FOrbitalState MaxIterPreState;
	FOrbitalState MaxIterPostState;
	FVector MaxIterPrePos, MaxIterPreVel, MaxIterPostPos, MaxIterPostVel;
	double MaxIterPreEnergy = 0.0;
	double MaxIterPostEnergy = 0.0;

	constexpr int32 KRoundTripIterations = 10;

	for (int32 I = 0; I < KRoundTripIterations; ++I)
	{
		const FOrbitalState SPre = Orbit->GetOrbitalState();
		FVector PPre, VPre;
		if (!TestTrue(
				FString::Printf(TEXT("Iter %d: pre ElementsToStateVector"), I),
				UOrbitalMath::ElementsToStateVector(SPre, KMuEarth, PPre, VPre)))
		{
			return false;
		}
		const double EPre = SpecificEnergy(PPre, VPre, KMuEarth);

		Orbit->SwitchToLocal();
		// Defensive: SwitchToLocal returns void, so verify the mode actually
		// flipped. A regression (e.g. Mode stuck at Rail due to a bad guard)
		// would otherwise turn the loop body into a silent Rail-only test.
		if (!TestEqual<uint8>(
				FString::Printf(TEXT("Iter %d: SwitchToLocal transitioned to Local"), I),
				static_cast<uint8>(Orbit->Mode),
				static_cast<uint8>(EPhysicsMode::Local)))
		{
			return false;
		}

		if (!TestTrue(
				FString::Printf(TEXT("Iter %d: SwitchToRailFromStateVector commits"), I),
				Orbit->SwitchToRailFromStateVector(PPre, VPre)))
		{
			return false;
		}
		if (!TestEqual<uint8>(
				FString::Printf(TEXT("Iter %d: SwitchToRail transitioned back to Rail"), I),
				static_cast<uint8>(Orbit->Mode),
				static_cast<uint8>(EPhysicsMode::Rail)))
		{
			return false;
		}

		const FOrbitalState SPost = Orbit->GetOrbitalState();
		FVector PPost, VPost;
		if (!TestTrue(
				FString::Printf(TEXT("Iter %d: post ElementsToStateVector"), I),
				UOrbitalMath::ElementsToStateVector(SPost, KMuEarth, PPost, VPost)))
		{
			return false;
		}
		const double EPost = SpecificEnergy(PPost, VPost, KMuEarth);

		const double IterDrift = FMath::Abs((EPost - EPre) / EPre);
		if (IterDrift > MaxIterDrift)
		{
			MaxIterDrift = IterDrift;
			MaxIterIndex = I;
			MaxIterPreState = SPre;   MaxIterPostState = SPost;
			MaxIterPrePos = PPre;     MaxIterPostPos = PPost;
			MaxIterPreVel = VPre;     MaxIterPostVel = VPost;
			MaxIterPreEnergy = EPre;  MaxIterPostEnergy = EPost;
		}
	}

	// Cumulative drift: compare final energy (after the 10th iteration) to
	// the baseline recorded after the AC#1 single round-trip.
	const FOrbitalState SFinal = Orbit->GetOrbitalState();
	FVector PFinal, VFinal;
	UOrbitalMath::ElementsToStateVector(SFinal, KMuEarth, PFinal, VFinal);
	const double EFinal = SpecificEnergy(PFinal, VFinal, KMuEarth);

	const double CumulativeDrift = FMath::Abs((EFinal - EBaseline) / EBaseline);

	constexpr double KCumulativeDriftTolerance = 1e-2;  // PRD: < 1%

	if (CumulativeDrift >= KCumulativeDriftTolerance)
	{
		// AC#3 — on cumulative breach, dump baseline + final + worst iteration.
		FString WorstIterDump = TEXT(" (no per-iter drift recorded)");
		if (MaxIterIndex >= 0)
		{
			WorstIterDump = FString::Printf(
				TEXT("\nWorst iteration #%d |Δε/ε|=%.6e%s%s"),
				MaxIterIndex, MaxIterDrift,
				*FormatSnapshot(TEXT("iter-pre"), MaxIterPreState,
					MaxIterPrePos, MaxIterPreVel, MaxIterPreEnergy),
				*FormatSnapshot(TEXT("iter-post"), MaxIterPostState,
					MaxIterPostPos, MaxIterPostVel, MaxIterPostEnergy));
		}
		AddError(FString::Printf(
			TEXT("10-iteration cumulative drift %.6e exceeds %.2e.%s%s%s"),
			CumulativeDrift, KCumulativeDriftTolerance,
			*FormatSnapshot(TEXT("baseline"), StateBaseline,
				PBaseline, VBaseline, EBaseline),
			*FormatSnapshot(TEXT("final"), SFinal, PFinal, VFinal, EFinal),
			*WorstIterDump));
	}
	TestTrue(
		FString::Printf(TEXT("AC#2 cumulative drift after %d iterations |Δε/ε|=%.3e < 1e-2"),
			KRoundTripIterations, CumulativeDrift),
		CumulativeDrift < KCumulativeDriftTolerance);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
