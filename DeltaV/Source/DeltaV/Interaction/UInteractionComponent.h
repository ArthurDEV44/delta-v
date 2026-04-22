// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UInteractionComponent.generated.h"

class AActor;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractableChanged, AActor*, NewInteractable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractAttemptedWhileDisabled, AActor*, Target);

/**
 * US-025 — scans the world ahead of the owner every tick, exposes the best
 * IInteractable candidate + formatted prompt text for UI, and dispatches
 * OnInteract when TryInteract is called (from US-027 input binding).
 *
 * Selection is a two-step filter:
 *   1. Candidates within MaxDistanceCm of the probe origin, whose direction-
 *      from-origin vector lies inside the acceptance cone (MinAlignmentCosine).
 *   2. Highest alignment dot wins; ties broken by shortest distance.
 *
 * PickBestCandidate is static + pure so tests can exercise the selection rule
 * without setting up collision in a world.
 */
UCLASS(ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent))
class DELTAV_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Config ---------------------------------------------------------

	/** Max trace distance in centimeters. Default 200 cm = 2 m (AC#2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxDistanceCm = 200.0;

	/**
	 * Acceptance cone half-angle as the minimum cosine of the angle between
	 * owner-forward and direction-to-candidate. 0.5 ≈ 60° half-cone — matches
	 * a generous aim-assist window for a TPV interaction prompt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	double MinAlignmentCosine = 0.5;

	/** Zero means "scan every tick"; seconds > 0 throttles to that interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScanIntervalSeconds = 0.0f;

	// ---- Public API -----------------------------------------------------

	/**
	 * Collect all IInteractable actors in the world and pick the best one.
	 * Updates CurrentInteractable, broadcasts OnInteractableChanged when the
	 * selection actually changes, returns the winner (or nullptr).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	AActor* ScanForInteractable();

	/**
	 * Attempt an interaction with CurrentInteractable. Returns:
	 *   - true  → enabled target, OnInteract(PC) dispatched.
	 *   - false → no target, or target disabled (delegate fires instead).
	 * Callable from an Enhanced Input handler once US-027 lands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool TryInteract(APlayerController* PlayerController);

	/** Current best candidate (may be null). */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetCurrentInteractable() const { return CurrentInteractable.Get(); }

	/**
	 * Prompt payload for UI — empty if no candidate, "E — <Label>" when enabled,
	 * "E — <Label> (offline)" when the candidate's IsInteractionEnabled is false.
	 */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	FText GetCurrentPrompt() const;

	/** True when CurrentInteractable exists AND is enabled. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool IsCurrentEnabled() const;

	/**
	 * Direct setter used by tests (and future possession-switch code that
	 * wants to force a target for a frame). Triggers the changed delegate
	 * when the selection differs from the previous one.
	 */
	void SetCurrentInteractable(AActor* Target);

	// ---- Pure selection (unit-testable, world-independent) -------------

	/**
	 * Pick the best interactable from a pre-gathered candidate list using the
	 * same rule the runtime scan applies. Candidates that don't implement
	 * UInteractable, lie beyond MaxDistanceCm, or fall outside the acceptance
	 * cone are discarded silently.
	 */
	static AActor* PickBestCandidate(
		const FVector& OriginCm,
		const FVector& ForwardUnit,
		double MaxDistanceCm,
		double MinAlignmentCosine,
		const TArray<AActor*>& Candidates);

	// ---- Delegates ------------------------------------------------------

	/** Fires when the selected interactable transitions to a different actor (or null). */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractableChanged OnInteractableChanged;

	/** Fires when TryInteract was called but the target was disabled — UI/audio cue source. */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractAttemptedWhileDisabled OnInteractAttemptedWhileDisabled;

private:
	/** Weak ref — cleared automatically if the target is destroyed. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CurrentInteractable;

	/** Time accumulator for ScanIntervalSeconds throttling. */
	float TimeSinceLastScan = 0.0f;

	/**
	 * Compute the probe origin + forward unit for the owning actor. Prefers the
	 * first UCameraComponent on the owner (so the TPV spring-arm camera is the
	 * natural probe frame) and falls back to the owner's actor transform.
	 */
	void GetProbeFrame(FVector& OutOriginCm, FVector& OutForwardUnit) const;
};
