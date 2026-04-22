// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UVehiclePartComponent.generated.h"

class AVehicle;

/**
 * A physical contribution to an AVehicle's inertial properties.
 *
 * PRD: US-013. Each part carries its mass (kg), local center-of-mass offset
 * (cm, relative to the part's scene-component origin, UE native units), and a
 * principal-axis inertia diagonal (kg·m²). The owning AVehicle aggregates these
 * via parallel-axis theorem, recomputing lazily on a dirty flag.
 *
 * Setters MUST be used at runtime — direct field mutation will not invalidate
 * the cached aggregate state on the owning vehicle.
 */
UCLASS(ClassGroup = (Vehicles), meta = (BlueprintSpawnableComponent))
class DELTAV_API UVehiclePartComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVehiclePartComponent();

	/** Part mass in kilograms. Must be finite and >= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicles|Part",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double Mass = 0.0;

	/**
	 * Local center of mass offset (centimeters, UE native) from this component's
	 * transform origin. The part's world CoM contribution is
	 *   GetRelativeLocation() + LocalCenterOfMass
	 * taken in the owning vehicle's actor-local frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicles|Part")
	FVector LocalCenterOfMass = FVector::ZeroVector;

	/**
	 * Principal-axis inertia diagonal in kg·m², expressed about the part's
	 * own center of mass in the part's local frame. Entries MUST be >= 0.
	 *
	 * The aggregation on AVehicle applies the parallel-axis theorem to shift
	 * each part's tensor to the composite center of mass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicles|Part")
	FVector LocalInertiaDiagonal = FVector::ZeroVector;

	/**
	 * Set the part's mass and notify the owning vehicle. Rejects non-finite
	 * values with a log warning (leaves the previous value intact).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Part")
	void SetMass(double NewMass);

	/** Set the local CoM offset (cm) and notify the owning vehicle. */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Part")
	void SetLocalCenterOfMass(const FVector& NewLocalCoM);

	/**
	 * Set the principal-axis inertia diagonal (kg·m²) and notify the owning
	 * vehicle. Negative or non-finite components are rejected with a log warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Part")
	void SetLocalInertiaDiagonal(const FVector& NewLocalInertiaDiagonal);

	// UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	/**
	 * Mark the owning AVehicle's aggregate inertial state dirty. No-op if the
	 * owner is not an AVehicle (e.g., attached to a non-vehicle actor in editor).
	 */
	void NotifyOwnerDirty() const;
};
