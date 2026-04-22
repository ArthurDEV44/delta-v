// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ADeltaVHUD.h"

#include "DeltaV.h"
#include "UI/UVehicleDebugHUDWidget.h"
#include "Vehicles/AVehicle.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

const FName ADeltaVHUD::VehicleHUDCategory(TEXT("VehicleHUD"));

namespace
{
	// 10 Hz → 0.1 s period per AC#1 ("mis à jour à 10 Hz").
	constexpr float KVehicleHUDRefreshSeconds = 0.1f;
}

ADeltaVHUD::ADeltaVHUD()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ADeltaVHUD::ShowDebug(FName DebugType)
{
	Super::ShowDebug(DebugType);

	if (DebugType != VehicleHUDCategory)
	{
		return;
	}

	// Rebind bVehicleHUDVisible to the canonical state AHUD just mutated in
	// its DebugDisplay array, rather than independently flipping our own bool.
	// If anything else (another subsystem, cheat manager, config-driven
	// preload) pushes/removes our category name, our widget lifecycle stays
	// aligned with the engine's truth.
	const bool bCanonicalVisible = DebugDisplay.Contains(VehicleHUDCategory);

	if (bCanonicalVisible == bVehicleHUDVisible)
	{
		// No change we did not already act on — nothing to do.
		return;
	}

	bVehicleHUDVisible = bCanonicalVisible;

	if (bVehicleHUDVisible)
	{
		EnableVehicleHUD();
	}
	else
	{
		DisableVehicleHUD();
	}
}

void ADeltaVHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisableVehicleHUD();
	Super::EndPlay(EndPlayReason);
}

void ADeltaVHUD::EnableVehicleHUD()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (PC == nullptr)
	{
		// ADeltaVHUD is spawned per-local-player; if for some reason we have
		// no owning PC yet (very early in the HUD lifecycle), defer — the
		// next ShowDebug toggle will retry. Pull our flag AND the engine's
		// DebugDisplay entry back in sync so the next toggle reads as "off".
		UE_LOG(LogDeltaV, Warning,
			TEXT("ADeltaVHUD::EnableVehicleHUD: no owning PlayerController — deferring"));
		bVehicleHUDVisible = false;
		DebugDisplay.Remove(VehicleHUDCategory);
		return;
	}

	if (VehicleDebugWidget == nullptr)
	{
		VehicleDebugWidget = CreateWidget<UVehicleDebugHUDWidget>(
			PC, UVehicleDebugHUDWidget::StaticClass(), TEXT("VehicleDebugHUD"));
	}

	if (VehicleDebugWidget == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("ADeltaVHUD::EnableVehicleHUD: CreateWidget returned null"));
		bVehicleHUDVisible = false;
		DebugDisplay.Remove(VehicleHUDCategory);
		return;
	}

	if (!VehicleDebugWidget->IsInViewport())
	{
		VehicleDebugWidget->AddToViewport(/*ZOrder=*/ 50);
	}

	// Immediate refresh so the widget is not showing "No active vehicle" for
	// a full 100 ms on activation if a pawn is already possessed.
	RefreshVehicleHUD();

	if (!RefreshTimerHandle.IsValid())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimerHandle,
			this, &ADeltaVHUD::RefreshVehicleHUD,
			KVehicleHUDRefreshSeconds,
			/*bLoop=*/ true);
	}
}

void ADeltaVHUD::DisableVehicleHUD()
{
	if (UWorld* World = GetWorld())
	{
		if (RefreshTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(RefreshTimerHandle);
		}
	}
	RefreshTimerHandle.Invalidate();

	if (VehicleDebugWidget != nullptr)
	{
		if (VehicleDebugWidget->IsInViewport())
		{
			VehicleDebugWidget->RemoveFromParent();
		}
		// Let GC reclaim — keeping the widget around is pointless since a
		// future toggle-on will build a fresh one.
		VehicleDebugWidget = nullptr;
	}
}

void ADeltaVHUD::RefreshVehicleHUD()
{
	if (VehicleDebugWidget == nullptr)
	{
		return;
	}

	AVehicle* Active = FindActiveVehicle();
	VehicleDebugWidget->SetActiveVehicle(Active);
}

AVehicle* ADeltaVHUD::FindActiveVehicle() const
{
	// 1. Prefer the currently possessed actor if it is an AVehicle (AC#3).
	// AVehicle derives from AActor (not APawn); possession wires via AController,
	// but HUD retrieval goes through GetPawn() and may return an AVehicle actor
	// attached to a pawn. For now the possessed pawn itself is never an AVehicle,
	// so we skip straight to world-scan — US-029 will wire possession properly.
	(void)GetOwningPlayerController();

	// 2. Fallback — first AVehicle actor we can find in the world. Useful
	//    before possession wiring lands (US-029) and for the free-camera
	//    case where a non-vehicle pawn is possessed.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AVehicle> It(World); It; ++It)
		{
			if (AVehicle* Actor = *It)
			{
				return Actor;
			}
		}
	}

	// 3. Nothing found — widget will render "No active vehicle" (AC#4).
	return nullptr;
}
