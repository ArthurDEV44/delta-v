// Copyright Epic Games, Inc. All Rights Reserved.

#include "Base/CelestialBody.h"
#include "Core/SOIManager.h"
#include "Tests/SOIManagerTestListener.h"

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

#include <limits>

// Listener implementations live outside WITH_DEV_AUTOMATION_TESTS so the
// reflection-generated vtable always has concrete symbols to link against.
void USOIManagerTestListener::HandleTransition(FGuid VesselKey, UCelestialBody* NewSOI)
{
	TransitionCount++;
	LastTransitionKey = VesselKey;
	LastTransitionBody = NewSOI;
}

void USOIManagerTestListener::HandleOrphan(FGuid VesselKey)
{
	OrphanCount++;
	LastOrphanKey = VesselKey;
}

#if WITH_DEV_AUTOMATION_TESTS

namespace SOIManagerTestNS
{
	/** mu constants (m^3 / s^2). */
	constexpr double KMuEarth = 3.986004418e14;
	constexpr double KMuMoon = 4.9048695e12;
	constexpr double KMuAsteroid = 1.0e5;  // small body

	/** Distances (m). */
	constexpr double KEarthRadius = 6378136.3;
	constexpr double KEarthMoonDistance = 384400000.0;
	constexpr double KEarthSOIInSunFrame = 9.25e8;       // AC#1 reference
	constexpr double KMoonSOIInEarthFrame = 6.61e7;      // AC#1 reference

	UCelestialBody* MakeBody(const FName& Name, double Mu, double SOI, const FVector& Pos)
	{
		UCelestialBody* B = NewObject<UCelestialBody>();
		B->BodyName = Name;
		B->GravitationalParameter = Mu;
		B->SOIRadius = SOI;
		B->WorldPosition = Pos;
		return B;
	}

	USOIManager* MakeManager()
	{
		// USOIManager derives from UGameInstanceSubsystem, which enforces
		// ClassWithin = UGameInstance. Manufacture a transient GI as Outer so
		// NewObject passes the class-within check in headless automation.
		UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
		USOIManager* M = NewObject<USOIManager>(GI);
		// Subsystem::Initialize needs a FSubsystemCollection that the engine
		// normally builds when the GI spins up. In tests the public API is
		// idempotent vs Initialize (no internal state to bootstrap), so we
		// skip Initialize and rely on default-constructed members.
		return M;
	}

	USOIManagerTestListener* MakeListener(USOIManager* M)
	{
		USOIManagerTestListener* L = NewObject<USOIManagerTestListener>();
		M->OnSOITransitionEnter.AddDynamic(L, &USOIManagerTestListener::HandleTransition);
		M->OnSOIOrphan.AddDynamic(L, &USOIManagerTestListener::HandleOrphan);
		return L;
	}
}
using namespace SOIManagerTestNS;

// AC#1 — Registration + radii.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerRegistrationTest,
	"DeltaV.Orbital.SOIManager.Registration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerRegistrationTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	UCelestialBody* Moon = MakeBody(TEXT("Moon"), KMuMoon, KMoonSOIInEarthFrame,
		FVector(KEarthMoonDistance, 0.0, 0.0));
	UCelestialBody* Asteroid = MakeBody(TEXT("Asteroid"), KMuAsteroid, 1.0e4,
		FVector(2.0e9, 1.0e9, 0.0));

	M->RegisterBody(Earth);
	M->RegisterBody(Moon);
	M->RegisterBody(Asteroid);

	TestEqual(TEXT("GetAllBodies().Num() == 3"), M->GetAllBodies().Num(), 3);
	TestTrue(TEXT("Earth has positive SOI radius"), Earth->SOIRadius > 0.0);
	TestTrue(TEXT("Moon has positive SOI radius"), Moon->SOIRadius > 0.0);
	TestTrue(TEXT("Asteroid has positive SOI radius"), Asteroid->SOIRadius > 0.0);

	// Duplicate registration is a no-op.
	M->RegisterBody(Earth);
	TestEqual(TEXT("Duplicate register is a no-op"), M->GetAllBodies().Num(), 3);

	// Null register is a no-op.
	M->RegisterBody(nullptr);
	TestEqual(TEXT("Null register is a no-op"), M->GetAllBodies().Num(), 3);

	// Unregister.
	M->UnregisterBody(Asteroid);
	TestEqual(TEXT("Unregister drops the body"), M->GetAllBodies().Num(), 2);

	return true;
}

// AC#2 — Query at 300 km Earth altitude returns Earth.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerQueryEarthTest,
	"DeltaV.Orbital.SOIManager.QueryCurrentSOI_Earth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerQueryEarthTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	UCelestialBody* Moon = MakeBody(TEXT("Moon"), KMuMoon, KMoonSOIInEarthFrame,
		FVector(KEarthMoonDistance, 0.0, 0.0));
	M->RegisterBody(Earth);
	M->RegisterBody(Moon);

	const FVector VesselPos(KEarthRadius + 300e3, 0.0, 0.0);
	UCelestialBody* Current = M->QueryCurrentSOI(VesselPos);
	TestNotNull(TEXT("Vessel at 300 km alt is inside some SOI"), Current);
	TestEqual(TEXT("Current SOI is Earth"), Current, Earth);

	return true;
}

// AC#3 — Transition Earth -> Moon fires OnSOITransitionEnter with (VesselKey, Moon).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerTransitionTest,
	"DeltaV.Orbital.SOIManager.TransitionEarthToMoon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerTransitionTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	UCelestialBody* Moon = MakeBody(TEXT("Moon"), KMuMoon, KMoonSOIInEarthFrame,
		FVector(KEarthMoonDistance, 0.0, 0.0));
	M->RegisterBody(Earth);
	M->RegisterBody(Moon);
	USOIManagerTestListener* L = MakeListener(M);

	const FGuid V = FGuid::NewGuid();

	// Frame 1: vessel inside Earth SOI, not inside Moon SOI yet.
	const FVector PosEarth(KEarthRadius + 1000e3, 0.0, 0.0);
	UCelestialBody* SOI1 = M->UpdateVessel(V, PosEarth, 0.0);
	TestEqual(TEXT("Frame 1 SOI is Earth"), SOI1, Earth);
	TestEqual<int32>(TEXT("Frame 1: 1 transition fired (into Earth)"), L->TransitionCount, 1);

	// Frame 2: vessel at Moon's position (inside Moon SOI). Hysteresis is the
	// sole transition gate, so a legitimate crossing fires immediately on the
	// next frame — AC#3 requires transitions "within 1 frame" of the crossing.
	const FVector PosMoon = Moon->WorldPosition + FVector(0.0, 0.0, 1000e3);
	UCelestialBody* SOI2 = M->UpdateVessel(V, PosMoon, 1.0);
	TestEqual(TEXT("Frame 2 SOI is Moon"), SOI2, Moon);
	TestEqual<int32>(TEXT("Frame 2: cumulative transition count = 2"), L->TransitionCount, 2);
	TestEqual(TEXT("Last transition body is Moon"), L->LastTransitionBody.Get(), Moon);

	return true;
}

// AC#4 — Hysteresis + cooldown suppress chattering. Oscillate vessel at 10 Hz
// across the Earth<->Moon boundary for 120 s of world time; expect ≤ 2
// transitions (one entry into Earth, at most one into Moon after the 60 s cooldown).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerHysteresisTest,
	"DeltaV.Orbital.SOIManager.HysteresisSuppresses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerHysteresisTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	UCelestialBody* Moon = MakeBody(TEXT("Moon"), KMuMoon, KMoonSOIInEarthFrame,
		FVector(KEarthMoonDistance, 0.0, 0.0));
	M->RegisterBody(Earth);
	M->RegisterBody(Moon);
	USOIManagerTestListener* L = MakeListener(M);

	const FGuid V = FGuid::NewGuid();
	// Place the vessel right at the Moon SOI boundary from Earth's side.
	// Earth direction to Moon is +X. The Moon SOI edge facing Earth is at
	// (KEarthMoonDistance - MoonSOI, 0, 0).
	const double EdgeX = KEarthMoonDistance - Moon->SOIRadius;
	// Oscillate position within ±0.25% (well inside the 0.5% hysteresis band).
	const double BandHalf = 0.0025 * Moon->SOIRadius;

	const double SimDurationSec = 120.0;
	const double Hz = 10.0;
	const int32 Ticks = static_cast<int32>(SimDurationSec * Hz);
	for (int32 I = 0; I < Ticks; ++I)
	{
		const double Now = I / Hz;
		const bool bInside = (I % 2) == 0;
		const double X = EdgeX + (bInside ? -BandHalf : +BandHalf);
		M->UpdateVessel(V, FVector(X, 0.0, 0.0), Now);
	}

	// Expected behavior:
	// - Tick 0 (t=0) first classification -> transition into whichever body
	//   the oscillation happens to be in: 1 transition.
	// - Hysteresis ±0.5% * SOI ≫ our ±0.25% oscillation -> no further candidate
	//   changes regardless of log-throttle window.
	// - Therefore total transitions = 1.
	TestEqual<int32>(
		FString::Printf(TEXT("Chattering suppressed by hysteresis: %d transitions (expected 1)"),
			L->TransitionCount),
		L->TransitionCount, 1);

	return true;
}

// AC#5 — Vessel outside every SOI -> OnSOIOrphan event, no crash, nullptr returned.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerOrphanTest,
	"DeltaV.Orbital.SOIManager.OrphanVessel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerOrphanTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	M->RegisterBody(Earth);
	USOIManagerTestListener* L = MakeListener(M);

	const FGuid V = FGuid::NewGuid();
	// 1e12 m is ~1000x Earth's SOI — definitively outside.
	const FVector FarAway(1.0e12, 0.0, 0.0);

	UCelestialBody* Result = M->UpdateVessel(V, FarAway, 0.0);
	TestNull(TEXT("Orphan returns nullptr"), Result);
	TestEqual<int32>(TEXT("OnSOIOrphan fired exactly once"), L->OrphanCount, 1);
	TestEqual(TEXT("OrphanKey matches vessel key"), L->LastOrphanKey, V);
	TestNull(TEXT("GetAssignedSOI returns nullptr"), M->GetAssignedSOI(V));

	return true;
}

// Legitimate rapid transitions must fire immediately (AC#3 "within 1 frame").
// Regression test for the cooldown-trap issue: transition cooldown must not
// block real physics crossings happening in quick succession.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerRapidTransitionsTest,
	"DeltaV.Orbital.SOIManager.RapidLegitimateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerRapidTransitionsTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	UCelestialBody* Moon = MakeBody(TEXT("Moon"), KMuMoon, KMoonSOIInEarthFrame,
		FVector(KEarthMoonDistance, 0.0, 0.0));
	M->RegisterBody(Earth);
	M->RegisterBody(Moon);
	USOIManagerTestListener* L = MakeListener(M);

	const FGuid V = FGuid::NewGuid();

	// Sub-second cadence across Earth -> Moon -> out (orphan region far away).
	// All three must fire promptly even though Δt between transitions is 1 s.
	M->UpdateVessel(V, FVector(KEarthRadius + 1000e3, 0, 0), 0.0);         // -> Earth
	M->UpdateVessel(V, Moon->WorldPosition + FVector(0, 0, 1000e3), 1.0);  // -> Moon
	M->UpdateVessel(V, FVector(1.0e12, 0, 0), 2.0);                        // -> Orphan

	TestEqual<int32>(TEXT("Three rapid legitimate transitions -> 2 events"),
		L->TransitionCount, 2);
	TestEqual<int32>(TEXT("One orphan event fired"), L->OrphanCount, 1);

	return true;
}

// Invalid inputs: non-finite vessel position, non-finite world time, stateless
// query with garbage input — all must fail closed without crashing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSOIManagerInvalidInputsTest,
	"DeltaV.Orbital.SOIManager.InvalidInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSOIManagerInvalidInputsTest::RunTest(const FString& Parameters)
{
	USOIManager* M = MakeManager();
	UCelestialBody* Earth = MakeBody(TEXT("Earth"), KMuEarth, KEarthSOIInSunFrame, FVector::ZeroVector);
	M->RegisterBody(Earth);

	const FGuid V = FGuid::NewGuid();
	const double Inf = std::numeric_limits<double>::infinity();

	TestNull(TEXT("Non-finite Pos returns null"),
		M->UpdateVessel(V, FVector(Inf, 0.0, 0.0), 0.0));
	TestNull(TEXT("Non-finite WorldTime returns null"),
		M->UpdateVessel(V, FVector::ZeroVector, Inf));
	TestNull(TEXT("QueryCurrentSOI with non-finite Pos returns null"),
		M->QueryCurrentSOI(FVector(Inf, 0.0, 0.0)));

	// ForgetVessel on unknown key is a no-op.
	M->ForgetVessel(FGuid::NewGuid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
