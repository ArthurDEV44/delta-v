// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UPowerComponent.h"

#include "DeltaV.h"
#include "Vehicles/UInstrumentComponent.h"

namespace
{
	// Seconds per hour — conversion factor for W · s → Wh integration.
	constexpr double KSecondsPerHour = 3600.0;

	// Epsilon (Wh) below which we treat charge as zero for depletion detection.
	constexpr double KPowerDepleteEpsilonWh = 1e-9;

	// ClampMin UPROPERTY meta is editor-only; it does not protect against a
	// C++ or Blueprint call that writes a non-finite or negative value through
	// the raw field at runtime. SanitizeNonNegative and SanitizedCapacity
	// coerce such values to a safe (zero) floor so subsequent FMath::Max /
	// FMath::Clamp calls can't propagate NaN into CurrentChargeWh.
	FORCEINLINE double SanitizeNonNegative(double Value)
	{
		return (FMath::IsFinite(Value) && Value > 0.0) ? Value : 0.0;
	}
}

UPowerComponent::UPowerComponent()
{
	// TickPower is driven by the owning actor (ASatellite::StepSimulation) so
	// there is a single integration path. Leaving the component auto-tick on
	// would double-drive TickPower in PIE and drain the battery 2× as fast
	// as the headless tests report.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Sensible defaults matching the US-015 AC#1 scenario so a satellite
	// built programmatically without further configuration already has a
	// meaningful power budget.
	CapacityWh = 100.0;
	CurrentChargeWh = 100.0;
	SolarPanelPowerW = 50.0;
}

double UPowerComponent::TickPower(double DeltaSeconds)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return 0.0;
	}

	// Sanitize at every read site: a non-finite / negative UPROPERTY value
	// (editor ClampMin is UX-only) would otherwise poison FMath::Max into NaN
	// and then propagate through the clamp into CurrentChargeWh.
	const double Capacity = SanitizeNonNegative(CapacityWh);
	const double SolarW   = bIsInShadow ? 0.0 : SanitizeNonNegative(SolarPanelPowerW);
	const double DrawW    = SanitizeNonNegative(GetActiveDrawW());

	const double PrevCharge    = FMath::IsFinite(CurrentChargeWh) ? CurrentChargeWh : 0.0;
	const double NetW          = SolarW - DrawW;

	if (!FMath::IsFinite(NetW))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UPowerComponent::TickPower: '%s' computed non-finite net power (solar=%.3g, draw=%.3g)"),
			*GetPathName(), SolarW, DrawW);
		return 0.0;
	}

	const double DeltaWh   = NetW * DeltaSeconds / KSecondsPerHour;
	const double NewCharge = FMath::Clamp(PrevCharge + DeltaWh, 0.0, Capacity);

	CurrentChargeWh = NewCharge;

	// Depletion detection: fire once on the 0 crossing so listeners receive a
	// single OnPowerDepleted even if subsequent ticks keep charge at 0.
	const bool bJustDepleted =
		(PrevCharge > KPowerDepleteEpsilonWh) &&
		(NewCharge <= KPowerDepleteEpsilonWh);

	if (bJustDepleted && !bSafeMode)
	{
		bSafeMode = true;

		UE_LOG(LogDeltaV, Warning,
			TEXT("UPowerComponent::TickPower: '%s' depleted — entering safe mode"),
			*GetPathName());

		// Fire AFTER we flip bSafeMode so a listener that queries back into us
		// sees the latched state.
		OnPowerDepleted.Broadcast();
	}

	return NetW;
}

void UPowerComponent::RegisterInstrumentDraw(UInstrumentComponent* Instrument)
{
	if (Instrument == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<UInstrumentComponent>& Existing : TrackedInstruments)
	{
		if (Existing.Get() == Instrument)
		{
			return;
		}
	}

	TrackedInstruments.Emplace(Instrument);
}

void UPowerComponent::UnregisterInstrumentDraw(UInstrumentComponent* Instrument)
{
	if (Instrument == nullptr)
	{
		return;
	}

	TrackedInstruments.RemoveAll(
		[Instrument](const TWeakObjectPtr<UInstrumentComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Instrument;
		});
}

double UPowerComponent::GetActiveDrawW() const
{
	double Sum = 0.0;
	for (const TWeakObjectPtr<UInstrumentComponent>& Weak : TrackedInstruments)
	{
		const UInstrumentComponent* Instrument = Weak.Get();
		if (Instrument == nullptr)
		{
			continue;
		}
		Sum += FMath::Max(Instrument->GetPowerDrawW(), 0.0);
	}
	return Sum;
}

bool UPowerComponent::ExitSafeMode()
{
	if (!bSafeMode)
	{
		return false;
	}

	if (CurrentChargeWh <= KPowerDepleteEpsilonWh)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UPowerComponent::ExitSafeMode: '%s' refused — battery still empty"),
			*GetPathName());
		return false;
	}

	bSafeMode = false;
	OnSafeModeExited.Broadcast();
	return true;
}

void UPowerComponent::SetCurrentChargeWh(double NewChargeWh)
{
	if (!FMath::IsFinite(NewChargeWh))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UPowerComponent::SetCurrentChargeWh: rejected non-finite value %.17g on '%s'"),
			NewChargeWh, *GetPathName());
		return;
	}

	// Upper bound goes through SanitizeNonNegative — a NaN CapacityWh must
	// not leak into the clamp (it would silently clamp every charge to 0).
	CurrentChargeWh = FMath::Clamp(NewChargeWh, 0.0, SanitizeNonNegative(CapacityWh));
}

bool UPowerComponent::IsPowerDepleted() const
{
	return CurrentChargeWh <= KPowerDepleteEpsilonWh;
}

void UPowerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Intentionally does NOT forward to TickPower — the owning actor drives
	// integration (see class-header docs). This override exists only so a
	// future change that re-enables PrimaryComponentTick in a subclass
	// remains explicit about the single-driver contract.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
