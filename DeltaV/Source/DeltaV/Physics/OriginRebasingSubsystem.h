// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OriginRebasingSubsystem.generated.h"

/**
 * Broadcast when the world origin is rebased. AppliedOffsetMeters is the
 * vector the tracked actor was at (in meters, in the pre-rebase local
 * coordinate system) — after the rebase, every actor's local position has
 * shifted by -AppliedOffsetMeters so the tracked actor sits near (0,0,0).
 *
 * HUD / logging / camera code can subscribe to keep UI coordinates stable.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnOriginRebased, FVector, AppliedOffsetMeters);

/**
 * Per-world subsystem that shifts the world origin via UWorld::SetNewWorldOrigin
 * whenever the active tracked actor drifts more than RebaseThresholdMeters
 * from the current origin. Prevents f32 Chaos-solver jitter at orbital scales
 * (100 km+ above Earth's surface).
 *
 * PRD: US-023 (EP-004 Dual-Tier Physics Coupling).
 *
 * Tick path
 * ---------
 * Inherits from UTickableWorldSubsystem, so the engine ticks this object
 * automatically once the world is initialised. Tick gates on
 * TrackedActor validity and delegates all the work to EvaluateAndRebase.
 *
 * Origin arithmetic
 * -----------------
 * UWorld::SetNewWorldOrigin takes an FIntVector in centimetres representing
 * the ABSOLUTE new origin in the engine's global frame, not a relative
 * delta. We compute it as (World->OriginLocation + FIntVector(ActorLocCm));
 * after the call the tracked actor sits near (0,0,0) in the new local
 * frame. A cm-to-m conversion on the offset is broadcast via OnOriginRebased
 * for HUD / camera consumers that care about the absolute displacement.
 *
 * Test seam
 * ---------
 * EvaluateAndRebase is exposed publicly so automation tests can drive the
 * detection / delegate / telemetry path without a registered UWorld. The
 * actual UWorld::SetNewWorldOrigin call is guarded by GetWorld() — when
 * the subsystem is spawned via NewObject<T>(GetTransientPackage()) in a
 * transient-package test, the pure logic runs but the world-level shift
 * is skipped (there is nothing to shift).
 *
 * Unhappy path (AC#4)
 * -------------------
 * If an external system (ULaunchSequenceComponent in US-038) calls
 * SetBurnActive(true), a subsequent rebase emits a one-time warning.
 * Rebasing during a burn is not fatal — UE cascades ApplyWorldOffset
 * through the Chaos body — but the warning is a red flag for QA.
 */
UCLASS()
class DELTAV_API UOriginRebasingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UTickableWorldSubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return TrackedActor.IsValid(); }
	virtual bool IsTickableInEditor() const override { return false; }

	/**
	 * Distance (metres) from the current world origin at which a rebase
	 * triggers. Default 10 km per PRD; clamp prevents misconfiguration to
	 * thrash territory (< 100 m would rebase every few ticks under any
	 * motion).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|OriginRebasing",
		meta = (ClampMin = "100.0", UIMin = "100.0"))
	double RebaseThresholdMeters = 10000.0;

	/**
	 * Actor whose distance from the origin drives the rebase decision.
	 * Typically the currently possessed pawn (set by UPossessionManager
	 * when it lands in US-029). Weak ref — a destroyed tracked actor
	 * simply stops ticking the subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|OriginRebasing")
	void SetTrackedActor(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Physics|OriginRebasing")
	AActor* GetTrackedActor() const { return TrackedActor.Get(); }

	/**
	 * Signal that a burn is active. If a rebase triggers while this is true,
	 * a throttled warning is emitted. Callers (US-038 launch sequence) flip
	 * this around the ignition → MECO window.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|OriginRebasing")
	void SetBurnActive(bool bActive);

	UFUNCTION(BlueprintCallable, Category = "Physics|OriginRebasing")
	bool IsBurnActive() const { return bBurnActive; }

	/**
	 * Run one detection cycle. Exposed so automation tests can drive it
	 * without relying on the engine tick. Returns true if a rebase was
	 * performed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|OriginRebasing")
	bool EvaluateAndRebase();

	/** Broadcast on every successful rebase. Offset is in metres. */
	UPROPERTY(BlueprintAssignable, Category = "Physics|OriginRebasing")
	FOnOriginRebased OnOriginRebased;

	/** Count of successful rebases since subsystem init. */
	UPROPERTY(BlueprintReadOnly, Category = "Physics|OriginRebasing")
	int32 RebaseCount = 0;

	/**
	 * Offset applied on the last rebase, in metres. Useful for the debug
	 * HUD (AC#3) and for post-hoc diagnostics. Zero before the first rebase.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Physics|OriginRebasing")
	FVector LastAppliedOffsetMeters = FVector::ZeroVector;

private:
	/** Tracked actor (weak). Null = no tick, no rebase decisions. */
	TWeakObjectPtr<AActor> TrackedActor;

	/** External burn-active signal (set by US-038 launch sequence). */
	bool bBurnActive = false;

	/** Throttle so a burn-active rebase only logs once per lifetime. */
	bool bHasLoggedBurnWarning = false;

	/**
	 * Mirrors UWorld::OriginLocation when we have a world. In transient-
	 * package unit tests we have no world; this field lets the threshold
	 * math produce a deterministic result regardless.
	 */
	FIntVector MirroredOriginOffsetCm = FIntVector::ZeroValue;
};
