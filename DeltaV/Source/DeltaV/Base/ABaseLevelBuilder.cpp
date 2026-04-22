// Copyright Epic Games, Inc. All Rights Reserved.

#include "Base/ABaseLevelBuilder.h"

#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeltaV.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double KRoomInteriorX = 800.0;
	constexpr double KRoomInteriorY = 800.0;
	constexpr double KRoomInteriorZ = 400.0;
	constexpr double KCorridorLen   = 600.0;
	constexpr double KCorridorWidth = 300.0;
	constexpr double KCorridorZ     = 300.0;
	constexpr double KWallThickness = 20.0;
	constexpr double KScreenZ       = 250.0;
	constexpr double KScreenSize    = 150.0;

	// Engine primitive that ships with every UE install.
	const TCHAR* KCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* KPlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

	UStaticMesh* LoadPrimitiveMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}
}

ABaseLevelBuilder::ABaseLevelBuilder()
{
	PrimaryActorTick.bCanEverTick = false;

	BuilderRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BuilderRoot"));
	RootComponent = BuilderRoot;
	// Root stays Movable (default) so child attachment honours UE's mobility rule
	// (children may be equal or more dynamic than the parent — not less). Lumen
	// still captures this geometry at runtime; a static-baked variant can be
	// authored in the editor when L_Base.umap is saved.
}

void ABaseLevelBuilder::BeginPlay()
{
	Super::BeginPlay();

	BuildBaseLayout();

	UE_LOG(LogDeltaV, Log,
		TEXT("US-024 BaseLevelBuilder: built %d rooms, %d corridors, %d screens. Preferred spawn: %s"),
		GetRoomCount(), GetCorridorCount(),
		GetScreenCount(EBaseRoomId::ControlRoom),
		*GetSpawnLocation().ToString());
}

void ABaseLevelBuilder::BuildBaseLayout()
{
	ClearBuiltGeometry();
	AuthorRoomSpecs();

	for (const FBaseRoomSpec& Spec : RoomSpecs)
	{
		BuildRoomShell(Spec);
		BuildRoomLight(Spec);
	}

	// Linear corridor chain: ControlRoom -> CrewQuarters -> LaunchConsole -> PayloadBay.
	// Matches the narrative layout in PRD EP-005 intro ("4 salles connectées par couloirs").
	for (int32 i = 0; i + 1 < RoomSpecs.Num(); ++i)
	{
		BuildCorridorBetween(RoomSpecs[i], RoomSpecs[i + 1]);
	}

	const FBaseRoomSpec* ControlRoom = FindRoom(EBaseRoomId::ControlRoom);
	if (ControlRoom != nullptr)
	{
		BuildControlRoomScreens(*ControlRoom);
	}
}

FBox ABaseLevelBuilder::GetRoomBounds(EBaseRoomId RoomId) const
{
	if (const FBaseRoomSpec* Spec = FindRoom(RoomId))
	{
		const FVector HalfExtent = Spec->InteriorSizeCm * 0.5;
		return FBox(Spec->CenterCm - HalfExtent, Spec->CenterCm + HalfExtent);
	}
	return FBox(ForceInit);
}

int32 ABaseLevelBuilder::GetScreenCount(EBaseRoomId RoomId) const
{
	// Screens are only authored in the control room for US-024.
	if (RoomId != EBaseRoomId::ControlRoom)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TObjectPtr<UStaticMeshComponent>& Mesh : ScreenMeshes)
	{
		if (Mesh != nullptr)
		{
			++Count;
		}
	}
	return Count;
}

FVector ABaseLevelBuilder::GetSpawnLocation() const
{
	if (const FBaseRoomSpec* Spec = FindRoom(EBaseRoomId::ControlRoom))
	{
		// 100 cm off the floor so the capsule doesn't clip.
		return Spec->CenterCm + FVector(0.0, 0.0, -Spec->InteriorSizeCm.Z * 0.5 + 100.0);
	}
	return FVector::ZeroVector;
}

bool ABaseLevelBuilder::AreAllRoomsReachableFromControlRoom() const
{
	const int32 RoomCount = RoomSpecs.Num();
	if (RoomCount == 0)
	{
		return false;
	}

	// Build an adjacency map: rooms are adjacent if a corridor AABB touches both interior AABBs.
	TArray<TSet<int32>> Adjacency;
	Adjacency.SetNum(RoomCount);

	for (const TObjectPtr<UStaticMeshComponent>& Corridor : CorridorComponents)
	{
		if (Corridor == nullptr)
		{
			continue;
		}
		const FBox CorridorBox = Corridor->Bounds.GetBox();

		TArray<int32> Touched;
		for (int32 i = 0; i < RoomCount; ++i)
		{
			const FVector Half = RoomSpecs[i].InteriorSizeCm * 0.5;
			const FBox RoomBox(RoomSpecs[i].CenterCm - Half, RoomSpecs[i].CenterCm + Half);
			if (RoomBox.Intersect(CorridorBox))
			{
				Touched.Add(i);
			}
		}
		for (int32 A : Touched)
		{
			for (int32 B : Touched)
			{
				if (A != B)
				{
					Adjacency[A].Add(B);
				}
			}
		}
	}

	// BFS from control-room index.
	int32 ControlIdx = INDEX_NONE;
	for (int32 i = 0; i < RoomCount; ++i)
	{
		if (RoomSpecs[i].RoomId == EBaseRoomId::ControlRoom)
		{
			ControlIdx = i;
			break;
		}
	}
	if (ControlIdx == INDEX_NONE)
	{
		return false;
	}

	TSet<int32> Visited;
	TArray<int32> Frontier;
	Frontier.Add(ControlIdx);
	Visited.Add(ControlIdx);
	while (Frontier.Num() > 0)
	{
		const int32 Current = Frontier.Pop();
		for (int32 Neighbor : Adjacency[Current])
		{
			if (!Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Frontier.Add(Neighbor);
			}
		}
	}
	return Visited.Num() == RoomCount;
}

void ABaseLevelBuilder::ClearBuiltGeometry()
{
	for (TObjectPtr<UStaticMeshComponent>& Mesh : RoomMeshes)
	{
		if (Mesh != nullptr)
		{
			Mesh->DestroyComponent();
		}
	}
	RoomMeshes.Reset();

	for (TObjectPtr<UStaticMeshComponent>& Mesh : ScreenMeshes)
	{
		if (Mesh != nullptr)
		{
			Mesh->DestroyComponent();
		}
	}
	ScreenMeshes.Reset();

	for (TObjectPtr<UStaticMeshComponent>& Mesh : CorridorComponents)
	{
		if (Mesh != nullptr)
		{
			Mesh->DestroyComponent();
		}
	}
	CorridorComponents.Reset();

	for (TObjectPtr<UPointLightComponent>& Light : RoomLights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	RoomLights.Reset();

	for (TObjectPtr<URectLightComponent>& Light : ScreenLights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	ScreenLights.Reset();

	RoomSpecs.Reset();
}

void ABaseLevelBuilder::AuthorRoomSpecs()
{
	// Linear layout along +X. Rooms sit on a shared Z plane so the commander
	// walks without elevation changes.
	const double Spacing = KRoomInteriorX + KCorridorLen;

	auto Add = [&](EBaseRoomId Id, int32 IndexAlongX)
	{
		FBaseRoomSpec Spec;
		Spec.RoomId = Id;
		Spec.CenterCm = FVector(IndexAlongX * Spacing, 0.0, 0.0);
		Spec.InteriorSizeCm = FVector(KRoomInteriorX, KRoomInteriorY, KRoomInteriorZ);
		RoomSpecs.Add(Spec);
	};

	Add(EBaseRoomId::ControlRoom,   0);
	Add(EBaseRoomId::CrewQuarters,  1);
	Add(EBaseRoomId::LaunchConsole, 2);
	Add(EBaseRoomId::PayloadBay,    3);
}

void ABaseLevelBuilder::BuildRoomShell(const FBaseRoomSpec& Spec)
{
	UStaticMesh* CubeMesh = LoadPrimitiveMesh(KCubeMeshPath);
	if (CubeMesh == nullptr)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("US-024 BaseLevelBuilder: %s missing — headless RHI may skip mesh load. Continuing with empty components."),
			KCubeMeshPath);
	}

	// Engine cube mesh is 100×100×100 cm, so scale = size / 100.
	const FVector FloorScale   (Spec.InteriorSizeCm.X / 100.0, Spec.InteriorSizeCm.Y / 100.0, KWallThickness / 100.0);
	const FVector CeilingScale = FloorScale;
	const FVector WallXScale   (KWallThickness / 100.0, Spec.InteriorSizeCm.Y / 100.0, Spec.InteriorSizeCm.Z / 100.0);
	const FVector WallYScale   (Spec.InteriorSizeCm.X / 100.0, KWallThickness / 100.0, Spec.InteriorSizeCm.Z / 100.0);

	auto SpawnPart = [&](const FString& Label, const FVector& RelLocation, const FVector& Scale)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(
			this, UStaticMeshComponent::StaticClass(), *Label);
		Comp->SetupAttachment(BuilderRoot);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetRelativeLocation(RelLocation);
		Comp->SetRelativeScale3D(Scale);
		if (CubeMesh != nullptr)
		{
			Comp->SetStaticMesh(CubeMesh);
		}
		Comp->RegisterComponent();
		RoomMeshes.Add(Comp);
	};

	const FString Prefix = FString::Printf(TEXT("Room_%d_"), static_cast<int32>(Spec.RoomId));
	const FVector C = Spec.CenterCm;
	const FVector Half = Spec.InteriorSizeCm * 0.5;

	SpawnPart(Prefix + TEXT("Floor"),   C + FVector(0, 0, -Half.Z - KWallThickness * 0.5), FloorScale);
	SpawnPart(Prefix + TEXT("Ceiling"), C + FVector(0, 0,  Half.Z + KWallThickness * 0.5), CeilingScale);
	SpawnPart(Prefix + TEXT("WallXMin"), C + FVector(-Half.X - KWallThickness * 0.5, 0, 0), WallXScale);
	SpawnPart(Prefix + TEXT("WallXMax"), C + FVector( Half.X + KWallThickness * 0.5, 0, 0), WallXScale);
	SpawnPart(Prefix + TEXT("WallYMin"), C + FVector(0, -Half.Y - KWallThickness * 0.5, 0), WallYScale);
	SpawnPart(Prefix + TEXT("WallYMax"), C + FVector(0,  Half.Y + KWallThickness * 0.5, 0), WallYScale);
}

void ABaseLevelBuilder::BuildCorridorBetween(const FBaseRoomSpec& A, const FBaseRoomSpec& B)
{
	UStaticMesh* CubeMesh = LoadPrimitiveMesh(KCubeMeshPath);

	const FVector Mid = (A.CenterCm + B.CenterCm) * 0.5;
	// Corridor length spans the gap between room interior faces.
	const double AEdgeX = A.CenterCm.X + A.InteriorSizeCm.X * 0.5;
	const double BEdgeX = B.CenterCm.X - B.InteriorSizeCm.X * 0.5;
	const double Length = FMath::Max(BEdgeX - AEdgeX, 100.0);

	// Extend a few cm into each room so the corridor AABB overlaps the room AABB
	// (used by AreAllRoomsReachableFromControlRoom).
	const double Overlap = 50.0;
	const FVector Scale(
		(Length + 2.0 * Overlap) / 100.0,
		KCorridorWidth / 100.0,
		KCorridorZ / 100.0);

	UStaticMeshComponent* Corridor = NewObject<UStaticMeshComponent>(
		this, UStaticMeshComponent::StaticClass(),
		*FString::Printf(TEXT("Corridor_%d_%d"),
			static_cast<int32>(A.RoomId), static_cast<int32>(B.RoomId)));
	Corridor->SetupAttachment(BuilderRoot);
	Corridor->SetMobility(EComponentMobility::Movable);
	Corridor->SetRelativeLocation(FVector(Mid.X, Mid.Y, 0.0));
	Corridor->SetRelativeScale3D(Scale);
	if (CubeMesh != nullptr)
	{
		Corridor->SetStaticMesh(CubeMesh);
	}
	Corridor->RegisterComponent();
	Corridor->UpdateBounds();
	CorridorComponents.Add(Corridor);
}

void ABaseLevelBuilder::BuildControlRoomScreens(const FBaseRoomSpec& ControlRoom)
{
	UStaticMesh* PlaneMesh = LoadPrimitiveMesh(KPlaneMeshPath);

	// Three screens along the +Y wall of the control room, elevated on the wall.
	const double WallY = ControlRoom.CenterCm.Y + ControlRoom.InteriorSizeCm.Y * 0.5 - KWallThickness;
	const double Z = ControlRoom.CenterCm.Z - ControlRoom.InteriorSizeCm.Z * 0.5 + KScreenZ;
	const double Spacing = 220.0;
	const double BaseX = ControlRoom.CenterCm.X - Spacing;

	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* Screen = NewObject<UStaticMeshComponent>(
			this, UStaticMeshComponent::StaticClass(),
			*FString::Printf(TEXT("Screen_%d"), i));
		Screen->SetupAttachment(BuilderRoot);
		Screen->SetMobility(EComponentMobility::Movable);
		Screen->SetRelativeLocation(FVector(BaseX + i * Spacing, WallY, Z));
		// Engine plane is 100×100 on XY facing +Z; rotate to face the room interior (−Y).
		Screen->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
		Screen->SetRelativeScale3D(FVector(KScreenSize / 100.0, KScreenSize / 100.0, 1.0));
		if (PlaneMesh != nullptr)
		{
			Screen->SetStaticMesh(PlaneMesh);
		}
		Screen->RegisterComponent();
		ScreenMeshes.Add(Screen);

		URectLightComponent* RectLight = NewObject<URectLightComponent>(
			this, URectLightComponent::StaticClass(),
			*FString::Printf(TEXT("ScreenLight_%d"), i));
		RectLight->SetupAttachment(BuilderRoot);
		RectLight->SetMobility(EComponentMobility::Movable);
		RectLight->SetRelativeLocation(FVector(BaseX + i * Spacing, WallY - 10.0, Z));
		RectLight->SetIntensity(500.0);
		RectLight->SetSourceWidth(KScreenSize);
		RectLight->SetSourceHeight(KScreenSize);
		RectLight->RegisterComponent();
		ScreenLights.Add(RectLight);
	}
}

void ABaseLevelBuilder::BuildRoomLight(const FBaseRoomSpec& Spec)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(
		this, UPointLightComponent::StaticClass(),
		*FString::Printf(TEXT("RoomLight_%d"), static_cast<int32>(Spec.RoomId)));
	Light->SetupAttachment(BuilderRoot);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetRelativeLocation(Spec.CenterCm + FVector(0, 0, Spec.InteriorSizeCm.Z * 0.4));
	Light->SetIntensity(5000.0);
	Light->SetAttenuationRadius(FMath::Max(Spec.InteriorSizeCm.X, Spec.InteriorSizeCm.Y));
	Light->RegisterComponent();
	RoomLights.Add(Light);
}

const FBaseRoomSpec* ABaseLevelBuilder::FindRoom(EBaseRoomId RoomId) const
{
	for (const FBaseRoomSpec& Spec : RoomSpecs)
	{
		if (Spec.RoomId == RoomId)
		{
			return &Spec;
		}
	}
	return nullptr;
}
