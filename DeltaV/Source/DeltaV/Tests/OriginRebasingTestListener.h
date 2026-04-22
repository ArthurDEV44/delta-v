// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OriginRebasingTestListener.generated.h"

/**
 * Event-counter helper for the UOriginRebasingSubsystem automation suite.
 * A dynamic multicast delegate's AddDynamic requires the receiver be a
 * UObject with a UFUNCTION handler — hence this tiny class.
 *
 * Kept outside WITH_DEV_AUTOMATION_TESTS because UHT cannot conditionally
 * generate the reflection vtable, and omitting the class in shipping would
 * produce unresolved-symbol link errors for its generated body.
 */
UCLASS()
class UOriginRebasingTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleRebased(FVector AppliedOffsetMeters);

	int32 BroadcastCount = 0;
	FVector LastOffset = FVector::ZeroVector;
};
