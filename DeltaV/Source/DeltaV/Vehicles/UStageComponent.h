// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Vehicles/UVehiclePartComponent.h"
#include "UStageComponent.generated.h"

struct FStageDef;

/**
 * A single rocket stage — a mass-bearing part with ignition / combustion /
 * separation semantics on top of the base UVehiclePartComponent plumbing.
 *
 * PRD: US-014.
 *
 * Mass accounting invariant
 * -------------------------
 * The inherited UVehiclePartComponent::Mass field is kept equal to
 *   DryMassKg + FuelMassRemainingKg
 * at all times, via SetMass(). This lets the owning AVehicle's aggregation
 * see fuel consumption as a normal mass delta and fire OnInertialPropertiesChanged
 * through the existing threshold logic. Callers MUST NOT write Mass directly —
 * use InitFromStageDef / AdvanceCombustion / SetDryMassKg / SetFuelMassRemainingKg.
 *
 * Ignition / combustion
 * ---------------------
 * Ignite() sets bIgnited=true and resets the burn-time clock. AdvanceCombustion(dt)
 * samples the normalized thrust curve at the current burn time, converts to a
 * mass-flow rate via m_dot = Thrust / (Isp * g0), and consumes up to that much
 * fuel (clamped by remaining fuel). Returns the actual fuel consumed in kg so
 * callers can integrate thrust impulse with the same truncation.
 *
 * Separation
 * ----------
 * Separate(WorldOverride) jettisons this stage from its current AVehicle owner
 * into a new transient AVehicle carrying the stage's dry mass as a plain
 * UVehiclePartComponent. The old UStageComponent is detached and destroyed; the
 * parent AVehicle's aggregate shifts accordingly (via UnregisterPart).
 *
 * Passing WorldOverride = nullptr uses the owner actor's world; if the owner
 * has no world (automation), the caller must provide one.
 */
UCLASS(ClassGroup = (Vehicles), meta = (BlueprintSpawnableComponent))
class DELTAV_API UStageComponent : public UVehiclePartComponent
{
	GENERATED_BODY()

public:
	UStageComponent();

	/** Designer-authored stage name (propagated from FStageDef::StageName). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage")
	FName StageName = NAME_None;

	/** Stage dry mass (kg). Constant over the stage's lifetime. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double DryMassKg = 0.0;

	/** Initial fuel mass (kg) at ignition. Preserved so tests can compute burn fractions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double FuelMassInitialKg = 0.0;

	/** Fuel remaining (kg). Decrements each AdvanceCombustion tick while ignited. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double FuelMassRemainingKg = 0.0;

	/** Specific impulse (s). Used for thrust → mass flow conversion. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	double SpecificImpulseSeconds = 300.0;

	/** Max thrust (N) at full throttle (curve = 1.0). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxThrustNewtons = 0.0;

	/** Normalized thrust curve (0..1) vs burn time (s). Empty = flat 1.0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage")
	FRuntimeFloatCurve ThrustCurve;

	/** true once Ignite() has been called and while fuel remains. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage")
	bool bIgnited = false;

	/** Seconds of burn time accumulated since ignition. Clock argument to ThrustCurve. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double BurnTimeSeconds = 0.0;

	/** True once Separate() has consumed this component. Prevents double-spawn on re-entrant calls. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicles|Stage")
	bool bSeparated = false;

	/**
	 * Populate all stage fields from a FStageDef. Does NOT register with the
	 * owner — that happens through the standard UActorComponent registration
	 * path (OnRegister) once the component is attached to an AVehicle.
	 */
	void InitFromStageDef(const FStageDef& StageDef);

	/** Begin burning. No-op (with warning log) if already ignited. */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	void Ignite();

	/** Extinguish without expending fuel (e.g., aborted burn). */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	void Extinguish();

	/** True while bIgnited == true, regardless of fuel state. */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	bool IsIgnited() const { return bIgnited; }

	/** True when FuelMassRemainingKg <= 0 (within a small epsilon). */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	bool IsFuelDepleted() const;

	/**
	 * Thrust magnitude (N) at the current (or provided) burn time. Zero when
	 * not ignited or fuel depleted. Clamps the curve multiplier to [0, 1].
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	double GetCurrentThrustNewtons(double OverrideBurnTimeSeconds = -1.0) const;

	/**
	 * Mass flow rate (kg/s) implied by the current thrust and Isp. Zero when
	 * not ignited or fuel depleted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	double GetMassFlowRateKgPerSec(double OverrideBurnTimeSeconds = -1.0) const;

	/**
	 * Advance combustion by DeltaSeconds. Samples ThrustCurve at the *start* of
	 * the interval (simple Euler — good enough at 60+ Hz, analytic for constant
	 * curves). Consumes up to (m_dot * dt) kg of fuel, decrements Mass, ticks
	 * BurnTimeSeconds forward, and auto-extinguishes when fuel runs out.
	 *
	 * Returns the actual fuel mass consumed (kg) this step.
	 *
	 * @note For AC#2 the thrust curve is a flat 1.0, so the linear mass decay
	 *       the test asserts falls out as m_dot = MaxThrust/(Isp*g0) per second.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	double AdvanceCombustion(double DeltaSeconds);

	/**
	 * Jettison this stage into a new transient AVehicle.
	 *
	 * The new AVehicle is spawned at this component's current world transform
	 * and carries a plain UVehiclePartComponent whose mass equals
	 *   DryMassKg + FuelMassRemainingKg
	 * — i.e., mass conservation, not dry-only: any residual fuel goes with
	 * the shell (there is no out-of-rocket "vented fuel" sink). Typical
	 * callers invoke Separate only after IsFuelDepleted() == true, which is
	 * the US-014 AC#3 scenario; a warning is logged otherwise.
	 *
	 * The original UStageComponent is unregistered from its owner and destroyed;
	 * the owning AVehicle's aggregate mass drops by the same total. Calling
	 * Separate more than once on the same component is a no-op that returns
	 * nullptr (bSeparated guard — safe under re-entrant broadcasts).
	 *
	 * @param WorldOverride If non-null, the world to spawn into. Otherwise uses
	 *                      the current owner's world. Required if the owner has
	 *                      no world (e.g., automation with a transient actor).
	 * @return The newly spawned AVehicle, or nullptr on failure (already
	 *         separated, no owner, no valid world, or spawn rejected).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicles|Stage")
	class AVehicle* Separate(UWorld* WorldOverride = nullptr);

private:
	/** Sync the inherited UVehiclePartComponent::Mass to DryMassKg + FuelMassRemainingKg. */
	void SyncAggregateMass();

	/** Sample the thrust curve safely — returns 1.0 when no keys are set. */
	double SampleThrustMultiplier(double BurnTime) const;
};
