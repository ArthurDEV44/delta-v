// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/ARocket.h"
#include "Vehicles/ASatellite.h"
#include "Vehicles/UInstrumentComponent.h"
#include "Vehicles/UPowerComponent.h"
#include "Vehicles/URocketDef.h"
#include "Vehicles/UScannerInstrumentComponent.h"
#include "Vehicles/UStageComponent.h"
#include "Vehicles/UVehicleDef.h"
#include "Vehicles/UVehicleFactory.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// -------------------------------------------------------------------------
	// Test world helpers — shared with RocketTest / SatelliteTest patterns.
	// -------------------------------------------------------------------------
	UWorld* MakeTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, /*bInformEngineOfWorld=*/ false,
			FName(TEXT("US016VehicleFactoryTestWorld")));
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

	// -------------------------------------------------------------------------
	// Def builders.
	// -------------------------------------------------------------------------
	URocketDef* BuildMinimalRocketDef(UObject* Outer = GetTransientPackage())
	{
		URocketDef* Def = NewObject<URocketDef>(Outer);
		Def->RocketName = TEXT("TestRocket");
		Def->PayloadDryMassKg = 0.0;

		FStageDef S1;
		S1.StageName = TEXT("S1");
		S1.DryMassKg = 10'000.0;
		S1.FuelMassKg = 100'000.0;
		S1.SpecificImpulseSeconds = 300.0;
		S1.MaxThrustNewtons = 5'000'000.0;
		S1.LocalInertiaDiagonalKgM2 = FVector(1000.0, 1000.0, 100.0);
		Def->Stages.Add(S1);

		FStageDef S2;
		S2.StageName = TEXT("S2");
		S2.DryMassKg = 2'000.0;
		S2.FuelMassKg = 20'000.0;
		S2.SpecificImpulseSeconds = 340.0;
		S2.MaxThrustNewtons = 800'000.0;
		S2.LocalMountOffsetCm = FVector(0.0, 0.0, 3000.0);
		S2.LocalInertiaDiagonalKgM2 = FVector(200.0, 200.0, 50.0);
		Def->Stages.Add(S2);

		return Def;
	}

	UVehicleDef* BuildRocketWithSatelliteDef(UObject* Outer = GetTransientPackage())
	{
		UVehicleDef* Def = NewObject<UVehicleDef>(Outer);
		Def->VehicleName = TEXT("MyFalcon9");
		Def->RocketBody = BuildMinimalRocketDef(Def);

		Def->SatellitePayload.bEnabled = true;
		Def->SatellitePayload.SatelliteName = TEXT("Sonde01");
		Def->SatellitePayload.DryMassKg = 500.0;
		Def->SatellitePayload.BatteryCapacityWh = 150.0;
		Def->SatellitePayload.InitialChargeWh = 150.0;
		Def->SatellitePayload.SolarPanelPowerW = 75.0;
		Def->SatellitePayload.MountOffsetCm = FVector(0.0, 0.0, 200.0);

		FInstrumentDef Scanner;
		Scanner.InstrumentName = TEXT("MainScanner");
		Scanner.InstrumentClass = UScannerInstrumentComponent::StaticClass();
		Scanner.PowerDrawW = 30.0;
		Scanner.RequiredExposureSeconds = 30.0;
		Scanner.MaxRangeCm = 5'000'000.0;
		Def->SatellitePayload.Instruments.Add(Scanner);

		return Def;
	}

	UVehicleDef* BuildBareSatelliteDef(UObject* Outer = GetTransientPackage())
	{
		UVehicleDef* Def = NewObject<UVehicleDef>(Outer);
		Def->VehicleName = TEXT("FreeFlyer");
		Def->RocketBody = nullptr;

		Def->SatellitePayload.bEnabled = true;
		Def->SatellitePayload.SatelliteName = TEXT("Sonde");
		Def->SatellitePayload.DryMassKg = 300.0;
		Def->SatellitePayload.BatteryCapacityWh = 200.0;
		Def->SatellitePayload.InitialChargeWh = 100.0;
		Def->SatellitePayload.SolarPanelPowerW = 40.0;
		return Def;
	}
}

// =============================================================================
// AC#1 — UVehicleDef with 2 stages + 1 satellite payload spawns an ARocket
//        with an attached ASatellite, carrying every field from the def.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFactoryRocketWithSatelliteTest,
	"DeltaV.Vehicles.Factory.SpawnRocketWithSatellitePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleFactoryRocketWithSatelliteTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	UVehicleDef* Def = BuildRocketWithSatelliteDef();
	TestTrue(TEXT("Def passes IsValid"), Def->IsValid());

	AActor* Spawned = UVehicleFactory::Spawn(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("Factory returned a non-null actor"), Spawned))
	{
		DestroyTestWorld(World);
		return false;
	}

	ARocket* Rocket = Cast<ARocket>(Spawned);
	if (!TestNotNull(TEXT("Top-level actor is an ARocket"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual<int32>(TEXT("Rocket has 2 stages"), Rocket->Stages.Num(), 2);

	// Tsiolkovsky mass contract: rocket's effective PayloadDryMass includes
	// the satellite dry mass. So rocket TotalMass includes stage dry + fuel +
	// (satellite dry 500 kg) — the attached ASatellite actor is a SEPARATE
	// AVehicle and does NOT contribute to the rocket's AVehicle aggregate.
	// We just verify the satellite dry mass IS carried as payload.
	TestNotNull(TEXT("Rocket has a PayloadPart"), Rocket->PayloadPart.Get());
	if (Rocket->PayloadPart.Get() != nullptr)
	{
		TestTrue(
			FString::Printf(TEXT("Payload part mass %.3f ≈ satellite dry 500"),
				Rocket->PayloadPart->Mass),
			FMath::IsNearlyEqual(Rocket->PayloadPart->Mass, 500.0, 1e-6));
	}

	// Attached satellite actor check — iterate child actors.
	TArray<AActor*> Attached;
	Rocket->GetAttachedActors(Attached);

	ASatellite* AttachedSat = nullptr;
	for (AActor* A : Attached)
	{
		if (ASatellite* S = Cast<ASatellite>(A))
		{
			AttachedSat = S;
			break;
		}
	}

	if (!TestNotNull(TEXT("ASatellite attached to the rocket"), AttachedSat))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Satellite fields propagated from the def.
	UPowerComponent* SatPower = AttachedSat->PowerComponent;
	if (TestNotNull(TEXT("Attached satellite has PowerComponent"), SatPower))
	{
		TestEqual(TEXT("Battery capacity"),
			SatPower->CapacityWh, 150.0);
		TestEqual(TEXT("Initial charge"),
			SatPower->CurrentChargeWh, 150.0);
		TestEqual(TEXT("Solar panel power"),
			SatPower->SolarPanelPowerW, 75.0);
	}

	// Instrument propagated.
	TestEqual<int32>(TEXT("One instrument registered on the satellite"),
		AttachedSat->Instruments.Num(), 1);

	if (AttachedSat->Instruments.Num() > 0)
	{
		UInstrumentComponent* Instr = AttachedSat->Instruments[0].Get();
		if (TestNotNull(TEXT("Instrument is live"), Instr))
		{
			TestEqual(TEXT("Instrument PowerDrawW"), Instr->PowerDrawW, 30.0);
			UScannerInstrumentComponent* Scanner = Cast<UScannerInstrumentComponent>(Instr);
			TestNotNull(TEXT("Instrument is a scanner"), Scanner);
			if (Scanner != nullptr)
			{
				TestEqual(TEXT("Scanner exposure"),
					Scanner->RequiredExposureSeconds, 30.0);
				TestEqual(TEXT("Scanner range"),
					Scanner->MaxRangeCm, 5'000'000.0);
			}
		}
	}

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#2 — 5 sequential spawns + destroy + GC → no WeakPtr survives past GC.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFactoryFiveSequentialSpawnsNoLeakTest,
	"DeltaV.Vehicles.Factory.FiveSequentialSpawnsNoLeak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleFactoryFiveSequentialSpawnsNoLeakTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	TArray<TWeakObjectPtr<AActor>> Weaks;
	int32 TopLevelSpawnCount = 0;

	// 5 different flavours — exercises every dispatch branch of the factory.
	for (int32 Idx = 0; Idx < 5; ++Idx)
	{
		UVehicleDef* Def = nullptr;

		switch (Idx)
		{
		case 0: // bare satellite
			Def = BuildBareSatelliteDef();
			break;
		case 1: // rocket only (no satellite)
			{
				Def = NewObject<UVehicleDef>(GetTransientPackage());
				Def->VehicleName = TEXT("PureRocket");
				Def->RocketBody = BuildMinimalRocketDef(Def);
				// SatellitePayload left disabled.
			}
			break;
		case 2: // rocket + satellite
			Def = BuildRocketWithSatelliteDef();
			break;
		case 3: // small rocket (1 stage)
			{
				Def = NewObject<UVehicleDef>(GetTransientPackage());
				Def->VehicleName = TEXT("SmallRocket");
				URocketDef* R = NewObject<URocketDef>(Def);
				R->RocketName = TEXT("R_Small");
				FStageDef S;
				S.StageName = TEXT("Solo");
				S.DryMassKg = 1000.0;
				S.FuelMassKg = 5000.0;
				S.SpecificImpulseSeconds = 280.0;
				S.MaxThrustNewtons = 200'000.0;
				R->Stages.Add(S);
				Def->RocketBody = R;
			}
			break;
		case 4: // shuttle — rocket with a richer satellite payload
			{
				Def = BuildRocketWithSatelliteDef();
				Def->VehicleName = TEXT("Shuttle");
				// Add a second scanner to stress the instrument loop.
				FInstrumentDef Aux;
				Aux.InstrumentName = TEXT("AuxScanner");
				Aux.InstrumentClass = UScannerInstrumentComponent::StaticClass();
				Aux.PowerDrawW = 15.0;
				Aux.RequiredExposureSeconds = 10.0;
				Aux.MaxRangeCm = 1'000'000.0;
				Def->SatellitePayload.Instruments.Add(Aux);
			}
			break;
		default: break;
		}

		if (!TestNotNull(TEXT("Def constructed for spawn index"), Def))
		{
			continue;
		}

		AActor* Spawned = UVehicleFactory::Spawn(World, Def, FTransform::Identity);
		if (Spawned != nullptr)
		{
			++TopLevelSpawnCount;
			Weaks.Add(Spawned);

			// Track attached children to verify the ARocket::Destroyed cascade
			// also tore them down. We do NOT destroy them manually — that's
			// the point of the cascade. If a future regression breaks the
			// cascade, Weaks[child] will still be valid after GC and the test
			// will fail.
			TArray<AActor*> Children;
			Spawned->GetAttachedActors(Children, /*bResetArray=*/ true, /*bRecursivelyIncludeAttachedActors=*/ true);
			for (AActor* Child : Children)
			{
				if (Child != nullptr)
				{
					Weaks.Add(Child);
				}
			}

			Spawned->Destroy();
		}
	}

	TestEqual<int32>(TEXT("All 5 top-level spawns succeeded"), TopLevelSpawnCount, 5);

	// Force a full GC pass and verify every actor weak-ref is cleared.
	CollectGarbage(RF_NoFlags, /*bPurgeObjectHashTables=*/ true);

	int32 SurvivingCount = 0;
	for (const TWeakObjectPtr<AActor>& Weak : Weaks)
	{
		if (Weak.IsValid())
		{
			++SurvivingCount;
		}
	}

	TestEqual<int32>(TEXT("Zero actors survive GC (no leaks)"),
		SurvivingCount, 0);

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 — Edit def, re-spawn, new values are reflected (no reload required).
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFactoryReSpawnReflectsModifiedDefTest,
	"DeltaV.Vehicles.Factory.ReSpawnReflectsModifiedDef",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleFactoryReSpawnReflectsModifiedDefTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	UVehicleDef* Def = BuildBareSatelliteDef();
	Def->SatellitePayload.BatteryCapacityWh = 100.0;
	Def->SatellitePayload.InitialChargeWh = 100.0;

	AActor* First = UVehicleFactory::Spawn(World, Def, FTransform::Identity);
	ASatellite* FirstSat = Cast<ASatellite>(First);
	if (!TestNotNull(TEXT("First spawn returned ASatellite"), FirstSat))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("First spawn capacity = 100"),
		FirstSat->PowerComponent->CapacityWh, 100.0);

	// Mutate the def in place — simulates an editor tweak on the asset.
	Def->SatellitePayload.BatteryCapacityWh = 250.0;
	Def->SatellitePayload.InitialChargeWh = 250.0;
	Def->SatellitePayload.SolarPanelPowerW = 125.0;

	AActor* Second = UVehicleFactory::Spawn(World, Def, FTransform::Identity);
	ASatellite* SecondSat = Cast<ASatellite>(Second);
	if (!TestNotNull(TEXT("Second spawn returned ASatellite"), SecondSat))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("Second spawn capacity reflects edit = 250"),
		SecondSat->PowerComponent->CapacityWh, 250.0);
	TestEqual(TEXT("Second spawn solar reflects edit = 125"),
		SecondSat->PowerComponent->SolarPanelPowerW, 125.0);

	// First spawn is unchanged — each spawn snapshots the def at call time.
	TestEqual(TEXT("First spawn capacity unchanged = 100"),
		FirstSat->PowerComponent->CapacityWh, 100.0);

	First->Destroy();
	Second->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#4 — Invalid def (neither rocket nor satellite) → nullptr + log error,
//        no partial actor in the world.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFactoryInvalidDefReturnsNullTest,
	"DeltaV.Vehicles.Factory.InvalidDefReturnsNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleFactoryInvalidDefReturnsNullTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	const int32 ActorsBefore = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;

	// Case A — null def.
	AddExpectedError(
		TEXT("null UVehicleDef"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Null def → nullptr"),
		UVehicleFactory::Spawn(World, /*Def=*/ nullptr, FTransform::Identity));

	// Case B — empty def (no rocket, payload disabled).
	UVehicleDef* Empty = NewObject<UVehicleDef>(GetTransientPackage());
	Empty->VehicleName = TEXT("Nothing");
	TestFalse(TEXT("Empty def fails IsValid"), Empty->IsValid());

	AddExpectedError(
		TEXT("neither RocketBody nor enabled SatellitePayload"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Empty def → nullptr"),
		UVehicleFactory::Spawn(World, Empty, FTransform::Identity));

	// Case C — def with rocket body that's itself invalid (no stages).
	UVehicleDef* BadRocket = NewObject<UVehicleDef>(GetTransientPackage());
	BadRocket->RocketBody = NewObject<URocketDef>(BadRocket);
	BadRocket->RocketBody->RocketName = TEXT("Shell");
	// Stages array intentionally empty.

	AddExpectedError(
		TEXT("has zero stages"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Def with empty-stages rocket body → nullptr"),
		UVehicleFactory::Spawn(World, BadRocket, FTransform::Identity));

	// Case D — satellite payload with non-finite battery capacity.
	UVehicleDef* BadSat = NewObject<UVehicleDef>(GetTransientPackage());
	BadSat->SatellitePayload.bEnabled = true;
	BadSat->SatellitePayload.DryMassKg = 100.0;
	BadSat->SatellitePayload.BatteryCapacityWh = std::numeric_limits<double>::quiet_NaN();

	AddExpectedError(
		TEXT("BatteryCapacityWh"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Def with NaN battery → nullptr"),
		UVehicleFactory::Spawn(World, BadSat, FTransform::Identity));

	const int32 ActorsAfter = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;
	TestEqual<int32>(TEXT("No partial actors left in the world"),
		ActorsAfter, ActorsBefore);

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// Invariant — FindVehicleDefByName(NAME_None) safely returns nullptr.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFactoryFindByNameSafetyTest,
	"DeltaV.Vehicles.Factory.FindByNameNullSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleFactoryFindByNameSafetyTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("FindVehicleDefByName(NAME_None) returns nullptr"),
		UVehicleFactory::FindVehicleDefByName(NAME_None));

	// Unknown name — tests sometimes run without a fully-populated registry.
	// The call must return nullptr cleanly rather than crashing.
	TestNull(TEXT("Unknown-name lookup returns nullptr"),
		UVehicleFactory::FindVehicleDefByName(FName(TEXT("Definitely_Not_A_Vehicle_asdfqwer"))));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
