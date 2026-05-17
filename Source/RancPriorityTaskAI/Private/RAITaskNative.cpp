// Copyright Rancorous Games, 2024

#include "RAITaskNative.h"

float URAITaskNative::CalculatePriority_Implementation()
{
	return NativeCalculatePriority();
}

void URAITaskNative::BeginTask_Implementation(const FRAITaskInvokeArguments& Args)
{
	Super::BeginTask_Implementation(Args);
	NativeBeginTask(Args);
}

void URAITaskNative::EndTask_Implementation(bool Success, float BeginAgainCooldown, bool WasInterrupted)
{
	// NativeEndTask runs first so subclasses can still access live state (Character, etc.)
	NativeEndTask(Success, WasInterrupted);
	Super::EndTask_Implementation(Success, BeginAgainCooldown, WasInterrupted);
}

void URAITaskNative::OnPerceptionStimulus_Implementation(AActor* Actor, FAIStimulus Stimulus)
{
	NativeOnPerception(Actor, Stimulus);
}

void URAITaskNative::OnCustomTrigger_Implementation(FGameplayTag Trigger, UObject* Payload)
{
	NativeOnCustomTrigger(Trigger, Payload);
}

void URAITaskNative::OnInvokedTaskCompleted_Implementation(bool WasSuccessful)
{
	NativeOnInvokedTaskCompleted(WasSuccessful);
}
