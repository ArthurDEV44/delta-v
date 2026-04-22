// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/OrbitalComponent.h"

#include "DeltaV.h"
#include "Base/CelestialBody.h"
#include "Orbital/OrbitalMath.h"
#include "Vehicles/AVehicle.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	// UE native units are centimeters; orbital math is strict SI meters.
	constexpr double KMetersToCm = 100.0;

	// Earth's standard gravitational parameter (m^3 / s^2). Used only when no
	// ParentBody is set and no override is configured — logged once per
	// component so misconfiguration is visible without spamming.
	constexpr double KEarthMu = 3.986004418e14;
}

UOrbitalComponent::UOrbitalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Rail propagation is pure math on the game thread; no physics scene reads.
	// Tick group TG_PrePhysics keeps rail-driven transforms stable relative to
	// local-mode Chaos bodies that update in TG_DuringPhysics.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UOrbitalComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeOwnerBinding();
}

void UOrbitalComponent::InitializeOwnerBinding()
{
	// Idempotent: a prior init already settled the outcome (failure or success),
	// and re-running would either double-log or needlessly re-touch the root.
	if (bOwnerCheckFailed || CachedOwner.IsValid())
	{
		return;
	}

	AActor* Owner = GetOwner();
	AVehicle* VehicleOwner = Cast<AVehicle>(Owner);

	if (VehicleOwner == nullptr)
	{
		// AC#4 — no AVehicle parent. Log, disable tick permanently, early-out.
		// Both bCanEverTick and the runtime toggle are cleared so neither a
		// subsequent SetComponentTickEnabled nor a RegisterComponent flip can
		// silently re-enable rail propagation on an unsupported owner.
		UE_LOG(LogDeltaV, Error,
			TEXT("UOrbitalComponent on '%s' requires an AVehicle owner (got '%s'); disabling tick."),
			*GetNameSafe(this),
			Owner ? *Owner->GetClass()->GetName() : TEXT("null"));

		bOwnerCheckFailed = true;
		PrimaryComponentTick.bCanEverTick = false;
		SetComponentTickEnabled(false);
		return;
	}

	CachedOwner = VehicleOwner;
	ApplyModeToOwner();
}

void UOrbitalComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// AC#4 — defensive, though BeginPlay already disabled the tick.
	if (bOwnerCheckFailed || !CachedOwner.IsValid())
	{
		return;
	}

	// AC#2 — Local mode hands off to Chaos; nothing to do on this component.
	if (Mode == EPhysicsMode::Local)
	{
		return;
	}

	// AC#1 — Rail mode: advance FOrbitalState and push to the owner's transform.
	TickRail(static_cast<double>(DeltaTime));
}

void UOrbitalComponent::SetPhysicsMode(EPhysicsMode NewMode)
{
	if (bOwnerCheckFailed)
	{
		return;
	}

	Mode = NewMode;

	// Re-apply even if the enum value didn't change — the caller may have
	// replaced the root primitive (e.g., after a mesh swap) and we still
	// want the Chaos toggle to match our mode.
	ApplyModeToOwner();
}

void UOrbitalComponent::ApplyModeToOwner()
{
	AActor* Owner = CachedOwner.Get();
	if (Owner == nullptr)
	{
		return;
	}

	UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Owner->GetRootComponent());

	if (RootPrim == nullptr)
	{
		// Rail + no primitive: benign (no Chaos body to manage).
		if (Mode == EPhysicsMode::Local && !bHasLoggedNoPrimitiveRoot)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("UOrbitalComponent on '%s': Local mode requested but owner root is not a UPrimitiveComponent; Chaos rigid body not activated."),
				*GetNameSafe(Owner));
			bHasLoggedNoPrimitiveRoot = true;
		}
		return;
	}

	RootPrim->SetSimulatePhysics(Mode == EPhysicsMode::Local);
}

double UOrbitalComponent::ResolveMuCore(bool& bOutUsedFallback) const
{
	bOutUsedFallback = false;

	// Finite + strictly positive: defends against NaN / Inf sneaking in via a
	// Blueprint setter or a hand-edited savegame bypassing the ClampMin UI.
	if (FMath::IsFinite(GravitationalParameterOverride)
		&& GravitationalParameterOverride > 0.0)
	{
		return GravitationalParameterOverride;
	}

	if (const UCelestialBody* Body = CurrentState.ParentBody.Get())
	{
		if (FMath::IsFinite(Body->GravitationalParameter)
			&& Body->GravitationalParameter > 0.0)
		{
			return Body->GravitationalParameter;
		}
	}

	bOutUsedFallback = true;
	return KEarthMu;
}

double UOrbitalComponent::ResolveMu()
{
	bool bUsedFallback = false;
	const double Mu = ResolveMuCore(bUsedFallback);

	if (bUsedFallback && !bHasLoggedMuFallback)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UOrbitalComponent on '%s': no ParentBody and no GravitationalParameterOverride; falling back to Earth mu (%.3e)."),
			*GetNameSafe(GetOwner()), KEarthMu);
		bHasLoggedMuFallback = true;
	}
	return Mu;
}

double UOrbitalComponent::ResolveMuSilent() const
{
	bool bUsedFallback = false;
	return ResolveMuCore(bUsedFallback);
}

void UOrbitalComponent::TickRail(double DeltaSeconds)
{
	AActor* Owner = CachedOwner.Get();
	if (Owner == nullptr)
	{
		return;
	}

	const double Mu = ResolveMu();

	// --- Compute phase: build the full next frame on the stack. CurrentState
	// is NOT mutated here, so a failure in propagation OR in the downstream
	// RV conversion leaves the component in exactly its prior state. This is
	// the atomic-commit contract that keeps internal state and rendered
	// transform from diverging.
	FOrbitalState NewState;
	if (!UOrbitalMath::PropagateKepler(CurrentState, DeltaSeconds, Mu, NewState))
	{
		if (!bHasLoggedPropagationFailure)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("UOrbitalComponent on '%s': PropagateKepler failed (Δt=%.3e, mu=%.3e); holding last transform."),
				*GetNameSafe(Owner), DeltaSeconds, Mu);
			bHasLoggedPropagationFailure = true;
		}
		return;
	}

	FVector PosMeters(FVector::ZeroVector);
	FVector VelMeters(FVector::ZeroVector);
	if (!UOrbitalMath::ElementsToStateVector(NewState, Mu, PosMeters, VelMeters))
	{
		// ElementsToStateVector already logged. State unchanged by design.
		return;
	}

	// World location: ParentBody->WorldPosition is expressed in meters (see
	// UCelestialBody.h). Convert everything to UE centimeters.
	FVector ParentOffsetMeters(FVector::ZeroVector);
	if (const UCelestialBody* Body = NewState.ParentBody.Get())
	{
		ParentOffsetMeters = Body->WorldPosition;
	}
	const FVector WorldLocationCm = (PosMeters + ParentOffsetMeters) * KMetersToCm;

	// Prograde attitude: actor forward (X) along velocity, up (Z) along the
	// orbit-plane radial-out direction. Gram-Schmidt orthogonalises radial
	// against forward so the basis is exactly orthonormal even away from
	// periapsis / apoapsis (where position and velocity are not orthogonal).
	// If either vector is degenerate — which shouldn't happen for a valid
	// orbit — fall back to location-only.
	const FVector ForwardDir = VelMeters.GetSafeNormal();
	const FVector RadialDir = PosMeters.GetSafeNormal();

	FRotator NewRotation;
	bool bHasRotation = false;
	if (!ForwardDir.IsNearlyZero() && !RadialDir.IsNearlyZero())
	{
		FVector UpDir = RadialDir - FVector::DotProduct(RadialDir, ForwardDir) * ForwardDir;
		if (UpDir.Normalize())
		{
			// FRotationMatrix::MakeFromXZ encodes the "X = forward, Z = up"
			// convention UE uses natively; Y is derived by cross product.
			NewRotation = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();
			bHasRotation = true;
		}
	}

	// --- Commit phase: everything downstream succeeded; publish the new
	// state AND transform atomically.
	CurrentState = NewState;
	if (bHasRotation)
	{
		Owner->SetActorLocationAndRotation(
			WorldLocationCm, NewRotation, /*bSweep=*/ false);
	}
	else
	{
		Owner->SetActorLocation(WorldLocationCm, /*bSweep=*/ false);
	}
}

bool UOrbitalComponent::ComputeInjectionVelocities(
	FVector& OutLinearVelocityMps,
	FVector& OutAngularVelocityRadps) const
{
	// Zero the outputs up-front so callers that ignore the return value get
	// a deterministic zero rather than undefined contents on failure.
	OutLinearVelocityMps = FVector::ZeroVector;
	OutAngularVelocityRadps = FVector::ZeroVector;

	const double Mu = ResolveMuSilent();

	FVector PosMeters(FVector::ZeroVector);
	FVector VelMeters(FVector::ZeroVector);
	if (!UOrbitalMath::ElementsToStateVector(CurrentState, Mu, PosMeters, VelMeters))
	{
		// ElementsToStateVector already logged a warning — no further log here,
		// the caller (SwitchToLocal) will surface its own diagnostic.
		return false;
	}

	// Linear velocity: the Cartesian velocity directly.
	OutLinearVelocityMps = VelMeters;

	// Orbital angular velocity vector: ω = (r × v) / |r|². Keeps a
	// prograde-pointing body rotating at the instantaneous orbital rate so
	// it continues to face prograde once Chaos takes over (no external
	// torques applied). Units: rad/s, independent of length scale since r
	// cancels between the cross product and the squared-length divisor.
	const double RSquared = PosMeters.SizeSquared();
	if (RSquared > 0.0 && FMath::IsFinite(RSquared))
	{
		const FVector Omega = FVector::CrossProduct(PosMeters, VelMeters) / RSquared;
		// Defensive: even with RSquared > 0 finite, rounding near a tiny r
		// could produce a non-finite ω. Zero out rather than forward NaN
		// into the Chaos body, which would hard-assert.
		if (FMath::IsFinite(Omega.X) && FMath::IsFinite(Omega.Y) && FMath::IsFinite(Omega.Z))
		{
			OutAngularVelocityRadps = Omega;
		}
	}

	return true;
}

void UOrbitalComponent::SwitchToLocal()
{
	if (bOwnerCheckFailed)
	{
		return;
	}

	// AC#4 — already Local: no-op + warning (once per component lifetime).
	if (Mode == EPhysicsMode::Local)
	{
		if (!bHasLoggedAlreadyLocalSwitch)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("UOrbitalComponent on '%s': SwitchToLocal called while already in Local mode; no-op."),
				*GetNameSafe(GetOwner()));
			bHasLoggedAlreadyLocalSwitch = true;
		}
		return;
	}

	// Compute the injection BEFORE flipping the mode — if the math fails,
	// we stay in Rail so downstream logic isn't left with a half-applied
	// switch and a Chaos body that wasn't seeded with a velocity.
	FVector InjectLinMps, InjectAngRadps;
	if (!ComputeInjectionVelocities(InjectLinMps, InjectAngRadps))
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UOrbitalComponent on '%s': SwitchToLocal aborted — ElementsToStateVector rejected the current state."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// Commit the mode + physics flag first so the body instance is live
	// before we try to push velocities into it.
	Mode = EPhysicsMode::Local;
	ApplyModeToOwner();

	// Record the injection telemetry unconditionally so tests and diagnostics
	// see the values the component intended to push, even if the root below
	// turns out to be missing or the owner was destroyed during ApplyMode.
	LastInjectedLinearVelocityMps = InjectLinMps;
	LastInjectedAngularVelocityRadps = InjectAngRadps;

	// Re-validate the owner after ApplyModeToOwner — in principle a BP
	// delegate wired on SetSimulatePhysics could have destroyed the actor,
	// leaving CachedOwner dangling. UE's weak-ref machinery returns null in
	// that case; we just need to respect it.
	AActor* Owner = CachedOwner.Get();
	if (Owner == nullptr)
	{
		return;
	}
	UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	if (RootPrim == nullptr)
	{
		// ApplyModeToOwner already emitted the "Local mode without primitive
		// root" warning; nothing more to do — the mode flag is set for code
		// that keys off it, and the Chaos handoff is simply a no-op.
		return;
	}

	// Chaos uses UE native cm/s for linear velocity; our math is strict SI m/s.
	RootPrim->SetPhysicsLinearVelocity(
		InjectLinMps * KMetersToCm, /*bAddToCurrent=*/ false);
	RootPrim->SetPhysicsAngularVelocityInRadians(
		InjectAngRadps, /*bAddToCurrent=*/ false);
}

void UOrbitalComponent::SwitchToRail()
{
	if (bOwnerCheckFailed)
	{
		return;
	}

	AActor* Owner = CachedOwner.Get();
	if (Owner == nullptr)
	{
		return;
	}

	// Capture world position in SI metres. The parent body's WorldPosition
	// is already in metres (see UCelestialBody.h), so we convert the actor's
	// cm location to metres before subtracting.
	FVector ParentOffsetMeters(FVector::ZeroVector);
	if (const UCelestialBody* Body = CurrentState.ParentBody.Get())
	{
		ParentOffsetMeters = Body->WorldPosition;
	}
	const FVector PosMeters =
		Owner->GetActorLocation() / KMetersToCm - ParentOffsetMeters;

	// Capture live Chaos velocity. On an unregistered primitive (or a root
	// that is not a UPrimitiveComponent), GetPhysicsLinearVelocity returns
	// zero — SwitchToRailFromStateVector will then surface the near-zero
	// velocity warning, which is the right diagnostic for a caller that
	// invoked SwitchToRail on a vehicle that never actually had a Chaos body.
	FVector VelMps(FVector::ZeroVector);
	if (UPrimitiveComponent* RootPrim =
			Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		VelMps = RootPrim->GetPhysicsLinearVelocity() / KMetersToCm;
	}

	SwitchToRailFromStateVector(PosMeters, VelMps);
}

bool UOrbitalComponent::SwitchToRailFromStateVector(
	const FVector& PosMeters, const FVector& VelMps)
{
	if (bOwnerCheckFailed)
	{
		return false;
	}

	// Input finitude check. This is a BlueprintCallable entry point and
	// receives raw FVectors from save-game data or designer scripts; NaN
	// comparisons always return false, so without this guard a hostile or
	// hand-edited PosMeters = (NaN, NaN, NaN) would silently bypass the
	// altitude gate and be passed into StateVectorToElements (which does
	// its own validation, but the altitude-gate bypass is the real concern).
	if (!FMath::IsFinite(PosMeters.X) || !FMath::IsFinite(PosMeters.Y) || !FMath::IsFinite(PosMeters.Z)
		|| !FMath::IsFinite(VelMps.X) || !FMath::IsFinite(VelMps.Y) || !FMath::IsFinite(VelMps.Z))
	{
		UE_LOG(LogDeltaV, Error,
			TEXT("UOrbitalComponent on '%s': SwitchToRailFromStateVector rejected — non-finite input."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// Symmetric guard to SwitchToLocal's AC#4: a redundant switch is a bug
	// upstream — log once and refuse so nobody silently overwrites a valid
	// Rail state with a stale captured snapshot.
	if (Mode == EPhysicsMode::Rail)
	{
		if (!bHasLoggedAlreadyRailSwitch)
		{
			UE_LOG(LogDeltaV, Warning,
				TEXT("UOrbitalComponent on '%s': SwitchToRail called while already in Rail mode; no-op."),
				*GetNameSafe(GetOwner()));
			bHasLoggedAlreadyRailSwitch = true;
		}
		return false;
	}

	// Cache the parent-body weak ref at entry. We re-use it at the commit
	// step to avoid a TOCTOU gap if anything between here and the commit
	// would invalidate the ref (UE's game thread is cooperative and won't
	// GC mid-function, so this is mostly a correctness-by-construction
	// safeguard, but it also keeps the altitude check and the mu resolution
	// reading from the same snapshot).
	const TWeakObjectPtr<UCelestialBody> CapturedParentBody = CurrentState.ParentBody;

	// AC#4 — altitude gate. Only enforceable when a ParentBody with a
	// positive EquatorialRadius is set; otherwise we trust the caller and
	// snap directly. This intentionally is NOT throttled: an altitude-below-
	// surface rejection is a rare, high-signal event the designer wants to
	// see every time it happens (e.g., a re-entry sim gone wrong).
	if (const UCelestialBody* Body = CapturedParentBody.Get())
	{
		if (FMath::IsFinite(Body->EquatorialRadius) && Body->EquatorialRadius > 0.0
			&& PosMeters.SizeSquared() < Body->EquatorialRadius * Body->EquatorialRadius)
		{
			UE_LOG(LogDeltaV, Error,
				TEXT("UOrbitalComponent on '%s': SwitchToRail refused — vehicle altitude below parent body surface (|r|=%.3e m, R=%.3e m)."),
				*GetNameSafe(GetOwner()),
				PosMeters.Length(), Body->EquatorialRadius);
			return false;
		}
	}

	// AC#3 — near-zero velocity degeneracy warning. This fires BEFORE the
	// StateVectorToElements call so the diagnostic is emitted regardless of
	// whether the conversion happens to succeed under a particular fixture.
	constexpr double KNearZeroVelocityMps = 0.1;
	if (VelMps.SizeSquared() < KNearZeroVelocityMps * KNearZeroVelocityMps
		&& !bHasLoggedNearZeroVelocity)
	{
		UE_LOG(LogDeltaV, Warning,
			TEXT("UOrbitalComponent on '%s': near-zero velocity, orbital elements degenerate (|v|=%.3e m/s)."),
			*GetNameSafe(GetOwner()), VelMps.Length());
		bHasLoggedNearZeroVelocity = true;
	}

	const double Mu = ResolveMu();

	FOrbitalState NewState;
	if (!UOrbitalMath::StateVectorToElements(PosMeters, VelMps, Mu, NewState))
	{
		// StateVectorToElements already logged. State unchanged by design.
		return false;
	}

	// StateVectorToElements zeroes the OutState and therefore clears
	// ParentBody — the conversion is unit-math, not a context read. Restore
	// from the weak ref we cached at entry so downstream Rail ticks resolve
	// mu + world offset correctly.
	NewState.ParentBody = CapturedParentBody;

	CurrentState = NewState;
	Mode = EPhysicsMode::Rail;
	ApplyModeToOwner();
	return true;
}
