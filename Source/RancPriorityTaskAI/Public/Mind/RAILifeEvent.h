// Copyright Rancorous Games, 2024
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "RAILifeEvent.generated.h"

/**
 * FRAILifeEvent — the unifying primitive for all mind-system input.
 *
 * Every memorable moment, internal or external, becomes one of these.
 * All mind components (memory, emotion, relationships, social) receive
 * events through URAIMindComponent::Witness(), which fills WorldTime and
 * broadcasts OnLifeEvent. See MIND_DESIGN.md §2 for the full rationale.
 *
 * Tag namespaces (declare in GameplayTags project settings):
 *   Event.*        — broad category (Event.Combat.Attacked, Event.Social.Shout)
 *   Action.*       — what was done  (Action.Hit, Action.Give, Action.Flee)
 *   Object.*       — what was used  (Object.Spear, Object.Fruit.White)
 *   Affect.*       — emotional response (Affect.Fear, Affect.Comfort)
 *   Concept.*      — primitive language vocabulary (Concept.Word.Wolf, Concept.You)
 */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAILifeEvent
{
	GENERATED_BODY()

	/** Categorical "what kind of thing happened" — drives default encoding rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	FGameplayTag Kind;           // Event.Combat.Attacked, Event.Social.Insulted

	/** Specific action performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	FGameplayTag Action;         // Action.Hit, Action.Shout, Action.Give, Action.Flee

	/** Tags describing involved objects or concepts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	FGameplayTagContainer ObjectTags; // Object.Spear, Object.Fruit.White, Resource.Water

	// ── Cast of the scene ─────────────────────────────────────────────────────

	/** Who performed the action. Null for environmental events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	TWeakObjectPtr<AActor> Actor;

	/** Who/what the action was directed at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	TWeakObjectPtr<AActor> Target;

	/** Others who were present and may encode the event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	TArray<AActor*> Witnesses;

	/** Where it happened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	FVector Location = FVector::ZeroVector;

	/** Optional structured payload for task-specific extras. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	FInstancedStruct Detail;

	// ── Affective baseline ────────────────────────────────────────────────────
	// Memory and emotion encoders use these as inputs, then modulate by personality.

	/** -1 (very negative) .. +1 (very positive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind", meta=(ClampMin="-1", ClampMax="1"))
	float BaselineValence = 0.f;

	/** 0 (calm / unremarkable) .. 1 (extremely arousing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind", meta=(ClampMin="0", ClampMax="1"))
	float BaselineArousal = 0.f;

	// ── Provenance ────────────────────────────────────────────────────────────
	// Null Source = direct experience. Non-null = hearsay (another NPC told me).

	/** Who communicated this event to this mind. Null if directly witnessed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind")
	TWeakObjectPtr<AActor> Source;

	/** How much to trust the source. 1.0 = full confidence (direct experience). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Mind", meta=(ClampMin="0", ClampMax="1"))
	float SourceCredibility = 1.f;

	/** Filled by URAIMindComponent::Witness() — do not set manually. */
	UPROPERTY(BlueprintReadOnly, Category="RAI|Mind")
	float WorldTime = 0.f;

	// ── Helpers ───────────────────────────────────────────────────────────────

	bool IsDirectExperience() const { return !Source.IsValid(); }
	bool IsHearsay()          const { return Source.IsValid(); }
};
