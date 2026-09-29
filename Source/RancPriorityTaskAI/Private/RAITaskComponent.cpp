// Copyright Rancorous Games, 2024

#include "RAITaskComponent.h"
#include "RAIManagerComponent.h"
#include "RAIController.h"
#include "RAITags.h"
#include "RAILogCategory.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarRAILoopPenalty(TEXT("rai.LoopPenalty"), 1,
	TEXT("Apply temporary loop penalties (events remain enabled when disabled)."));

URAITaskComponent::URAITaskComponent() { PrimaryComponentTick.bCanEverTick = false; }

void URAITaskComponent::Initialize_Implementation(ACharacter* InCharacter, ARAIController* Controller)
{
	Character = InCharacter;
	OwnerController = Controller;
	InterruptType = DefaultInterruptType;
	Cooldown = FMath::Max(Cooldown, 0.f);
}

void URAITaskComponent::SetRunState(ERAITaskRunState State)
{
	RunState = State;
	IsTaskActive = State != ERAITaskRunState::Inactive;
	IsWaiting = IsTaskActive && (bExplicitWait || ChildInvokedTask != nullptr);
}

void URAITaskComponent::RefreshWaitingState()
{
	if (RunState == ERAITaskRunState::Ending) return;
	SetRunState(IsTaskActive ? (bExplicitWait || ChildInvokedTask ? ERAITaskRunState::Waiting : ERAITaskRunState::Running)
		: ERAITaskRunState::Inactive);
}

void URAITaskComponent::ResetRuntimeState()
{
	++RunGeneration;
	ParentInvokingTask = nullptr;
	ChildInvokedTask = nullptr;
	bExplicitWait = false;
	SetRunState(ERAITaskRunState::Inactive);
	InvokeArgs = {};
	WaitTimeoutHandle.Invalidate();
	RestartHandle.Invalidate();
	WorldTimeBegun = WorldTimeEnd = LastActivityTime = LoopPenaltyUntil = LoopStartWorldTime = -1.0;
	CurrentTaskLoopCount = 0;
	Priority = 0.f;
	NextBeginCooldown = 0.f;
	PendingOutcomeReason = LastOutcome = LastChildOutcome = {};
	IsOverridingInterruptionType = false;
	bWarnedRunningWithoutWait = false;
	InterruptType = DefaultInterruptType;
}

void URAITaskComponent::BeginTaskCore(const FRAITaskInvokeArguments& Arguments) { BeginTask(Arguments); }

void URAITaskComponent::BeginTask_Implementation(const FRAITaskInvokeArguments& Arguments)
{
#if !UE_BUILD_SHIPPING
	if (OwnerController && OwnerController->bTraceThoughts)
		OwnerController->TraceThought(FString(TEXT("Beginning: ")) + GetFName().ToString());
#endif
}

void URAITaskComponent::EndTask_Implementation(bool Success, float BeginAgainCooldown, bool WasInterrupted)
{
	if (!ManagerComponent || !IsTaskActive) return;
	const FGameplayTag Reason = PendingOutcomeReason.IsValid() ? PendingOutcomeReason
		: FGameplayTag(WasInterrupted ? RAITags::Outcome_Interrupted : (Success ? RAITags::Outcome_Success : RAITags::Outcome_Failure));
	PendingOutcomeReason = {};
	ManagerComponent->RequestTaskEnd(this, Success, BeginAgainCooldown, WasInterrupted, Reason);
}

void URAITaskComponent::EndTaskWithReason(bool Success, FGameplayTag Reason, float CooldownSeconds, bool Interrupted)
{
	if (!IsTaskActive || (RunState == ERAITaskRunState::Ending && !Interrupted)) return;
	if (RunState == ERAITaskRunState::Ending && ManagerComponent)
	{
		ManagerComponent->RequestTaskEnd(this, Success, CooldownSeconds, Interrupted, Reason);
		return;
	}
	PendingOutcomeReason = Reason;
	EndTask(Success, CooldownSeconds, Interrupted);
}

void URAITaskComponent::OnInvokedTaskCompletedWithReason_Implementation(bool Success, FGameplayTag Reason)
{
	OnInvokedTaskCompleted(Success);
}

bool URAITaskComponent::IsTaskReady()
{
	const double Now = GetNow();
	if (Now < LoopPenaltyUntil) return false;
	if (WorldTimeBegun < 0.0) return true;
	if (NextBeginCooldown > 0.f && WorldTimeEnd >= 0.0 && Now - WorldTimeEnd < NextBeginCooldown) return false;
	const double Reference = CooldownBasis == ERAICooldownBasis::EndToBegin ? WorldTimeEnd : WorldTimeBegun;
	return Cooldown <= 0.f || Reference < 0.0 || Now - Reference >= Cooldown;
}

void URAITaskComponent::SetPriority(float Value) { Priority = Value; }
float URAITaskComponent::CalculatePriority_Implementation() { return 0.f; }
float URAITaskComponent::GetPriority() const { return Priority; }
float URAITaskComponent::GetEffectivePriority() const { return GetRootTask()->Priority; }
double URAITaskComponent::GetNow() const
{
	return ManagerComponent ? ManagerComponent->GetNow() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}
IRAIScheduler* URAITaskComponent::GetScheduler() const { return ManagerComponent ? ManagerComponent->GetScheduler() : nullptr; }

bool URAITaskComponent::CheckForInfLoop()
{
	const double Now = GetNow();
	if (LoopStartWorldTime < 0.0 || Now - LoopStartWorldTime > LoopCountDetectionPeriod)
	{
		LoopStartWorldTime = Now;
		CurrentTaskLoopCount = 0;
	}
	if (++CurrentTaskLoopCount < FMath::Max(MaxTaskLoopCount, 1)) return false;
	CurrentTaskLoopCount = 0;
	if (ManagerComponent)
	{
		ManagerComponent->RecordTrace(ERAITraceType::LoopDetected, this, {}, static_cast<float>(MaxTaskLoopCount));
		ManagerComponent->OnLoopDetected.Broadcast(this, MaxTaskLoopCount, LoopCountDetectionPeriod);
	}
	UE_LOG(LogRAI, Error, TEXT("Task %s infinite loop detected"), *GetName());
	if (CVarRAILoopPenalty.GetValueOnGameThread() != 0)
	{
		for (URAITaskComponent* Task = this; Task; Task = Task->ParentInvokingTask)
			Task->LoopPenaltyUntil = FMath::Max(Task->LoopPenaltyUntil, Now + LoopCountDetectionPeriod);
	}
	return true;
}

void URAITaskComponent::Restart()
{
	if (!ManagerComponent || !IsTaskActive || !ManagerComponent->IsActive() || RunState == ERAITaskRunState::Ending) return;
	if (GetNow() < LoopPenaltyUntil)
	{
		if (IRAIScheduler* Scheduler = GetScheduler())
		{
			Scheduler->Cancel(RestartHandle);
			RestartHandle = Scheduler->ScheduleOnce(this, LoopPenaltyUntil - GetNow(), [WeakThis = TWeakObjectPtr<URAITaskComponent>(this)]
			{
				if (URAITaskComponent* Self = WeakThis.Get()) Self->Restart();
			});
		}
		return;
	}
	ManagerComponent->RestartTask(this);
}

bool URAITaskComponent::InvokeTask(TSubclassOf<URAITaskComponent> TaskClass, FRAITaskInvokeArguments Arguments)
{
	ERAIInvokeRejectReason Reason;
	return InvokeTaskWithResult(TaskClass, Arguments, Reason) != ERAIInvokeResult::Rejected;
}

ERAIInvokeResult URAITaskComponent::InvokeTaskWithResult(TSubclassOf<URAITaskComponent> TaskClass, FRAITaskInvokeArguments Arguments,
	ERAIInvokeRejectReason& RejectReason)
{
	if (!ManagerComponent)
	{
		RejectReason = ERAIInvokeRejectReason::NotInitialized;
		return ERAIInvokeResult::Rejected;
	}
	return ManagerComponent->InvokeTaskWithResult(TaskClass, this, Arguments, RejectReason);
}

void URAITaskComponent::EndInvokedChild(FGameplayTag Reason)
{
	if (ChildInvokedTask) ChildInvokedTask->EndTaskWithReason(false, Reason.IsValid() ? Reason : FGameplayTag(RAITags::Outcome_EndedByParent), 0.f, true);
}

void URAITaskComponent::TraceThought(FString Thought) { if (OwnerController) OwnerController->TraceThought(Thought); }
ERAIField URAITaskComponent::GetSimulationField() { return ERAIField::NearField; }

URAITaskComponent* URAITaskComponent::GetOldestInvokingAncestor() const
{
	return ParentInvokingTask ? GetRootTask() : nullptr;
}
URAITaskComponent* URAITaskComponent::GetRootTask() const
{
	const URAITaskComponent* Root = this;
	while (Root->ParentInvokingTask) Root = Root->ParentInvokingTask;
	return const_cast<URAITaskComponent*>(Root);
}
int32 URAITaskComponent::GetChainDepth() const
{
	int32 Depth = 1;
	for (const URAITaskComponent* Parent = ParentInvokingTask; Parent; Parent = Parent->ParentInvokingTask) ++Depth;
	return Depth;
}
bool URAITaskComponent::IsAncestorOf(const URAITaskComponent* Task) const
{
	for (const URAITaskComponent* Parent = Task ? Task->ParentInvokingTask : nullptr; Parent; Parent = Parent->ParentInvokingTask)
		if (Parent == this) return true;
	return false;
}
bool URAITaskComponent::IsDescendantOf(const URAITaskComponent* Task) const { return Task && Task->IsAncestorOf(this); }

void URAITaskComponent::BeginWaiting(double MaxWaitTime, bool OverrideType, ERAIInterruptionType WaitingType)
{
	if (!IsTaskActive || RunState == ERAITaskRunState::Ending) return;
	bExplicitWait = true;
	NotifyActivity();
	RefreshWaitingState();
	if (IRAIScheduler* Scheduler = GetScheduler())
	{
		Scheduler->Cancel(WaitTimeoutHandle);
		if (MaxWaitTime > 0.0)
			WaitTimeoutHandle = Scheduler->ScheduleOnce(this, MaxWaitTime, [WeakThis = TWeakObjectPtr<URAITaskComponent>(this)]
			{
				if (URAITaskComponent* Self = WeakThis.Get()) Self->OnWaitTimeout();
			});
	}
	IsOverridingInterruptionType = OverrideType;
	if (OverrideType) InterruptType = WaitingType;
}

void URAITaskComponent::DoneWaiting(ERAIInterruptionType ReturnType, EDoneWaitingExecutionStates& Branch)
{
	Branch = IsTaskActive && ManagerComponent && ManagerComponent->IsActive() && RunState != ERAITaskRunState::Ending
		? EDoneWaitingExecutionStates::Continue : EDoneWaitingExecutionStates::TaskEnded;
	if (IRAIScheduler* Scheduler = GetScheduler()) Scheduler->Cancel(WaitTimeoutHandle);
	bExplicitWait = false;
	if (IsTaskActive && IsOverridingInterruptionType) InterruptType = ReturnType;
	IsOverridingInterruptionType = false;
	NotifyActivity();
	RefreshWaitingState();
}

void URAITaskComponent::OnWaitTimeout()
{
	WaitTimeoutHandle.Invalidate();
	if (IsTaskActive && bExplicitWait)
	{
		if (ManagerComponent) ManagerComponent->RecordTrace(ERAITraceType::WaitTimeout, this, RAITags::Outcome_Timeout);
		EndTaskWithReason(false, RAITags::Outcome_Timeout);
	}
}

void URAITaskComponent::SetTaskEnabled(bool Enabled)
{
	if (IsEnabled == Enabled) return;
	IsEnabled = Enabled;
	if (ManagerComponent)
	{
		ManagerComponent->RebuildPrimaryTasks();
		ManagerComponent->RequestReevaluation(RAITags::Reevaluate_TaskEnabledChanged);
	}
}
void URAITaskComponent::NotifyActivity() { LastActivityTime = GetNow(); }
void URAITaskComponent::OnPerceptionStimulus_Implementation(AActor* Actor, FAIStimulus Stimulus) {}
void URAITaskComponent::OnCustomTrigger_Implementation(FGameplayTag Trigger, UObject* Payload) {}
