// Copyright Rancorous Games, 2023

#include "RAIController.h"

#include "GameplayTagContainer.h"
#include "RAIManagerComponent.h"
#include "RAITaskComponent.h"
#include "Perception/AIPerceptionComponent.h"

class URAITaskComponent;
class URAIManagerComponent;

ARAIController::ARAIController()
{
}

inline void ARAIController::BeginPlay()
{
	Super::BeginPlay();
	
	if (AutoHandleSensoryInput)
	{
		AIPerceptionComponent = GetAIPerceptionComponent();
		if (!AIPerceptionComponent)
		{
			// Create and attach a new perception component if it doesn't exist
			AIPerceptionComponent = NewObject<UAIPerceptionComponent>(this);
			if (AIPerceptionComponent)
			{
				AIPerceptionComponent->RegisterComponent();
			}
		}
	
		AIPerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &ARAIController::OnPerceptionUpdated);
	}
}

void ARAIController::TraceThought(FString Thought)
{
   Thoughts.Add(Thought);

	// Check if the thoughts count exceeds the maximum allowed
	if (Thoughts.Num() > MaxThoughtMemoryCount)
	{
		// Calculate the number of thoughts to remove
		int32 ThoughtsToRemove = Thoughts.Num() - MaxThoughtMemoryCount;

		// Remove the older half of thoughts
		Thoughts.RemoveAt(0, ThoughtsToRemove);
	}
}

void ARAIController::TriggerCustom(TSubclassOf<URAITaskComponent> Task, FGameplayTag Trigger, UObject* Payload)
{
	if (ManagerComponent)
	{
		if (auto* FoundTask = ManagerComponent->GetTaskByClass(Task))
		{
			FoundTask->OnCustomTrigger(Trigger, Payload);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TriggerCustom: Task not found: %s"), *Task->GetName());
		}
	}
}

void ARAIController::TriggerCustomAll(FGameplayTag Trigger, UObject* Payload)
{
	if (ManagerComponent)
	{
		for (auto* Task : ManagerComponent->AllTasks)
		{
			Task->OnCustomTrigger(Trigger, Payload);
		}
	}
}

void ARAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	
	if (!ManagerComponent)
	{
		ManagerComponent = FindComponentByClass<URAIManagerComponent>();
	}

	if (ManagerComponent)
	{
		ManagerComponent->Initialize(this, InPawn);
	}
}

void ARAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (ManagerComponent)
	{
		ManagerComponent->OnPerceptionStimulus(Actor, Stimulus);
	}
}
