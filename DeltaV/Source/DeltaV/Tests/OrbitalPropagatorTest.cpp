// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orbital/OrbitalMath.h"
#include "Orbital/OrbitalState.h"

#include "Base/CelestialBody.h"
#include "Core/SOIManager.h"
#include "Tests/SOIManagerTestListener.h"

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace OrbitalPropagatorTestNS
{
	constexpr double KMuEarth = 3.986004418e14;
	constexpr double KEarthRadius = 6378136.3;

	/** Build an elliptical orbit at periapsis (nu = 0) from (a, e) and orientation. */
	FOrbitalState MakeStateAtPeriapsis(double SemiMajorAxis, double Eccentricity,
		double InclinationRad = 0.0, double RAANRad = 0.0, double ArgPeriRad = 0.0)
	{
		FOrbitalState State;
		State.SemiMajorAxis = SemiMajorAxis;
		State.Eccentricity = Eccentricity;
		State.Inclination = InclinationRad;
		State.RightAscensionOfAscendingNode = RAANRad;
		State.ArgumentOfPeriapsis = ArgPeriRad;
		State.TrueAnomaly = 0.0;
		State.Epoch = 0.0;
		return State;
	}
}
using namespace OrbitalPropagatorTestNS;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalPropagatorKeplerTest,
	"DeltaV.Orbital.Propagator.Kepler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalPropagatorKeplerTest::RunTest(const FString& Parameters)
{
	// AC#1 — LEO circular, propagate 90 min (5400 s). Expected new position
	// within 10 m of theoretical r(t). Build a truly circular orbit (e=0),
	// place at periapsis, propagate, then compare against the analytic
	// circular-motion prediction.
	{
		const double Altitude = 400e3;
		const double A = KEarthRadius + Altitude;
		const double DeltaT = 5400.0;

		FOrbitalState S0 = MakeStateAtPeriapsis(A, 0.0,
			FMath::DegreesToRadians(51.6));
		FOrbitalState S1;
		const bool bOk = UOrbitalMath::PropagateKepler(S0, DeltaT, KMuEarth, S1);
		TestTrue(TEXT("LEO 90-min propagation succeeded"), bOk);
		if (!bOk) { return false; }

		// Cartesian positions of initial and propagated states.
		FVector P0, V0, P1, V1;
		TestTrue(TEXT("LEO: initial state->vector"),
			UOrbitalMath::ElementsToStateVector(S0, KMuEarth, P0, V0));
		TestTrue(TEXT("LEO: propagated state->vector"),
			UOrbitalMath::ElementsToStateVector(S1, KMuEarth, P1, V1));

		// Analytic reference: circular orbit, rotate P0 by mean motion * DeltaT
		// around the orbit normal. Since e = 0, nu advances linearly with time.
		const double N = FMath::Sqrt(KMuEarth / (A * A * A));
		const double ExpectedNu = N * DeltaT;
		FOrbitalState SExpected = S0;
		SExpected.TrueAnomaly = ExpectedNu;
		FVector PExpected, VExpected;
		UOrbitalMath::ElementsToStateVector(SExpected, KMuEarth, PExpected, VExpected);

		const double PosError = (P1 - PExpected).Length();
		TestTrue(
			FString::Printf(TEXT("LEO 90-min |Δr| = %.3e m < 10 m"), PosError),
			PosError < 10.0);

		// Orbital invariants must be preserved by analytic Kepler propagation.
		TestTrue(TEXT("LEO: a preserved"),
			FMath::Abs(S1.SemiMajorAxis - S0.SemiMajorAxis) < 1e-6);
		TestTrue(TEXT("LEO: e preserved"),
			FMath::Abs(S1.Eccentricity - S0.Eccentricity) < 1e-12);
		TestTrue(TEXT("LEO: i preserved"),
			FMath::Abs(S1.Inclination - S0.Inclination) < 1e-12);
		TestTrue(TEXT("LEO: Epoch advanced"),
			FMath::Abs(S1.Epoch - (S0.Epoch + DeltaT)) < 1e-9);
	}

	// AC#2 — GTO (highly elliptical), propagate half-period; should reach
	// apoapsis (r = a*(1+e)) with < 50 m error. Perigee 6578 km, apogee 42164 km.
	{
		const double Perigee = 6578e3;
		const double Apogee = 42164e3;
		const double A = 0.5 * (Perigee + Apogee);
		const double Ecc = (Apogee - Perigee) / (Apogee + Perigee);
		const double T = 2.0 * UE_DOUBLE_PI * FMath::Sqrt((A * A * A) / KMuEarth);
		const double HalfT = 0.5 * T;

		FOrbitalState S0 = MakeStateAtPeriapsis(A, Ecc);
		FOrbitalState S1;
		const bool bOk = UOrbitalMath::PropagateKepler(S0, HalfT, KMuEarth, S1);
		TestTrue(TEXT("GTO half-period propagation succeeded"), bOk);
		if (!bOk) { return false; }

		FVector P1, V1;
		TestTrue(TEXT("GTO: propagated state->vector"),
			UOrbitalMath::ElementsToStateVector(S1, KMuEarth, P1, V1));

		// At apoapsis, distance to focus is a*(1+e) and ν = π.
		const double ExpectedR = A * (1.0 + Ecc);  // = Apogee by construction
		const double ActualR = P1.Length();
		TestTrue(
			FString::Printf(TEXT("GTO half-period |r - apoapsis| = %.3e m < 50 m"),
				FMath::Abs(ActualR - ExpectedR)),
			FMath::Abs(ActualR - ExpectedR) < 50.0);

		// True anomaly should be π (within float tolerance of the half-angle chain).
		const double NuError = FMath::Abs(FMath::Abs(S1.TrueAnomaly) - UE_DOUBLE_PI);
		TestTrue(
			FString::Printf(TEXT("GTO half-period ν ≈ π (|Δν|=%.3e rad)"), NuError),
			NuError < 1e-9);
	}

	// AC#3 — DeltaSeconds = 0 returns bit-equal state.
	{
		FOrbitalState S0 = MakeStateAtPeriapsis(
			24000e3, 0.5, FMath::DegreesToRadians(28.5),
			FMath::DegreesToRadians(60.0), FMath::DegreesToRadians(90.0));
		S0.TrueAnomaly = FMath::DegreesToRadians(45.0);
		S0.Epoch = 1234.5;

		FOrbitalState S1;
		const bool bOk = UOrbitalMath::PropagateKepler(S0, 0.0, KMuEarth, S1);
		TestTrue(TEXT("Δt=0 propagation succeeded"), bOk);

		TestEqual<double>(TEXT("Δt=0: a bit-equal"), S1.SemiMajorAxis, S0.SemiMajorAxis);
		TestEqual<double>(TEXT("Δt=0: e bit-equal"), S1.Eccentricity, S0.Eccentricity);
		TestEqual<double>(TEXT("Δt=0: i bit-equal"), S1.Inclination, S0.Inclination);
		TestEqual<double>(TEXT("Δt=0: RAAN bit-equal"),
			S1.RightAscensionOfAscendingNode, S0.RightAscensionOfAscendingNode);
		TestEqual<double>(TEXT("Δt=0: ω bit-equal"),
			S1.ArgumentOfPeriapsis, S0.ArgumentOfPeriapsis);
		TestEqual<double>(TEXT("Δt=0: ν bit-equal"), S1.TrueAnomaly, S0.TrueAnomaly);
		TestEqual<double>(TEXT("Δt=0: Epoch bit-equal"), S1.Epoch, S0.Epoch);
	}

	// AC#4 — Negative Δt propagates retrograde-in-time. Round-trip +Δ then -Δ
	// must recover the original true anomaly and Epoch.
	{
		const double A = 7000e3;
		const double Ecc = 0.3;
		const double DeltaT = 1200.0;

		FOrbitalState S0 = MakeStateAtPeriapsis(A, Ecc,
			FMath::DegreesToRadians(45.0));
		S0.TrueAnomaly = FMath::DegreesToRadians(20.0);

		FOrbitalState SForward;
		TestTrue(TEXT("Round-trip: forward step"),
			UOrbitalMath::PropagateKepler(S0, DeltaT, KMuEarth, SForward));

		FOrbitalState SBack;
		TestTrue(TEXT("Round-trip: backward step with negative Δt"),
			UOrbitalMath::PropagateKepler(SForward, -DeltaT, KMuEarth, SBack));

		// Wrap angular delta into [-pi, pi] for a fair comparison.
		double DNu = SBack.TrueAnomaly - S0.TrueAnomaly;
		DNu = FMath::Fmod(DNu + UE_DOUBLE_PI, UE_DOUBLE_TWO_PI);
		if (DNu < 0.0) { DNu += UE_DOUBLE_TWO_PI; }
		DNu -= UE_DOUBLE_PI;

		TestTrue(
			FString::Printf(TEXT("Round-trip Δν=%.3e rad < 1e-9"), FMath::Abs(DNu)),
			FMath::Abs(DNu) < 1e-9);
		TestTrue(
			FString::Printf(TEXT("Round-trip Epoch restored (|ΔEpoch|=%.3e)"),
				FMath::Abs(SBack.Epoch - S0.Epoch)),
			FMath::Abs(SBack.Epoch - S0.Epoch) < 1e-9);
	}

	// AC#4 — Invalid inputs rejected without crash.
	{
		FOrbitalState S0 = MakeStateAtPeriapsis(7000e3, 0.1);
		FOrbitalState Sout;

		TestFalse(TEXT("Zero Mu rejected"),
			UOrbitalMath::PropagateKepler(S0, 60.0, 0.0, Sout));
		TestEqual<double>(TEXT("Rejected input leaves OutState zeroed (a)"),
			Sout.SemiMajorAxis, 0.0);
		TestEqual<double>(TEXT("Rejected input leaves OutState zeroed (e)"),
			Sout.Eccentricity, 0.0);
		TestFalse(TEXT("Negative Mu rejected"),
			UOrbitalMath::PropagateKepler(S0, 60.0, -1.0, Sout));

		FOrbitalState SBadA = S0;
		SBadA.SemiMajorAxis = -1.0;
		TestFalse(TEXT("Negative SMA rejected"),
			UOrbitalMath::PropagateKepler(SBadA, 60.0, KMuEarth, Sout));

		FOrbitalState SBadE = S0;
		SBadE.Eccentricity = 1.5;
		TestFalse(TEXT("Hyperbolic e rejected"),
			UOrbitalMath::PropagateKepler(SBadE, 60.0, KMuEarth, Sout));

		TestFalse(TEXT("Non-finite Δt rejected"),
			UOrbitalMath::PropagateKepler(S0,
				std::numeric_limits<double>::infinity(), KMuEarth, Sout));
	}

	return true;
}

namespace
{
	/** Specific orbital energy ε = v²/2 − μ/|r|. Negative for bound orbits. */
	double SpecificEnergy(const FVector& R, const FVector& V, double Mu)
	{
		const double V2 = V.X * V.X + V.Y * V.Y + V.Z * V.Z;
		const double RMag = R.Length();
		return 0.5 * V2 - Mu / RMag;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalPropagatorLeapfrogTest,
	"DeltaV.Orbital.Propagator.Leapfrog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalPropagatorLeapfrogTest::RunTest(const FString& Parameters)
{
	// Shared LEO circular fixture for AC#1-3.
	const double Altitude = 400e3;
	const double A = KEarthRadius + Altitude;
	const double Period = 2.0 * UE_DOUBLE_PI * FMath::Sqrt((A * A * A) / KMuEarth);
	const double HundredRevs = 100.0 * Period;

	FOrbitalState S0 = MakeStateAtPeriapsis(A, 0.0, FMath::DegreesToRadians(51.6));

	// AC#1 — 100 revolutions at 100 Hz, cumulative energy drift < 0.01%.
	{
		FOrbitalState S1;
		const bool bOk = UOrbitalMath::PropagateLeapfrog(
			S0, HundredRevs, KMuEarth, 100.0, S1);
		TestTrue(TEXT("LEO × 100 rev Leapfrog succeeded"), bOk);
		if (!bOk) { return false; }

		FVector R0, V0, R1, V1;
		UOrbitalMath::ElementsToStateVector(S0, KMuEarth, R0, V0);
		UOrbitalMath::ElementsToStateVector(S1, KMuEarth, R1, V1);

		const double E0 = SpecificEnergy(R0, V0, KMuEarth);
		const double E1 = SpecificEnergy(R1, V1, KMuEarth);
		const double RelDrift = FMath::Abs((E1 - E0) / E0);

		TestTrue(
			FString::Printf(TEXT("LEO × 100 rev energy drift |Δε/ε|=%.3e < 1e-4"), RelDrift),
			RelDrift < 1e-4);
	}

	// AC#2 — Leapfrog ≈ Kepler analytic within 10 m after 100 revs.
	{
		FOrbitalState SKepler;
		FOrbitalState SLeapfrog;

		TestTrue(TEXT("Kepler propagation succeeded"),
			UOrbitalMath::PropagateKepler(S0, HundredRevs, KMuEarth, SKepler));
		TestTrue(TEXT("Leapfrog propagation succeeded"),
			UOrbitalMath::PropagateLeapfrog(S0, HundredRevs, KMuEarth, 100.0, SLeapfrog));

		FVector PK, VK, PL, VL;
		UOrbitalMath::ElementsToStateVector(SKepler, KMuEarth, PK, VK);
		UOrbitalMath::ElementsToStateVector(SLeapfrog, KMuEarth, PL, VL);

		const double PosDelta = (PL - PK).Length();
		TestTrue(
			FString::Printf(TEXT("LEO × 100 rev Kepler vs Leapfrog |ΔP|=%.3e m < 10 m"), PosDelta),
			PosDelta < 10.0);
	}

	// AC#3 — Per-revolution element drift. a, e, i must drift < 1 ppm per rev
	// (i.e. < 1e-4 over 100 revs in relative/absolute terms).
	{
		FOrbitalState S1;
		TestTrue(TEXT("Leapfrog 100 rev for element drift"),
			UOrbitalMath::PropagateLeapfrog(S0, HundredRevs, KMuEarth, 100.0, S1));

		const double RelA = FMath::Abs(S1.SemiMajorAxis - S0.SemiMajorAxis) / S0.SemiMajorAxis;
		TestTrue(
			FString::Printf(TEXT("LEO: a drift=%.3e (< 100 ppm)"), RelA),
			RelA < 1e-4);

		// Circular orbit: e stays near 0 (absolute bound). Bound: 1 ppm * 100 = 1e-4.
		TestTrue(
			FString::Printf(TEXT("LEO: e drift=%.3e (< 1e-4 absolute for circular)"),
				S1.Eccentricity),
			S1.Eccentricity < 1e-4);

		const double DeltaI = FMath::Abs(S1.Inclination - S0.Inclination);
		TestTrue(
			FString::Printf(TEXT("LEO: i drift=%.3e rad (< 1e-4 rad)"), DeltaI),
			DeltaI < 1e-4);
	}

	// AC#4 — StepHz < 10 clamped to 60 Hz. Verify by comparing the result of
	// a call at StepHz=5 against an explicit StepHz=60 call: identical state.
	{
		const double DeltaT = 600.0;  // short propagation so either hz resolves cleanly
		FOrbitalState SClamped;
		FOrbitalState SExplicit;

		TestTrue(TEXT("StepHz=5 (should clamp) succeeded"),
			UOrbitalMath::PropagateLeapfrog(S0, DeltaT, KMuEarth, 5.0, SClamped));
		TestTrue(TEXT("StepHz=60 reference succeeded"),
			UOrbitalMath::PropagateLeapfrog(S0, DeltaT, KMuEarth, 60.0, SExplicit));

		FVector PC, VC, PE, VE;
		UOrbitalMath::ElementsToStateVector(SClamped, KMuEarth, PC, VC);
		UOrbitalMath::ElementsToStateVector(SExplicit, KMuEarth, PE, VE);
		const double Delta = (PC - PE).Length();

		TestTrue(
			FString::Printf(TEXT("Clamped (5 Hz -> 60 Hz) ≡ explicit 60 Hz (|ΔP|=%.3e m)"), Delta),
			Delta < 1e-6);
	}

	// AC#4 — Invalid inputs rejected.
	{
		FOrbitalState Sout;
		FOrbitalState SLEO = MakeStateAtPeriapsis(A, 0.0);

		TestFalse(TEXT("Leapfrog: zero Mu rejected"),
			UOrbitalMath::PropagateLeapfrog(SLEO, 60.0, 0.0, 100.0, Sout));
		TestFalse(TEXT("Leapfrog: negative Mu rejected"),
			UOrbitalMath::PropagateLeapfrog(SLEO, 60.0, -1.0, 100.0, Sout));
		TestFalse(TEXT("Leapfrog: non-finite Δt rejected"),
			UOrbitalMath::PropagateLeapfrog(SLEO,
				std::numeric_limits<double>::infinity(), KMuEarth, 100.0, Sout));
		TestFalse(TEXT("Leapfrog: non-finite StepHz rejected"),
			UOrbitalMath::PropagateLeapfrog(SLEO, 60.0, KMuEarth,
				std::numeric_limits<double>::infinity(), Sout));

		FOrbitalState SBadE = SLEO;
		SBadE.Eccentricity = 1.5;
		TestFalse(TEXT("Leapfrog: hyperbolic e rejected (via ElementsToStateVector)"),
			UOrbitalMath::PropagateLeapfrog(SBadE, 60.0, KMuEarth, 100.0, Sout));

		// DoS guard: |Δt|·StepHz above KMaxSubsteps must fail fast.
		TestFalse(TEXT("Leapfrog: |Δt|·StepHz above substep cap rejected"),
			UOrbitalMath::PropagateLeapfrog(SLEO, 1.0e15, KMuEarth, 1.0e6, Sout));
	}

	// Aliasing safety — passing the same reference for In and Out must work.
	{
		FOrbitalState S = MakeStateAtPeriapsis(A, 0.0,
			FMath::DegreesToRadians(28.5));
		const double OriginalEpoch = S.Epoch;
		const double OriginalA = S.SemiMajorAxis;

		TestTrue(TEXT("Leapfrog: aliasing In == Out propagates cleanly"),
			UOrbitalMath::PropagateLeapfrog(S, 60.0, KMuEarth, 100.0, S));
		TestTrue(TEXT("Aliased: Epoch advanced"),
			FMath::Abs(S.Epoch - (OriginalEpoch + 60.0)) < 1e-9);
		TestTrue(TEXT("Aliased: a preserved within noise"),
			FMath::Abs(S.SemiMajorAxis - OriginalA) / OriginalA < 1e-6);
	}

	// AC#3 (time-reversibility sanity): forward then backward round-trip to
	// the original element set with sub-ppm precision over 1 revolution.
	{
		FOrbitalState SFwd, SBack;
		TestTrue(TEXT("Leapfrog: forward 1 rev"),
			UOrbitalMath::PropagateLeapfrog(S0, Period, KMuEarth, 100.0, SFwd));
		TestTrue(TEXT("Leapfrog: backward 1 rev"),
			UOrbitalMath::PropagateLeapfrog(SFwd, -Period, KMuEarth, 100.0, SBack));

		FVector PO, VO, PB, VB;
		UOrbitalMath::ElementsToStateVector(S0, KMuEarth, PO, VO);
		UOrbitalMath::ElementsToStateVector(SBack, KMuEarth, PB, VB);
		const double Delta = (PB - PO).Length();
		TestTrue(
			FString::Printf(TEXT("Leapfrog ±1 rev round-trip |ΔP|=%.3e m < 1 m"), Delta),
			Delta < 1.0);
	}

	return true;
}

// US-011 — Long-term regression test for the analytic Kepler propagator.
// Asserts the PRD contract: 100 LEO revolutions with drift < 10 m, energy
// drift < 1e-4, and hot-path runtime < 2 s (the time bound is a structural
// canary that flags any accidental dispatch through the heavy integrator
// path — analytic Kepler is nanoseconds, Leapfrog at 100 Hz is ~2.4 s).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalPropagatorLEO100RevolutionsTest,
	"DeltaV.Orbital.Propagator.LEO100Revolutions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalPropagatorLEO100RevolutionsTest::RunTest(const FString& Parameters)
{
	// Fixture: 400 km LEO circular, inclination 51.6° (ISS-like).
	const double Altitude = 400e3;
	const double A = KEarthRadius + Altitude;
	const double Period = 2.0 * UE_DOUBLE_PI * FMath::Sqrt((A * A * A) / KMuEarth);
	const double HundredRevs = 100.0 * Period;

	FOrbitalState S0 = MakeStateAtPeriapsis(A, 0.0, FMath::DegreesToRadians(51.6));

	// Capture start-state Cartesian + energy.
	FVector P0, V0;
	if (!TestTrue(TEXT("LEO100: initial state->vector"),
		UOrbitalMath::ElementsToStateVector(S0, KMuEarth, P0, V0)))
	{
		return false;
	}
	const double E0 = SpecificEnergy(P0, V0, KMuEarth);

	// AC#3 — Runtime bound. Measure only the propagator call itself, not the
	// surrounding RV↔COE conversions.
	const double T0 = FPlatformTime::Seconds();
	FOrbitalState S1;
	const bool bOk = UOrbitalMath::PropagateKepler(S0, HundredRevs, KMuEarth, S1);
	const double ElapsedSeconds = FPlatformTime::Seconds() - T0;

	if (!TestTrue(TEXT("LEO100: propagation succeeded"), bOk))
	{
		return false;
	}

	// Capture end-state Cartesian + energy.
	FVector P1, V1;
	if (!TestTrue(TEXT("LEO100: propagated state->vector"),
		UOrbitalMath::ElementsToStateVector(S1, KMuEarth, P1, V1)))
	{
		return false;
	}
	const double E1 = SpecificEnergy(P1, V1, KMuEarth);

	// AC#1 — Position drift. After exactly 100 integer revolutions, the
	// theoretical new position equals the initial position, so we compare
	// propagated against initial. On breach, emit the PRD-mandated failure
	// message verbatim (includes the em-dash).
	const double Drift = (P1 - P0).Length();
	if (Drift >= 10.0)
	{
		AddError(FString::Printf(
			TEXT("Drift %.3fm exceeds 10m threshold — consider Leapfrog fallback"),
			Drift));
	}

	// AC#2 — Relative energy drift.
	const double RelEnergyDrift = FMath::Abs((E1 - E0) / E0);
	TestTrue(
		FString::Printf(TEXT("LEO100: |Δε/ε|=%.3e < 1e-4"), RelEnergyDrift),
		RelEnergyDrift < 1e-4);

	// AC#3 — Runtime bound.
	TestTrue(
		FString::Printf(TEXT("LEO100: propagation time=%.3f s < 2.0 s"), ElapsedSeconds),
		ElapsedSeconds < 2.0);

	// Sanity on invariants: a, e, i must be preserved (Kepler analytic).
	TestTrue(TEXT("LEO100: a preserved"),
		FMath::Abs(S1.SemiMajorAxis - S0.SemiMajorAxis) < 1e-6);
	TestTrue(TEXT("LEO100: e preserved"),
		FMath::Abs(S1.Eccentricity - S0.Eccentricity) < 1e-12);
	TestTrue(TEXT("LEO100: i preserved"),
		FMath::Abs(S1.Inclination - S0.Inclination) < 1e-12);

	return true;
}

// US-012 — Patched-conics integration test: LEO + TLI Hohmann burn propagated
// in Earth-centred rail until SOI manager detects Moon SOI entry. Validates
// that PropagateKepler (US-008) + USOIManager (US-010) cooperate end-to-end.
// The Moon is frozen on the −X axis (Hohmann apogee line); the vessel's
// prograde TLI puts it on an ellipse whose apogee coincides with the Moon's
// position ≈ 4.98 days after burn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrbitalPropagatorHohmannEarthMoonTest,
	"DeltaV.Orbital.Propagator.HohmannEarthMoon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalPropagatorHohmannEarthMoonTest::RunTest(const FString& Parameters)
{
	// --- Fixture constants --------------------------------------------------
	constexpr double KLeoAltitude = 300e3;
	constexpr double KMoonDistance = 384400000.0;       // m
	constexpr double KMoonSOIRadius = 66100000.0;       // m (Laplace in Earth frame)
	constexpr double KEarthSOIRadius = 1.0e9;           // m (big enough to contain ellipse)
	constexpr double KTickSeconds = 60.0;               // 1-minute cadence
	constexpr double KMaxSimSeconds = 7.0 * 86400.0;    // 7 days ceiling
	constexpr double KArrivalWindowLo = (4.0 * 24.0 + 18.0) * 3600.0;  // 4d18h
	constexpr double KArrivalWindowHi = (5.0 * 24.0 + 6.0) * 3600.0;   // 5d06h
	constexpr double KArrivalPosErrorMax = 100e3;       // 100 km

	// --- Hohmann transfer algebra ------------------------------------------
	const double R1 = KEarthRadius + KLeoAltitude;      // perigee
	const double R2 = KMoonDistance;                    // apogee
	const double ATransfer = 0.5 * (R1 + R2);
	const double ETransfer = (R2 - R1) / (R2 + R1);
	const double VPerigeeTransfer = FMath::Sqrt(KMuEarth * (2.0 / R1 - 1.0 / ATransfer));

	// Initial Cartesian state: perigee at (+R1, 0, 0), prograde velocity +Y.
	// For this geometry, apogee (nu = pi) lands at (-R2, 0, 0).
	const FVector InitialPos(R1, 0.0, 0.0);
	const FVector InitialVel(0.0, VPerigeeTransfer, 0.0);

	FOrbitalState InitialState;
	if (!TestTrue(TEXT("Hohmann: initial RV->COE"),
		UOrbitalMath::StateVectorToElements(InitialPos, InitialVel, KMuEarth, InitialState)))
	{
		return false;
	}

	// Sanity: the constructed ellipse should match our Hohmann parameters.
	TestTrue(FString::Printf(TEXT("Hohmann: transfer SMA matches (%.3e m vs %.3e)"),
		InitialState.SemiMajorAxis, ATransfer),
		FMath::Abs(InitialState.SemiMajorAxis - ATransfer) < 1e-3);
	TestTrue(FString::Printf(TEXT("Hohmann: transfer e matches (%.6f vs %.6f)"),
		InitialState.Eccentricity, ETransfer),
		FMath::Abs(InitialState.Eccentricity - ETransfer) < 1e-6);

	// --- SOI manager setup -------------------------------------------------
	UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
	USOIManager* Manager = NewObject<USOIManager>(GI);

	UCelestialBody* Earth = NewObject<UCelestialBody>();
	Earth->BodyName = TEXT("Earth");
	Earth->GravitationalParameter = KMuEarth;
	Earth->SOIRadius = KEarthSOIRadius;
	Earth->WorldPosition = FVector::ZeroVector;

	UCelestialBody* Moon = NewObject<UCelestialBody>();
	Moon->BodyName = TEXT("Moon");
	Moon->GravitationalParameter = 4.9048695e12;
	Moon->SOIRadius = KMoonSOIRadius;
	Moon->WorldPosition = FVector(-KMoonDistance, 0.0, 0.0);  // apogee side

	Manager->RegisterBody(Earth);
	Manager->RegisterBody(Moon);

	USOIManagerTestListener* Listener = NewObject<USOIManagerTestListener>();
	Manager->OnSOITransitionEnter.AddDynamic(
		Listener, &USOIManagerTestListener::HandleTransition);
	Manager->OnSOIOrphan.AddDynamic(
		Listener, &USOIManagerTestListener::HandleOrphan);

	const FGuid Vessel = FGuid::NewGuid();

	// --- Transfer loop -----------------------------------------------------
	//
	// Note on the PRD arrival window:
	//   PRD AC#1 says "vessel enters Moon SOI between 4d18h and 5d06h". Taken
	//   literally, this is physics-impossible: for a true Hohmann whose apogee
	//   equals the Moon's orbital radius, the vessel crosses the 66,100 km
	//   Moon SOI sphere near ν≈173° (about 2.7 d after TLI) and then hangs
	//   near apogee for ~2.3 d before passing through ν=180°. The PRD window
	//   matches *closest approach* (≈ apogee passage, ~4.98 d) — which is
	//   clearly the author's intent. We assert the PRD window against the
	//   closest-approach time and separately verify the SOI entry precedes it.
	double MinDistanceToMoon = TNumericLimits<double>::Max();
	double ClosestApproachTime = -1.0;
	FVector ClosestApproachPos = FVector::ZeroVector;
	double SOIEntryTime = -1.0;
	FVector SOIEntryPos = FVector::ZeroVector;

	const int32 TotalTicks = FMath::CeilToInt32(KMaxSimSeconds / KTickSeconds);
	for (int32 I = 0; I <= TotalTicks; ++I)
	{
		const double T = static_cast<double>(I) * KTickSeconds;

		FOrbitalState Propagated;
		if (!UOrbitalMath::PropagateKepler(InitialState, T, KMuEarth, Propagated))
		{
			AddError(FString::Printf(
				TEXT("Hohmann: PropagateKepler failed at t=%.1f s"), T));
			return false;
		}

		FVector VesselPos, VesselVel;
		if (!UOrbitalMath::ElementsToStateVector(
			Propagated, KMuEarth, VesselPos, VesselVel))
		{
			AddError(FString::Printf(
				TEXT("Hohmann: ElementsToStateVector failed at t=%.1f s"), T));
			return false;
		}

		Manager->UpdateVessel(Vessel, VesselPos, T);

		const double D = (VesselPos - Moon->WorldPosition).Length();
		if (D < MinDistanceToMoon)
		{
			MinDistanceToMoon = D;
			ClosestApproachTime = T;
			ClosestApproachPos = VesselPos;
		}
		if (SOIEntryTime < 0.0 && Listener->LastTransitionBody == Moon)
		{
			SOIEntryTime = T;
			SOIEntryPos = VesselPos;
		}

		// Terminate the loop once we've passed apogee (ν > π) and the vessel
		// has started receding from Moon — closest approach is locked in.
		// Detect by checking if distance is growing AND we've had an SOI entry.
		if (SOIEntryTime >= 0.0
			&& I > 10
			&& D > MinDistanceToMoon * 1.01
			&& ClosestApproachTime > SOIEntryTime)
		{
			break;
		}
	}

	// --- AC#3 (unhappy path): vessel never entered Moon SOI ---------------
	if (SOIEntryTime < 0.0)
	{
		const double Miss = MinDistanceToMoon;
		// Correct Δv recommendation: the vessel's actual apogee is RApoActual
		// = a(1+e) of the flown transfer, NOT R2. Offset the target apogee to
		// the Moon's orbital radius and compute the perigee-velocity delta.
		// Symmetry is not assumed; try both directions and pick the smaller.
		const double RApoActual = InitialState.SemiMajorAxis
			* (1.0 + InitialState.Eccentricity);
		auto PerigeeV = [R1](double TargetApogee) {
			const double A = 0.5 * (R1 + TargetApogee);
			return FMath::Sqrt(KMuEarth * (2.0 / R1 - 1.0 / A));
		};
		const double DvUp = FMath::Abs(PerigeeV(R2) - PerigeeV(RApoActual));
		const double DvDown = FMath::Abs(PerigeeV(R2) - PerigeeV(FMath::Max(R1, RApoActual)));
		const double DvCorrection = FMath::Min(DvUp, DvDown);

		AddError(FString::Printf(
			TEXT("Missed Moon SOI: min passage distance %.1f km, recommended Δv correction %.1f m/s"),
			Miss / 1000.0, DvCorrection));
		return false;
	}

	// --- AC#1: closest-approach time falls inside the PRD window ----------
	const double ClosestApproachHours = ClosestApproachTime / 3600.0;
	TestTrue(
		FString::Printf(TEXT("Hohmann closest approach t=%.2f h in [%.2f, %.2f] h (PRD window)"),
			ClosestApproachHours,
			KArrivalWindowLo / 3600.0, KArrivalWindowHi / 3600.0),
		ClosestApproachTime >= KArrivalWindowLo
			&& ClosestApproachTime <= KArrivalWindowHi);

	// Sanity: SOI entry must precede closest approach (physical constraint).
	TestTrue(
		FString::Printf(TEXT("Hohmann: SOI entry (%.2f h) precedes closest approach (%.2f h)"),
			SOIEntryTime / 3600.0, ClosestApproachHours),
		SOIEntryTime < ClosestApproachTime);

	// --- AC#2: position error at closest approach vs theoretical ----------
	// "Theoretical 2-body" = single-shot PropagateKepler from t=0 to the
	// same tick. Loop is pure analytic, so drift should be sub-mm, well
	// under the 100 km AC bound.
	FOrbitalState TheoreticalState;
	FVector TheoreticalPos, TheoreticalVel;
	if (!TestTrue(TEXT("Hohmann: theoretical single-shot propagation"),
			UOrbitalMath::PropagateKepler(InitialState, ClosestApproachTime,
				KMuEarth, TheoreticalState))
		|| !TestTrue(TEXT("Hohmann: theoretical COE->RV"),
			UOrbitalMath::ElementsToStateVector(
				TheoreticalState, KMuEarth, TheoreticalPos, TheoreticalVel)))
	{
		return false;
	}

	const double PosError = (ClosestApproachPos - TheoreticalPos).Length();
	TestTrue(
		FString::Printf(TEXT("Hohmann closest approach |ΔP|=%.3e m < 100 km"), PosError),
		PosError < KArrivalPosErrorMax);

	// Invariant: at closest approach the vessel should be at Moon's position
	// within the 60 s tick sampling granularity. At apogee v ≈ 188 m/s so one
	// tick covers ~11 km; allow 15 km absolute.
	const double MoonDistanceAtApogee = MinDistanceToMoon;
	TestTrue(
		FString::Printf(TEXT("Hohmann closest approach |vessel-Moon|=%.3e m < 15 km"),
			MoonDistanceAtApogee),
		MoonDistanceAtApogee < 15e3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
