// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Orbital/OrbitalState.h"
#include "OrbitalMath.generated.h"

/**
 * Pure f64 conversions between Cartesian state vectors (Pos, Vel) and classical
 * Keplerian elements (FOrbitalState).
 *
 * PRD: US-007. All arithmetic in f64. Inputs are SI (meters, m/s, m^3/s^2).
 *
 * The parent-body-attached flag on FOrbitalState is NOT written here — callers
 * assign ParentBody themselves after conversion. Mu is passed in so this module
 * has no dependency on US-010's SOI manager.
 */
UCLASS()
class DELTAV_API UOrbitalMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * State vector -> classical orbital elements.
	 *
	 * Degenerate-case handling per AC#3:
	 *  - e < SMALL_E (near-circular)           : ArgumentOfPeriapsis = 0, TrueAnomaly = argument of latitude
	 *  - additionally |n| < SMALL_N (equatorial): RightAscensionOfAscendingNode = 0, TrueAnomaly = true longitude
	 *
	 * @return false + UE_LOG Warning on zero / non-finite inputs, non-positive Mu,
	 *         or hyperbolic/parabolic orbits (e >= 1). OutState is zero-initialised
	 *         on failure to avoid leaking uninitialised memory to Blueprint callers.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Math")
	static bool StateVectorToElements(
		const FVector& Pos,
		const FVector& Vel,
		double Mu,
		FOrbitalState& OutState);

	/**
	 * Classical orbital elements -> state vector.
	 *
	 * @return false + UE_LOG Warning on non-positive Mu, non-finite elements,
	 *         or hyperbolic eccentricity (e >= 1). Outputs are zeroed on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Math")
	static bool ElementsToStateVector(
		const FOrbitalState& State,
		double Mu,
		FVector& OutPos,
		FVector& OutVel);

	/**
	 * Analytic 2-body (Kepler) propagation: advances TrueAnomaly in time while
	 * holding a, e, i, RAAN, ω constant; updates Epoch by DeltaSeconds.
	 *
	 * PRD: US-008. Reuses the US-006 Newton-Raphson solver.
	 *
	 * - DeltaSeconds == 0                : OutState is bit-equal to InState.
	 * - DeltaSeconds < 0                 : well-defined retrograde-in-time propagation
	 *                                      (Kepler propagation is time-reversible).
	 * - Near-circular (e < SMALL_E)      : ν = M, short-circuit conversions.
	 *
	 * @return false + UE_LOG Warning on non-positive Mu, non-positive SMA, hyperbolic e,
	 *         non-finite inputs, or Newton-Raphson non-convergence. OutState is zeroed
	 *         on failure (after writing zero-initialised state) for defensive callers.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Math")
	static bool PropagateKepler(
		const FOrbitalState& InState,
		double DeltaSeconds,
		double Mu,
		FOrbitalState& OutState);

	/**
	 * True anomaly -> mean anomaly (via eccentric anomaly), closed-form.
	 * Returned value is wrapped to [-pi, pi]. For e < SMALL_E (near-circular)
	 * returns TrueAnomaly wrapped unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Math")
	static double TrueAnomalyToMean(double TrueAnomaly, double Eccentricity);

	/**
	 * 2-body propagation using Kick-Drift-Kick Leapfrog — a 2nd-order
	 * symplectic integrator. Energy is bounded (oscillates, does not drift)
	 * which makes this the right fallback for long orbits and the foundation
	 * for perturbations (drag in US-049, etc.).
	 *
	 * PRD: US-009. Central gravity only; no drag, no J2.
	 *
	 * - DeltaSeconds == 0   : OutState bit-equal to InState.
	 * - DeltaSeconds < 0    : well-defined retrograde integration (time-reversible).
	 * - StepHz < 10         : clamped to 60 Hz + warning log (AC#4).
	 *
	 * @return false + UE_LOG Warning on validation failure or if the inner
	 *         RV↔COE conversions fail (e.g., integrator produced hyperbolic state).
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Math")
	static bool PropagateLeapfrog(
		const FOrbitalState& InState,
		double DeltaSeconds,
		double Mu,
		double StepHz,
		FOrbitalState& OutState);
};
