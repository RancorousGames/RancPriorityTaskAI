// Copyright Rancorous Games, 2026

#include "Tests/RAIMindViewTestTypes.h"

#include "RAIManagerComponent.h"
#include "RAIMindViewHooks.h"
#include "RAIMindViewRecorder.h"
#include "RAIMindViewSection.h"
#include "RAIMindViewSession.h"
#include "RAIMindViewSubsystem.h"
#include "RAIMindViewTrackRule.h"
#include "RAIScheduling.h"
#include "RAITags.h"
#include "Mind/RAIMemoryComponent.h"
#include "Mind/RAIMindComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

ARAIMindViewTestController::ARAIMindViewTestController(const FObjectInitializer& Initializer) : Super(Initializer)
{
	AutoHandleSensoryInput = false;
	ManagerComponent = CreateDefaultSubobject<URAIManagerComponent>(TEXT("Manager"));
	CreateDefaultSubobject<URAIMindViewTestTask>(TEXT("TaskA"));
	CreateDefaultSubobject<URAIMindViewTestTaskB>(TEXT("TaskB"))->Score = 0.f;
	CreateDefaultSubobject<URAIMindViewTestChild>(TEXT("Child"))->IsPrimaryTask = false;
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace
{
struct FMindViewTestServices : IRAITimeSource, IRAIScheduler
{
	double Time = 0.0;
	uint64 NextId = 1;
	struct FPending { TWeakObjectPtr<UObject> Owner; double Due; TFunction<void()> Callback; };
	TMap<uint64, FPending> Pending;
	virtual double Now() const override { return Time; }
	virtual FRAIScheduleHandle ScheduleOnce(UObject* Owner, double Delay, TFunction<void()> Callback) override
	{
		const uint64 Id = NextId++;
		Pending.Add(Id, {Owner, Time + Delay, MoveTemp(Callback)});
		return {Id};
	}
	virtual void Cancel(FRAIScheduleHandle& Handle) override { Pending.Remove(Handle.Id); Handle.Invalidate(); }
	virtual void CancelAll(const UObject* Owner) override
	{
		for (auto It = Pending.CreateIterator(); It; ++It) if (It.Value().Owner.Get() == Owner) It.RemoveCurrent();
	}
	void Advance(double Seconds)
	{
		Time += Seconds;
		TArray<uint64> Due;
		for (const auto& Pair : Pending) if (Pair.Value.Due <= Time) Due.Add(Pair.Key);
		Due.Sort();
		for (uint64 Id : Due) { FPending Entry; if (Pending.RemoveAndCopyValue(Id, Entry) && Entry.Owner.IsValid()) Entry.Callback(); }
	}
};

struct FMindViewFixture
{
	TStrongObjectPtr<UWorld> World;
	TStrongObjectPtr<UGameInstance> GameInstance;
	ARAIMindViewTestController* Controller = nullptr;
	URAIManagerComponent* Manager = nullptr;
	URAIMindViewTestTask* TaskA = nullptr;
	URAIMindViewTestTaskB* TaskB = nullptr;
	URAIMindViewTestChild* Child = nullptr;
	APawn* Pawn = nullptr;
	URAIMindViewSubsystem* MindView = nullptr;
	TSharedPtr<FMindViewTestServices> Clock;

	FMindViewFixture()
		: World(UWorld::CreateWorld(EWorldType::Game, false))
		, GameInstance(NewObject<UGameInstance>(World.Get()))
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
		World->SetGameInstance(GameInstance.Get());
		GameInstance->Init();
		World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Controller = World->SpawnActor<ARAIMindViewTestController>();
		Manager = Controller->ManagerComponent;
		TaskA = Controller->FindComponentByClass<URAIMindViewTestTask>();
		TaskB = Controller->FindComponentByClass<URAIMindViewTestTaskB>();
		Child = Controller->FindComponentByClass<URAIMindViewTestChild>();
		World->BeginPlay();
		Pawn = World->SpawnActor<ACharacter>();
		Controller->Possess(Pawn);
		MindView = World->GetSubsystem<URAIMindViewSubsystem>();
		Clock = MakeShared<FMindViewTestServices>();
		Manager->SetServices(Clock, Clock);
		Manager->Initialize(Controller, Pawn);
	}
	~FMindViewFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		GameInstance->Shutdown();
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World.Get());
	}
	void Arbitrate() { Manager->UpdateActiveTasks(); }
	void Tick(int32 Frames = 1) { for (int32 Index = 0; Index < Frames; ++Index) World->Tick(LEVELTICK_All, 0.05f); }
	FRAIMindViewDemand Demand(std::initializer_list<FName> Full, float Rate = 60.f) const
	{
		FRAIMindViewDemand Result;
		for (const FName& Id : Full) Result.FullSections.Add(Id);
		Result.RateHz = Rate;
		return Result;
	}
	FRAIMindViewSnapshot Capture()
	{
		FRAIMindViewSnapshot Snapshot;
		MindView->CaptureNow(Pawn, Snapshot);
		return Snapshot;
	}
	URAIMemoryComponent* AddMind(APawn* Target)
	{
		URAIMemoryComponent* Memory = NewObject<URAIMemoryComponent>(Target);
		Target->AddInstanceComponent(Memory);
		Memory->RegisterComponent();
		URAIMindComponent* Mind = NewObject<URAIMindComponent>(Target);
		Target->AddInstanceComponent(Mind);
		// Registering on an actor that has begun play begins the component; memory first so the mind resolves it.
		Mind->RegisterComponent();
		return Memory;
	}
};
}

#define RAI_MINDVIEW_TEST(Class, Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "RancPriorityTaskAI.MindView." Name, EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter) \
	bool Class::RunTest(const FString& Parameters)

RAI_MINDVIEW_TEST(FRAIMindViewZeroCostTest, "ZeroCostWhenIdle")
{
	FMindViewFixture F;
	TestNotNull(TEXT("subsystem exists in game worlds"), F.MindView);
	if (!F.MindView) return false;
	F.Arbitrate();
	F.Tick(4);
	int32 Evaluations = 0;
	auto Count = [&Evaluations] { ++Evaluations; return 1.f; };
	RAI_THOUGHT(F.Pawn, RAITags::Thought_Legacy, ERAIThoughtTone::Neutral, nullptr, nullptr, Count());
	F.Controller->TraceThought(TEXT("legacy thought while idle"));
	TestFalse(TEXT("thought hook inactive"), FRAIMindViewHooks::IsActive());
	TestEqual(TEXT("thought arguments not evaluated"), Evaluations, 0);
	TestFalse(TEXT("no demand"), F.MindView->HasAnyDemand());
	TestEqual(TEXT("no recorders"), F.MindView->GetRecorderCount(), 0);
	TestFalse(TEXT("no explanation capture"), F.Manager->IsCapturingExplanations());
	TestFalse(TEXT("trace ring idle"), F.Manager->IsRecordingTrace());
	TestTrue(TEXT("trace ring empty"), F.Manager->GetTrace().IsEmpty());
	TestFalse(TEXT("no arbitration listener"), F.Manager->OnArbitrationNative.IsBound());
	TestFalse(TEXT("no begin listener"), F.Manager->OnTaskBeginNative.IsBound());
	TestEqual(TEXT("subsystem does not tick"), F.MindView->GetTickableTickType(), ETickableTickType::Never);
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewDemandTest, "DemandMergeAndTeardown")
{
	FMindViewFixture F;
	F.MindView->SetDemand(F.Pawn, TEXT("Panel"), F.Demand({RAIMindViewSections::Decisions}, 4.f));
	FRAIMindViewDemand Card;
	Card.CompactSections.Add(RAIMindViewSections::Thoughts);
	Card.RateHz = 2.f;
	Card.CompactCount = 7;
	F.MindView->SetDemand(F.Pawn, TEXT("Cards"), Card);
	FRAIMindViewDemand Merged;
	TestTrue(TEXT("merged demand exists"), F.MindView->GetMergedDemand(F.Pawn, Merged));
	TestTrue(TEXT("full sections merged"), Merged.FullSections.Contains(RAIMindViewSections::Decisions));
	TestTrue(TEXT("compact sections merged"), Merged.CompactSections.Contains(RAIMindViewSections::Thoughts));
	TestEqual(TEXT("fastest rate wins"), Merged.RateHz, 4.f);
	TestEqual(TEXT("largest count wins"), Merged.CompactCount, 7);
	TestEqual(TEXT("one recorder per subject"), F.MindView->GetRecorderCount(), 1);
	TestTrue(TEXT("hook active"), FRAIMindViewHooks::IsActive());
	TestTrue(TEXT("explanations demanded"), F.Manager->IsCapturingExplanations());
	TestTrue(TEXT("subsystem ticks"), F.MindView->GetTickableTickType() == ETickableTickType::Always);

	F.MindView->SetDemand(F.Pawn, TEXT("Panel"), FRAIMindViewDemand());
	TestEqual(TEXT("other consumer keeps the recorder"), F.MindView->GetRecorderCount(), 1);
	F.MindView->SetDemand(F.Pawn, TEXT("Cards"), FRAIMindViewDemand());
	TestEqual(TEXT("recorder released"), F.MindView->GetRecorderCount(), 0);
	TestFalse(TEXT("hook inactive again"), FRAIMindViewHooks::IsActive());
	TestFalse(TEXT("explanation demand released"), F.Manager->IsCapturingExplanations());
	TestFalse(TEXT("arbitration listener removed"), F.Manager->OnArbitrationNative.IsBound());
	TestEqual(TEXT("subsystem stops ticking"), F.MindView->GetTickableTickType(), ETickableTickType::Never);
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewSwitchTest, "SwitchCarriesTermsAndThought")
{
	FMindViewFixture F;
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::History, RAIMindViewSections::Thoughts, RAIMindViewSections::Decisions}));
	F.Arbitrate();
	F.TaskB->Score = 100.f;
	F.Arbitrate();
	TestTrue(TEXT("B took over"), F.TaskB->IsTaskActive);

	const FRAIMindViewSnapshot Snapshot = F.Capture();
	const FRAIMindViewHistory* History = Snapshot.GetSection<FRAIMindViewHistory>(RAIMindViewSections::History);
	TestNotNull(TEXT("history captured"), History);
	if (!History || History->Switches.Num() < 2) { AddError(TEXT("expected two switches")); return false; }
	const FRAIMindViewSwitch& Latest = History->Switches[0];
	TestEqual(TEXT("switched to B"), Latest.To, F.TaskB->GetFName());
	TestEqual(TEXT("switched from A"), Latest.From, F.TaskA->GetFName());
	TestEqual(TEXT("decision is an interruption"), Latest.Decision, ERAIArbitrationDecision::Interrupted);
	TestTrue(TEXT("winner terms recorded"), Latest.ToTerms.Num() == 1 && FMath::IsNearlyEqual(Latest.ToTerms[0].Value, 100.f));
	TestTrue(TEXT("previous root terms recorded"), Latest.FromTerms.Num() == 1 && FMath::IsNearlyEqual(Latest.FromTerms[0].Value, 10.f));
	TestTrue(TEXT("previous chain recorded"), Latest.PreviousChain.Num() == 1 && Latest.PreviousChain[0] == F.TaskA->GetFName());
	TestEqual(TEXT("idle start has no previous root"), History->Switches[1].From, FName());

	const FRAIMindViewThoughts* Thoughts = Snapshot.GetSection<FRAIMindViewThoughts>(RAIMindViewSections::Thoughts);
	TestTrue(TEXT("switch narrated as a thought"), Thoughts && Thoughts->Thoughts.Num() >= 1
		&& Thoughts->Thoughts[0].Kind == RAITags::Thought_Switch && Thoughts->Thoughts[0].P == F.TaskB->GetFName());

	const FRAIMindViewDecisions* Decisions = Snapshot.GetSection<FRAIMindViewDecisions>(RAIMindViewSections::Decisions);
	TestTrue(TEXT("decisions ranked with terms"), Decisions && Decisions->Candidates.Num() == 2
		&& Decisions->Candidates[0].Task == F.TaskB->GetFName() && Decisions->Candidates[0].bWinner
		&& Decisions->Candidates[0].bActiveRoot && Decisions->Candidates[0].Terms.Num() == 1);
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewThoughtTest, "ThoughtsRecordedOnlyWhileDemanded")
{
	FMindViewFixture F;
	int32 Evaluations = 0;
	auto Count = [&Evaluations] { ++Evaluations; return 2.f; };
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Thoughts}));
	RAI_THOUGHT(F.Pawn, RAITags::Thought_Legacy, ERAIThoughtTone::Urgent, F.Pawn, nullptr, Count());
	F.Controller->TraceThought(TEXT("free text"));
	TestEqual(TEXT("arguments evaluated once while active"), Evaluations, 1);
	const FRAIMindViewSnapshot Snapshot = F.Capture();
	const FRAIMindViewThoughts* Thoughts = Snapshot.GetSection<FRAIMindViewThoughts>(RAIMindViewSections::Thoughts);
	TestTrue(TEXT("two thoughts newest first"), Thoughts && Thoughts->Thoughts.Num() == 2);
	if (Thoughts && Thoughts->Thoughts.Num() == 2)
	{
		TestEqual(TEXT("legacy text kept"), Thoughts->Thoughts[0].Legacy, FString(TEXT("free text")));
		TestTrue(TEXT("typed thought kept"), Thoughts->Thoughts[1].Tone == ERAIThoughtTone::Urgent && Thoughts->Thoughts[1].V0 == 2.f
			&& Thoughts->Thoughts[1].A.Get() == F.Pawn);
	}
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), FRAIMindViewDemand());
	RAI_THOUGHT(F.Pawn, RAITags::Thought_Legacy, ERAIThoughtTone::Neutral, nullptr, nullptr, Count());
	TestEqual(TEXT("not evaluated after demand ends"), Evaluations, 1);
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewDetectorTest, "RepeatWithoutChangeDetector")
{
	FMindViewFixture F;
	URAIMemoryComponent* Memory = F.AddMind(F.Pawn);
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Detectors}));
	auto Repeat = [&F]
	{
		F.Arbitrate();
		F.TaskA->EndTask(false);
		F.Clock->Advance(2.0);
	};
	for (int32 Index = 0; Index < 3; ++Index) Repeat();
	const FRAIMindViewDetectors* Before = F.Capture().GetSection<FRAIMindViewDetectors>(RAIMindViewSections::Detectors);
	TestTrue(TEXT("three repeats are not yet suspicious"), Before && Before->Detections.IsEmpty());
	Repeat();
	FRAIMindViewSnapshot Snapshot = F.Capture();
	const FRAIMindViewDetectors* After = Snapshot.GetSection<FRAIMindViewDetectors>(RAIMindViewSections::Detectors);
	TestTrue(TEXT("fourth identical repeat detected"), After && After->Detections.Num() == 1 && After->bActive
		&& After->Detections[0].Kind == ERAIMindViewDetectorKind::RepeatWithoutChange && After->Detections[0].Count == 4);
	TestTrue(TEXT("header flag raised"), Snapshot.Header.bHasDetector);

	// A changing state (new memories) makes repetition legitimate.
	F.TaskB->Score = 20.f;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FRAILifeEvent Event;
		Event.BaselineArousal = 0.5f;
		Memory->EncodeEpisodic(Event);
		F.Arbitrate();
		F.TaskB->EndTask(false);
		F.Clock->Advance(2.0);
	}
	const FRAIMindViewDetectors* Changing = F.Capture().GetSection<FRAIMindViewDetectors>(RAIMindViewSections::Detectors);
	TestTrue(TEXT("repeats with state change are not flagged"), Changing && Changing->Detections.Num() == 1);
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), FRAIMindViewDemand());
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewWhoKnowsTest, "WhoKnowsGroupsByOrigin")
{
	FMindViewFixture F;
	APawn* Alice = F.World->SpawnActor<ACharacter>();
	APawn* Bob = F.World->SpawnActor<ACharacter>();
	APawn* Cara = F.World->SpawnActor<ACharacter>();
	APawn* Victim = F.World->SpawnActor<ACharacter>();
	APawn* Other = F.World->SpawnActor<ACharacter>();
	URAIMemoryComponent* A = F.AddMind(Alice);
	URAIMemoryComponent* B = F.AddMind(Bob);
	URAIMemoryComponent* C = F.AddMind(Cara);
	F.AddMind(Other);

	FRAILifeEvent Seen;
	Seen.OriginId = FGuid::NewGuid();
	Seen.Actor = Alice;
	Seen.Target = Victim;
	Seen.WorldTime = 1.0;
	A->EncodeEpisodic(Seen);
	FRAILifeEvent Told = Seen;
	Told.Source = Alice;
	Told.SourceCredibility = 0.8f;
	Told.WorldTime = 2.0;
	B->EncodeEpisodic(Told);
	FRAILifeEvent Garbled = Told;
	Garbled.Source = Bob;
	Garbled.Target = Other;
	Garbled.WorldTime = 3.0;
	C->EncodeEpisodic(Garbled);

	FRAIMindViewFactKey Key;
	Key.OriginId = Seen.OriginId;
	const TArray<FRAIMindViewKnower> Knowers = F.MindView->WhoKnows(Key);
	TestEqual(TEXT("three holders, unrelated mind excluded"), Knowers.Num(), 3);
	if (Knowers.Num() == 3)
	{
		TestTrue(TEXT("first-hand first"), Knowers[0].Holder.Get() == Alice && Knowers[0].bFirstHand && !Knowers[0].bContradicts);
		TestTrue(TEXT("hearsay keeps its source"), Knowers[1].Holder.Get() == Bob && Knowers[1].Source.Get() == Alice && !Knowers[1].bContradicts);
		TestTrue(TEXT("different target contradicts"), Knowers[2].Holder.Get() == Cara && Knowers[2].bContradicts);
	}

	FRAISemanticFact Fact;
	Fact.Subject = RAITags::Outcome_Success;
	Fact.Predicate = RAITags::Outcome_Failure;
	A->LearnFact(Fact);
	Fact.LearnedFrom = Alice;
	B->LearnFact(Fact);
	FRAIMindViewFactKey FactKey;
	FactKey.Subject = Fact.Subject;
	FactKey.Predicate = Fact.Predicate;
	const TArray<FRAIMindViewKnower> FactKnowers = F.MindView->WhoKnows(FactKey);
	TestTrue(TEXT("semantic facts found"), FactKnowers.Num() == 2 && FactKnowers[0].bFirstHand && FactKnowers[1].Source.Get() == Alice);
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewDestroyedTest, "DestroyedSubjectReleasesRecorder")
{
	FMindViewFixture F;
	int32 Lost = 0;
	F.MindView->OnSubjectLost.AddLambda([&Lost](FObjectKey) { ++Lost; });
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Chain}));
	F.Arbitrate();
	int32 Snapshots = 0;
	F.MindView->OnSnapshot.AddLambda([&Snapshots](const FRAIMindViewSnapshot&) { ++Snapshots; });
	F.Tick(2);
	TestTrue(TEXT("production tick produced snapshots"), Snapshots > 0);
	F.Controller->UnPossess();
	F.Pawn->Destroy();
	F.Tick(2);
	TestEqual(TEXT("loss reported once"), Lost, 1);
	TestEqual(TEXT("recorder released"), F.MindView->GetRecorderCount(), 0);
	TestFalse(TEXT("manager released"), F.Manager->IsCapturingExplanations() || F.Manager->OnArbitrationNative.IsBound());
	TestFalse(TEXT("hook inactive"), FRAIMindViewHooks::IsActive());
	F.MindView->OnSubjectLost.Clear();
	F.MindView->OnSnapshot.Clear();
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewRepossessTest, "RepossessRebindsRecorder")
{
	FMindViewFixture F;
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Chain}));
	TestTrue(TEXT("bound to first manager"), F.Manager->IsCapturingExplanations());
	F.Controller->UnPossess();
	ARAIMindViewTestController* Second = F.World->SpawnActor<ARAIMindViewTestController>();
	Second->Possess(F.Pawn);
	F.Tick(1);
	URAIMindViewRecorder* Recorder = F.MindView->FindRecorder(F.Pawn);
	TestTrue(TEXT("recorder follows the new manager"), Recorder && Recorder->GetManager() == Second->ManagerComponent);
	TestFalse(TEXT("old manager released"), F.Manager->IsCapturingExplanations());
	TestTrue(TEXT("new manager captures explanations"), Second->ManagerComponent->IsCapturingExplanations());
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), FRAIMindViewDemand());
	TestFalse(TEXT("new manager released"), Second->ManagerComponent->IsCapturingExplanations());
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewRevisionTest, "SectionRevisionStableWhenUnchanged")
{
	FMindViewFixture F;
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Decisions, RAIMindViewSections::Chain}));
	F.Arbitrate();
	const FRAIMindViewSnapshot First = F.Capture();
	const FRAIMindViewSnapshot Second = F.Capture();
	for (const FName Id : {RAIMindViewSections::Decisions, RAIMindViewSections::Chain})
		TestEqual(*FString::Printf(TEXT("%s revision stable"), *Id.ToString()), First.FindSection(Id)->Revision, Second.FindSection(Id)->Revision);
	F.TaskA->Score = 11.f;
	F.Arbitrate();
	TestNotEqual(TEXT("decisions revision changes with priority"), First.FindSection(RAIMindViewSections::Decisions)->Revision,
		F.Capture().FindSection(RAIMindViewSections::Decisions)->Revision);
	F.MindView->SetDemand(F.Pawn, TEXT("Test"), FRAIMindViewDemand());
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewSessionTest, "SessionTrackingPinsAndRulesDriveDemand")
{
	FMindViewFixture F;
	TStrongObjectPtr<ULocalPlayer> Player(NewObject<ULocalPlayer>(GEngine));
	Player->PlayerAdded(nullptr, 7);
	URAIMindViewSession* Session = Player->GetSubsystem<URAIMindViewSession>();
	TestNotNull(TEXT("session exists"), Session);
	if (!Session) { Player->PlayerRemoved(); return false; }
	const TArray<FRAIMindViewPin> SavedPins = Session->GetPins();
	Session->BindWorld(F.World.Get());
	Session->SetPins({{RAIMindViewSections::Thoughts, 3}});

	Session->SelectPrimary(F.Pawn);
	TestFalse(TEXT("disabled session holds no demand"), F.MindView->HasAnyDemand());
	Session->SetEnabled(true);
	FRAIMindViewDemand Demand;
	TestTrue(TEXT("primary demanded"), F.MindView->GetMergedDemand(F.Pawn, Demand));
	TestTrue(TEXT("panel sections at full detail"), Demand.FullSections.Contains(RAIMindViewSections::Decisions));
	TestTrue(TEXT("pinned sections at compact detail"), Demand.CompactSections.Contains(RAIMindViewSections::Thoughts) && Demand.CompactCount == 3);
	TestEqual(TEXT("carded"), Session->GetCarded().Num(), 1);

	APawn* Rulee = F.World->SpawnActor<ACharacter>();
	ARAIMindViewTestController* Second = F.World->SpawnActor<ARAIMindViewTestController>();
	Second->Possess(Rulee);
	Session->AddRule(NewObject<URAIMindViewRule_AllSubjects>(Session));
	TestTrue(TEXT("rule tracks the other subject"), Session->IsTracked(Rulee) && F.MindView->GetMergedDemand(Rulee, Demand));
	F.Tick(2);
	TestNotNull(TEXT("snapshot delivered through production tick"), Session->GetSnapshot(Rulee));

	Session->ClearTracked();
	TestFalse(TEXT("rule subject released"), F.MindView->GetMergedDemand(Rulee, Demand));
	TestTrue(TEXT("primary still inspected"), F.MindView->GetMergedDemand(F.Pawn, Demand) && Demand.CompactSections.IsEmpty());
	Session->SetEnabled(false);
	TestFalse(TEXT("disable clears all demand"), F.MindView->HasAnyDemand());
	TestFalse(TEXT("hook inactive"), FRAIMindViewHooks::IsActive());

	Session->SetPins(SavedPins);
	Session->BindWorld(nullptr);
	Player->PlayerRemoved();
	return true;
}

RAI_MINDVIEW_TEST(FRAIMindViewMemoryLogTest, "MemoryChangeLogDescribesChanges")
{
	FMindViewFixture F;
	URAIMemoryComponent* Memory = F.AddMind(F.Pawn);
	Memory->WorkingSetCap = 2;
	FRAILifeEvent Heard;
	Heard.Source = F.Pawn;
	Memory->EncodeEpisodic(Heard);
	TestFalse(TEXT("nothing described without demand"), Memory->OnMemoryChangedNative.IsBound());

	F.MindView->SetDemand(F.Pawn, TEXT("Test"), F.Demand({RAIMindViewSections::Memory}));
	TestTrue(TEXT("listener bound while demanded"), Memory->OnMemoryChangedNative.IsBound());
	FRAILifeEvent Seen;
	Seen.OriginId = FGuid::NewGuid();
	Seen.Target = F.Pawn;
	Memory->EncodeEpisodic(Seen);
	FRAISemanticFact Fact;
	Fact.Subject = RAITags::Outcome_Success;
	Fact.Predicate = RAITags::Outcome_Failure;
	Fact.Confidence = .4f;
	Memory->LearnFact(Fact);
	Fact.Confidence = 1.f;
	Memory->LearnFact(Fact);
	Memory->EncodeEpisodic(Seen); // third working episode exceeds the cap: consolidation and forgetting

	const FRAIMindViewMemory* Data = F.Capture().GetSection<FRAIMindViewMemory>(RAIMindViewSections::Memory);
	if (!Data) { AddError(TEXT("memory section missing")); return false; }
	auto Find = [Data](FName Kind) { return Data->Changes.FindByPredicate([Kind](const FRAIMindViewMemoryChange& C) { return C.Kind == Kind; }); };
	const FRAIMindViewMemoryChange* Added = Find(TEXT("Added"));
	TestTrue(TEXT("witnessed episode logged with identity"), Added && Added->Reason == FName(TEXT("Witnessed")) && Added->Items.Num() == 1
		&& Added->Items[0].Target.Get() == F.Pawn);
	const FRAIMindViewMemoryChange* Learned = Find(TEXT("FactLearned"));
	TestTrue(TEXT("new fact logged"), Learned && Learned->OldConfidence < 0.f && Learned->FactSubject == Fact.Subject);
	const FRAIMindViewMemoryChange* Changed = Find(TEXT("Changed"));
	TestTrue(TEXT("revised fact logged old -> new"), Changed && FMath::IsNearlyEqual(Changed->OldConfidence, .4f) && Changed->NewConfidence > .4f
		&& Changed->Reason == FName(TEXT("Blended")));
	const FRAIMindViewMemoryChange* Consolidated = Find(TEXT("Consolidated"));
	TestTrue(TEXT("consolidation logged with reason and items"), Consolidated && Consolidated->Reason == FName(TEXT("WorkingSetCap"))
		&& Consolidated->Items.Num() == Consolidated->Count);
	TestTrue(TEXT("log is newest first"), Data->Changes.Num() >= 4 && Data->Changes.Last().Kind == FName(TEXT("Added")));

	F.MindView->SetDemand(F.Pawn, TEXT("Test"), FRAIMindViewDemand());
	TestFalse(TEXT("listener released"), Memory->OnMemoryChangedNative.IsBound());
	return true;
}

#undef RAI_MINDVIEW_TEST
#endif
