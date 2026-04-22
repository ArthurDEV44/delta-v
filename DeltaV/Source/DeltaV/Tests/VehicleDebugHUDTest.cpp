// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/UVehicleDebugHUDWidget.h"
#include "Vehicles/ARocket.h"
#include "Vehicles/ASatellite.h"
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
	/** Build a small Falcon-9-ish rocket def programmatically. */
	URocketDef* MakeBurnableRocketDef()
	{
		URocketDef* Def = NewObject<URocketDef>(GetTransientPackage());
		Def->RocketName = TEXT("HUDTestRocket");
		Def->PayloadDryMassKg = 0.0;

		FStageDef S;
		S.StageName = TEXT("S1");
		S.DryMassKg = 1000.0;
		S.FuelMassKg = 10'000.0;
		S.SpecificImpulseSeconds = 300.0;
		S.MaxThrustNewtons = 300'000.0;
		Def->Stages.Add(S);

		return Def;
	}

	UWorld* MakeTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, /*bInformEngineOfWorld=*/ false,
			FName(TEXT("US017HUDTestWorld")));
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

	/** Find the first line starting with a given prefix — helper for value-extraction asserts. */
	int32 FindLineStartingWith(const TArray<FString>& Lines, const TCHAR* Prefix)
	{
		for (int32 I = 0; I < Lines.Num(); ++I)
		{
			if (Lines[I].StartsWith(Prefix))
			{
				return I;
			}
		}
		return INDEX_NONE;
	}
}

// =============================================================================
// AC#1 — Formatting: mass / thrust / fuel / deltaV each rendered with 3 decimals.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleDebugHUDFormattingTest,
	"DeltaV.Vehicles.DebugHUD.FormattingPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDebugHUDFormattingTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	URocketDef* Def = MakeBurnableRocketDef();
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("Rocket spawned"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	const TArray<FString> Lines = UVehicleDebugHUDWidget::BuildDebugLines(Rocket);

	TestEqual<int32>(TEXT("Exactly 5 lines (Vehicle, Mass, Thrust, Fuel, DeltaV)"),
		Lines.Num(), 5);

	// Each numeric line must contain exactly 3 decimals. Printf %.3f guarantees
	// that locale-independently, so checking for a ".NNN " pattern is reliable.
	auto HasThreeDecimals = [](const FString& Line) -> bool
		{
			// Look for a dot followed by exactly 3 digits followed by either a
			// non-digit or end-of-string. Loop bound uses `I + 3 < Line.Len()`
			// (not `Line.Len() - 4`) so a hypothetical short line cannot wrap
			// the signed subtraction into a huge positive value.
			for (int32 I = 0; I + 3 < Line.Len(); ++I)
			{
				if (Line[I] == TEXT('.')
					&& FChar::IsDigit(Line[I + 1])
					&& FChar::IsDigit(Line[I + 2])
					&& FChar::IsDigit(Line[I + 3])
					&& (I + 4 == Line.Len() || !FChar::IsDigit(Line[I + 4])))
				{
					return true;
				}
			}
			return false;
		};

	const int32 MassIdx   = FindLineStartingWith(Lines, TEXT("Mass"));
	const int32 ThrustIdx = FindLineStartingWith(Lines, TEXT("Thrust"));
	const int32 FuelIdx   = FindLineStartingWith(Lines, TEXT("Fuel"));
	const int32 DvIdx     = FindLineStartingWith(Lines, TEXT("DeltaV"));

	if (TestNotEqual<int32>(TEXT("Mass line present"), MassIdx, INDEX_NONE))
	{
		TestTrue(FString::Printf(TEXT("Mass line '%s' uses 3-decimal precision"), *Lines[MassIdx]),
			HasThreeDecimals(Lines[MassIdx]));
		TestTrue(TEXT("Mass line ends with 'kg'"), Lines[MassIdx].EndsWith(TEXT("kg")));
	}
	if (TestNotEqual<int32>(TEXT("Thrust line present"), ThrustIdx, INDEX_NONE))
	{
		TestTrue(FString::Printf(TEXT("Thrust line '%s' uses 3-decimal precision"), *Lines[ThrustIdx]),
			HasThreeDecimals(Lines[ThrustIdx]));
		TestTrue(TEXT("Thrust line ends with 'N'"), Lines[ThrustIdx].EndsWith(TEXT("N")));
	}
	if (TestNotEqual<int32>(TEXT("Fuel line present"), FuelIdx, INDEX_NONE))
	{
		TestTrue(FString::Printf(TEXT("Fuel line '%s' uses 3-decimal precision"), *Lines[FuelIdx]),
			HasThreeDecimals(Lines[FuelIdx]));
	}
	if (TestNotEqual<int32>(TEXT("DeltaV line present"), DvIdx, INDEX_NONE))
	{
		TestTrue(FString::Printf(TEXT("DeltaV line '%s' uses 3-decimal precision"), *Lines[DvIdx]),
			HasThreeDecimals(Lines[DvIdx]));
		TestTrue(TEXT("DeltaV line ends with 'm/s'"), Lines[DvIdx].EndsWith(TEXT("m/s")));
	}

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#2 — Rocket burning → fuel decreases in real time, deltaV decreases too.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleDebugHUDRocketBurnTest,
	"DeltaV.Vehicles.DebugHUD.RocketBurnUpdatesFuelAndDeltaV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDebugHUDRocketBurnTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	URocketDef* Def = MakeBurnableRocketDef();
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("Rocket spawned"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Snapshot BEFORE burn.
	const double FuelBefore = Rocket->Stages[0]->FuelMassRemainingKg;
	const double DvBefore   = Rocket->CalculateTheoreticalDeltaV();

	TestTrue(TEXT("Initial fuel > 0"), FuelBefore > 0.0);
	TestTrue(TEXT("Initial dV > 0"), DvBefore > 0.0);

	// Ignite + burn 10 seconds. m_dot = F/(Isp*g0) = 300000 / (300*9.80665) ≈ 101.97 kg/s.
	// After 10 s, fuel should drop ~1019.7 kg.
	UStageComponent* S1 = Rocket->Stages[0].Get();
	S1->Ignite();
	Rocket->TickCombustion(10.0);

	const double FuelAfter = Rocket->Stages[0]->FuelMassRemainingKg;
	const double DvAfter   = Rocket->CalculateTheoreticalDeltaV();

	TestTrue(
		FString::Printf(TEXT("Fuel decreased: before %.3f → after %.3f"), FuelBefore, FuelAfter),
		FuelAfter < FuelBefore);
	TestTrue(
		FString::Printf(TEXT("DeltaV decreased: before %.3f → after %.3f"), DvBefore, DvAfter),
		DvAfter < DvBefore);

	// And the HUD formatter observes the same deltas — i.e., the widget
	// reflects the live values rather than cached numbers.
	const TArray<FString> Lines = UVehicleDebugHUDWidget::BuildDebugLines(Rocket);
	const int32 FuelIdx = FindLineStartingWith(Lines, TEXT("Fuel"));
	const int32 DvIdx   = FindLineStartingWith(Lines, TEXT("DeltaV"));
	if (TestNotEqual<int32>(TEXT("Fuel line present after burn"), FuelIdx, INDEX_NONE))
	{
		const FString Expected = FString::Printf(TEXT("Fuel   : %.3f kg"), FuelAfter);
		TestEqual(TEXT("Fuel line matches live remaining fuel to 3 decimals"),
			Lines[FuelIdx], Expected);
	}
	if (TestNotEqual<int32>(TEXT("DeltaV line present after burn"), DvIdx, INDEX_NONE))
	{
		const FString Expected = FString::Printf(TEXT("DeltaV : %.3f m/s"), DvAfter);
		TestEqual(TEXT("DeltaV line matches live CalculateTheoreticalDeltaV to 3 decimals"),
			Lines[DvIdx], Expected);
	}

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 — Changing the active vehicle on the widget instance re-targets it.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleDebugHUDSwitchVehicleTest,
	"DeltaV.Vehicles.DebugHUD.SwitchVehicleRetargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDebugHUDSwitchVehicleTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	// Two distinct vehicles — one rocket, one bare AVehicle — so the rendered
	// output clearly changes on switch.
	URocketDef* Def = MakeBurnableRocketDef();
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	AVehicle* Plain = World->SpawnActor<AVehicle>(AVehicle::StaticClass(), FTransform::Identity);

	if (!TestNotNull(TEXT("Rocket spawned"), Rocket) ||
	    !TestNotNull(TEXT("Plain AVehicle spawned"), Plain))
	{
		DestroyTestWorld(World);
		return false;
	}

	UVehicleDebugHUDWidget* Widget =
		NewObject<UVehicleDebugHUDWidget>(World, UVehicleDebugHUDWidget::StaticClass());
	if (!TestNotNull(TEXT("Widget constructed"), Widget))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Use the headless helper — avoids needing a full viewport / PC setup.
	Widget->SetActiveVehicle(Rocket);
	const TArray<FString> RocketLines = UVehicleDebugHUDWidget::BuildDebugLines(Rocket);
	TestTrue(TEXT("Rocket lines contain rocket name"),
		RocketLines[0].Contains(Rocket->GetName()));

	Widget->SetActiveVehicle(Plain);
	const TArray<FString> PlainLines = UVehicleDebugHUDWidget::BuildDebugLines(Plain);
	TestTrue(TEXT("Plain-vehicle lines contain plain-vehicle name"),
		PlainLines[0].Contains(Plain->GetName()));
	TestNotEqual(TEXT("Lines differ between the two vehicles"),
		RocketLines[0], PlainLines[0]);

	// Plain AVehicle has no fuel/thrust/deltaV — formatter renders 0.000.
	const int32 FuelIdx = FindLineStartingWith(PlainLines, TEXT("Fuel"));
	if (FuelIdx != INDEX_NONE)
	{
		TestEqual(TEXT("Non-rocket vehicle renders Fuel as 0.000"),
			PlainLines[FuelIdx], FString(TEXT("Fuel   : 0.000 kg")));
	}

	Widget->RemoveFromParent();
	Rocket->Destroy();
	Plain->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#4 — No active vehicle → widget renders "No active vehicle" safely.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleDebugHUDNoActiveVehicleTest,
	"DeltaV.Vehicles.DebugHUD.NoActiveVehicleDisplaysFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDebugHUDNoActiveVehicleTest::RunTest(const FString& Parameters)
{
	// Pure formatter — no world needed.
	const TArray<FString> Lines = UVehicleDebugHUDWidget::BuildDebugLines(nullptr);
	TestEqual<int32>(TEXT("Exactly one line for the null case"),
		Lines.Num(), 1);
	if (Lines.Num() > 0)
	{
		TestEqual(TEXT("Line reads 'No active vehicle'"),
			Lines[0], FString(TEXT("No active vehicle")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
