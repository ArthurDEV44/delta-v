// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UStageComponent.h"

#include "DeltaV.h"
#include "Vehicles/AVehicle.h"
#include "Vehicles/URocketDef.h"

#include "Curves/RichCurve.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Standard gravity (m/s²) for Isp → mass-flow conversion. SI.
	constexpr double KStandardGravity = 9.80665;

	// Epsilon (kg) below which we treat remaining fuel as zero and extinguish.
	constexpr double KFuelDepleteEpsilonKg = 1e-6;
}

UStageComponent::UStageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

void UStageComponent::InitFromStageDef(const FStageDef& StageDef)
{
	StageName = StageDef.StageName;
	DryMassKg = StageDef.DryMassKg;
	FuelMassInitialKg = StageDef.FuelMassKg;
	FuelMassRemainingKg = StageDef.FuelMassKg;
	SpecificImpulseSeconds = StageDef.SpecificImpulseSeconds;
	MaxThrustNewtons = StageDef.MaxThrustNewtons;
	ThrustCurve = StageDef.ThrustCurve;

	bIgnited = false;
	BurnTimeSeconds = 0.0;

	// Inherited UVehiclePartComponent state — mass / CoM / inertia.
	// LocalMountOffset is applied to the SceneComponent's RelativeLocation by
	// ARocket::InitFromDef; LocalCenterOfMass stays zero (stage CoM at mount
	// point). Setting both would double-count the offset in the parent vehicle's
	// aggregation (which does GetRelativeLocation() + LocalCenterOfMass).
	LocalCenterOfMass = FVector::ZeroVector;
	LocalInertiaDiagonal = StageDef.LocalInertiaDiagonalKgM2;

	SyncAggregateMass();
}

void UStageComponent::Ignite()
{
	if (bIgnited)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Ignite: '%s' is already ignited"),
			*GetPathName());
		return;
	}

	if (FuelMassRemainingKg <= KFuelDepleteEpsilonKg)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Ignite: '%s' has no fuel, cannot ignite"),
			*GetPathName());
		return;
	}

	bIgnited = true;
	BurnTimeSeconds = 0.0;
}

void UStageComponent::Extinguish()
{
	bIgnited = false;
}

bool UStageComponent::IsFuelDepleted() const
{
	return FuelMassRemainingKg <= KFuelDepleteEpsilonKg;
}

double UStageComponent::SampleThrustMultiplier(double BurnTime) const
{
	const FRichCurve* Curve = ThrustCurve.GetRichCurveConst();
	if (Curve == nullptr || Curve->GetNumKeys() == 0)
	{
		// Unset curve → simple engine running at full throttle.
		return 1.0;
	}

	const float Value = Curve->Eval(static_cast<float>(BurnTime));

	// FMath::Clamp(NaN, 0, 1) returns NaN (NaN compares false with both
	// bounds), which would poison mass-flow and silently corrupt
	// FuelMassRemainingKg. Reject non-finite curve output and fail closed
	// (zero thrust) so a pathological designer-authored curve cannot crash
	// the combustion loop.
	if (!FMath::IsFinite(Value))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::SampleThrustMultiplier: '%s' ThrustCurve returned non-finite value at t=%.3f — treating as 0"),
			*GetPathName(), BurnTime);
		return 0.0;
	}

	return FMath::Clamp(static_cast<double>(Value), 0.0, 1.0);
}

double UStageComponent::GetCurrentThrustNewtons(double OverrideBurnTimeSeconds) const
{
	if (!bIgnited || IsFuelDepleted())
	{
		return 0.0;
	}

	const double Time = (OverrideBurnTimeSeconds >= 0.0) ? OverrideBurnTimeSeconds : BurnTimeSeconds;
	return MaxThrustNewtons * SampleThrustMultiplier(Time);
}

double UStageComponent::GetMassFlowRateKgPerSec(double OverrideBurnTimeSeconds) const
{
	const double Thrust = GetCurrentThrustNewtons(OverrideBurnTimeSeconds);
	if (Thrust <= 0.0 || SpecificImpulseSeconds <= 0.0)
	{
		return 0.0;
	}

	// m_dot = F / (Isp * g0)
	return Thrust / (SpecificImpulseSeconds * KStandardGravity);
}

double UStageComponent::AdvanceCombustion(double DeltaSeconds)
{
	if (!bIgnited || DeltaSeconds <= 0.0 || !FMath::IsFinite(DeltaSeconds))
	{
		return 0.0;
	}

	if (IsFuelDepleted())
	{
		// Nothing to burn — ensure we're not still flagged ignited.
		Extinguish();
		return 0.0;
	}

	// Simple Euler: sample thrust at the start of the interval. For a flat
	// ThrustCurve the mass decay is exactly linear (AC#2's assertion); for a
	// shaped curve the per-step error is O(dt·|dThrust/dt|), tolerable at
	// 60+ Hz call rates.
	const double MassFlow = GetMassFlowRateKgPerSec(BurnTimeSeconds);
	double Consumed = MassFlow * DeltaSeconds;

	// Reject a non-finite product defensively — GetMassFlowRateKgPerSec can
	// only return a non-finite value if Thrust is non-finite, which the
	// thrust-curve sampler and URocketDef::IsValid already guard, but a
	// late-loaded curve mutation in the editor could slip through.
	if (!FMath::IsFinite(Consumed))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::AdvanceCombustion: '%s' computed non-finite Consumed=%.17g — skipping step"),
			*GetPathName(), Consumed);
		return 0.0;
	}

	// Clamp to available fuel so we never underflow.
	if (Consumed > FuelMassRemainingKg)
	{
		Consumed = FuelMassRemainingKg;
	}

	FuelMassRemainingKg -= Consumed;
	BurnTimeSeconds += DeltaSeconds;

	// Push the new total mass through the base class setter so the owning
	// AVehicle sees a standard mass delta and the OnInertialPropertiesChanged
	// threshold logic fires naturally.
	SyncAggregateMass();

	if (IsFuelDepleted())
	{
		Extinguish();
	}

	return Consumed;
}

AVehicle* UStageComponent::Separate(UWorld* WorldOverride)
{
	// Idempotency guard — prevents double-spawn if Separate is re-entered
	// from an OnInertialPropertiesChanged broadcast fired by our own
	// UnregisterPart call below, or from any other caller retrying.
	if (bSeparated)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Separate: '%s' already separated — refusing second call"),
			*GetPathName());
		return nullptr;
	}

	if (!::IsValid(this))
	{
		return nullptr;
	}

	AVehicle* OwnerVehicle = Cast<AVehicle>(GetOwner());
	if (OwnerVehicle == nullptr)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Separate: '%s' has no AVehicle owner"),
			*GetPathName());
		return nullptr;
	}

	UWorld* World = (WorldOverride != nullptr) ? WorldOverride : OwnerVehicle->GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Separate: '%s' has no world to spawn into"),
			*GetPathName());
		return nullptr;
	}

	// Capture state for the detached actor BEFORE we destroy ourselves.
	const FName PrevStageName = StageName;
	const double DryMassSnapshot = DryMassKg;
	const double FuelRemainingSnapshot = FMath::Max(FuelMassRemainingKg, 0.0);
	const FVector LocalInertiaSnapshot = LocalInertiaDiagonal;
	const FTransform WorldXform = GetComponentTransform();

	if (FuelRemainingSnapshot > 0.0)
	{
		// Contract: separation preserves mass — residual fuel rides with the
		// jettisoned shell. Typical callers drain the stage first; warn so
		// mistakes are visible in the log without being fatal.
		UE_LOG(LogDeltaV, Warning,
			TEXT("UStageComponent::Separate: '%s' separated with %.3f kg of residual fuel — carried on detached shell"),
			*PrevStageName.ToString(), FuelRemainingSnapshot);
	}

	// Set the guard NOW — any reentrant call that fires during the spawn
	// sequence (via delegate broadcast) will early-out.
	bSeparated = true;

	AVehicle* Detached = World->SpawnActorDeferred<AVehicle>(
		AVehicle::StaticClass(), WorldXform, /*Owner=*/ nullptr, /*Instigator=*/ nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Detached == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UStageComponent::Separate: SpawnActorDeferred<AVehicle> failed for '%s'"),
			*PrevStageName.ToString());
		// Rolling back bSeparated here is wrong (nothing to separate from has
		// changed yet, but a retry would hit the same spawn failure). Leave
		// guarded so we do not loop.
		return nullptr;
	}

	// Create and configure the dry-mass part. Route ALL field writes through
	// setters so non-finite / negative values are rejected and the owning
	// vehicle is notified dirty.
	UVehiclePartComponent* DryMassPart = NewObject<UVehiclePartComponent>(
		Detached, UVehiclePartComponent::StaticClass(),
		MakeUniqueObjectName(Detached, UVehiclePartComponent::StaticClass(),
			FName(*FString::Printf(TEXT("SeparatedStage_%s"), *PrevStageName.ToString()))));

	if (DryMassPart == nullptr)
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UStageComponent::Separate: failed to create dry-mass part on detached actor"));
		Detached->Destroy();
		return nullptr;
	}

	DryMassPart->SetLocalCenterOfMass(FVector::ZeroVector);
	DryMassPart->SetLocalInertiaDiagonal(LocalInertiaSnapshot);
	DryMassPart->SetMass(DryMassSnapshot + FuelRemainingSnapshot);

	USceneComponent* DetachedRoot = Detached->GetRootComponent();
	if (DetachedRoot != nullptr)
	{
		DryMassPart->AttachToComponent(DetachedRoot, FAttachmentTransformRules::KeepRelativeTransform);
	}
	DryMassPart->RegisterComponent();

	// If registration silently failed (UE returns without an exception), the
	// part will not be a live scene component. Verify and roll back to avoid
	// leaving a deferred actor dangling before we call FinishSpawningActor.
	if (!DryMassPart->IsRegistered())
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UStageComponent::Separate: dry-mass part failed to register — destroying detached actor"));
		Detached->Destroy();
		return nullptr;
	}

	// Finish spawning — runs BeginPlay, which does a forced RecomputeInertialProperties.
	UGameplayStatics::FinishSpawningActor(Detached, WorldXform);

	// Now detach ourselves from the original rocket. UnregisterPart drives the
	// parent's dirty flag; DestroyComponent severs the scene-component attachment
	// (and fires a redundant-but-safe UnregisterPart via OnUnregister).
	OwnerVehicle->UnregisterPart(this);
	DestroyComponent();

	// Force an immediate recompute on the parent so callers observing mass
	// right after Separate() see the post-separation total without waiting
	// for a tick.
	OwnerVehicle->RecomputeInertialProperties();

	UE_LOG(LogDeltaV, Log,
		TEXT("UStageComponent::Separate: jettisoned stage '%s' (dry %.3f kg + fuel %.3f kg on detached shell)"),
		*PrevStageName.ToString(), DryMassSnapshot, FuelRemainingSnapshot);

	return Detached;
}

void UStageComponent::SyncAggregateMass()
{
	const double NewMass = FMath::Max(DryMassKg + FuelMassRemainingKg, 0.0);

	// Route through the base class setter so the owning AVehicle is notified
	// via NotifyOwnerDirty; a direct field write would skip the dirty flag.
	SetMass(NewMass);
}
