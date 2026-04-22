// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABaseLevelBuilder.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class URectLightComponent;

/**
 * US-024 — Procedural shell builder for the walkable base (L_Base).
 *
 * Assembles 4 rooms (ControlRoom, CrewQuarters, LaunchConsole, PayloadBay)
 * connected by 3 corridors, using engine BasicShapes/Cube static meshes as
 * floors / ceilings / walls. Control room hosts 3 placeholder "screens".
 *
 * Editor step (one-time, manual):
 *  1. Create /Game/Maps/L_Base.umap (empty World)
 *  2. Drop ABaseLevelBuilder at world origin
 *  3. Drop APlayerStart at GetSpawnLocation() (logged at BeginPlay)
 *  4. Save, then set L_Base as GameDefaultMap in Project Settings
 *
 * Runtime: BeginPlay calls BuildBaseLayout() which is idempotent. Tests call
 * BuildBaseLayout() directly on a transient actor (headless, -NullRHI safe).
 */
UENUM(BlueprintType)
enum class EBaseRoomId : uint8
{
	ControlRoom    UMETA(DisplayName = "Control Room"),
	CrewQuarters   UMETA(DisplayName = "Crew Quarters"),
	LaunchConsole  UMETA(DisplayName = "Launch Console"),
	PayloadBay     UMETA(DisplayName = "Payload Bay"),
};

USTRUCT(BlueprintType)
struct FBaseRoomSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base|Room")
	EBaseRoomId RoomId = EBaseRoomId::ControlRoom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base|Room")
	FVector CenterCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base|Room")
	FVector InteriorSizeCm = FVector(800.0, 800.0, 400.0);
};

UCLASS()
class DELTAV_API ABaseLevelBuilder : public AActor
{
	GENERATED_BODY()

public:
	ABaseLevelBuilder();

	virtual void BeginPlay() override;

	/**
	 * Build the full base: rooms, corridors, screens, lights. Idempotent — a
	 * second call clears and rebuilds. Safe to call from tests without BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Base|Builder")
	void BuildBaseLayout();

	/** Number of rooms authored. Always 4 after BuildBaseLayout. */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	int32 GetRoomCount() const { return RoomSpecs.Num(); }

	/** World-space axis-aligned interior bounds for a room. */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	FBox GetRoomBounds(EBaseRoomId RoomId) const;

	/** Number of placeholder "screen" planes spawned inside a room. */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	int32 GetScreenCount(EBaseRoomId RoomId) const;

	/** Number of corridor volumes. Expected 3 for a 4-room linear layout. */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	int32 GetCorridorCount() const { return CorridorComponents.Num(); }

	/** Preferred commander spawn location — center of the control room floor. */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	FVector GetSpawnLocation() const;

	/**
	 * True when every room is reachable from the control room by traversing
	 * corridors whose AABBs overlap room interior bounds.
	 */
	UFUNCTION(BlueprintPure, Category = "Base|Builder")
	bool AreAllRoomsReachableFromControlRoom() const;

private:
	/** Root for the whole procedural assembly. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneComponent* BuilderRoot;

	/** Room specs in authored order (ControlRoom, CrewQuarters, LaunchConsole, PayloadBay). */
	UPROPERTY(VisibleAnywhere, Category = "Base|Builder")
	TArray<FBaseRoomSpec> RoomSpecs;

	/** Every mesh spawned for rooms — owned so GC cleans up on rebuild. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RoomMeshes;

	/** One placeholder plane per control-room screen. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ScreenMeshes;

	/** Corridor floor meshes (bounds used for reachability). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CorridorComponents;

	/** Lights added per room. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> RoomLights;

	/** Rect lights that back the control-room placeholder screens. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> ScreenLights;

	void ClearBuiltGeometry();
	void AuthorRoomSpecs();
	void BuildRoomShell(const FBaseRoomSpec& Spec);
	void BuildCorridorBetween(const FBaseRoomSpec& A, const FBaseRoomSpec& B);
	void BuildControlRoomScreens(const FBaseRoomSpec& ControlRoom);
	void BuildRoomLight(const FBaseRoomSpec& Spec);

	const FBaseRoomSpec* FindRoom(EBaseRoomId RoomId) const;
};
