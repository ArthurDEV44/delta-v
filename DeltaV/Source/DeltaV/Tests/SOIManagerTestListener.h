// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SOIManagerTestListener.generated.h"

class UCelestialBody;

/**
 * Event-counter helper for USOIManager automation tests. Subscribes to the
 * subsystem's dynamic multicast delegates via AddDynamic (which requires a
 * UFUNCTION on a UObject — hence this tiny class).
 *
 * Always compiled (UHT forbids UCLASS inside arbitrary preprocessor blocks);
 * the handful of extra bytes of metadata in shipping builds is negligible.
 */
UCLASS()
class USOIManagerTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleTransition(FGuid VesselKey, UCelestialBody* NewSOI);

	UFUNCTION()
	void HandleOrphan(FGuid VesselKey);

	int32 TransitionCount = 0;
	int32 OrphanCount = 0;
	FGuid LastTransitionKey;

	UPROPERTY()
	TObjectPtr<UCelestialBody> LastTransitionBody;

	FGuid LastOrphanKey;
};
