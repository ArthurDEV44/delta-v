// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/FScanResult.h"
#include "Vehicles/UInstrumentComponent.h"
#include "UScannerInstrumentComponent.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScanCompleted, const FScanResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScanFailed, const FScanResult&, InvalidResult);

/**
 * Scanner instrument — accumulates exposure time on a target actor and emits
 * a typed FScanResult when RequiredExposureSeconds is reached.
 *
 * PRD: US-015, AC#2 & AC#4.
 *
 * Lifecycle
 * ---------
 *  BeginScan(Target) → Activate() → ticks accumulate ExposureElapsed →
 *  Exposure >= Required → OnScanCompleted fires with a valid FScanResult,
 *  scanner deactivates.
 *
 * Failure paths
 * -------------
 *  - Target null, out of range, or destroyed mid-scan → OnScanFailed with an
 *    invalid FScanResult; scanner deactivates.
 *  - Power depletion / safe-mode entry mid-scan → OnSafeModeEntered (base)
 *    cancels the scan and fires OnScanFailed.
 *  - Activation refused by UInstrumentComponent::Activate (no power) →
 *    BeginScan returns FScanResult::Invalid() immediately (no delegate fires;
 *    the synchronous return channel carries the failure).
 */
UCLASS(ClassGroup = (Vehicles), meta = (BlueprintSpawnableComponent))
class DELTAV_API UScannerInstrumentComponent : public UInstrumentComponent
{
	GENERATED_BODY()

public:
	UScannerInstrumentComponent();

	/** Exposure time required to complete a scan, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scanner",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double RequiredExposureSeconds = 30.0;

	/** Maximum effective scan range, in centimeters (UE native). Beyond this, exposure does not accumulate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scanner",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxRangeCm = 5000000.0; // 50 km per AC#2.

	/** Accumulated exposure on the current target since BeginScan. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scanner",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double ExposureElapsed = 0.0;

	/** Current scan target — cleared on completion, failure, or cancellation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scanner")
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Broadcast when a scan reaches RequiredExposureSeconds with the target in range. */
	UPROPERTY(BlueprintAssignable, Category = "Scanner")
	FOnScanCompleted OnScanCompleted;

	/** Broadcast when a scan fails (target lost, out of range, power cut). */
	UPROPERTY(BlueprintAssignable, Category = "Scanner")
	FOnScanFailed OnScanFailed;

	/**
	 * Start a scan against Target. Fails (returning FScanResult::Invalid())
	 * when:
	 *   - Target is null or already destroyed
	 *   - Activate() refuses (no sibling UPowerComponent, depleted, safe-mode)
	 *
	 * On success: marks bActive, resets ExposureElapsed, and returns
	 * FScanResult::Invalid() (the "in-progress" sentinel) — completion is
	 * delivered asynchronously through OnScanCompleted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scanner")
	FScanResult BeginScan(AActor* Target);

	/** Abort a running scan. Broadcasts OnScanFailed and deactivates. */
	UFUNCTION(BlueprintCallable, Category = "Scanner")
	void CancelScan();

	/** Per-tick exposure accumulation. Called from ASatellite::Tick or tests. */
	virtual void TickInstrument(double DeltaSeconds) override;

	/** On safe mode, cancel any in-progress scan before the base class deactivates. */
	virtual void OnSafeModeEntered() override;

	/**
	 * Synthesize a FScanResult for Target at the current world time. Exposed
	 * so tests can verify the data pathway independently of the tick loop.
	 * Returns Invalid() for a null Target.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scanner")
	FScanResult ProduceScanResult(AActor* Target) const;

private:
	/** Distance (cm) from the component's world location to Target. POSITIVE_INFINITY for null/unreachable. */
	double DistanceToTargetCm(const AActor* Target) const;

	/** Build the invalid result, broadcast OnScanFailed, deactivate. */
	void FailScan(const FScanResult& InvalidResult);
};
