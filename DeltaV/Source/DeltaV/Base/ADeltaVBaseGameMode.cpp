// Copyright Epic Games, Inc. All Rights Reserved.

#include "Base/ADeltaVBaseGameMode.h"

#include "Base/ABaseLevelBuilder.h"
#include "DeltaV.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ADeltaVBaseGameMode::ADeltaVBaseGameMode()
{
	// ACommanderCharacter is UCLASS(abstract) so it cannot be spawned directly —
	// resolve the concrete Blueprint subclass at construction time.
	static ConstructorHelpers::FClassFinder<APawn> CommanderPawnClass(
		TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	DefaultPawnClass = CommanderPawnClass.Class;
}

void ADeltaVBaseGameMode::StartPlay()
{
	UWorld* World = GetWorld();
	if (World != nullptr)
	{
		// If the level was authored with a builder already placed, reuse it —
		// otherwise spawn one at origin. PIE without the manual editor step stays
		// functional.
		for (TActorIterator<ABaseLevelBuilder> It(World); It; ++It)
		{
			CachedBuilder = *It;
			break;
		}

		if (CachedBuilder == nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CachedBuilder = World->SpawnActor<ABaseLevelBuilder>(
				ABaseLevelBuilder::StaticClass(),
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				Params);

			if (CachedBuilder == nullptr)
			{
				UE_LOG(LogDeltaV, Warning,
					TEXT("US-024 BaseGameMode: failed to spawn ABaseLevelBuilder."));
			}
		}
	}

	Super::StartPlay();
}
