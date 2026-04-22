// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UVehicleFactory.h"

#include "DeltaV.h"
#include "Vehicles/ARocket.h"
#include "Vehicles/ASatellite.h"
#include "Vehicles/URocketDef.h"
#include "Vehicles/UInstrumentComponent.h"
#include "Vehicles/UPowerComponent.h"
#include "Vehicles/UScannerInstrumentComponent.h"
#include "Vehicles/UVehicleDef.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Return the first APlayerStart actor in World, or nullptr if none. */
	APlayerStart* FindFirstPlayerStart(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}

		TArray<AActor*> Starts;
		UGameplayStatics::GetAllActorsOfClass(World, APlayerStart::StaticClass(), Starts);
		for (AActor* Actor : Starts)
		{
			if (APlayerStart* Start = Cast<APlayerStart>(Actor))
			{
				return Start;
			}
		}
		return nullptr;
	}

	/**
	 * Build a URocketDef on the fly if the UVehicleDef carries a satellite
	 * payload — mass conservation for Tsiolkovsky requires the rocket's
	 * payload mass to include the satellite's dry mass. We duplicate the
	 * authoritative def and override PayloadDryMassKg. Done on a transient
	 * copy so the original editor-authored asset is not mutated.
	 */
	URocketDef* MakeEffectiveRocketDef(const UVehicleDef& VehicleDef, UObject* Outer)
	{
		URocketDef* Source = VehicleDef.RocketBody;
		if (Source == nullptr)
		{
			return nullptr;
		}

		URocketDef* Effective = DuplicateObject<URocketDef>(Source, Outer);
		if (Effective == nullptr)
		{
			return nullptr;
		}

		if (VehicleDef.SatellitePayload.bEnabled)
		{
			const double SatMass = FMath::Max(VehicleDef.SatellitePayload.DryMassKg, 0.0);
			// The editor-authored PayloadDryMassKg represents any non-satellite
			// ballast; we add the satellite dry mass on top so the rocket's
			// Tsiolkovsky sees the full riding load.
			Effective->PayloadDryMassKg = FMath::Max(Effective->PayloadDryMassKg, 0.0) + SatMass;
		}

		return Effective;
	}

	/**
	 * Resolve an FInstrumentDef::InstrumentClass to a concrete UInstrumentComponent
	 * subclass. Null or non-subclass inputs BOTH fall back to
	 * UScannerInstrumentComponent — the only concrete instrument shipping
	 * today. Returning the abstract-ish base would give a silently-useless
	 * component with no scanner behavior, so we converge both bad paths on
	 * the same functional fallback.
	 */
	UClass* ResolveInstrumentClass(const TSubclassOf<UInstrumentComponent>& Requested)
	{
		UClass* Resolved = Requested.Get();
		if (Resolved == nullptr || !Resolved->IsChildOf(UInstrumentComponent::StaticClass()))
		{
			return UScannerInstrumentComponent::StaticClass();
		}
		return Resolved;
	}

	/** Populate a freshly spawned satellite's components from a payload def. */
	void ConfigureSatellite(ASatellite& Satellite, const FSatellitePayloadDef& Payload)
	{
		if (UPowerComponent* Power = Satellite.PowerComponent)
		{
			Power->CapacityWh = FMath::Max(Payload.BatteryCapacityWh, 0.0);
			Power->SolarPanelPowerW = FMath::Max(Payload.SolarPanelPowerW, 0.0);
			Power->SetCurrentChargeWh(FMath::Max(Payload.InitialChargeWh, 0.0));
		}

		for (const FInstrumentDef& InstDef : Payload.Instruments)
		{
			UClass* Class = ResolveInstrumentClass(InstDef.InstrumentClass);

			const FName ComponentName = MakeUniqueObjectName(
				&Satellite, Class,
				FName(*FString::Printf(TEXT("Instrument_%s"), *InstDef.InstrumentName.ToString())));

			UInstrumentComponent* Instrument =
				NewObject<UInstrumentComponent>(&Satellite, Class, ComponentName);

			if (Instrument == nullptr)
			{
				UE_LOG(LogDeltaV, Error,
					TEXT("UVehicleFactory: failed to NewObject instrument '%s' of class '%s'"),
					*InstDef.InstrumentName.ToString(), *Class->GetName());
				continue;
			}

			Instrument->PowerDrawW = FMath::Max(InstDef.PowerDrawW, 0.0);

			// Scanner-specific knobs only apply when the concrete class is a scanner.
			if (UScannerInstrumentComponent* Scanner = Cast<UScannerInstrumentComponent>(Instrument))
			{
				Scanner->RequiredExposureSeconds =
					FMath::IsFinite(InstDef.RequiredExposureSeconds) && InstDef.RequiredExposureSeconds > 0.0
						? InstDef.RequiredExposureSeconds
						: 30.0;
				Scanner->MaxRangeCm =
					FMath::IsFinite(InstDef.MaxRangeCm) && InstDef.MaxRangeCm >= 0.0
						? InstDef.MaxRangeCm
						: 5'000'000.0;
			}

			Instrument->RegisterComponent();
			Satellite.RegisterInstrument(Instrument);
		}
	}
}

AActor* UVehicleFactory::Spawn(UWorld* World, UVehicleDef* Def, const FTransform& SpawnTransform)
{
	if (World == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::Spawn: null World — refusing to spawn"));
		return nullptr;
	}

	if (Def == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::Spawn: null UVehicleDef — refusing to spawn"));
		return nullptr;
	}

	FString ValidationError;
	if (!Def->IsValid(&ValidationError))
	{
		// AC#4 — explicit log error, no partial actor leaked into the world.
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::Spawn: invalid UVehicleDef '%s' — %s"),
			*Def->GetName(), *ValidationError);
		return nullptr;
	}

	// Apply per-def spawn offset on top of the caller-provided transform.
	FTransform FinalTransform = SpawnTransform;
	FinalTransform.SetLocation(FinalTransform.GetLocation() + Def->SpawnOffsetCm);

	const bool bHasRocket = Def->RocketBody != nullptr;
	const bool bHasSatellite = Def->SatellitePayload.bEnabled;

	if (bHasRocket)
	{
		// Build an effective rocket def on the fly so the satellite's dry mass
		// rolls into the rocket's Tsiolkovsky payload. Original editor asset
		// is not mutated (DuplicateObject into a transient outer).
		URocketDef* EffectiveRocket = MakeEffectiveRocketDef(*Def, GetTransientPackage());
		if (EffectiveRocket == nullptr)
		{
			UE_LOG(LogDeltaV, Error,
				TEXT("UVehicleFactory::Spawn: failed to duplicate RocketBody for def '%s'"),
				*Def->GetName());
			return nullptr;
		}

		ARocket* Rocket = ARocket::SpawnFromDef(World, EffectiveRocket, FinalTransform);
		if (Rocket == nullptr)
		{
			// ARocket::SpawnFromDef already logged the specific reason.
			return nullptr;
		}

		if (bHasSatellite)
		{
			// Spawn the satellite at the rocket's payload mount point + the
			// payload def's per-sat offset. Rotate the local-frame offset by
			// the spawn rotation so a rocket spawned tilted still mounts its
			// satellite above itself (not offset in world-axis space).
			const FQuat SpawnRot = FinalTransform.GetRotation();
			const FVector LocalOffset = EffectiveRocket->PayloadMountOffsetCm + Def->SatellitePayload.MountOffsetCm;
			const FVector SatWorldLoc = FinalTransform.GetLocation() + SpawnRot.RotateVector(LocalOffset);

			const FTransform SatTransform(SpawnRot, SatWorldLoc);

			ASatellite* Sat = SpawnSatelliteFromPayload(
				World, Def->SatellitePayload, SatTransform, /*ParentToAttach=*/ Rocket);

			if (Sat == nullptr)
			{
				// Tsiolkovsky was computed against a payload mass that now has
				// no corresponding physical actor — the rocket's state would
				// be inconsistent. Tear the rocket down and return nullptr so
				// callers see a clean failure, matching AC#4's all-or-nothing
				// spawn contract.
				UE_LOG(LogDeltaV, Error,
					TEXT("UVehicleFactory::Spawn: satellite payload failed to spawn for def '%s' — tearing down rocket to preserve consistency"),
					*Def->GetName());
				Rocket->Destroy();
				return nullptr;
			}
		}

		return Rocket;
	}

	// Pure-satellite path.
	ASatellite* Sat = SpawnSatelliteFromPayload(
		World, Def->SatellitePayload, FinalTransform, /*ParentToAttach=*/ nullptr);

	return Sat;
}

ASatellite* UVehicleFactory::SpawnSatelliteFromPayload(
	UWorld* World,
	const FSatellitePayloadDef& Payload,
	const FTransform& SpawnTransform,
	AActor* ParentToAttach)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	ASatellite* Sat = World->SpawnActorDeferred<ASatellite>(
		ASatellite::StaticClass(), SpawnTransform, /*Owner=*/ nullptr, /*Instigator=*/ nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Sat == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::SpawnSatelliteFromPayload: SpawnActorDeferred<ASatellite> returned null"));
		return nullptr;
	}

	ConfigureSatellite(*Sat, Payload);

	UGameplayStatics::FinishSpawningActor(Sat, SpawnTransform);

	if (ParentToAttach != nullptr)
	{
		Sat->AttachToActor(ParentToAttach, FAttachmentTransformRules::KeepWorldTransform);
	}

	return Sat;
}

UVehicleDef* UVehicleFactory::FindVehicleDefByName(FName Name)
{
	if (Name.IsNone())
	{
		return nullptr;
	}

	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();

	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UVehicleDef::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/ true);

	for (const FAssetData& Asset : Assets)
	{
		// Match either on the asset's own name (filename) OR on the
		// authored VehicleName FName tag if the author set one.
		if (Asset.AssetName == Name)
		{
			return Cast<UVehicleDef>(Asset.GetAsset());
		}
		if (UVehicleDef* Def = Cast<UVehicleDef>(Asset.GetAsset()))
		{
			if (Def->VehicleName == Name)
			{
				return Def;
			}
		}
	}

	return nullptr;
}

AActor* UVehicleFactory::SpawnByName(UWorld* World, FName Name)
{
	if (World == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::SpawnByName: null World"));
		return nullptr;
	}

	UVehicleDef* Def = FindVehicleDefByName(Name);
	if (Def == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UVehicleFactory::SpawnByName: no UVehicleDef named '%s' found in AssetRegistry"),
			*Name.ToString());
		return nullptr;
	}

	FTransform Where = FTransform::Identity;
	if (APlayerStart* Start = FindFirstPlayerStart(World))
	{
		Where = Start->GetActorTransform();
	}

	return Spawn(World, Def, Where);
}

// =============================================================================
// Console command: spawn.vehicle <DefName>
//
// Registration is module-owned (see DeltaV.cpp) rather than a file-scope
// FAutoConsoleCommand. File-scope statics re-run their constructor on Live
// Coding reloads, which can collide with IConsoleManager's existing registration
// for the same command name. Module-owned registration gives us explicit
// Startup/Shutdown sites.
// =============================================================================

void UVehicleFactory::HandleSpawnVehicleConsoleCommand(
	const TArray<FString>& Args,
	UWorld* World,
	FOutputDevice& Ar)
{
	if (World == nullptr)
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("spawn.vehicle: no active world"));
		return;
	}

	if (Args.Num() < 1)
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("Usage: spawn.vehicle <DefName>"));
		return;
	}

	const FName DefName(*Args[0]);
	AActor* Spawned = UVehicleFactory::SpawnByName(World, DefName);
	if (Spawned == nullptr)
	{
		Ar.Logf(ELogVerbosity::Error,
			TEXT("spawn.vehicle: failed to spawn '%s' (see log for details)"),
			*DefName.ToString());
		return;
	}

	Ar.Logf(TEXT("spawn.vehicle: spawned '%s' (%s) at %s"),
		*DefName.ToString(),
		*Spawned->GetClass()->GetName(),
		*Spawned->GetActorLocation().ToCompactString());
}
