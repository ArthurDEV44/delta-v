// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orbital/KeplerSolver.h"

#include "DeltaV.h"

namespace
{
	/** Wrap angle to [-pi, pi] in double precision. */
	double WrapPi(double Angle)
	{
		double Wrapped = FMath::Fmod(Angle + UE_DOUBLE_PI, UE_DOUBLE_TWO_PI);
		if (Wrapped < 0.0)
		{
			Wrapped += UE_DOUBLE_TWO_PI;
		}
		return Wrapped - UE_DOUBLE_PI;
	}
}

EKeplerSolverStatus UKeplerSolver::SolveEquation(
	const double MeanAnomaly,
	const double Eccentricity,
	double& OutEccentricAnomaly,
	double& OutResidual,
	int32& OutIterations,
	const double Tolerance,
	const int32 MaxIterations)
{
	OutEccentricAnomaly = 0.0;
	OutResidual = TNumericLimits<double>::Max();
	OutIterations = 0;

	if (FMath::IsNaN(Eccentricity) || Eccentricity < 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("KeplerSolver: invalid eccentricity %.17g (must be >= 0)."),
			Eccentricity);
		return EKeplerSolverStatus::InvalidEccentricity;
	}

	if (Eccentricity >= 1.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("KeplerSolver: eccentricity %.17g is parabolic/hyperbolic; not supported."),
			Eccentricity);
		return EKeplerSolverStatus::HyperbolicNotSupported;
	}

	if (!FMath::IsFinite(MeanAnomaly))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("KeplerSolver: non-finite MeanAnomaly %.17g."),
			MeanAnomaly);
		return EKeplerSolverStatus::InvalidMeanAnomaly;
	}

	const double M = WrapPi(MeanAnomaly);

	// Circular case: E == M (bit-exact).
	if (Eccentricity == 0.0)
	{
		OutEccentricAnomaly = M;
		OutResidual = 0.0;
		OutIterations = 0;
		return EKeplerSolverStatus::Success;
	}

	// M == 0 => E == 0 is the exact solution for any e; short-circuit to avoid
	// an unnecessary Danby seed for the symmetric case.
	if (M == 0.0)
	{
		OutEccentricAnomaly = 0.0;
		OutResidual = 0.0;
		OutIterations = 0;
		return EKeplerSolverStatus::Success;
	}

	// Initial guess: E0 = M + e*sin(M) for moderate e, or a Danby-style
	// E0 = M + 0.85 * sign(M) * e for high e (robust convergence near e -> 1).
	double E;
	if (Eccentricity < 0.8)
	{
		E = M + Eccentricity * FMath::Sin(M);
	}
	else
	{
		const double SignM = (M > 0.0) ? 1.0 : -1.0;
		E = M + 0.85 * SignM * Eccentricity;
	}

	double Residual = E - Eccentricity * FMath::Sin(E) - M;

	int32 Iteration = 0;
	for (; Iteration < MaxIterations; ++Iteration)
	{
		if (FMath::Abs(Residual) <= Tolerance)
		{
			break;
		}

		const double Derivative = 1.0 - Eccentricity * FMath::Cos(E);
		// For e in [0, 1) the derivative stays >= 1 - e > 0 mathematically;
		// guard against underflow / non-finite values just in case.
		if (FMath::Abs(Derivative) < UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(Derivative))
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("KeplerSolver: singular derivative at iter %d (M=%.17g, e=%.17g)."),
				Iteration, MeanAnomaly, Eccentricity);
			OutEccentricAnomaly = E;
			OutResidual = FMath::Abs(Residual);
			OutIterations = Iteration;
			return EKeplerSolverStatus::DidNotConverge;
		}

		E -= Residual / Derivative;
		Residual = E - Eccentricity * FMath::Sin(E) - M;
	}

	OutEccentricAnomaly = E;
	OutResidual = FMath::Abs(Residual);
	OutIterations = Iteration;

	if (!FMath::IsFinite(E) || !FMath::IsFinite(Residual))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("KeplerSolver: non-finite result (M=%.17g, e=%.17g, iter=%d)."),
			MeanAnomaly, Eccentricity, Iteration);
		return EKeplerSolverStatus::DidNotConverge;
	}

	return (OutResidual <= Tolerance)
		? EKeplerSolverStatus::Success
		: EKeplerSolverStatus::DidNotConverge;
}
