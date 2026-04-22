// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/AVehicle.h"

#include "DeltaV.h"
#include "Vehicles/UVehiclePartComponent.h"

#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"

namespace
{
	// Centimeters -> meters. All part CoMs are in UE native cm; inertia is SI (kg·m²).
	constexpr double KCmToM = 0.01;

	// Floor for the fractional-threshold denominator so broadcast logic behaves
	// sensibly when LastBroadcastMass is near zero (e.g., empty vehicle just
	// received its first part).
	constexpr double KBroadcastMassFloorKg = 1.0;

	// FName used by "show debug Vehicle" toggles.
	const FName KShowDebugCategory(TEXT("Vehicle"));
}

AVehicle::AVehicle()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Provide a root so parts can attach under a SceneComponent parent. Using a
	// plain USceneComponent keeps the base generic; subclasses (ARocket, ASatellite)
	// can replace with a physics-enabled primitive as needed.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVehicle::BeginPlay()
{
	Super::BeginPlay();

	// Parts register themselves via OnRegister before BeginPlay; but a freshly
	// spawned actor with no parts should land in the documented degenerate state
	// deterministically rather than holding the constructor defaults.
	RecomputeInertialProperties(/*bForce=*/ true);
}

void AVehicle::MarkInertialPropertiesDirty()
{
	bInertialPropertiesDirty = true;
}

void AVehicle::RecomputeInertialProperties(bool bForce)
{
	if (!bInertialPropertiesDirty && !bForce)
	{
		return;
	}

	const double PrevBroadcastMass = LastBroadcastMass;

	DoRecompute();

	const double Denominator = FMath::Max(FMath::Abs(PrevBroadcastMass), KBroadcastMassFloorKg);
	const double MassDelta = FMath::Abs(TotalMass - PrevBroadcastMass);
	const bool bFirstPopulatedBroadcast = (PrevBroadcastMass == 0.0) && (TotalMass > 0.0);
	const bool bCrossedThreshold = MassDelta >= (MassChangeBroadcastThreshold * Denominator);

	// Clear the dirty flag *before* broadcast so a listener that calls
	// MarkInertialPropertiesDirty / RegisterPart / SetMass inside its handler
	// leaves us correctly flagged dirty on return, to be consumed next frame.
	bInertialPropertiesDirty = false;

	if (bFirstPopulatedBroadcast || bCrossedThreshold)
	{
		LastBroadcastMass = TotalMass;
		OnInertialPropertiesChanged.Broadcast(TotalMass, CenterOfMass, MomentOfInertia);
	}
}

void AVehicle::RegisterPart(UVehiclePartComponent* Part)
{
	if (Part == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<UVehiclePartComponent>& Existing : RegisteredParts)
	{
		if (Existing.Get() == Part)
		{
			return;
		}
	}

	RegisteredParts.Emplace(Part);
	MarkInertialPropertiesDirty();
}

void AVehicle::UnregisterPart(UVehiclePartComponent* Part)
{
	if (Part == nullptr)
	{
		return;
	}

	const int32 RemovedCount = RegisteredParts.RemoveAll(
		[Part](const TWeakObjectPtr<UVehiclePartComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Part;
		});

	if (RemovedCount > 0)
	{
		MarkInertialPropertiesDirty();
	}
}

TArray<UVehiclePartComponent*> AVehicle::GetRegisteredParts() const
{
	TArray<UVehiclePartComponent*> Result;
	Result.Reserve(RegisteredParts.Num());

	for (const TWeakObjectPtr<UVehiclePartComponent>& Weak : RegisteredParts)
	{
		if (UVehiclePartComponent* Part = Weak.Get())
		{
			Result.Add(Part);
		}
	}

	return Result;
}

FString AVehicle::GetDebugInfoString() const
{
	// Format: 3-decimal precision on all numeric fields per AC#1.
	return FString::Printf(
		TEXT("Vehicle '%s'\n")
		TEXT("  TotalMass      : %.3f kg\n")
		TEXT("  CenterOfMass   : (%.3f, %.3f, %.3f) cm\n")
		TEXT("  MomentOfInertia (kg.m^2, about CoM, local frame):\n")
		TEXT("    [%.3f, %.3f, %.3f]\n")
		TEXT("    [%.3f, %.3f, %.3f]\n")
		TEXT("    [%.3f, %.3f, %.3f]\n")
		TEXT("  Parts          : %d registered, dirty = %s"),
		*GetName(),
		TotalMass,
		CenterOfMass.X, CenterOfMass.Y, CenterOfMass.Z,
		MomentOfInertia.M[0][0], MomentOfInertia.M[0][1], MomentOfInertia.M[0][2],
		MomentOfInertia.M[1][0], MomentOfInertia.M[1][1], MomentOfInertia.M[1][2],
		MomentOfInertia.M[2][0], MomentOfInertia.M[2][1], MomentOfInertia.M[2][2],
		RegisteredParts.Num(),
		bInertialPropertiesDirty ? TEXT("true") : TEXT("false"));
}

void AVehicle::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	if (!DebugDisplay.IsDisplayOn(KShowDebugCategory) || Canvas == nullptr)
	{
		return;
	}

	// Ensure we render consistent data even if tick is disabled.
	if (bInertialPropertiesDirty)
	{
		RecomputeInertialProperties();
	}

	const FString Info = GetDebugInfoString();

	FFontRenderInfo RenderInfo;
	RenderInfo.bEnableShadow = true;

	UFont* Font = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;

	Canvas->SetDrawColor(FColor::White);
	YPos += Canvas->DrawText(Font, Info, 4.0f, YPos, 1.0f, 1.0f, RenderInfo);
}

void AVehicle::DoRecompute()
{
	// Drop stale weak refs first so Num() reflects the real live set.
	RegisteredParts.RemoveAll([](const TWeakObjectPtr<UVehiclePartComponent>& Weak)
		{
			return !Weak.IsValid();
		});

	if (RegisteredParts.Num() == 0)
	{
		TotalMass = 0.0;
		CenterOfMass = FVector::ZeroVector;
		MomentOfInertia = FMatrix::Identity;

		if (!bHasLoggedEmptyWarning)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("AVehicle::DoRecompute: '%s' has no UVehiclePartComponent — mass=0, CoM=origin, MoI=identity"),
				*GetName());
			bHasLoggedEmptyWarning = true;
		}
		return;
	}

	// Defense-in-depth: validate each part's data before we add it in. Setters
	// reject non-finite / negative inputs, but a caller can write the UPROPERTY
	// fields directly (e.g., from C++ or a Blueprint cast) and bypass that gate,
	// so we re-check here to keep NaN / Inf out of the tensor no matter how the
	// state was populated. Corrupt parts are skipped with a throttled warning.
	auto IsPartDataValid = [](const UVehiclePartComponent* Part) -> bool
		{
			if (!FMath::IsFinite(Part->Mass) || Part->Mass < 0.0)
			{
				return false;
			}
			if (!FMath::IsFinite(Part->LocalCenterOfMass.X) ||
				!FMath::IsFinite(Part->LocalCenterOfMass.Y) ||
				!FMath::IsFinite(Part->LocalCenterOfMass.Z))
			{
				return false;
			}
			if (!FMath::IsFinite(Part->LocalInertiaDiagonal.X) ||
				!FMath::IsFinite(Part->LocalInertiaDiagonal.Y) ||
				!FMath::IsFinite(Part->LocalInertiaDiagonal.Z))
			{
				return false;
			}
			if (Part->LocalInertiaDiagonal.X < 0.0 ||
				Part->LocalInertiaDiagonal.Y < 0.0 ||
				Part->LocalInertiaDiagonal.Z < 0.0)
			{
				return false;
			}
			return true;
		};

	// Pass 1 — total mass and weighted CoM numerator (in cm, local frame).
	double MassSum = 0.0;
	FVector WeightedCoMSumCm = FVector::ZeroVector;

	for (const TWeakObjectPtr<UVehiclePartComponent>& Weak : RegisteredParts)
	{
		UVehiclePartComponent* Part = Weak.Get();
		if (Part == nullptr || Part->Mass <= 0.0)
		{
			continue;
		}

		if (!IsPartDataValid(Part))
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("AVehicle::DoRecompute: skipping corrupt part '%s' (non-finite or negative data)"),
				*Part->GetPathName());
			continue;
		}

		// Part CoM in the vehicle's actor-local frame, cm.
		const FVector PartCoMLocalCm = Part->GetRelativeLocation() + Part->LocalCenterOfMass;

		MassSum += Part->Mass;
		WeightedCoMSumCm += Part->Mass * PartCoMLocalCm;
	}

	if (MassSum <= 0.0)
	{
		// All parts have zero / negative mass — treat as empty for safety.
		TotalMass = 0.0;
		CenterOfMass = FVector::ZeroVector;
		MomentOfInertia = FMatrix::Identity;

		if (!bHasLoggedEmptyWarning)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("AVehicle::DoRecompute: '%s' has parts but total mass is zero — mass=0, CoM=origin, MoI=identity"),
				*GetName());
			bHasLoggedEmptyWarning = true;
		}
		return;
	}

	TotalMass = MassSum;
	CenterOfMass = WeightedCoMSumCm / MassSum;

	// Pass 2 — inertia tensor via parallel-axis theorem, about CenterOfMass.
	// Displacements converted to meters to keep MoI in SI (kg·m²).
	// FMatrix is 4x4; the 4th row/col is left as zero (homogeneous row unused).
	FMatrix Tensor(EForceInit::ForceInitToZero);

	for (const TWeakObjectPtr<UVehiclePartComponent>& Weak : RegisteredParts)
	{
		UVehiclePartComponent* Part = Weak.Get();
		if (Part == nullptr || Part->Mass <= 0.0)
		{
			continue;
		}

		// Skip the same corrupt parts as pass 1 — the warning has already been
		// logged there, so stay silent here to avoid duplicates per sweep.
		if (!IsPartDataValid(Part))
		{
			continue;
		}

		const FVector PartCoMLocalCm = Part->GetRelativeLocation() + Part->LocalCenterOfMass;
		const FVector DisplacementM = (PartCoMLocalCm - CenterOfMass) * KCmToM;

		const double Dx = DisplacementM.X;
		const double Dy = DisplacementM.Y;
		const double Dz = DisplacementM.Z;
		const double M  = Part->Mass;

		// Part's own diagonal inertia (kg·m²) in the part's local frame.
		// Treated as expressed along the vehicle's principal axes — sub-story
		// concern (per-part rotation) is deferred to ARocket/ASatellite.
		const FVector I = Part->LocalInertiaDiagonal;

		// Steiner term: m * (||d||^2 * I - d (x) d)
		Tensor.M[0][0] += I.X + M * (Dy * Dy + Dz * Dz);
		Tensor.M[1][1] += I.Y + M * (Dx * Dx + Dz * Dz);
		Tensor.M[2][2] += I.Z + M * (Dx * Dx + Dy * Dy);

		// Off-diagonals: -m * d_i * d_j (symmetric)
		const double Ixy = -M * Dx * Dy;
		const double Ixz = -M * Dx * Dz;
		const double Iyz = -M * Dy * Dz;

		Tensor.M[0][1] += Ixy;
		Tensor.M[1][0] += Ixy;
		Tensor.M[0][2] += Ixz;
		Tensor.M[2][0] += Ixz;
		Tensor.M[1][2] += Iyz;
		Tensor.M[2][1] += Iyz;
	}

	// FMatrix is 4x4; leave the 4th row/col as zero (homogeneous row not used).
	MomentOfInertia = Tensor;
}
