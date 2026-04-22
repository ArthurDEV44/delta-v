// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicles/UVehicleDef.h"

#include "Vehicles/URocketDef.h"

namespace
{
	bool IsVectorFinite(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	bool IsInertiaDiagonalValid(const FVector& V)
	{
		return IsVectorFinite(V) && V.X >= 0.0 && V.Y >= 0.0 && V.Z >= 0.0;
	}
}

bool UVehicleDef::IsValid(FString* OutError) const
{
	const auto Fail = [OutError](const FString& Msg) -> bool
		{
			if (OutError != nullptr)
			{
				*OutError = Msg;
			}
			return false;
		};

	const bool bHasRocket = RocketBody != nullptr;
	const bool bHasSatellite = SatellitePayload.bEnabled;

	if (!bHasRocket && !bHasSatellite)
	{
		return Fail(FString::Printf(
			TEXT("UVehicleDef '%s' has neither RocketBody nor enabled SatellitePayload — nothing to spawn"),
			*GetName()));
	}

	if (!IsVectorFinite(SpawnOffsetCm))
	{
		return Fail(FString::Printf(
			TEXT("UVehicleDef '%s' SpawnOffsetCm is non-finite"),
			*GetName()));
	}

	if (bHasRocket)
	{
		FString RocketError;
		if (!RocketBody->IsValid(&RocketError))
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' RocketBody '%s' is invalid — %s"),
				*GetName(), *RocketBody->GetName(), *RocketError));
		}
	}

	if (bHasSatellite)
	{
		if (!FMath::IsFinite(SatellitePayload.DryMassKg) || SatellitePayload.DryMassKg < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.DryMassKg=%.17g is invalid"),
				*GetName(), SatellitePayload.DryMassKg));
		}
		if (!FMath::IsFinite(SatellitePayload.BatteryCapacityWh) || SatellitePayload.BatteryCapacityWh < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.BatteryCapacityWh=%.17g is invalid"),
				*GetName(), SatellitePayload.BatteryCapacityWh));
		}
		if (!FMath::IsFinite(SatellitePayload.InitialChargeWh) || SatellitePayload.InitialChargeWh < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.InitialChargeWh=%.17g is invalid"),
				*GetName(), SatellitePayload.InitialChargeWh));
		}
		if (!FMath::IsFinite(SatellitePayload.SolarPanelPowerW) || SatellitePayload.SolarPanelPowerW < 0.0)
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.SolarPanelPowerW=%.17g is invalid"),
				*GetName(), SatellitePayload.SolarPanelPowerW));
		}
		if (!IsVectorFinite(SatellitePayload.MountOffsetCm))
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.MountOffsetCm is non-finite"),
				*GetName()));
		}
		if (!IsInertiaDiagonalValid(SatellitePayload.InertiaDiagonalKgM2))
		{
			return Fail(FString::Printf(
				TEXT("UVehicleDef '%s' SatellitePayload.InertiaDiagonalKgM2 is non-finite or negative"),
				*GetName()));
		}

		for (int32 Idx = 0; Idx < SatellitePayload.Instruments.Num(); ++Idx)
		{
			const FInstrumentDef& I = SatellitePayload.Instruments[Idx];
			if (!FMath::IsFinite(I.PowerDrawW) || I.PowerDrawW < 0.0)
			{
				return Fail(FString::Printf(
					TEXT("UVehicleDef '%s' Instrument[%d] '%s' has invalid PowerDrawW=%.17g"),
					*GetName(), Idx, *I.InstrumentName.ToString(), I.PowerDrawW));
			}
			if (!FMath::IsFinite(I.RequiredExposureSeconds) || I.RequiredExposureSeconds < 0.0)
			{
				return Fail(FString::Printf(
					TEXT("UVehicleDef '%s' Instrument[%d] '%s' has invalid RequiredExposureSeconds=%.17g"),
					*GetName(), Idx, *I.InstrumentName.ToString(), I.RequiredExposureSeconds));
			}
			if (!FMath::IsFinite(I.MaxRangeCm) || I.MaxRangeCm < 0.0)
			{
				return Fail(FString::Printf(
					TEXT("UVehicleDef '%s' Instrument[%d] '%s' has invalid MaxRangeCm=%.17g"),
					*GetName(), Idx, *I.InstrumentName.ToString(), I.MaxRangeCm));
			}
		}
	}

	return true;
}
