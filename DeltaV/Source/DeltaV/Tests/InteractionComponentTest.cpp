// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/IInteractable.h"
#include "Interaction/UInteractionComponent.h"
#include "Tests/InteractionTestSpy.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UWorld* MakeInteractionTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, /*bInformEngineOfWorld=*/ false,
			FName(TEXT("US025InteractionTestWorld")));
		if (World == nullptr)
		{
			return nullptr;
		}
		FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.SetCurrentWorld(World);
		return World;
	}

	void DestroyInteractionTestWorld(UWorld* World)
	{
		if (World == nullptr || GEngine == nullptr)
		{
			return;
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AInteractionTestSpy* SpawnSpy(UWorld* World, const FVector& Location,
		const FString& Label = TEXT("Open Console"), bool bEnabled = true)
	{
		AInteractionTestSpy* Spy = World->SpawnActor<AInteractionTestSpy>(
			AInteractionTestSpy::StaticClass(), Location, FRotator::ZeroRotator);
		if (Spy != nullptr)
		{
			Spy->PromptLabel = FText::FromString(Label);
			Spy->bEnabled = bEnabled;
		}
		return Spy;
	}

	UInteractionComponent* SpawnHostWithComponent(UWorld* World)
	{
		AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Host == nullptr)
		{
			return nullptr;
		}
		UInteractionComponent* Comp = NewObject<UInteractionComponent>(Host);
		Comp->RegisterComponent();
		return Comp;
	}
}

// =============================================================================
// AC#1 — Actor implementing IInteractable is detectable by the selection logic.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInteractableDetectedByPickerTest,
	"DeltaV.Interaction.IInteractable.DetectedByPicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FInteractableDetectedByPickerTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeInteractionTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	AInteractionTestSpy* Spy = SpawnSpy(World, FVector(150.0, 0.0, 0.0));  // 1.5 m ahead
	if (!TestNotNull(TEXT("Spy spawned"), Spy))
	{
		DestroyInteractionTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Spy class implements UInteractable"),
		Spy->GetClass()->ImplementsInterface(UInteractable::StaticClass()));

	const TArray<AActor*> Candidates = { Spy };
	AActor* Winner = UInteractionComponent::PickBestCandidate(
		FVector::ZeroVector, FVector::ForwardVector,
		/*MaxDistanceCm=*/ 200.0, /*MinCos=*/ 0.5, Candidates);
	TestEqual<AActor*>(TEXT("Picker returns the spy at 1.5 m forward"), Winner, Spy);

	DestroyInteractionTestWorld(World);
	return true;
}

// =============================================================================
// AC#2 — Prompt text reflects the target's label and enabled state.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInteractionPromptReflectsTargetTest,
	"DeltaV.Interaction.InteractionComponent.PromptReflectsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FInteractionPromptReflectsTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeInteractionTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	UInteractionComponent* Comp = SpawnHostWithComponent(World);
	AInteractionTestSpy* Spy = SpawnSpy(World, FVector(100.0, 0.0, 0.0),
		TEXT("Open Console"), /*bEnabled=*/ true);
	if (Comp == nullptr || Spy == nullptr)
	{
		DestroyInteractionTestWorld(World);
		return false;
	}

	TestTrue(TEXT("No prompt when no target"), Comp->GetCurrentPrompt().IsEmpty());

	Comp->SetCurrentInteractable(Spy);
	TestEqual(TEXT("Enabled prompt is 'E — Open Console'"),
		Comp->GetCurrentPrompt().ToString(), FString(TEXT("E — Open Console")));
	TestTrue(TEXT("IsCurrentEnabled true for enabled spy"), Comp->IsCurrentEnabled());

	DestroyInteractionTestWorld(World);
	return true;
}

// =============================================================================
// AC#3 — Press E (TryInteract) dispatches OnInteract with the PlayerController.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInteractionTryInteractDispatchesTest,
	"DeltaV.Interaction.InteractionComponent.TryInteractDispatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FInteractionTryInteractDispatchesTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeInteractionTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	UInteractionComponent* Comp = SpawnHostWithComponent(World);
	AInteractionTestSpy* Spy = SpawnSpy(World, FVector(100.0, 0.0, 0.0));
	if (Comp == nullptr || Spy == nullptr)
	{
		DestroyInteractionTestWorld(World);
		return false;
	}

	Comp->SetCurrentInteractable(Spy);

	// Nullable PC — the interface takes it as a pass-through; null is a valid
	// test value since our spy only records what it received.
	TestTrue(TEXT("TryInteract succeeds on enabled target"), Comp->TryInteract(nullptr));
	TestEqual<int32>(TEXT("OnInteract called exactly once"), Spy->InteractCallCount, 1);

	// A second press should also succeed — the interface doesn't de-bounce.
	TestTrue(TEXT("Second TryInteract succeeds"), Comp->TryInteract(nullptr));
	TestEqual<int32>(TEXT("OnInteract called twice total"), Spy->InteractCallCount, 2);

	// No target → TryInteract returns false, no dispatch.
	Comp->SetCurrentInteractable(nullptr);
	TestFalse(TEXT("TryInteract with no target returns false"), Comp->TryInteract(nullptr));
	TestEqual<int32>(TEXT("Count unchanged when no target"), Spy->InteractCallCount, 2);

	DestroyInteractionTestWorld(World);
	return true;
}

// =============================================================================
// AC#4 — Multiple candidates: closest-aligned-with-forward wins.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInteractionClosestAlignedWinsTest,
	"DeltaV.Interaction.InteractionComponent.ClosestAlignedWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FInteractionClosestAlignedWinsTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeInteractionTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	// Three candidates:
	//  A — dead ahead, 150 cm          (alignment 1.0, distance 150)   ← should win
	//  B — 30° off axis,   100 cm      (alignment ~0.866, distance 100)
	//  C — dead ahead, 180 cm          (alignment 1.0, distance 180)   — same alignment, farther
	AInteractionTestSpy* A = SpawnSpy(World, FVector(150.0, 0.0, 0.0), TEXT("A"));
	AInteractionTestSpy* B = SpawnSpy(World, FVector(FMath::Cos(FMath::DegreesToRadians(30.0)) * 100.0,
		FMath::Sin(FMath::DegreesToRadians(30.0)) * 100.0, 0.0), TEXT("B"));
	AInteractionTestSpy* C = SpawnSpy(World, FVector(180.0, 0.0, 0.0), TEXT("C"));

	if (A == nullptr || B == nullptr || C == nullptr)
	{
		DestroyInteractionTestWorld(World);
		return false;
	}

	const TArray<AActor*> Candidates = { B, C, A };  // intentionally out of order
	AActor* Winner = UInteractionComponent::PickBestCandidate(
		FVector::ZeroVector, FVector::ForwardVector,
		/*MaxDistanceCm=*/ 200.0, /*MinCos=*/ 0.5, Candidates);

	TestEqual<AActor*>(TEXT("A wins (best alignment, shorter than C)"), Winner, static_cast<AActor*>(A));

	// Out-of-cone candidate must be ignored even if closer: move B to 90° off-axis
	// which is below the 0.5 cosine threshold (60° half-cone).
	B->SetActorLocation(FVector(0.0, 100.0, 0.0));
	const TArray<AActor*> Candidates2 = { B, A };
	AActor* Winner2 = UInteractionComponent::PickBestCandidate(
		FVector::ZeroVector, FVector::ForwardVector, 200.0, 0.5, Candidates2);
	TestEqual<AActor*>(TEXT("B out of cone is rejected; A wins"), Winner2, static_cast<AActor*>(A));

	// Out-of-range candidate is ignored.
	A->SetActorLocation(FVector(300.0, 0.0, 0.0));  // beyond 200 cm
	const TArray<AActor*> Candidates3 = { A };
	AActor* Winner3 = UInteractionComponent::PickBestCandidate(
		FVector::ZeroVector, FVector::ForwardVector, 200.0, 0.5, Candidates3);
	TestNull(TEXT("Out-of-range candidate produces no winner"), Winner3);

	DestroyInteractionTestWorld(World);
	return true;
}

// =============================================================================
// AC#5 Unhappy path — disabled interactable: prompt greyed, no dispatch, delegate fires.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInteractionDisabledGatingTest,
	"DeltaV.Interaction.InteractionComponent.DisabledGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FInteractionDisabledGatingTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeInteractionTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	UInteractionComponent* Comp = SpawnHostWithComponent(World);
	AInteractionTestSpy* Spy = SpawnSpy(World, FVector(100.0, 0.0, 0.0),
		TEXT("Open Console"), /*bEnabled=*/ false);
	if (Comp == nullptr || Spy == nullptr)
	{
		DestroyInteractionTestWorld(World);
		return false;
	}

	Comp->SetCurrentInteractable(Spy);
	TestFalse(TEXT("IsCurrentEnabled false for disabled spy"), Comp->IsCurrentEnabled());
	TestEqual(TEXT("Disabled prompt is 'E — Open Console (offline)'"),
		Comp->GetCurrentPrompt().ToString(),
		FString(TEXT("E — Open Console (offline)")));

	UInteractionTestListener* Listener = NewObject<UInteractionTestListener>();
	Comp->OnInteractAttemptedWhileDisabled.AddDynamic(
		Listener, &UInteractionTestListener::HandleAttemptedWhileDisabled);

	TestFalse(TEXT("TryInteract on disabled target returns false"), Comp->TryInteract(nullptr));
	TestEqual<int32>(TEXT("OnInteract NOT called on disabled target"), Spy->InteractCallCount, 0);
	TestEqual<int32>(TEXT("OnInteractAttemptedWhileDisabled fired once"),
		Listener->DisabledAttemptCount, 1);
	TestEqual<AActor*>(TEXT("Delegate carries the disabled target"),
		Listener->LastDisabledTarget.Get(), static_cast<AActor*>(Spy));

	DestroyInteractionTestWorld(World);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
