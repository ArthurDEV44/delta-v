// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/URocketDef.h"

namespace
{
	bool IsVectorFinite(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	bool IsInertiaDiagonalValid(const FVector& V)
	{
		return IsVectorFinite(V) && V.X >= 0.0 && V.Y >= 0.0 && V.Z >= 0.0;
	}

	// Isp floor — matches UPROPERTY meta (ClampMin = "1.0"). A denormal Isp
	// would give mass-flow ≈ Thrust / denormal → Inf, so we reject anything
	// below a sane physical floor here as well.
	constexpr double KMinPhysicalIspSeconds = 1.0;
}

bool URocketDef::IsValid(FString* OutError) const
{
	const auto Fail = [OutError](const FString& Msg) -> bool
		{
			if (OutError != nullptr)
			{
				*OutError = Msg;
			}
			return false;
		};

	if (Stages.Num() == 0)
	{
		return Fail(FString::Printf(TEXT("URocketDef '%s' has zero stages"), *GetName()));
	}

	if (!FMath::IsFinite(PayloadDryMassKg) || PayloadDryMassKg < 0.0)
	{
		return Fail(FString::Printf(
			TEXT("URocketDef '%s' has invalid PayloadDryMassKg=%.17g"),
			*GetName(), PayloadDryMassKg));
	}

	if (!IsVectorFinite(PayloadMountOffsetCm))
	{
		return Fail(FString::Printf(
			TEXT("URocketDef '%s' PayloadMountOffsetCm is non-finite"),
			*GetName()));
	}

	if (!IsInertiaDiagonalValid(PayloadInertiaDiagonalKgM2))
	{
		return Fail(FString::Printf(
			TEXT("URocketDef '%s' PayloadInertiaDiagonalKgM2 is non-finite or negative"),
			*GetName()));
	}

	for (int32 Idx = 0; Idx < Stages.Num(); ++Idx)
	{
		const FStageDef& S = Stages[Idx];

		if (!FMath::IsFinite(S.DryMassKg) || S.DryMassKg <= 0.0)
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' has invalid DryMassKg=%.17g (must be > 0)"),
				*GetName(), Idx, *S.StageName.ToString(), S.DryMassKg));
		}

		if (!FMath::IsFinite(S.FuelMassKg) || S.FuelMassKg < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' has invalid FuelMassKg=%.17g"),
				*GetName(), Idx, *S.StageName.ToString(), S.FuelMassKg));
		}

		if (!FMath::IsFinite(S.SpecificImpulseSeconds) || S.SpecificImpulseSeconds < KMinPhysicalIspSeconds)
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' has invalid Isp=%.17g (must be >= %.1f s)"),
				*GetName(), Idx, *S.StageName.ToString(), S.SpecificImpulseSeconds, KMinPhysicalIspSeconds));
		}

		if (!FMath::IsFinite(S.MaxThrustNewtons) || S.MaxThrustNewtons < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' has invalid MaxThrustNewtons=%.17g"),
				*GetName(), Idx, *S.StageName.ToString(), S.MaxThrustNewtons));
		}

		if (!IsVectorFinite(S.LocalMountOffsetCm))
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' LocalMountOffsetCm is non-finite"),
				*GetName(), Idx, *S.StageName.ToString()));
		}

		if (!IsInertiaDiagonalValid(S.LocalInertiaDiagonalKgM2))
		{
			return Fail(FString::Printf(
				TEXT("URocketDef '%s' stage[%d] '%s' LocalInertiaDiagonalKgM2 is non-finite or negative"),
				*GetName(), Idx, *S.StageName.ToString()));
		}
	}

	return true;
}
