// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UPowerComponent.generated.h"

class UInstrumentComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPowerDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSafeModeExited);

/**
 * Energy budget for an ASatellite (or any AActor that wants a battery).
 *
 * PRD: US-015.
 *
 * Units
 * -----
 *  - CapacityWh, CurrentChargeWh : watt-hours.
 *  - SolarPanelPowerW            : watts (constant rated output in direct sunlight).
 *  - Instrument draws            : watts (queried from each registered
 *                                  UInstrumentComponent::GetPowerDrawW()).
 *  - TickPower(DeltaSeconds)     : seconds. Energy delta is
 *                                    NetPowerW * DeltaSeconds / 3600.
 *
 * Shadow model
 * ------------
 * bIsInShadow is driven by the caller (ASatellite, a test harness, or a
 * future light-source subsystem). In shadow, solar input is zero. Outside
 * shadow, solar input is SolarPanelPowerW (no orientation modulation yet —
 * panel pointing is a future refinement).
 *
 * Safe mode
 * ---------
 * When CurrentChargeWh transitions to 0 with a non-zero previous value,
 * bSafeMode is set and OnPowerDepleted fires. Safe mode does NOT automatically
 * reset — callers (ASatellite) turn instruments off in response to the
 * delegate. Recharging back above zero while in shadow is impossible
 * (SolarPanelPowerW only contributes out of shadow); once out of shadow the
 * charge can recover but safe mode stays latched until ExitSafeMode is called
 * by gameplay logic (intentional — reboot semantics are a gameplay choice,
 * not a power-law consequence).
 *
 * Draw tracking
 * -------------
 * Instruments self-register via RegisterInstrumentDraw/UnregisterInstrumentDraw.
 * A weak list is used so a destroyed instrument doesn't dangle. The total
 * active-draw sum is recomputed at every TickPower call.
 */
UCLASS(ClassGroup = (Vehicles), meta = (BlueprintSpawnableComponent))
class DELTAV_API UPowerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPowerComponent();

	/** Battery capacity (Wh). CurrentChargeWh is clamped to [0, CapacityWh] on writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double CapacityWh = 100.0;

	/** Current battery charge (Wh). Updated by TickPower. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double CurrentChargeWh = 100.0;

	/** Rated solar panel output in direct sunlight (W). Zero when bIsInShadow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double SolarPanelPowerW = 50.0;

	/** Set by the owning satellite / test harness. When true, solar contribution is zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bIsInShadow = false;

	/** Latched after OnPowerDepleted fires. Cleared only by ExitSafeMode(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	bool bSafeMode = false;

	/**
	 * Minimal beacon flag — stays TRUE in safe mode per AC#3 ("comms OFF
	 * except minimal beacon"). Gameplay logic that routes comms checks
	 * against this flag rather than the generic bSafeMode gate.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	bool bMinimalBeaconOn = true;

	/** Fires once when CurrentChargeWh crosses down to zero. */
	UPROPERTY(BlueprintAssignable, Category = "Power")
	FOnPowerDepleted OnPowerDepleted;

	/** Fires once when ExitSafeMode() successfully clears bSafeMode. */
	UPROPERTY(BlueprintAssignable, Category = "Power")
	FOnSafeModeExited OnSafeModeExited;

	/**
	 * Advance the energy budget by DeltaSeconds (seconds). Non-finite or
	 * non-positive DeltaSeconds is a no-op. Safe to call from the actor's
	 * tick or from a test harness.
	 *
	 * Returns the net power (W) seen this step — positive = recharging,
	 * negative = draining.
	 */
	UFUNCTION(BlueprintCallable, Category = "Power")
	double TickPower(double DeltaSeconds);

	/**
	 * Add an instrument to the draw list. Idempotent — no-op on duplicates
	 * and on null. Weak-ref'd so a destroyed instrument does not dangle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Power")
	void RegisterInstrumentDraw(UInstrumentComponent* Instrument);

	/** Remove an instrument from the draw list. No-op if not registered. */
	UFUNCTION(BlueprintCallable, Category = "Power")
	void UnregisterInstrumentDraw(UInstrumentComponent* Instrument);

	/** Sum of PowerDrawW across currently-active registered instruments (W). */
	UFUNCTION(BlueprintCallable, Category = "Power")
	double GetActiveDrawW() const;

	/**
	 * Explicitly clear bSafeMode. No-op if bSafeMode is already false or if
	 * CurrentChargeWh is still zero (safe mode can't lift while the battery
	 * is empty — that would just re-trip on the next tick).
	 */
	UFUNCTION(BlueprintCallable, Category = "Power")
	bool ExitSafeMode();

	/** Convenience for tests and gameplay: directly set the charge, clamped. */
	UFUNCTION(BlueprintCallable, Category = "Power")
	void SetCurrentChargeWh(double NewChargeWh);

	/** True when CurrentChargeWh is effectively zero. */
	UFUNCTION(BlueprintCallable, Category = "Power")
	bool IsPowerDepleted() const;

	// UActorComponent — tick drives TickPower while the component is registered.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Weak refs to registered instruments so destruction doesn't dangle. */
	TArray<TWeakObjectPtr<UInstrumentComponent>> TrackedInstruments;
};
