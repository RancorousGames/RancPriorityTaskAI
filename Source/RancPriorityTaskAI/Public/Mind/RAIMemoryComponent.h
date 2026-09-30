// Copyright Rancorous Games, 2024
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Mind/RAILifeEvent.h"
#include "RAIScheduling.h"
#include "RAIMemoryComponent.generated.h"

/**
 * FRAIEpisodicMemory — a single experienced (or heard) event in an NPC's memory.
 * See MIND_DESIGN.md §3.2.
 */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAIEpisodicMemory
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") FGuid              Id;
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") FRAILifeEvent      Event;          // frozen at encode time

	/** This mind's personal interpretation — diverges from Event baselines by personality. */
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") float              Valence    = 0.f;  // -1..+1
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") float              Arousal    = 0.f;  //  0..1
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") float              Salience   = 0.f;  // drives retention

	/** Retention erosion. Drops with age, resets on recall. */
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") float              Confidence = 1.f;
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") int32              RecallCount      = 0;
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") double             LastRecalledTime = 0.0;

	/** Consolidated memories survive the working-set cap and decay much slower. */
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") bool               bConsolidated   = false;

	/** Tag index for fast associative retrieval. Populated on encode. */
	UPROPERTY(BlueprintReadOnly, Category="RAI|Memory") FGameplayTagContainer IndexTags;
};

/**
 * FRAISemanticFact — a generalised rule this NPC believes to be true.
 * Example: "White fruit poisons children." See MIND_DESIGN.md §3.3.
 */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAI_API FRAISemanticFact
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Memory") FGameplayTag         Subject;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Memory") FGameplayTag         Predicate;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Memory") FInstancedStruct     Value;    // optional payload
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Memory") float                Confidence = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RAI|Memory") TWeakObjectPtr<AActor> LearnedFrom; // null = innate
};

UENUM(BlueprintType)
enum class ERAIFactMergePolicy : uint8
{
	BlendConfidence,
	Replace,
	KeepHigherConfidence
};

enum class ERAIMemoryChange : uint8
{
	Encoded,
	Consolidated,
	Forgotten,
	FactLearned
};

/** Identity of an affected episode, copied because forgotten episodes no longer exist after the change. */
struct FRAIMemoryEpisodeRef
{
	FGuid Id;
	FGuid OriginId;
	FGameplayTag Kind;
	FGameplayTag Action;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<AActor> Target;
};

/** One storage change, described for debug views. Built only while a listener is bound. */
struct FRAIMemoryChange
{
	ERAIMemoryChange Kind = ERAIMemoryChange::Encoded;
	int32 Count = 0;
	/** Why it happened: Witnessed/Heard for encodes, WorkingSetCap/Maintenance for consolidation, New/Blended/Replaced for facts. */
	FName Reason;
	/** Up to MaxDescribedEpisodes affected episodes. */
	TArray<FRAIMemoryEpisodeRef, TInlineAllocator<4>> Episodes;
	FGameplayTag FactSubject;
	FGameplayTag FactPredicate;
	/** -1 when the fact was not previously known. */
	float OldConfidence = -1.f;
	float NewConfidence = 0.f;

	static constexpr int32 MaxDescribedEpisodes = 8;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRAIMemoryChangedNative, const FRAIMemoryChange&);

/** Owner-agnostic episodic and semantic storage; attach alongside a mind on any actor. */
UCLASS(ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAIMemoryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URAIMemoryComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void SetServices(TSharedPtr<IRAITimeSource> Time, TSharedPtr<IRAIScheduler> InScheduler);
	double GetNow() const;
	void SetConsolidationPaused(bool bPaused);
	bool IsConsolidationPaused() const;

	// ── Configuration ─────────────────────────────────────────────────────────

	/** Max short-term episodes before consolidation/forgetting runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RAI|Memory")
	int32 WorkingSetCap = 60;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RAI|Memory", meta=(ClampMin="0"))
	int32 LongTermCap = 240;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RAI|Memory", meta=(ClampMin="0", ClampMax="1"))
	float FactConfidenceBlend = 0.3f;

	/** Half-life of unconsolidated memories in game-time seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RAI|Memory")
	float EpisodicShortHalfLife = 600.f;

	/** Half-life of consolidated memories in time-source seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RAI|Memory")
	float EpisodicLongHalfLife = 86400.f;

	// ── Write (called by URAIMindComponent only) ──────────────────────────────

	/**
	 * Encode a life event into episodic memory.
	 * Personality bias (via URAIPersonalityComponent) is applied here.
	 * Call via URAIMindComponent::Witness(), not directly.
	 * Must be UFUNCTION so the mind's dynamic delegate can bind to it.
	 */
	UFUNCTION()
	void EncodeEpisodic(const FRAILifeEvent& Event);

	/** Default merge stores new value/source and blends confidence with FactConfidenceBlend. */
	void LearnFact(FRAISemanticFact Fact);
	void LearnFact(FRAISemanticFact Fact, ERAIFactMergePolicy Policy);

	// ── Read ──────────────────────────────────────────────────────────────────

	/**
	 * Retrieve episodes matching the tag query, sorted by current salience × confidence.
	 */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	TArray<FRAIEpisodicMemory> Recall(const FGameplayTagQuery& Query, int32 MaxResults = 8) const;
	/** Indices remain valid only until storage is mutated. MaxResults <= 0 returns all matches. */
	TArray<int32> RecallRefs(const FGameplayTagQuery& Query, int32 MaxResults = 8) const;
	TArray<int32> RecallAboutRefs(AActor* Subject, FGameplayTag KindFilter = {}, int32 MaxResults = 8) const;

	/** Episodes about a specific actor. Optionally filter by event Kind. */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	TArray<FRAIEpisodicMemory> RecallAbout(AActor* Subject,
	                                       FGameplayTag KindFilter = FGameplayTag(),
	                                       int32 MaxResults = 8) const;

	/**
	 * Aggregate valence across all memories mentioning Subject.
	 * Range -1..+1. "How do I feel about this actor overall?"
	 */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	float ValenceTowards(AActor* Subject) const;

	/** Current confidence (decay applied). */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	float CurrentConfidence(const FRAIEpisodicMemory& Memory) const;

	/** Mark recall — boosts retention by resetting LastRecalledTime. */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	void Touch(const FGuid& MemoryId);

	// ── Semantic ──────────────────────────────────────────────────────────────

	/** Returns true if this NPC knows Subject→Predicate at any confidence. */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	bool Knows(FGameplayTag Subject, FGameplayTag Predicate) const;

	/** Returns true if confidence meets MinConfidence. */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	bool Believes(FGameplayTag Subject, FGameplayTag Predicate, float MinConfidence = 0.5f) const;

	/** Everything this NPC knows about Subject. */
	UFUNCTION(BlueprintCallable, Category="RAI|Memory")
	TArray<FRAISemanticFact> What(FGameplayTag Subject) const;

	/** Storage changes, for debug views. Monotonic revision below changes on every notification. */
	FRAIMemoryChangedNative OnMemoryChangedNative;
	uint32 GetRevision() const { return Revision; }

	// ── Storage (accessible for debug / serialization) ────────────────────────

	UPROPERTY(VisibleAnywhere, Transient, Category="RAI|Memory")
	TArray<FRAIEpisodicMemory> Episodic;

	UPROPERTY(VisibleAnywhere, Transient, Category="RAI|Memory")
	TArray<FRAISemanticFact>   Semantic;

private:
	uint32 Revision = 0;
	/** Bumps the revision and, only if someone listens, builds and broadcasts the change descriptor. */
	void NotifyChanged(ERAIMemoryChange Change, int32 Count, TFunctionRef<void(FRAIMemoryChange&)> Describe);
	static FRAIMemoryEpisodeRef MakeRef(const FRAIEpisodicMemory& Memory);
	bool bConsolidationPaused = false;
	void ConsolidateOrForget(FName Reason);

	/** Returns 0..1 measuring how similar E is to the N most recent episodes. */
	float SimilarityToRecentEpisodes(const FRAILifeEvent& E, int32 N = 5) const;

	TSharedPtr<IRAITimeSource> TimeSource;
	TSharedPtr<IRAIScheduler> Scheduler;
	FRAIScheduleHandle ConsolidationTimer;
	double ConsolidationDue = 0.0;
	double ConsolidationRemaining = 60.0;
	void ScheduleConsolidation();
	float ConfidenceAt(const FRAIEpisodicMemory& Memory, double Now) const;
	TArray<int32> RankMatches(TFunctionRef<bool(const FRAIEpisodicMemory&)> Matches, int32 MaxResults) const;
	void OnConsolidationTick();
};
