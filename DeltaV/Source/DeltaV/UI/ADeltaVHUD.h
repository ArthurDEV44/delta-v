// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ADeltaVHUD.generated.h"

class AVehicle;
class UVehicleDebugHUDWidget;

/**
 * Project HUD.
 *
 * PRD: US-017. Hosts the vehicle debug widget and wires it up to the
 * `show debug VehicleHUD` toggle. The widget itself is a C++ UUserWidget
 * built programmatically, so no WBP asset is required for the shipping
 * feature.
 *
 * Lifecycle
 * ---------
 *  - ShowDebug("VehicleHUD") toggles bVehicleHUDVisible.
 *  - On toggle-on: create widget (if not already), AddToViewport, start
 *    10 Hz refresh timer.
 *  - On toggle-off: clear timer, RemoveFromViewport.
 *  - EndPlay tears everything down.
 *
 * Active-vehicle resolution (refresh callback)
 * --------------------------------------------
 *  1. GetOwningPlayerController()->GetPawn() cast to AVehicle — covers the
 *     "currently possessed vehicle" case (AC#3).
 *  2. Fallback: first AVehicle actor in the world.
 *  3. If neither → widget renders "No active vehicle" (AC#4).
 */
UCLASS()
class DELTAV_API ADeltaVHUD : public AHUD
{
	GENERATED_BODY()

public:
	ADeltaVHUD();

	/** Whether the VehicleHUD category is currently on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HUD")
	bool bVehicleHUDVisible = false;

	/** The widget instance (null while the category is off). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UVehicleDebugHUDWidget> VehicleDebugWidget = nullptr;

	/** Category name matching the `show debug VehicleHUD` console invocation. */
	static const FName VehicleHUDCategory;

	// AHUD
	virtual void ShowDebug(FName DebugType) override;

	// AActor
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FTimerHandle RefreshTimerHandle;

	/** Build the widget + add to viewport + start the 10 Hz refresh. Idempotent. */
	void EnableVehicleHUD();

	/** Tear down the widget + stop the timer. Idempotent. */
	void DisableVehicleHUD();

	/**
	 * 10 Hz timer callback. Resolves the active vehicle (possessed pawn, then
	 * first AVehicle in world) and pushes it into the widget.
	 */
	UFUNCTION()
	void RefreshVehicleHUD();

	/** Resolve the best-guess "currently active vehicle" for the widget. */
	AVehicle* FindActiveVehicle() const;
};
