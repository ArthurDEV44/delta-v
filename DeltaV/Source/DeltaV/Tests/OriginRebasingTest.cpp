// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/OriginRebasingTestListener.h"
#include "Physics/OriginRebasingSubsystem.h"

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

void UOriginRebasingTestListener::HandleRebased(FVector AppliedOffsetMeters)
{
	++BroadcastCount;
	LastOffset = AppliedOffsetMeters;
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Spawn a transient-package subsystem instance — tests don't want a live world. */
	UOriginRebasingSubsystem* MakeSubsystem()
	{
		return NewObject<UOriginRebasingSubsystem>(GetTransientPackage());
	}

	/** Spawn a plain AActor in the transient package and place it at Loc (cm). */
	AActor* MakeActorAt(const FVector& LocCm)
	{
		AActor* Actor = NewObject<AActor>(GetTransientPackage());
		// The default AActor has no root component; give it one so
		// SetActorLocation lands on a real transform rather than silently
		// dropping. Must be a USceneComponent (or subclass).
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Actor->SetActorLocation(LocCm);
		return Actor;
	}
}

// =============================================================================
// AC#1 — actor above threshold triggers rebase with correct offset;
//        actor below threshold is a no-op.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOriginRebasingThresholdDetectionTest,
	"DeltaV.Physics.OriginRebasing.ThresholdDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOriginRebasingThresholdDetectionTest::RunTest(const FString& Parameters)
{
	UOriginRebasingSubsystem* Subsystem = MakeSubsystem();
	TestEqual(TEXT("Initial RebaseCount == 0"), Subsystem->RebaseCount, 0);

	// Actor at 5 km — well under the 10 km default threshold.
	AActor* Actor = MakeActorAt(FVector(5000.0 * 100.0, 0.0, 0.0));
	Subsystem->SetTrackedActor(Actor);

	TestFalse(TEXT("Below threshold: EvaluateAndRebase returns false"),
		Subsystem->EvaluateAndRebase());
	TestEqual(TEXT("Below threshold: counter unchanged"), Subsystem->RebaseCount, 0);
	TestTrue(TEXT("Below threshold: LastAppliedOffsetMeters still zero"),
		Subsystem->LastAppliedOffsetMeters.IsNearlyZero());

	// Move the actor past the threshold (15 km).
	Actor->SetActorLocation(FVector(15000.0 * 100.0, 0.0, 0.0));

	TestTrue(TEXT("Above threshold: EvaluateAndRebase returns true"),
		Subsystem->EvaluateAndRebase());
	TestEqual(TEXT("After rebase: counter advanced"), Subsystem->RebaseCount, 1);

	// Offset in metres should match the actor's pre-rebase location. The
	// cm → FIntVector round-trip introduces at most 0.5 cm = 0.005 m of
	// rounding noise; compare with 1 cm tolerance.
	const FVector Expected(15000.0, 0.0, 0.0);
	const double OffsetErrorM = (Subsystem->LastAppliedOffsetMeters - Expected).Length();
	TestTrue(
		FString::Printf(TEXT("LastAppliedOffsetMeters ≈ (15000, 0, 0) (|Δ|=%.3e m < 0.01 m)"),
			OffsetErrorM),
		OffsetErrorM < 0.01);

	return true;
}

// =============================================================================
// AC#3 — OnOriginRebased delegate fires with correct offset (metres).
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOriginRebasingDelegateTest,
	"DeltaV.Physics.OriginRebasing.DelegateFiresWithCorrectOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOriginRebasingDelegateTest::RunTest(const FString& Parameters)
{
	UOriginRebasingSubsystem* Subsystem = MakeSubsystem();

	UOriginRebasingTestListener* Listener = NewObject<UOriginRebasingTestListener>();
	Subsystem->OnOriginRebased.AddDynamic(
		Listener, &UOriginRebasingTestListener::HandleRebased);

	// Actor at (12 km, 3 km, 0) — triggers threshold and has a non-trivial
	// direction so a sign error would be obvious.
	AActor* Actor = MakeActorAt(FVector(12000.0 * 100.0, 3000.0 * 100.0, 0.0));
	Subsystem->SetTrackedActor(Actor);

	TestTrue(TEXT("Above-threshold rebase succeeds"),
		Subsystem->EvaluateAndRebase());
	TestEqual(TEXT("Listener received exactly one broadcast"),
		Listener->BroadcastCount, 1);

	const FVector Expected(12000.0, 3000.0, 0.0);
	const double OffsetErrorM = (Listener->LastOffset - Expected).Length();
	TestTrue(
		FString::Printf(TEXT("Listener offset matches expected (|Δ|=%.3e m < 0.01 m)"),
			OffsetErrorM),
		OffsetErrorM < 0.01);

	return true;
}

// =============================================================================
// Guard — no tracked actor means EvaluateAndRebase is a no-op.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOriginRebasingNoTrackedActorTest,
	"DeltaV.Physics.OriginRebasing.NoTrackedActorIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOriginRebasingNoTrackedActorTest::RunTest(const FString& Parameters)
{
	UOriginRebasingSubsystem* Subsystem = MakeSubsystem();
	UOriginRebasingTestListener* Listener = NewObject<UOriginRebasingTestListener>();
	Subsystem->OnOriginRebased.AddDynamic(
		Listener, &UOriginRebasingTestListener::HandleRebased);

	TestFalse(TEXT("No tracked actor: EvaluateAndRebase returns false"),
		Subsystem->EvaluateAndRebase());
	TestEqual(TEXT("No tracked actor: counter stays zero"),
		Subsystem->RebaseCount, 0);
	TestEqual(TEXT("No tracked actor: delegate never fired"),
		Listener->BroadcastCount, 0);

	return true;
}

// =============================================================================
// AC#4 — rebasing while a burn is active logs a warning exactly once.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOriginRebasingBurnActiveWarnsTest,
	"DeltaV.Physics.OriginRebasing.BurnActiveLogsWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FOriginRebasingBurnActiveWarnsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("rebasing during active burn may affect physics"),
		EAutomationExpectedErrorFlags::Contains, 1);

	UOriginRebasingSubsystem* Subsystem = MakeSubsystem();
	AActor* Actor = MakeActorAt(FVector(15000.0 * 100.0, 0.0, 0.0));
	Subsystem->SetTrackedActor(Actor);
	Subsystem->SetBurnActive(true);

	TestTrue(TEXT("Burn-active rebase still fires"),
		Subsystem->EvaluateAndRebase());

	// A second rebase with the flag still set must NOT re-log (one-shot
	// throttle). Move the actor again to trigger another rebase.
	Actor->SetActorLocation(FVector(15000.0 * 100.0, 0.0, 0.0));
	TestTrue(TEXT("Second rebase also fires"),
		Subsystem->EvaluateAndRebase());

	TestEqual(TEXT("Counter is 2 after two rebases"), Subsystem->RebaseCount, 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
