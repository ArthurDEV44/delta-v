// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/AVehicle.h"
#include "ARocket.generated.h"

class URocketDef;
class UStageComponent;
class UVehiclePartComponent;

/**
 * Multi-stage rocket vehicle.
 *
 * PRD: US-014. An ARocket is an AVehicle whose mass budget comes from a stack
 * of UStageComponent (bottom-up, index 0 fires first) plus an optional
 * payload UVehiclePartComponent. All properties are driven by a URocketDef
 * DataAsset so new configurations can be authored without a C++ rebuild.
 *
 * Spawn
 * -----
 * Static factory ARocket::SpawnFromDef(World, Def, Transform):
 *   1. Rejects null World / null Def / Def.IsValid()==false with an explicit
 *      UE_LOG Error, without creating any partial actor (AC#4).
 *   2. Uses SpawnActorDeferred → InitFromDef → FinishSpawning so all components
 *      exist before BeginPlay triggers the initial inertial recompute.
 *
 * Theoretical delta-V
 * -------------------
 * Staged Tsiolkovsky: iterate stages from bottom to top. At stage i:
 *   m_init_i = (sum of dry+fuel from stage i upward) + PayloadDryMass
 *   m_final_i = m_init_i - Stages[i].FuelMassRemaining
 *   dV_i     = Isp_i * g0 * ln(m_init_i / m_final_i)
 * Total dV = Σ dV_i. Uses the *current* fuel snapshot; immediately after
 * spawn that matches the def's nominal dV.
 *
 * Combustion
 * ----------
 * ARocket itself does not tick. Callers (gameplay loop, tests) drive
 * TickCombustion(dt) which forwards dt to every ignited stage's
 * AdvanceCombustion. Ignition is orchestrated by IgniteNextStage() which
 * targets the lowest not-yet-ignited stage.
 */
UCLASS()
class DELTAV_API ARocket : public AVehicle
{
	GENERATED_BODY()

public:
	ARocket();

	/** The def this rocket was populated from. May be null (e.g., when constructed in a test without a def). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<const URocketDef> SourceDef = nullptr;

	/** Stage stack, bottom-up. Stages[0] fires first and separates first. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TArray<TObjectPtr<UStageComponent>> Stages;

	/** Payload dry mass part. Null if the def specifies PayloadDryMassKg == 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<UVehiclePartComponent> PayloadPart = nullptr;

	/**
	 * Spawn + populate an ARocket from a URocketDef. Validates the def first
	 * and logs + returns nullptr on any failure, with no partial actor in the
	 * world (AC#4). Does not register the rocket with any subsystem — that
	 * wiring happens in US-016 / US-019 once the higher-level factory lands.
	 */
	static ARocket* SpawnFromDef(UWorld* World, const URocketDef* Def, const FTransform& Transform);

	/**
	 * One-shot initialization from a def. Builds stage + payload components,
	 * registers them, and triggers a forced inertial recompute. Safe to call
	 * only once — subsequent calls log a warning and no-op.
	 */
	void InitFromDef(const URocketDef* Def);

	/**
	 * Ignite the lowest not-yet-ignited stage with fuel. Returns the ignited
	 * stage, or nullptr if none could be ignited (all separated, all empty,
	 * or all already burning).
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	UStageComponent* IgniteNextStage();

	/**
	 * Forward DeltaSeconds to every currently ignited stage's AdvanceCombustion.
	 * Returns the total fuel consumed (kg) across all stages this step.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	double TickCombustion(double DeltaSeconds);

	/**
	 * Total current mass in kg: payload + sum over remaining stages of
	 * (DryMass + FuelRemaining). Convenience wrapper around the aggregated
	 * TotalMass after a recompute — kept separate so tests can assert mass
	 * math independent of the AVehicle aggregation plumbing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	double GetCurrentTotalMassKg() const;

	/**
	 * Staged Tsiolkovsky delta-V (m/s) over all remaining stages, using each
	 * stage's current fuel snapshot. Zero when no stages remain or all stages
	 * are dry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	double CalculateTheoreticalDeltaV() const;

	/**
	 * Destroy hook — cascades destruction to any actor attached to us (e.g.,
	 * a satellite payload attached by UVehicleFactory). UE does NOT auto-destroy
	 * attached children, so without this override a Destroy() on the rocket
	 * would leave the satellite as an orphaned world actor.
	 */
	virtual void Destroyed() override;

private:
	/** Guard so InitFromDef can only succeed once. */
	bool bInitialized = false;

	/**
	 * One-shot log guard for CalculateTheoreticalDeltaV when it encounters a
	 * stage with FuelLeft > 0 but Isp <= 0 (PRD US-018 AC#3). Mutable so the
	 * warning can fire from the const accessor without surprising callers
	 * with a state change — the guard is purely diagnostic.
	 */
	mutable bool bWarnedInvalidIsp = false;
};
