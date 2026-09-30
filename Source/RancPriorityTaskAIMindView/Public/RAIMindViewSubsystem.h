// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "RAIMindViewTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "RAIMindViewSubsystem.generated.h"

class APawn;
class URAIManagerComponent;
class URAIMemoryComponent;
class URAIMindViewRecorder;
class URAIMindViewSection;
struct FRAIThoughtArgs;
struct FRAIMindViewCaptureContext;

DECLARE_MULTICAST_DELEGATE_OneParam(FRAIMindViewSnapshotNative, const FRAIMindViewSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRAIMindViewSubjectLostNative, FObjectKey);

/**
 * Authority-side MindView capture. Everything is demand-driven: with no demand the subsystem does not tick,
 * binds nothing on any mind, creates no recorders and leaves the thought hook inactive.
 */
UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URAIMindViewSubsystem* Get(const UObject* WorldContext);

	/** Game modules add section classes at startup; existing and future subsystems instantiate them. */
	static void RegisterSectionClass(TSubclassOf<URAIMindViewSection> SectionClass);
	static void UnregisterSectionClass(TSubclassOf<URAIMindViewSection> SectionClass);

	/** A pawn with a RAI manager-driven controller, or an actor carrying a RAI memory or mind component. */
	static bool IsSubject(const AActor* Actor);
	static URAIManagerComponent* ResolveManager(const AActor* Subject);
	static URAIMemoryComponent* ResolveMemory(const AActor* Subject);

	/** Optional game hook for names and kinds. Default: actor label (editor) or name, kind RAI.MindView.Kind.Default. */
	TFunction<void(const AActor* Subject, FRAIMindViewHeader& InOut)> DescribeSubject;

	/** Set, replace or (with an empty demand) remove one consumer's demand for a subject. */
	void SetDemand(AActor* Subject, FName Consumer, const FRAIMindViewDemand& Demand);
	void ClearConsumer(FName Consumer);
	bool HasAnyDemand() const { return !Subjects.IsEmpty(); }
	int32 GetRecorderCount() const { return RecorderRefs.Num(); }
	URAIMindViewRecorder* FindRecorder(const AActor* Subject) const;
	bool GetMergedDemand(const AActor* Subject, FRAIMindViewDemand& Out) const;

	/** Sections this subject can show, in registration order. */
	TArray<FName> GetSupportedSections(AActor* Subject) const;
	URAIMindViewSection* FindSection(FName Id) const;

	/** Capture immediately with the merged demand (or the given sections at Full detail when none is set). */
	bool CaptureNow(AActor* Subject, FRAIMindViewSnapshot& Out, const TArray<FName>* SectionsOverride = nullptr);

	/** Every mind in this world holding the given knowledge; Reference (optional) decides contradictions. */
	TArray<FRAIMindViewKnower> WhoKnows(const FRAIMindViewFactKey& Key, const AActor* Reference = nullptr);

	/** Snapshots for consumers; filter by Snapshot.Header.Subject. */
	FRAIMindViewSnapshotNative OnSnapshot;
	/** A demanded subject was destroyed or stopped being a subject; its demand is dropped. */
	FRAIMindViewSubjectLostNative OnSubjectLost;

	/** Routed from FRAIMindViewHooks. */
	void ReceiveThought(const AActor* Subject, const FRAIThoughtArgs& Args, const FString* LegacyText);

	UPROPERTY(EditAnywhere, Category = "RAI|MindView") float MaxCaptureMsPerFrame = 0.25f;
	UPROPERTY(EditAnywhere, Category = "RAI|MindView") int32 DetectorRepeatCount = 4;
	UPROPERTY(EditAnywhere, Category = "RAI|MindView") double DetectorWindowSeconds = 60.0;
	UPROPERTY(EditAnywhere, Category = "RAI|MindView") float WhoKnowsCacheSeconds = 1.f;

	// USubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FSubjectState
	{
		TWeakObjectPtr<AActor> Subject;
		TMap<FName, FRAIMindViewDemand> ByConsumer;
		FRAIMindViewDemand Merged;
		TWeakObjectPtr<URAIMindViewRecorder> Recorder;
		double NextCaptureRealTime = 0.0;
		uint32 Serial = 0;
	};

	void CreateSections();
	void AddSectionInstance(UClass* SectionClass);
	void BeginSubject(FSubjectState& State);
	void EndSubject(FSubjectState& State);
	void RefreshRecorder(FSubjectState& State);
	void UpdateActivation();
	void BuildContext(AActor* Subject, const URAIMindViewRecorder* Recorder, FRAIMindViewCaptureContext& Out) const;
	void Capture(FSubjectState& State, FRAIMindViewSnapshot& Out, const TArray<FName>* SectionsOverride);
	uint32 ComputeStateRevision(AActor* Subject) const;

	TMap<FObjectKey, FSubjectState> Subjects;
	TArray<FObjectKey> CaptureOrder;
	int32 CaptureCursor = 0;
	bool bActivated = false;

	UPROPERTY(Transient) TArray<TObjectPtr<URAIMindViewSection>> Sections;
	UPROPERTY(Transient) TArray<TObjectPtr<URAIMindViewRecorder>> RecorderRefs;

	struct FWhoKnowsCache { double RealTime = 0.0; TArray<FRAIMindViewKnower> Result; TWeakObjectPtr<const AActor> Reference; };
	TMap<FRAIMindViewFactKey, FWhoKnowsCache> WhoKnowsCache;

	static TArray<TSubclassOf<URAIMindViewSection>>& SectionClasses();
};
