// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "RAIMindViewTypes.h"
#include "UObject/Object.h"
#include "RAIMindViewRecorder.generated.h"

class APawn;
class URAIManagerComponent;
class URAIMemoryComponent;
class URAITaskComponent;
struct FRAIThoughtArgs;
struct FRAIArbitrationResult;
struct FRAIMemoryChange;

/** Fixed-capacity ring, newest last. Allocation happens once when the first item is pushed. */
template <typename T>
class TRAIMindViewRing
{
public:
	explicit TRAIMindViewRing(int32 InCapacity = 64) : Capacity(FMath::Max(InCapacity, 1)) {}
	void SetCapacity(int32 InCapacity) { Capacity = FMath::Max(InCapacity, 1); Reset(); }
	void Reset() { Items.Reset(); Head = 0; }
	int32 Num() const { return Items.Num(); }
	int32 GetCapacity() const { return Capacity; }
	void Push(T Item)
	{
		if (Items.Num() < Capacity) { Items.Add(MoveTemp(Item)); return; }
		Items[Head] = MoveTemp(Item);
		Head = (Head + 1) % Capacity;
	}
	/** Index 0 is the newest item. */
	const T& FromNewest(int32 Index) const { return Items[(Head + Items.Num() - 1 - Index) % Items.Num()]; }
	template <typename F> void ForEachNewest(int32 Max, F&& Func) const
	{
		const int32 Count = Max > 0 ? FMath::Min(Max, Items.Num()) : Items.Num();
		for (int32 Index = 0; Index < Count; ++Index) Func(FromNewest(Index));
	}
private:
	TArray<T> Items;
	int32 Head = 0;
	int32 Capacity;
};

/**
 * Records one subject's decision process while it is demanded by a MindView consumer.
 * It exists only between demand begin and end; history therefore starts when tracking starts.
 * Authority-side, write-only from the simulation's point of view.
 */
UCLASS(Transient)
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewRecorder : public UObject
{
	GENERATED_BODY()

public:
	void Start(AActor* InSubject);
	void Stop();
	/** Rebinds when the subject's driving manager or memory changed (possession, respawn of components). */
	void Refresh(URAIManagerComponent* InManager, URAIMemoryComponent* InMemory);

	void AddThought(const FRAIThoughtArgs& Args, const FString* LegacyText, double Now);

	AActor* GetSubject() const { return Subject.Get(); }
	URAIManagerComponent* GetManager() const { return Manager.Get(); }
	URAIMemoryComponent* GetMemory() const { return Memory.Get(); }

	const TRAIMindViewRing<FRAIThought>& GetThoughts() const { return Thoughts; }
	const TRAIMindViewRing<FRAIMindViewSwitch>& GetSwitches() const { return Switches; }
	const TRAIMindViewRing<FRAIMindViewLifecycleEvent>& GetLifecycle() const { return Lifecycle; }
	const TRAIMindViewRing<FRAIMindViewDetection>& GetDetections() const { return Detections; }
	const TRAIMindViewRing<FRAIMindViewMemoryChange>& GetMemoryChanges() const { return MemoryChanges; }
	ERAIArbitrationDecision GetLastDecision() const { return LastDecision; }
	float GetLastActiveRootPriority() const { return LastActiveRootPriority; }

	uint32 GetArbitrationSerial() const { return ArbitrationSerial; }
	uint32 GetLifecycleSerial() const { return LifecycleSerial; }
	uint32 GetThoughtSerial() const { return ThoughtSerial; }
	uint32 GetSwitchSerial() const { return SwitchSerial; }
	uint32 GetDetectorSerial() const { return DetectorSerial; }
	uint32 GetMemoryChangeSerial() const { return MemoryChangeSerial; }

	/** Detector configuration (copied from the subsystem). */
	int32 RepeatCount = 4;
	double RepeatWindowSeconds = 60.0;

	/** Supplies the subject's state revision for the repeat-without-change detector. */
	TFunction<uint32()> StateRevisionProvider;

private:
	void Bind();
	void Unbind();
	double Now() const;
	void HandleArbitration(const FRAIArbitrationResult& Result);
	void HandleTaskBegin(URAITaskComponent* Task);
	void HandleTaskEnd(URAITaskComponent* Task, bool bSuccess, FGameplayTag Reason, bool bInterrupted);
	void HandleMemoryChanged(const FRAIMemoryChange& Change);
	UFUNCTION() void HandleManagerLoop(URAITaskComponent* Task, int32 Count, double Window);
	static void ChainOf(const URAITaskComponent* Leaf, TArray<FName>& Out);
	void CheckRepeat(const TArray<FName>& Chain);

	TWeakObjectPtr<AActor> Subject;
	TWeakObjectPtr<URAIManagerComponent> Manager;
	TWeakObjectPtr<URAIMemoryComponent> Memory;
	FDelegateHandle ArbitrationHandle, BeginHandle, EndHandle, MemoryHandle;
	bool bRunning = false;

	TRAIMindViewRing<FRAIThought> Thoughts{200};
	TRAIMindViewRing<FRAIMindViewSwitch> Switches{100};
	TRAIMindViewRing<FRAIMindViewLifecycleEvent> Lifecycle{256};
	TRAIMindViewRing<FRAIMindViewDetection> Detections{32};
	TRAIMindViewRing<FRAIMindViewMemoryChange> MemoryChanges{100};

	struct FRepeatSample { uint32 ChainHash; double At; uint32 StateRevision; };
	TArray<FRepeatSample> RepeatSamples;
	TMap<uint32, double> LastReportedRepeat;
	/** Chain as of the latest begin (root first). */
	TArray<FName> CurrentChain;
	/** Deepest chain run under the current root, reported as the previous chain on the next switch. */
	TArray<FName> LastFullChain;
	/** A root that began inside arbitration, applied after the arbitration event. */
	TArray<FName> PendingRootChain;

	ERAIArbitrationDecision LastDecision = ERAIArbitrationDecision::NoCandidate;
	float LastActiveRootPriority = 0.f;
	uint32 ArbitrationSerial = 0, LifecycleSerial = 0, ThoughtSerial = 0, SwitchSerial = 0, DetectorSerial = 0, MemoryChangeSerial = 0;
};
