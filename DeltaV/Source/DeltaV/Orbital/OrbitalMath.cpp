// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orbital/OrbitalMath.h"

#include "DeltaV.h"
#include "Orbital/KeplerSolver.h"

namespace
{
	// Numerical thresholds for degenerate-case branching. Chosen well below the
	// PRD AC#3 threshold (1e-4) so the fallback path is only taken when the
	// classical formulas are actually ill-conditioned.
	// Thresholds for the degenerate-case branching. Raised above f64 noise so
	// the fallbacks actually fire before the classical formulas become
	// ill-conditioned. Still 2 orders of magnitude below the PRD AC#3 threshold
	// (1e-4) so the typical "eccentricity worth naming" orbit keeps the main path.
	constexpr double KSmallEccentricity = 1e-6;
	constexpr double KSmallNodeMagnitude = 1e-6;

	/** Clamp x to [-1, 1] before feeding to acos to protect against f64 roundoff. */
	FORCEINLINE double SafeAcos(double X)
	{
		return FMath::Acos(FMath::Clamp(X, -1.0, 1.0));
	}

	/** |v|^2 without the sqrt. */
	FORCEINLINE double SizeSq(const FVector& V)
	{
		return V.X * V.X + V.Y * V.Y + V.Z * V.Z;
	}
}

bool UOrbitalMath::StateVectorToElements(
	const FVector& Pos,
	const FVector& Vel,
	const double Mu,
	FOrbitalState& OutState)
{
	OutState = FOrbitalState();

	if (!FMath::IsFinite(Mu) || Mu <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::StateVectorToElements: invalid Mu %.17g."), Mu);
		return false;
	}

	if (!FMath::IsFinite(Pos.X) || !FMath::IsFinite(Pos.Y) || !FMath::IsFinite(Pos.Z)
		|| !FMath::IsFinite(Vel.X) || !FMath::IsFinite(Vel.Y) || !FMath::IsFinite(Vel.Z))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::StateVectorToElements: non-finite Pos/Vel components."));
		return false;
	}

	const double R2 = SizeSq(Pos);
	const double V2 = SizeSq(Vel);

	if (R2 <= UE_DOUBLE_SMALL_NUMBER || V2 <= UE_DOUBLE_SMALL_NUMBER)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::StateVectorToElements: zero Pos or Vel (|r|^2=%.3e, |v|^2=%.3e)."),
			R2, V2);
		return false;
	}

	const double R = FMath::Sqrt(R2);

	// Angular momentum h = r x v.
	const FVector H = FVector::CrossProduct(Pos, Vel);
	const double HMag = H.Length();
	if (HMag <= UE_DOUBLE_SMALL_NUMBER)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::StateVectorToElements: rectilinear trajectory (|h|=%.3e)."), HMag);
		return false;
	}

	// Specific energy and semi-major axis. Elliptic: Energy < 0 => a > 0.
	const double Energy = 0.5 * V2 - Mu / R;
	if (Energy >= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::StateVectorToElements: parabolic/hyperbolic energy %.17g; not supported."),
			Energy);
		return false;
	}
	const double SemiMajor = -Mu / (2.0 * Energy);

	// Eccentricity vector: e_vec = (v x h)/Mu - r/|r|.
	const FVector VCrossH = FVector::CrossProduct(Vel, H);
	const FVector EVec = VCrossH / Mu - Pos / R;
	const double EMag = EVec.Length();

	// Inclination: i = acos(h_z / |h|).
	const double Inclination = SafeAcos(H.Z / HMag);

	// Node vector n = K x h  where K = (0, 0, 1).
	const FVector NodeVec(-H.Y, H.X, 0.0);
	const double NMag = NodeVec.Length();

	// RAAN (Omega).
	double RAAN = 0.0;
	if (NMag > KSmallNodeMagnitude)
	{
		RAAN = FMath::Atan2(NodeVec.Y, NodeVec.X);
		if (RAAN < 0.0)
		{
			RAAN += UE_DOUBLE_TWO_PI;
		}
	}

	// Argument of periapsis (omega) and true anomaly (nu) — with degenerate-case
	// fallbacks per AC#3.
	double ArgPeri = 0.0;
	double TrueAnomaly = 0.0;

	const bool bCircular = (EMag < KSmallEccentricity);
	const bool bEquatorial = (NMag <= KSmallNodeMagnitude);

	if (!bCircular && !bEquatorial)
	{
		// Standard case.
		const double CosOmega = FVector::DotProduct(NodeVec, EVec) / (NMag * EMag);
		ArgPeri = SafeAcos(CosOmega);
		if (EVec.Z < 0.0)
		{
			ArgPeri = UE_DOUBLE_TWO_PI - ArgPeri;
		}

		const double CosNu = FVector::DotProduct(EVec, Pos) / (EMag * R);
		TrueAnomaly = SafeAcos(CosNu);
		if (FVector::DotProduct(Pos, Vel) < 0.0)
		{
			TrueAnomaly = UE_DOUBLE_TWO_PI - TrueAnomaly;
		}
	}
	else if (!bCircular && bEquatorial)
	{
		// Equatorial elliptical: RAAN undefined (set 0), encode ω as longitude of
		// periapsis measured from X axis. For retrograde (H.Z < 0) we mirror the
		// signed atan2 BEFORE wrapping into [0, 2π) so the final value is
		// quadrant-correct regardless of sign.
		double OmegaRaw = FMath::Atan2(EVec.Y, EVec.X);
		if (H.Z < 0.0)
		{
			OmegaRaw = -OmegaRaw;
		}
		if (OmegaRaw < 0.0)
		{
			OmegaRaw += UE_DOUBLE_TWO_PI;
		}
		ArgPeri = OmegaRaw;

		const double CosNu = FVector::DotProduct(EVec, Pos) / (EMag * R);
		TrueAnomaly = SafeAcos(CosNu);
		if (FVector::DotProduct(Pos, Vel) < 0.0)
		{
			TrueAnomaly = UE_DOUBLE_TWO_PI - TrueAnomaly;
		}
	}
	else if (bCircular && !bEquatorial)
	{
		// Circular inclined: ω undefined (set 0), encode ν as argument of latitude.
		const double CosU = FVector::DotProduct(NodeVec, Pos) / (NMag * R);
		TrueAnomaly = SafeAcos(CosU);
		if (Pos.Z < 0.0)
		{
			TrueAnomaly = UE_DOUBLE_TWO_PI - TrueAnomaly;
		}
	}
	else
	{
		// Circular equatorial: both ω and Ω undefined. Encode ν as true longitude
		// measured from X axis. Retrograde mirror applied before the [0, 2π) wrap
		// so quadrants stay correct.
		double LambdaRaw = FMath::Atan2(Pos.Y, Pos.X);
		if (H.Z < 0.0)
		{
			LambdaRaw = -LambdaRaw;
		}
		if (LambdaRaw < 0.0)
		{
			LambdaRaw += UE_DOUBLE_TWO_PI;
		}
		TrueAnomaly = LambdaRaw;
	}

	OutState.SemiMajorAxis = SemiMajor;
	OutState.Eccentricity = EMag;
	OutState.Inclination = Inclination;
	OutState.RightAscensionOfAscendingNode = RAAN;
	OutState.ArgumentOfPeriapsis = ArgPeri;
	OutState.TrueAnomaly = TrueAnomaly;
	return true;
}

bool UOrbitalMath::ElementsToStateVector(
	const FOrbitalState& State,
	const double Mu,
	FVector& OutPos,
	FVector& OutVel)
{
	OutPos = FVector::ZeroVector;
	OutVel = FVector::ZeroVector;

	if (!FMath::IsFinite(Mu) || Mu <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::ElementsToStateVector: invalid Mu %.17g."), Mu);
		return false;
	}

	const double A = State.SemiMajorAxis;
	const double E = State.Eccentricity;
	const double I = State.Inclination;
	const double RAAN = State.RightAscensionOfAscendingNode;
	const double ArgPeri = State.ArgumentOfPeriapsis;
	const double Nu = State.TrueAnomaly;

	if (!FMath::IsFinite(A) || !FMath::IsFinite(E) || !FMath::IsFinite(I)
		|| !FMath::IsFinite(RAAN) || !FMath::IsFinite(ArgPeri) || !FMath::IsFinite(Nu))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::ElementsToStateVector: non-finite element(s)."));
		return false;
	}

	if (E < 0.0 || E >= 1.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::ElementsToStateVector: eccentricity %.17g not in [0, 1)."), E);
		return false;
	}

	if (A <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::ElementsToStateVector: non-positive semi-major axis %.17g."), A);
		return false;
	}

	// Semi-latus rectum p = a(1 - e^2). For e in [0, 1) this is strictly positive.
	const double P = A * (1.0 - E * E);
	const double CosNu = FMath::Cos(Nu);
	const double SinNu = FMath::Sin(Nu);
	const double OnePlusECosNu = 1.0 + E * CosNu;

	if (OnePlusECosNu <= UE_DOUBLE_SMALL_NUMBER)
	{
		// Degenerate for a parabolic/hyperbolic trajectory (already rejected above)
		// but also arbitrary-precision catastrophe guard.
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::ElementsToStateVector: degenerate 1+e*cos(nu)=%.3e."),
			OnePlusECosNu);
		return false;
	}

	// Position / velocity in the perifocal (PQW) frame.
	const double RPqw = P / OnePlusECosNu;
	const FVector RPerifocal(RPqw * CosNu, RPqw * SinNu, 0.0);

	const double SqrtMuOverP = FMath::Sqrt(Mu / P);
	const FVector VPerifocal(-SqrtMuOverP * SinNu, SqrtMuOverP * (E + CosNu), 0.0);

	// Rotation from PQW to inertial:  R = R3(-RAAN) * R1(-i) * R3(-ArgPeri)
	// Pre-compute sines / cosines.
	const double CosRAAN = FMath::Cos(RAAN);
	const double SinRAAN = FMath::Sin(RAAN);
	const double CosI = FMath::Cos(I);
	const double SinI = FMath::Sin(I);
	const double CosW = FMath::Cos(ArgPeri);
	const double SinW = FMath::Sin(ArgPeri);

	// Only the first two columns of the PQW->ECI rotation are used because
	// the perifocal Z component is always zero by construction.
	const double M11 = CosRAAN * CosW - SinRAAN * SinW * CosI;
	const double M12 = -CosRAAN * SinW - SinRAAN * CosW * CosI;

	const double M21 = SinRAAN * CosW + CosRAAN * SinW * CosI;
	const double M22 = -SinRAAN * SinW + CosRAAN * CosW * CosI;

	const double M31 = SinW * SinI;
	const double M32 = CosW * SinI;

	OutPos = FVector(
		M11 * RPerifocal.X + M12 * RPerifocal.Y,
		M21 * RPerifocal.X + M22 * RPerifocal.Y,
		M31 * RPerifocal.X + M32 * RPerifocal.Y);

	OutVel = FVector(
		M11 * VPerifocal.X + M12 * VPerifocal.Y,
		M21 * VPerifocal.X + M22 * VPerifocal.Y,
		M31 * VPerifocal.X + M32 * VPerifocal.Y);

	return true;
}

namespace
{
	/** Wrap angle to [-pi, pi] in double precision. Mirrors the solver's WrapPiMath. */
	double WrapPiMath(const double Angle)
	{
		double Wrapped = FMath::Fmod(Angle + UE_DOUBLE_PI, UE_DOUBLE_TWO_PI);
		if (Wrapped < 0.0)
		{
			Wrapped += UE_DOUBLE_TWO_PI;
		}
		return Wrapped - UE_DOUBLE_PI;
	}
}

double UOrbitalMath::TrueAnomalyToMean(const double TrueAnomaly, const double Eccentricity)
{
	if (!FMath::IsFinite(TrueAnomaly) || !FMath::IsFinite(Eccentricity)
		|| Eccentricity < 0.0 || Eccentricity >= 1.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::TrueAnomalyToMean: invalid input (nu=%.17g, e=%.17g); returning wrapped nu."),
			TrueAnomaly, Eccentricity);
		return WrapPiMath(TrueAnomaly);
	}

	// Half-angle form is bit-exact at e=0 (both sqrts evaluate to 1), so no
	// small-e short-circuit is needed; running the full formula also avoids
	// the O(e) phase discontinuity at any threshold boundary.
	//     E = 2 * atan2( sqrt(1-e) * sin(nu/2), sqrt(1+e) * cos(nu/2) )
	const double HalfNu = 0.5 * TrueAnomaly;
	const double SqrtOneMinusE = FMath::Sqrt(1.0 - Eccentricity);
	const double SqrtOnePlusE = FMath::Sqrt(1.0 + Eccentricity);
	const double E = 2.0 * FMath::Atan2(
		SqrtOneMinusE * FMath::Sin(HalfNu),
		SqrtOnePlusE * FMath::Cos(HalfNu));

	// M = E - e * sin(E)
	const double Mean = E - Eccentricity * FMath::Sin(E);
	return WrapPiMath(Mean);
}

bool UOrbitalMath::PropagateKepler(
	const FOrbitalState& InState,
	const double DeltaSeconds,
	const double Mu,
	FOrbitalState& OutState)
{
	// AC#3: DeltaSeconds == 0 -> bit-equal passthrough. Do this BEFORE any
	// validation so identity propagation never touches floating-point state.
	if (DeltaSeconds == 0.0)
	{
		OutState = InState;
		return true;
	}

	OutState = FOrbitalState();

	if (!FMath::IsFinite(Mu) || Mu <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: invalid Mu %.17g."), Mu);
		return false;
	}

	if (!FMath::IsFinite(DeltaSeconds))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: non-finite DeltaSeconds."));
		return false;
	}

	const double A = InState.SemiMajorAxis;
	const double E = InState.Eccentricity;
	const double Nu0 = InState.TrueAnomaly;

	if (!FMath::IsFinite(A) || !FMath::IsFinite(E) || !FMath::IsFinite(Nu0)
		|| !FMath::IsFinite(InState.Inclination)
		|| !FMath::IsFinite(InState.RightAscensionOfAscendingNode)
		|| !FMath::IsFinite(InState.ArgumentOfPeriapsis)
		|| !FMath::IsFinite(InState.Epoch))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: non-finite element(s) in InState."));
		return false;
	}

	if (A <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: non-positive semi-major axis %.17g."), A);
		return false;
	}

	if (E < 0.0 || E >= 1.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: eccentricity %.17g not in [0, 1)."), E);
		return false;
	}

	// Mean motion n = sqrt(mu / a^3). Guard against overflow for pathologically
	// large SMA: A^3 can exceed DBL_MAX for A > ~6e102, producing Inf -> NaN.
	const double N = FMath::Sqrt(Mu / (A * A * A));
	if (!FMath::IsFinite(N))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: non-finite mean motion (A=%.17g, Mu=%.17g)."),
			A, Mu);
		return false;
	}

	// Advance mean anomaly. Wrap to [-pi, pi] before handing to the solver so
	// large |n*DeltaSeconds| (many revolutions) doesn't degrade Newton convergence.
	const double M0 = TrueAnomalyToMean(Nu0, E);
	const double M1 = WrapPiMath(M0 + N * DeltaSeconds);

	// Always run through the solver; US-006 handles e == 0.0 as a bit-exact
	// short-circuit internally, so circular orbits pay no cost and we avoid
	// the phase discontinuity a custom near-circular approximation would
	// introduce at any threshold boundary.
	double EccentricAnomaly = 0.0;
	double Residual = 0.0;
	int32 Iterations = 0;
	const EKeplerSolverStatus Status = UKeplerSolver::SolveEquation(
		M1, E, EccentricAnomaly, Residual, Iterations);
	if (Status != EKeplerSolverStatus::Success)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateKepler: solver failed (status=%d, M=%.17g, e=%.17g, iter=%d, residual=%.17g)."),
			static_cast<int32>(Status), M1, E, Iterations, Residual);
		return false;
	}

	// nu = 2 * atan2( sqrt(1+e) * sin(E/2), sqrt(1-e) * cos(E/2) )
	const double HalfE = 0.5 * EccentricAnomaly;
	const double SqrtOnePlusE = FMath::Sqrt(1.0 + E);
	const double SqrtOneMinusE = FMath::Sqrt(1.0 - E);
	const double NewTrueAnomaly = 2.0 * FMath::Atan2(
		SqrtOnePlusE * FMath::Sin(HalfE),
		SqrtOneMinusE * FMath::Cos(HalfE));

	OutState = InState;
	OutState.TrueAnomaly = NewTrueAnomaly;
	OutState.Epoch = InState.Epoch + DeltaSeconds;
	return true;
}

namespace
{
	/** Central-gravity acceleration a = -Mu * r / |r|^3. */
	FORCEINLINE FVector CentralAcceleration(const FVector& R, const double Mu)
	{
		const double R2 = R.X * R.X + R.Y * R.Y + R.Z * R.Z;
		const double RMag = FMath::Sqrt(R2);
		const double Inv = Mu / (R2 * RMag);
		return FVector(-R.X * Inv, -R.Y * Inv, -R.Z * Inv);
	}
}

bool UOrbitalMath::PropagateLeapfrog(
	const FOrbitalState& InState,
	const double DeltaSeconds,
	const double Mu,
	const double StepHz,
	FOrbitalState& OutState)
{
	// Snapshot the full input so aliasing (In == Out) is safe: every downstream
	// read goes through the local, and OutState zero-init never clobbers us.
	const FOrbitalState SnapshotIn = InState;

	// AC#3 (mirror of US-008): DeltaSeconds == 0 -> bit-equal passthrough,
	// before any floating-point arithmetic.
	if (DeltaSeconds == 0.0)
	{
		OutState = SnapshotIn;
		return true;
	}

	OutState = FOrbitalState();

	if (!FMath::IsFinite(Mu) || Mu <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateLeapfrog: invalid Mu %.17g."), Mu);
		return false;
	}

	if (!FMath::IsFinite(DeltaSeconds))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateLeapfrog: non-finite DeltaSeconds."));
		return false;
	}

	if (!FMath::IsFinite(StepHz))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateLeapfrog: non-finite StepHz."));
		return false;
	}

	// AC#4: StepHz below 10 Hz is unstable for LEO -> clamp to 60 Hz minimum.
	// Warn once per process to avoid log flooding on per-tick misuse.
	double EffectiveHz = StepHz;
	if (StepHz < 10.0)
	{
		static bool bWarnedLowStepHz = false;
		if (!bWarnedLowStepHz)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("OrbitalMath::PropagateLeapfrog: StepHz=%.3f below 10 Hz minimum; clamping to 60 Hz. (Further occurrences suppressed.)"),
				StepHz);
			bWarnedLowStepHz = true;
		}
		EffectiveHz = 60.0;
	}

	// Build initial Cartesian state from the incoming elements. This also
	// validates e in [0,1), a > 0, finite fields — no duplicate checks here.
	FVector R, V;
	if (!ElementsToStateVector(SnapshotIn, Mu, R, V))
	{
		// ElementsToStateVector already logged; nothing to add.
		return false;
	}

	// Substep count. For |Δt| > 0 the ceil guarantees |h| <= 1 / EffectiveHz.
	// Upper bound prevents DoS from pathological caller inputs and avoids
	// undefined-behaviour on the double->int64 cast (UB for values outside
	// [INT64_MIN, INT64_MAX]).
	constexpr double KMaxSubsteps = 1.0e9;  // ~193 yr at 60 Hz, 31 yr at 1 kHz.
	const double StepsReal = FMath::Abs(DeltaSeconds) * EffectiveHz;
	if (StepsReal > KMaxSubsteps)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateLeapfrog: |Δt|*StepHz=%.3e exceeds substep cap %.0e (DeltaSeconds=%.3e, StepHz=%.3f)."),
			StepsReal, KMaxSubsteps, DeltaSeconds, EffectiveHz);
		return false;
	}
	const int64 N = FMath::Max<int64>(1, static_cast<int64>(FMath::CeilToDouble(StepsReal)));
	const double H = DeltaSeconds / static_cast<double>(N);  // signed
	const double HalfH = 0.5 * H;

	// Kick-Drift-Kick Leapfrog. Reuse the acceleration at the end of one step
	// as the "kick 1" of the next (would save a sqrt per step) — but the clarity
	// of the explicit form matters more than shaving 5.5 M sqrts, and the
	// compiler inlines CentralAcceleration. Keep the explicit form.
	for (int64 I = 0; I < N; ++I)
	{
		FVector A = CentralAcceleration(R, Mu);
		V += HalfH * A;
		R += H * V;
		A = CentralAcceleration(R, Mu);
		V += HalfH * A;
	}

	if (!FMath::IsFinite(R.X) || !FMath::IsFinite(R.Y) || !FMath::IsFinite(R.Z)
		|| !FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y) || !FMath::IsFinite(V.Z))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("OrbitalMath::PropagateLeapfrog: non-finite integrator state after %lld steps."), N);
		OutState = FOrbitalState();
		return false;
	}

	if (!StateVectorToElements(R, V, Mu, OutState))
	{
		// StateVectorToElements already logged; mirror the failure.
		return false;
	}

	OutState.Epoch = SnapshotIn.Epoch + DeltaSeconds;
	OutState.ParentBody = SnapshotIn.ParentBody;
	return true;
}
