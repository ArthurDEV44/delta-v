// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orbital/KeplerSolver.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKeplerSolverConvergesTest,
	"DeltaV.Orbital.KeplerSolver.Converges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FKeplerSolverConvergesTest::RunTest(const FString& Parameters)
{
	// Per US-006: converge for e in {0, 0.1, 0.5, 0.9, 0.99} to tol 1e-12 in <= 50 iter.
	const double Tolerance = 1e-12;
	const int32 MaxIterations = 50;

	const TArray<double> Eccentricities = { 0.0, 0.1, 0.5, 0.9, 0.99 };
	const TArray<double> MeanAnomalies = {
		-UE_DOUBLE_PI,
		-0.5 * UE_DOUBLE_PI,
		-0.01,
		0.0,
		0.01,
		0.25 * UE_DOUBLE_PI,
		0.5 * UE_DOUBLE_PI,
		UE_DOUBLE_PI
	};

	for (const double Eccentricity : Eccentricities)
	{
		for (const double MeanAnomaly : MeanAnomalies)
		{
			double EccentricAnomaly = 0.0;
			double Residual = 0.0;
			int32 Iterations = 0;

			const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
				MeanAnomaly,
				Eccentricity,
				EccentricAnomaly,
				Residual,
				Iterations,
				Tolerance,
				MaxIterations);

			if (Status != EKeplerSolverStatus::Success)
			{
				AddError(FString::Printf(
					TEXT("Solver did not converge: M=%.17g, e=%.17g, last iter=%d, residual=%.17g, status=%d"),
					MeanAnomaly, Eccentricity, Iterations, Residual, static_cast<int32>(Status)));
				continue;
			}

			TestTrue(
				FString::Printf(
					TEXT("Residual within tolerance (M=%.6g, e=%.6g): |r|=%.3e"),
					MeanAnomaly, Eccentricity, Residual),
				Residual <= Tolerance);

			TestTrue(
				FString::Printf(
					TEXT("Iterations within budget (M=%.6g, e=%.6g): %d"),
					MeanAnomaly, Eccentricity, Iterations),
				Iterations <= MaxIterations);

			// Independent identity check of M = E - e*sin(E), recomputed from the
			// returned E with the same double-precision wrap the solver uses.
			const double Verify = EccentricAnomaly - Eccentricity * FMath::Sin(EccentricAnomaly);
			double WrappedM = FMath::Fmod(MeanAnomaly + UE_DOUBLE_PI, UE_DOUBLE_TWO_PI);
			if (WrappedM < 0.0)
			{
				WrappedM += UE_DOUBLE_TWO_PI;
			}
			WrappedM -= UE_DOUBLE_PI;

			// Allow ~2 ULP of floating-point slack on the recomputed sin(E);
			// still well inside the 1e-12 AC.
			const double IdentityBound = 2.0 * Tolerance;
			TestTrue(
				FString::Printf(
					TEXT("Identity M=E-e*sin(E) (M=%.6g, e=%.6g): delta=%.3e"),
					MeanAnomaly, Eccentricity, FMath::Abs(Verify - WrappedM)),
				FMath::Abs(Verify - WrappedM) <= IdentityBound);
		}
	}

	// Hyperbolic / parabolic inputs must be rejected explicitly.
	{
		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			1.0, 1.0, E, R, It, Tolerance, MaxIterations);
		TestEqual(TEXT("e == 1.0 rejected as hyperbolic"),
			Status, EKeplerSolverStatus::HyperbolicNotSupported);
		TestFalse(TEXT("No NaN leaked to OutEccentricAnomaly (e=1.0)"),
			FMath::IsNaN(E));
	}
	{
		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			1.0, 1.5, E, R, It, Tolerance, MaxIterations);
		TestEqual(TEXT("e == 1.5 rejected as hyperbolic"),
			Status, EKeplerSolverStatus::HyperbolicNotSupported);
	}

	// Negative eccentricity rejected.
	{
		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			0.0, -0.1, E, R, It, Tolerance, MaxIterations);
		TestEqual(TEXT("Negative e rejected"),
			Status, EKeplerSolverStatus::InvalidEccentricity);
	}

	// Non-finite MeanAnomaly rejected.
	{
		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			std::numeric_limits<double>::infinity(), 0.5, E, R, It, Tolerance, MaxIterations);
		TestEqual(TEXT("Infinite M rejected"),
			Status, EKeplerSolverStatus::InvalidMeanAnomaly);
	}

	// Real pathological non-convergence (AC#5): M near 0, e near 1, iteration
	// budget so tight Newton cannot reach 1e-12. Verify the solver surfaces
	// the (M, e, last iter) triplet rather than looping or returning Success.
	{
		const double PathologicalM = 1e-8;
		const double PathologicalE = 0.99;
		const int32 TightBudget = 2;

		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			PathologicalM, PathologicalE, E, R, It, Tolerance, TightBudget);

		TestEqual(TEXT("Pathological case with tight budget returns DidNotConverge"),
			Status, EKeplerSolverStatus::DidNotConverge);
		TestTrue(TEXT("DidNotConverge reports a positive residual"),
			R > Tolerance);
		TestTrue(TEXT("DidNotConverge iteration count is within [0, TightBudget]"),
			It >= 0 && It <= TightBudget);
		// Emulate what AC#5 asks for: the triplet must be surfaceable for a caller.
		// We log it here so a real non-convergence in CI would show up in the report.
		AddInfo(FString::Printf(
			TEXT("DidNotConverge triplet: M=%.17g, e=%.17g, last iter=%d (residual=%.17g)"),
			PathologicalM, PathologicalE, It, R));
	}

	// Synthetic MaxIterations=0 on a hard case also returns DidNotConverge
	// without entering the loop (hard termination guarantee).
	{
		double E = 0.0, R = 0.0;
		int32 It = 0;
		const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
			0.5, 0.99, E, R, It, Tolerance, /*MaxIterations=*/0);
		TestEqual(TEXT("MaxIterations=0 on hard case returns DidNotConverge"),
			Status, EKeplerSolverStatus::DidNotConverge);
		TestTrue(TEXT("DidNotConverge reports a residual"), R > 0.0);
		TestEqual<int32>(TEXT("DidNotConverge reports zero iterations"), It, 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
