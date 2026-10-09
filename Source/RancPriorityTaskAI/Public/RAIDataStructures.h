// Copyright Rancorous Games, 2023

#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RAIDataStructures.generated.h"

class URAITaskComponent;

/*
 How reluctant a running task is to be replaced by a different, higher-priority task.
 The value is interpreted by the manager's URAIInterruptPolicy. The default policy maps each value to a
 priority gap configured on URAIManagerComponent; the candidate must exceed the active chain's effective
 priority by more than that gap:
   Always        = any higher priority (gap 0.01)
   WaitASec      = WaitASecInterruptPriorityGap
   PreferablyNot = PreferablyNotInterruptPriorityGap
   OnlyIfNeeded  = OnlyIfNeededInterruptPriorityGap
   IfPanic       = IfPanicInterruptPriorityGap
   IfLifeOrDeath = IfLifeOrDeathInterruptPriorityGap
   Never         = never replaced by arbitration. The task can still end itself, time out, or be ended by
                   InterruptIfReachesZero.
*/
UENUM(BlueprintType)
enum class ERAIInterruptionType : uint8
{
	Always,
	WaitASec,
	PreferablyNot,
	OnlyIfNeeded,
	IfPanic,
	IfLifeOrDeath,
	Never
};

UENUM(BlueprintType)
enum class ERAIField : uint8
{
	NearField,
	DistantField
	// ProbabilisticField
};

UENUM(BlueprintType)
enum class EDoneWaitingExecutionStates : uint8
{
	Continue,
	TaskEnded
};

/** Lifecycle state of a task. IsTaskActive == (RunState != Inactive). */
UENUM(BlueprintType)
enum class ERAITaskRunState : uint8
{
	Inactive,
	/** Began and is doing its own work (timers, delegates, movement...). Running is a valid resting state. */
	Running,
	/** Waiting on an explicit BeginWaiting() or on an invoked child. */
	Waiting,
	/** EndTask was requested during a manager operation; the end is queued and processed when the operation unwinds. */
	Ending
};

UENUM(BlueprintType)
enum class ERAIInvokeResult : uint8
{
	/** The child began and is still running. */
	Started,
	/** The child began and ended during its own BeginTask. The parent receives OnInvokedTaskCompleted after the invoke call has returned. */
	CompletedImmediately,
	/** Nothing changed. See ERAIInvokeRejectReason. */
	Rejected
};

UENUM(BlueprintType)
enum class ERAIInvokeRejectReason : uint8
{
	None,
	NotInitialized,
	TaskNotFound,
	InvokerNotActive,
	InvokerAlreadyHasChild,
	IsInvoker,
	IsAncestorOfInvoker,
	TargetAlreadyActive,
	MaxDepthExceeded
};

/** What Cooldown is measured from. NextBeginCooldown (EndTask's BeginAgainCooldown) is always measured from the end. */
UENUM(BlueprintType)
enum class ERAICooldownBasis : uint8
{
	/** Cooldown seconds must pass between two begins (historic behavior). */
	BeginToBegin,
	/** Cooldown seconds must pass between the end and the next begin. */
	EndToBegin
};

UENUM(BlueprintType)
enum class ERAIArbitrationDecision : uint8
{
	/** No task scored above the threshold. */
	NoCandidate,
	/** Nothing was running; the winner was started. */
	StartedIdle,
	/** The active chain keeps running (the winner is its root, or an invoked task that did not justify a takeover). */
	Continued,
	/** A different winner exists but the interrupt policy kept the active chain. */
	Blocked,
	/** The active chain was ended and the winner started as a new root. */
	Interrupted,
	/** The active chain's root dropped to or below TaskThreshold with InterruptIfReachesZero set, and was ended. */
	EndedAtThreshold,
	/** Ownership moved to an equivalent root; the invoked child chain kept running. */
	Relabeled
};

/** Presentation tone of a debug thought (see RAI_THOUGHT). */
UENUM(BlueprintType)
enum class ERAIThoughtTone : uint8
{
	Neutral,
	Good,
	Bad,
	Urgent
};

UENUM(BlueprintType)
enum class ERAITraceType : uint8
{
	TaskBegin,
	TaskEnd,
	TaskRestart,
	InvokeRejected,
	WaitTimeout,
	LoopDetected,
	Arbitration,
	ReevaluationRequested,
	InvariantViolated,
	ChainHandoff,
	/** Game-side named method appraisal (chosen and runner-up) in the TS33 journal. */
	MethodChosen
};

/** One entry in the manager's bounded trace ring. Allocation-free to record. */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAITraceRecord
{
	GENERATED_BODY()

	/** Time from the manager's time source. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	double Time = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	ERAITraceType Type = ERAITraceType::TaskBegin;

	/** Subject task (component name). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	FName Task;

	/** Related task or class: the parent for begin/end/reject, the previous root for arbitration. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	FName Other;

	/** Priority for arbitration, loop count for loop detection. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	float Value = 0.f;

	/** Outcome reason for task ends, reevaluation reason for reevaluation requests. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	FGameplayTag Tag;

	/** ERAIArbitrationDecision for arbitration, ERAIInvokeRejectReason for rejections. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	uint8 Code = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Trace")
	bool bInterrupted = false;

	bool operator==(const FRAITraceRecord& Rhs) const
	{
		return Time == Rhs.Time && Type == Rhs.Type && Task == Rhs.Task && Other == Rhs.Other
			&& Value == Rhs.Value && Tag == Rhs.Tag && Code == Rhs.Code
			&& bSuccess == Rhs.bSuccess && bInterrupted == Rhs.bInterrupted;
	}
	bool operator!=(const FRAITraceRecord& Rhs) const { return !(*this == Rhs); }
};

/** Allocation-free native trace sink. The caller owns the sink and must detach it before destruction. */
class RANCPRIORITYTASKAI_API IRAITraceSink
{
public:
	virtual ~IRAITraceSink() = default;
	virtual void OnRAITraceRecord(const UObject* Source, const FRAITraceRecord& Record) = 0;
};

/** A named contribution to a task's priority, for debugging (R8). */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAIPriorityTerm
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	FName Name;

	/** Optional semantic explanation group. The plugin groups terms but never caps/saturates them. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	FName Group;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	float Value = 0.f;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAIPriorityExplanation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TArray<FRAIPriorityTerm> Terms;

	void Add(FName Name, float Value, FName Group = NAME_None)
	{
		FRAIPriorityTerm& Term = Terms.AddDefaulted_GetRef();
		Term.Name = Name;
		Term.Group = Group;
		Term.Value = Value;
	}
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAIArbitrationCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TObjectPtr<URAITaskComponent> Task = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	float Priority = 0.f;

	/** False when on cooldown or loop penalty. Active tasks are always ready. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	bool bReady = false;

	/** None when the task was eligible; otherwise Cooldown or BelowThreshold. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	FName ExcludedReason;

	/** Filled only when the manager's bCaptureExplanations is set (development builds). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	FRAIPriorityExplanation Explanation;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAIArbitrationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	double Time = 0.0;

	/** Enabled primary tasks, highest priority first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TArray<FRAIArbitrationCandidate> Candidates;

	/** Highest-priority eligible task, or null. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TObjectPtr<URAITaskComponent> Winner = nullptr;

	/** Root of the active chain before this arbitration. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TObjectPtr<URAITaskComponent> PreviousRoot = nullptr;

	/** Root of the active chain after this arbitration. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	TObjectPtr<URAITaskComponent> ResultingRoot = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "RAI|Arbitration")
	ERAIArbitrationDecision Decision = ERAIArbitrationDecision::NoCandidate;
};
