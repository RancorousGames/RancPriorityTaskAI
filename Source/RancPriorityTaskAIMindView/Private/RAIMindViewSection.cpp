// Copyright Rancorous Games, 2026

#include "RAIMindViewSection.h"

#include "Mind/RAIMemoryComponent.h"
#include "RAIManagerComponent.h"
#include "RAIMindViewRecorder.h"
#include "RAITaskComponent.h"

namespace RAIMindViewSections
{
	const FName Decisions(TEXT("Decisions"));
	const FName Chain(TEXT("Chain"));
	const FName History(TEXT("History"));
	const FName Thoughts(TEXT("Thoughts"));
	const FName Detectors(TEXT("Detectors"));
	const FName Memory(TEXT("Memory"));
}

namespace
{
uint32 Mix(uint32 Hash, uint32 Value) { return HashCombine(Hash, Value); }
uint32 Quantize(float Value) { return static_cast<uint32>(FMath::RoundToInt(Value * 10.f)); }
int32 RowLimit(const FRAIMindViewCaptureContext& Context, int32 FullLimit)
{
	return Context.Detail == ERAIMindViewDetail::Compact ? FMath::Max(Context.Count, 1) : FullLimit;
}
}

void URAIMindViewSection_Decisions::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewDecisions Data;
	URAIManagerComponent* Manager = Context.Manager;
	const URAITaskComponent* ActiveRoot = Manager->GetActiveTask() ? Manager->GetActiveTask()->GetRootTask() : nullptr;
	const bool bTerms = Context.Detail == ERAIMindViewDetail::Full;
	float Best = Manager->TaskThreshold;
	int32 WinnerIndex = INDEX_NONE;
	for (URAITaskComponent* Task : Manager->GetPrimaryTasks())
	{
		if (!Task || !Task->IsEnabled) continue;
		FRAIMindViewCandidate& Candidate = Data.Candidates.AddDefaulted_GetRef();
		Candidate.Task = Task->GetFName();
		Candidate.Priority = Task->GetPriority();
		Candidate.bReady = Task->IsTaskActive || Task->IsTaskReady();
		Candidate.bActiveRoot = Task == ActiveRoot;
		Candidate.ExcludedReason = !Candidate.bReady ? FName(TEXT("Cooldown"))
			: (Candidate.Priority <= Manager->TaskThreshold ? FName(TEXT("BelowThreshold")) : NAME_None);
		if (bTerms)
		{
			FRAIPriorityExplanation Explanation;
			Task->DescribePriority(Explanation);
			Candidate.Terms = MoveTemp(Explanation.Terms);
		}
		if (Candidate.bReady && Candidate.Priority > Best) { Best = Candidate.Priority; WinnerIndex = Data.Candidates.Num() - 1; }
	}
	if (WinnerIndex != INDEX_NONE) Data.Candidates[WinnerIndex].bWinner = true;
	Data.Candidates.StableSort([](const FRAIMindViewCandidate& A, const FRAIMindViewCandidate& B) { return A.Priority > B.Priority; });
	if (!bTerms && Data.Candidates.Num() > Context.Count) Data.Candidates.SetNum(FMath::Max(Context.Count, 1));
	if (const URAIMindViewRecorder* Recorder = Context.Recorder)
	{
		Data.Decision = Recorder->GetLastDecision();
		Data.ActiveRootPriority = Recorder->GetLastActiveRootPriority();
	}
	Data.At = Context.Now;

	uint32 Revision = Mix(static_cast<uint32>(Data.Decision), Data.Candidates.Num());
	for (const FRAIMindViewCandidate& Candidate : Data.Candidates)
	{
		Revision = Mix(Revision, GetTypeHash(Candidate.Task));
		Revision = Mix(Revision, Quantize(Candidate.Priority));
		Revision = Mix(Revision, (Candidate.bReady ? 1u : 0u) | (Candidate.bWinner ? 2u : 0u) | (Candidate.bActiveRoot ? 4u : 0u));
		for (const FRAIPriorityTerm& Term : Candidate.Terms) Revision = Mix(Mix(Revision, GetTypeHash(Term.Name)), Quantize(Term.Value));
	}
	Out.Revision = Revision;
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

void URAIMindViewSection_Chain::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewChain Data;
	uint32 Revision = 0;
	for (const URAITaskComponent* Task = Context.Manager->GetActiveTask(); Task; Task = Task->GetInvokingParent())
	{
		FRAIMindViewChainEntry Entry;
		Entry.Task = Task->GetFName();
		Entry.RunState = Task->GetRunState();
		Entry.BegunAt = Task->GetTimeBegun();
		Entry.Priority = Task->GetPriority();
		Entry.LastChildOutcome = Task->GetLastChildOutcome();
		Revision = Mix(Mix(Mix(Revision, GetTypeHash(Entry.Task)), static_cast<uint32>(Entry.RunState)), GetTypeHash(Entry.BegunAt));
		Data.Entries.Insert(MoveTemp(Entry), 0);
	}
	Out.Revision = Mix(Revision, Data.Entries.Num());
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

void URAIMindViewSection_History::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewHistory Data;
	uint32 Revision = 0;
	if (const URAIMindViewRecorder* Recorder = Context.Recorder)
	{
		Recorder->GetSwitches().ForEachNewest(RowLimit(Context, 0), [&Data](const FRAIMindViewSwitch& Switch) { Data.Switches.Add(Switch); });
		Revision = Mix(Recorder->GetSwitchSerial(), Data.Switches.Num());
		if (Context.Detail == ERAIMindViewDetail::Full)
		{
			Recorder->GetLifecycle().ForEachNewest(100, [&Data](const FRAIMindViewLifecycleEvent& Event) { Data.Lifecycle.Add(Event); });
			Revision = Mix(Revision, Recorder->GetLifecycleSerial());
		}
	}
	Out.Revision = Revision;
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

void URAIMindViewSection_Thoughts::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewThoughts Data;
	uint32 Revision = 0;
	if (const URAIMindViewRecorder* Recorder = Context.Recorder)
	{
		Recorder->GetThoughts().ForEachNewest(RowLimit(Context, 0), [&Data](const FRAIThought& Thought) { Data.Thoughts.Add(Thought); });
		Revision = Mix(Recorder->GetThoughtSerial(), Data.Thoughts.Num());
	}
	Out.Revision = Revision;
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

void URAIMindViewSection_Detectors::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewDetectors Data;
	uint32 Revision = 0;
	if (const URAIMindViewRecorder* Recorder = Context.Recorder)
	{
		Recorder->GetDetections().ForEachNewest(RowLimit(Context, 0), [&Data](const FRAIMindViewDetection& Detection) { Data.Detections.Add(Detection); });
		Data.bActive = !Data.Detections.IsEmpty() && Context.Now - Data.Detections[0].At <= ActiveSeconds;
		Revision = Mix(Mix(Recorder->GetDetectorSerial(), Data.bActive ? 1u : 0u), Data.Detections.Num());
	}
	Out.Revision = Revision;
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

void URAIMindViewSection_Memory::Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const
{
	FRAIMindViewMemory Data;
	const URAIMemoryComponent* Memory = Context.Memory;
	Data.TotalEpisodes = Memory->Episodic.Num();
	const int32 Limit = RowLimit(Context, 200);
	for (int32 Index = Memory->Episodic.Num() - 1; Index >= 0 && Data.Episodes.Num() < Limit; --Index)
	{
		const FRAIEpisodicMemory& M = Memory->Episodic[Index];
		FRAIMindViewEpisode& Episode = Data.Episodes.AddDefaulted_GetRef();
		Episode.Id = M.Id;
		Episode.OriginId = M.Event.OriginId;
		Episode.At = M.Event.WorldTime;
		Episode.Kind = M.Event.Kind;
		Episode.Action = M.Event.Action;
		Episode.Actor = M.Event.Actor;
		Episode.Target = M.Event.Target;
		Episode.Source = M.Event.Source;
		Episode.SourceCredibility = M.Event.SourceCredibility;
		Episode.Valence = M.Valence;
		Episode.Salience = M.Salience;
		Episode.Confidence = Memory->CurrentConfidence(M);
		Episode.bConsolidated = M.bConsolidated;
	}
	if (Context.Detail == ERAIMindViewDetail::Full)
	{
		for (const FRAISemanticFact& Fact : Memory->Semantic)
		{
			FRAIMindViewFact& Out2 = Data.Facts.AddDefaulted_GetRef();
			Out2.Subject = Fact.Subject;
			Out2.Predicate = Fact.Predicate;
			Out2.Confidence = Fact.Confidence;
			Out2.LearnedFrom = Fact.LearnedFrom;
		}
		if (Context.Recorder)
			Context.Recorder->GetMemoryChanges().ForEachNewest(0, [&Data](const FRAIMindViewMemoryChange& Change) { Data.Changes.Add(Change); });
	}
	// Confidence decays continuously; refresh the view every few seconds even without storage changes.
	uint32 Revision = Mix(Memory->GetRevision(), static_cast<uint32>(FMath::FloorToInt(Context.Now / 5.0)));
	Revision = Mix(Mix(Revision, Data.Episodes.Num()), Context.Recorder ? Context.Recorder->GetMemoryChangeSerial() : 0u);
	Out.Revision = Revision;
	Out.Data = FInstancedStruct::Make(MoveTemp(Data));
}

uint32 URAIMindViewSection_Memory::GetStateRevision(const FRAIMindViewCaptureContext& Context) const
{
	return Context.Memory ? Context.Memory->GetRevision() : 0u;
}
