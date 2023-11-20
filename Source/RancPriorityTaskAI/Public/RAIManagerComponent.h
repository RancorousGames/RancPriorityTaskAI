#pragma once

#include "CoreMinimal.h"
#include "RAITaskinvokeArguments.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "RAIManagerComponent.generated.h"

class URAITaskComponent;
class APawn;
class ARAIController;
class UCharacterMovementComponent;
class UPawnMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUtilityTaskEvent, URAITaskComponent*, Task);

/*
Determines which tasks are the best to do.
*/
UCLASS(Blueprintable, BlueprintType, ClassGroup=(UtilityCombat), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAIManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	URAIManagerComponent();

protected:

	virtual void BeginPlay() override;

public:

//*************************************************************************
//* Delegate events
//*************************************************************************

	UPROPERTY(BlueprintAssignable)
	FUtilityTaskEvent OnAnyTaskEnter;
	
	UPROPERTY(BlueprintAssignable)
	FUtilityTaskEvent OnAnyTaskExit;

//*************************************************************************
//* Static references
//*************************************************************************
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Manager)
	ARAIController* OwningController = nullptr;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = ControlledPawn)
	APawn* ControlledPawn = nullptr;
	
	UPROPERTY(VisibleAnywhere, Transient, Category = Focus)
	AActor* ControllerFocus = nullptr;


//*************************************************************************
//* Configuration Variables
//*************************************************************************
	
	/*  Minimum value a task must score to be considered */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Manager)
	float TaskThreshold = 0.1f;

	/* Minimum priority difference that must be overcome to interrupt a task with interruption type WaitASec */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Manager)
	float WaitASecInterruptPriorityGap = 10.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type PreferablyNotInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Manager)
	float PreferablyNotInterruptPriorityGap = 25.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type OnlyIfNeeded */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Manager)
	float OnlyIfNeededInterruptPriorityGap = 45.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type IfPanicInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Manager)
	float IfPanicInterruptPriorityGap = 95.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type IfLifeOrDeathInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Manager)
	float IfLifeOrDeathInterruptPriorityGap = 250.f;
	
//*************************************************************************
//* Status
//*************************************************************************

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadWrite, Category = Focus)
	float DistanceToFocus = -1.0f;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadWrite, Category = Focus)
	float DistanceToFocusLastDetectedPoint = -1.0f;

	UPROPERTY(EditAnywhere, Transient, BlueprintReadWrite, Category = Focus)
	FVector FocusLastDetectedPoint = FVector(0.0f, 0.0f, 0.0f);
	
	/*  All the possible tasks. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Manager)
	TArray<URAITaskComponent*> AllTasks = {};
	
	/* same elements as AllTasks but more efficient lookup */
	TMap<TSubclassOf<URAITaskComponent>, URAITaskComponent*> TaskTypeToInstanceMap = {};
	
	/*  Current active Task.  */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Manager)
	URAITaskComponent* ActiveTask = {};
	
	/*  PrimaryTasks is a subset of AllTasks that are primary and need priority updates */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Manager)
	TArray<URAITaskComponent*> PrimaryTasks = {};
	
//*************************************************************************
//* Methods
//*************************************************************************

	UFUNCTION(BlueprintCallable, Category = Initialize)
	void Initialize();
	
	UFUNCTION(BlueprintCallable, Category = Basic)
	URAITaskComponent* GetTaskByClass(TSubclassOf<URAITaskComponent> TaskClass) const;

	/* Call this to update the AIs priorities and update active task if needed, typicall on tick or slow tick */
	UFUNCTION(BlueprintCallable, Category = Basic)
	void UpdateActiveTasks();

	/* E.g. when a task is deemed to have timed out */
	UFUNCTION(BlueprintCallable, Category = Basic)
	void ForceInterruptActiveTask(URAITaskComponent* AssumedActiveTask);

	void OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus);
	
	void InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent*  ParentInvokingTask, FRAITaskInvokeArguments& InvokeArguments);

//*************************************************************************
//* Private
//*************************************************************************
private:

	void StartTask(URAITaskComponent* Task, FRAITaskInvokeArguments InvokeArgument = FRAITaskInvokeArguments());
	
	URAITaskComponent* UpdateTaskPriorities();

	bool AnnouncedBadTaskReturnWarning = false;
	bool ReinvokeActiveTask = false;

	bool CheckIfTaskShouldInterrupt(const URAITaskComponent* ActiveTask, const URAITaskComponent* InterruptingTask);
};

