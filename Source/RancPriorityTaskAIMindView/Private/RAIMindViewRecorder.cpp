// Copyright Rancorous Games, 2026

#include "RAIMindViewRecorder.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Mind/RAIMemoryComponent.h"
#include "RAIManagerComponent.h"
#include "RAIMindViewHooks.h"
#include "RAITags.h"
#include "RAITaskComponent.h"

namespace
{
TArray<FRAIPriorityTerm> TermsOf(const FRAIArbitrationResult& Result, const URAITaskComponent* Task, float& OutPriority)
{
	for (const FRAIArbitrationCandidate& Candidate : Result.Candidates)
	{
		if (Candidate.Task == Task)
		{
			OutPriority = Candidate.Priority;
			return Candidate.Explanation.Terms;
		}
	}
	OutPriority = Task ? Task->GetPriority() : 0.f;
	return {};
}

FName MemoryChangeName(const FRAIMemoryChange& Change)
{
	switch (Change.Kind)
	{
	case ERAIMemoryChange::Encoded: return TEXT("Added");
	case ERAIMemoryChange::Consolidated: return TEXT("Consolidated");
	case ERAIMemoryChange::Forgotten: return TEXT("Removed");
	case ERAIMemoryChange::FactLearned: return Change.OldConfidence < 0.f ? FName(TEXT("FactLearned")) : FName(TEXT("Changed"));
	}
	return NAME_None;
}
}

void URAIMindViewRecorder::Start(AActor* InSubject)
{
	Subject = InSubject;
	bRunning = true;
}

void URAIMindViewRecorder::Stop()
{
	Unbind();
	bRunning = false;
	Manager.Reset();
	Memory.Reset();
	StateRevisionProvider = nullptr;
}

void URAIMindViewRecorder::Refresh(URAIManagerComponent* InManager, URAIMemoryComponent* InMemory)
{
	if (!bRunning || (Manager.Get() == InManager && Memory.Get() == InMemory)) return;
	Unbind();
	Manager = InManager;
	Memory = InMemory;
	CurrentChain.Reset();
	Bind();
}

void URAIMindViewRecorder::Bind()
{
	if (URAIManagerComponent* M = Manager.Get())
	{
		M->AddExplanationDemand();
		ArbitrationHandle = M->OnArbitrationNative.AddUObject(this, &URAIMindViewRecorder::HandleArbitration);
		BeginHandle = M->OnTaskBeginNative.AddUObject(this, &URAIMindViewRecorder::HandleTaskBegin);
		EndHandle = M->OnTaskEndNative.AddUObject(this, &URAIMindViewRecorder::HandleTaskEnd);
		M->OnLoopDetected.AddUniqueDynamic(this, &URAIMindViewRecorder::HandleManagerLoop);
		if (const URAITaskComponent* Active = M->GetActiveTask()) { ChainOf(Active, CurrentChain); LastFullChain = CurrentChain; }
	}
	if (URAIMemoryComponent* Mem = Memory.Get())
		MemoryHandle = Mem->OnMemoryChangedNative.AddUObject(this, &URAIMindViewRecorder::HandleMemoryChanged);
}

void URAIMindViewRecorder::Unbind()
{
	// Unbind from the objects we bound to, even if they are being destroyed.
	if (URAIManagerComponent* M = Manager.Get())
	{
		M->RemoveExplanationDemand();
		M->OnArbitrationNative.Remove(ArbitrationHandle);
		M->OnTaskBeginNative.Remove(BeginHandle);
		M->OnTaskEndNative.Remove(EndHandle);
		M->OnLoopDetected.RemoveDynamic(this, &URAIMindViewRecorder::HandleManagerLoop);
	}
	if (URAIMemoryComponent* Mem = Memory.Get()) Mem->OnMemoryChangedNative.Remove(MemoryHandle);
	ArbitrationHandle.Reset(); BeginHandle.Reset(); EndHandle.Reset(); MemoryHandle.Reset();
}

double URAIMindViewRecorder::Now() const
{
	if (const URAIManagerComponent* M = Manager.Get()) return M->GetNow();
	if (const URAIMemoryComponent* Mem = Memory.Get()) return Mem->GetNow();
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void URAIMindViewRecorder::AddThought(const FRAIThoughtArgs& Args, const FString* LegacyText, double InNow)
{
	FRAIThought Thought;
	Thought.Time = InNow;
	Thought.Kind = Args.Kind;
	Thought.Tone = Args.Tone;
	Thought.A = const_cast<AActor*>(Args.A);
	Thought.B = const_cast<AActor*>(Args.B);
	Thought.V0 = Args.V0;
	Thought.V1 = Args.V1;
	Thought.P = Args.P;
	if (LegacyText) Thought.Legacy = *LegacyText;
	Thoughts.Push(MoveTemp(Thought));
	++ThoughtSerial;
}

void URAIMindViewRecorder::HandleArbitration(const FRAIArbitrationResult& Result)
{
	LastDecision = Result.Decision;
	LastActiveRootPriority = Result.PreviousRoot ? Result.PreviousRoot->GetPriority() : 0.f;
	++ArbitrationSerial;

	const URAITaskComponent* To = Result.ResultingRoot;
	const URAITaskComponent* From = Result.PreviousRoot;
	ON_SCOPE_EXIT
	{
		if (!PendingRootChain.IsEmpty()) { LastFullChain = MoveTemp(PendingRootChain); PendingRootChain.Reset(); }
	};
	if (!To || To == From) return;

	FRAIMindViewSwitch Switch;
	Switch.At = Result.Time;
	Switch.From = From ? From->GetFName() : NAME_None;
	Switch.To = To->GetFName();
	Switch.Decision = Result.Decision;
	Switch.ToTerms = TermsOf(Result, To, Switch.ToPriority);
	if (From) Switch.FromTerms = TermsOf(Result, From, Switch.FromPriority);
	if (From) Switch.PreviousChain = LastFullChain;
	Switches.Push(MoveTemp(Switch));
	++SwitchSerial;

	// Purpose switches are narrated automatically so tasks never have to author them.
	FRAIThoughtArgs Args(RAITags::Thought_Switch, ERAIThoughtTone::Neutral, nullptr, nullptr, Switches.FromNewest(0).ToPriority,
		Switches.FromNewest(0).FromPriority, Switches.FromNewest(0).To);
	AddThought(Args, nullptr, Result.Time);
}

void URAIMindViewRecorder::HandleTaskBegin(URAITaskComponent* Task)
{
	if (!Task) return;
	FRAIMindViewLifecycleEvent Event;
	Event.At = Now();
	Event.Task = Task->GetFName();
	Event.bBegin = true;
	Event.Depth = Task->GetChainDepth();
	Lifecycle.Push(MoveTemp(Event));
	++LifecycleSerial;
	ChainOf(Task, CurrentChain);
	// A root begins inside arbitration, before the arbitration event: keep the previous chain until the switch is recorded.
	if (CurrentChain.Num() == 1) PendingRootChain = CurrentChain;
	else LastFullChain = CurrentChain;
	CheckRepeat(CurrentChain);
}

void URAIMindViewRecorder::HandleTaskEnd(URAITaskComponent* Task, bool bSuccess, FGameplayTag Reason, bool bInterrupted)
{
	if (!Task) return;
	// The manager clears chain links before broadcasting an end, so locate the task in the recorded chain by name.
	const int32 Index = CurrentChain.Find(Task->GetFName());
	FRAIMindViewLifecycleEvent Event;
	Event.At = Now();
	Event.Task = Task->GetFName();
	Event.bSuccess = bSuccess;
	Event.bInterrupted = bInterrupted;
	Event.Reason = Reason;
	Event.Depth = Index == INDEX_NONE ? 0 : Index + 1;
	Lifecycle.Push(MoveTemp(Event));
	++LifecycleSerial;
	if (Index != INDEX_NONE) CurrentChain.SetNum(Index);
}

void URAIMindViewRecorder::HandleMemoryChanged(const FRAIMemoryChange& Change)
{
	FRAIMindViewMemoryChange Entry;
	Entry.At = Now();
	Entry.Kind = MemoryChangeName(Change);
	Entry.Count = Change.Count;
	Entry.Reason = Change.Reason;
	for (const FRAIMemoryEpisodeRef& Ref : Change.Episodes)
	{
		FRAIMindViewMemoryItem& Item = Entry.Items.AddDefaulted_GetRef();
		Item.Id = Ref.Id;
		Item.OriginId = Ref.OriginId;
		Item.Kind = Ref.Kind;
		Item.Action = Ref.Action;
		Item.Actor = Ref.Actor;
		Item.Target = Ref.Target;
	}
	Entry.FactSubject = Change.FactSubject;
	Entry.FactPredicate = Change.FactPredicate;
	Entry.OldConfidence = Change.OldConfidence;
	Entry.NewConfidence = Change.NewConfidence;
	MemoryChanges.Push(MoveTemp(Entry));
	++MemoryChangeSerial;
}

void URAIMindViewRecorder::HandleManagerLoop(URAITaskComponent* Task, int32 Count, double Window)
{
	FRAIMindViewDetection Detection;
	Detection.At = Now();
	Detection.Kind = ERAIMindViewDetectorKind::ManagerLoop;
	ChainOf(Task, Detection.Chain);
	Detection.Count = Count;
	Detection.StateRevision = StateRevisionProvider ? StateRevisionProvider() : 0;
	Detections.Push(MoveTemp(Detection));
	++DetectorSerial;
}

void URAIMindViewRecorder::ChainOf(const URAITaskComponent* Leaf, TArray<FName>& Out)
{
	Out.Reset();
	for (const URAITaskComponent* Cursor = Leaf; Cursor; Cursor = Cursor->GetInvokingParent()) Out.Insert(Cursor->GetFName(), 0);
}

void URAIMindViewRecorder::CheckRepeat(const TArray<FName>& Chain)
{
	if (Chain.IsEmpty() || RepeatCount <= 1) return;
	uint32 Hash = 0;
	for (const FName& Name : Chain) Hash = HashCombine(Hash, GetTypeHash(Name));
	const double At = Now();
	const uint32 Revision = StateRevisionProvider ? StateRevisionProvider() : 0;
	RepeatSamples.RemoveAll([this, At](const FRepeatSample& Sample) { return At - Sample.At > RepeatWindowSeconds; });
	RepeatSamples.Add({Hash, At, Revision});

	int32 Same = 0;
	for (const FRepeatSample& Sample : RepeatSamples) if (Sample.ChainHash == Hash && Sample.StateRevision == Revision) ++Same;
	if (Same < RepeatCount) return;
	if (const double* Reported = LastReportedRepeat.Find(Hash); Reported && At - *Reported <= RepeatWindowSeconds) return;
	LastReportedRepeat.Add(Hash, At);

	FRAIMindViewDetection Detection;
	Detection.At = At;
	Detection.Kind = ERAIMindViewDetectorKind::RepeatWithoutChange;
	Detection.Chain = Chain;
	Detection.Count = Same;
	Detection.StateRevision = Revision;
	Detections.Push(MoveTemp(Detection));
	++DetectorSerial;
}
