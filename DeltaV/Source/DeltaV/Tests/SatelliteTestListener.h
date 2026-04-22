// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Vehicles/FScanResult.h"
#include "SatelliteTestListener.generated.h"

/**
 * Event-counter helper for ASatellite / UScannerInstrumentComponent / UPowerComponent
 * automation tests. Subscribes to dynamic multicast delegates via AddDynamic
 * (which requires a UFUNCTION on a UObject — hence this tiny class).
 *
 * Always compiled (UHT forbids UCLASS inside arbitrary preprocessor blocks);
 * the handful of extra bytes of metadata in shipping builds is negligible.
 */
UCLASS()
class USatelliteTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleScanCompleted(const FScanResult& Result);

	UFUNCTION()
	void HandleScanFailed(const FScanResult& InvalidResult);

	UFUNCTION()
	void HandlePowerDepleted();

	int32 ScanCompletedCount = 0;
	int32 ScanFailedCount = 0;
	int32 PowerDepletedCount = 0;

	FScanResult LastCompletedResult;
	FScanResult LastFailedResult;
};
