// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Interaction/IInteractable.h"
#include "UObject/Object.h"
#include "InteractionTestSpy.generated.h"

class APlayerController;

/**
 * US-025 test fixture — an Actor that implements IInteractable and records
 * every OnInteract call so tests can assert dispatch. Always compiled for the
 * editor target (UHT forbids UCLASS inside arbitrary preprocessor blocks);
 * the handful of extra bytes of metadata in shipping builds is negligible.
 */
UCLASS(NotBlueprintable, Transient)
class AInteractionTestSpy : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	FText PromptLabel;

	UPROPERTY(Transient)
	bool bEnabled = true;

	UPROPERTY(Transient)
	int32 InteractCallCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> LastPC;

	AInteractionTestSpy()
	{
		PromptLabel = FText::FromString(TEXT("Open Console"));
		// A root scene component is required for SpawnActor location to stick,
		// and the picker reads GetActorLocation() — without a root, every spy
		// would silently collapse to the world origin.
		USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpyRoot"));
		RootComponent = SceneRoot;
	}

	virtual FText GetInteractionText() const override { return PromptLabel; }
	virtual bool IsInteractionEnabled() const override { return bEnabled; }
	virtual void OnInteract(APlayerController* PC) override
	{
		++InteractCallCount;
		LastPC = PC;
	}
};

/**
 * Event-counter helper for UInteractionComponent's dynamic delegates. Dynamic
 * multicast delegates only accept UFUNCTION-tagged methods on UObject (not
 * lambdas), so tests route the broadcast through this tiny UObject.
 */
UCLASS()
class UInteractionTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleInteractableChanged(AActor* NewInteractable)
	{
		++ChangedCount;
		LastInteractable = NewInteractable;
	}

	UFUNCTION()
	void HandleAttemptedWhileDisabled(AActor* Target)
	{
		++DisabledAttemptCount;
		LastDisabledTarget = Target;
	}

	int32 ChangedCount = 0;
	int32 DisabledAttemptCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<AActor> LastInteractable;

	UPROPERTY(Transient)
	TObjectPtr<AActor> LastDisabledTarget;
};
