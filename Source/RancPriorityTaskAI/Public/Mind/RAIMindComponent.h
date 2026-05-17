// Copyright Rancorous Games, 2024
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Mind/RAILifeEvent.h"
#include "RAIMindComponent.generated.h"

class URAIMemoryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRAILifeEventDelegate, const FRAILifeEvent&, Event);

/**
 * URAIMindComponent — the façade for an NPC's mental architecture.
 *
 * Owns (and provides one-line accessors to) the sub-components:
 *   - URAIMemoryComponent      (M1, implemented)
 *   - URAIPersonalityComponent (M1, stub — see MIND_DESIGN.md §4)
 *   - URAIEmotionComponent     (M2)
 *   - URAIRelationshipComponent(M2)
 *   - URAISocialComponent      (M3)
 *
 * The single entry point for all mental input is Witness(). Everything else
 * — memories, mood shifts, relationship updates — flows from there via
 * OnLifeEvent. Tasks never write to sub-components directly.
 *
 * Lives on ARAIController. Attach it and its children there via
 * CreateDefaultSubobject in AWTAIController's constructor.
 */
UCLASS(ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAIMindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URAIMindComponent();

	virtual void BeginPlay() override;

	// ── Sub-components ────────────────────────────────────────────────────────
	// Each is a sibling component on the owning controller, not a child object.
	// Populated during AWTAIController::BeginPlay (or via CreateDefaultSubobject).

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RAI|Mind")
	TObjectPtr<URAIMemoryComponent> Memory;

	// Future: URAIPersonalityComponent, URAIEmotionComponent, etc.
	// Added here as null pointers so tasks can safely test IsValid() without crashing.

	// ── Event Bus ─────────────────────────────────────────────────────────────

	/**
	 * Broadcast an event to all registered mind listeners.
	 * This is the only correct way to feed information into the mind system.
	 * Fills Event.WorldTime automatically.
	 */
	UFUNCTION(BlueprintCallable, Category="RAI|Mind")
	void Witness(FRAILifeEvent Event);

	/**
	 * Fired after WorldTime is stamped. Memory, emotion, and relationship
	 * components bind to this in their BeginPlay.
	 */
	UPROPERTY(BlueprintAssignable, Category="RAI|Mind")
	FRAILifeEventDelegate OnLifeEvent;

	// ── Convenience Accessors ─────────────────────────────────────────────────

	/** How does this NPC feel about the given actor? Range -1..+1. */
	UFUNCTION(BlueprintCallable, Category="RAI|Mind")
	float ValenceTowards(AActor* Subject) const;

	/** True if this NPC knows Subject→Predicate as a semantic fact. */
	UFUNCTION(BlueprintCallable, Category="RAI|Mind")
	bool Knows(FGameplayTag Subject, FGameplayTag Predicate) const;
};
