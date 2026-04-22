// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UVehicleFactory.generated.h"

class AActor;
class ASatellite;
class UVehicleDef;
class UWorld;

/**
 * Static factory that turns a UVehicleDef DataAsset into a live actor in the
 * world.
 *
 * PRD: US-016.
 *
 * Dispatch logic
 * --------------
 *  - Def has RocketBody + SatellitePayload.bEnabled → spawn ARocket, then
 *    spawn ASatellite, attach satellite to the rocket at the rocket's
 *    payload-mount world transform offset by SatellitePayload.MountOffsetCm.
 *    Returns the rocket.
 *  - Def has RocketBody, no satellite payload → ARocket::SpawnFromDef only.
 *    Returns the rocket.
 *  - Def has satellite payload, no RocketBody → spawn ASatellite, populate
 *    power + instruments from the payload def. Returns the satellite.
 *  - Def is invalid (neither rocket nor satellite) → returns nullptr, logs
 *    a single UE_LOG Error, no partial actor is created. AC#4.
 *
 * Thread-safety
 * -------------
 * Spawn must be called from the game thread (UWorld::SpawnActor requirement).
 * AssetRegistry lookups are main-thread-only in PIE.
 */
UCLASS()
class DELTAV_API UVehicleFactory : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build a vehicle in World from Def at SpawnTransform. See class-header
	 * docs for dispatch logic. Returns the top-level actor for the vehicle,
	 * or nullptr if the def is invalid or the world is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Factory",
		meta = (DisplayName = "Spawn Vehicle From Def"))
	static AActor* Spawn(UWorld* World, UVehicleDef* Def, const FTransform& SpawnTransform);

	/**
	 * Resolve a UVehicleDef by VehicleName (or fall back to asset name) via
	 * the AssetRegistry. Returns nullptr if no matching asset is found.
	 *
	 * Intended primarily for the `spawn.vehicle <name>` console command; tests
	 * usually build defs programmatically and bypass this path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Factory")
	static UVehicleDef* FindVehicleDefByName(FName Name);

	/**
	 * Combined lookup + spawn. SpawnTransform is sourced from the first
	 * APlayerStart in World; if none is found, identity is used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Factory")
	static AActor* SpawnByName(UWorld* World, FName Name);

	/**
	 * Handler for the `spawn.vehicle <DefName>` console command. Public so
	 * the module's StartupModule can register it with IConsoleManager; tests
	 * can also invoke it directly without going through the console shell.
	 */
	static void HandleSpawnVehicleConsoleCommand(
		const TArray<FString>& Args,
		UWorld* World,
		FOutputDevice& Ar);

private:
	/** Spawn the satellite subtree. Used for both bare-satellite and payload paths. */
	static ASatellite* SpawnSatelliteFromPayload(
		UWorld* World,
		const struct FSatellitePayloadDef& Payload,
		const FTransform& SpawnTransform,
		AActor* ParentToAttach);
};
