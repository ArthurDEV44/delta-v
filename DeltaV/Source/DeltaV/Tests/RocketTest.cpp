// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/ARocket.h"
#include "Vehicles/AVehicle.h"
#include "Vehicles/URocketDef.h"
#include "Vehicles/UStageComponent.h"
#include "Vehicles/UVehiclePartComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double KStandardGravity = 9.80665;

	// Falcon-9-like parameters picked so staged Tsiolkovsky lands at ~9400 m/s
	// with the masses demanded by US-014 ACs:
	//   - Stage 1 wet 411 t, dry 27 t (AC#2 linear-decay target)
	//   - Total mass 549 t (AC#1 target)
	// Payload and stage-2 masses are set so Stage1_Wet + Stage2_Wet + Payload = 549 t.
	//
	// Isp 311 s (S1) and 348 s (S2) match averaged / vacuum Merlin performance
	// numbers; the resulting staged dV works out analytically to 9400.1 m/s,
	// comfortably inside AC#1's ±5 % band.

	constexpr double KStage1DryKg  = 27000.0;
	constexpr double KStage1FuelKg = 384000.0;
	constexpr double KStage1Isp    = 311.0;

	constexpr double KStage2DryKg  = 4500.0;
	constexpr double KStage2FuelKg = 112280.0;
	constexpr double KStage2Isp    = 348.0;

	constexpr double KPayloadKg    = 21220.0;

	constexpr double KStage1BurnSeconds = 150.0;

	// Thrust sized so a 150 s burn empties the stage at constant curve = 1.0.
	// m_dot = Fuel / BurnTime ; Thrust = m_dot * Isp * g0.
	constexpr double KStage1MaxThrust =
		(KStage1FuelKg / KStage1BurnSeconds) * KStage1Isp * KStandardGravity;
	constexpr double KStage2MaxThrust = 934000.0; // Merlin Vac nominal. Irrelevant to AC#1/#2 but set to a realistic value.

	/** Build a Falcon-9-like URocketDef programmatically. Transient package — no save path. */
	URocketDef* MakeFalcon9LikeDef()
	{
		URocketDef* Def = NewObject<URocketDef>(GetTransientPackage());
		Def->RocketName = TEXT("Falcon9Like");
		Def->PayloadDryMassKg = KPayloadKg;
		Def->PayloadMountOffsetCm = FVector(0.0, 0.0, 5000.0);
		Def->PayloadInertiaDiagonalKgM2 = FVector(100.0, 100.0, 50.0);

		FStageDef Stage1;
		Stage1.StageName = TEXT("S1");
		Stage1.DryMassKg = KStage1DryKg;
		Stage1.FuelMassKg = KStage1FuelKg;
		Stage1.SpecificImpulseSeconds = KStage1Isp;
		Stage1.MaxThrustNewtons = KStage1MaxThrust;
		Stage1.LocalMountOffsetCm = FVector(0.0, 0.0, 0.0);
		Stage1.LocalInertiaDiagonalKgM2 = FVector(10000.0, 10000.0, 500.0);
		Def->Stages.Add(Stage1);

		FStageDef Stage2;
		Stage2.StageName = TEXT("S2");
		Stage2.DryMassKg = KStage2DryKg;
		Stage2.FuelMassKg = KStage2FuelKg;
		Stage2.SpecificImpulseSeconds = KStage2Isp;
		Stage2.MaxThrustNewtons = KStage2MaxThrust;
		Stage2.LocalMountOffsetCm = FVector(0.0, 0.0, 4000.0);
		Stage2.LocalInertiaDiagonalKgM2 = FVector(3000.0, 3000.0, 200.0);
		Def->Stages.Add(Stage2);

		return Def;
	}

	/** Create a throwaway game world for tests that need a real UWorld. Caller owns the returned world and must call DestroyWorld(). */
	UWorld* MakeTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/ false, FName(TEXT("US014RocketTestWorld")));
		if (World == nullptr)
		{
			return nullptr;
		}

		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);

		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		if (World == nullptr || GEngine == nullptr)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(/*bBroadcastWorldDestroyedEvent=*/ false);
	}
}

// =============================================================================
// AC#1 — Spawn from Falcon-9-like URocketDef → 2 stages, total 549 t,
//        theoretical dV ≈ 9400 m/s (±5 %).
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketSpawnFromDefTest,
	"DeltaV.Vehicles.Rocket.SpawnFromFalcon9Def",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketSpawnFromDefTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world was created"), World))
	{
		return false;
	}

	URocketDef* Def = MakeFalcon9LikeDef();
	TestTrue(TEXT("Falcon-9-like def passes IsValid"), Def->IsValid());

	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("SpawnFromDef returned an ARocket"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual<int32>(TEXT("Rocket has 2 stages"), Rocket->Stages.Num(), 2);

	TestNotNull(TEXT("Rocket has a PayloadPart"), Rocket->PayloadPart.Get());

	// Total mass: stage 1 wet + stage 2 wet + payload = 549 000 kg.
	const double ExpectedTotal = (KStage1DryKg + KStage1FuelKg) +
	                             (KStage2DryKg + KStage2FuelKg) +
	                             KPayloadKg;
	TestEqual<int32>(TEXT("Expected total mass == 549 000 kg"),
		static_cast<int32>(ExpectedTotal + 0.5), 549000);

	// Both the direct helper and the aggregated AVehicle::TotalMass must agree.
	TestTrue(
		FString::Printf(TEXT("GetCurrentTotalMassKg=%.3f matches expected 549000"),
			Rocket->GetCurrentTotalMassKg()),
		FMath::IsNearlyEqual(Rocket->GetCurrentTotalMassKg(), ExpectedTotal, 1.0));
	TestTrue(
		FString::Printf(TEXT("AVehicle::TotalMass=%.3f matches expected 549000"),
			Rocket->TotalMass),
		FMath::IsNearlyEqual(Rocket->TotalMass, ExpectedTotal, 1.0));

	// Theoretical dV: ±5 % around 9400 m/s per AC#1.
	const double DeltaV = Rocket->CalculateTheoreticalDeltaV();
	const double Lower  = 9400.0 * 0.95;
	const double Upper  = 9400.0 * 1.05;

	AddInfo(FString::Printf(TEXT("CalculateTheoreticalDeltaV = %.2f m/s (target 9400 ± 5 %%)"), DeltaV));
	TestTrue(
		FString::Printf(TEXT("dV %.2f inside [%.2f, %.2f]"), DeltaV, Lower, Upper),
		DeltaV >= Lower && DeltaV <= Upper);

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#2 — Stage 1 ignited, 2.5 min burn at flat thrust curve drops stage mass
//        from 411 t → 27 t linearly.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketStage1LinearBurnTest,
	"DeltaV.Vehicles.Rocket.Stage1LinearBurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketStage1LinearBurnTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world was created"), World))
	{
		return false;
	}

	URocketDef* Def = MakeFalcon9LikeDef();
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("SpawnFromDef returned a rocket"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	UStageComponent* Stage1 = Rocket->Stages[0].Get();
	if (!TestNotNull(TEXT("Stage 1 is present"), Stage1))
	{
		DestroyTestWorld(World);
		return false;
	}

	// AC#2 expects the BURN to start at 411 t stage-total-mass (wet).
	const double Stage1WetKg = KStage1DryKg + KStage1FuelKg;
	TestTrue(
		FString::Printf(TEXT("Stage 1 starts at wet mass %.3f (expected %.3f)"),
			Stage1->Mass, Stage1WetKg),
		FMath::IsNearlyEqual(Stage1->Mass, Stage1WetKg, 1e-6));

	// Ignite + drive 150 × 1 s combustion steps. Constant curve ⇒ linear decay.
	Rocket->IgniteNextStage();
	TestTrue(TEXT("Stage 1 ignited"), Stage1->IsIgnited());

	constexpr int32 NumSteps = 150;
	constexpr double DtSeconds = 1.0;

	// Sample mass at 1/3 and 2/3 of the burn for a linearity check.
	double MassAt50s = -1.0;
	double MassAt100s = -1.0;

	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		Rocket->TickCombustion(DtSeconds);
		if (Step == 49)
		{
			MassAt50s = Stage1->Mass;
		}
		else if (Step == 99)
		{
			MassAt100s = Stage1->Mass;
		}
	}

	// Endpoint: stage mass should equal dry mass (27 t) within tight tolerance.
	// Tolerance is 0.01 % of dry mass so floating-point drift does not bite.
	const double EndTolerance = FMath::Max(KStage1DryKg * 1e-4, 1e-3);
	TestTrue(
		FString::Printf(TEXT("Stage 1 final mass %.3f ≈ dry %.3f (tol %.6g)"),
			Stage1->Mass, KStage1DryKg, EndTolerance),
		FMath::IsNearlyEqual(Stage1->Mass, KStage1DryKg, EndTolerance));

	TestTrue(TEXT("Stage 1 fuel depleted at end of burn"), Stage1->IsFuelDepleted());
	TestFalse(TEXT("Stage 1 auto-extinguishes at fuel depletion"), Stage1->IsIgnited());

	// Linearity: at 50 s we should have burned 1/3 of the fuel, at 100 s 2/3.
	const double Expected50  = Stage1WetKg - (KStage1FuelKg / 3.0);
	const double Expected100 = Stage1WetKg - (2.0 * KStage1FuelKg / 3.0);
	const double LinearTol   = KStage1FuelKg * 1e-4; // 0.01 % of fuel.

	TestTrue(
		FString::Printf(TEXT("Stage 1 at 50 s: %.3f ≈ %.3f (linear, tol %.3f)"),
			MassAt50s, Expected50, LinearTol),
		FMath::IsNearlyEqual(MassAt50s, Expected50, LinearTol));
	TestTrue(
		FString::Printf(TEXT("Stage 1 at 100 s: %.3f ≈ %.3f (linear, tol %.3f)"),
			MassAt100s, Expected100, LinearTol),
		FMath::IsNearlyEqual(MassAt100s, Expected100, LinearTol));

	// Mass flow rate invariant: AC#2's "linear decay" implies constant m_dot.
	const double ExpectedMassFlowKgPerS = KStage1FuelKg / KStage1BurnSeconds;
	const double ObservedMassFlowKgPerS =
		(KStage1DryKg + KStage1FuelKg - MassAt50s) / 50.0;
	TestTrue(
		FString::Printf(TEXT("Observed m_dot %.3f ≈ expected %.3f kg/s"),
			ObservedMassFlowKgPerS, ExpectedMassFlowKgPerS),
		FMath::IsNearlyEqual(ObservedMassFlowKgPerS, ExpectedMassFlowKgPerS, 1e-6));

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 — UStageComponent::Separate spawns a detached AActor and drops the
//        rocket's mass by the separated stage's dry mass.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketStageSeparationTest,
	"DeltaV.Vehicles.Rocket.StageSeparationDetachesActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketStageSeparationTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world was created"), World))
	{
		return false;
	}

	URocketDef* Def = MakeFalcon9LikeDef();
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("SpawnFromDef returned a rocket"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	UStageComponent* Stage1 = Rocket->Stages[0].Get();
	if (!TestNotNull(TEXT("Stage 1 present"), Stage1))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Drain stage 1 fuel manually — we don't need a 150 s burn loop to prove
	// the separation mechanics; AC#3 is about the Separate() call, not timing.
	Stage1->FuelMassRemainingKg = 0.0;
	Stage1->SetMass(KStage1DryKg); // route through setter to dirty the parent.
	Rocket->RecomputeInertialProperties();

	const double MassBefore = Rocket->TotalMass;
	const double ExpectedDrop = KStage1DryKg;

	AVehicle* Separated = Stage1->Separate(/*WorldOverride=*/ World);
	if (!TestNotNull(TEXT("Separate() returned a detached AVehicle"), Separated))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Separated actor is an AVehicle (not subclass)"),
		Separated->GetClass() == AVehicle::StaticClass());
	TestTrue(TEXT("Separated actor lives in the same world"),
		Separated->GetWorld() == World);
	TestNull(TEXT("Separated actor is not parented to the rocket"),
		Cast<AActor>(Separated->GetAttachParentActor()));

	// The detached AVehicle carries a single UVehiclePartComponent with the
	// stage's dry mass.
	const TArray<UVehiclePartComponent*> DetachedParts = Separated->GetRegisteredParts();
	TestEqual<int32>(TEXT("Detached AVehicle has exactly one part"),
		DetachedParts.Num(), 1);
	if (DetachedParts.Num() == 1)
	{
		TestTrue(
			FString::Printf(TEXT("Detached part mass %.3f ≈ stage dry %.3f"),
				DetachedParts[0]->Mass, KStage1DryKg),
			FMath::IsNearlyEqual(DetachedParts[0]->Mass, KStage1DryKg, 1e-6));
	}

	Separated->RecomputeInertialProperties(/*bForce=*/ true);
	TestTrue(
		FString::Printf(TEXT("Separated AVehicle TotalMass %.3f ≈ %.3f"),
			Separated->TotalMass, KStage1DryKg),
		FMath::IsNearlyEqual(Separated->TotalMass, KStage1DryKg, 1e-6));

	// AC#3: rocket mass decreases by stage 1 dry (27 t).
	const double MassAfter = Rocket->TotalMass;
	TestTrue(
		FString::Printf(TEXT("Rocket mass dropped by %.3f kg (expected %.3f)"),
			MassBefore - MassAfter, ExpectedDrop),
		FMath::IsNearlyEqual(MassBefore - MassAfter, ExpectedDrop, 1e-3));

	// Stage 0 should now be the old Stage 1 (pruned entry auto-cleaned on
	// UnregisterPart inside Separate()). Stages[0] still references the destroyed
	// component via TObjectPtr — the GC will clear it. What we can verify is
	// that the component is no longer a registered part on the rocket.
	const TArray<UVehiclePartComponent*> RocketPartsAfter = Rocket->GetRegisteredParts();
	for (const UVehiclePartComponent* Part : RocketPartsAfter)
	{
		TestFalse(TEXT("Separated stage is no longer a registered part on the rocket"),
			Part == Stage1);
	}

	Separated->Destroy();
	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#4 — Empty / null / invalid def → nullptr + explicit error log,
//        no partial actor in the world.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketSpawnInvalidDefTest,
	"DeltaV.Vehicles.Rocket.SpawnEmptyDefFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketSpawnInvalidDefTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world was created"), World))
	{
		return false;
	}

	const int32 ActorsBefore = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;

	// Case A — null def.
	AddExpectedError(
		TEXT("null URocketDef"),
		EAutomationExpectedErrorFlags::Contains, 1);
	ARocket* FromNull = ARocket::SpawnFromDef(World, /*Def=*/ nullptr, FTransform::Identity);
	TestNull(TEXT("Null def → nullptr"), FromNull);

	// Case B — empty stages array.
	URocketDef* Empty = NewObject<URocketDef>(GetTransientPackage());
	Empty->RocketName = TEXT("EmptyDef");
	TestFalse(TEXT("Empty def fails IsValid"), Empty->IsValid());

	AddExpectedError(
		TEXT("has zero stages"),
		EAutomationExpectedErrorFlags::Contains, 1);
	ARocket* FromEmpty = ARocket::SpawnFromDef(World, Empty, FTransform::Identity);
	TestNull(TEXT("Empty def → nullptr"), FromEmpty);

	// Case C — negative stage mass.
	URocketDef* Bad = NewObject<URocketDef>(GetTransientPackage());
	Bad->RocketName = TEXT("BadDef");
	FStageDef BadStage;
	BadStage.StageName = TEXT("Bad");
	BadStage.DryMassKg = -1.0; // invalid
	BadStage.FuelMassKg = 100.0;
	BadStage.SpecificImpulseSeconds = 300.0;
	BadStage.MaxThrustNewtons = 1000.0;
	Bad->Stages.Add(BadStage);

	AddExpectedError(
		TEXT("invalid DryMassKg"),
		EAutomationExpectedErrorFlags::Contains, 1);
	ARocket* FromBad = ARocket::SpawnFromDef(World, Bad, FTransform::Identity);
	TestNull(TEXT("Negative-dry-mass def → nullptr"), FromBad);

	// No partial actor in the level after the three failed spawns.
	const int32 ActorsAfter = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;
	TestEqual<int32>(TEXT("No partial ARocket left in the world"),
		ActorsAfter, ActorsBefore);

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// Invariant — Empty / flat ThrustCurve sampled as 1.0 at any time.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStageComponentThrustCurveSafetyTest,
	"DeltaV.Vehicles.Rocket.StageThrustCurveSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FStageComponentThrustCurveSafetyTest::RunTest(const FString& Parameters)
{
	UStageComponent* Stage = NewObject<UStageComponent>(GetTransientPackage());

	FStageDef Def;
	Def.StageName = TEXT("UnitTest");
	Def.DryMassKg = 100.0;
	Def.FuelMassKg = 100.0;
	Def.SpecificImpulseSeconds = 300.0;
	Def.MaxThrustNewtons = 1000.0;
	// Leave Def.ThrustCurve empty — the component must treat this as a flat 1.0.

	Stage->InitFromStageDef(Def);
	Stage->Ignite();

	const double Thrust = Stage->GetCurrentThrustNewtons();
	TestEqual(TEXT("Empty thrust curve samples as MaxThrustNewtons"),
		Thrust, 1000.0);

	const double ExpectedMDot = 1000.0 / (300.0 * KStandardGravity);
	TestTrue(
		FString::Printf(TEXT("m_dot %.9f ≈ expected %.9f"),
			Stage->GetMassFlowRateKgPerSec(), ExpectedMDot),
		FMath::IsNearlyEqual(Stage->GetMassFlowRateKgPerSec(), ExpectedMDot, 1e-9));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
