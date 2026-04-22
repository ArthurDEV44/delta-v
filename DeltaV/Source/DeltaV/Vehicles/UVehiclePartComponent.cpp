// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UVehiclePartComponent.h"

#include "DeltaV.h"
#include "Vehicles/AVehicle.h"

UVehiclePartComponent::UVehiclePartComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

void UVehiclePartComponent::SetMass(double NewMass)
{
	const bool bValid = FMath::IsFinite(NewMass) && NewMass >= 0.0;
	if (!bValid)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UVehiclePartComponent::SetMass: rejected non-finite or negative value %.17g on '%s'"),
			NewMass, *GetPathName());
		return;
	}

	if (Mass == NewMass)
	{
		return;
	}

	Mass = NewMass;
	NotifyOwnerDirty();
}

void UVehiclePartComponent::SetLocalCenterOfMass(const FVector& NewLocalCoM)
{
	if (!FMath::IsFinite(NewLocalCoM.X) || !FMath::IsFinite(NewLocalCoM.Y) || !FMath::IsFinite(NewLocalCoM.Z))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UVehiclePartComponent::SetLocalCenterOfMass: rejected non-finite vector on '%s'"),
			*GetPathName());
		return;
	}

	if (LocalCenterOfMass == NewLocalCoM)
	{
		return;
	}

	LocalCenterOfMass = NewLocalCoM;
	NotifyOwnerDirty();
}

void UVehiclePartComponent::SetLocalInertiaDiagonal(const FVector& NewLocalInertiaDiagonal)
{
	const bool bFinite =
		FMath::IsFinite(NewLocalInertiaDiagonal.X) &&
		FMath::IsFinite(NewLocalInertiaDiagonal.Y) &&
		FMath::IsFinite(NewLocalInertiaDiagonal.Z);
	const bool bNonNegative =
		NewLocalInertiaDiagonal.X >= 0.0 &&
		NewLocalInertiaDiagonal.Y >= 0.0 &&
		NewLocalInertiaDiagonal.Z >= 0.0;

	if (!bFinite || !bNonNegative)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UVehiclePartComponent::SetLocalInertiaDiagonal: rejected invalid diagonal (%.17g, %.17g, %.17g) on '%s'"),
			NewLocalInertiaDiagonal.X, NewLocalInertiaDiagonal.Y, NewLocalInertiaDiagonal.Z,
			*GetPathName());
		return;
	}

	if (LocalInertiaDiagonal == NewLocalInertiaDiagonal)
	{
		return;
	}

	LocalInertiaDiagonal = NewLocalInertiaDiagonal;
	NotifyOwnerDirty();
}

void UVehiclePartComponent::OnRegister()
{
	Super::OnRegister();

	if (AVehicle* Vehicle = Cast<AVehicle>(GetOwner()))
	{
		Vehicle->RegisterPart(this);
	}
}

void UVehiclePartComponent::OnUnregister()
{
	if (AVehicle* Vehicle = Cast<AVehicle>(GetOwner()))
	{
		Vehicle->UnregisterPart(this);
	}

	Super::OnUnregister();
}

void UVehiclePartComponent::NotifyOwnerDirty() const
{
	if (AVehicle* Vehicle = Cast<AVehicle>(GetOwner()))
	{
		Vehicle->MarkInertialPropertiesDirty();
	}
}
