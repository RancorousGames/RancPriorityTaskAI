// Copyright Rancorous Games, 2026

#include "RAIMindViewSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Mind/RAIMemoryComponent.h"
#include "Mind/RAIMindComponent.h"
#include "RAIController.h"
#include "RAIManagerComponent.h"
#include "RAIMindViewHooks.h"
#include "RAIMindViewRecorder.h"
#include "RAIMindViewSection.h"
#include "RAIMindViewTags.h"
#include "RAITaskComponent.h"
#include "UObject/UObjectIterator.h"

TArray<TSubclassOf<URAIMindViewSection>>& URAIMindViewSubsystem::SectionClasses()
{
	static TArray<TSubclassOf<URAIMindViewSection>> Classes;
	return Classes;
}

URAIMindViewSubsystem* URAIMindViewSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URAIMindViewSubsystem>() : nullptr;
}

void URAIMindViewSubsystem::RegisterSectionClass(TSubclassOf<URAIMindViewSection> SectionClass)
{
	if (!SectionClass || SectionClasses().Contains(SectionClass)) return;
	SectionClasses().Add(SectionClass);
	for (TObjectIterator<URAIMindViewSubsystem> It; It; ++It)
		if (!It->IsTemplate() && It->IsInitialized()) It->AddSectionInstance(SectionClass);
}

void URAIMindViewSubsystem::UnregisterSectionClass(TSubclassOf<URAIMindViewSection> SectionClass)
{
	SectionClasses().Remove(SectionClass);
	for (TObjectIterator<URAIMindViewSubsystem> It; It; ++It)
		It->Sections.RemoveAll([SectionClass](const URAIMindViewSection* Section) { return !Section || Section->GetClass() == SectionClass; });
}

bool URAIMindViewSubsystem::IsSubject(const AActor* Actor)
{
	return IsValid(Actor) && (ResolveManager(Actor) || ResolveMemory(Actor) || Actor->FindComponentByClass<URAIMindComponent>());
}

URAIManagerComponent* URAIMindViewSubsystem::ResolveManager(const AActor* Subject)
{
	const APawn* Pawn = Cast<APawn>(Subject);
	const ARAIController* Controller = Pawn ? Cast<ARAIController>(Pawn->GetController()) : nullptr;
	URAIManagerComponent* Manager = Controller ? Controller->ManagerComponent : nullptr;
	return Manager && Manager->GetControlledPawn() == Pawn ? Manager : nullptr;
}

URAIMemoryComponent* URAIMindViewSubsystem::ResolveMemory(const AActor* Subject)
{
	if (!Subject) return nullptr;
	if (URAIMemoryComponent* Memory = Subject->FindComponentByClass<URAIMemoryComponent>()) return Memory;
	const APawn* Pawn = Cast<APawn>(Subject);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	return Controller ? Controller->FindComponentByClass<URAIMemoryComponent>() : nullptr;
}

void URAIMindViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CreateSections();
}

void URAIMindViewSubsystem::Deinitialize()
{
	for (auto& Pair : Subjects) EndSubject(Pair.Value);
	Subjects.Reset();
	CaptureOrder.Reset();
	UpdateActivation();
	Sections.Reset();
	WhoKnowsCache.Reset();
	Super::Deinitialize();
}

bool URAIMindViewSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ETickableTickType URAIMindViewSubsystem::GetTickableTickType() const
{
	if (IsTemplate() || !IsInitialized() || Subjects.IsEmpty()) return ETickableTickType::Never;
	return ETickableTickType::Always;
}

TStatId URAIMindViewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URAIMindViewSubsystem, STATGROUP_Tickables);
}

void URAIMindViewSubsystem::CreateSections()
{
	Sections.Reset();
	for (UClass* Class : {URAIMindViewSection_Decisions::StaticClass(), URAIMindViewSection_Chain::StaticClass(),
		URAIMindViewSection_History::StaticClass(), URAIMindViewSection_Thoughts::StaticClass(),
		URAIMindViewSection_Detectors::StaticClass(), URAIMindViewSection_Memory::StaticClass()})
		AddSectionInstance(Class);
	for (const TSubclassOf<URAIMindViewSection>& Class : SectionClasses()) AddSectionInstance(Class);
}

void URAIMindViewSubsystem::AddSectionInstance(UClass* SectionClass)
{
	if (!SectionClass || SectionClass->HasAnyClassFlags(CLASS_Abstract)) return;
	if (Sections.ContainsByPredicate([SectionClass](const URAIMindViewSection* S) { return S && S->GetClass() == SectionClass; })) return;
	URAIMindViewSection* Section = NewObject<URAIMindViewSection>(this, SectionClass);
	// A later registration with the same id replaces the earlier provider (games may override generic sections).
	Sections.RemoveAll([Section](const URAIMindViewSection* S) { return !S || S->GetSectionId() == Section->GetSectionId(); });
	Sections.Add(Section);
}

URAIMindViewSection* URAIMindViewSubsystem::FindSection(FName Id) const
{
	for (URAIMindViewSection* Section : Sections) if (Section && Section->GetSectionId() == Id) return Section;
	return nullptr;
}

void URAIMindViewSubsystem::SetDemand(AActor* Subject, FName Consumer, const FRAIMindViewDemand& Demand)
{
	if (!Subject || Consumer.IsNone()) return;
	const FObjectKey Key(Subject);
	FSubjectState* State = Subjects.Find(Key);
	if (Demand.IsEmpty())
	{
		if (!State || !State->ByConsumer.Remove(Consumer)) return;
	}
	else
	{
		if (!IsSubject(Subject)) return;
		if (!State)
		{
			State = &Subjects.Add(Key);
			State->Subject = Subject;
			CaptureOrder.Add(Key);
		}
		State->ByConsumer.Add(Consumer, Demand);
	}

	FRAIMindViewDemand Merged;
	Merged.RateHz = 0.f;
	Merged.CompactCount = 0;
	for (const auto& Pair : State->ByConsumer) Merged.MergeFrom(Pair.Value);
	State->Merged = Merged;
	State->NextCaptureRealTime = 0.0;

	if (State->ByConsumer.IsEmpty())
	{
		EndSubject(*State);
		Subjects.Remove(Key);
		CaptureOrder.Remove(Key);
	}
	else if (!State->Recorder.IsValid())
	{
		BeginSubject(*State);
	}
	UpdateActivation();
}

void URAIMindViewSubsystem::ClearConsumer(FName Consumer)
{
	TArray<AActor*> Affected;
	for (const auto& Pair : Subjects)
		if (Pair.Value.ByConsumer.Contains(Consumer))
			if (AActor* Subject = Pair.Value.Subject.Get()) Affected.Add(Subject);
	for (AActor* Subject : Affected) SetDemand(Subject, Consumer, FRAIMindViewDemand());
	// Subjects that died while demanded are removed by the next tick.
}

URAIMindViewRecorder* URAIMindViewSubsystem::FindRecorder(const AActor* Subject) const
{
	const FSubjectState* State = Subject ? Subjects.Find(FObjectKey(Subject)) : nullptr;
	return State ? State->Recorder.Get() : nullptr;
}

bool URAIMindViewSubsystem::GetMergedDemand(const AActor* Subject, FRAIMindViewDemand& Out) const
{
	const FSubjectState* State = Subject ? Subjects.Find(FObjectKey(Subject)) : nullptr;
	if (!State) return false;
	Out = State->Merged;
	return true;
}

void URAIMindViewSubsystem::BeginSubject(FSubjectState& State)
{
	AActor* Subject = State.Subject.Get();
	URAIMindViewRecorder* Recorder = NewObject<URAIMindViewRecorder>(this);
	RecorderRefs.Add(Recorder);
	State.Recorder = Recorder;
	Recorder->RepeatCount = DetectorRepeatCount;
	Recorder->RepeatWindowSeconds = DetectorWindowSeconds;
	Recorder->StateRevisionProvider = [WeakThis = TWeakObjectPtr<URAIMindViewSubsystem>(this), WeakSubject = State.Subject]()
	{
		URAIMindViewSubsystem* Self = WeakThis.Get();
		return Self ? Self->ComputeStateRevision(WeakSubject.Get()) : 0u;
	};
	Recorder->Start(Subject);
	Recorder->Refresh(ResolveManager(Subject), ResolveMemory(Subject));
	for (URAIMindViewSection* Section : Sections) if (Section) Section->OnDemandBegin(Subject);
}

void URAIMindViewSubsystem::EndSubject(FSubjectState& State)
{
	AActor* Subject = State.Subject.Get();
	for (URAIMindViewSection* Section : Sections) if (Section) Section->OnDemandEnd(Subject);
	if (URAIMindViewRecorder* Recorder = State.Recorder.Get())
	{
		Recorder->Stop();
		RecorderRefs.Remove(Recorder);
	}
	State.Recorder.Reset();
}

void URAIMindViewSubsystem::RefreshRecorder(FSubjectState& State)
{
	if (URAIMindViewRecorder* Recorder = State.Recorder.Get())
	{
		AActor* Subject = State.Subject.Get();
		Recorder->Refresh(ResolveManager(Subject), ResolveMemory(Subject));
	}
}

void URAIMindViewSubsystem::UpdateActivation()
{
#if !UE_BUILD_SHIPPING
	const bool bWant = !Subjects.IsEmpty();
	if (bWant != bActivated)
	{
		bActivated = bWant;
		if (bWant) FRAIMindViewHooks::AddActivation();
		else FRAIMindViewHooks::RemoveActivation();
	}
#endif
	if (IsInitialized()) SetTickableTickType(GetTickableTickType());
}

uint32 URAIMindViewSubsystem::ComputeStateRevision(AActor* Subject) const
{
	if (!Subject) return 0;
	FRAIMindViewCaptureContext Context;
	BuildContext(Subject, FindRecorder(Subject), Context);
	uint32 Revision = 0;
	for (const URAIMindViewSection* Section : Sections)
		if (Section && Section->Supports(Context)) Revision = HashCombine(Revision, Section->GetStateRevision(Context));
	return Revision;
}

void URAIMindViewSubsystem::BuildContext(AActor* Subject, const URAIMindViewRecorder* Recorder, FRAIMindViewCaptureContext& Out) const
{
	Out.Subject = Subject;
	Out.Pawn = Cast<APawn>(Subject);
	Out.Manager = ResolveManager(Subject);
	Out.Memory = ResolveMemory(Subject);
	Out.Recorder = Recorder;
	Out.Now = Out.Manager ? Out.Manager->GetNow() : (Out.Memory ? Out.Memory->GetNow() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0));
}

void URAIMindViewSubsystem::Capture(FSubjectState& State, FRAIMindViewSnapshot& Out, const TArray<FName>* SectionsOverride)
{
	AActor* Subject = State.Subject.Get();
	const URAIMindViewRecorder* Recorder = State.Recorder.Get();
	FRAIMindViewCaptureContext Context;
	BuildContext(Subject, Recorder, Context);
	Context.Count = FMath::Max(State.Merged.CompactCount, 1);

	Out = FRAIMindViewSnapshot();
	Out.CapturedAt = Context.Now;
	Out.Serial = ++State.Serial;
	FRAIMindViewHeader& Header = Out.Header;
	Header.Subject = Subject;
#if WITH_EDITOR
	Header.DisplayName = FText::FromString(Subject->GetActorLabel());
#else
	Header.DisplayName = FText::FromString(Subject->GetName());
#endif
	Header.Kind = RAIMindViewTags::Kind_Default;
	const APawn* Pawn = Context.Pawn;
	Header.ControlMode = Pawn && Pawn->IsPlayerControlled() ? ERAIMindViewControlMode::Player
		: (Context.Manager ? ERAIMindViewControlMode::AI : ERAIMindViewControlMode::None);
	if (Context.Manager)
		if (const URAITaskComponent* Leaf = Context.Manager->GetActiveTask())
		{
			Header.Leaf = Leaf->GetFName();
			Header.Root = Leaf->GetRootTask()->GetFName();
		}
	if (Recorder && Recorder->GetDetections().Num() > 0)
		if (const URAIMindViewSection_Detectors* Detectors = Cast<URAIMindViewSection_Detectors>(FindSection(RAIMindViewSections::Detectors)))
			Header.bHasDetector = Context.Now - Recorder->GetDetections().FromNewest(0).At <= Detectors->ActiveSeconds;
	if (DescribeSubject) DescribeSubject(Subject, Header);

	for (const URAIMindViewSection* Section : Sections)
	{
		if (!Section) continue;
		const FName Id = Section->GetSectionId();
		if (SectionsOverride) { if (!SectionsOverride->Contains(Id)) continue; Context.Detail = ERAIMindViewDetail::Full; }
		else if (State.Merged.FullSections.Contains(Id)) Context.Detail = ERAIMindViewDetail::Full;
		else if (State.Merged.CompactSections.Contains(Id)) Context.Detail = ERAIMindViewDetail::Compact;
		else continue;
		if (!Section->Supports(Context)) continue;
		FRAIMindViewSectionData& Data = Out.Sections.AddDefaulted_GetRef();
		Data.Id = Id;
		Data.Truth = Section->GetTruth();
		Data.Detail = Context.Detail;
		Section->Capture(Context, Data);
	}
}

bool URAIMindViewSubsystem::CaptureNow(AActor* Subject, FRAIMindViewSnapshot& Out, const TArray<FName>* SectionsOverride)
{
	if (!IsSubject(Subject)) return false;
	if (FSubjectState* State = Subjects.Find(FObjectKey(Subject)))
	{
		RefreshRecorder(*State);
		Capture(*State, Out, SectionsOverride);
		return true;
	}
	// Not demanded: capture without recorder history.
	FSubjectState Temporary;
	Temporary.Subject = Subject;
	TArray<FName> All;
	if (!SectionsOverride) { All = GetSupportedSections(Subject); SectionsOverride = &All; }
	Capture(Temporary, Out, SectionsOverride);
	return true;
}

TArray<FName> URAIMindViewSubsystem::GetSupportedSections(AActor* Subject) const
{
	TArray<FName> Result;
	if (!IsSubject(Subject)) return Result;
	FRAIMindViewCaptureContext Context;
	BuildContext(Subject, FindRecorder(Subject), Context);
	for (const URAIMindViewSection* Section : Sections)
		if (Section && Section->Supports(Context)) Result.Add(Section->GetSectionId());
	return Result;
}

void URAIMindViewSubsystem::ReceiveThought(const AActor* Subject, const FRAIThoughtArgs& Args, const FString* LegacyText)
{
	if (URAIMindViewRecorder* Recorder = FindRecorder(Subject))
	{
		FRAIMindViewCaptureContext Context;
		BuildContext(const_cast<AActor*>(Subject), Recorder, Context);
		Recorder->AddThought(Args, LegacyText, Context.Now);
	}
}

void URAIMindViewSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Drop subjects that no longer exist (destroyed pawn, removed mind).
	TArray<FObjectKey> Lost;
	for (auto& Pair : Subjects)
		if (!IsSubject(Pair.Value.Subject.Get())) Lost.Add(Pair.Key);
	for (const FObjectKey& Key : Lost)
	{
		if (FSubjectState* State = Subjects.Find(Key)) EndSubject(*State);
		Subjects.Remove(Key);
		CaptureOrder.Remove(Key);
	}
	if (!Lost.IsEmpty())
	{
		UpdateActivation();
		for (const FObjectKey& Key : Lost) OnSubjectLost.Broadcast(Key);
	}
	if (CaptureOrder.IsEmpty()) return;

	// Captures are budgeted and round-robin; snapshots are broadcast after the loop so consumers may change demand.
	const double Start = FPlatformTime::Seconds();
	const double Budget = FMath::Max(MaxCaptureMsPerFrame, 0.01f) / 1000.0;
	TArray<FRAIMindViewSnapshot, TInlineAllocator<4>> Ready;
	const int32 Count = CaptureOrder.Num();
	int32 Visited = 0;
	for (; Visited < Count; ++Visited)
	{
		FSubjectState* State = Subjects.Find(CaptureOrder[(CaptureCursor + Visited) % Count]);
		if (!State) continue;
		RefreshRecorder(*State);
		const double Now = FPlatformTime::Seconds();
		if (Now < State->NextCaptureRealTime) continue;
		State->NextCaptureRealTime = Now + 1.0 / FMath::Max(State->Merged.RateHz, 0.1f);
		Capture(*State, Ready.AddDefaulted_GetRef(), nullptr);
		if (FPlatformTime::Seconds() - Start > Budget) { ++Visited; break; }
	}
	CaptureCursor = Count ? (CaptureCursor + Visited) % Count : 0;
	for (const FRAIMindViewSnapshot& Snapshot : Ready) OnSnapshot.Broadcast(Snapshot);
}

TArray<FRAIMindViewKnower> URAIMindViewSubsystem::WhoKnows(const FRAIMindViewFactKey& Key, const AActor* Reference)
{
	if (!Key.IsValid()) return {};
	const double Real = FPlatformTime::Seconds();
	if (const FWhoKnowsCache* Cached = WhoKnowsCache.Find(Key); Cached && Real - Cached->RealTime <= WhoKnowsCacheSeconds && Cached->Reference.Get() == Reference)
		return Cached->Result;

	struct FVersion { FRAIMindViewKnower Knower; uint32 Signature = 0; const FInstancedStruct* Value = nullptr; };
	TArray<FVersion> Versions;
	UWorld* World = GetWorld();
	for (TObjectIterator<URAIMemoryComponent> It; It; ++It)
	{
		const URAIMemoryComponent* Memory = *It;
		if (!IsValid(Memory) || Memory->IsTemplate() || Memory->GetWorld() != World) continue;
		AActor* Holder = Memory->GetOwner();
		if (const AController* Controller = Cast<AController>(Holder)) Holder = Controller->GetPawn() ? Controller->GetPawn() : Holder;
		if (Key.IsEpisode())
		{
			const FRAIEpisodicMemory* Newest = nullptr;
			for (const FRAIEpisodicMemory& M : Memory->Episodic)
				if (M.Event.OriginId == Key.OriginId && (!Newest || M.Event.WorldTime >= Newest->Event.WorldTime)) Newest = &M;
			if (!Newest) continue;
			FVersion& Version = Versions.AddDefaulted_GetRef();
			Version.Knower.Holder = Holder;
			Version.Knower.bFirstHand = Newest->Event.IsDirectExperience();
			Version.Knower.Source = Newest->Event.Source;
			Version.Knower.Credibility = Newest->Event.SourceCredibility;
			Version.Knower.LearnedAt = Newest->Event.WorldTime;
			Version.Signature = HashCombine(HashCombine(GetTypeHash(Newest->Event.Action), GetTypeHash(Newest->Event.Kind)),
				HashCombine(GetTypeHash(Newest->Event.Actor), GetTypeHash(Newest->Event.Target)));
		}
		else
		{
			for (const FRAISemanticFact& Fact : Memory->Semantic)
			{
				if (Fact.Subject != Key.Subject || Fact.Predicate != Key.Predicate) continue;
				FVersion& Version = Versions.AddDefaulted_GetRef();
				Version.Knower.Holder = Holder;
				Version.Knower.bFirstHand = !Fact.LearnedFrom.IsValid();
				Version.Knower.Source = Fact.LearnedFrom;
				Version.Knower.Credibility = Fact.Confidence;
				Version.Value = &Fact.Value;
				break;
			}
		}
	}

	auto SameVersion = [&Key](const FVersion& A, const FVersion& B)
	{
		if (Key.IsEpisode()) return A.Signature == B.Signature;
		if (!A.Value || !B.Value) return A.Value == B.Value;
		return A.Value->Identical(B.Value, PPF_None);
	};
	const FVersion* ReferenceVersion = Versions.FindByPredicate([Reference](const FVersion& V) { return Reference && V.Knower.Holder.Get() == Reference; });
	if (!ReferenceVersion) ReferenceVersion = Versions.FindByPredicate([](const FVersion& V) { return V.Knower.bFirstHand; });
	if (!ReferenceVersion && !Versions.IsEmpty()) ReferenceVersion = &Versions[0];

	TArray<FRAIMindViewKnower> Result;
	for (const FVersion& Version : Versions)
	{
		FRAIMindViewKnower& Knower = Result.Add_GetRef(Version.Knower);
		Knower.bContradicts = ReferenceVersion && !SameVersion(Version, *ReferenceVersion);
	}
	Result.StableSort([](const FRAIMindViewKnower& A, const FRAIMindViewKnower& B)
	{
		return A.bFirstHand != B.bFirstHand ? A.bFirstHand : A.LearnedAt < B.LearnedAt;
	});
	FWhoKnowsCache& Cache = WhoKnowsCache.Add(Key);
	Cache.RealTime = Real;
	Cache.Result = Result;
	Cache.Reference = Reference;
	return Result;
}
