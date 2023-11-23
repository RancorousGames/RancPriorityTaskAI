#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RAIDataStructures.h"
#include "RAITaskinvokeArguments.h"
#include "Perception/AIPerceptionTypes.h"
#include "RAITaskComponent.generated.h"

class URAIManagerComponent;
class ARAIController;


/*  The Purpose of this component is to encapsulate a specific task that an AI can do. */
UCLASS(Blueprintable, BlueprintType, ClassGroup=(UtilityCombat), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAITaskComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	URAITaskComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	//*************************************************************************
	//* Static references
	//*************************************************************************

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RAI|References")
	URAIManagerComponent* ManagerComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RAI|References")
	ARAIController* OwnerController = nullptr;

	//*************************************************************************
	//* Configuration
	//*************************************************************************

	/*  Whether a task is currently active/running */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Configuration")
	bool IsPrimaryTask = false;

	/*  Time in seconds that must pass until the task can fire again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
	float Cooldown = 0.0f;

	/*  A task reaching a higher priority of another task may interrupt the other task depending on the InterruptType */
	/*  Always will always let a higher priority task interrupt, Never will never let a higher priority task interrupt. */
	/*  In between options require a certain amount of exceeding priority. See ERAIInterruptionType for options. */
	/*  A task may switch InterruptType dynamically while active, but it will be reset to default InterruptType when ended. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
	ERAIInterruptionType InterruptType = ERAIInterruptionType::Always;

	/*  The default starting InterruptType, also used after a task has ended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
	ERAIInterruptionType DefaultInterruptType = ERAIInterruptionType::Always;

	/*  Whether the manager should end this task if it reaches zero priority regardless of interruption type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
	bool InterruptIfReachesZero = true;

	//*************************************************************************
	//* Status variables
	//*************************************************************************

	/*  Whether a task is currently active/running */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Transient, Category = "RAI|Status")
	bool IsTaskActive = false;

	/*  A task may be waiting for something to finish, e.g: An invoked task, a timer, a movement command to finish */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Status")
	bool IsWaiting = false;

	/* Sometimes a Primary Task may invoke another task to perform something, e.g. a GetFood task might invoke a Hunt task
	 This is a reference to the Primary Task that invoked this task */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Status")
	URAITaskComponent* ParentInvokingTask = nullptr;

	/*  Reference to the task that this task has invoked */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Status")
	URAITaskComponent* ChildInvokedTask = nullptr;

	/* The current InvokeArguments if invoked, if not invoked this will be accessible but not valid */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Status")
	FRAITaskInvokeArguments InvokeArgs;

	//*************************************************************************
	//* Methods
	//*************************************************************************

	/* Called by ManagerComponent only for Primary tasks, no need to implement for invoked tasks */
	UFUNCTION(BlueprintImplementableEvent, Category = RAI)
	float CalculatePriority();

	/*  Get the current priority of this task, if it is an invoked task it will return the priority of its oldest ancestor or 0 if not invoked */
	UFUNCTION(BlueprintCallable, Category = RAI)
	float GetPriority() const;

	/*  This is called by the manager component whenever we begin this task */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = RAI)
	void BeginTask(const FRAITaskInvokeArguments& InvokeArguments = FRAITaskInvokeArguments());

	/*  This is called by the manager component whenever we begin this task ends */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = RAI,
		Meta = (SuccessToolTip = "Whether the task succeeded.", AdvancedDisplay = "InterruptParentInvokers",
			InterruptParentInvokersToolTip =
			"Whether to interrupt the parent invokers. If true then parents will also end and OnInvokedTaskCompleted will not be called."
		))
	void EndTask(bool Success = true, bool InterruptParentInvokers = false);

	/* Start the task over and call BeginTask again */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void Restart();

	UFUNCTION(BlueprintImplementableEvent, Category = RAI)
	void OnInvokedTaskCompleted(bool WasSuccessful);

	/*  A Primary Task may invoke another task to perform something, e.g. a GetFood task might invoke a Hunt task */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, FRAITaskInvokeArguments InvokeArguments);

	/*  Add a thought to RAIControllers thoughts for debugging */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void TraceThought(FString Thought);

	/*  Call this to indicate the task is waiting for something else, e.g. an AIMoveTo command */
	UFUNCTION(BlueprintCallable, Category = RAI,
		Meta = (MaxWaitTimeToolTip="Maximum time to wait in seconds. Use 0 for indefinite waiting.",
			OverrideInterruptionTypeToolTip="Set to true to override the default interruption type while waiting.",
			InterruptTypeWhileWaitingToolTip="The interruption type to use while waiting.", AdvancedDisplay = "OverrideInterruptionType, InterruptTypeWhileWaiting"))
	void BeginWaiting(double MaxWaitTime = 0.f, bool OverrideInterruptionType = false,
	                  ERAIInterruptionType InterruptTypeWhileWaiting = ERAIInterruptionType::Never);

	/*  Call this to indicate we are done waiting  */
	UFUNCTION(BlueprintCallable, Category = RAI,
		Meta = (ExpandEnumAsExecs = "ReturnBranch", InterruptTypeToReturnToToolTip=
			"InterruptTypeToReturnTo is only used if OverrideInterruptionType was set to true when starting the wait.", AdvancedDisplay = "InterruptTypeToReturnTo"))
	void DoneWaiting(ERAIInterruptionType InterruptTypeToReturnTo, EDoneWaitingExecutionStates& ReturnBranch);

	/* Not yet implemented - Whether the task should fully simulate near player or simple simulate far from player */
	UFUNCTION(BlueprintCallable, Category = RAI)
	ERAIField GetSimulationField();

	/* Get the original invoking task */
	UFUNCTION(BlueprintPure, Category = RAI)
	URAITaskComponent* GetOldestInvokingAncestor() const;

	/*  If is parent of Task or parent of parent of Task etc */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsAncestorOf(const URAITaskComponent* Task) const;

	/*  If is child of Task or child of child of Task etc */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsDescendantOf(const URAITaskComponent* Task) const;

	//*************************************************************************
	//* Private bookkeeping
	//*************************************************************************

private:
	float WorldTimeBegun = -1.0f;

	float WorldTimeEnd = -1.0f;

	double MaxWaitDuration;

	bool IsOverridingInterruptionType;

	// Timer handle for the waiting period
	FTimerHandle WaitTimerHandle;

	FTimerHandle RestartTimerHandle;

	void OnWaitTimeout();


	//*************************************************************************
	//* Used by RAIManagerComponent only
	//*************************************************************************
public:
	/*  Whether to write highly detailed debug information to the log */
	bool DebugLoggingEnabled = false;
	int MaxTaskLoopCount; // Set on ManagerComponent. Engine will detect after 15 repeats in the same frame, we detect across frames within LoopCountDetectionPeriod seconds


	/* ONLY CALL FROM MANAGER COMPONENT */
	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	void OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus);

	/*  False if on Cooldown */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsTaskReady();

	void SetPriority(float NewPriority);

protected:
	//*************************************************************************
	//* Private
	//*************************************************************************

	/*  Last value calculated by CalculateUtility(). Use GetPriority() instead. */
	float Priority = 0.0f;

	int CurrentTaskLoopCount = 0;
	bool LoopPenaltyApplied = false;
	float LoopStartWorldTime = -1.0f;
	ERAIInterruptionType LoopPenaltySavedInterruptType;
	// during penalty, we temporarily prevent interruption, this is to set it back. 
	const float LoopCountDetectionPeriod = 1.f; // After how many seconds do we reset loopcount

	bool CheckForInfLoop();
};
