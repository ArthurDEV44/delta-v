// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AVehicle.generated.h"

class UCanvas;
class UVehiclePartComponent;
class FDebugDisplayInfo;

/**
 * Broadcast when the aggregated inertial properties of a vehicle have changed
 * by at least 10 % of the last broadcast mass. Not fired for sub-threshold
 * changes; callers who want every tick should poll directly.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnInertialPropertiesChanged,
	double, NewTotalMass,
	FVector, NewCenterOfMass,
	FMatrix, NewMomentOfInertia);

/**
 * Base class for every physical vehicle in DeltaV. Aggregates inertial
 * properties (total mass, center-of-mass, moment of inertia tensor) from
 * attached UVehiclePartComponent children.
 *
 * PRD: US-013.
 *
 * Aggregation invariants
 * ----------------------
 *  - TotalMass       : sum of part masses, kilograms (double).
 *  - CenterOfMass    : mass-weighted centroid of part CoMs in the vehicle's
 *                      actor-local frame, centimeters (UE native).
 *  - MomentOfInertia : 3×3 symmetric tensor in kg·m², expressed about
 *                      CenterOfMass in the vehicle's local frame. Built by
 *                      parallel-axis theorem from each part's diagonal tensor.
 *
 * Recomputation is dirty-flag gated: RegisterPart, UnregisterPart, and each
 * UVehiclePartComponent setter mark the owning vehicle dirty. RecomputeInertialProperties()
 * early-outs O(1) when clean, so 1000 idle vehicles cost O(1000) boolean
 * reads per frame and only pay the aggregation cost on actual change.
 *
 * Empty-vehicle degenerate case (no parts): TotalMass = 0, CenterOfMass =
 * origin, MomentOfInertia = identity (so downstream consumers can multiply
 * safely). A single log warning is emitted the first time this is observed.
 */
UCLASS()
class DELTAV_API AVehicle : public AActor
{
	GENERATED_BODY()

public:
	AVehicle();

	/** Total aggregated mass (kg). Updated by RecomputeInertialProperties. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Inertial")
	double TotalMass = 0.0;

	/**
	 * Mass-weighted center of mass in the vehicle's local frame (cm, UE native).
	 * Zero when the vehicle has no parts.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Inertial")
	FVector CenterOfMass = FVector::ZeroVector;

	/**
	 * Moment of inertia tensor about CenterOfMass, in kg·m² (SI), expressed
	 * in the vehicle's local frame. Identity when the vehicle has no parts,
	 * as a safe default for downstream physics math.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Inertial")
	FMatrix MomentOfInertia = FMatrix::Identity;

	/**
	 * Broadcast when |TotalMass - LastBroadcastMass| has crossed
	 * (MassChangeBroadcastThreshold * max(|LastBroadcastMass|, 1 kg)).
	 * Always also fired on the first successful recompute with non-zero mass.
	 *
	 * Reentrancy contract: listeners MAY call MarkInertialPropertiesDirty,
	 * RegisterPart, UnregisterPart, or any UVehiclePartComponent setter inside
	 * the handler. The vehicle re-flags itself dirty and picks up the change on
	 * the next RecomputeInertialProperties call. Listeners MUST NOT call
	 * RecomputeInertialProperties synchronously (would re-broadcast and risks
	 * repeated observer invocation this frame).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Vehicles|Inertial")
	FOnInertialPropertiesChanged OnInertialPropertiesChanged;

	/**
	 * Fractional mass delta that triggers OnInertialPropertiesChanged.
	 * Default 0.10 (10 %) per US-013 acceptance criterion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicles|Inertial",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MassChangeBroadcastThreshold = 0.10;

	/** Mark aggregated inertial state as stale. O(1). */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Inertial")
	void MarkInertialPropertiesDirty();

	/** Is the aggregated state currently stale? */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Inertial")
	bool IsInertialPropertiesDirty() const { return bInertialPropertiesDirty; }

	/**
	 * Recompute aggregated inertial properties from the current set of
	 * registered parts. Early-outs in O(1) when the dirty flag is clear
	 * unless bForce = true. Always consumes the dirty flag on entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Inertial")
	void RecomputeInertialProperties(bool bForce = false);

	/**
	 * Register a part with this vehicle. Called from UVehiclePartComponent::OnRegister.
	 * Idempotent; no-op on duplicate registration.
	 */
	void RegisterPart(UVehiclePartComponent* Part);

	/** Unregister a part. Called from UVehiclePartComponent::OnUnregister. */
	void UnregisterPart(UVehiclePartComponent* Part);

	/** Read-only view of the currently registered parts (stale entries filtered). */
	TArray<UVehiclePartComponent*> GetRegisteredParts() const;

	/**
	 * Human-readable summary of the current aggregated state with 3-decimal
	 * precision for mass / CoM / MoI components. Used by DisplayDebug and
	 * by automation tests to verify AC#1 without spinning up a canvas.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Inertial")
	FString GetDebugInfoString() const;

	// AActor — invoked via "show debug Vehicle" when this actor is the
	// currently displayed-debug target (e.g., the possessed pawn).
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Core aggregation kernel. Assumes RegisteredParts is already filtered of
	 * stale weak refs. Writes TotalMass / CenterOfMass / MomentOfInertia.
	 * Emits the empty-vehicle warning at most once per actor lifetime.
	 */
	void DoRecompute();

	/** Weak refs to registered parts so a GC'd component doesn't dangle. */
	TArray<TWeakObjectPtr<UVehiclePartComponent>> RegisteredParts;

	/** Mass at the last OnInertialPropertiesChanged broadcast. 0 = never broadcast. */
	double LastBroadcastMass = 0.0;

	/** Cached stale flag. true = TotalMass / CenterOfMass / MomentOfInertia must not be trusted. */
	bool bInertialPropertiesDirty = true;

	/** Guard so the empty-vehicle warning logs once per actor lifetime. */
	bool bHasLoggedEmptyWarning = false;
};
