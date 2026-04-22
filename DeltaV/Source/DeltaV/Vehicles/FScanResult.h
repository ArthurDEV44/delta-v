// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FScanResult.generated.h"

class AActor;

/**
 * Typed result of a scanner instrument sweep.
 *
 * PRD: US-015, AC#2 — a completed scan emits a FScanResult with estimated
 * mass and composition. AC#4 — a scan that cannot run (no power, out of range,
 * lost target) returns FScanResult::Invalid() without crashing.
 *
 * Composition tags are stored as a flat FName array; upgrading to a
 * FGameplayTagContainer is deferred until a gameplay-tag taxonomy is defined
 * (post-US-015 work).
 */
USTRUCT(BlueprintType)
struct DELTAV_API FScanResult
{
	GENERATED_BODY()

	/** True when the scan completed successfully and the typed fields are populated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite|Scan")
	bool bValid = false;

	/** Name of the scanned actor (captured at scan completion). NAME_None when invalid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite|Scan")
	FName TargetName = NAME_None;

	/** Estimated target mass in kilograms. Zero when invalid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite|Scan")
	double EstimatedMassKg = 0.0;

	/** Coarse composition classification (e.g., "Rock", "Metal"). Empty when invalid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite|Scan")
	TArray<FName> CompositionTags;

	/** World-time (seconds) at which the scan completed. Zero when invalid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Satellite|Scan")
	double CompletedWorldSeconds = 0.0;

	/** Canonical "no result" value — bValid == false. */
	static FScanResult Invalid()
	{
		return FScanResult{};
	}
};
