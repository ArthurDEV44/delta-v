// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CelestialBody.generated.h"

/**
 * A celestial body tracked by the SOI manager (planet, moon, asteroid).
 *
 * PRD: placeholder introduced in US-006, fleshed out in US-010.
 *
 * Position lives in the game's inertial world frame (f64). The SOI radius is
 * either (a) assigned by data (level designer) or (b) computed from the Laplace
 * formula r_SOI = a * (mu / mu_parent)^(2/5) — the subsystem doesn't care which.
 */
UCLASS(BlueprintType)
class DELTAV_API UCelestialBody : public UObject
{
	GENERATED_BODY()

public:
	/** Display / log name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|Body")
	FName BodyName;

	/** Standard gravitational parameter (GM), m^3/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|Body")
	double GravitationalParameter = 0.0;

	/** Equatorial radius in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|Body")
	double EquatorialRadius = 0.0;

	/**
	 * Sphere-of-influence radius in meters. Positive finite values only.
	 * A value of 0 means the body is never considered a containing SOI.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|Body")
	double SOIRadius = 0.0;

	/**
	 * Parent body (Moon's parent is Earth, Earth's is Sun, …). Root bodies
	 * have an invalid/null parent.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Orbital|Body")
	TWeakObjectPtr<UCelestialBody> Parent;

	/**
	 * World inertial position (meters). Updated by the body's motion model —
	 * for US-010 the tests set this directly; later stories update from orbital
	 * propagation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital|Body")
	FVector WorldPosition = FVector::ZeroVector;

	/** Convenience: Laplace SOI estimate given parent semi-major-axis and mu. */
	UFUNCTION(BlueprintCallable, Category = "Orbital|Body")
	static double ComputeLaplaceSOI(double SemiMajorAxis, double Mu, double ParentMu);
};
