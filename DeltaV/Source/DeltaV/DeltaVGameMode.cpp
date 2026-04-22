// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeltaVGameMode.h"

#include "UI/ADeltaVHUD.h"

ADeltaVGameMode::ADeltaVGameMode()
{
	// US-017: HUD class that hosts the vehicle debug widget + `show debug VehicleHUD` toggle.
	HUDClass = ADeltaVHUD::StaticClass();
}
