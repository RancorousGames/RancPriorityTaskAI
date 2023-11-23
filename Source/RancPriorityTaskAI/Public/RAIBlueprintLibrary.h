#pragma once

// This was an attempt at making a ai move to that would automatically do BeginWait/DoneWaiting

/*#include "RAIController.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AIController.h"
#include "LatentActions.h"
#include "Tasks/AITask_MoveTo.h"
#include "RAIBlueprintLibrary.generated.h"


struct FLatentActionInfo;

class FMoveToLatentAction : public FPendingLatentAction
{
public:
	FMoveToLatentAction(const FLatentActionInfo& LatentInfo, AAIController* Controller, FVector GoalLocation, AActor* GoalActor, float AcceptanceRadius)
	: ExecutionFunction(LatentInfo.ExecutionFunction), OutputLink(LatentInfo.Linkage), CallbackTarget(LatentInfo.CallbackTarget)
	{
		// Initiate the move task using AIMoveTo
		UAITask_MoveTo* MoveTask = UAITask_MoveTo::AIMoveTo(Controller, GoalLocation, GoalActor, AcceptanceRadius);
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		Response.FinishAndTriggerIf(bIsComplete, ExecutionFunction, OutputLink, CallbackTarget);
	}

private:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	bool bIsComplete = false;

	void OnMoveCompleted(EPathFollowingResult::Type Result)
	{
		bIsComplete = true;
	}
};


UCLASS()
class RANCPRIORITYTASKAI_API URAIBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Latent function to move AI to a location or actor and wait until it arrives
	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (AdvancedDisplay = "AcceptanceRadius,StopOnOverlap,AcceptPartialPath,bUsePathfinding,bUseContinuousGoalTracking,ProjectGoalOnNavigation", DefaultToSelf = "Controller", BlueprintInternalUseOnly = "TRUE", DisplayName = "Move To Location or Actor and Wait"))
	static UAITask_MoveTo* MoveToLocationOrActorAndWait(ARAIController* Controller, FVector GoalLocation, AActor* GoalActor = nullptr, float AcceptanceRadius = -1.f, bool bUseContinuousGoalTracking = false);


	UFUNCTION(BlueprintCallable, Category = "Test")
	static UAITask_MoveTo* TestBlueprintLib(ARAIController* Controller)
	{
		
		UAITask_MoveTo* MyTask = nullptr;

		return nullptr;
	}
	
};*/
