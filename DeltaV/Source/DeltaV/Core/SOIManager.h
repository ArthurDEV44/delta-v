// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "SOIManager.generated.h"

class UCelestialBody;

/** Broadcast when a vessel enters a new SOI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSOITransitionEnter, FGuid, VesselKey, UCelestialBody*, NewSOI);

/** Broadcast when a vessel is outside every registered SOI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnSOIOrphan, FGuid, VesselKey);

/** Per-vessel tracking state — not exposed to Blueprint. */
struct FSOIAssignmentState
{
	TWeakObjectPtr<UCelestialBody> CurrentSOI;
	/** Last time a transition was LOGGED for this vessel (not gated). */
	double LastTransitionLogTime = -TNumericLimits<double>::Max();
	/** True once the first classification has been emitted. */
	bool bEverClassified = false;
};

/**
 * SOI registry + classifier for all vessels.
 *
 * PRD: US-010. Pure data — no physics. Stateful per-vessel tracking supports
 * hysteresis (default 0.5 %) and a cooldown window (default 60 s) to suppress
 * chattering at SOI boundaries.
 *
 * Test harness: see DeltaV.Orbital.SOIManager.*
 */
UCLASS()
class DELTAV_API USOIManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Add a body to the registry. No-op if already registered or null. */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	void RegisterBody(UCelestialBody* Body);

	/** Remove a body from the registry. No-op if not present. */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	void UnregisterBody(UCelestialBody* Body);

	/** Snapshot of currently-registered bodies (stale weak-ptrs filtered). */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	TArray<UCelestialBody*> GetAllBodies() const;

	/**
	 * Stateless query: which body's sphere currently contains Pos (deepest wins)?
	 * Does NOT apply hysteresis and does NOT fire events. Returns nullptr if
	 * Pos is outside every sphere.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	UCelestialBody* QueryCurrentSOI(const FVector& Pos) const;

	/**
	 * Stateful update: reclassify a vessel with hysteresis, fire events on
	 * transition. Returns the vessel's SOI after the update (may be nullptr
	 * on orphan).
	 *
	 * Caller lifecycle contract: the manager stores one entry per VesselKey
	 * indefinitely; callers MUST invoke ForgetVessel() when a vessel is
	 * destroyed to avoid unbounded memory growth.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	UCelestialBody* UpdateVessel(
		const FGuid& VesselKey,
		const FVector& Pos,
		double WorldTimeSeconds);

	/** Read the currently-assigned SOI for a vessel, or nullptr if unknown. */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	UCelestialBody* GetAssignedSOI(const FGuid& VesselKey) const;

	/** Drop all tracking state for a vessel. Call on vessel destruction. */
	UFUNCTION(BlueprintCallable, Category = "Orbital|SOI")
	void ForgetVessel(const FGuid& VesselKey);

	/**
	 * Hysteresis band as a fraction of SOI radius (default 0.005 = 0.5%).
	 * This is the sole transition gate: once set properly, chattering is
	 * impossible at the physics level, not just suppressed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|SOI")
	double HysteresisFraction = 0.005;

	/**
	 * Minimum seconds between log lines for a given vessel's transitions.
	 * Transitions still fire (events always broadcast); logging is throttled
	 * to avoid spam if hysteresis is misconfigured by data.
	 * Per PRD: "max 1 transition logged per 60s" under the hysteresis flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|SOI")
	double TransitionLogCooldownSeconds = 60.0;

	/** Fired on transition into a new SOI (after hysteresis + cooldown gates). */
	UPROPERTY(BlueprintAssignable, Category = "Orbital|SOI")
	FOnSOITransitionEnter OnSOITransitionEnter;

	/** Fired when a vessel has no containing SOI. */
	UPROPERTY(BlueprintAssignable, Category = "Orbital|SOI")
	FOnSOIOrphan OnSOIOrphan;

private:
	/** Registered bodies. Strong refs — subsystem owns a slot, not the body. */
	UPROPERTY()
	TArray<TObjectPtr<UCelestialBody>> Bodies;

	/** Per-vessel classification state. */
	TMap<FGuid, FSOIAssignmentState> VesselAssignments;

	/**
	 * Hysteresis-aware candidate lookup: for each body, the "stay" radius is
	 * R*(1+h) when that body is the vessel's current assignment, else R*(1-h).
	 * The narrowest body whose adjusted sphere contains Pos wins.
	 */
	UCelestialBody* QueryHysteresisAwareSOI(
		const FVector& Pos,
		UCelestialBody* CurrentAssignment) const;
};
