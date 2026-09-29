// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RAIDataStructures.h"
#include "RAIInterruptPolicy.generated.h"

class URAIManagerComponent;
class URAITaskComponent;

/** Everything an interrupt policy may need to decide. Built by the manager for each decision. */
struct RANCPRIORITYTASKAI_API FRAIArbitrationContext
{
	const URAIManagerComponent* Manager = nullptr;
	double Now = 0.0;

	/** Root of the active chain. Its latest priority is the chain's effective priority. */
	const URAITaskComponent* ActiveRoot = nullptr;

	/** Effective priority of the active chain (the root's latest computed priority). */
	float ActivePriority = 0.f;

	/** Latest computed priority of the candidate. */
	float CandidatePriority = 0.f;

	/**
	 * True when the candidate is itself running inside the active chain as an invoked task. Returning true then ends
	 * the whole chain and restarts the candidate as the new root (its own purpose takes over from the root's).
	 */
	bool bCandidateInActiveChain = false;
};

/**
 * Decides whether an arbitration winner replaces the active chain.
 *
 * The base class is the default policy and reproduces the historic gap behavior: the deepest active task's
 * InterruptType selects a gap from the manager (WaitASecInterruptPriorityGap, ...), and the candidate must exceed
 * the chain's effective priority by more than that gap. The gap values stay on the manager so existing assets keep
 * their configuration.
 *
 * Subclass in C++ and assign an instance to URAIManagerComponent::InterruptPolicy for game-specific rules.
 */
UCLASS(EditInlineNew, DefaultToInstanced, CollapseCategories, ClassGroup = (RAI))
class RANCPRIORITYTASKAI_API URAIInterruptPolicy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @param Active    The deepest active task of the chain (ActiveTask). Its InterruptType expresses the current reluctance.
	 * @param Candidate The arbitration winner, which differs from the chain root.
	 */
	virtual bool ShouldInterrupt(const FRAIArbitrationContext& Context, const URAITaskComponent* Active,
	                             const URAITaskComponent* Candidate) const;

	/** Gap for an interruption type from the manager's configuration. Negative for Never. */
	static float GetPriorityGap(const URAIManagerComponent& Manager, ERAIInterruptionType Type);
};
