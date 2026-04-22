// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/UVehicleDebugHUDWidget.h"

#include "DeltaV.h"
#include "Vehicles/ARocket.h"
#include "Vehicles/AVehicle.h"
#include "Vehicles/UStageComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	/**
	 * Sum of MaxThrust * CurrentCurveMultiplier across every ignited stage.
	 * Non-ignited / depleted stages contribute zero. Mirrors the live thrust
	 * a gameplay consumer would see this frame.
	 */
	double ComputeCurrentThrustNewtons(const ARocket& Rocket)
	{
		double Total = 0.0;
		for (const TObjectPtr<UStageComponent>& StagePtr : Rocket.Stages)
		{
			const UStageComponent* Stage = StagePtr.Get();
			if (Stage == nullptr)
			{
				continue;
			}
			Total += FMath::Max(Stage->GetCurrentThrustNewtons(), 0.0);
		}
		return Total;
	}

	/** Remaining fuel, summed across all live stages (kg). */
	double ComputeRemainingFuelKg(const ARocket& Rocket)
	{
		double Total = 0.0;
		for (const TObjectPtr<UStageComponent>& StagePtr : Rocket.Stages)
		{
			const UStageComponent* Stage = StagePtr.Get();
			if (Stage == nullptr)
			{
				continue;
			}
			Total += FMath::Max(Stage->FuelMassRemainingKg, 0.0);
		}
		return Total;
	}
}

TArray<FString> UVehicleDebugHUDWidget::BuildDebugLines(const AVehicle* Vehicle)
{
	TArray<FString> Lines;

	if (Vehicle == nullptr || !::IsValid(Vehicle))
	{
		Lines.Add(TEXT("No active vehicle"));
		return Lines;
	}

	// Sanitize every value symmetrically so a rogue NaN / Inf in any one
	// field renders as 0.000 rather than "nan"/"inf" degrading the HUD.
	auto Sanitize = [](double V) { return FMath::IsFinite(V) ? V : 0.0; };

	const double Mass = Sanitize(Vehicle->TotalMass);

	double ThrustN = 0.0;
	double FuelKg = 0.0;
	double DeltaVMs = 0.0;

	if (const ARocket* Rocket = Cast<const ARocket>(Vehicle))
	{
		ThrustN  = Sanitize(ComputeCurrentThrustNewtons(*Rocket));
		FuelKg   = Sanitize(ComputeRemainingFuelKg(*Rocket));
		DeltaVMs = Sanitize(Rocket->CalculateTheoreticalDeltaV());
	}

	Lines.Reserve(5);
	Lines.Add(FString::Printf(TEXT("Vehicle: %s"), *Vehicle->GetName()));
	Lines.Add(FString::Printf(TEXT("Mass   : %.3f kg"), Mass));
	Lines.Add(FString::Printf(TEXT("Thrust : %.3f N"), ThrustN));
	Lines.Add(FString::Printf(TEXT("Fuel   : %.3f kg"), FuelKg));
	Lines.Add(FString::Printf(TEXT("DeltaV : %.3f m/s"), DeltaVMs));

	return Lines;
}

void UVehicleDebugHUDWidget::SetActiveVehicle(AVehicle* NewVehicle)
{
	ActiveVehicle = NewVehicle;
	Refresh();
}

void UVehicleDebugHUDWidget::Refresh()
{
	if (DebugTextBlock == nullptr)
	{
		// NativeConstruct has not run yet (tests call Refresh on a non-viewport
		// widget). Nothing to render — BuildDebugLines is the authoritative
		// state for headless consumers.
		return;
	}

	AVehicle* Vehicle = ActiveVehicle.Get();
	const TArray<FString> Lines = BuildDebugLines(Vehicle);
	DebugTextBlock->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}

void UVehicleDebugHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (WidgetTree == nullptr)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UVehicleDebugHUDWidget::NativeConstruct: null WidgetTree on '%s'"),
			*GetName());
		return;
	}

	// Build layout in code rather than requiring a companion .uasset so the
	// widget works in fully headless / cooked-only scenarios and in tests.
	DebugRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DebugRoot"));
	DebugTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DebugTextBlock"));

	if (DebugRoot != nullptr && DebugTextBlock != nullptr)
	{
		UVerticalBoxSlot* BoxSlot = DebugRoot->AddChildToVerticalBox(DebugTextBlock);
		if (BoxSlot != nullptr)
		{
			BoxSlot->SetPadding(FMargin(12.0f, 8.0f, 12.0f, 8.0f));
		}

		DebugTextBlock->SetText(FText::FromString(TEXT("No active vehicle")));

		// WidgetTree must have a root — assign our vertical box.
		WidgetTree->RootWidget = DebugRoot;
	}

	// Initial render now that the widget tree is live.
	Refresh();
}
