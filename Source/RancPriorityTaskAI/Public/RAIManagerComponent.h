// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "RAIDataStructures.h"
#include "RAIScheduling.h"
#include "RAITaskinvokeArguments.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "RAIManagerComponent.generated.h"

class URAITaskComponent;
class URAIInterruptPolicy;
class APawn;
class ACharacter;
class ARAIController;
class UCharacterMovementComponent;
class UPawnMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUtilityTaskEvent, URAITaskComponent*, Task);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRAITaskEndEvent, URAITaskComponent*, Task, bool, Success, FGameplayTag, Reason, bool, WasInterrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRAIArbitrationEvent, const FRAIArbitrationResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRAIInvokeRejectedEvent, URAITaskComponent*, Parent, TSubclassOf<URAITaskComponent>, TaskClass, ERAIInvokeRejectReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRAILoopEvent, URAITaskComponent*, Task, int32, Count, double, Window);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRAIReevaluationEvent, FGameplayTag, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRAIInvariantEvent, const FString&, Message);
DECLARE_MULTICAST_DELEGATE_OneParam(FRAINativeTaskBegin, URAITaskComponent*);
DECLARE_MULTICAST_DELEGATE_FourParams(FRAINativeTaskEnd, URAITaskComponent*, bool, FGameplayTag, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FRAINativeArbitration, const FRAIArbitrationResult&);

/*
Determines which tasks are the best to do.
*/
UCLASS(Blueprintable, BlueprintType, ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAIManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	URAIManagerComponent();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	
public:

//*************************************************************************
//* Delegate events
//*************************************************************************

	UPROPERTY(BlueprintAssignable)
	FUtilityTaskEvent OnAnyTaskEnter;
	
	UPROPERTY(BlueprintAssignable)
	FUtilityTaskEvent OnAnyTaskExit;
	UPROPERTY(BlueprintAssignable) FUtilityTaskEvent OnTaskBegin;
	UPROPERTY(BlueprintAssignable) FRAITaskEndEvent OnTaskEnd;
	UPROPERTY(BlueprintAssignable) FRAIArbitrationEvent OnArbitration;
	UPROPERTY(BlueprintAssignable) FRAIInvokeRejectedEvent OnInvokeRejected;
	UPROPERTY(BlueprintAssignable) FRAILoopEvent OnLoopDetected;
	UPROPERTY(BlueprintAssignable) FRAIReevaluationEvent OnReevaluationRequested;
	UPROPERTY(BlueprintAssignable) FRAIInvariantEvent OnInvariantViolated;
	FRAINativeTaskBegin OnTaskBeginNative;
	FRAINativeTaskEnd OnTaskEndNative;
	FRAINativeArbitration OnArbitrationNative;

//*************************************************************************
//* Static references
//*************************************************************************
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	ARAIController* OwningController = nullptr;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	ACharacter* Character = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	APawn* Pawn = nullptr;
	
	UPROPERTY(VisibleAnywhere, Transient, Category = Focus)
	AActor* ControllerFocus = nullptr;
	
	/*  All the possible tasks. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	TArray<URAITaskComponent*> AllTasks = {};
	
//*************************************************************************
//* Configuration Variables
//*************************************************************************
	
	/*  Whether to write highly detailed debug information to the log */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Debug")
	bool DebugLoggingEnabled = false;
	
	/*  How many starts/restarts a task can do within a short period of time without getting detected as an infinite loop */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RAI|Debug")
	int MaxTaskLoopCount = 25;
	
	/*  Minimum value a task must score to be considered */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
	float TaskThreshold = 0.f;
	UPROPERTY(EditAnywhere, Instanced, Category = "RAI|Manager")
	URAIInterruptPolicy* InterruptPolicy = nullptr;
	UPROPERTY(EditAnywhere, Category = "RAI|Manager", meta=(ClampMin="1"))
	int32 MaxInvokeDepth = 6;
	UPROPERTY(EditAnywhere, Category = "RAI|Compatibility", meta=(DeprecatedProperty, DeprecationMessage="Running tasks no longer need reinvocation. Prefer timers or activities."))
	bool bLegacyReinvokeIfNotWaiting = false;
	UPROPERTY(EditAnywhere, Category = "RAI|Debug") bool bCheckInvariants = true;
	/** Forces priority explanations on. Debug consumers normally use AddExplanationDemand instead. */
	UPROPERTY(EditAnywhere, Category = "RAI|Debug") bool bCaptureExplanations = false;
	UPROPERTY(EditAnywhere, Category = "RAI|Debug", meta=(ClampMin="0")) int32 TraceCapacity = 128;
	/** Forces the trace ring to record. Otherwise it records only while a consumer holds trace demand. */
	UPROPERTY(EditAnywhere, Category = "RAI|Debug") bool bAlwaysRecordTrace = false;
	UPROPERTY(EditAnywhere, Category = "RAI|Debug") double RunningDiagnosticSeconds = 30.0;

	/* Minimum priority difference that must be overcome to interrupt a task with interruption type WaitASec */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
	float WaitASecInterruptPriorityGap = 10.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type PreferablyNotInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
	float PreferablyNotInterruptPriorityGap = 25.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type OnlyIfNeeded */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
	float OnlyIfNeededInterruptPriorityGap = 45.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type IfPanicInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
	float IfPanicInterruptPriorityGap = 95.f;
	/* Minimum priority difference that must be overcome to interrupt a task with interruption type IfLifeOrDeathInterrupt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Manager")
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
	
	/*  Current active Task.  */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	URAITaskComponent* ActiveTask = {};
	
	/*  PrimaryTasks is a subset of AllTasks that are primary and need priority updates */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Manager")
	TArray<URAITaskComponent*> PrimaryTasks = {};
	
//*************************************************************************
//* Methods
//*************************************************************************

	
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager")
	URAITaskComponent* GetTaskByClass(TSubclassOf<URAITaskComponent> TaskClass) const;

	/* Call this to update the AIs priorities and update active task if needed, typicall on tick or slow tick */
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager")
	void UpdateActiveTasks();

	/* E.g. when a task is deemed to have timed out */
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager")
	void ForceInterruptActiveTask(URAITaskComponent* AssumedActiveTask);

	/** Broadcasts a custom trigger to all tasks. */
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager")
	void TriggerCustomEvent(FGameplayTag Trigger, UObject* Payload);
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager") void Deinitialize();
	UFUNCTION(BlueprintCallable, Category = "RAI|Manager") void RequestReevaluation(FGameplayTag Reason);
	UFUNCTION(BlueprintPure, Category = "RAI|Manager") TArray<FRAITraceRecord> GetTrace() const;

	/** Reference-counted request for DescribePriority terms in arbitration results (debug consumers such as MindView). */
	void AddExplanationDemand() { ++ExplanationDemand; }
	void RemoveExplanationDemand() { ExplanationDemand = FMath::Max(ExplanationDemand - 1, 0); }
	bool IsCapturingExplanations() const { return bCaptureExplanations || ExplanationDemand > 0; }
	/** Reference-counted request for the trace ring. The ring is empty and unallocated while nobody needs it. */
	void AddTraceDemand() { ++TraceDemand; }
	void RemoveTraceDemand() { TraceDemand = FMath::Max(TraceDemand - 1, 0); }
	bool IsRecordingTrace() const { return TraceCapacity > 0 && (bAlwaysRecordTrace || TraceDemand > 0); }
	UFUNCTION(BlueprintPure, Category = "RAI|Manager") URAITaskComponent* GetActiveTask() const { return ActiveTask; }
	const TArray<URAITaskComponent*>& GetAllTasks() const { return AllTasks; }
	const TArray<URAITaskComponent*>& GetPrimaryTasks() const { return PrimaryTasks; }
	UFUNCTION(BlueprintPure, Category = "RAI|Manager") ARAIController* GetOwningController() const { return OwningController; }
	UFUNCTION(BlueprintPure, Category = "RAI|Manager") APawn* GetControlledPawn() const { return Pawn; }
	bool ValidateInvariants() const;
	void SetServices(TSharedPtr<IRAITimeSource> Time, TSharedPtr<IRAIScheduler> Scheduler);
	double GetNow() const;
	IRAIScheduler* GetScheduler() const { return Scheduler.Get(); }
	void RebuildPrimaryTasks();
	virtual void Deactivate() override;

	
//*************************************************************************
//* Only called from RAIManagerComponent or self
//*************************************************************************
	
	void Initialize(ARAIController* Controller, APawn* Pawn);
	void OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus);
	bool InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent*  ParentInvokingTask, FRAITaskInvokeArguments& InvokeArguments);
	ERAIInvokeResult InvokeTaskWithResult(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent* Parent,
		const FRAITaskInvokeArguments& Arguments, ERAIInvokeRejectReason& RejectReason);
	void RequestTaskEnd(URAITaskComponent* Task, bool Success, float Cooldown, bool Interrupted, FGameplayTag Reason);
	void RestartTask(URAITaskComponent* Task);
	void RecordTrace(ERAITraceType Type, const URAITaskComponent* Task, FGameplayTag Reason = {}, float Value = 0.f,
		const URAITaskComponent* Other = nullptr, uint8 Code = 0, bool Success = false, bool Interrupted = false);
	void TaskEnded(URAITaskComponent* Task);
	void ReturnToInvokingTask(URAITaskComponent* CompletedTask, URAITaskComponent* ParentTask, bool Success);

	//*************************************************************************
//* Private
//*************************************************************************
private:
	struct FPendingEnd
	{
		TWeakObjectPtr<URAITaskComponent> Task;
		bool Success;
		float Cooldown;
		bool Interrupted;
		FGameplayTag Reason;
		uint64 Generation;
	};
	TArray<FPendingEnd> PendingEnds;
	TSharedPtr<IRAITimeSource> TimeSource;
	TSharedPtr<IRAIScheduler> Scheduler;
	mutable TMap<UClass*, URAITaskComponent*> TaskByClass;
	UPROPERTY(Transient) TArray<FRAITraceRecord> TraceRing;
	int32 TraceWriteIndex = 0;
	int32 ExplanationDemand = 0;
	int32 TraceDemand = 0;
	int32 OperationDepth = 0;
	uint64 LifecycleGeneration = 0;
	bool bDrainingEnds = false;
	bool bDeinitializing = false;
	bool bUpdating = false;
	bool bDirty = false;
	bool bHasArbitration = false;
	bool bForceArbitrationEvent = false;
	TWeakObjectPtr<URAITaskComponent> LastWinner;
	TWeakObjectPtr<URAITaskComponent> LastRoot;
	ERAIArbitrationDecision LastDecision = ERAIArbitrationDecision::NoCandidate;
	FRAIScheduleHandle DeferredDrain;
	void DrainEnds();
	void FinishEnd(const FPendingEnd& End);
	void LeaveOperation(bool DeferDrain = false);
	void ScheduleDrain();
	
	void StartTask(URAITaskComponent* Task, FRAITaskInvokeArguments InvokeArgument = FRAITaskInvokeArguments());
	bool CheckIfTaskShouldInterrupt(const URAITaskComponent* ActiveTask, const URAITaskComponent* InterruptingTask) const;
};


