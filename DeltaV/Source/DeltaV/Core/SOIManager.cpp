// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/SOIManager.h"

#include "DeltaV.h"
#include "Base/CelestialBody.h"

void USOIManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Bodies.Reset();
	VesselAssignments.Reset();
}

void USOIManager::Deinitialize()
{
	Bodies.Reset();
	VesselAssignments.Reset();
	Super::Deinitialize();
}

void USOIManager::RegisterBody(UCelestialBody* Body)
{
	if (!IsValid(Body))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("USOIManager::RegisterBody: null or pending-kill body."));
		return;
	}
	Bodies.AddUnique(Body);
}

void USOIManager::UnregisterBody(UCelestialBody* Body)
{
	Bodies.Remove(Body);
}

TArray<UCelestialBody*> USOIManager::GetAllBodies() const
{
	TArray<UCelestialBody*> Out;
	Out.Reserve(Bodies.Num());
	for (const TObjectPtr<UCelestialBody>& Slot : Bodies)
	{
		if (UCelestialBody* B = Slot.Get(); IsValid(B))
		{
			Out.Add(B);
		}
	}
	return Out;
}

UCelestialBody* USOIManager::QueryCurrentSOI(const FVector& Pos) const
{
	if (!FMath::IsFinite(Pos.X) || !FMath::IsFinite(Pos.Y) || !FMath::IsFinite(Pos.Z))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("USOIManager::QueryCurrentSOI: non-finite vessel position."));
		return nullptr;
	}

	UCelestialBody* Best = nullptr;
	double BestRadius = TNumericLimits<double>::Max();

	for (const TObjectPtr<UCelestialBody>& Slot : Bodies)
	{
		UCelestialBody* B = Slot.Get();
		if (!IsValid(B) || B->SOIRadius <= 0.0)
		{
			continue;
		}
		const double D = (Pos - B->WorldPosition).Length();
		if (D <= B->SOIRadius && B->SOIRadius < BestRadius)
		{
			Best = B;
			BestRadius = B->SOIRadius;
		}
	}
	return Best;
}

UCelestialBody* USOIManager::QueryHysteresisAwareSOI(
	const FVector& Pos,
	UCelestialBody* CurrentAssignment) const
{
	// Two-pass search.
	//
	// Pass 1: if the vessel is still inside its *widened* current-SOI sphere,
	// stay there unless a body with a STRICTLY smaller raw SOI also contains
	// the vessel (nested SOI — we must drop deeper).
	//
	// Pass 2: if pass 1 didn't keep the current assignment, find the body with
	// the smallest raw SOI whose *narrowed* sphere contains the vessel.
	//
	// This preserves the "deepest wins" rule for genuine transitions while
	// avoiding spurious flips when a nearby body's narrowed sphere happens to
	// still contain the vessel.

	if (IsValid(CurrentAssignment) && CurrentAssignment->SOIRadius > 0.0)
	{
		const double WidenedR = CurrentAssignment->SOIRadius * (1.0 + HysteresisFraction);
		const double D = (Pos - CurrentAssignment->WorldPosition).Length();
		if (D <= WidenedR)
		{
			// Current assignment still contains us. Check for a strictly deeper
			// body (narrowed) that also contains us — that is a legitimate
			// nested-SOI transition (rare but possible: asteroid inside Moon SOI).
			UCelestialBody* DeeperCandidate = nullptr;
			for (const TObjectPtr<UCelestialBody>& Slot : Bodies)
			{
				UCelestialBody* B = Slot.Get();
				if (!IsValid(B) || B == CurrentAssignment || B->SOIRadius <= 0.0)
				{
					continue;
				}
				if (B->SOIRadius >= CurrentAssignment->SOIRadius)
				{
					continue;  // only accept strictly-smaller (deeper) SOIs
				}
				const double NarrowedR = B->SOIRadius * (1.0 - HysteresisFraction);
				const double DB = (Pos - B->WorldPosition).Length();
				if (DB <= NarrowedR
					&& (DeeperCandidate == nullptr
						|| B->SOIRadius < DeeperCandidate->SOIRadius))
				{
					DeeperCandidate = B;
				}
			}
			return (DeeperCandidate != nullptr) ? DeeperCandidate : CurrentAssignment;
		}
	}

	// Current assignment lost (or we never had one). Find the narrowest body
	// whose narrowed sphere still contains the vessel.
	UCelestialBody* Best = nullptr;
	for (const TObjectPtr<UCelestialBody>& Slot : Bodies)
	{
		UCelestialBody* B = Slot.Get();
		if (!IsValid(B) || B == CurrentAssignment || B->SOIRadius <= 0.0)
		{
			continue;
		}
		const double NarrowedR = B->SOIRadius * (1.0 - HysteresisFraction);
		const double D = (Pos - B->WorldPosition).Length();
		if (D <= NarrowedR
			&& (Best == nullptr || B->SOIRadius < Best->SOIRadius))
		{
			Best = B;
		}
	}
	return Best;
}

UCelestialBody* USOIManager::UpdateVessel(
	const FGuid& VesselKey,
	const FVector& Pos,
	const double WorldTimeSeconds)
{
	if (!FMath::IsFinite(Pos.X) || !FMath::IsFinite(Pos.Y) || !FMath::IsFinite(Pos.Z))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("USOIManager::UpdateVessel: non-finite vessel position for key %s."),
			*VesselKey.ToString());
		return nullptr;
	}
	if (!FMath::IsFinite(WorldTimeSeconds))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("USOIManager::UpdateVessel: non-finite WorldTimeSeconds."));
		return nullptr;
	}

	FSOIAssignmentState& Assignment = VesselAssignments.FindOrAdd(VesselKey);
	UCelestialBody* CurrentBody = Assignment.CurrentSOI.Get();

	UCelestialBody* Candidate = QueryHysteresisAwareSOI(Pos, CurrentBody);

	// No change AND we've already classified at least once: nothing to do.
	// Hysteresis is the sole transition gate, so if Candidate == CurrentBody
	// it's because the physics genuinely says "stay here". No event needed.
	if (Assignment.bEverClassified && Candidate == CurrentBody)
	{
		return CurrentBody;
	}

	// Commit the new state BEFORE broadcasting, so listeners that re-enter
	// UpdateVessel see consistent state. Snapshot the throttle timestamp to
	// avoid touching Assignment after Broadcast (the TMap may rehash if a
	// listener adds or removes a vessel).
	const bool bWasSilentInterval = (WorldTimeSeconds - Assignment.LastTransitionLogTime)
		>= TransitionLogCooldownSeconds;
	Assignment.CurrentSOI = Candidate;
	Assignment.bEverClassified = true;
	if (bWasSilentInterval)
	{
		Assignment.LastTransitionLogTime = WorldTimeSeconds;
	}

	// Snapshot for Broadcast / log lines (re-entrancy safe).
	const FString VesselKeyString = VesselKey.ToString();
	const FString BodyNameString = (Candidate != nullptr) ? Candidate->BodyName.ToString() : FString();

	if (Candidate != nullptr)
	{
		if (bWasSilentInterval)
		{
			UE_LOG(LogDeltaV, Log,
				TEXT("USOIManager: vessel %s transitioned into SOI '%s' at t=%.3f."),
				*VesselKeyString, *BodyNameString, WorldTimeSeconds);
		}
		OnSOITransitionEnter.Broadcast(VesselKey, Candidate);
	}
	else
	{
		if (bWasSilentInterval)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("USOIManager: vessel %s is orphan (no SOI contains it) at t=%.3f."),
				*VesselKeyString, WorldTimeSeconds);
		}
		OnSOIOrphan.Broadcast(VesselKey);
	}
	return Candidate;
}

UCelestialBody* USOIManager::GetAssignedSOI(const FGuid& VesselKey) const
{
	if (const FSOIAssignmentState* Found = VesselAssignments.Find(VesselKey))
	{
		return Found->CurrentSOI.Get();
	}
	return nullptr;
}

void USOIManager::ForgetVessel(const FGuid& VesselKey)
{
	VesselAssignments.Remove(VesselKey);
}
