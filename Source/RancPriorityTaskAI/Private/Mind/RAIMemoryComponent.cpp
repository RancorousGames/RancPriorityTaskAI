// Copyright Rancorous Games, 2024

#include "Mind/RAIMemoryComponent.h"
#include "Engine/World.h"
#include <algorithm>
#include "Algo/Count.h"

URAIMemoryComponent::URAIMemoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URAIMemoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TimeSource) TimeSource = MakeShared<FRAIWorldTimeSource>(GetWorld());
	if (!Scheduler) Scheduler = MakeShared<FRAITimerManagerScheduler>(GetWorld());
	ScheduleConsolidation();
}

void URAIMemoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Scheduler) Scheduler->CancelAll(this);
	ConsolidationTimer.Invalidate();
	Super::EndPlay(Reason);
}

void URAIMemoryComponent::SetServices(TSharedPtr<IRAITimeSource> Time, TSharedPtr<IRAIScheduler> InScheduler)
{
	if (Scheduler) Scheduler->CancelAll(this);
	ConsolidationTimer.Invalidate();
	TimeSource = MoveTemp(Time);
	Scheduler = MoveTemp(InScheduler);
	ConsolidationRemaining = 60.0;
	if (HasBegunPlay())
	{
		if (!Scheduler) Scheduler = MakeShared<FRAITimerManagerScheduler>(GetWorld());
		ScheduleConsolidation();
	}
}

double URAIMemoryComponent::GetNow() const
{
	return TimeSource ? TimeSource->Now() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void URAIMemoryComponent::ScheduleConsolidation()
{
	if (bConsolidationPaused || !Scheduler) return;
	ConsolidationDue = GetNow() + ConsolidationRemaining;
	ConsolidationTimer = Scheduler->ScheduleOnce(this, ConsolidationRemaining,
		[Self = TWeakObjectPtr<URAIMemoryComponent>(this)]
		{
			if (URAIMemoryComponent* Memory = Self.Get())
			{
				Memory->ConsolidationTimer.Invalidate();
				Memory->OnConsolidationTick();
				Memory->ConsolidationRemaining = 60.0;
				Memory->ScheduleConsolidation();
			}
		});
}

void URAIMemoryComponent::SetConsolidationPaused(bool bPaused)
{
	if (bConsolidationPaused == bPaused) return;
	if (bPaused && Scheduler && ConsolidationTimer.IsValid())
	{
		ConsolidationRemaining = FMath::Max(ConsolidationDue - GetNow(), 0.0);
		Scheduler->Cancel(ConsolidationTimer);
	}
	bConsolidationPaused = bPaused;
	if (!bPaused && HasBegunPlay()) ScheduleConsolidation();
}

bool URAIMemoryComponent::IsConsolidationPaused() const
{
	return bConsolidationPaused;
}

// ── Encoding ──────────────────────────────────────────────────────────────────

void URAIMemoryComponent::EncodeEpisodic(const FRAILifeEvent& Event)
{
	FRAIEpisodicMemory M;
	M.Id    = FGuid::NewGuid();
	M.Event = Event;

	// Personality bias is applied by the caller (URAIMindComponent) before this
	// call — Valence and Arousal deltas are already in Event.BaselineValence/Arousal.
	// We store them as-is here; personalised drift happens at the mind layer.
	M.Valence = FMath::Clamp(Event.BaselineValence, -1.f, 1.f);
	M.Arousal = FMath::Clamp(Event.BaselineArousal,  0.f, 1.f);

	// Personal relevance: direct experience > witness > hearsay
	const float Relevance = Event.IsDirectExperience() ? 1.f
	                       : (Event.SourceCredibility * 0.5f);

	const float Novelty   = 1.f - SimilarityToRecentEpisodes(Event);
	M.Salience = FMath::Clamp(M.Arousal * Relevance * (0.5f + 0.5f * Novelty), 0.f, 1.f);

	// Build tag index for associative retrieval
	if (Event.Kind.IsValid())  M.IndexTags.AddTag(Event.Kind);
	if (Event.Action.IsValid()) M.IndexTags.AddTag(Event.Action);
	M.IndexTags.AppendTags(Event.ObjectTags);

	Episodic.Add(MoveTemp(M));

	if (Algo::CountIf(Episodic, [](const FRAIEpisodicMemory& Entry) { return !Entry.bConsolidated; }) > FMath::Max(WorkingSetCap, 0))
	{
		ConsolidateOrForget();
	}
}

void URAIMemoryComponent::LearnFact(FRAISemanticFact Fact)
{
	LearnFact(MoveTemp(Fact), ERAIFactMergePolicy::BlendConfidence);
}

void URAIMemoryComponent::LearnFact(FRAISemanticFact Fact, ERAIFactMergePolicy Policy)
{
	Fact.Confidence = FMath::Clamp(Fact.Confidence, 0.f, 1.f);
	// Update existing fact if Subject+Predicate already known
	for (FRAISemanticFact& Existing : Semantic)
	{
		if (Existing.Subject == Fact.Subject && Existing.Predicate == Fact.Predicate)
		{
			if (Policy == ERAIFactMergePolicy::KeepHigherConfidence && Existing.Confidence > Fact.Confidence) return;
			if (Policy == ERAIFactMergePolicy::BlendConfidence)
				Fact.Confidence = FMath::Lerp(Existing.Confidence, Fact.Confidence, FMath::Clamp(FactConfidenceBlend, 0.f, 1.f));
			Existing = MoveTemp(Fact);
			return;
		}
	}
	Semantic.Add(MoveTemp(Fact));
}

// ── Episodic Read ─────────────────────────────────────────────────────────────

TArray<FRAIEpisodicMemory> URAIMemoryComponent::Recall(
	const FGameplayTagQuery& Query, int32 MaxResults) const
{
	TArray<FRAIEpisodicMemory> Result;
	for (int32 Index : RecallRefs(Query, MaxResults)) Result.Add(Episodic[Index]);
	return Result;
}

TArray<int32> URAIMemoryComponent::RecallRefs(const FGameplayTagQuery& Query, int32 MaxResults) const
{
	return RankMatches([&Query](const FRAIEpisodicMemory& M) { return Query.IsEmpty() || Query.Matches(M.IndexTags); }, MaxResults);
}

TArray<int32> URAIMemoryComponent::RankMatches(TFunctionRef<bool(const FRAIEpisodicMemory&)> Matches, int32 MaxResults) const
{
	struct FScoredIndex { int32 Index; float Score; };
	TArray<FScoredIndex> Ranked;
	const double Now = GetNow();
	for (int32 Index = 0; Index < Episodic.Num(); ++Index)
		if (Matches(Episodic[Index])) Ranked.Add({Index, Episodic[Index].Salience * ConfidenceAt(Episodic[Index], Now)});
	const int32 Count = MaxResults > 0 ? FMath::Min(MaxResults, Ranked.Num()) : Ranked.Num();
	if (Count > 0)
		std::partial_sort(Ranked.GetData(), Ranked.GetData() + Count, Ranked.GetData() + Ranked.Num(),
			[](const FScoredIndex& A, const FScoredIndex& B) { return A.Score == B.Score ? A.Index < B.Index : A.Score > B.Score; });
	TArray<int32> Result;
	Result.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index) Result.Add(Ranked[Index].Index);
	return Result;
}

TArray<FRAIEpisodicMemory> URAIMemoryComponent::RecallAbout(
	AActor* Subject, FGameplayTag KindFilter, int32 MaxResults) const
{
	TArray<FRAIEpisodicMemory> Result;
	for (int32 Index : RecallAboutRefs(Subject, KindFilter, MaxResults)) Result.Add(Episodic[Index]);
	return Result;
}

TArray<int32> URAIMemoryComponent::RecallAboutRefs(AActor* Subject, FGameplayTag KindFilter, int32 MaxResults) const
{
	if (!Subject) return {};
	return RankMatches([Subject, KindFilter](const FRAIEpisodicMemory& M)
	{
		return (M.Event.Actor == Subject || M.Event.Target == Subject || M.Event.Witnesses.Contains(Subject))
			&& (!KindFilter.IsValid() || M.Event.Kind.MatchesTag(KindFilter));
	}, MaxResults);
}

float URAIMemoryComponent::ValenceTowards(AActor* Subject) const
{
	if (!Subject) return 0.f;

	float WeightedSum = 0.f;
	float TotalWeight = 0.f;
	const double Now = GetNow();

	for (const FRAIEpisodicMemory& M : Episodic)
	{
		const bool Mentions = (M.Event.Actor == Subject || M.Event.Target == Subject);
		if (!Mentions) continue;

		const float W = M.Salience * ConfidenceAt(M, Now);
		WeightedSum  += M.Valence * W;
		TotalWeight  += W;
	}

	return TotalWeight > SMALL_NUMBER ? WeightedSum / TotalWeight : 0.f;
}

float URAIMemoryComponent::CurrentConfidence(const FRAIEpisodicMemory& M) const
{
	return ConfidenceAt(M, GetNow());
}

float URAIMemoryComponent::ConfidenceAt(const FRAIEpisodicMemory& M, double Now) const
{
	const double Age = FMath::Max(Now - FMath::Max(M.LastRecalledTime, M.Event.WorldTime), 0.0);
	const double HalfLife = FMath::Max(static_cast<double>(M.bConsolidated ? EpisodicLongHalfLife : EpisodicShortHalfLife), UE_DOUBLE_SMALL_NUMBER);
	return M.Confidence * FMath::Pow(0.5, Age / HalfLife);
}

void URAIMemoryComponent::Touch(const FGuid& MemoryId)
{
	for (FRAIEpisodicMemory& M : Episodic)
	{
		if (M.Id == MemoryId)
		{
			M.RecallCount++;
			M.LastRecalledTime = GetNow();
			return;
		}
	}
}

// ── Semantic ──────────────────────────────────────────────────────────────────

bool URAIMemoryComponent::Knows(FGameplayTag Subject, FGameplayTag Predicate) const
{
	for (const FRAISemanticFact& F : Semantic)
	{
		if (F.Subject == Subject && F.Predicate == Predicate) return true;
	}
	return false;
}

bool URAIMemoryComponent::Believes(FGameplayTag Subject, FGameplayTag Predicate,
                                   float MinConfidence) const
{
	for (const FRAISemanticFact& F : Semantic)
	{
		if (F.Subject == Subject && F.Predicate == Predicate)
		{
			return F.Confidence >= MinConfidence;
		}
	}
	return false;
}

TArray<FRAISemanticFact> URAIMemoryComponent::What(FGameplayTag Subject) const
{
	TArray<FRAISemanticFact> Results;
	for (const FRAISemanticFact& F : Semantic)
	{
		if (F.Subject == Subject) Results.Add(F);
	}
	return Results;
}

// ── Consolidation ─────────────────────────────────────────────────────────────

void URAIMemoryComponent::ConsolidateOrForget()
{
	const TArray<int32> Working = RankMatches([](const FRAIEpisodicMemory& M) { return !M.bConsolidated; }, 0);
	const int32 Promote = FMath::Min(Working.Num(), FMath::Max(FMath::RoundToInt(Working.Num() * 0.25f), 10));
	for (int32 Index = 0; Index < Promote; ++Index) Episodic[Working[Index]].bConsolidated = true;
	const TArray<int32> LongTerm = RankMatches([](const FRAIEpisodicMemory& M) { return M.bConsolidated; }, 0);
	TSet<int32> Keep;
	for (int32 Index = 0; Index < FMath::Min(LongTerm.Num(), FMath::Max(LongTermCap, 0)); ++Index) Keep.Add(LongTerm[Index]);
	const int32 KeepWorking = FMath::Min(Working.Num() - Promote, FMath::Max(WorkingSetCap / 2, 0));
	for (int32 Index = Promote; Index < Promote + KeepWorking; ++Index) Keep.Add(Working[Index]);
	// Remove backwards to preserve chronological storage and never append empty episodes.
	for (int32 Index = Episodic.Num() - 1; Index >= 0; --Index)
		if (!Keep.Contains(Index)) Episodic.RemoveAt(Index, EAllowShrinking::No);
}

void URAIMemoryComponent::OnConsolidationTick()
{
	if (Algo::CountIf(Episodic, [](const FRAIEpisodicMemory& M) { return !M.bConsolidated; }) > FMath::Max(WorkingSetCap, 0) * 0.8f
		|| Algo::CountIf(Episodic, [](const FRAIEpisodicMemory& M) { return M.bConsolidated; }) > FMath::Max(LongTermCap, 0))
	{
		ConsolidateOrForget();
	}
}

// ── Helpers ───────────────────────────────────────────────────────────────────

float URAIMemoryComponent::SimilarityToRecentEpisodes(const FRAILifeEvent& E, int32 N) const
{
	if (Episodic.IsEmpty()) return 0.f;

	const int32 StartIdx = FMath::Max(Episodic.Num() - N, 0);
	float MaxSimilarity  = 0.f;

	for (int32 i = StartIdx; i < Episodic.Num(); ++i)
	{
		const FRAILifeEvent& Recent = Episodic[i].Event;
		float Sim = 0.f;
		if (Recent.Kind   == E.Kind)   Sim += 0.4f;
		if (Recent.Action == E.Action) Sim += 0.3f;
		if (Recent.Actor  == E.Actor)  Sim += 0.2f;
		if (Recent.Target == E.Target) Sim += 0.1f;
		MaxSimilarity = FMath::Max(MaxSimilarity, Sim);
	}

	return MaxSimilarity;
}
