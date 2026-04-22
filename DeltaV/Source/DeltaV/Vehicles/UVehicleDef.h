// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "UVehicleDef.generated.h"

class URocketDef;
class UInstrumentComponent;

/**
 * Per-instrument configuration carried by a satellite payload def.
 *
 * PRD: US-016. The concrete instrument class is chosen at spawn time so the
 * same def schema works for scanners, antennas, and any future instrument
 * subclass without a def-side enum.
 */
USTRUCT(BlueprintType)
struct DELTAV_API FInstrumentDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Instrument")
	FName InstrumentName = NAME_None;

	/**
	 * Concrete class to instantiate. When null the factory falls back to
	 * UScannerInstrumentComponent (the only concrete instrument currently
	 * shipping — US-015), so a designer who leaves it unset still gets a
	 * working scanner. TSubclassOf constrains the editor picker to
	 * UInstrumentComponent-derived classes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Instrument")
	TSubclassOf<UInstrumentComponent> InstrumentClass;

	/** Active draw (W). Zero when the instrument is idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Instrument",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double PowerDrawW = 0.0;

	/** Seconds of exposure required (scanner only — ignored for other classes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Instrument",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double RequiredExposureSeconds = 30.0;

	/** Maximum scan range in cm (scanner only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Instrument",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxRangeCm = 5000000.0;
};

/**
 * Satellite-payload configuration.
 *
 * When used by UVehicleDef with a non-null RocketBody, describes a satellite
 * rider carried to orbit on top of a rocket. When used without a RocketBody,
 * describes the whole vehicle (a bare satellite).
 */
USTRUCT(BlueprintType)
struct DELTAV_API FSatellitePayloadDef
{
	GENERATED_BODY()

	/** Populated to enable this payload — an unpopulated def treats the whole struct as "absent". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite")
	FName SatelliteName = NAME_None;

	/** Dry mass of the satellite itself (kg) — excludes fuel, includes bus + instruments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double DryMassKg = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double BatteryCapacityWh = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double InitialChargeWh = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double SolarPanelPowerW = 50.0;

	/** Where to attach the satellite relative to the rocket's payload-mount point (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite")
	FVector MountOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite")
	FVector InertiaDiagonalKgM2 = FVector(10.0, 10.0, 5.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Satellite")
	TArray<FInstrumentDef> Instruments;
};

/**
 * Umbrella data-asset describing a whole vehicle (rocket, satellite, or both).
 *
 * PRD: US-016. Spawns are orchestrated by UVehicleFactory which reads this
 * def and dispatches to ARocket::SpawnFromDef and/or spawns an ASatellite
 * attached to the rocket.
 *
 * A def must describe AT LEAST one of:
 *   - a RocketBody (URocketDef), OR
 *   - an enabled SatellitePayload.
 *
 * Validation (IsValid) is what the factory calls to satisfy AC#4 — an empty
 * def returns nullptr with an explicit log error, no partial actor.
 */
UCLASS(BlueprintType)
class DELTAV_API UVehicleDef : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Display/lookup name — what `spawn.vehicle <name>` matches against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	FName VehicleName = NAME_None;

	/**
	 * Rocket body, or null for pure-satellite vehicles. When populated, the
	 * factory routes through ARocket::SpawnFromDef to reuse the full US-014
	 * validation + stage-stack + Tsiolkovsky path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	TObjectPtr<URocketDef> RocketBody = nullptr;

	/**
	 * Satellite payload. Enabled independently of the rocket body so one def
	 * can describe:
	 *   - pure rocket           (RocketBody set,  Payload.bEnabled = false)
	 *   - bare satellite        (RocketBody null, Payload.bEnabled = true)
	 *   - rocket with satellite (RocketBody set,  Payload.bEnabled = true)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	FSatellitePayloadDef SatellitePayload;

	/**
	 * Additional world-space offset applied to the spawn transform passed to
	 * the factory. Useful to lift a rocket off a launch-pad mesh without
	 * modifying the player-start location.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	FVector SpawnOffsetCm = FVector::ZeroVector;

	/**
	 * Non-fatal validation — returns false if the def could not spawn any
	 * vehicle. Populates OutError with a diagnostic message. AC#4.
	 */
	bool IsValid(FString* OutError = nullptr) const;
};
