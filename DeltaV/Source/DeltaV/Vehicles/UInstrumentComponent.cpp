// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UInstrumentComponent.h"

#include "DeltaV.h"
#include "Vehicles/UPowerComponent.h"

UInstrumentComponent::UInstrumentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

bool UInstrumentComponent::ActivateInstrument()
{
	if (bActive)
	{
		return false;
	}

	UPowerComponent* Power = ResolvePower();
	if (Power == nullptr)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UInstrumentComponent::ActivateInstrument: '%s' has no sibling UPowerComponent"),
			*GetPathName());
		return false;
	}

	if (Power->bSafeMode || Power->IsPowerDepleted())
	{
		// AC#4 — activation without energy returns without enabling the draw,
		// logs a warning, and (for scanners) gets wrapped in a FScanResult::Invalid.
		UE_LOG(LogDeltaV, Warning,
			TEXT("UInstrumentComponent::ActivateInstrument: '%s' refused — power depleted or in safe mode"),
			*GetPathName());
		return false;
	}

	bActive = true;
	Power->RegisterInstrumentDraw(this);
	return true;
}

void UInstrumentComponent::DeactivateInstrument()
{
	if (!bActive)
	{
		return;
	}

	bActive = false;

	if (UPowerComponent* Power = CachedPower.Get())
	{
		Power->UnregisterInstrumentDraw(this);
	}
}

double UInstrumentComponent::GetPowerDrawW() const
{
	return bActive ? FMath::Max(PowerDrawW, 0.0) : 0.0;
}

void UInstrumentComponent::OnSafeModeEntered()
{
	// Default: shut off. Subclasses can override to reset internal state
	// before calling Super::OnSafeModeEntered.
	DeactivateInstrument();
}

void UInstrumentComponent::TickInstrument(double /*DeltaSeconds*/)
{
	// Default is a no-op — scanners / antennas override.
}

void UInstrumentComponent::OnRegister()
{
	Super::OnRegister();

	// Populate the cache eagerly when the component goes live so GetPower()
	// is cheap during tick.
	ResolvePower();
}

void UInstrumentComponent::OnUnregister()
{
	// Drop the draw slot on destruction so a stale weak entry doesn't sit in
	// the power component's tracked list beyond our lifetime.
	if (UPowerComponent* Power = CachedPower.Get())
	{
		Power->UnregisterInstrumentDraw(this);
	}

	Super::OnUnregister();
}

UPowerComponent* UInstrumentComponent::ResolvePower()
{
	if (UPowerComponent* Cached = CachedPower.Get())
	{
		return Cached;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	UPowerComponent* Power = Owner->FindComponentByClass<UPowerComponent>();
	CachedPower = Power;
	return Power;
}

const UPowerComponent* UInstrumentComponent::GetPower() const
{
	return CachedPower.Get();
}
