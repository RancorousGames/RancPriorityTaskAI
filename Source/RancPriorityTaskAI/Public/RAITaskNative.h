// Copyright Rancorous Games, 2024

#pragma once
#include "GameplayTagContainer.h"
#include "Perception/AIPerceptionTypes.h"
#include "RAITaskComponent.h"
#include "RAITaskNative.generated.h"

/**
 * C++-first base class for RAI tasks.
 *
 * Override the Native* virtuals instead of the _Implementation suffixed
 * BlueprintNativeEvent methods. The bridge overrides are sealed (final) so
 * only the Native* layer is open to subclasses.
 *
 * Blueprints cannot subclass this — use URAITaskComponent for Blueprint tasks.
 *
 * Typical C++ task:
 *   class UMyTask : public URAITaskNative
 *   {
 *       virtual float NativeCalculatePriority() override;
 *       virtual void  NativeBeginTask(const FRAITaskInvokeArguments& Args) override;
 *   };
 */
UCLASS(Abstract, NotBlueprintable)
class RANCPRIORITYTASKAI_API URAITaskNative : public URAITaskComponent
{
	GENERATED_BODY()
public:
	// ---- Override these in C++ subclasses ----

	/** Return the priority score. Higher wins. Return 0 to opt out of selection. */
	virtual float NativeCalculatePriority() { return 0.f; }

	/** Called when the task becomes the active task. */
	virtual void NativeBeginTask(const FRAITaskInvokeArguments& Args) {}

	/** Called when the task ends for any reason. Guaranteed to run before bookkeeping clears state. */
	virtual void NativeEndTask(bool bSuccess, bool bInterrupted) {}

	/** Called when the perception system detects a new stimulus (requires AutoHandleSensoryInput). */
	virtual void NativeOnPerception(AActor* Actor, const FAIStimulus& Stimulus) {}

	/** Called when a custom gameplay tag event is broadcast to all tasks. */
	virtual void NativeOnCustomTrigger(FGameplayTag Trigger, UObject* Payload) {}

	/** Called when a sub-task this task previously invoked has finished. */
	virtual void NativeOnInvokedTaskCompleted(bool bWasSuccessful) {}

protected:
	// ---- Bridges — sealed so subclasses must use the Native* virtuals ----

	virtual float CalculatePriority_Implementation() override final;
	virtual void  BeginTask_Implementation(const FRAITaskInvokeArguments& Args) override final;
	virtual void  EndTask_Implementation(bool Success, float BeginAgainCooldown, bool WasInterrupted) override final;
	virtual void  OnPerceptionStimulus_Implementation(AActor* Actor, FAIStimulus Stimulus) override final;
	virtual void  OnCustomTrigger_Implementation(FGameplayTag Trigger, UObject* Payload) override final;
	virtual void  OnInvokedTaskCompleted_Implementation(bool WasSuccessful) override final;
};
