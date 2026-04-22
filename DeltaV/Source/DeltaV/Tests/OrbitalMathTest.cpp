// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orbital/OrbitalMath.h"
#include "Orbital/OrbitalState.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OrbitalMathTestNS
{
	/** Standard gravitational parameter of Earth (m^3 / s^2), CODATA-like. */
	constexpr double KMuEarth = 3.986004418e14;

	/** Radius of Earth used to build the ISS LEO fixture (m). */
	constexpr double KEarthRadius = 6378136.3;
}
using namespace OrbitalMathTestNS;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalMathConversionsTest,
	"DeltaV.Orbital.OrbitalMath.Conversions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalMathConversionsTest::RunTest(const FString& Parameters)
{
	// AC#1 — ISS-like LEO at 400 km altitude.
	// Use a canonical circular orbit: Pos along X, Vel along Y, i=0 would be equatorial;
	// put it at i=51.6 deg to match ISS, with r = 6378.1363 km + 400 km.
	{
		const double Altitude = 400e3;
		const double R = KEarthRadius + Altitude;
		// Circular speed v = sqrt(mu / r) introduces e ~ 0; push slightly elliptical
		// to match the PRD's e ~ 0.0003 expectation for an ISS-like orbit.
		const double VCirc = FMath::Sqrt(KMuEarth / R);
		// Perturb tangential speed by ~0.00015 to target e ~ 0.0003.
		const double VTan = VCirc * 1.00015;
		// Place at the ascending node with inclination 51.6 deg.
		const double Incl = FMath::DegreesToRadians(51.6);

		const FVector Pos(R, 0.0, 0.0);
		// Velocity in the orbital plane, pitched by Incl.
		const FVector Vel(0.0, VTan * FMath::Cos(Incl), VTan * FMath::Sin(Incl));

		FOrbitalState State;
		const bool bOk = UOrbitalMath::StateVectorToElements(Pos, Vel, KMuEarth, State);
		TestTrue(TEXT("ISS RV->COE returns true"), bOk);
		if (!bOk)
		{
			return false;
		}

		// Expected SMA from the constructed orbit: |v|^2 = Mu*(2/r - 1/a) so
		// a = 1 / (2/r - v^2/mu). With v = v_circ*(1 + 1.5e-4):
		const double VTanSq = VTan * VTan;
		const double ExpectedA = 1.0 / (2.0 / R - VTanSq / KMuEarth);

		// a ≈ expected value ± 1 m — AC#1 first half.
		TestTrue(
			FString::Printf(TEXT("ISS semi-major axis within 1 m of expected (a=%.6f m, expected=%.6f m)"),
				State.SemiMajorAxis, ExpectedA),
			FMath::Abs(State.SemiMajorAxis - ExpectedA) < 1.0);

		// e ≈ 3e-4 ± 1e-5 — AC#1 second half. Radial-free tangential push gives
		// e = ((1+δ)^2 - 1) ≈ 2δ for small δ; with δ = 1.5e-4, e ≈ 3e-4.
		TestTrue(
			FString::Printf(TEXT("ISS eccentricity ~3e-4 (e=%.6f)"),
				State.Eccentricity),
			FMath::Abs(State.Eccentricity - 3.0e-4) < 1e-5);

		// Inclination close to 51.6 deg.
		TestTrue(
			FString::Printf(TEXT("ISS inclination near 51.6 deg (i=%.6f rad)"),
				State.Inclination),
			FMath::Abs(FMath::RadiansToDegrees(State.Inclination) - 51.6) < 1e-6);
	}

	// AC#2 — Round-trip on three representative orbits. Tolerance per PRD:
	// |ΔPos| < 1 mm, |ΔVel| < 1 µm/s.
	{
		struct FRoundTripCase
		{
			const TCHAR* Name;
			FVector Pos;
			FVector Vel;
		};

		const TArray<FRoundTripCase> Cases = {
			// ISS-like LEO.
			{
				TEXT("ISS LEO"),
				FVector(6778136.3, 0.0, 0.0),
				FVector(0.0, 4764.0, 6046.0)  // ~ 7.69 km/s split across yz
			},
			// GTO — perigee 6578 km, apogee 42164 km.
			{
				TEXT("GTO at perigee"),
				FVector(6578000.0, 0.0, 0.0),
				FVector(0.0, 10240.0, 0.0)
			},
			// Inclined Molniya-like.
			{
				TEXT("Molniya-like"),
				FVector(6800000.0, 0.0, 0.0),
				FVector(0.0, 8300.0, 6500.0)
			}
		};

		for (const FRoundTripCase& Case : Cases)
		{
			FOrbitalState State;
			const bool bForward = UOrbitalMath::StateVectorToElements(
				Case.Pos, Case.Vel, KMuEarth, State);
			if (!TestTrue(FString::Printf(TEXT("[%s] RV->COE succeeded"), Case.Name), bForward))
			{
				continue;
			}

			FVector Pos2, Vel2;
			const bool bInverse = UOrbitalMath::ElementsToStateVector(
				State, KMuEarth, Pos2, Vel2);
			if (!TestTrue(FString::Printf(TEXT("[%s] COE->RV succeeded"), Case.Name), bInverse))
			{
				continue;
			}

			const double DPos = (Pos2 - Case.Pos).Length();
			const double DVel = (Vel2 - Case.Vel).Length();

			TestTrue(
				FString::Printf(TEXT("[%s] |ΔPos|=%.3e m < 1 mm"), Case.Name, DPos),
				DPos < 1e-3);
			TestTrue(
				FString::Printf(TEXT("[%s] |ΔVel|=%.3e m/s < 1 µm/s"), Case.Name, DVel),
				DVel < 1e-6);
		}
	}

	// AC#3 — Near-circular orbit must produce finite elements (no NaN) with the
	// degenerate-case fallbacks. Construct a circular-inclined orbit with |v| = v_circ
	// exactly, which forces e below the 1e-6 fallback threshold and routes through
	// the argument-of-latitude branch.
	{
		const double R = 6778e3;
		const double VCirc = FMath::Sqrt(KMuEarth / R);
		const double Incl = FMath::DegreesToRadians(30.0);
		const FVector Pos(R, 0.0, 0.0);
		const FVector Vel(0.0, VCirc * FMath::Cos(Incl), VCirc * FMath::Sin(Incl));

		FOrbitalState State;
		const bool bOk = UOrbitalMath::StateVectorToElements(Pos, Vel, KMuEarth, State);
		TestTrue(TEXT("Circular-inclined RV->COE returns true"), bOk);
		TestTrue(TEXT("Circular-inclined: a finite"), FMath::IsFinite(State.SemiMajorAxis));
		TestTrue(TEXT("Circular-inclined: e finite"), FMath::IsFinite(State.Eccentricity));
		TestTrue(TEXT("Circular-inclined: i finite"), FMath::IsFinite(State.Inclination));
		TestTrue(TEXT("Circular-inclined: RAAN finite"),
			FMath::IsFinite(State.RightAscensionOfAscendingNode));
		TestTrue(TEXT("Circular-inclined: ArgPeri finite"),
			FMath::IsFinite(State.ArgumentOfPeriapsis));
		TestTrue(TEXT("Circular-inclined: TrueAnomaly finite"),
			FMath::IsFinite(State.TrueAnomaly));
		TestTrue(TEXT("Circular-inclined: Epoch finite"), FMath::IsFinite(State.Epoch));

		// AC#3 requires the PRD threshold to apply — confirm we are below it AND
		// actually trigger the fallback (ArgPeri = 0 exactly).
		TestTrue(TEXT("Circular-inclined: e < PRD threshold 1e-4"),
			State.Eccentricity < 1e-4);
		TestEqual<double>(TEXT("Circular-inclined fallback: ArgPeri pinned to 0"),
			State.ArgumentOfPeriapsis, 0.0);
	}

	// AC#3 — Circular equatorial (e ≈ 0, i ≈ 0) — tightest degeneracy.
	{
		const double R = 7000e3;
		const double VCirc = FMath::Sqrt(KMuEarth / R);
		const FVector Pos(R, 0.0, 0.0);
		const FVector Vel(0.0, VCirc, 0.0);

		FOrbitalState State;
		const bool bOk = UOrbitalMath::StateVectorToElements(Pos, Vel, KMuEarth, State);
		TestTrue(TEXT("Circular equatorial RV->COE returns true"), bOk);
		TestTrue(TEXT("Circular equatorial: all angles finite"),
			FMath::IsFinite(State.RightAscensionOfAscendingNode)
			&& FMath::IsFinite(State.ArgumentOfPeriapsis)
			&& FMath::IsFinite(State.TrueAnomaly));
		TestTrue(TEXT("Circular equatorial: e ≈ 0"),
			State.Eccentricity < 1e-8);
		TestTrue(TEXT("Circular equatorial: i ≈ 0"),
			FMath::Abs(State.Inclination) < 1e-8);
	}

	// AC#4 — Invalid inputs must return false without crashing.
	{
		FOrbitalState State;
		TestFalse(TEXT("Zero Pos rejected"),
			UOrbitalMath::StateVectorToElements(
				FVector::ZeroVector, FVector(0.0, 7800.0, 0.0), KMuEarth, State));
		TestFalse(TEXT("Zero Vel rejected"),
			UOrbitalMath::StateVectorToElements(
				FVector(6778e3, 0.0, 0.0), FVector::ZeroVector, KMuEarth, State));
		TestFalse(TEXT("Zero Mu rejected"),
			UOrbitalMath::StateVectorToElements(
				FVector(6778e3, 0.0, 0.0), FVector(0.0, 7800.0, 0.0), 0.0, State));
		TestFalse(TEXT("Negative Mu rejected"),
			UOrbitalMath::StateVectorToElements(
				FVector(6778e3, 0.0, 0.0), FVector(0.0, 7800.0, 0.0), -1.0, State));
	}

	// AC#4 (inverse direction) — invalid Mu and hyperbolic elements rejected.
	{
		FVector Pos, Vel;
		FOrbitalState State;
		State.SemiMajorAxis = 7000e3;
		State.Eccentricity = 0.1;
		TestFalse(TEXT("Inverse: zero Mu rejected"),
			UOrbitalMath::ElementsToStateVector(State, 0.0, Pos, Vel));

		State.Eccentricity = 1.2;  // hyperbolic
		TestFalse(TEXT("Inverse: hyperbolic e rejected"),
			UOrbitalMath::ElementsToStateVector(State, KMuEarth, Pos, Vel));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
