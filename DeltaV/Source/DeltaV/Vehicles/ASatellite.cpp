// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/ASatellite.h"

#include "DeltaV.h"
#include "Vehicles/UInstrumentComponent.h"
#include "Vehicles/UPowerComponent.h"

ASatellite::ASatellite()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PowerComponent = CreateDefaultSubobject<UPowerComponent>(TEXT("PowerComponent"));
}

void ASatellite::BeginPlay()
{
	Super::BeginPlay();

	// Wire the depletion → safe-mode propagation exactly once per actor
	// lifetime. BeginPlay can run more than once in some re-registration
	// scenarios; bPowerListenerBound prevents duplicate subscriptions.
	if (!bPowerListenerBound && PowerComponent != nullptr)
	{
		PowerComponent->OnPowerDepleted.AddDynamic(this, &ASatellite::HandlePowerDepleted);
		bPowerListenerBound = true;
	}
}

void ASatellite::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Drive the simulation through the shared helper so tests and gameplay
	// take the same integration path.
	StepSimulation(static_cast<double>(DeltaTime));
}

void ASatellite::StepSimulation(double DeltaSeconds)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return;
	}

	if (PowerComponent != nullptr)
	{
		PowerComponent->TickPower(DeltaSeconds);
	}

	// Iterate a snapshot of the instrument list so a virtual TickInstrument
	// that (legitimately) unregisters itself cannot invalidate iteration.
	// Cheap: TArray<TObjectPtr<...>> copy is just pointer-width per entry.
	TArray<TObjectPtr<UInstrumentComponent>> Snapshot = Instruments;
	for (const TObjectPtr<UInstrumentComponent>& InstrPtr : Snapshot)
	{
		UInstrumentComponent* Instrument = InstrPtr.Get();
		if (Instrument == nullptr)
		{
			continue;
		}
		Instrument->TickInstrument(DeltaSeconds);
	}
}

void ASatellite::RegisterInstrument(UInstrumentComponent* Instrument)
{
	if (Instrument == nullptr)
	{
		return;
	}

	for (const TObjectPtr<UInstrumentComponent>& Existing : Instruments)
	{
		if (Existing.Get() == Instrument)
		{
			return;
		}
	}

	Instruments.Add(Instrument);
}

void ASatellite::UnregisterInstrument(UInstrumentComponent* Instrument)
{
	if (Instrument == nullptr)
	{
		return;
	}

	Instruments.RemoveAll(
		[Instrument](const TObjectPtr<UInstrumentComponent>& Existing)
		{
			return Existing.Get() == nullptr || Existing.Get() == Instrument;
		});
}

bool ASatellite::IsSafeMode() const
{
	return PowerComponent != nullptr && PowerComponent->bSafeMode;
}

bool ASatellite::IsMinimalBeaconOn() const
{
	return PowerComponent != nullptr && PowerComponent->bMinimalBeaconOn;
}

void ASatellite::HandlePowerDepleted()
{
	UE_LOG(LogDeltaV, Warning,
		TEXT("ASatellite::HandlePowerDepleted: '%s' entering safe mode — shutting down instruments (beacon stays on)"),
		*GetName());

	// Snapshot before iterating — a subclass override of OnSafeModeEntered
	// could legitimately call back into ASatellite::UnregisterInstrument,
	// which would mutate Instruments under iteration. TObjectPtr copy is
	// pointer-sized per entry.
	TArray<TObjectPtr<UInstrumentComponent>> Snapshot = Instruments;
	for (const TObjectPtr<UInstrumentComponent>& InstrPtr : Snapshot)
	{
		UInstrumentComponent* Instrument = InstrPtr.Get();
		if (Instrument == nullptr)
		{
			continue;
		}
		Instrument->OnSafeModeEntered();
	}
}
