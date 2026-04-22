// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UVehicleDebugHUDWidget.generated.h"

class AVehicle;
class UTextBlock;
class UVerticalBox;

/**
 * Tiny debug HUD widget listing the active vehicle's aggregated mass, current
 * thrust, fuel remaining, and theoretical delta-V.
 *
 * PRD: US-017.
 *
 * Update model
 * ------------
 * Refresh() is called by ADeltaVHUD's 10 Hz timer; the widget itself does not
 * tick. Each refresh re-reads the current AVehicle target (set by
 * SetActiveVehicle) and rebuilds a small multi-line text block. All math is
 * pulled from already-live actor state — the widget has no caching beyond
 * the last string it rendered.
 *
 * Headless-test seam
 * ------------------
 * BuildDebugLines(const AVehicle*) is a static, pure function that tests
 * call directly without spinning up a viewport / world / player controller.
 * It is the authoritative formatter — Refresh() just joins the lines and
 * sets the widget's visible text. Tests assert on BuildDebugLines output
 * to cover AC#1 / AC#2 / AC#4 without going through UMG.
 */
UCLASS()
class DELTAV_API UVehicleDebugHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Vehicle whose telemetry the widget currently displays. Weak so a
	 * GC'd / destroyed vehicle does not dangle; Refresh handles a stale
	 * weak ref gracefully.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|DebugHUD")
	TWeakObjectPtr<AVehicle> ActiveVehicle;

	/**
	 * Re-target the widget on a different vehicle and force an immediate
	 * Refresh. Passing nullptr shifts the widget into the "No active vehicle"
	 * state (AC#4).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|DebugHUD")
	void SetActiveVehicle(AVehicle* NewVehicle);

	/**
	 * Re-read ActiveVehicle and update the visible text. Called by
	 * ADeltaVHUD's 10 Hz timer (AC#1). Safe to call with no vehicle set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|DebugHUD")
	void Refresh();

	/**
	 * Pure formatter — produces one string per line for the given vehicle.
	 * Returns {"No active vehicle"} for null / pending-kill input (AC#4).
	 *
	 * Format (AC#1 — 3 decimals on every numeric field):
	 *   Vehicle: <name>
	 *   Mass   : %.3f kg
	 *   Thrust : %.3f N          (rocket-only; 0.000 for non-rocket vehicles)
	 *   Fuel   : %.3f kg         (rocket-only; 0.000 for non-rocket vehicles)
	 *   DeltaV : %.3f m/s        (rocket-only; 0.000 for non-rocket vehicles)
	 *
	 * For a pure ASatellite (no stages), the rocket-only lines render with
	 * 0.000 values rather than being omitted — keeps the layout stable so
	 * the text block does not reflow during vehicle switches at runtime.
	 */
	static TArray<FString> BuildDebugLines(const AVehicle* Vehicle);

protected:
	virtual void NativeConstruct() override;

private:
	/** Root vertical box — one child, the multi-line TextBlock. Built in NativeConstruct. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> DebugRoot;

	/** Target text block — Refresh writes the joined BuildDebugLines result here. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DebugTextBlock;
};
