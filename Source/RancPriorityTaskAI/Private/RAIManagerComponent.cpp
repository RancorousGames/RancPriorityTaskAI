#include "RAIManagerComponent.h"

#include "RAITaskComponent.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Math/UnrealMathUtility.h"
#include "GameFramework/Pawn.h"
#include "Runtime/Engine/Public/DrawDebugHelpers.h"
#include "RAIController.h"

URAIManagerComponent::URAIManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void URAIManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.bCanEverTick = false;
}

void URAIManagerComponent::Initialize()
{
	OwningController = Cast<ARAIController>(GetOwner());

	if (!OwningController)
	{
		UE_LOG(LogTemp, Error, TEXT("RAIManagerComponent must be attached to an RAIController"))
	}
	else
	{
		ControlledPawn = OwningController->GetPawn();
		OwningController->GetComponents<URAITaskComponent>(AllTasks, false);
	}


	for (URAITaskComponent* TaskComponent : AllTasks)
	{
		UClass* TaskClass = TaskComponent->GetClass();
		TaskTypeToInstanceMap.Add(TaskClass, TaskComponent);

		if (TaskComponent->IsPrimaryTask)
		{
			PrimaryTasks.Add(TaskComponent);
		}

		TaskComponent->OwnerController = OwningController;
	}
}

URAITaskComponent* URAIManagerComponent::GetTaskByClass(TSubclassOf<URAITaskComponent> TaskClass) const
{
	if (URAITaskComponent* TaskComponent = TaskTypeToInstanceMap.FindRef(TaskClass))
	{
		return TaskComponent;
	}

	UE_LOG(LogTemp, Error, TEXT("Could not find task of class: %s, did you add the task to your AI?"),
	       *(TaskClass->GetFName().ToString() ))

	return nullptr;
}

void URAIManagerComponent::UpdateActiveTasks()
{
	if (ActiveTask && ReinvokeActiveTask)
	{
		// this is a fallback case where the task returned without finishing or initiating a wait, we will keep
		// we will keep starting it until it finishes or reaches the max loop count
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
		if (!ActiveTask->IsTaskActive) // wake up if not active, should never happen
		{
			UE_LOG(LogTemp, Warning, TEXT("AI Active task  %s was not active, waking up."),
			       *(ActiveTask->GetFName().ToString() ))

			StartTask(ActiveTask);
		}

		return;
	}

	if (!ActiveTask || !ActiveTask->IsTaskActive)
	{
		StartTask(BestTask);
	}
	else if (BestTask && CheckIfTaskShouldInterrupt(ActiveTask, BestTask))
	{
		ActiveTask->EndTask(false); //Ensure clean up
		OnAnyTaskExit.Broadcast(ActiveTask);

		StartTask(BestTask);
	}
}

void URAIManagerComponent::StartTask(URAITaskComponent* Task, FRAITaskInvokeArguments InvokeArguments)
{
	ActiveTask = Task;
	Task->BeginTask(InvokeArguments);

	if (Task->IsTaskActive && !Task->IsWaiting && Task->Cooldown <= 0.f)
	{
		// The task returned without finishing or initiating a wait
		if (!AnnouncedBadTaskReturnWarning)
		{
			AnnouncedBadTaskReturnWarning = true;
			UE_LOG(LogTemp, Warning,
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
		if (TaskComponent)
		{
			TaskComponent->OnPerceptionStimulus(Actor, Stimulus);
		}
	}
}

void URAIManagerComponent::InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent* ParentInvokingTask,
                                      FRAITaskInvokeArguments& InvokeArguments)
{
	if (URAITaskComponent* InvokedTask = GetTaskByClass(TaskClass))
	{
		InvokedTask->ParentInvokingTask = ParentInvokingTask;
		ParentInvokingTask->ChildInvokedTask = InvokedTask;
		InvokedTask->InvokeArgs = InvokeArguments;
		ParentInvokingTask->IsWaiting = true;
		OwningController->TraceThought(FString("Invoking task: ") + InvokedTask->GetFName().ToString());
		StartTask(InvokedTask, InvokeArguments);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could not find task of class: %s, did you add the task to your AI?"),
		       *(TaskClass->GetFName().ToString() ))
	}
}


URAITaskComponent* URAIManagerComponent::UpdateTaskPriorities()
{
	URAITaskComponent* BestTask = nullptr;
	float BestTaskScore = 0.f;

	for (URAITaskComponent* Task : PrimaryTasks)
	{
		if (!Task)
		{
			UE_LOG(LogTemp, Error, TEXT("Task %s was removed from the AIController"),
			       *(Task->GetFName().ToString() ))
			continue;
		}

		float Priority = Task->CalculatePriority();
		//This isn't const for debug purposes, so this method and loop can't be const.
		Task->Priority = Priority;

		if (Task->IsTaskReady() && Priority > BestTaskScore)
		{
			BestTaskScore = Priority;
			BestTask = Task;
		}
	}

	return BestTask;
}

bool URAIManagerComponent::CheckIfTaskShouldInterrupt(const URAITaskComponent* TaskToInterrupt,
                                                      const URAITaskComponent* InterruptingTask)
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
		return true;

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

	return (InterruptingTask->Priority - TaskToInterrupt->Priority) > priorityGap;
}
