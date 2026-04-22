// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/SatelliteTestListener.h"
#include "Vehicles/ASatellite.h"
#include "Vehicles/AVehicle.h"
#include "Vehicles/FScanResult.h"
#include "Vehicles/UInstrumentComponent.h"
#include "Vehicles/UPowerComponent.h"
#include "Vehicles/UScannerInstrumentComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Listener implementations live outside WITH_DEV_AUTOMATION_TESTS so the
// reflection-generated vtable always has concrete symbols to link against.
void USatelliteTestListener::HandleScanCompleted(const FScanResult& Result)
{
	++ScanCompletedCount;
	LastCompletedResult = Result;
}

void USatelliteTestListener::HandleScanFailed(const FScanResult& InvalidResult)
{
	++ScanFailedCount;
	LastFailedResult = InvalidResult;
}

void USatelliteTestListener::HandlePowerDepleted()
{
	++PowerDepletedCount;
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * Build a satellite programmatically, outside of any UWorld. NewObject is
	 * enough for AC#1 / AC#3 / AC#4 (pure state-machine work). AC#2 spins up
	 * a minimal world so the scanner can compute a real world-space range.
	 */
	ASatellite* MakeTransientSatellite()
	{
		ASatellite* Sat = NewObject<ASatellite>(GetTransientPackage());
		// Construction-time sub-object creation gives us PowerComponent already.
		return Sat;
	}

	UScannerInstrumentComponent* AddScanner(ASatellite* Sat, double PowerDrawW, double RequiredExposureS)
	{
		UScannerInstrumentComponent* Scanner = NewObject<UScannerInstrumentComponent>(Sat);
		Scanner->PowerDrawW = PowerDrawW;
		Scanner->RequiredExposureSeconds = RequiredExposureS;
		Sat->RegisterInstrument(Scanner);
		return Scanner;
	}

	UWorld* MakeTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, /*bInformEngineOfWorld=*/ false,
			FName(TEXT("US015SatelliteTestWorld")));
		if (World == nullptr)
		{
			return nullptr;
		}
		FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.SetCurrentWorld(World);
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		if (World == nullptr || GEngine == nullptr)
		{
			return;
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

// =============================================================================
// AC#1 — 100 Wh battery + 50 W solar + 1 scanner @ 30 W, in shadow, 20 min →
//        battery drops by 10 Wh.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSatelliteShadowPowerDrainTest,
	"DeltaV.Vehicles.Satellite.ShadowPowerDrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSatelliteShadowPowerDrainTest::RunTest(const FString& Parameters)
{
	ASatellite* Sat = MakeTransientSatellite();
	UPowerComponent* Power = Sat->PowerComponent;

	Power->CapacityWh = 100.0;
	Power->SetCurrentChargeWh(100.0);
	Power->SolarPanelPowerW = 50.0;
	Power->bIsInShadow = true;

	UScannerInstrumentComponent* Scanner = AddScanner(Sat, /*PowerDrawW=*/ 30.0, /*Req=*/ 30.0);

	// Manually activate without BeginScan — we don't care about a target here,
	// only the draw accounting. Activate goes through UInstrumentComponent.
	TestTrue(TEXT("Scanner activates on a fully charged satellite"), Scanner->ActivateInstrument());

	TestTrue(
		FString::Printf(TEXT("Active draw = 30 W (got %.3f W)"), Power->GetActiveDrawW()),
		FMath::IsNearlyEqual(Power->GetActiveDrawW(), 30.0, 1e-9));

	// Advance 20 minutes = 1200 seconds. Drive TickPower directly rather than
	// StepSimulation — AC#1 scope is the energy budget, not the instrument
	// lifecycle. The scanner's own TickInstrument would fail-close with no
	// bound target, muddying the test. The math is exact for constant draw,
	// so one coarse step matches 1200 × 1-second steps to the last ULP.
	constexpr double TwentyMinSeconds = 20.0 * 60.0;
	const double NetW = Power->TickPower(TwentyMinSeconds);
	TestTrue(
		FString::Printf(TEXT("NetW reported as -30 W (got %.3f)"), NetW),
		FMath::IsNearlyEqual(NetW, -30.0, 1e-9));

	// Expected: 30 W × 1200 s / 3600 (s/h) = 10 Wh drain.
	const double Expected = 100.0 - 10.0;
	TestTrue(
		FString::Printf(TEXT("After 20 min shadow @ 30 W: %.6f Wh ≈ %.6f Wh"),
			Power->CurrentChargeWh, Expected),
		FMath::IsNearlyEqual(Power->CurrentChargeWh, Expected, 1e-9));

	// Scanner still active (battery > 0) and NOT in safe mode.
	TestTrue(TEXT("Scanner still active"), Scanner->bActive);
	TestFalse(TEXT("Not in safe mode yet"), Power->bSafeMode);

	return true;
}

// =============================================================================
// AC#2 — Scanner activated facing an asteroid at 50 km, 30 s exposure →
//        OnScanCompleted fires with a valid FScanResult.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSatelliteScanProducesTypedResultTest,
	"DeltaV.Vehicles.Satellite.ScanProducesTypedResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSatelliteScanProducesTypedResultTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	// Spawn a satellite and a target actor 50 km apart.
	ASatellite* Sat = World->SpawnActor<ASatellite>(
		ASatellite::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Satellite spawned"), Sat))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Use AVehicle as the target stand-in: AActor has no root component by
	// default, so GetActorLocation()/SetActorLocation are no-ops on a bare
	// AActor. AVehicle's constructor creates a scene root, giving us real
	// world-space positioning for the range check. US-046 will swap this for
	// AAsteroid when the asteroid class lands.
	AVehicle* Target = World->SpawnActor<AVehicle>(
		AVehicle::StaticClass(), FTransform(FVector(5'000'000.0, 0.0, 0.0)));
	if (!TestNotNull(TEXT("Target actor spawned"), Target))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Fresh charge, out of shadow → solar replaces the 30 W scanner draw
	// exactly at the SolarPanelPowerW=30 default? Not quite — Solar=50W by
	// default, so during the scan the battery actually rises slightly. That
	// is fine for AC#2: we just need the scan to COMPLETE.
	UPowerComponent* Power = Sat->PowerComponent;
	Power->CapacityWh = 100.0;
	Power->SetCurrentChargeWh(100.0);
	Power->bIsInShadow = false;

	UScannerInstrumentComponent* Scanner = AddScanner(Sat, 30.0, /*RequiredExposureS=*/ 30.0);
	Scanner->MaxRangeCm = 5'000'000.0 + 1.0; // 50 km + epsilon — AC says "at 50 km" inclusive.

	USatelliteTestListener* Listener = NewObject<USatelliteTestListener>();
	Scanner->OnScanCompleted.AddDynamic(Listener, &USatelliteTestListener::HandleScanCompleted);
	Scanner->OnScanFailed.AddDynamic(Listener, &USatelliteTestListener::HandleScanFailed);

	// BeginScan returns Invalid() as the "in-progress" sentinel — the real
	// result arrives via OnScanCompleted.
	const FScanResult InitialReturn = Scanner->BeginScan(Target);
	TestFalse(TEXT("BeginScan returns Invalid() while scan is in progress"),
		InitialReturn.bValid);
	TestTrue(TEXT("Scanner is active after BeginScan"), Scanner->bActive);

	// Drive 30 s of exposure in 1-second steps.
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Sat->StepSimulation(1.0);
	}

	TestEqual<int32>(TEXT("OnScanCompleted fires exactly once"),
		Listener->ScanCompletedCount, 1);
	TestEqual<int32>(TEXT("OnScanFailed does not fire"),
		Listener->ScanFailedCount, 0);

	const FScanResult& R = Listener->LastCompletedResult;
	TestTrue(TEXT("Scan result bValid"), R.bValid);
	TestEqual<FName>(TEXT("TargetName is the target actor's FName"),
		R.TargetName, Target->GetFName());
	TestTrue(TEXT("EstimatedMassKg is > 0"), R.EstimatedMassKg > 0.0);
	TestTrue(TEXT("CompositionTags non-empty"), R.CompositionTags.Num() > 0);
	TestFalse(TEXT("Scanner deactivated on completion"), Scanner->bActive);

	// Range-boundary subtest: move the target to 60 km, scan should fail.
	Target->SetActorLocation(FVector(6'000'000.0, 0.0, 0.0));
	Listener->ScanCompletedCount = 0;
	Listener->ScanFailedCount = 0;

	Scanner->BeginScan(Target);
	Sat->StepSimulation(1.0); // first tick flags out-of-range.
	TestEqual<int32>(TEXT("Out-of-range scan fails via OnScanFailed"),
		Listener->ScanFailedCount, 1);
	TestEqual<int32>(TEXT("Out-of-range scan does not complete"),
		Listener->ScanCompletedCount, 0);

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 — Battery in shadow → depletion → safe mode engaged, instruments OFF,
//        beacon stays on.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSatelliteSafeModeOnDepletionTest,
	"DeltaV.Vehicles.Satellite.SafeModeOnDepletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSatelliteSafeModeOnDepletionTest::RunTest(const FString& Parameters)
{
	ASatellite* Sat = MakeTransientSatellite();
	UPowerComponent* Power = Sat->PowerComponent;

	// Force BeginPlay wiring manually — needed for OnPowerDepleted → HandlePowerDepleted
	// since NewObject'd actors without a world don't go through BeginPlay.
	// Rebind the subscription directly from the test.
	Power->OnPowerDepleted.AddDynamic(Sat, &ASatellite::HandlePowerDepleted);

	// Low charge + in shadow so we deplete in a small number of ticks.
	Power->CapacityWh = 10.0;
	Power->SetCurrentChargeWh(10.0);
	Power->SolarPanelPowerW = 50.0;
	Power->bIsInShadow = true;

	UScannerInstrumentComponent* Scanner = AddScanner(Sat, /*PowerDrawW=*/ 30.0, /*Req=*/ 30.0);
	TestTrue(TEXT("Scanner activates while battery > 0"), Scanner->ActivateInstrument());

	USatelliteTestListener* Listener = NewObject<USatelliteTestListener>();
	Power->OnPowerDepleted.AddDynamic(Listener, &USatelliteTestListener::HandlePowerDepleted);

	// 10 Wh at 30 W = 1200 s to deplete. Run a single coarse step past that point.
	AddExpectedError(
		TEXT("entering safe mode"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(
		TEXT("shutting down instruments"),
		EAutomationExpectedErrorFlags::Contains, 1);

	Sat->StepSimulation(1500.0);

	TestEqual<int32>(TEXT("OnPowerDepleted fires exactly once"),
		Listener->PowerDepletedCount, 1);
	TestTrue(TEXT("Power component in safe mode"), Power->bSafeMode);
	TestTrue(TEXT("Power component reports depleted"), Power->IsPowerDepleted());
	TestFalse(TEXT("Scanner deactivated by safe-mode propagation"),
		Scanner->bActive);
	TestTrue(TEXT("Beacon stays on even in safe mode"),
		Sat->IsMinimalBeaconOn());

	// Idempotency — a second depletion tick must not re-fire OnPowerDepleted.
	Sat->StepSimulation(1.0);
	TestEqual<int32>(TEXT("OnPowerDepleted does not re-fire on subsequent tick"),
		Listener->PowerDepletedCount, 1);

	return true;
}

// =============================================================================
// AC#4 (unhappy) — Instrument activated without energy returns Invalid +
//                 log warning, does not crash.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSatelliteUnpoweredActivationFailsTest,
	"DeltaV.Vehicles.Satellite.UnpoweredActivationFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSatelliteUnpoweredActivationFailsTest::RunTest(const FString& Parameters)
{
	ASatellite* Sat = MakeTransientSatellite();
	UPowerComponent* Power = Sat->PowerComponent;

	Power->CapacityWh = 100.0;
	Power->SetCurrentChargeWh(0.0);
	Power->bIsInShadow = true;

	UScannerInstrumentComponent* Scanner = AddScanner(Sat, 30.0, 30.0);

	// BeginScan is the synchronous return path. AC#4 requires FScanResult::Invalid.
	AddExpectedError(
		TEXT("refused"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AActor* DummyTarget = NewObject<AActor>(GetTransientPackage());
	const FScanResult Returned = Scanner->BeginScan(DummyTarget);

	TestFalse(TEXT("BeginScan returns Invalid when no power"), Returned.bValid);
	TestFalse(TEXT("Scanner not active after failed BeginScan"), Scanner->bActive);
	TestEqual<int32>(TEXT("Active draw remains 0 W"),
		static_cast<int32>(Power->GetActiveDrawW() + 0.5), 0);

	// Directly calling Activate must also fail for the same reason.
	AddExpectedError(
		TEXT("refused"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Direct ActivateInstrument() also fails with no power"), Scanner->ActivateInstrument());

	return true;
}

// =============================================================================
// Invariant — ProduceScanResult(nullptr) is safe and returns Invalid().
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSatelliteProduceScanResultNullSafetyTest,
	"DeltaV.Vehicles.Satellite.ProduceScanResultNullSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FSatelliteProduceScanResultNullSafetyTest::RunTest(const FString& Parameters)
{
	ASatellite* Sat = MakeTransientSatellite();
	UScannerInstrumentComponent* Scanner = AddScanner(Sat, 30.0, 30.0);

	const FScanResult NullResult = Scanner->ProduceScanResult(nullptr);
	TestFalse(TEXT("ProduceScanResult(nullptr) is invalid"), NullResult.bValid);
	TestEqual(TEXT("EstimatedMassKg is zero for invalid result"),
		NullResult.EstimatedMassKg, 0.0);
	TestEqual<int32>(TEXT("CompositionTags empty for invalid result"),
		NullResult.CompositionTags.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
