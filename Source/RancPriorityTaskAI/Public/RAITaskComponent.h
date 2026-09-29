// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RAIDataStructures.h"
#include "RAIScheduling.h"
#include "RAITaskinvokeArguments.h"
#include "Perception/AIPerceptionTypes.h"
#include "RAITaskComponent.generated.h"

class URAIManagerComponent;
class ARAIController;
class ACharacter;
class APawn;

/*
 * The purpose of this component is to encapsulate a specific task that an AI can do.
 *
 * Lifecycle (driven by URAIManagerComponent):
 *   BeginTask -> Running | Waiting (BeginWaiting / invoked child) -> EndTask -> Inactive
 * A task that returns from BeginTask without waiting is simply Running; it is not restarted.
 * Ending a task always ends its invoked descendants first, as interrupted.
 * A non-interrupted end returns to the invoking parent (OnInvokedTaskCompleted[WithReason]).
 *
 * Tasks are server-side: the manager only arbitrates where the controller has authority.
 *
 * Status fields (IsTaskActive, IsWaiting, ParentInvokingTask, ChildInvokedTask, InvokeArgs) are written by the
 * manager only. Treat them as read-only; they will become protected in 2.0.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAITaskComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class URAIManagerComponent;

public:
	URAITaskComponent();

	//*************************************************************************
	//* References (set by the manager)
	//*************************************************************************

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|References")
	URAIManagerComponent* ManagerComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|References")
	ARAIController* OwnerController = nullptr;

	/** Controlled pawn. Works for any APawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|References")
	APawn* Pawn = nullptr;

	/** Cast of Pawn to ACharacter (null for non-character pawns). Prefer Pawn in new code. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|References")
	ACharacter* Character = nullptr;

	//*************************************************************************
	//* Configuration
	//*************************************************************************

	/* Used to dynamically enable/disable the prioritization of this task. A disabled task can still be invoked.
	 * Prefer SetTaskEnabled at runtime, which also requests reevaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Status")
	bool IsEnabled = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
	bool IsPrimaryTask = true;

	/*  Seconds that must pass until the task can begin again. Measured according to CooldownBasis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
	float Cooldown = 0.0f;

	/*  What Cooldown is measured from. BeginToBegin is the historic behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
	ERAICooldownBasis CooldownBasis = ERAICooldownBasis::BeginToBegin;
	
	/* Single use cooldown, measured from the end of the task. Set by EndTask's BeginAgainCooldown, cleared on the next begin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
	float NextBeginCooldown = 0.0f;

	/*  A task reaching a higher priority of another task may interrupt the other task depending on the InterruptType */
	/*  Always will always let a higher priority task interrupt, Never will never let a higher priority task interrupt. */
	/*  In between options require a certain amount of exceeding priority. See ERAIInterruptionType for options. */
	/*  A task may switch InterruptType dynamically while active, but it will be reset to default InterruptType when ended. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Status")
	ERAIInterruptionType InterruptType = ERAIInterruptionType::Always;

	/*  The default starting InterruptType, also used after a task has ended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
	ERAIInterruptionType DefaultInterruptType = ERAIInterruptionType::Always;

	/*  When this task is the root of the active chain and its priority drops to the manager's TaskThreshold or below,
	 *  the manager ends the chain (interrupted, reason RAI.Outcome.PriorityZero) regardless of InterruptType. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
	bool InterruptIfReachesZero = true;

	//*************************************************************************
	//* Status (read-only, written by the manager)
	//*************************************************************************

	/*  Whether a task is currently active/running (RunState != Inactive) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Status")
	bool IsTaskActive = false;

	/*  Waiting on an explicit BeginWaiting or on an invoked child */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Status")
	bool IsWaiting = false;

	/* The task that invoked this task, if any. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Status")
	URAITaskComponent* ParentInvokingTask = nullptr;

	/*  The task this task has invoked, if any. End it with EndInvokedChild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Status")
	URAITaskComponent* ChildInvokedTask = nullptr;

	/* The current InvokeArguments if invoked, if not invoked this will be accessible but not valid */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "RAI|Status")
	FRAITaskInvokeArguments InvokeArgs;

	//*************************************************************************
	//* Methods
	//*************************************************************************

	/* Called by ManagerComponent only for Primary tasks, no need to implement for invoked tasks */
	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	float CalculatePriority();
	virtual float CalculatePriority_Implementation();

	/* Optional: named terms explaining the last priority, captured when the manager's bCaptureExplanations is set. */
	virtual void DescribePriority(FRAIPriorityExplanation& Out) const {}

	/*  This task's own last computed priority (0 if never computed). */
	UFUNCTION(BlueprintCallable, Category = RAI)
	float GetPriority() const;

	/*  Priority that competes in arbitration for this task's chain: the chain root's priority, or own if not in a chain. */
	UFUNCTION(BlueprintPure, Category = RAI)
	float GetEffectivePriority() const;
	
	/*  This is called when the AIController OnPossess is called and it and ManagerComponent are both initialized */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = RAI)
	void Initialize(ACharacter* _Character, ARAIController* _OwnerController);

	/*  Called by the manager component whenever this task begins; calls BeginTask for blueprint implementations */
	virtual void BeginTaskCore(const FRAITaskInvokeArguments& InvokeArguments = FRAITaskInvokeArguments());

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = RAI)
	void BeginTask(const FRAITaskInvokeArguments& InvokeArguments = FRAITaskInvokeArguments());

	/*  End this task. Descendants end first (interrupted). Unless interrupted, the invoking parent is resumed. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = RAI,
		Meta = (SuccessToolTip = "Whether the task succeeded.", AdvancedDisplay = "WasInterrupted, BeginAgainCooldown",
		    BeginAgainCooldownToolTip = "Sets a single use cooldown, measured from now, preventing the task from being started again for the specified duration.",
			WasInterruptedToolTip =
			"If the task ended because of a more important task interrupted it. The parent is not resumed and OnInvokedTaskCompleted is not called."
		))
	void EndTask(bool Success = true, float BeginAgainCooldown = 0, bool WasInterrupted = false);

	/*  EndTask with an outcome reason (e.g. RAI.Outcome.Timeout or a game-defined tag), delivered to the parent. */
	UFUNCTION(BlueprintCallable, Category = RAI, Meta = (AdvancedDisplay = "BeginAgainCooldown, WasInterrupted"))
	void EndTaskWithReason(bool Success, FGameplayTag Reason, float BeginAgainCooldown = 0, bool WasInterrupted = false);

	/* Start the task over and call BeginTask again */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void Restart();

	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	void OnInvokedTaskCompleted(bool WasSuccessful);
	virtual void OnInvokedTaskCompleted_Implementation(bool WasSuccessful) {}

	/*  Called by the manager when an invoked child ends without interruption. Default calls OnInvokedTaskCompleted. */
	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	void OnInvokedTaskCompletedWithReason(bool WasSuccessful, FGameplayTag Reason);
	virtual void OnInvokedTaskCompletedWithReason_Implementation(bool WasSuccessful, FGameplayTag Reason);

	/*  A task may invoke another task to perform something, e.g. a GetFood task might invoke a Hunt task.
	 *  Returns true when the child started or completed immediately (completion is then delivered after this call
	 *  returns), false when rejected (nothing changed). */
	UFUNCTION(BlueprintCallable, Category = RAI)
	bool InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, FRAITaskInvokeArguments InvokeArguments);

	/*  InvokeTask with the detailed result. */
	UFUNCTION(BlueprintCallable, Category = RAI)
	ERAIInvokeResult InvokeTaskWithResult(TSubclassOf<URAITaskComponent> TaskClass, FRAITaskInvokeArguments InvokeArguments,
	                                      ERAIInvokeRejectReason& RejectReason);

	/*  End the active invoked child (interrupted: OnInvokedTaskCompleted is not called). This task resumes as the active task. */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void EndInvokedChild(FGameplayTag Reason);

	/*  Add a thought to RAIControllers thoughts for debugging (requires the controller's bTraceThoughts) */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void TraceThought(FString Thought);

	/*  Call this to indicate the task is waiting for something else, e.g. an AIMoveTo command.
	 *  If MaxWaitTime > 0 and the wait is not ended with DoneWaiting in time, the task fails with RAI.Outcome.Timeout
	 *  (its invoked descendants are interrupted first) and returns to its parent. */
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

	/* Enable or disable prioritization and request reevaluation. */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void SetTaskEnabled(bool bEnabled);

	/* Tell the manager this running task made progress (silences the dev-build "running without wait" diagnostic). */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void NotifyActivity();

	/* Not yet implemented - Whether the task should fully simulate near player or simple simulate far from player */
	UFUNCTION(BlueprintCallable, Category = RAI)
	ERAIField GetSimulationField();

	/* Get the original invoking task (null if this task was not invoked) */
	UFUNCTION(BlueprintPure, Category = RAI)
	URAITaskComponent* GetOldestInvokingAncestor() const;

	/* Root of this task's chain: the oldest ancestor, or this task. */
	UFUNCTION(BlueprintPure, Category = RAI)
	URAITaskComponent* GetRootTask() const;

	UFUNCTION(BlueprintPure, Category = RAI)
	URAITaskComponent* GetInvokingParent() const { return ParentInvokingTask; }

	UFUNCTION(BlueprintPure, Category = RAI)
	URAITaskComponent* GetInvokedChild() const { return ChildInvokedTask; }

	UFUNCTION(BlueprintPure, Category = RAI)
	const FRAITaskInvokeArguments& GetInvokeArgs() const { return InvokeArgs; }

	UFUNCTION(BlueprintPure, Category = RAI)
	ERAITaskRunState GetRunState() const { return RunState; }
	UFUNCTION(BlueprintPure, Category = RAI)
	URAIManagerComponent* GetManager() const { return ManagerComponent; }
	UFUNCTION(BlueprintPure, Category = RAI)
	ARAIController* GetOwnerController() const { return OwnerController; }
	UFUNCTION(BlueprintPure, Category = RAI)
	APawn* GetControlledPawn() const { return Pawn; }

	/* Outcome reason of this task's last end. */
	UFUNCTION(BlueprintPure, Category = RAI)
	FGameplayTag GetLastOutcome() const { return LastOutcome; }

	/* Outcome reason of the last invoked child that ended. */
	UFUNCTION(BlueprintPure, Category = RAI)
	FGameplayTag GetLastChildOutcome() const { return LastChildOutcome; }

	/* 1 for a root, 2 for its child, ... */
	UFUNCTION(BlueprintPure, Category = RAI)
	int32 GetChainDepth() const;

	/*  If is parent of Task or parent of parent of Task etc */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsAncestorOf(const URAITaskComponent* Task) const;

	/*  If is child of Task or child of child of Task etc */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsDescendantOf(const URAITaskComponent* Task) const;

	/*  False if on cooldown or loop penalty */
	UFUNCTION(BlueprintPure, Category = "RAI|ManagerInterface")
	bool IsTaskReady();

	/* Current time from the manager's time source (world time if unmanaged). */
	double GetNow() const;

	/* Scheduler owned by the manager, or null if uninitialized. */
	IRAIScheduler* GetScheduler() const;

	/*  Whether to write highly detailed debug information to the log (set from the manager) */
	bool DebugLoggingEnabled = false;

	/* Loop detection threshold (set from the manager): begins/returns within LoopCountDetectionPeriod seconds. */
	int32 MaxTaskLoopCount = 25;

	/* ONLY CALL FROM MANAGER COMPONENT */
	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	void OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus);
	
	UFUNCTION(BlueprintNativeEvent, Category = RAI)
	void OnCustomTrigger(FGameplayTag Trigger, UObject* Payload);

	void SetPriority(float NewPriority);

protected:
	/*  Last value calculated by CalculatePriority(). Use GetPriority() instead. */
	float Priority = 0.0f;

private:
	ERAITaskRunState RunState = ERAITaskRunState::Inactive;
	uint64 RunGeneration = 0;
	bool bExplicitWait = false;
	bool IsOverridingInterruptionType = false;
	bool bWarnedRunningWithoutWait = false;

	double WorldTimeBegun = -1.0;
	double WorldTimeEnd = -1.0;
	double LastActivityTime = -1.0;
	double LoopPenaltyUntil = -1.0;
	double LoopStartWorldTime = -1.0;
	int32 CurrentTaskLoopCount = 0;
	static constexpr double LoopCountDetectionPeriod = 1.0;

	FGameplayTag PendingOutcomeReason;
	FGameplayTag LastOutcome;
	FGameplayTag LastChildOutcome;

	FRAIScheduleHandle WaitTimeoutHandle;
	FRAIScheduleHandle RestartHandle;

	void OnWaitTimeout();

	/* Returns true if a loop was detected on this call. */
	bool CheckForInfLoop();

	void SetRunState(ERAITaskRunState NewState);
	void RefreshWaitingState();
	void ResetRuntimeState();
};
