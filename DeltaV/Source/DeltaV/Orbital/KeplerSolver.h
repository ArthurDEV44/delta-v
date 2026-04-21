// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KeplerSolver.generated.h"

/** Outcome of a Kepler-equation solve. */
UENUM(BlueprintType)
enum class EKeplerSolverStatus : uint8
{
	/** Newton-Raphson converged within tolerance. */
	Success,

	/** Eccentricity >= 1.0 (parabolic / hyperbolic) — unsupported in this solver. */
	HyperbolicNotSupported,

	/** Eccentricity outside [0, 1), negative, or NaN. */
	InvalidEccentricity,

	/** Mean anomaly is NaN or non-finite. */
	InvalidMeanAnomaly,

	/** Did not reach tolerance within MaxIterations. */
	DidNotConverge
};

/**
 * Newton-Raphson solver for Kepler's equation M = E - e * sin(E).
 *
 * PRD: US-006. All arithmetic in f64. Must converge for e in [0, 0.99].
 */
UCLASS()
class DELTAV_API UKeplerSolver : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Solve Kepler's equation for the eccentric anomaly.
	 *
	 * @param MeanAnomaly           Input M (radians). Wrapped internally to [-pi, pi].
	 * @param Eccentricity          Input e. Must be in [0, 1).
	 * @param OutEccentricAnomaly   On Success, the eccentric anomaly E (radians).
	 * @param OutResidual           |E - e*sin(E) - M| at termination (always set).
	 * @param OutIterations         Number of Newton-Raphson iterations executed.
	 * @param Tolerance             Convergence threshold on residual. Default 1e-12.
	 * @param MaxIterations         Hard cap on iterations. Default 50.
	 * @return EKeplerSolverStatus  Success, or an explicit error code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Kepler")
	static EKeplerSolverStatus SolveEquation(
		double MeanAnomaly,
		double Eccentricity,
		double& OutEccentricAnomaly,
		double& OutResidual,
		int32& OutIterations,
		double Tolerance = 1e-12,
		int32 MaxIterations = 50);
};
