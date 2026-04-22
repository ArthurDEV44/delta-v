// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DeltaVGameMode.h"
#include "ADeltaVBaseGameMode.generated.h"

class ABaseLevelBuilder;

/**
 * US-024 — GameMode for the walkable base level (L_Base).
 *
 * - DefaultPawnClass wires ACommanderCharacter so the commander is possessed on PIE.
 * - On StartPlay, spawns an ABaseLevelBuilder at world origin if none is present in
 *   the loaded level. This keeps the level bootable even before the editor step of
 *   placing the builder manually is done.
 */
UCLASS()
class DELTAV_API ADeltaVBaseGameMode : public ADeltaVGameMode
{
	GENERATED_BODY()

public:
	ADeltaVBaseGameMode();

	virtual void StartPlay() override;

	/** Returns the builder instance for the current world, or null. */
	UFUNCTION(BlueprintPure, Category = "Base|GameMode")
	ABaseLevelBuilder* GetBaseLevelBuilder() const { return CachedBuilder; }

private:
	UPROPERTY(Transient)
	TObjectPtr<ABaseLevelBuilder> CachedBuilder;
};
