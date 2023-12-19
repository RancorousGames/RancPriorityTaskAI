// Copyright Rancorous Games, 2023

#include "RAITaskComponent.h"

#include "GameplayTagContainer.h"
#include "RAIManagerComponent.h"
#include "RAIController.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"

// Sets default values for this component's properties
URAITaskComponent::URAITaskComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void URAITaskComponent::BeginPlay()
{
	Super::BeginPlay();
	
	InterruptType = DefaultInterruptType;

	// Validate configuration values.
	if (Cooldown < 0.0f)
	{
		Cooldown = 0.0f;
	}
}

void URAITaskComponent::BeginTask_Implementation(const FRAITaskInvokeArguments& InvokeArguments)
{
	OwnerController->TraceThought(FString("Beginning: ") + GetFName().ToString());
	WorldTimeBegun = GetWorld()->GetTimeSeconds();
	IsTaskActive = true;
	IsWaiting = false;
	
	CheckForInfLoop();
}

void URAITaskComponent::EndTask_Implementation(bool Success, bool WasInterrupted)
{
	if (DebugLoggingEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("Task %s ended with success %d"), *GetClass()->GetName(), Success);
	}
	
	ManagerComponent->TaskEnded(this);
	WorldTimeEnd = GetWorld()->GetTimeSeconds();
	IsTaskActive = false;
	IsWaiting = false;
	InvokeArgs = FRAITaskInvokeArguments();
	InterruptType = DefaultInterruptType;

	if (ParentInvokingTask != nullptr)
	{
		ParentInvokingTask->IsWaiting = false;
		auto* CurrentParentInvokingTask = ParentInvokingTask;
		ParentInvokingTask = nullptr;

		if (!WasInterrupted)
		{
			CurrentParentInvokingTask->CheckForInfLoop();

			ManagerComponent->ReturnToInvokingTask(this, CurrentParentInvokingTask, Success);
		}
	}

	if (ChildInvokedTask != nullptr)
	{
		ChildInvokedTask->EndTask(false, WasInterrupted);
		ChildInvokedTask = nullptr;
	}
}

bool URAITaskComponent::IsTaskReady()
{
	if (Cooldown <= 0.0f || WorldTimeBegun <= 0.0f)
	{
		return true; //Either Cooldown is none, or we haven't done the task yet.
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	
	if (CurrentTime - WorldTimeBegun >= Cooldown)
	{
		return true;
	}
	
	return false;
}

void URAITaskComponent::SetPriority(float NewPriority)
{
	Priority = NewPriority;
}


bool URAITaskComponent::CheckForInfLoop()
{
	// If we get more than MaxTaskLoopCount calls within LoopCountDetectionPeriod then we call it an infinite loop
	
	const float Elapsed = GetWorld()->GetTimeSeconds() - LoopStartWorldTime;
	if (LoopStartWorldTime < 0 || Elapsed > LoopCountDetectionPeriod)
	{
		LoopStartWorldTime = GetWorld()->GetTimeSeconds();
		CurrentTaskLoopCount = 0;
		return false;
	}
	
	CurrentTaskLoopCount++;
	if (CurrentTaskLoopCount >= MaxTaskLoopCount)
	{
		 if (DebugLoggingEnabled)
		 {
			 UE_LOG(LogTemp, Warning, TEXT("Task %s Infinite loop detection"), *GetClass()->GetName());
		 }
		
		if (Cooldown <= 0.f)
		{
			// go through each parent and set cooldown
			URAITaskComponent* TaskToSetCooldown = this;
			while (TaskToSetCooldown != nullptr)
			{
				TaskToSetCooldown->Cooldown = 1.f;
				UE_LOG(LogTemp, Warning, TEXT("Task %s seems to be in an infinite loop, adding a cooldown to it"), *GetClass()->GetName());
				TaskToSetCooldown = TaskToSetCooldown->ParentInvokingTask;
			}

			LoopPenaltyApplied = true;
		}

		return true;
	}

	return false;
}

void URAITaskComponent::OnPerceptionStimulus_Implementation(AActor* actor, FAIStimulus Stimulus)
{
}

void URAITaskComponent::OnCustomTrigger_Implementation(FGameplayTag Trigger, UObject* Payload)
{
}

float URAITaskComponent::GetPriority() const
{
	if (!IsPrimaryTask)
	{
		if (const URAITaskComponent* AncestorTask = GetOldestInvokingAncestor())
		{
			return AncestorTask->Priority;
		}
	}
	
	return Priority;
}

void URAITaskComponent::Restart()
{
	if (DebugLoggingEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("Task %s restarting"), *GetClass()->GetName());
	}
	
	if (LoopPenaltyApplied || CheckForInfLoop())
	{
		// If this task has been detected as in an infinite loop, we will respect cooldown on Restart
		if (IsTaskReady())
		{
			if (DebugLoggingEnabled)
			{
				UE_LOG(LogTemp, Display, TEXT("Task %s delayed restart ready"), *GetClass()->GetName());
			}
			
			InterruptType = LoopPenaltySavedInterruptType;
			BeginTask(InvokeArgs);
		}
		else
		{
			if (DebugLoggingEnabled)
			{
				UE_LOG(LogTemp, Display, TEXT("Task %s has penalty so delaying Restart"), *GetClass()->GetName());
			}
			
			LoopPenaltySavedInterruptType = InterruptType;
			GetWorld()->GetTimerManager().SetTimer(RestartTimerHandle, this, &URAITaskComponent::Restart, Cooldown, false);
			InterruptType = ERAIInterruptionType::Never;
		}
	}
	else
	{
		BeginTask(InvokeArgs);
	}
}

void URAITaskComponent::InvokeTask(TSubclassOf<URAITaskComponent> TaskClass,
                                   FRAITaskInvokeArguments InvokeArguments)
{
	ManagerComponent->InvokeTask(TaskClass, this, InvokeArguments);
}

void URAITaskComponent::TraceThought(FString Thought)
{
	OwnerController->TraceThought(Thought);
}

ERAIField URAITaskComponent::GetSimulationField()
{
	// Todo: Implement
	return ERAIField::NearField;
}

URAITaskComponent* URAITaskComponent::GetOldestInvokingAncestor() const
{
	URAITaskComponent* CurrentParentInvokingTask = ParentInvokingTask;
	while(CurrentParentInvokingTask && CurrentParentInvokingTask->ParentInvokingTask != nullptr)
	{
		CurrentParentInvokingTask = CurrentParentInvokingTask->ParentInvokingTask;
	}

	return CurrentParentInvokingTask;
}

void URAITaskComponent::BeginWaiting(double MaxWaitTime, bool OverrideInterruptionType, ERAIInterruptionType InterruptTypeWhileWaiting)
{
	// Set the task to waiting state
	IsWaiting = true;

	// If a valid wait time is provided, set up a timer to end waiting
	if (MaxWaitTime > 0.f)
	{
		// Assuming you have access to a World context or TimerManager
		GetWorld()->GetTimerManager().SetTimer(WaitTimerHandle, this, &URAITaskComponent::OnWaitTimeout, MaxWaitTime, false);
	}
	IsOverridingInterruptionType = OverrideInterruptionType;
	if (OverrideInterruptionType)
	{
		InterruptType = InterruptTypeWhileWaiting;
	}
}

void URAITaskComponent::DoneWaiting(ERAIInterruptionType InterruptTypeToReturnTo, EDoneWaitingExecutionStates& ReturnBranch)
{
	const bool WasInterrupted = !IsTaskActive;
	
	if (DebugLoggingEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("Task %s done waiting"), *GetClass()->GetName());
	}

	if (WasInterrupted)
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogTemp, Display, TEXT("Task %s was interrupted while waiting"), *GetClass()->GetName());
		}
		
		ReturnBranch = EDoneWaitingExecutionStates::TaskEnded;
	}
	else
	{
		ReturnBranch = EDoneWaitingExecutionStates::Continue;
	}

	
	if (IsWaiting)
	{
		// Clear the timer if it's active
		if (WaitTimerHandle.IsValid())
		{
			GetWorld()->GetTimerManager().ClearTimer(WaitTimerHandle);
		}

		// Set the task to not waiting state
		IsWaiting = false;

		if (!WasInterrupted && IsOverridingInterruptionType)
		{
			InterruptType = InterruptTypeToReturnTo;
		}
	}
}

void URAITaskComponent::OnWaitTimeout()
{
	// This function is called when the wait time exceeds MaxWaitTime

	// Set the task to not waiting state
	IsWaiting = false;
	
	OwnerController->TraceThought(FString::Printf(TEXT("Task %s timed out!"), *GetClass()->GetName()));

	ManagerComponent->ForceInterruptActiveTask(this);
}

bool URAITaskComponent::IsAncestorOf(const URAITaskComponent* Task) const
{
	const URAITaskComponent *ParentTask = Task->ParentInvokingTask;
	while (ParentTask != nullptr)
	{
		if (ParentTask == this)
		{
			return true;
		}
		ParentTask = ParentTask->ParentInvokingTask;
	}
	
	return false;
}

bool URAITaskComponent::IsDescendantOf(const URAITaskComponent* Task) const
{
	const URAITaskComponent *ChildTask = Task->ChildInvokedTask;
	while (ChildTask != nullptr)
	{
		if (ChildTask == this)
		{
			return true;
		}
		ChildTask = ChildTask->ChildInvokedTask;
	}

	return false;
}
