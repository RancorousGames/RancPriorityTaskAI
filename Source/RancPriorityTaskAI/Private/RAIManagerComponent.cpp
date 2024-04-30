// Copyright Rancorous Games, 2023

#include "RAIManagerComponent.h"

#include "RAITaskComponent.h"
#include "GameFramework/Pawn.h"
#include "RAIController.h"
#include "RAILogCategory.h"
#include "GameFramework/Character.h"
#include "RancUtilityLibrary.h"

URAIManagerComponent::URAIManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void URAIManagerComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URAIManagerComponent::Initialize(ARAIController* Controller, APawn* Pawn)
{
	if (OwningController == nullptr || Character == nullptr)
	{
		OwningController = Controller;

		if (!OwningController)
		{
			UE_LOG(LogRAI, Error, TEXT("RAIManagerComponent must be attached to an RAIController"))
		}
		else
		{
			Character = Cast<ACharacter>(Pawn);
			// error if controlledpawn null
			if (!Character)
			{
				UE_LOG(LogRAI, Error,
				       TEXT("Tried to initialize RAIManagerComponent but the controlled pawn was null"));
			}

			OwningController->GetComponents<URAITaskComponent>(AllTasks, false);
		}

		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("RAIManagerComponent initialized with %d tasks"), AllTasks.Num())
		}

		for (URAITaskComponent* TaskComponent : AllTasks)
		{
			UClass* TaskClass = TaskComponent->GetClass();
			TaskTypeToInstanceMap.Add(TaskClass, TaskComponent);

			TaskComponent->ManagerComponent = this;
			TaskComponent->DebugLoggingEnabled = DebugLoggingEnabled;
			TaskComponent->MaxTaskLoopCount = MaxTaskLoopCount;

			if (TaskComponent->IsPrimaryTask)
			{
				PrimaryTasks.Add(TaskComponent);
			}

			TaskComponent->OwnerController = OwningController;
		}

		for (URAITaskComponent* TaskComponent : AllTasks)
		{
			TaskComponent->Initialize(Character, OwningController);
		}
	}
}

URAITaskComponent* URAIManagerComponent::GetTaskByClass(TSubclassOf<URAITaskComponent> TaskClass) const
{
	if (URAITaskComponent* TaskComponent = TaskTypeToInstanceMap.FindRef(TaskClass))
	{
		return TaskComponent;
	}

	UE_LOG(LogRAI, Error, TEXT("Could not find task of class: %s, did you add the task to your AI?"),
	       *(TaskClass->GetFName().ToString() ))

	return nullptr;
}

void URAIManagerComponent::UpdateActiveTasks()
{
	if (OwningController == nullptr)
	{
		return;
	}

	if (ActiveTask && ReinvokeActiveTask)
	{
		// this is a fallback case where the task returned without finishing or initiating a wait, we will keep
		// we will keep starting it until it finishes or reaches the max loop count

		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Task %s returned without calling EndTask or initiating a wait, reinvoking. This indiciates a problem with your Task implementation."),
			       *(ActiveTask->GetFName().ToString() ))
		}

		ReinvokeActiveTask = false;
		StartTask(ActiveTask);
		return;
	}

	URAITaskComponent* BestTask = UpdateTaskPriorities();

	if (!BestTask || (ActiveTask && ActiveTask->IsDescendantOf(BestTask)))
	{
		return;
	}

	// If we are continuing the same task
	if (ActiveTask && (ActiveTask == BestTask))
	{
		if (!ActiveTask->IsTaskActive && ActiveTask->IsTaskReady()) // wake up if not active, should never happen
		{
			if (DebugLoggingEnabled)
			{
				UE_LOG(LogRAI, Display, TEXT("AI Active task  %s was not active, waking up."),
				       *(ActiveTask->GetFName().ToString() ))
			}

			StartTask(ActiveTask);
		}

		return;
	}

	if (!ActiveTask || !ActiveTask->IsTaskActive)
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("No Active task, starting best task  %s."),
			       *(BestTask->GetFName().ToString() ))
		}

		StartTask(BestTask);
	}
	else if (BestTask && CheckIfTaskShouldInterrupt(ActiveTask, BestTask))
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Task %s is interrupting task %s."),
			       *(BestTask->GetFName().ToString() ), *(ActiveTask->GetFName().ToString() ))
		}

		// We are interrupting one task for another
		if (auto* Ancestor = ActiveTask->GetOldestInvokingAncestor())
		{
			// ancestor will trigger EndTask in all its children
			Ancestor->EndTask(false, 0, true);
		}
		else
		{
			ActiveTask->EndTask(false, 0, true);
		}

		OwningController->StopMovement();
		OnAnyTaskExit.Broadcast(ActiveTask);

		StartTask(BestTask);
	}
}

void URAIManagerComponent::StartTask(URAITaskComponent* Task, FRAITaskInvokeArguments InvokeArguments)
{
	ActiveTask = Task;
	Task->BeginTask(InvokeArguments);

	if (DebugLoggingEnabled)
	{
		UE_LOG(LogRAI, Display, TEXT("Starting task %s."), *(Task->GetFName().ToString() ))
	}

	if (Task->IsTaskActive && !Task->IsWaiting && Task->Cooldown <= 0.f)
	{
		// The task returned without finishing or initiating a wait
		if (!AnnouncedBadTaskReturnWarning)
		{
			AnnouncedBadTaskReturnWarning = true;
			UE_LOG(LogRAI, Warning,
			       TEXT("Task %s returned without finishing or initiating a wait. Did you forget to call EndTask?"),
			       *(Task->GetFName().ToString() ))
		}

		ReinvokeActiveTask = true;
	}

	OnAnyTaskEnter.Broadcast(Task);
}

void URAIManagerComponent::ForceInterruptActiveTask(URAITaskComponent* AssumedActiveTask)
{
	if (AssumedActiveTask && AssumedActiveTask == ActiveTask)
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Forcing task %s to end."), *(ActiveTask->GetFName().ToString() ))
		}

		// Interrupt the active task
		AssumedActiveTask->EndTask(false);
		OnAnyTaskExit.Broadcast(ActiveTask);

		ActiveTask = nullptr;
	}
}

void URAIManagerComponent::OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus)
{
	for (URAITaskComponent* TaskComponent : AllTasks)
	{
		if (TaskComponent && TaskComponent->IsEnabled)
		{
			TaskComponent->OnPerceptionStimulus(Actor, Stimulus);
		}
	}
}

bool URAIManagerComponent::InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent* ParentInvokingTask,
                                      FRAITaskInvokeArguments& InvokeArguments)
{
	if (URAITaskComponent* InvokedTask = GetTaskByClass(TaskClass))
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Invoking task %s."), *(InvokedTask->GetFName().ToString() ))
		}

		InvokedTask->ParentInvokingTask = ParentInvokingTask;
		ParentInvokingTask->ChildInvokedTask = InvokedTask;
		InvokedTask->InvokeArgs = InvokeArguments;
		ParentInvokingTask->IsWaiting = true;
		StartTask(InvokedTask, InvokeArguments);

		return true;
	}
	else
	{
		UE_LOG(LogRAI, Error, TEXT("Could not find task of class: %s, did you add the task to your AI?"),
		       *(TaskClass->GetFName().ToString() ))
	}
	return false;
}

void URAIManagerComponent::TaskEnded(URAITaskComponent* Task)
{
	if (Task == ActiveTask)
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Active Task %s ended."), *(Task->GetFName().ToString() ))
		}

		ActiveTask = nullptr;
	}
}

void URAIManagerComponent::ReturnToInvokingTask(URAITaskComponent* CompletedTask, URAITaskComponent* ParentTask,
                                                bool Success)
{
	if (ParentTask != nullptr)
	{
		if (DebugLoggingEnabled)
		{
			UE_LOG(LogRAI, Display, TEXT("Task %s completed successfully, returning to invoking parent task %s"),
			       *(CompletedTask->GetClass()->GetName()), *(ParentTask->GetClass()->GetName()))
		}

		ActiveTask = ParentTask;
		ParentTask->OnInvokedTaskCompleted(Success);
	}
}


URAITaskComponent* URAIManagerComponent::UpdateTaskPriorities()
{
	URAITaskComponent* BestTask = nullptr;
	float BestTaskScore = 0.f;

	for (URAITaskComponent* Task : PrimaryTasks)
	{
		if (!Task || !Task->IsEnabled)
		{
			continue;
		}
		

		float Priority = Task->CalculatePriority();
		Task->SetPriority(Priority);

		if (Task->IsTaskReady() && Priority > BestTaskScore)
		{
			BestTaskScore = Priority;
			BestTask = Task;
		}
	}

	return BestTask;
}

bool URAIManagerComponent::CheckIfTaskShouldInterrupt(const URAITaskComponent* TaskToInterrupt,
                                                      const URAITaskComponent* InterruptingTask) const
{
	if (!ActiveTask || !InterruptingTask)
	{
		return false;
	}

	const ERAIInterruptionType ActiveTaskInterruptionType = TaskToInterrupt->InterruptType;

	float priorityGap = 0.f;
	switch (ActiveTaskInterruptionType)
	{
	case ERAIInterruptionType::Always:
		priorityGap = 0.01f;
		break;

	case ERAIInterruptionType::WaitASec:
		priorityGap = WaitASecInterruptPriorityGap;
		break;

	case ERAIInterruptionType::PreferablyNot:
		priorityGap = PreferablyNotInterruptPriorityGap;
		break;

	case ERAIInterruptionType::OnlyIfNeeded:
		priorityGap = OnlyIfNeededInterruptPriorityGap;
		break;

	case ERAIInterruptionType::IfPanic:
		priorityGap = IfPanicInterruptPriorityGap;
		break;

	case ERAIInterruptionType::IfLifeOrDeath:
		priorityGap = IfLifeOrDeathInterruptPriorityGap;
		break;

	case ERAIInterruptionType::Never:
		return false;
	}

	bool ShouldInterrupt = (InterruptingTask->GetPriority() - TaskToInterrupt->GetPriority()) > priorityGap;

	if (ShouldInterrupt && DebugLoggingEnabled)
	{
		URancUtilityLibrary::ThrottledLog(
			FString("Task ") + InterruptingTask->GetFName().ToString() + FString(" should interrupt ") + TaskToInterrupt
			->GetFName().ToString() + FString(" = ") + FString("true"), 3.f,
			FString("ShouldInterrupt"));
	}

	return ShouldInterrupt;
}
