// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/OriginRebasingSubsystem.h"

#include "DeltaV.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	// UE physics / transform unit is centimetres; our PRD-facing threshold is
	// in metres. Keep the conversion factor local so the threshold-squared
	// comparison stays in cm² without polluting the header.
	constexpr double KMetersToCm = 100.0;

	// Hard safety bound on the actor's local-frame magnitude before we cast to
	// FIntVector (int32). With INT32_MAX ≈ 2.147e9 cm ≈ 21,474 km, we leave a
	// margin and refuse anything above 1e9 cm (10,000 km) — well beyond the
	// 10 km rebase threshold, so reaching this guard means rebasing was
	// inhibited for an unreasonably long window.
	constexpr double KMaxActorLocMagnitudeCm = 1.0e9;

	// Floor on RebaseThresholdMeters. Property metadata ClampMin enforces this
	// for editor input but not for savegame loads or Blueprint sets — we
	// re-check at runtime.
	constexpr double KMinThresholdMeters = 100.0;
}

void UOriginRebasingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Sync the mirrored origin with the world's starting origin. In PIE /
	// packaged builds this is almost always (0, 0, 0), but a cooked level
	// that was authored with a non-zero origin would otherwise leave the
	// mirror out of step for the lifetime of the subsystem.
	if (UWorld* World = GetWorld())
	{
		MirroredOriginOffsetCm = World->OriginLocation;
	}
}

void UOriginRebasingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	EvaluateAndRebase();
}

TStatId UOriginRebasingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOriginRebasingSubsystem, STATGROUP_Tickables);
}

void UOriginRebasingSubsystem::SetTrackedActor(AActor* Actor)
{
	TrackedActor = Actor;
}

void UOriginRebasingSubsystem::SetBurnActive(bool bActive)
{
	bBurnActive = bActive;
	// Each burn cycle is a discrete event — a second burn in the same session
	// should get its own chance to log the mid-burn-rebase warning. Reset the
	// throttle on the off-transition.
	if (!bActive)
	{
		bHasLoggedBurnWarning = false;
	}
}

bool UOriginRebasingSubsystem::EvaluateAndRebase()
{
	AActor* Actor = TrackedActor.Get();
	if (Actor == nullptr)
	{
		return false;
	}

	// Defensive threshold validation. ClampMin metadata is UI-only; a savegame
	// or BP setter can deliver a non-finite or <= 0 value that would produce
	// NaN comparisons (always false) or a rebase-per-tick storm. Refuse.
	if (!FMath::IsFinite(RebaseThresholdMeters)
		|| RebaseThresholdMeters < KMinThresholdMeters)
	{
		return false;
	}

	// Distance from the current origin in the ACTIVE local frame. GetActorLocation
	// is already expressed relative to the current UWorld::OriginLocation, so a
	// simple magnitude check here captures the PRD's "actor 15 km from origin".
	const FVector ActorLocCm = Actor->GetActorLocation();

	// Non-finite guard first — any NaN / Inf component poisons every magnitude
	// check below, so short-circuit with a clean error.
	if (ActorLocCm.ContainsNaN())
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UOriginRebasingSubsystem: tracked actor has non-finite position; rebase aborted."));
		return false;
	}

	// Int32-safety guard. If rebasing was inhibited for an unreasonably long
	// window (scripted teleport, suppression bug, disabled subsystem) the
	// actor's local cm position could overflow int32 on the upcoming cast
	// (INT32_MAX ≈ 21,474 km in cm). Refuse at an order-of-magnitude below
	// that so downstream math is never tempted to saturate.
	if (ActorLocCm.GetAbsMax() > KMaxActorLocMagnitudeCm)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UOriginRebasingSubsystem: actor local magnitude %.3e cm exceeds int32-safe bound; rebase aborted."),
			ActorLocCm.GetAbsMax());
		return false;
	}

	const double ThresholdCm = RebaseThresholdMeters * KMetersToCm;
	const double ThresholdSqCm = ThresholdCm * ThresholdCm;
	if (ActorLocCm.SizeSquared() <= ThresholdSqCm)
	{
		return false;
	}

	// Compute the absolute new origin in engine cm. SetNewWorldOrigin wants
	// the ABSOLUTE coordinate of the point that will become (0,0,0) in the
	// post-rebase local frame — i.e., the current absolute world position
	// of the tracked actor.
	const FIntVector ActorLocIntCm(
		FMath::RoundToInt(ActorLocCm.X),
		FMath::RoundToInt(ActorLocCm.Y),
		FMath::RoundToInt(ActorLocCm.Z));

	UWorld* World = GetWorld();
	const FIntVector CurrentAbsoluteOriginCm =
		World ? World->OriginLocation : MirroredOriginOffsetCm;
	const FIntVector NewAbsoluteOriginCm = CurrentAbsoluteOriginCm + ActorLocIntCm;

	// Commit the telemetry BEFORE calling SetNewWorldOrigin. The engine
	// cascades ApplyWorldOffset through every registered actor synchronously,
	// and observers wired to OnOriginRebased may read LastAppliedOffsetMeters
	// / RebaseCount as part of their handler. Having them current simplifies
	// the contract.
	LastAppliedOffsetMeters = ActorLocCm / KMetersToCm;
	++RebaseCount;
	MirroredOriginOffsetCm = NewAbsoluteOriginCm;

	// AC#4 — warn exactly once per lifetime when a rebase fires mid-burn.
	if (bBurnActive && !bHasLoggedBurnWarning)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UOriginRebasingSubsystem: rebasing during active burn may affect physics (offset = (%.3f, %.3f, %.3f) m)."),
			LastAppliedOffsetMeters.X,
			LastAppliedOffsetMeters.Y,
			LastAppliedOffsetMeters.Z);
		bHasLoggedBurnWarning = true;
	}

	// AC#3 — informational log for the debug HUD to surface.
	UE_LOG(LogDeltaV, Log,
		TEXT("UOriginRebasingSubsystem: world origin rebased by (%.3f, %.3f, %.3f) m, count=%d."),
		LastAppliedOffsetMeters.X,
		LastAppliedOffsetMeters.Y,
		LastAppliedOffsetMeters.Z,
		RebaseCount);

	OnOriginRebased.Broadcast(LastAppliedOffsetMeters);

	if (World)
	{
		// UE cascades ApplyWorldOffset to every actor / component here;
		// no manual bookkeeping required for Chaos bodies or attached meshes.
		World->SetNewWorldOrigin(NewAbsoluteOriginCm);
	}

	return true;
}
