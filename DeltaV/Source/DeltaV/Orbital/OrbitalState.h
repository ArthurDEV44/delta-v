// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "OrbitalState.generated.h"

class UCelestialBody;

/**
 * Classical Keplerian orbital elements at an instant (epoch).
 * All scalars are f64 per PRD NFR (no precision loss for 100-rev drift budget).
 *
 * Angles are in radians. Semi-major axis is in meters.
 */
USTRUCT(BlueprintType)
struct DELTAV_API FOrbitalState
{
	GENERATED_BODY()

	/** Semi-major axis (a), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double SemiMajorAxis = 0.0;

	/** Eccentricity (e), dimensionless. 0 = circular, [0,1) = elliptic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double Eccentricity = 0.0;

	/** Inclination (i), radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double Inclination = 0.0;

	/** Right Ascension of the Ascending Node (RAAN, Omega), radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double RightAscensionOfAscendingNode = 0.0;

	/** Argument of periapsis (omega), radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double ArgumentOfPeriapsis = 0.0;

	/** True anomaly (nu), radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double TrueAnomaly = 0.0;

	/** Epoch (seconds since J2000 or mission-defined reference). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|State")
	double Epoch = 0.0;

	/** Parent body for this state. Weak ref — bodies live on the SOI manager. */
	UPROPERTY(BlueprintReadWrite, Category = "Orbital|State")
	TWeakObjectPtr<UCelestialBody> ParentBody;
};
