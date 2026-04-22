// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UScannerInstrumentComponent.h"

#include "DeltaV.h"
#include "Vehicles/UPowerComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr double KCompositionEstimateMassMin = 1.0;   // kg
	constexpr double KCompositionEstimateMassMax = 1e9;   // kg (~10^6 t)

	// Deterministic pseudo-random from a name hash — so the same target always
	// yields the same estimate in tests without pulling in FRandomStream.
	double HashToFraction(const FName& Name)
	{
		const uint32 Hash = GetTypeHash(Name);
		return static_cast<double>(Hash) / static_cast<double>(MAX_uint32);
	}
}

UScannerInstrumentComponent::UScannerInstrumentComponent()
{
	PowerDrawW = 30.0; // AC#1 reference value.
}

FScanResult UScannerInstrumentComponent::BeginScan(AActor* Target)
{
	if (Target == nullptr || !::IsValid(Target))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UScannerInstrumentComponent::BeginScan: '%s' received null/invalid target"),
			*GetPathName());
		return FScanResult::Invalid();
	}

	if (bActive)
	{
		// Scanner is already running — cancel the in-flight scan before
		// rebinding to a new target. Matches ASatellite gameplay expectations
		// (single scanner, one target at a time).
		CancelScan();
	}

	if (!ActivateInstrument())
	{
		// ActivateInstrument() already logged the specific reason (no power
		// component, depleted, or safe mode). AC#4 — caller receives the
		// Invalid sentinel.
		return FScanResult::Invalid();
	}

	CurrentTarget = Target;
	ExposureElapsed = 0.0;

	// The success of BeginScan is signalled by bActive == true; the synchronous
	// return value is still Invalid() (completion arrives via OnScanCompleted).
	return FScanResult::Invalid();
}

void UScannerInstrumentComponent::CancelScan()
{
	if (!bActive && !CurrentTarget.IsValid())
	{
		return;
	}

	FScanResult InvalidResult = FScanResult::Invalid();
	if (AActor* Target = CurrentTarget.Get())
	{
		InvalidResult.TargetName = Target->GetFName();
	}

	CurrentTarget.Reset();
	ExposureElapsed = 0.0;

	if (bActive)
	{
		DeactivateInstrument();
	}

	OnScanFailed.Broadcast(InvalidResult);
}

void UScannerInstrumentComponent::TickInstrument(double DeltaSeconds)
{
	if (!bActive || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return;
	}

	AActor* Target = CurrentTarget.Get();
	if (Target == nullptr)
	{
		FailScan(FScanResult::Invalid());
		return;
	}

	if (!::IsValid(Target))
	{
		FailScan(FScanResult::Invalid());
		return;
	}

	// Validate authoring-time parameters at runtime — editor ClampMin is only
	// a UX hint, so a C++/Blueprint writer can still slip a NaN or <= 0
	// value through. Fail-closed with a warning rather than silently fail
	// every scan or trivially-complete.
	if (!FMath::IsFinite(RequiredExposureSeconds) || RequiredExposureSeconds <= 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UScannerInstrumentComponent::TickInstrument: '%s' has invalid RequiredExposureSeconds=%.17g — aborting scan"),
			*GetPathName(), RequiredExposureSeconds);
		FScanResult Out = FScanResult::Invalid();
		Out.TargetName = Target->GetFName();
		FailScan(Out);
		return;
	}
	if (!FMath::IsFinite(MaxRangeCm) || MaxRangeCm < 0.0)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UScannerInstrumentComponent::TickInstrument: '%s' has invalid MaxRangeCm=%.17g — aborting scan"),
			*GetPathName(), MaxRangeCm);
		FScanResult Out = FScanResult::Invalid();
		Out.TargetName = Target->GetFName();
		FailScan(Out);
		return;
	}

	const double DistanceCm = DistanceToTargetCm(Target);
	if (!(DistanceCm <= MaxRangeCm))
	{
		// Note: use the inverted form so NaN distances fail the predicate.
		FScanResult Out = FScanResult::Invalid();
		Out.TargetName = Target->GetFName();
		FailScan(Out);
		return;
	}

	// Power is checked via bActive / UPowerComponent; if power cut while we
	// were ticking, ASatellite / OnSafeModeEntered has already deactivated us,
	// so the bActive guard above covers it. Still — defense in depth:
	if (const UPowerComponent* Power = GetPower())
	{
		if (Power->bSafeMode || Power->IsPowerDepleted())
		{
			FScanResult Out = FScanResult::Invalid();
			Out.TargetName = Target->GetFName();
			FailScan(Out);
			return;
		}
	}

	ExposureElapsed += DeltaSeconds;
	if (!FMath::IsFinite(ExposureElapsed))
	{
		// A pathological DeltaSeconds can push accumulation to +Inf via
		// finite-plus-huge rounding; treat as complete rather than spin.
		ExposureElapsed = RequiredExposureSeconds;
	}

	// Use strict >= on doubles — KINDA_SMALL_NUMBER is float-precision and
	// unnecessary here since both operands are doubles. A caller wanting
	// guaranteed completion should pass DeltaSeconds >= RequiredExposureSeconds.
	if (ExposureElapsed >= RequiredExposureSeconds)
	{
		FScanResult Result = ProduceScanResult(Target);

		// Clear state BEFORE broadcasting so a listener that re-enters with
		// BeginScan on a new target sees a clean scanner.
		CurrentTarget.Reset();
		ExposureElapsed = 0.0;
		DeactivateInstrument();

		OnScanCompleted.Broadcast(Result);
	}
}

void UScannerInstrumentComponent::OnSafeModeEntered()
{
	// Explicitly cancel so listeners are informed the scan did not complete
	// before the base class flips bActive off. CancelScan already calls
	// DeactivateInstrument() internally; Super::OnSafeModeEntered() also calls
	// DeactivateInstrument(), but it is idempotent (no-op on !bActive) so the
	// double-call is harmless. We still invoke Super so a future base-class
	// safe-mode hook runs.
	if (bActive || CurrentTarget.IsValid())
	{
		CancelScan();
	}
	Super::OnSafeModeEntered();
}

FScanResult UScannerInstrumentComponent::ProduceScanResult(AActor* Target) const
{
	if (Target == nullptr || !::IsValid(Target))
	{
		return FScanResult::Invalid();
	}

	FScanResult Result;
	Result.bValid = true;
	Result.TargetName = Target->GetFName();

	// Deterministic mass placeholder — a stable mapping from the target's
	// name hash so tests can assert an exact value. Future work (US-046):
	// pull the real mass from AAsteroid.
	const double Fraction = HashToFraction(Target->GetFName());
	Result.EstimatedMassKg = FMath::Lerp(
		KCompositionEstimateMassMin, KCompositionEstimateMassMax, Fraction);

	// Placeholder composition tags — two categories decided by hash parity.
	Result.CompositionTags.Reset();
	const uint32 Hash = GetTypeHash(Target->GetFName());
	if ((Hash & 1u) == 0u)
	{
		Result.CompositionTags.Add(FName(TEXT("Rock")));
		Result.CompositionTags.Add(FName(TEXT("Silicate")));
	}
	else
	{
		Result.CompositionTags.Add(FName(TEXT("Metal")));
		Result.CompositionTags.Add(FName(TEXT("IronNickel")));
	}

	if (UWorld* World = GetWorld())
	{
		Result.CompletedWorldSeconds = World->GetTimeSeconds();
	}

	return Result;
}

double UScannerInstrumentComponent::DistanceToTargetCm(const AActor* Target) const
{
	if (Target == nullptr || !::IsValid(Target))
	{
		return TNumericLimits<double>::Max();
	}

	// Instruments are UActorComponents without a scene transform of their own;
	// range is measured from the owning satellite's actor location. Fine for
	// the US-015 AC#2 scale (kilometers); a later story can swap in an antenna
	// pointing offset if needed.
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return TNumericLimits<double>::Max();
	}

	const FVector MyLocation = Owner->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector Delta = TargetLocation - MyLocation;

	if (!FMath::IsFinite(Delta.X) || !FMath::IsFinite(Delta.Y) || !FMath::IsFinite(Delta.Z))
	{
		return TNumericLimits<double>::Max();
	}

	return Delta.Size();
}

void UScannerInstrumentComponent::FailScan(const FScanResult& InvalidResult)
{
	CurrentTarget.Reset();
	ExposureElapsed = 0.0;
	if (bActive)
	{
		DeactivateInstrument();
	}
	OnScanFailed.Broadcast(InvalidResult);
}
