// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/ARocket.h"

#include "DeltaV.h"
#include "Vehicles/URocketDef.h"
#include "Vehicles/UStageComponent.h"
#include "Vehicles/UVehiclePartComponent.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr double KStandardGravity = 9.80665;
}

ARocket::ARocket()
{
	// Rocket stays non-ticking — combustion is caller-driven via TickCombustion.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

ARocket* ARocket::SpawnFromDef(UWorld* World, const URocketDef* Def, const FTransform& Transform)
{
	if (World == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("ARocket::SpawnFromDef: null World — refusing to spawn"));
		return nullptr;
	}

	if (Def == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("ARocket::SpawnFromDef: null URocketDef — refusing to spawn"));
		return nullptr;
	}

	FString ValidationError;
	if (!Def->IsValid(&ValidationError))
	{
		// AC#4: explicit log error, no partial actor left in the world.
		UE_LOG(LogDeltaV, Error,
			TEXT("ARocket::SpawnFromDef: invalid URocketDef '%s' — %s"),
			*Def->GetName(), *ValidationError);
		return nullptr;
	}

	ARocket* Rocket = World->SpawnActorDeferred<ARocket>(
		ARocket::StaticClass(), Transform, /*Owner=*/ nullptr, /*Instigator=*/ nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Rocket == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("ARocket::SpawnFromDef: SpawnActorDeferred<ARocket> returned null for def '%s'"),
			*Def->GetName());
		return nullptr;
	}

	// Build the stage stack + payload BEFORE BeginPlay so the base class's
	// initial forced recompute sees the fully populated rocket.
	Rocket->InitFromDef(Def);

	UGameplayStatics::FinishSpawningActor(Rocket, Transform);
	return Rocket;
}

void ARocket::InitFromDef(const URocketDef* Def)
{
	if (bInitialized)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("ARocket::InitFromDef: '%s' already initialized — ignoring second call"),
			*GetName());
		return;
	}

	if (Def == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("ARocket::InitFromDef: '%s' received null def"),
			*GetName());
		return;
	}

	SourceDef = Def;

	USceneComponent* Root = GetRootComponent();

	Stages.Reserve(Def->Stages.Num());
	for (int32 Idx = 0; Idx < Def->Stages.Num(); ++Idx)
	{
		const FStageDef& StageDef = Def->Stages[Idx];

		const FName ComponentName = MakeUniqueObjectName(
			this, UStageComponent::StaticClass(),
			FName(*FString::Printf(TEXT("Stage_%d_%s"), Idx, *StageDef.StageName.ToString())));

		UStageComponent* Stage = NewObject<UStageComponent>(this, UStageComponent::StaticClass(), ComponentName);
		if (Stage == nullptr)
		{
			UE_LOG(LogDeltaV, Error,
				TEXT("ARocket::InitFromDef: '%s' failed to allocate UStageComponent for stage[%d]"),
				*GetName(), Idx);
			continue;
		}

		Stage->InitFromStageDef(StageDef);

		// Set relative location BEFORE attaching so KeepRelativeTransform
		// snaps to the desired offset rather than picking up whatever world
		// transform a freshly-NewObject'd SceneComponent happens to carry
		// under a non-identity spawn transform on the rocket root.
		Stage->SetRelativeLocation(StageDef.LocalMountOffsetCm);
		if (Root != nullptr)
		{
			Stage->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}

		// OnRegister will call AVehicle::RegisterPart and mark the aggregate dirty.
		Stage->RegisterComponent();

		Stages.Add(Stage);
	}

	if (Def->PayloadDryMassKg > 0.0)
	{
		const FName PayloadName = MakeUniqueObjectName(
			this, UVehiclePartComponent::StaticClass(), FName(TEXT("Payload")));

		PayloadPart = NewObject<UVehiclePartComponent>(this, UVehiclePartComponent::StaticClass(), PayloadName);
		if (PayloadPart != nullptr)
		{
			// Route through setters so non-finite / negative values are rejected
			// by UVehiclePartComponent's finite-value gates (defense-in-depth:
			// URocketDef::IsValid already validated, but InitFromDef is public
			// and a future caller may bypass the factory).
			PayloadPart->SetRelativeLocation(Def->PayloadMountOffsetCm);
			if (Root != nullptr)
			{
				PayloadPart->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
			}
			PayloadPart->SetLocalCenterOfMass(FVector::ZeroVector);
			PayloadPart->SetLocalInertiaDiagonal(Def->PayloadInertiaDiagonalKgM2);
			PayloadPart->SetMass(Def->PayloadDryMassKg);
			PayloadPart->RegisterComponent();
		}
		else
		{
			UE_LOG(LogDeltaV, Error,
				TEXT("ARocket::InitFromDef: '%s' failed to allocate payload UVehiclePartComponent"),
				*GetName());
		}
	}

	// BeginPlay will run a forced RecomputeInertialProperties once
	// FinishSpawningActor resumes the spawn flow, so there is no need to
	// recompute here. Firing it now would broadcast OnInertialPropertiesChanged
	// before the actor is fully constructed, which the AVehicle contract does
	// not promise to support.
	bInitialized = true;
}

UStageComponent* ARocket::IgniteNextStage()
{
	for (const TObjectPtr<UStageComponent>& StagePtr : Stages)
	{
		UStageComponent* Stage = StagePtr.Get();
		if (Stage == nullptr)
		{
			continue;
		}
		if (Stage->IsIgnited() || Stage->IsFuelDepleted())
		{
			continue;
		}

		Stage->Ignite();
		return Stage;
	}

	return nullptr;
}

double ARocket::TickCombustion(double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0 || !FMath::IsFinite(DeltaSeconds))
	{
		return 0.0;
	}

	double TotalConsumed = 0.0;
	for (const TObjectPtr<UStageComponent>& StagePtr : Stages)
	{
		UStageComponent* Stage = StagePtr.Get();
		if (Stage == nullptr || !Stage->IsIgnited())
		{
			continue;
		}

		TotalConsumed += Stage->AdvanceCombustion(DeltaSeconds);
	}

	if (TotalConsumed > 0.0)
	{
		// Stage::AdvanceCombustion already routes through SetMass → NotifyOwnerDirty,
		// so the aggregate will refresh on the next recompute. Force it now so
		// GetCurrentTotalMassKg reflects the burn immediately.
		RecomputeInertialProperties();
	}

	return TotalConsumed;
}

double ARocket::GetCurrentTotalMassKg() const
{
	// Sum live parts directly — independent of AVehicle's cached aggregate so
	// tests can probe the math even when the dirty flag has not been consumed.
	double Sum = 0.0;

	for (const TObjectPtr<UStageComponent>& StagePtr : Stages)
	{
		const UStageComponent* Stage = StagePtr.Get();
		if (Stage == nullptr)
		{
			continue;
		}
		Sum += FMath::Max(Stage->DryMassKg + Stage->FuelMassRemainingKg, 0.0);
	}

	if (PayloadPart != nullptr)
	{
		Sum += FMath::Max(PayloadPart->Mass, 0.0);
	}

	return Sum;
}

void ARocket::Destroyed()
{
	// Cascade destroy to any attached child actors (satellite payload mounted
	// by UVehicleFactory::Spawn). UE detaches but does not destroy; without
	// this the satellite would leak as an orphan world actor.
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, /*bResetArray=*/ true, /*bRecursivelyIncludeAttachedActors=*/ true);
	for (AActor* Child : Attached)
	{
		if (Child != nullptr && !Child->IsActorBeingDestroyed())
		{
			Child->Destroy();
		}
	}

	Super::Destroyed();
}

double ARocket::CalculateTheoreticalDeltaV() const
{
	// Payload mass rides with every stage's final mass (never separates).
	const double PayloadMass = (PayloadPart != nullptr) ? FMath::Max(PayloadPart->Mass, 0.0) : 0.0;

	// Pre-compute the cumulative mass ABOVE each stage (i.e., payload plus all
	// stages with index > i), which is what remains attached after stage i
	// separates.
	TArray<double> MassAbove;
	MassAbove.SetNumZeroed(Stages.Num());

	{
		double Running = PayloadMass;
		for (int32 Idx = Stages.Num() - 1; Idx >= 0; --Idx)
		{
			MassAbove[Idx] = Running;

			const UStageComponent* Stage = Stages[Idx].Get();
			if (Stage != nullptr)
			{
				Running += FMath::Max(Stage->DryMassKg + Stage->FuelMassRemainingKg, 0.0);
			}
		}
	}

	double TotalDeltaV = 0.0;

	for (int32 Idx = 0; Idx < Stages.Num(); ++Idx)
	{
		const UStageComponent* Stage = Stages[Idx].Get();
		if (Stage == nullptr)
		{
			continue;
		}

		const double StageWet = FMath::Max(Stage->DryMassKg + Stage->FuelMassRemainingKg, 0.0);
		const double StageDry = FMath::Max(Stage->DryMassKg, 0.0);
		const double FuelLeft = FMath::Max(Stage->FuelMassRemainingKg, 0.0);
		const double Isp = Stage->SpecificImpulseSeconds;

		if (FuelLeft > 0.0 && Isp <= 0.0 && !bWarnedInvalidIsp)
		{
			// PRD US-018 AC#3: a fueled stage with non-positive Isp contributes
			// zero delta-V and surfaces a log warning so bad data is visible.
			// Guarded so the HUD (US-017) polling CalculateTheoreticalDeltaV
			// at 10 Hz cannot spam the log.
			UE_LOG(LogDeltaV, Warning,
				TEXT("ARocket::CalculateTheoreticalDeltaV: '%s' stage '%s' has invalid Isp=%.17g with %.3f kg of fuel — stage contributes 0 dV"),
				*GetName(),
				*Stage->StageName.ToString(),
				Isp,
				FuelLeft);
			bWarnedInvalidIsp = true;
		}

		if (FuelLeft <= 0.0 || Isp <= 0.0 || StageDry <= 0.0)
		{
			continue;
		}

		const double MassInit  = MassAbove[Idx] + StageWet;
		const double MassFinal = MassAbove[Idx] + StageDry;

		if (MassFinal <= 0.0 || MassInit <= MassFinal)
		{
			continue;
		}

		TotalDeltaV += Isp * KStandardGravity * FMath::Loge(MassInit / MassFinal);
	}

	return TotalDeltaV;
}
