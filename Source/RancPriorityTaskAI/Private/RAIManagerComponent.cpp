// Copyright Rancorous Games, 2023

#include "RAIManagerComponent.h"
#include "RAITaskComponent.h"
#include "RAIController.h"
#include "RAIInterruptPolicy.h"
#include "RAITags.h"
#include "RAILogCategory.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"

URAIManagerComponent::URAIManagerComponent() { PrimaryComponentTick.bCanEverTick = false; }
void URAIManagerComponent::BeginPlay() { Super::BeginPlay(); }
void URAIManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Deinitialize();
	Super::EndPlay(Reason);
}

void URAIManagerComponent::SetServices(TSharedPtr<IRAITimeSource> Time, TSharedPtr<IRAIScheduler> InScheduler)
{
	// Handles must be cancelled through their original scheduler.
	Deinitialize();
	TimeSource = MoveTemp(Time);
	Scheduler = MoveTemp(InScheduler);
}

double URAIManagerComponent::GetNow() const
{
	return TimeSource ? TimeSource->Now() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void URAIManagerComponent::Initialize(ARAIController* Controller, APawn* InPawn)
{
	if (bDeinitializing || !Controller || !InPawn) return;
	if (OwningController == Controller && Pawn == InPawn) return;
	Deinitialize();
	OwningController = Controller;
	Pawn = InPawn;
	Character = Cast<ACharacter>(InPawn);
	if (!TimeSource) TimeSource = MakeShared<FRAIWorldTimeSource>(GetWorld());
	if (!Scheduler) Scheduler = MakeShared<FRAITimerManagerScheduler>(GetWorld());
	if (!InterruptPolicy) InterruptPolicy = NewObject<URAIInterruptPolicy>(this);
	Controller->GetComponents<URAITaskComponent>(AllTasks, false);
	TaskByClass.Reset();
	TraceRing.Reset();
	TraceRing.Reserve(FMath::Max(TraceCapacity, 0));
	TraceWriteIndex = 0;
	bHasArbitration = false;
	for (URAITaskComponent* Task : AllTasks)
	{
		Task->ResetRuntimeState();
		Task->ManagerComponent = this;
		Task->OwnerController = Controller;
		Task->Pawn = InPawn;
		Task->Character = Character;
		Task->DebugLoggingEnabled = DebugLoggingEnabled;
		Task->MaxTaskLoopCount = MaxTaskLoopCount;
		TaskByClass.FindOrAdd(Task->GetClass(), Task);
	}
	RebuildPrimaryTasks();
	const TArray<URAITaskComponent*> Tasks = AllTasks;
	for (URAITaskComponent* Task : Tasks)
	{
		if (Task->ManagerComponent != this) break;
		Task->Initialize(Character, Controller);
	}
	SetActive(Controller->IsRAIActive());
	ValidateInvariants();
}

void URAIManagerComponent::Deinitialize()
{
	if (bDeinitializing) return;
	bDeinitializing = true;
	++LifecycleGeneration;
	ON_SCOPE_EXIT { bDeinitializing = false; };
	if (ActiveTask) ActiveTask->GetRootTask()->EndTaskWithReason(false, RAITags::Outcome_Deinitialized, 0.f, true);
	const TArray<URAITaskComponent*> Tasks = AllTasks;
	for (URAITaskComponent* Task : Tasks)
	{
		if (!Task) continue;
		if (Task->IsTaskActive) Task->EndTaskWithReason(false, RAITags::Outcome_Deinitialized, 0.f, true);
		if (Scheduler) Scheduler->CancelAll(Task);
		// Consumer timers and movement delegates also belong to the old possession.
		if (GetWorld()) GetWorld()->GetTimerManager().ClearAllTimersForObject(Task);
		if (OwningController && OwningController->GetPathFollowingComponent())
			OwningController->GetPathFollowingComponent()->OnRequestFinished.RemoveAll(Task);
		Task->ResetRuntimeState();
		Task->ManagerComponent = nullptr;
		Task->OwnerController = nullptr;
		Task->Pawn = nullptr;
		Task->Character = nullptr;
	}
	if (Scheduler) Scheduler->CancelAll(this);
	DeferredDrain.Invalidate();
	PendingEnds.Reset();
	ActiveTask = nullptr;
	OwningController = nullptr;
	Pawn = nullptr;
	Character = nullptr;
	ControllerFocus = nullptr;
	AllTasks.Reset();
	PrimaryTasks.Reset();
	TaskByClass.Reset();
	bDirty = false;
}

void URAIManagerComponent::Deactivate()
{
	Super::Deactivate();
	if (ActiveTask) ActiveTask->GetRootTask()->EndTaskWithReason(false, RAITags::Outcome_Deactivated, 0.f, true);
	if (Scheduler) Scheduler->CancelAll(this);
	DeferredDrain.Invalidate();
	DrainEnds();
}

void URAIManagerComponent::RebuildPrimaryTasks()
{
	PrimaryTasks.Reset();
	for (URAITaskComponent* Task : AllTasks)
		if (Task && Task->IsPrimaryTask && Task->IsEnabled) PrimaryTasks.Add(Task);
}

URAITaskComponent* URAIManagerComponent::GetTaskByClass(TSubclassOf<URAITaskComponent> TaskClass) const
{
	if (!TaskClass) return nullptr;
	if (URAITaskComponent* const* Found = TaskByClass.Find(TaskClass.Get())) return *Found;
	for (URAITaskComponent* Task : AllTasks)
	{
		if (Task && Task->IsA(TaskClass))
		{
			TaskByClass.Add(TaskClass.Get(), Task);
			return Task;
		}
	}
	TaskByClass.Add(TaskClass.Get(), nullptr);
	return nullptr;
}

void URAIManagerComponent::RequestReevaluation(FGameplayTag Reason)
{
	bDirty = true;
	RecordTrace(ERAITraceType::ReevaluationRequested, nullptr, Reason);
	OnReevaluationRequested.Broadcast(Reason);
}

void URAIManagerComponent::LeaveOperation(bool DeferDrain)
{
	check(OperationDepth > 0);
	if (--OperationDepth == 0)
	{
		if (DeferDrain) ScheduleDrain();
		else DrainEnds();
		ValidateInvariants();
	}
}

void URAIManagerComponent::ScheduleDrain()
{
	if (PendingEnds.IsEmpty() || DeferredDrain.IsValid() || !Scheduler) return;
	DeferredDrain = Scheduler->ScheduleOnce(this, 0.0, [WeakThis = TWeakObjectPtr<URAIManagerComponent>(this)]
	{
		if (URAIManagerComponent* Self = WeakThis.Get())
		{
			Self->DeferredDrain.Invalidate();
			Self->DrainEnds();
			Self->ValidateInvariants();
		}
	});
}

void URAIManagerComponent::DrainEnds()
{
	if (OperationDepth || bDrainingEnds) return;
	bDrainingEnds = true;
	ON_SCOPE_EXIT { bDrainingEnds = false; };
	int32 Count = 0;
	// Bound work when completions synchronously create and finish more children.
	while (!PendingEnds.IsEmpty() && Count++ < FMath::Max(MaxTaskLoopCount, 1) * FMath::Max(AllTasks.Num(), 1))
	{
		const FPendingEnd End = PendingEnds[0];
		PendingEnds.RemoveAt(0, EAllowShrinking::No);
		FinishEnd(End);
	}
	ScheduleDrain();
}

void URAIManagerComponent::RequestTaskEnd(URAITaskComponent* Task, bool Success, float Cooldown, bool Interrupted, FGameplayTag Reason)
{
	if (!Task || Task->ManagerComponent != this || !Task->IsTaskActive) return;
	if (Task->RunState == ERAITaskRunState::Ending && !Interrupted) return;
	Task->SetRunState(ERAITaskRunState::Ending);
	if (Scheduler) Scheduler->CancelAll(Task);
	Task->WaitTimeoutHandle.Invalidate();
	Task->RestartHandle.Invalidate();
	const FPendingEnd End{Task, Success, FMath::Max(Cooldown, 0.f), Interrupted, Reason, Task->RunGeneration};
	if (!Interrupted && (OperationDepth || bDrainingEnds)) PendingEnds.Add(End);
	else
	{
		++OperationDepth;
		FinishEnd(End);
		LeaveOperation();
	}
}

void URAIManagerComponent::FinishEnd(const FPendingEnd& End)
{
	URAITaskComponent* Task = End.Task.Get();
	if (!Task || Task->ManagerComponent != this || !Task->IsTaskActive || Task->RunGeneration != End.Generation) return;
	if (URAITaskComponent* Child = Task->ChildInvokedTask)
		Child->EndTaskWithReason(false, RAITags::Outcome_ParentEnded, 0.f, true);
	if (Task->ManagerComponent != this || !Task->IsTaskActive || Task->RunGeneration != End.Generation) return;
	URAITaskComponent* Parent = Task->ParentInvokingTask;
	const uint64 ParentGeneration = Parent ? Parent->RunGeneration : 0;
	Task->WorldTimeEnd = GetNow();
	Task->NextBeginCooldown = End.Cooldown;
	Task->LastOutcome = End.Reason;
	Task->PendingOutcomeReason = {};
	Task->SetRunState(ERAITaskRunState::Inactive);
	Task->bExplicitWait = false;
	Task->IsWaiting = false;
	Task->InterruptType = Task->DefaultInterruptType;
	Task->IsOverridingInterruptionType = false;
	Task->ParentInvokingTask = nullptr;
	Task->ChildInvokedTask = nullptr;
	Task->InvokeArgs = {};
	if (Parent && Parent->ChildInvokedTask == Task)
	{
		Parent->ChildInvokedTask = nullptr;
		Parent->RefreshWaitingState();
	}
	if (ActiveTask == Task) ActiveTask = Parent && Parent->IsTaskActive ? Parent : nullptr;
	RecordTrace(ERAITraceType::TaskEnd, Task, End.Reason, 0.f, Parent, 0, End.Success, End.Interrupted);
	OnAnyTaskExit.Broadcast(Task);
	OnTaskEnd.Broadcast(Task, End.Success, End.Reason, End.Interrupted);
	OnTaskEndNative.Broadcast(Task, End.Success, End.Reason, End.Interrupted);
	// Exit listeners may detach the controller or replace the parent's work.
	if (!End.Interrupted && Parent && Parent->ManagerComponent == this && Parent->IsTaskActive
		&& Parent->RunGeneration == ParentGeneration && Parent->RunState != ERAITaskRunState::Ending && !Parent->ChildInvokedTask)
	{
		Parent->LastChildOutcome = End.Reason;
		Parent->NotifyActivity();
		Parent->CheckForInfLoop();
		Parent->OnInvokedTaskCompletedWithReason(End.Success, End.Reason);
	}
	if (!ActiveTask && !bDeinitializing) RequestReevaluation(RAITags::Reevaluate_ChainEnded);
}

void URAIManagerComponent::StartTask(URAITaskComponent* Task, FRAITaskInvokeArguments Arguments)
{
	if (!Task || Task->ManagerComponent != this || !IsActive() || bDeinitializing) return;
	ActiveTask = Task;
	Task->InvokeArgs = Arguments;
	Task->WorldTimeBegun = GetNow();
	Task->LastActivityTime = Task->WorldTimeBegun;
	Task->NextBeginCooldown = 0.f;
	Task->bExplicitWait = false;
	Task->bWarnedRunningWithoutWait = false;
	Task->SetRunState(ERAITaskRunState::Running);
	const uint64 Generation = ++Task->RunGeneration;
	RecordTrace(ERAITraceType::TaskBegin, Task, {}, 0.f, Task->ParentInvokingTask);
	OnAnyTaskEnter.Broadcast(Task);
	OnTaskBegin.Broadcast(Task);
	OnTaskBeginNative.Broadcast(Task);
	if (Task->ManagerComponent == this && Task->RunGeneration == Generation && Task->RunState == ERAITaskRunState::Running)
	{
		Task->CheckForInfLoop();
		Task->BeginTaskCore(Arguments);
	}
}

bool URAIManagerComponent::InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent* Parent, FRAITaskInvokeArguments& Arguments)
{
	ERAIInvokeRejectReason Reason;
	return InvokeTaskWithResult(TaskClass, Parent, Arguments, Reason) != ERAIInvokeResult::Rejected;
}

ERAIInvokeResult URAIManagerComponent::InvokeTaskWithResult(TSubclassOf<URAITaskComponent> TaskClass, URAITaskComponent* Parent,
	const FRAITaskInvokeArguments& Arguments, ERAIInvokeRejectReason& RejectReason)
{
	RejectReason = ERAIInvokeRejectReason::None;
	URAITaskComponent* Target = GetTaskByClass(TaskClass);
	if (!OwningController || !IsActive() || bDeinitializing) RejectReason = ERAIInvokeRejectReason::NotInitialized;
	else if (!Parent || Parent->ManagerComponent != this || !Parent->IsTaskActive || Parent->RunState == ERAITaskRunState::Ending)
		RejectReason = ERAIInvokeRejectReason::InvokerNotActive;
	else if (!Target) RejectReason = ERAIInvokeRejectReason::TaskNotFound;
	else if (Target == Parent) RejectReason = ERAIInvokeRejectReason::IsInvoker;
	else if (Target->IsAncestorOf(Parent)) RejectReason = ERAIInvokeRejectReason::IsAncestorOfInvoker;
	else if (Target->IsTaskActive) RejectReason = ERAIInvokeRejectReason::TargetAlreadyActive;
	else if (Parent->ChildInvokedTask) RejectReason = ERAIInvokeRejectReason::InvokerAlreadyHasChild;
	else if (Parent != ActiveTask) RejectReason = ERAIInvokeRejectReason::InvokerNotActive;
	else if (Parent->GetChainDepth() >= MaxInvokeDepth) RejectReason = ERAIInvokeRejectReason::MaxDepthExceeded;
	if (RejectReason != ERAIInvokeRejectReason::None)
	{
		RecordTrace(ERAITraceType::InvokeRejected, Parent, {}, 0.f, Target, static_cast<uint8>(RejectReason));
		OnInvokeRejected.Broadcast(Parent, TaskClass, RejectReason);
		ValidateInvariants();
		return ERAIInvokeResult::Rejected;
	}
	const bool Outermost = OperationDepth == 0 && !bDrainingEnds;
	++OperationDepth;
	Target->ParentInvokingTask = Parent;
	Parent->ChildInvokedTask = Target;
	Parent->RefreshWaitingState();
	StartTask(Target, Arguments);
	const ERAIInvokeResult Result = !Target->IsTaskActive || Target->RunState == ERAITaskRunState::Ending
		? ERAIInvokeResult::CompletedImmediately : ERAIInvokeResult::Started;
	LeaveOperation(Outermost);
	return Result;
}

void URAIManagerComponent::RestartTask(URAITaskComponent* Task)
{
	if (!Task || !Task->IsTaskActive || Task->ManagerComponent != this || !IsActive() || Task->ChildInvokedTask
		|| Task->RunState == ERAITaskRunState::Ending) return;
	const bool Outermost = OperationDepth == 0;
	++OperationDepth;
	if (Scheduler) Scheduler->CancelAll(Task);
	Task->WaitTimeoutHandle.Invalidate();
	Task->RestartHandle.Invalidate();
	Task->bExplicitWait = false;
	Task->SetRunState(ERAITaskRunState::Running);
	Task->WorldTimeBegun = GetNow();
	Task->NotifyActivity();
	Task->CheckForInfLoop();
	RecordTrace(ERAITraceType::TaskRestart, Task);
	const FRAITaskInvokeArguments Arguments = Task->InvokeArgs;
	Task->BeginTaskCore(Arguments);
	LeaveOperation(Outermost);
}

void URAIManagerComponent::UpdateActiveTasks()
{
	if (!IsActive() || !OwningController || !Pawn || !OwningController->HasAuthority() || bDeinitializing || bUpdating) return;
	bUpdating = true;
	++OperationDepth;
	ON_SCOPE_EXIT { bUpdating = false; LeaveOperation(); };
	bDirty = false;
	const uint64 Generation = LifecycleGeneration;
	FRAIArbitrationResult Result;
	Result.Time = GetNow();
	Result.PreviousRoot = ActiveTask ? ActiveTask->GetRootTask() : nullptr;
	float BestScore = TaskThreshold;
	const bool Capture = OnArbitration.IsBound() || OnArbitrationNative.IsBound() || bForceArbitrationEvent || bCaptureExplanations;
	TArray<URAITaskComponent*, TInlineAllocator<32>> Tasks;
	Tasks.Append(PrimaryTasks);
	for (URAITaskComponent* Task : Tasks)
	{
		if (!Task || Task->ManagerComponent != this || !Task->IsEnabled) continue;
		const float Score = Task->CalculatePriority();
		if (LifecycleGeneration != Generation || Task->ManagerComponent != this || !OwningController || !IsActive()) return;
		Task->SetPriority(Score);
		const bool Ready = Task->IsTaskActive || Task->IsTaskReady();
		if (Capture)
		{
			FRAIArbitrationCandidate& Candidate = Result.Candidates.AddDefaulted_GetRef();
			Candidate.Task = Task; Candidate.Priority = Score; Candidate.bReady = Ready;
			Candidate.ExcludedReason = !Ready ? FName(TEXT("Cooldown")) : (Score <= TaskThreshold ? FName(TEXT("BelowThreshold")) : NAME_None);
#if !UE_BUILD_SHIPPING
			if (bCaptureExplanations) Task->DescribePriority(Candidate.Explanation);
#endif
		}
		if (Ready && Score > BestScore) { BestScore = Score; Result.Winner = Task; }
	}
	URAITaskComponent* Root = ActiveTask ? ActiveTask->GetRootTask() : nullptr;
	if (Root && Root->InterruptIfReachesZero && Root->GetPriority() <= TaskThreshold)
	{
		Root->EndTaskWithReason(false, RAITags::Outcome_PriorityZero, 0.f, true);
		Result.Decision = ERAIArbitrationDecision::EndedAtThreshold;
	}
	if (!OwningController || !IsActive()) return;
	URAITaskComponent* Winner = Result.Winner;
	Root = ActiveTask ? ActiveTask->GetRootTask() : nullptr;
	if (Winner)
	{
		if (!ActiveTask)
		{
			StartTask(Winner);
			if (Result.Decision != ERAIArbitrationDecision::EndedAtThreshold) Result.Decision = ERAIArbitrationDecision::StartedIdle;
		}
		else if (Winner == Root) Result.Decision = ERAIArbitrationDecision::Continued;
		else if (CheckIfTaskShouldInterrupt(ActiveTask, Winner))
		{
			Root->EndTaskWithReason(false, RAITags::Outcome_Replaced, 0.f, true);
			if (OwningController) OwningController->StopMovement();
			if (!ActiveTask && Winner->ManagerComponent == this) StartTask(Winner);
			Result.Decision = ERAIArbitrationDecision::Interrupted;
		}
		else Result.Decision = Winner->IsTaskActive ? ERAIArbitrationDecision::Continued : ERAIArbitrationDecision::Blocked;
	}
	Result.ResultingRoot = ActiveTask ? ActiveTask->GetRootTask() : nullptr;
	const bool Changed = !bHasArbitration || LastWinner.Get() != Winner || LastRoot.Get() != Result.ResultingRoot
		|| LastDecision != Result.Decision || bForceArbitrationEvent;
	bHasArbitration = true; bForceArbitrationEvent = false;
	LastWinner = Winner; LastRoot = Result.ResultingRoot; LastDecision = Result.Decision;
	if (Changed)
	{
		RecordTrace(ERAITraceType::Arbitration, Winner, {}, BestScore, Result.PreviousRoot, static_cast<uint8>(Result.Decision));
		Result.Candidates.StableSort([](const FRAIArbitrationCandidate& A, const FRAIArbitrationCandidate& B) { return A.Priority > B.Priority; });
		OnArbitration.Broadcast(Result);
		OnArbitrationNative.Broadcast(Result);
	}
#if !UE_BUILD_SHIPPING
	if (ActiveTask && ActiveTask->RunState == ERAITaskRunState::Running && !ActiveTask->bWarnedRunningWithoutWait
		&& RunningDiagnosticSeconds > 0 && GetNow() - ActiveTask->LastActivityTime > RunningDiagnosticSeconds)
	{
		ActiveTask->bWarnedRunningWithoutWait = true;
		UE_LOG(LogRAI, Warning, TEXT("Task %s is running without reported activity; call NotifyActivity for ongoing work."), *ActiveTask->GetName());
	}
#endif
	if (bLegacyReinvokeIfNotWaiting && ActiveTask && ActiveTask->RunState == ERAITaskRunState::Running)
		RestartTask(ActiveTask);
}

bool URAIManagerComponent::CheckIfTaskShouldInterrupt(const URAITaskComponent* Active, const URAITaskComponent* Candidate) const
{
	if (!InterruptPolicy || !Active || !Candidate) return false;
	FRAIArbitrationContext Context;
	Context.Manager = this; Context.Now = GetNow(); Context.ActiveRoot = Active->GetRootTask();
	Context.ActivePriority = Active->GetEffectivePriority(); Context.CandidatePriority = Candidate->GetPriority();
	Context.bCandidateInActiveChain = Candidate->IsTaskActive;
	return InterruptPolicy->ShouldInterrupt(Context, Active, Candidate);
}

void URAIManagerComponent::ForceInterruptActiveTask(URAITaskComponent* Task)
{
	if (Task && Task->ManagerComponent == this && Task->IsTaskActive)
		Task->EndTaskWithReason(false, RAITags::Outcome_Forced);
	ValidateInvariants();
}

void URAIManagerComponent::TriggerCustomEvent(FGameplayTag Trigger, UObject* Payload)
{
	++OperationDepth;
	const TArray<URAITaskComponent*> Tasks = AllTasks;
	for (URAITaskComponent* Task : Tasks)
		if (Task && Task->ManagerComponent == this && Task->IsEnabled) Task->OnCustomTrigger(Trigger, Payload);
	LeaveOperation();
}

void URAIManagerComponent::OnPerceptionStimulus(AActor* Actor, FAIStimulus Stimulus)
{
	++OperationDepth;
	const TArray<URAITaskComponent*> Tasks = AllTasks;
	for (URAITaskComponent* Task : Tasks)
		if (Task && Task->ManagerComponent == this && Task->IsEnabled) Task->OnPerceptionStimulus(Actor, Stimulus);
	LeaveOperation();
}

void URAIManagerComponent::TaskEnded(URAITaskComponent* Task)
{
	if (Task && Task->IsTaskActive) RequestTaskEnd(Task, true, 0.f, false, RAITags::Outcome_Success);
}

void URAIManagerComponent::ReturnToInvokingTask(URAITaskComponent* CompletedTask, URAITaskComponent* ParentTask, bool Success)
{
	if (ParentTask && ParentTask->ManagerComponent == this && ParentTask->IsTaskActive && !ParentTask->ChildInvokedTask)
	{
		ActiveTask = ParentTask;
		ParentTask->OnInvokedTaskCompletedWithReason(Success, CompletedTask ? CompletedTask->GetLastOutcome() : FGameplayTag());
	}
}

void URAIManagerComponent::RecordTrace(ERAITraceType Type, const URAITaskComponent* Task, FGameplayTag Reason,
	float Value, const URAITaskComponent* Other, uint8 Code, bool Success, bool Interrupted)
{
	if (TraceCapacity <= 0) return;
	FRAITraceRecord Entry;
	Entry.Time = GetNow(); Entry.Type = Type; Entry.Task = Task ? Task->GetFName() : NAME_None;
	Entry.Other = Other ? Other->GetFName() : NAME_None; Entry.Tag = Reason; Entry.Value = Value;
	Entry.Code = Code; Entry.bSuccess = Success; Entry.bInterrupted = Interrupted;
	if (TraceRing.Num() < TraceCapacity) TraceRing.Add(Entry);
	else { TraceRing[TraceWriteIndex] = Entry; TraceWriteIndex = (TraceWriteIndex + 1) % TraceRing.Num(); }
}

TArray<FRAITraceRecord> URAIManagerComponent::GetTrace() const
{
	TArray<FRAITraceRecord> Result;
	Result.Reserve(TraceRing.Num());
	for (int32 Index = 0; Index < TraceRing.Num(); ++Index) Result.Add(TraceRing[(TraceWriteIndex + Index) % TraceRing.Num()]);
	return Result;
}

bool URAIManagerComponent::ValidateInvariants() const
{
#if !UE_BUILD_SHIPPING
	if (!bCheckInvariants || bDeinitializing || OperationDepth) return true;
	TArray<const URAITaskComponent*, TInlineAllocator<8>> Chain;
	const URAITaskComponent* Cursor = ActiveTask;
	const URAITaskComponent* Child = nullptr;
	bool Valid = true;
	while (Cursor)
	{
		if (Chain.Contains(Cursor)) { Valid = false; break; }
		Chain.Add(Cursor);
		Valid &= Cursor->IsTaskActive && Cursor->ChildInvokedTask == Child;
		Valid &= !Cursor->IsWaiting || Cursor->bExplicitWait || Cursor->ChildInvokedTask;
		Child = Cursor; Cursor = Cursor->ParentInvokingTask;
	}
	for (const URAITaskComponent* Task : AllTasks)
	{
		if (!Task) continue;
		Valid &= Task->IsTaskActive == Chain.Contains(Task);
		Valid &= Task->IsTaskActive == (Task->RunState != ERAITaskRunState::Inactive);
		if (!Task->IsTaskActive) Valid &= !Task->ParentInvokingTask && !Task->ChildInvokedTask && !Task->IsWaiting;
	}
	if (!Valid)
	{
		UE_LOG(LogRAI, Error, TEXT("Task chain invariant violated"));
		URAIManagerComponent* Self = const_cast<URAIManagerComponent*>(this);
		Self->RecordTrace(ERAITraceType::InvariantViolated, ActiveTask);
		Self->OnInvariantViolated.Broadcast(TEXT("Task chain invariant violated"));
	}
	return Valid;
#else
	return true;
#endif
}
