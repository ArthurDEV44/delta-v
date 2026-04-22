// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/ARocket.h"
#include "Vehicles/URocketDef.h"
#include "Vehicles/UStageComponent.h"
#include "Vehicles/UVehiclePartComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Must match the value ARocket::CalculateTheoreticalDeltaV uses internally. */
	constexpr double KStandardGravity = 9.80665;

	/** Tolerance bracket required by AC#1 and AC#2 — ±1 %. */
	constexpr double KTsiolkovskyTolerance = 0.01;

	/**
	 * Reference Tsiolkovsky calculator — authored here independently from
	 * ARocket::CalculateTheoreticalDeltaV so the test is actually proving the
	 * production code against the closed-form equation, not tautologically
	 * verifying itself.
	 *
	 * Matches the staged formulation the production code uses:
	 *   for each stage i (bottom-up), with Isp > 0 and FuelKg > 0:
	 *     m_init_i  = sum_of_wet_masses(stages[i..end]) + PayloadKg
	 *     m_final_i = m_init_i - stages[i].FuelKg
	 *     dV       += Isp_i * g0 * ln(m_init_i / m_final_i)
	 */
	double ReferenceStagedTsiolkovsky(const TArray<FStageDef>& Stages, double PayloadKg)
	{
		const double Payload = FMath::Max(PayloadKg, 0.0);

		// MassAbove[i] = sum of (dry + fuel) for stages[i+1..end] + payload.
		TArray<double> MassAbove;
		MassAbove.SetNumZeroed(Stages.Num());
		double Running = Payload;
		for (int32 Idx = Stages.Num() - 1; Idx >= 0; --Idx)
		{
			MassAbove[Idx] = Running;
			const FStageDef& S = Stages[Idx];
			Running += FMath::Max(S.DryMassKg, 0.0) + FMath::Max(S.FuelMassKg, 0.0);
		}

		double DeltaV = 0.0;
		for (int32 Idx = 0; Idx < Stages.Num(); ++Idx)
		{
			const FStageDef& S = Stages[Idx];
			const double StageDry = FMath::Max(S.DryMassKg, 0.0);
			const double StageFuel = FMath::Max(S.FuelMassKg, 0.0);
			const double Isp = S.SpecificImpulseSeconds;

			if (StageFuel <= 0.0 || Isp <= 0.0 || StageDry <= 0.0)
			{
				continue;
			}

			const double MassInit  = MassAbove[Idx] + StageDry + StageFuel;
			const double MassFinal = MassAbove[Idx] + StageDry;

			if (MassFinal <= 0.0 || MassInit <= MassFinal)
			{
				continue;
			}

			DeltaV += Isp * KStandardGravity * FMath::Loge(MassInit / MassFinal);
		}

		return DeltaV;
	}

	/** Build a URocketDef from a stage list + payload. Transient — no asset on disk. */
	URocketDef* BuildDef(const TArray<FStageDef>& Stages, double PayloadKg, const FString& Name)
	{
		URocketDef* Def = NewObject<URocketDef>(GetTransientPackage());
		Def->RocketName = FName(*Name);
		Def->PayloadDryMassKg = FMath::Max(PayloadKg, 0.0);
		Def->Stages = Stages;
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
			FName(TEXT("US018TsiolkovskyTestWorld")));
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

	/** Convenience for building a single stage with common defaults. */
	FStageDef MakeStage(
		const TCHAR* Name,
		double DryKg,
		double FuelKg,
		double IspS,
		double MaxThrustN = 1'000'000.0)
	{
		FStageDef S;
		S.StageName = FName(Name);
		S.DryMassKg = DryKg;
		S.FuelMassKg = FuelKg;
		S.SpecificImpulseSeconds = IspS;
		S.MaxThrustNewtons = MaxThrustN;
		S.LocalInertiaDiagonalKgM2 = FVector(100.0, 100.0, 50.0);
		return S;
	}
}

// =============================================================================
// AC#1 — Falcon-9-like config: total 549 t, S1 dry 22.2 t, S2 dry 4.5 t,
//        Isp avg 300 s → dV ≈ 9,420 m/s ±1 %.
//
// Stage masses chosen so staged Tsiolkovsky at Isp=300 hits the AC's 9420
// target. See the comment block below for the analytic derivation.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketTsiolkovskyFalcon9Test,
	"DeltaV.Vehicles.Rocket.TsiolkovskyEquation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketTsiolkovskyFalcon9Test::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	// Solving for stage split at total=549 t, S1_dry=22.2 t, S2_dry=4.5 t,
	// payload=4.5 t so staged Tsiolkovsky(Isp=300) yields ~9420 m/s:
	//
	//  dV = 300·g0·[ ln(549/(22.2 + S2_wet + P))
	//              + ln((S2_wet + P)/(4.5 + P)) ]  == 9420
	//
	// With P = 4.5 t, solving algebraically gives S2_wet ≈ 10.48 t, so:
	//   S1_wet = 534.02 t  (dry 22.2 + fuel 511.82)
	//   S2_wet =  10.48 t  (dry  4.5 + fuel   5.98)
	//   Payload=   4.50 t
	//   Total  = 549.00 t ✓
	//
	// This stage ratio is not physically realistic (S2 is tiny), but it is the
	// unique Falcon-9-like split the PRD's numeric constraints collapse onto.
	// The test cares about Tsiolkovsky agreement, not stage-engineering realism.
	TArray<FStageDef> Stages;
	Stages.Add(MakeStage(TEXT("S1"), /*dry=*/ 22'200.0, /*fuel=*/ 511'820.0, /*Isp=*/ 300.0));
	Stages.Add(MakeStage(TEXT("S2"), /*dry=*/  4'500.0, /*fuel=*/   5'980.0, /*Isp=*/ 300.0));
	constexpr double KPayloadKg = 4'500.0;

	// Sanity: total matches the PRD's 549 t.
	const double TotalKg =
		(Stages[0].DryMassKg + Stages[0].FuelMassKg) +
		(Stages[1].DryMassKg + Stages[1].FuelMassKg) +
		KPayloadKg;
	TestEqual<int32>(TEXT("Total rocket mass equals 549 000 kg (549 t)"),
		static_cast<int32>(TotalKg + 0.5), 549'000);

	URocketDef* Def = BuildDef(Stages, KPayloadKg, TEXT("Falcon9Like"));
	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("Rocket spawned"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	const double ExpectedDv = ReferenceStagedTsiolkovsky(Stages, KPayloadKg);
	const double ComputedDv = Rocket->CalculateTheoreticalDeltaV();

	AddInfo(FString::Printf(
		TEXT("Falcon-9-like: reference dV = %.3f m/s, production dV = %.3f m/s"),
		ExpectedDv, ComputedDv));

	// Primary check: production code matches reference Tsiolkovsky to 1 %.
	const double RelError = FMath::Abs(ComputedDv - ExpectedDv) / FMath::Max(ExpectedDv, 1.0);
	TestTrue(
		FString::Printf(TEXT("Production dV matches reference Tsiolkovsky within 1%%: relerr=%.6f"), RelError),
		RelError <= KTsiolkovskyTolerance);

	// AC sanity: the config really does model a Falcon-9-class dV near 9420 m/s.
	const double AcRelError = FMath::Abs(ExpectedDv - 9420.0) / 9420.0;
	TestTrue(
		FString::Printf(TEXT("Reference Tsiolkovsky ≈ 9420 m/s (relerr=%.6f vs 9420)"), AcRelError),
		AcRelError <= KTsiolkovskyTolerance);

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#2 — Five varied configs, each matching reference Tsiolkovsky within 1 %.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketTsiolkovskyFiveConfigsTest,
	"DeltaV.Vehicles.Rocket.TsiolkovskyFiveConfigs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketTsiolkovskyFiveConfigsTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	struct FConfig
	{
		FString Name;
		TArray<FStageDef> Stages;
		double PayloadKg;
	};

	TArray<FConfig> Configs;

	// 1. Small — single-stage sounding rocket.
	{
		FConfig C;
		C.Name = TEXT("Small");
		C.Stages.Add(MakeStage(TEXT("S1"), /*dry*/ 100.0, /*fuel*/ 900.0, /*Isp*/ 250.0));
		C.PayloadKg = 10.0;
		Configs.Add(C);
	}

	// 2. Medium — two-stage classic LEO launcher.
	{
		FConfig C;
		C.Name = TEXT("Medium");
		C.Stages.Add(MakeStage(TEXT("S1"), 5'000.0, 50'000.0, 280.0));
		C.Stages.Add(MakeStage(TEXT("S2"), 1'000.0, 10'000.0, 320.0));
		C.PayloadKg = 500.0;
		Configs.Add(C);
	}

	// 3. Big — three-stage heavy lifter.
	{
		FConfig C;
		C.Name = TEXT("Big");
		C.Stages.Add(MakeStage(TEXT("S1"), 100'000.0, 1'000'000.0, 290.0));
		C.Stages.Add(MakeStage(TEXT("S2"),  20'000.0,   200'000.0, 340.0));
		C.Stages.Add(MakeStage(TEXT("S3"),   5'000.0,    40'000.0, 360.0));
		C.PayloadKg = 5'000.0;
		Configs.Add(C);
	}

	// 4. SSTO — single-stage-to-orbit (requires high Isp to close).
	{
		FConfig C;
		C.Name = TEXT("SSTO");
		C.Stages.Add(MakeStage(TEXT("Core"), 50'000.0, 450'000.0, 380.0));
		C.PayloadKg = 10'000.0;
		Configs.Add(C);
	}

	// 5. TSTO — two-stage-to-orbit with a high-Isp upper.
	{
		FConfig C;
		C.Name = TEXT("TSTO");
		C.Stages.Add(MakeStage(TEXT("S1"), 30'000.0, 270'000.0, 300.0));
		C.Stages.Add(MakeStage(TEXT("S2"),  5'000.0,  45'000.0, 450.0));
		C.PayloadKg = 8'000.0;
		Configs.Add(C);
	}

	int32 Failures = 0;
	for (const FConfig& C : Configs)
	{
		URocketDef* Def = BuildDef(C.Stages, C.PayloadKg, C.Name);
		ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);

		if (!TestNotNull(FString::Printf(TEXT("Config '%s' spawned"), *C.Name), Rocket))
		{
			++Failures;
			continue;
		}

		const double ExpectedDv = ReferenceStagedTsiolkovsky(C.Stages, C.PayloadKg);
		const double ComputedDv = Rocket->CalculateTheoreticalDeltaV();
		const double RelError = FMath::Abs(ComputedDv - ExpectedDv) /
			FMath::Max(FMath::Abs(ExpectedDv), 1.0);

		AddInfo(FString::Printf(
			TEXT("[%s] expected=%.3f m/s  computed=%.3f m/s  relerr=%.6f"),
			*C.Name, ExpectedDv, ComputedDv, RelError));

		const bool bOk = RelError <= KTsiolkovskyTolerance;
		TestTrue(
			FString::Printf(TEXT("Config '%s' matches Tsiolkovsky within 1%%"), *C.Name),
			bOk);
		if (!bOk)
		{
			++Failures;
		}

		Rocket->Destroy();
	}

	TestEqual<int32>(TEXT("All 5 configs pass"), Failures, 0);

	DestroyTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 (unhappy path) — a fueled stage with Isp=0 contributes zero dV and
//                      surfaces a log warning.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRocketTsiolkovskyZeroIspUnhappyPathTest,
	"DeltaV.Vehicles.Rocket.TsiolkovskyZeroIspUnhappyPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FRocketTsiolkovskyZeroIspUnhappyPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	// Start from a valid def so SpawnFromDef's URocketDef::IsValid gate passes,
	// then zero the Isp on every spawned stage directly. URocketDef::IsValid
	// would otherwise reject Isp < 1.0 (set up during US-014 hardening).
	TArray<FStageDef> Stages;
	Stages.Add(MakeStage(TEXT("S1"), /*dry*/ 1'000.0, /*fuel*/ 9'000.0, /*Isp*/ 300.0));
	URocketDef* Def = BuildDef(Stages, /*Payload=*/ 100.0, TEXT("ZeroIsp"));

	ARocket* Rocket = ARocket::SpawnFromDef(World, Def, FTransform::Identity);
	if (!TestNotNull(TEXT("Rocket spawned"), Rocket))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Corrupt the runtime state to simulate a pathological Isp = 0 case.
	for (const TObjectPtr<UStageComponent>& StagePtr : Rocket->Stages)
	{
		if (UStageComponent* Stage = StagePtr.Get())
		{
			Stage->SpecificImpulseSeconds = 0.0;
		}
	}

	// AC#3: expect the one-shot warning log from CalculateTheoreticalDeltaV.
	AddExpectedError(
		TEXT("invalid Isp="),
		EAutomationExpectedErrorFlags::Contains, 1);

	const double ComputedDv = Rocket->CalculateTheoreticalDeltaV();

	TestEqual(TEXT("Zero-Isp rocket reports 0 m/s dV"),
		ComputedDv, 0.0);

	// Re-invoking must NOT re-emit the warning (AC#3 only requires one log;
	// HUD polling at 10 Hz would spam the output otherwise).
	const double SecondCall = Rocket->CalculateTheoreticalDeltaV();
	TestEqual(TEXT("Second call also returns 0"),
		SecondCall, 0.0);

	Rocket->Destroy();
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
