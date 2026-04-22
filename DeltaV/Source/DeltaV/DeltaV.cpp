// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeltaV.h"

#include "Vehicles/UVehicleFactory.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

/**
 * Game module for DeltaV.
 *
 * StartupModule / ShutdownModule host the registration of console commands
 * that depend on project-level code (e.g., `spawn.vehicle`). File-scope
 * FAutoConsoleCommand statics in game modules re-run their constructor on
 * Live Coding reloads and can collide with already-registered commands;
 * module-owned registration avoids that class of bug and gives us explicit
 * unregistration on shutdown.
 */
class FDeltaVGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

		IConsoleManager& Console = IConsoleManager::Get();

		// Guard against double registration across reloads.
		if (Console.FindConsoleObject(TEXT("spawn.vehicle")) == nullptr)
		{
			SpawnVehicleConsoleCommand = Console.RegisterConsoleCommand(
				TEXT("spawn.vehicle"),
				TEXT("Spawn a UVehicleDef by name at the current player start. Usage: spawn.vehicle <DefName>"),
				FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
					&UVehicleFactory::HandleSpawnVehicleConsoleCommand),
				ECVF_Default);
		}
	}

	virtual void ShutdownModule() override
	{
		IConsoleManager& Console = IConsoleManager::Get();

		if (SpawnVehicleConsoleCommand != nullptr)
		{
			Console.UnregisterConsoleObject(SpawnVehicleConsoleCommand);
			SpawnVehicleConsoleCommand = nullptr;
		}

		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	/** Owned pointer — unregistered on ShutdownModule. */
	IConsoleCommand* SpawnVehicleConsoleCommand = nullptr;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDeltaVGameModule, DeltaV, "DeltaV");

DEFINE_LOG_CATEGORY(LogDeltaV)
