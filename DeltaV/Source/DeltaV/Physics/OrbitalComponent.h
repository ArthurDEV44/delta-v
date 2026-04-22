// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Orbital/OrbitalState.h"
#include "OrbitalComponent.generated.h"

/**
 * Physics integration mode for a vehicle.
 *
 *  - Rail  : position / rotation advanced analytically from FOrbitalState via
 *            Kepler propagation. Chaos rigid body is NOT simulated.
 *  - Local : Chaos rigid body takes over; external forces (gravity, drag, etc.)
 *            apply normally. The Kepler propagator is idle in this mode.
 *
 * Rail is the default on construction. Switching between modes while playing
 * is handled by future stories (US-020 / US-021) which add the state-injection
 * logic required for continuity; this component only handles per-tick behaviour
 * in each mode and a direct SetPhysicsMode that does NOT preserve velocity.
 */
UENUM(BlueprintType)
enum class EPhysicsMode : uint8
{
	Rail  UMETA(DisplayName = "Rail (Kepler propagation)"),
	Local UMETA(DisplayName = "Local (Chaos rigid body)")
};

/**
 * Attaches to any AVehicle to provide dual-tier physics: analytic Kepler
 * propagation in Rail mode, or Chaos rigid body simulation in Local mode.
 *
 * PRD: US-019 (EP-004 Dual-Tier Physics Coupling).
 *
 * Rail mode (AC#1)
 * ---------------
 * On TickComponent, the component advances FOrbitalState via
 * UOrbitalMath::PropagateKepler, converts to a Cartesian state vector with
 * ElementsToStateVector, applies meters -> centimeters, offsets by the parent
 * body's world position, and writes the result with SetActorLocationAndRotation.
 * Attitude is prograde-pointing: actor forward (X) along velocity, up (Z)
 * along orbit-plane radial-out. Chaos rigid body simulation is disabled on
 * the owner's root primitive.
 *
 * Local mode (AC#2)
 * -----------------
 * The component performs no per-tick work. Chaos simulation is enabled on the
 * owner's root primitive component (if any); external forces drive the actor.
 *
 * Rail -> Local transition (US-020)
 * ---------------------------------
 * SwitchToLocal() is the continuity-preserving handoff: it reads the current
 * FOrbitalState, computes Cartesian (position, velocity, angular velocity),
 * flips the Chaos body on, and injects the velocities so the body continues
 * its trajectory without visual jump. SetPhysicsMode (the bare enum setter)
 * does NOT preserve velocity — callers that want continuity MUST go through
 * SwitchToLocal.
 *
 * Local -> Rail transition (US-021)
 * ---------------------------------
 * SwitchToRail() is the capture-and-snap reverse handoff: it reads the live
 * Chaos body position + linear velocity, converts to orbital elements via
 * StateVectorToElements, flips Chaos off, and resumes Rail propagation from
 * the fresh state. A sub-surface altitude refuses the switch (AC#4); a
 * near-zero velocity logs a degeneracy warning but still attempts the
 * conversion (AC#3). SwitchToRailFromStateVector is the test seam that
 * drives the snap from an explicit Cartesian state, used by the automation
 * suite because a transient-package component has no realised Chaos body
 * to read from.
 *
 * Budget (AC#3)
 * -------------
 * TickComponent uses pure analytic Kepler (one Newton-Raphson eccentric-anomaly
 * solve plus a closed-form state-vector reconstruction). A 100-vehicle rail
 * scene must tick in under 1 ms/frame on the CPU target.
 *
 * Unhappy path (AC#4)
 * -------------------
 * If the owning AActor is not an AVehicle (or there is no owner at BeginPlay),
 * the component logs an error via LogDeltaV and permanently disables its tick.
 * No crash, no half-initialised state.
 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class DELTAV_API UOrbitalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOrbitalComponent();

	/**
	 * Physics mode for this tick. Rail = Kepler propagation, Local = Chaos.
	 * Changing this at edit-time propagates on BeginPlay. Changing at runtime
	 * should go through SetPhysicsMode to apply the Chaos toggle on the root.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Orbital")
	EPhysicsMode Mode = EPhysicsMode::Rail;

	/**
	 * Full f64 orbital state. Rail propagation mutates this in-place; Local
	 * leaves it untouched (capture-on-switch is US-021 territory).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Orbital")
	FOrbitalState CurrentState;

	/**
	 * Optional override for the gravitational parameter (m^3 / s^2). If > 0,
	 * it overrides any value derived from CurrentState.ParentBody. Used by
	 * tests and by level data that deliberately pins Mu.
	 *
	 * When this is 0 and CurrentState.ParentBody is invalid, the component
	 * falls back to Earth's mu (3.986004418e14) and logs a warning once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Orbital",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double GravitationalParameterOverride = 0.0;

	/**
	 * Apply a new physics mode and propagate the Chaos toggle on the owner's
	 * root component immediately. Does NOT preserve linear / angular velocity
	 * across mode changes — that is the job of US-020 (SwitchToLocal) and
	 * US-021 (SwitchToRail). No-op if the owner is invalid or the tick has
	 * been disabled by AC#4.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	void SetPhysicsMode(EPhysicsMode NewMode);

	/**
	 * Transition Rail -> Local while preserving orbital continuity (US-020).
	 *
	 *  1. Reads CurrentState and converts it to Cartesian (position, velocity).
	 *  2. Activates Chaos rigid body on the owner's root primitive.
	 *  3. Injects the linear velocity (cm/s) and the orbital angular velocity
	 *     (rad/s, ω = r × v / |r|²) so the body continues its trajectory
	 *     without visual / physical discontinuity.
	 *
	 * Called while already in Local mode: logs a warning once and returns
	 * without touching anything (AC#4).
	 *
	 * Called while the owner check has failed, CurrentState is invalid, or
	 * ElementsToStateVector rejects the inputs: logs and returns, staying in
	 * Rail mode so the caller can recover.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	void SwitchToLocal();

	/**
	 * Pure test / introspection seam. Computes the linear velocity (m/s,
	 * SI) and orbital angular velocity (rad/s) that SwitchToLocal would
	 * inject for the current state. Does NOT mutate the component and does
	 * NOT touch the owner — unit tests can validate the injection math
	 * without a registered Chaos body.
	 *
	 * @return false + zero-initialised outputs if CurrentState fails
	 *         ElementsToStateVector validation or mu is unresolvable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	bool ComputeInjectionVelocities(
		FVector& OutLinearVelocityMps,
		FVector& OutAngularVelocityRadps) const;

	/**
	 * Transition Local -> Rail by capturing the live Chaos body state (US-021).
	 *
	 *  1. Reads the owner's current world location and converts to SI metres,
	 *     subtracting the parent body's WorldPosition (if any).
	 *  2. Reads the root primitive's linear velocity (cm/s) and converts to
	 *     m/s.
	 *  3. Delegates to SwitchToRailFromStateVector.
	 *
	 * No-op when already in Rail mode (consistency with SwitchToLocal's
	 * symmetric guard). No-op when the owner check previously failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	void SwitchToRail();

	/**
	 * Snap back to Rail from an explicit Cartesian state (SI units).
	 *
	 *  - Refuses the switch and logs an error if the parent body is set and
	 *    the captured position is inside the body's EquatorialRadius (AC#4).
	 *  - Logs a "near-zero velocity, orbital elements degenerate" warning
	 *    if |VelMps| is below 0.1 m/s (AC#3); still attempts the conversion,
	 *    which may then fail internally in StateVectorToElements.
	 *  - On successful conversion, preserves the existing ParentBody ref
	 *    (StateVectorToElements zeros it), commits CurrentState, flips
	 *    Mode = Rail, and calls ApplyModeToOwner.
	 *
	 * Exposed publicly as the automation-test seam — transient-package
	 * components have no realised Chaos body, so GetPhysicsLinearVelocity
	 * in the full SwitchToRail path returns zero and tests cannot validate
	 * the math through it.
	 *
	 * @return true iff the snap committed and the mode flipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	bool SwitchToRailFromStateVector(
		const FVector& PosMeters, const FVector& VelMps);

	/**
	 * Last linear velocity that SwitchToLocal injected into the Chaos body,
	 * expressed in SI units (m/s). Zero before the first SwitchToLocal, or
	 * if the switch was refused. Exposed for automation tests — the actual
	 * Chaos body velocity is authoritative at runtime.
	 *
	 * NOTE: This is a diagnostic snapshot at the moment of the last switch,
	 * NOT a live read-back. SetOrbitalState / CurrentState edits do not
	 * update this field; a stale value means no SwitchToLocal has happened
	 * since the edit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	FVector GetLastInjectedLinearVelocityMps() const { return LastInjectedLinearVelocityMps; }

	/**
	 * Last angular velocity injected (rad/s). Mirrors the linear getter
	 * including the same staleness caveat.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	FVector GetLastInjectedAngularVelocityRadps() const { return LastInjectedAngularVelocityRadps; }

	/** Read the current orbital state (copy, Blueprint-friendly). */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	FOrbitalState GetOrbitalState() const { return CurrentState; }

	/** Overwrite the orbital state (rail-mode fast-forward, editor authoring). */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	void SetOrbitalState(const FOrbitalState& NewState) { CurrentState = NewState; }

	/**
	 * Once true, the component stays tick-disabled for the rest of its life
	 * (AC#4). Exposed read-only so tests can assert the guard fired.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	bool HasFailedOwnerCheck() const { return bOwnerCheckFailed; }

	/**
	 * Runs the AC#4 owner validation and applies the initial mode to the
	 * owner's root. Idempotent. Called from BeginPlay in gameplay; exposed
	 * publicly so automation tests can exercise the same code path without
	 * spinning up a world (the transient-package pattern).
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Orbital")
	void InitializeOwnerBinding();

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/**
	 * Resolve mu from (in order): GravitationalParameterOverride > 0,
	 * CurrentState.ParentBody->GravitationalParameter > 0, then Earth's mu as
	 * a last-resort fallback. The fallback logs once per component lifetime.
	 */
	double ResolveMu();

	/**
	 * Const counterpart that returns the resolved mu without emitting any
	 * diagnostics. Intended for const-qualified paths like
	 * ComputeInjectionVelocities; the tick path is responsible for logging
	 * misconfiguration via ResolveMu.
	 */
	double ResolveMuSilent() const;

	/**
	 * Pure lookup core shared by ResolveMu and ResolveMuSilent. Returns the
	 * resolved mu and reports via bOutUsedFallback whether the Earth-mu
	 * default had to be used. Does NOT log — callers decide.
	 */
	double ResolveMuCore(bool& bOutUsedFallback) const;

	/**
	 * Apply Mode to the owner's root primitive via SetSimulatePhysics. If the
	 * root is not a UPrimitiveComponent, logs a warning in Local mode (Chaos
	 * cannot drive a bare USceneComponent); silent in Rail mode.
	 */
	void ApplyModeToOwner();

	/**
	 * Single rail-tick kernel: propagate CurrentState by DeltaSeconds under
	 * resolved mu, convert to Cartesian, write SetActorLocation. On Kepler
	 * failure (non-finite Δt, hyperbolic state, ...) the component logs once
	 * and holds the last known transform — it does NOT overwrite with garbage.
	 */
	void TickRail(double DeltaSeconds);

	/** Set once in InitializeOwnerBinding after the AVehicle cast succeeds. */
	TWeakObjectPtr<class AVehicle> CachedOwner;

	/** true once AC#4 has fired; tick stays disabled for the actor's lifetime. */
	bool bOwnerCheckFailed = false;

	/** Throttle for the "fallback to Earth mu" warning. */
	bool bHasLoggedMuFallback = false;

	/** Throttle for the "Local without primitive root" warning. */
	bool bHasLoggedNoPrimitiveRoot = false;

	/** Throttle for the Kepler-failure warning so failing ticks don't spam. */
	bool bHasLoggedPropagationFailure = false;

	/** Throttle for the "SwitchToLocal while already Local" warning. */
	bool bHasLoggedAlreadyLocalSwitch = false;

	/** Throttle for the "SwitchToRail while already Rail" warning. */
	bool bHasLoggedAlreadyRailSwitch = false;

	/** Throttle for the near-zero-velocity degeneracy warning (AC#3). */
	bool bHasLoggedNearZeroVelocity = false;

	/** SI-unit linear velocity applied at the last successful SwitchToLocal. */
	UPROPERTY(Transient)
	FVector LastInjectedLinearVelocityMps = FVector::ZeroVector;

	/** SI-unit angular velocity applied at the last successful SwitchToLocal. */
	UPROPERTY(Transient)
	FVector LastInjectedAngularVelocityRadps = FVector::ZeroVector;
};
