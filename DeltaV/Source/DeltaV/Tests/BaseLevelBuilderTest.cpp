// Copyright Epic Games, Inc. All Rights Reserved.

#include "Base/ABaseLevelBuilder.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UWorld* MakeBaseTestWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Game, /*bInformEngineOfWorld=*/ false,
			FName(TEXT("US024BaseLevelBuilderTestWorld")));
		if (World == nullptr)
		{
			return nullptr;
		}
		FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.SetCurrentWorld(World);
		return World;
	}

	void DestroyBaseTestWorld(UWorld* World)
	{
		if (World == nullptr || GEngine == nullptr)
		{
			return;
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ABaseLevelBuilder* SpawnAndBuild(UWorld* World)
	{
		ABaseLevelBuilder* Builder = World->SpawnActor<ABaseLevelBuilder>(
			ABaseLevelBuilder::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Builder == nullptr)
		{
			return nullptr;
		}
		Builder->BuildBaseLayout();
		return Builder;
	}
}

// =============================================================================
// US-024 AC#1 — PIE spawn in control room with 3 placeholder screens.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBaseLevelBuilderSpawnsFourRoomsTest,
	"DeltaV.Base.LevelBuilder.SpawnsFourRooms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBaseLevelBuilderSpawnsFourRoomsTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBaseTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	ABaseLevelBuilder* Builder = SpawnAndBuild(World);
	if (!TestNotNull(TEXT("Builder spawned"), Builder))
	{
		DestroyBaseTestWorld(World);
		return false;
	}

	TestEqual<int32>(TEXT("Four rooms authored"), Builder->GetRoomCount(), 4);

	const EBaseRoomId Expected[] = {
		EBaseRoomId::ControlRoom,
		EBaseRoomId::CrewQuarters,
		EBaseRoomId::LaunchConsole,
		EBaseRoomId::PayloadBay,
	};
	for (EBaseRoomId RoomId : Expected)
	{
		const FBox Bounds = Builder->GetRoomBounds(RoomId);
		TestTrue(
			FString::Printf(TEXT("Room %d has non-zero bounds"), static_cast<int32>(RoomId)),
			Bounds.GetSize().SizeSquared() > 1.0);
		// Interior must be large enough for the commander (capsule half-height 96 cm).
		const FVector Size = Bounds.GetSize();
		TestTrue(
			FString::Printf(TEXT("Room %d interior >= 4x4x3 m"), static_cast<int32>(RoomId)),
			Size.X >= 400.0 && Size.Y >= 400.0 && Size.Z >= 300.0);
	}

	DestroyBaseTestWorld(World);
	return true;
}

// =============================================================================
// US-024 AC#1 — "je suis dans la control room avec vue sur 3 écrans placeholder"
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBaseLevelBuilderControlRoomScreensTest,
	"DeltaV.Base.LevelBuilder.ControlRoomHasThreeScreens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBaseLevelBuilderControlRoomScreensTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBaseTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	ABaseLevelBuilder* Builder = SpawnAndBuild(World);
	if (!TestNotNull(TEXT("Builder spawned"), Builder))
	{
		DestroyBaseTestWorld(World);
		return false;
	}

	TestEqual<int32>(
		TEXT("ControlRoom hosts 3 placeholder screens"),
		Builder->GetScreenCount(EBaseRoomId::ControlRoom), 3);
	TestEqual<int32>(
		TEXT("Non-control rooms host 0 screens (CrewQuarters)"),
		Builder->GetScreenCount(EBaseRoomId::CrewQuarters), 0);
	TestEqual<int32>(
		TEXT("Non-control rooms host 0 screens (LaunchConsole)"),
		Builder->GetScreenCount(EBaseRoomId::LaunchConsole), 0);

	const FVector Spawn = Builder->GetSpawnLocation();
	const FBox ControlBounds = Builder->GetRoomBounds(EBaseRoomId::ControlRoom);
	TestTrue(TEXT("Spawn location is inside control-room bounds"),
		ControlBounds.IsInsideOrOn(Spawn));

	DestroyBaseTestWorld(World);
	return true;
}

// =============================================================================
// US-024 AC#2 — "je traverse les 4 salles via couloirs" (connectivity check).
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBaseLevelBuilderRoomsConnectedTest,
	"DeltaV.Base.LevelBuilder.RoomsConnected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBaseLevelBuilderRoomsConnectedTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBaseTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	ABaseLevelBuilder* Builder = SpawnAndBuild(World);
	if (!TestNotNull(TEXT("Builder spawned"), Builder))
	{
		DestroyBaseTestWorld(World);
		return false;
	}

	// 3 corridors for 4 rooms in a linear chain.
	TestTrue(
		FString::Printf(TEXT("At least 3 corridors built (got %d)"),
			Builder->GetCorridorCount()),
		Builder->GetCorridorCount() >= 3);

	TestTrue(
		TEXT("All four rooms reachable from control room via corridor AABB overlap"),
		Builder->AreAllRoomsReachableFromControlRoom());

	DestroyBaseTestWorld(World);
	return true;
}

// =============================================================================
// Idempotency — BuildBaseLayout can be called twice without duplicating geometry.
// =============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBaseLevelBuilderIdempotentRebuildTest,
	"DeltaV.Base.LevelBuilder.RebuildIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBaseLevelBuilderIdempotentRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBaseTestWorld();
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}

	ABaseLevelBuilder* Builder = SpawnAndBuild(World);
	if (!TestNotNull(TEXT("Builder spawned"), Builder))
	{
		DestroyBaseTestWorld(World);
		return false;
	}

	const int32 Rooms1 = Builder->GetRoomCount();
	const int32 Corridors1 = Builder->GetCorridorCount();
	const int32 Screens1 = Builder->GetScreenCount(EBaseRoomId::ControlRoom);

	Builder->BuildBaseLayout();

	TestEqual<int32>(TEXT("Room count stable after rebuild"),
		Builder->GetRoomCount(), Rooms1);
	TestEqual<int32>(TEXT("Corridor count stable after rebuild"),
		Builder->GetCorridorCount(), Corridors1);
	TestEqual<int32>(TEXT("Screen count stable after rebuild"),
		Builder->GetScreenCount(EBaseRoomId::ControlRoom), Screens1);

	DestroyBaseTestWorld(World);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
