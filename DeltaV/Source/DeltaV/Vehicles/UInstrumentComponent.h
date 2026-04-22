// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UInstrumentComponent.generated.h"

class UPowerComponent;

/**
 * Base class for instruments that draw power from a sibling UPowerComponent.
 *
 * PRD: US-015. Instruments (scanners, antennas, etc.) declare a constant
 * PowerDrawW that is counted toward the owning satellite's active draw budget
 * while bActive == true. Activation fails closed when the shared
 * UPowerComponent is depleted or in safe mode (AC#4).
 *
 * Subclasses override TickInstrument(DeltaSeconds) for per-instrument logic
 * (scan exposure, antenna transmission, etc.). The base class handles the
 * power-budget wiring.
 *
 * The instrument keeps a weak reference to the resolved UPowerComponent so
 * it can query state without a lookup on every call. The reference is
 * lazily populated on the first call to Activate / ResolvePower.
 */
UCLASS(ClassGroup = (Vehicles), meta = (BlueprintSpawnableComponent, Abstract))
class DELTAV_API UInstrumentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInstrumentComponent();

	/** True while the instrument is consuming power and ticking. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instrument")
	bool bActive = false;

	/** Instrument power draw in watts while active. Zero when inactive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instrument",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double PowerDrawW = 0.0;

	/**
	 * Attempt to activate. Fails if the owner has no UPowerComponent, if the
	 * power is depleted, or if the owner is in safe mode. Returns true only
	 * when bActive transitioned false → true. Named *Instrument to avoid
	 * colliding with UActorComponent::Activate(bool) UFUNCTION in UHT.
	 */
	virtual bool ActivateInstrument();

	/** Deactivate unconditionally. No-op if already inactive. */
	virtual void DeactivateInstrument();

	/** Current power draw in watts — PowerDrawW when active, zero otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Instrument")
	virtual double GetPowerDrawW() const;

	/**
	 * Called by ASatellite when UPowerComponent has entered safe mode.
	 * Default impl is Deactivate; subclasses may extend to reset internal state
	 * (e.g., cancel an in-progress scan).
	 */
	virtual void OnSafeModeEntered();

	/**
	 * Per-instrument per-tick update. Default no-op. Subclasses override for
	 * scan exposure accumulation, antenna chatter, etc.
	 *
	 * Called by ASatellite::Tick AFTER UPowerComponent::TickPower so
	 * instruments always see the latest power state.
	 */
	virtual void TickInstrument(double DeltaSeconds);

	// UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

protected:
	/**
	 * Resolve and cache the owning actor's UPowerComponent. Returns nullptr
	 * if the owner has no such component. Safe to call repeatedly.
	 */
	UPowerComponent* ResolvePower();

	/** Same as ResolvePower but const — callers are read-only. */
	const UPowerComponent* GetPower() const;

	/** Weak ref to the sibling UPowerComponent; populated lazily on demand. */
	TWeakObjectPtr<UPowerComponent> CachedPower;
};
