#include "RAIController.h"
#include "RAIManagerComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Perception/AIPerceptionComponent.h"


class URAITaskComponent;
class URAIManagerComponent;

ARAIController::ARAIController()
{
}

inline void ARAIController::BeginPlay()
{
	Super::BeginPlay();
	ManagerComponent = FindComponentByClass<URAIManagerComponent>();

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



FVector ARAIController::GetFocalPointOnActor(const AActor* Actor) const
{
    if( Actor && UKismetMathLibrary::ClassIsChildOf(Actor->GetClass(),APawn::StaticClass()) )
    {
        return (Actor->GetActorLocation() + FVector(0.0f,0.0f,FocusEyeHeight)* (Actor->GetActorScale() ).Z  );
    }
    
    return Super::GetFocalPointOnActor(Actor);
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

void ARAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (ManagerComponent)
	{
		ManagerComponent->OnPerceptionStimulus(Actor, Stimulus);
	}
}
