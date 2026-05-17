// Copyright Rancorous Games, 2024

#include "Mind/RAIMemoryComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

URAIMemoryComponent::URAIMemoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URAIMemoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Consolidation runs once per in-game minute (adjust to game day length)
	GetWorld()->GetTimerManager().SetTimer(
		ConsolidationTimer, this,
		&URAIMemoryComponent::OnConsolidationTick,
		60.f, true);
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

	if (Episodic.Num() > WorkingSetCap)
	{
		ConsolidateOrForget();
	}
}

void URAIMemoryComponent::LearnFact(FRAISemanticFact Fact)
{
	// Update existing fact if Subject+Predicate already known
	for (FRAISemanticFact& Existing : Semantic)
	{
		if (Existing.Subject == Fact.Subject && Existing.Predicate == Fact.Predicate)
		{
			// Blend confidence toward the new information
			Existing.Confidence = FMath::Clamp(
				FMath::Lerp(Existing.Confidence, Fact.Confidence, 0.3f), 0.f, 1.f);
			return;
		}
	}
	Semantic.Add(MoveTemp(Fact));
}

// ── Episodic Read ─────────────────────────────────────────────────────────────

TArray<FRAIEpisodicMemory> URAIMemoryComponent::Recall(
	const FGameplayTagQuery& Query, int32 MaxResults) const
{
	TArray<FRAIEpisodicMemory> Matches;
	for (const FRAIEpisodicMemory& M : Episodic)
	{
		if (Query.IsEmpty() || Query.Matches(M.IndexTags))
		{
			Matches.Add(M);
		}
	}

	// Sort by current salience × confidence, descending
	Matches.Sort([this](const FRAIEpisodicMemory& A, const FRAIEpisodicMemory& B)
	{
		return (A.Salience * CurrentConfidence(A)) > (B.Salience * CurrentConfidence(B));
	});

	if (MaxResults > 0 && Matches.Num() > MaxResults)
	{
		Matches.SetNum(MaxResults);
	}
	return Matches;
}

TArray<FRAIEpisodicMemory> URAIMemoryComponent::RecallAbout(
	AActor* Subject, FGameplayTag KindFilter, int32 MaxResults) const
{
	if (!Subject) return {};

	TArray<FRAIEpisodicMemory> Matches;
	for (const FRAIEpisodicMemory& M : Episodic)
	{
		const bool ActorMatch  = M.Event.Actor  == Subject || M.Event.Target == Subject;
		const bool WitnessMatch = M.Event.Witnesses.Contains(Subject);
		if (!ActorMatch && !WitnessMatch) continue;
		if (KindFilter.IsValid() && !M.Event.Kind.MatchesTag(KindFilter)) continue;
		Matches.Add(M);
	}

	Matches.Sort([this](const FRAIEpisodicMemory& A, const FRAIEpisodicMemory& B)
	{
		return (A.Salience * CurrentConfidence(A)) > (B.Salience * CurrentConfidence(B));
	});

	if (MaxResults > 0 && Matches.Num() > MaxResults)
	{
		Matches.SetNum(MaxResults);
	}
	return Matches;
}

float URAIMemoryComponent::ValenceTowards(AActor* Subject) const
{
	if (!Subject) return 0.f;

	float WeightedSum = 0.f;
	float TotalWeight = 0.f;

	for (const FRAIEpisodicMemory& M : Episodic)
	{
		const bool Mentions = (M.Event.Actor == Subject || M.Event.Target == Subject);
		if (!Mentions) continue;

		const float W = M.Salience * CurrentConfidence(M);
		WeightedSum  += M.Valence * W;
		TotalWeight  += W;
	}

	return TotalWeight > SMALL_NUMBER ? WeightedSum / TotalWeight : 0.f;
}

float URAIMemoryComponent::CurrentConfidence(const FRAIEpisodicMemory& M) const
{
	if (!GetWorld()) return M.Confidence;

	const float Now       = GetWorld()->GetTimeSeconds();
	const float Reference = FMath::Max(M.LastRecalledTime, M.Event.WorldTime);
	const float Age       = FMath::Max(Now - Reference, 0.f);
	const float HalfLife  = M.bConsolidated ? EpisodicLongHalfLife : EpisodicShortHalfLife;
	return M.Confidence * FMath::Pow(0.5f, Age / HalfLife);
}

void URAIMemoryComponent::Touch(const FGuid& MemoryId)
{
	for (FRAIEpisodicMemory& M : Episodic)
	{
		if (M.Id == MemoryId)
		{
			M.RecallCount++;
			M.LastRecalledTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
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
	Episodic.Sort([this](const FRAIEpisodicMemory& A, const FRAIEpisodicMemory& B)
	{
		return (A.Salience * CurrentConfidence(A)) > (B.Salience * CurrentConfidence(B));
	});

	// Top 25% become long-term; rest is forgotten if over cap
	const int32 KeepCount = FMath::Max(FMath::RoundToInt(Episodic.Num() * 0.25f), 10);
	for (int32 i = 0; i < FMath::Min(KeepCount, Episodic.Num()); ++i)
	{
		Episodic[i].bConsolidated = true;
	}
	Episodic.SetNum(FMath::Max(KeepCount, WorkingSetCap / 2));
}

void URAIMemoryComponent::OnConsolidationTick()
{
	if (Episodic.Num() > WorkingSetCap * 0.8f)
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
