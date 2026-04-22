// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DeltaVGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class DELTAV_API ADeltaVGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	ADeltaVGameMode();
};



