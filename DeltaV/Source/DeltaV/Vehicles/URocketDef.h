// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "URocketDef.generated.h"

/**
 * Static configuration for a single rocket stage.
 *
 * PRD: US-014. A stage owns its dry mass, fuel mass, specific impulse, max
 * thrust, and a thrust curve (normalized multiplier of MaxThrust over burn
 * time). The parent URocketDef holds the stack of stages, ordered bottom-up:
 * index 0 is the first stage (first to fire and first to separate).
 *
 * Units
 * -----
 *  - DryMassKg / FuelMassKg            : kilograms (SI).
 *  - SpecificImpulseSeconds            : seconds (Isp).
 *  - MaxThrustNewtons                  : newtons (SI).
 *  - ThrustCurve                       : dimensionless multiplier in [0,1]
 *                                        applied to MaxThrust, parameterised
 *                                        by burn time in seconds since the
 *                                        stage was ignited. Empty/unset curve
 *                                        is treated as a flat 1.0.
 *  - LocalMountOffsetCm                : cm, UE native — offset of the stage's
 *                                        mount point in the rocket's actor-local
 *                                        frame.
 *  - LocalInertiaDiagonalKgM2          : kg·m², expressed about the stage's own
 *                                        CoM in the stage's local frame. Matches
 *                                        UVehiclePartComponent semantics.
 */
USTRUCT(BlueprintType)
struct DELTAV_API FStageDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage")
	FName StageName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double DryMassKg = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double FuelMassKg = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	double SpecificImpulseSeconds = 300.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxThrustNewtons = 0.0;

	/**
	 * Normalized thrust multiplier (0..1) vs burn time (seconds). Sampled by
	 * UStageComponent at runtime. If no keys are set, the component treats
	 * the curve as a constant 1.0 — useful for quick tests and simple engines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage")
	FRuntimeFloatCurve ThrustCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage")
	FVector LocalMountOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Stage")
	FVector LocalInertiaDiagonalKgM2 = FVector(1.0, 1.0, 1.0);
};

/**
 * Data-driven rocket blueprint: stack of stages + payload + metadata.
 *
 * PRD: US-014. Consumed by ARocket::SpawnFromDef. A designer can tune any
 * field in the editor, save, and re-spawn — no C++ recompile needed.
 *
 * Stage ordering
 * --------------
 * Stages[0] is the BOTTOM stage (first to fire, first to separate). Upper
 * stages sit on top and stay attached until the stage below them has been
 * jettisoned. When Stages[0] separates, Stages[1] becomes the new bottom
 * stage for the remaining rocket.
 *
 * Validation
 * ----------
 * IsValid enforces the non-empty-stacks rule per AC#4: a def with zero stages
 * (or null / negative masses / zero Isp on any stage) is rejected by the
 * factory before any AActor is created.
 */
UCLASS(BlueprintType)
class DELTAV_API URocketDef : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Human-readable rocket name — used in log messages and debug HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket")
	FName RocketName = NAME_None;

	/** Payload dry mass (kg). Modelled as a single extra UVehiclePartComponent on top of the stages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double PayloadDryMassKg = 0.0;

	/** Mount offset of the payload in the rocket's actor-local frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket")
	FVector PayloadMountOffsetCm = FVector::ZeroVector;

	/** Principal-axis inertia diagonal of the payload (kg·m²), about payload CoM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket")
	FVector PayloadInertiaDiagonalKgM2 = FVector(1.0, 1.0, 1.0);

	/** Stages, ordered bottom-up (index 0 fires first and separates first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket")
	TArray<FStageDef> Stages;

	/**
	 * Non-fatal validation — returns false if the def could not produce a
	 * physically meaningful rocket. Fills OutError with a diagnostic string
	 * if provided. Used by the factory; also callable from tests.
	 *
	 * Rules
	 * -----
	 *  - Stages.Num() > 0
	 *  - every Stages[i].DryMassKg  > 0
	 *  - every Stages[i].FuelMassKg >= 0
	 *  - every Stages[i].SpecificImpulseSeconds > 0
	 *  - every Stages[i].MaxThrustNewtons >= 0
	 *  - PayloadDryMassKg >= 0
	 *
	 * A stage with FuelMassKg == 0 is allowed (represents a strap-on booster
	 * shell or inert fairing); such a stage simply has no combustion.
	 */
	bool IsValid(FString* OutError = nullptr) const;
};
