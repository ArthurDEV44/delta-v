// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/AVehicle.h"
#include "ASatellite.generated.h"

class UInstrumentComponent;
class UPowerComponent;

/**
 * Unmanned vehicle that runs on a solar-charged battery and operates
 * scientific instruments.
 *
 * PRD: US-015.
 *
 * Structure
 * ---------
 *  - UPowerComponent (sub-object, always present) — battery + solar + safe-mode latch.
 *  - TArray<UInstrumentComponent*> Instruments — populated at runtime or by
 *    a URocketDef-like satellite def (future story). The base class knows
 *    nothing about specific instruments; scanners / antennas are added by
 *    callers via RegisterInstrument.
 *
 * Tick flow
 * ---------
 *  Tick → PowerComp->TickPower(dt) → each instrument's TickInstrument(dt).
 *  The power pass runs first so a draining battery turns instruments off
 *  BEFORE they accumulate another tick worth of exposure on a dead frame.
 *
 * Safe mode
 * ---------
 *  When UPowerComponent fires OnPowerDepleted we propagate it to every
 *  instrument via UInstrumentComponent::OnSafeModeEntered. bMinimalBeaconOn
 *  stays true through safe mode (AC#3: "comms OFF except minimal beacon").
 */
UCLASS()
class DELTAV_API ASatellite : public AVehicle
{
	GENERATED_BODY()

public:
	ASatellite();

	/** Battery + solar. Created as a default sub-object on construction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite")
	TObjectPtr<UPowerComponent> PowerComponent = nullptr;

	/**
	 * Instruments attached to this satellite. Filled via RegisterInstrument;
	 * iteration is nullptr-filtered so a destroyed instrument does not stall
	 * the tick loop.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite")
	TArray<TObjectPtr<UInstrumentComponent>> Instruments;

	/**
	 * Register an instrument with this satellite. Idempotent. Does not create
	 * the component — the caller NewObject's it and passes it here. Wires up
	 * the power-component cross-reference and adds the instrument to the
	 * draw-tracked list.
	 */
	UFUNCTION(BlueprintCallable, Category = "Satellite")
	void RegisterInstrument(UInstrumentComponent* Instrument);

	/** Remove an instrument from tracking. Does not destroy the component. */
	UFUNCTION(BlueprintCallable, Category = "Satellite")
	void UnregisterInstrument(UInstrumentComponent* Instrument);

	/** True when UPowerComponent is in safe mode. */
	UFUNCTION(BlueprintCallable, Category = "Satellite")
	bool IsSafeMode() const;

	/** True when UPowerComponent's minimal beacon flag is on. Stays true in safe mode. */
	UFUNCTION(BlueprintCallable, Category = "Satellite")
	bool IsMinimalBeaconOn() const;

	/**
	 * Advance power + every registered instrument by DeltaSeconds. Exposed so
	 * headless tests can drive integration without an active PIE tick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Satellite")
	void StepSimulation(double DeltaSeconds);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	/**
	 * Delegate handler for PowerComponent->OnPowerDepleted. Propagates safe-mode
	 * entry to every instrument. Public because AddDynamic requires visibility
	 * of the bound method — in-process wiring happens in BeginPlay; tests call
	 * this path by binding directly.
	 */
	UFUNCTION()
	void HandlePowerDepleted();

private:
	/** true once BeginPlay has wired the power-depleted subscription. */
	bool bPowerListenerBound = false;
};
