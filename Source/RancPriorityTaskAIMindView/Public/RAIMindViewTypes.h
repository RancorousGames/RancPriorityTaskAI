// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RAIDataStructures.h"
#include "StructUtils/InstancedStruct.h"
#include "RAIMindViewTypes.generated.h"

/** Which layer of truth a section shows. Views badge every section so belief never reads as fact. */
UENUM(BlueprintType)
enum class ERAIMindViewTruth : uint8
{
	/** Objective simulation state. */
	Truth,
	/** What the subject believes. */
	Belief,
	/** How a decision was reached. */
	Process
};

UENUM(BlueprintType)
enum class ERAIMindViewDetail : uint8
{
	/** Head-card form: a few lines. */
	Compact,
	/** Main-panel form. */
	Full
};

UENUM(BlueprintType)
enum class ERAIMindViewControlMode : uint8
{
	None,
	AI,
	Player
};

/** One typed debug thought. Formatted by the viewer from Kind. */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIThought
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double Time = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Kind;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIThoughtTone Tone = ERAIThoughtTone::Neutral;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> A;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> B;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float V0 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float V1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName P;
	/** Only for RAI.Thought.Legacy. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FString Legacy;
};

/** What one consumer needs from one subject. Merged across consumers by the subsystem. */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewDemand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") TSet<FName> FullSections;
	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") TSet<FName> CompactSections;
	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") float RateHz = 2.f;
	/** Row count for compact lists (thoughts, top purposes). */
	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") int32 CompactCount = 5;

	bool IsEmpty() const { return FullSections.IsEmpty() && CompactSections.IsEmpty(); }
	void MergeFrom(const FRAIMindViewDemand& Other);
	bool operator==(const FRAIMindViewDemand& Other) const;
};

/** A globally pinned section shown on head cards. */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewPin
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|MindView") FName SectionId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|MindView") int32 Count = 5;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewHeader
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Subject;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Kind;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIMindViewControlMode ControlMode = ERAIMindViewControlMode::None;
	/** Chain root and leaf component names; None when idle or without a manager. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Root;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Leaf;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bHasDetector = false;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewSectionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIMindViewTruth Truth = ERAIMindViewTruth::Process;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIMindViewDetail Detail = ERAIMindViewDetail::Full;
	/** Unchanged when nothing the section would render has changed, so views can skip rebuilding it. */
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FInstancedStruct Data;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FRAIMindViewHeader Header;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double CapturedAt = 0.0;
	UPROPERTY() uint32 Serial = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewSectionData> Sections;

	const FRAIMindViewSectionData* FindSection(FName Id) const { return Sections.FindByPredicate([Id](const FRAIMindViewSectionData& S) { return S.Id == Id; }); }
	template <typename T> const T* GetSection(FName Id) const
	{
		const FRAIMindViewSectionData* Section = FindSection(Id);
		return Section ? Section->Data.GetPtr<T>() : nullptr;
	}
};

// ─── Generic section payloads ──────────────────────────────────────────────

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Task;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Priority = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bReady = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bWinner = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bActiveRoot = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName ExcludedReason;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIPriorityTerm> Terms;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewDecisions
{
	GENERATED_BODY()

	/** Highest priority first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewCandidate> Candidates;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIArbitrationDecision Decision = ERAIArbitrationDecision::NoCandidate;
	/** Effective priority of the chain that was running when the decision was made (switching friction reference). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float ActiveRootPriority = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewChainEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Task;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAITaskRunState RunState = ERAITaskRunState::Inactive;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double BegunAt = -1.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Priority = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag LastChildOutcome;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewChain
{
	GENERATED_BODY()

	/** Root first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewChainEntry> Entries;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewSwitch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName From;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName To;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIArbitrationDecision Decision = ERAIArbitrationDecision::NoCandidate;
	/** Top terms of the new root and of the previous root at the moment of the switch. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIPriorityTerm> ToTerms;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIPriorityTerm> FromTerms;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float ToPriority = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float FromPriority = 0.f;
	/** Chain signature that was running before the switch, root first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FName> PreviousChain;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewLifecycleEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Task;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bBegin = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bInterrupted = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Reason;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") int32 Depth = 0;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewHistory
{
	GENERATED_BODY()

	/** Newest first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewSwitch> Switches;
	/** Newest first; only in Full detail. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewLifecycleEvent> Lifecycle;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewThoughts
{
	GENERATED_BODY()

	/** Newest first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIThought> Thoughts;
};

UENUM(BlueprintType)
enum class ERAIMindViewDetectorKind : uint8
{
	/** The same chain repeatedly started without any change in the subject's state. */
	RepeatWithoutChange,
	/** The manager's own start/restart loop protection fired. */
	ManagerLoop
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewDetection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") ERAIMindViewDetectorKind Kind = ERAIMindViewDetectorKind::RepeatWithoutChange;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FName> Chain;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") int32 Count = 0;
	UPROPERTY() uint32 StateRevision = 0;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewDetectors
{
	GENERATED_BODY()

	/** Newest first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewDetection> Detections;
	/** A detection newer than the subsystem's DetectorActiveSeconds. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bActive = false;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewEpisode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGuid Id;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGuid OriginId;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Kind;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Action;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Actor;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Target;
	/** Null when first-hand. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Source;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float SourceCredibility = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Valence = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Salience = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Confidence = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bConsolidated = false;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewFact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Subject;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Predicate;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Confidence = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> LearnedFrom;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewMemoryItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGuid Id;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGuid OriginId;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Kind;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag Action;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Actor;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Target;
};

/** One row of the memory change log (prototype MindView "Memory change log"). */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewMemoryChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double At = 0.0;
	/** Added, Consolidated (promoted to long-term), Removed (forgotten), FactLearned, or Changed (known fact revised). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Kind;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") int32 Count = 0;
	/** Witnessed/Heard, WorkingSetCap/Maintenance, New/Blended/Replaced. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FName Reason;
	/** Affected episodes (at most a few per change). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewMemoryItem> Items;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag FactSubject;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") FGameplayTag FactPredicate;
	/** -1 when the fact was new. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float OldConfidence = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float NewConfidence = 0.f;
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewMemory
{
	GENERATED_BODY()

	/** Newest first. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewEpisode> Episodes;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewFact> Facts;
	/** Newest first; recorded only while demanded. */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TArray<FRAIMindViewMemoryChange> Changes;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") int32 TotalEpisodes = 0;
};

// ─── "Who knows" ───────────────────────────────────────────────────────────

/** Identity of a piece of knowledge across minds: an episode origin, or a semantic Subject+Predicate. */
USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewFactKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") FGuid OriginId;
	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") FGameplayTag Subject;
	UPROPERTY(BlueprintReadWrite, Category = "RAI|MindView") FGameplayTag Predicate;

	bool IsEpisode() const { return OriginId.IsValid(); }
	bool IsValid() const { return OriginId.IsValid() || (Subject.IsValid() && Predicate.IsValid()); }
	bool operator==(const FRAIMindViewFactKey& Other) const { return OriginId == Other.OriginId && Subject == Other.Subject && Predicate == Other.Predicate; }
	friend uint32 GetTypeHash(const FRAIMindViewFactKey& Key) { return HashCombine(GetTypeHash(Key.OriginId), HashCombine(GetTypeHash(Key.Subject), GetTypeHash(Key.Predicate))); }
};

USTRUCT(BlueprintType)
struct RANCPRIORITYTASKAIMINDVIEW_API FRAIMindViewKnower
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Holder;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bFirstHand = false;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") TWeakObjectPtr<AActor> Source;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") float Credibility = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") double LearnedAt = 0.0;
	/** The holder's version differs from the reference holder's (different target/action or fact value). */
	UPROPERTY(BlueprintReadOnly, Category = "RAI|MindView") bool bContradicts = false;
};
