// Copyright Epic Games, Inc. All Rights Reserved.

#include "Base/CelestialBody.h"

#include "DeltaV.h"

double UCelestialBody::ComputeLaplaceSOI(
	const double SemiMajorAxis,
	const double Mu,
	const double ParentMu)
{
	if (!FMath::IsFinite(SemiMajorAxis) || SemiMajorAxis <= 0.0
		|| !FMath::IsFinite(Mu) || Mu <= 0.0
		|| !FMath::IsFinite(ParentMu) || ParentMu <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UCelestialBody::ComputeLaplaceSOI: invalid args (a=%.17g, mu=%.17g, parentMu=%.17g)."),
			SemiMajorAxis, Mu, ParentMu);
		return 0.0;
	}
	// r_SOI = a * (mu / parentMu)^(2/5)
	return SemiMajorAxis * FMath::Pow(Mu / ParentMu, 2.0 / 5.0);
}
