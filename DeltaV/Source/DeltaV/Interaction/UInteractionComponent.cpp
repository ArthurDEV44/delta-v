// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/UInteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "DeltaV.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Interaction/IInteractable.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ScanIntervalSeconds > 0.0f)
	{
		TimeSinceLastScan += DeltaTime;
		if (TimeSinceLastScan < ScanIntervalSeconds)
		{
			return;
		}
		TimeSinceLastScan = 0.0f;
	}

	ScanForInteractable();
}

AActor* UInteractionComponent::ScanForInteractable()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		SetCurrentInteractable(nullptr);
		return nullptr;
	}

	FVector Origin;
	FVector Forward;
	GetProbeFrame(Origin, Forward);

	// Collect every IInteractable in the world. For a 4-room base this is
	// trivial (< 20 actors); for larger levels a future optimization can scope
	// to an overlap sphere. Done lazily per scan — no cached state so actor
	// spawns / destroys during play are picked up next tick.
	TArray<AActor*> Candidates;
	AActor* Owner = GetOwner();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (Candidate == nullptr || Candidate == Owner)
		{
			continue;
		}
		if (!Candidate->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		{
			continue;
		}
		Candidates.Add(Candidate);
	}

	AActor* Winner = PickBestCandidate(Origin, Forward, MaxDistanceCm, MinAlignmentCosine, Candidates);
	SetCurrentInteractable(Winner);
	return Winner;
}

bool UInteractionComponent::TryInteract(APlayerController* PlayerController)
{
	AActor* Target = CurrentInteractable.Get();
	if (Target == nullptr)
	{
		return false;
	}

	IInteractable* Interactable = Cast<IInteractable>(Target);
	if (Interactable == nullptr)
	{
		// Defensive: target was removed from the interface but still held in our cache.
		SetCurrentInteractable(nullptr);
		return false;
	}

	if (!Interactable->IsInteractionEnabled())
	{
		OnInteractAttemptedWhileDisabled.Broadcast(Target);
		return false;
	}

	Interactable->OnInteract(PlayerController);
	return true;
}

FText UInteractionComponent::GetCurrentPrompt() const
{
	AActor* Target = CurrentInteractable.Get();
	if (Target == nullptr)
	{
		return FText::GetEmpty();
	}

	const IInteractable* Interactable = Cast<IInteractable>(Target);
	if (Interactable == nullptr)
	{
		return FText::GetEmpty();
	}

	const FText Label = Interactable->GetInteractionText();
	if (Interactable->IsInteractionEnabled())
	{
		return FText::Format(NSLOCTEXT("DeltaV.Interaction", "PromptEnabled", "E — {0}"), Label);
	}
	return FText::Format(NSLOCTEXT("DeltaV.Interaction", "PromptDisabled", "E — {0} (offline)"), Label);
}

bool UInteractionComponent::IsCurrentEnabled() const
{
	if (const IInteractable* Interactable = Cast<IInteractable>(CurrentInteractable.Get()))
	{
		return Interactable->IsInteractionEnabled();
	}
	return false;
}

void UInteractionComponent::SetCurrentInteractable(AActor* Target)
{
	AActor* Previous = CurrentInteractable.Get();
	if (Previous == Target)
	{
		return;
	}
	CurrentInteractable = Target;
	OnInteractableChanged.Broadcast(Target);
}

AActor* UInteractionComponent::PickBestCandidate(
	const FVector& OriginCm,
	const FVector& ForwardUnit,
	double MaxDistanceCm,
	double MinAlignmentCosine,
	const TArray<AActor*>& Candidates)
{
	if (MaxDistanceCm <= 0.0 || Candidates.Num() == 0)
	{
		return nullptr;
	}
	if (!FMath::IsFinite(ForwardUnit.X) || !FMath::IsFinite(ForwardUnit.Y) || !FMath::IsFinite(ForwardUnit.Z)
		|| ForwardUnit.IsNearlyZero())
	{
		return nullptr;
	}

	const FVector Forward = ForwardUnit.GetSafeNormal();

	AActor* Best = nullptr;
	double BestAlignment = -TNumericLimits<double>::Max();
	double BestDistance = TNumericLimits<double>::Max();

	for (AActor* Candidate : Candidates)
	{
		if (Candidate == nullptr)
		{
			continue;
		}
		if (!Candidate->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		{
			continue;
		}

		const FVector Delta = Candidate->GetActorLocation() - OriginCm;
		const double Distance = Delta.Size();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > MaxDistanceCm)
		{
			// Distance == 0 → cannot compute alignment; skip (unlikely in practice,
			// would mean the candidate is co-located with the commander).
			continue;
		}

		const FVector ToCandidate = Delta / Distance;
		const double Alignment = FVector::DotProduct(Forward, ToCandidate);
		if (Alignment < MinAlignmentCosine)
		{
			continue;
		}

		// Best-alignment wins; tie-break by closer distance.
		if (Alignment > BestAlignment
			|| (FMath::IsNearlyEqual(Alignment, BestAlignment) && Distance < BestDistance))
		{
			Best = Candidate;
			BestAlignment = Alignment;
			BestDistance = Distance;
		}
	}

	return Best;
}

void UInteractionComponent::GetProbeFrame(FVector& OutOriginCm, FVector& OutForwardUnit) const
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		OutOriginCm = FVector::ZeroVector;
		OutForwardUnit = FVector::ForwardVector;
		return;
	}

	// Prefer the first UCameraComponent — on the commander this is the follow
	// camera attached to the spring arm, giving a natural crosshair-origin probe.
	if (UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>())
	{
		OutOriginCm = Camera->GetComponentLocation();
		OutForwardUnit = Camera->GetForwardVector();
		return;
	}

	OutOriginCm = Owner->GetActorLocation();
	OutForwardUnit = Owner->GetActorForwardVector();
}
