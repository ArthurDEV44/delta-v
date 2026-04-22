// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IInteractable.generated.h"

class APlayerController;

/**
 * US-025 — interface implemented by any actor the commander can interact with
 * (launch console, airlock, terminal, door, etc.).
 *
 * Contract:
 *  - GetInteractionText()    — short active-verb label shown next to the "E" hint.
 *  - IsInteractionEnabled()  — false renders the hint greyed as "(offline)" and
 *                              short-circuits TryInteract to a no-op + audio cue.
 *  - OnInteract(PC)          — called when the player presses the interact input
 *                              while this actor is the current best candidate.
 *
 * C++-only interface — BP exposure deferred to when a gameplay story needs it.
 */
UINTERFACE(MinimalAPI, NotBlueprintable)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

class DELTAV_API IInteractable
{
	GENERATED_BODY()

public:
	/** Prompt label shown next to the "E" key hint. */
	virtual FText GetInteractionText() const = 0;

	/**
	 * True when the interactable can be activated. Disabled interactables still
	 * show a prompt (greyed) so the player discovers them; pressing E is a no-op
	 * with audio feedback handled by UI listeners.
	 */
	virtual bool IsInteractionEnabled() const { return true; }

	/**
	 * Called exactly once per "E" press while this actor is the current best
	 * candidate AND IsInteractionEnabled() returned true. The PlayerController
	 * is passed so the implementation can open widgets, possess target pawns,
	 * or dispatch player-scoped events without a GetWorld lookup.
	 */
	virtual void OnInteract(APlayerController* PlayerController) = 0;
};
