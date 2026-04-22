// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VehicleInertialTestListener.generated.h"

/**
 * Event-counter helper for AVehicle automation tests. Subscribes to the
 * dynamic multicast OnInertialPropertiesChanged via AddDynamic (which requires
 * a UFUNCTION on a UObject — hence this tiny class).
 *
 * Always compiled (UHT forbids UCLASS inside arbitrary preprocessor blocks);
 * the handful of extra bytes of metadata in shipping builds is negligible.
 */
UCLASS()
class UVehicleInertialTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleChanged(double NewTotalMass, FVector NewCenterOfMass, FMatrix NewMomentOfInertia);

	int32 BroadcastCount = 0;
	double LastMass = 0.0;
	FVector LastCoM = FVector::ZeroVector;
	FMatrix LastMoI = FMatrix::Identity;
};
