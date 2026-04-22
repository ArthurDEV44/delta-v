// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/VehicleInertialTestListener.h"
#include "Vehicles/AVehicle.h"
#include "Vehicles/UVehiclePartComponent.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

// Listener implementations live outside WITH_DEV_AUTOMATION_TESTS so the
// reflection-generated vtable always has concrete symbols to link against.
void UVehicleInertialTestListener::HandleChanged(
	double NewTotalMass,
	FVector NewCenterOfMass,
	FMatrix NewMomentOfInertia)
{
	++BroadcastCount;
	LastMass = NewTotalMass;
	LastCoM = NewCenterOfMass;
	LastMoI = NewMomentOfInertia;
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double KCmToM = 0.01;

	/** Create a transient AVehicle parented to the transient package — no world needed. */
	AVehicle* MakeVehicle()
	{
		return NewObject<AVehicle>(GetTransientPackage());
	}

	/**
	 * Construct a part owned by Vehicle, set its mass / local CoM / local inertia
	 * diagonal, and register it directly with the vehicle. Bypasses OnRegister
	 * (which fires through the scene component system and requires a registered
	 * world) because RegisterPart is the authoritative seam.
	 */
	UVehiclePartComponent* MakePart(
		AVehicle* Vehicle,
		double MassKg,
		const FVector& LocalCoMCm,
		const FVector& LocalInertiaDiagKgM2)
	{
		UVehiclePartComponent* Part = NewObject<UVehiclePartComponent>(Vehicle);
		Part->Mass = MassKg;
		Part->LocalCenterOfMass = LocalCoMCm;
		Part->LocalInertiaDiagonal = LocalInertiaDiagKgM2;
		Vehicle->RegisterPart(Part);
		return Part;
	}

	/** Bind a dynamic listener onto the vehicle and return it. */
	UVehicleInertialTestListener* MakeListener(AVehicle* Vehicle)
	{
		UVehicleInertialTestListener* Listener = NewObject<UVehicleInertialTestListener>();
		Vehicle->OnInertialPropertiesChanged.AddDynamic(
			Listener, &UVehicleInertialTestListener::HandleChanged);
		return Listener;
	}
}

// =============================================================================
// AC#1 — Aggregation correctness + 3-decimal debug string.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleInertialAggregationTest,
	"DeltaV.Vehicles.Vehicle.InertialAggregation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleInertialAggregationTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();

	// Symmetric 3-part rig. Three identical 10 kg point-like parts spaced
	// symmetrically around the vehicle origin along +X / -X / +Y:
	//
	//  Part A at (+100, 0, 0) cm   I = (0.1, 0.1, 0.1) kg.m^2
	//  Part B at (-100, 0, 0) cm   I = (0.1, 0.1, 0.1) kg.m^2
	//  Part C at (   0, 50, 0) cm  I = (0.1, 0.1, 0.1) kg.m^2
	//
	// Expected aggregate:
	//   TotalMass = 30 kg
	//   CoM       = (0, 50/3, 0) cm  (≈ 16.667 cm along Y)
	MakePart(Vehicle, 10.0, FVector(+100.0, 0.0, 0.0), FVector(0.1, 0.1, 0.1));
	MakePart(Vehicle, 10.0, FVector(-100.0, 0.0, 0.0), FVector(0.1, 0.1, 0.1));
	MakePart(Vehicle, 10.0, FVector(0.0, +50.0, 0.0),  FVector(0.1, 0.1, 0.1));

	Vehicle->RecomputeInertialProperties();

	// Total mass.
	TestEqual(TEXT("TotalMass == 30 kg"), Vehicle->TotalMass, 30.0);

	// CoM in cm, mass-weighted centroid.
	const FVector ExpectedCoM(0.0, 50.0 / 3.0, 0.0);
	TestTrue(
		FString::Printf(TEXT("CoM close to expected: got (%.6f, %.6f, %.6f), expected (%.6f, %.6f, %.6f)"),
			Vehicle->CenterOfMass.X, Vehicle->CenterOfMass.Y, Vehicle->CenterOfMass.Z,
			ExpectedCoM.X, ExpectedCoM.Y, ExpectedCoM.Z),
		Vehicle->CenterOfMass.Equals(ExpectedCoM, 1e-9));

	// Parallel-axis MoI — analytic computation for cross-check.
	//
	// Displacement of each part from composite CoM (converted to meters):
	//   A: ( 1.0, -1/6, 0)   ((+100 - 0) * 0.01, (0 - 50/3) * 0.01, 0)
	//   B: (-1.0, -1/6, 0)
	//   C: ( 0,    1/3, 0)   ((0 - 0) * 0.01,   (50 - 50/3) * 0.01, 0)
	const double M = 10.0;
	const double IlDiag = 0.1;
	auto Sq = [](double X) { return X * X; };

	const FVector dA((100.0 - 0.0) * KCmToM, (0.0 - 50.0 / 3.0) * KCmToM, 0.0);
	const FVector dB((-100.0 - 0.0) * KCmToM, (0.0 - 50.0 / 3.0) * KCmToM, 0.0);
	const FVector dC((0.0 - 0.0) * KCmToM, (50.0 - 50.0 / 3.0) * KCmToM, 0.0);

	const double Ixx =
		IlDiag + M * (Sq(dA.Y) + Sq(dA.Z)) +
		IlDiag + M * (Sq(dB.Y) + Sq(dB.Z)) +
		IlDiag + M * (Sq(dC.Y) + Sq(dC.Z));
	const double Iyy =
		IlDiag + M * (Sq(dA.X) + Sq(dA.Z)) +
		IlDiag + M * (Sq(dB.X) + Sq(dB.Z)) +
		IlDiag + M * (Sq(dC.X) + Sq(dC.Z));
	const double Izz =
		IlDiag + M * (Sq(dA.X) + Sq(dA.Y)) +
		IlDiag + M * (Sq(dB.X) + Sq(dB.Y)) +
		IlDiag + M * (Sq(dC.X) + Sq(dC.Y));

	const double Ixy = -M * dA.X * dA.Y - M * dB.X * dB.Y - M * dC.X * dC.Y;
	const double Ixz = -M * dA.X * dA.Z - M * dB.X * dB.Z - M * dC.X * dC.Z;
	const double Iyz = -M * dA.Y * dA.Z - M * dB.Y * dB.Z - M * dC.Y * dC.Z;

	const double Tol = 1e-9;
	TestTrue(TEXT("MoI[0][0] matches analytic Ixx"), FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[0][0], Ixx, Tol));
	TestTrue(TEXT("MoI[1][1] matches analytic Iyy"), FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[1][1], Iyy, Tol));
	TestTrue(TEXT("MoI[2][2] matches analytic Izz"), FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[2][2], Izz, Tol));

	TestTrue(TEXT("MoI symmetric (0,1)=(1,0)"),
		FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[0][1], Vehicle->MomentOfInertia.M[1][0], Tol));
	TestTrue(TEXT("MoI[0][1] matches analytic Ixy"),
		FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[0][1], Ixy, Tol));
	TestTrue(TEXT("MoI[0][2] matches analytic Ixz"),
		FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[0][2], Ixz, Tol));
	TestTrue(TEXT("MoI[1][2] matches analytic Iyz"),
		FMath::IsNearlyEqual(Vehicle->MomentOfInertia.M[1][2], Iyz, Tol));

	// AC#1: debug string exposes 3-decimal precision for every field. We look
	// for the mass line as the canonical marker — FString::Printf with "%.3f"
	// guarantees exactly 3 decimals in en-US locale, which Unreal uses.
	const FString Info = Vehicle->GetDebugInfoString();
	TestTrue(TEXT("Debug string contains 'TotalMass      : 30.000 kg'"),
		Info.Contains(TEXT("TotalMass      : 30.000 kg")));
	TestTrue(TEXT("Debug string reports 3 parts"),
		Info.Contains(TEXT("Parts          : 3 registered")));
	TestTrue(TEXT("Debug string reports CoM with 3 decimals on Y"),
		// 50/3 ≈ 16.667 formatted with %.3f
		Info.Contains(TEXT("CenterOfMass   : (0.000, 16.667, 0.000) cm")));

	return true;
}

// =============================================================================
// AC#2 — OnInertialPropertiesChanged fires when |Δmass| ≥ 10% of prior mass,
//        and is NOT fired for sub-threshold changes.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleInertialDeltaBroadcastTest,
	"DeltaV.Vehicles.Vehicle.DeltaBroadcast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleInertialDeltaBroadcastTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	UVehicleInertialTestListener* Listener = MakeListener(Vehicle);

	// Start with a 100 kg part.
	UVehiclePartComponent* FuelTank = MakePart(
		Vehicle, 100.0, FVector::ZeroVector, FVector(1.0, 1.0, 1.0));

	// First recompute with non-zero mass always broadcasts (initial population).
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("First populated recompute broadcasts exactly once"),
		Listener->BroadcastCount, 1);
	TestEqual(TEXT("Broadcast reports TotalMass = 100 kg"),
		Listener->LastMass, 100.0);

	// Sub-threshold: lose 5% of mass (drop from 100 → 95). Must NOT broadcast.
	FuelTank->SetMass(95.0);
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("5% drop does not fire broadcast"),
		Listener->BroadcastCount, 1);

	// Sub-threshold again (from last-broadcast 100 → 91, still < 10%). Must NOT broadcast.
	FuelTank->SetMass(91.0);
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("9% drop (vs last-broadcast 100) does not fire"),
		Listener->BroadcastCount, 1);

	// Threshold crossed: 100 → 90 (exactly 10%). Must broadcast.
	FuelTank->SetMass(90.0);
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("10% drop (vs last-broadcast 100) fires broadcast"),
		Listener->BroadcastCount, 2);
	TestEqual(TEXT("Broadcast reports TotalMass = 90 kg"),
		Listener->LastMass, 90.0);

	// After last broadcast was 90, we need |Δ| ≥ 9 kg to re-fire. Drop by 8 kg → no broadcast.
	FuelTank->SetMass(82.0);
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("Δ=8 kg from last-broadcast 90 does not fire"),
		Listener->BroadcastCount, 2);

	// Drop one more kg → cumulative Δ = 9 kg from 90 → fires.
	FuelTank->SetMass(81.0);
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("Cumulative Δ=9 kg reaches threshold and fires"),
		Listener->BroadcastCount, 3);
	TestEqual(TEXT("Broadcast reports TotalMass = 81 kg"),
		Listener->LastMass, 81.0);

	// Idempotent: recompute without any dirty change does nothing.
	Vehicle->RecomputeInertialProperties();
	TestEqual<int32>(TEXT("Clean recompute is a no-op"),
		Listener->BroadcastCount, 3);

	return true;
}

// =============================================================================
// AC#3 — 1000 vehicles × 3 parts, full dirty sweep under 5 ms.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleInertialStressPerformanceTest,
	"DeltaV.Vehicles.Vehicle.StressPerformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleInertialStressPerformanceTest::RunTest(const FString& Parameters)
{
	constexpr int32 NumVehicles = 1000;

	TArray<AVehicle*> Vehicles;
	Vehicles.Reserve(NumVehicles);

	for (int32 I = 0; I < NumVehicles; ++I)
	{
		AVehicle* V = MakeVehicle();
		MakePart(V, 10.0, FVector(+100.0, 0.0, 0.0), FVector(0.1, 0.1, 0.1));
		MakePart(V, 10.0, FVector(-100.0, 0.0, 0.0), FVector(0.1, 0.1, 0.1));
		MakePart(V, 10.0, FVector(0.0, +50.0, 0.0),  FVector(0.1, 0.1, 0.1));
		// MakePart -> RegisterPart already marks dirty; no explicit call needed.
		Vehicles.Add(V);
	}

	// Everything is dirty from construction. Measure one full sweep.
	const double T0 = FPlatformTime::Seconds();
	for (AVehicle* V : Vehicles)
	{
		V->RecomputeInertialProperties();
	}
	const double T1 = FPlatformTime::Seconds();
	const double DirtySweepMs = (T1 - T0) * 1000.0;

	// After the sweep, all vehicles are clean. A second sweep exercises the
	// early-out path and must be essentially free (tens of µs at most).
	const double T2 = FPlatformTime::Seconds();
	for (AVehicle* V : Vehicles)
	{
		V->RecomputeInertialProperties();
	}
	const double T3 = FPlatformTime::Seconds();
	const double CleanSweepMs = (T3 - T2) * 1000.0;

	AddInfo(FString::Printf(
		TEXT("Dirty sweep of %d vehicles: %.3f ms. Clean sweep: %.3f ms."),
		NumVehicles, DirtySweepMs, CleanSweepMs));

	// Primary AC#3 budget.
	TestTrue(
		FString::Printf(TEXT("Dirty sweep of 1000 vehicles under 5 ms budget (got %.3f ms)"),
			DirtySweepMs),
		DirtySweepMs < 5.0);

	// Regression guard on dirty-flag gating. Only checked when the dirty sweep
	// is long enough that timing jitter cannot dominate the ratio — below ~0.1
	// ms the microsecond-resolution timer noise swamps any real signal.
	if (DirtySweepMs > 0.1)
	{
		TestTrue(
			FString::Printf(
				TEXT("Clean sweep (%.3f ms) is at least 2x cheaper than dirty (%.3f ms)"),
				CleanSweepMs, DirtySweepMs),
			CleanSweepMs * 2.0 < DirtySweepMs);
	}
	else
	{
		AddInfo(TEXT("Dirty sweep under 0.1 ms; skipping ratio check (jitter dominates)."));
	}

	// Cleanup so the GC run after the test finds no dangling strong refs from
	// the test scratch array.
	Vehicles.Reset();
	return true;
}

// =============================================================================
// AC#4 — A vehicle with no parts reports mass=0, CoM=origin, MoI=identity,
//        emits a single log warning, and does not crash.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleEmptyVehicleSafetyTest,
	"DeltaV.Vehicles.Vehicle.EmptyVehicleSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleEmptyVehicleSafetyTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();

	// Match the warning that DoRecompute emits for an empty vehicle so the
	// automation harness doesn't surface it as a test failure. Regex is the
	// stable seam here (exact actor name contains a GUID-ish suffix).
	AddExpectedError(
		TEXT("has no UVehiclePartComponent"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	// First recompute: forces the empty-state branch, logs warning once.
	Vehicle->RecomputeInertialProperties(/*bForce=*/ true);

	TestEqual(TEXT("Empty vehicle TotalMass == 0"), Vehicle->TotalMass, 0.0);
	TestTrue(TEXT("Empty vehicle CoM == zero"),
		Vehicle->CenterOfMass.IsNearlyZero());
	TestTrue(TEXT("Empty vehicle MoI == identity"),
		Vehicle->MomentOfInertia.Equals(FMatrix::Identity, 1e-12));

	// Debug string must render without hitting any formatter assertions on the
	// degenerate values (e.g., no NaN / inf leakage).
	const FString Info = Vehicle->GetDebugInfoString();
	TestTrue(TEXT("Empty debug string shows 0 parts"),
		Info.Contains(TEXT("Parts          : 0 registered")));
	TestTrue(TEXT("Empty debug string shows 0.000 kg total mass"),
		Info.Contains(TEXT("TotalMass      : 0.000 kg")));

	// Second recompute: bHasLoggedEmptyWarning must gate the warning so only
	// one line shows up in the log, even across repeated forced recomputes.
	// AddExpectedError with count=1 fails the test if a second line appears.
	Vehicle->RecomputeInertialProperties(/*bForce=*/ true);

	return true;
}

// =============================================================================
// Invariant — dirty-flag gating: a clean vehicle must early-out.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleDirtyFlagGatingTest,
	"DeltaV.Vehicles.Vehicle.DirtyFlagGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FVehicleDirtyFlagGatingTest::RunTest(const FString& Parameters)
{
	AVehicle* Vehicle = MakeVehicle();
	UVehiclePartComponent* Part = MakePart(
		Vehicle, 50.0, FVector(+200.0, 0.0, 0.0), FVector(0.5, 0.5, 0.5));

	TestTrue(TEXT("Dirty after RegisterPart"), Vehicle->IsInertialPropertiesDirty());

	Vehicle->RecomputeInertialProperties();
	TestFalse(TEXT("Clean after recompute"), Vehicle->IsInertialPropertiesDirty());

	// Direct field mutation does NOT mark dirty — this is documented behavior
	// so the test pins it.
	Part->Mass = 123.456;
	TestFalse(TEXT("Direct field mutation does not mark dirty (documented)"),
		Vehicle->IsInertialPropertiesDirty());

	// Setter goes through NotifyOwnerDirty.
	Part->SetMass(75.0);
	TestTrue(TEXT("SetMass marks dirty"), Vehicle->IsInertialPropertiesDirty());

	Vehicle->RecomputeInertialProperties();
	TestFalse(TEXT("Clean again after recompute"),
		Vehicle->IsInertialPropertiesDirty());
	TestEqual(TEXT("TotalMass reflects the post-setter value"),
		Vehicle->TotalMass, 75.0);

	// Unregister marks dirty.
	Vehicle->UnregisterPart(Part);
	TestTrue(TEXT("UnregisterPart marks dirty"),
		Vehicle->IsInertialPropertiesDirty());

	// Expect the empty warning when the last part is removed and we recompute.
	AddExpectedError(
		TEXT("has no UVehiclePartComponent"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	Vehicle->RecomputeInertialProperties();
	TestEqual(TEXT("TotalMass back to 0 after last part unregistered"),
		Vehicle->TotalMass, 0.0);
	// Identity MoI after empty — invariant check so this doesn't silently drift.
	TestTrue(TEXT("MoI reverts to identity after going empty"),
		Vehicle->MomentOfInertia.Equals(FMatrix::Identity, 1e-12));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
