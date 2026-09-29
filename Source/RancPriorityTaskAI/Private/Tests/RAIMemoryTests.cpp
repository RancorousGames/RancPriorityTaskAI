#include "Tests/RAITestTypes.h"
#include "Mind/RAIMemoryComponent.h"
#include "Mind/RAIMindComponent.h"
#include "RAITags.h"
#include "SubSystems/RAIKnowledgeComponent.h"
#include "UObject/Class.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace
{
struct FMemoryServices : IRAITimeSource, IRAIScheduler
{
	double Time = 0.0;
	uint64 Next = 1;
	struct FWork { TWeakObjectPtr<UObject> Owner; double Due; TFunction<void()> Callback; };
	TMap<uint64, FWork> Work;
	double Now() const override { return Time; }
	FRAIScheduleHandle ScheduleOnce(UObject* Owner, double Delay, TFunction<void()> Callback) override
	{
		const uint64 Id = Next++;
		Work.Add(Id, {Owner, Time + Delay, MoveTemp(Callback)});
		return {Id};
	}
	void Cancel(FRAIScheduleHandle& Handle) override { Work.Remove(Handle.Id); Handle.Invalidate(); }
	void CancelAll(const UObject* Owner) override
	{
		for (auto It = Work.CreateIterator(); It; ++It) if (It.Value().Owner.Get() == Owner) It.RemoveCurrent();
	}
	void Advance(double Delta)
	{
		Time += Delta;
		TArray<uint64> Due;
		for (const auto& Pair : Work) if (Pair.Value.Due <= Time) Due.Add(Pair.Key);
		Due.Sort();
		for (uint64 Id : Due)
		{
			FWork Entry;
			if (Work.RemoveAndCopyValue(Id, Entry) && Entry.Owner.IsValid()) Entry.Callback();
		}
	}
};

struct FMemoryFixture
{
	TStrongObjectPtr<UWorld> World{UWorld::CreateWorld(EWorldType::Game, false)};
	TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(World.Get())};
	TSharedPtr<FMemoryServices> Services = MakeShared<FMemoryServices>();
	APawn* Pawn;
	URAIMemoryComponent* Memory;
	URAIMindComponent* Mind;
	FMemoryFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
		World->SetGameInstance(Instance.Get());
		Instance->Init();
		World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Pawn = World->SpawnActor<APawn>();
		Memory = NewObject<URAIMemoryComponent>(Pawn);
		Mind = NewObject<URAIMindComponent>(Pawn);
		Pawn->AddInstanceComponent(Memory);
		Pawn->AddInstanceComponent(Mind);
		Memory->SetServices(Services, Services);
		Memory->RegisterComponent();
		Mind->RegisterComponent();
		World->BeginPlay();
	}
	~FMemoryFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		Instance->Shutdown();
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World.Get());
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemoryConsolidated, "RancPriorityTaskAI.Memory.ConsolidatedSurvivesTruncation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemoryConsolidated::RunTest(const FString&)
{
	FMemoryFixture F;
	F.Memory->WorkingSetCap = 4;
	const FGuid Protected = FGuid::NewGuid();
	FRAIEpisodicMemory LongTerm;
	LongTerm.Id = Protected;
	LongTerm.bConsolidated = true;
	LongTerm.Salience = 0.001f;
	F.Memory->Episodic.Add(LongTerm);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		FRAIEpisodicMemory M;
		M.Id = FGuid::NewGuid(); M.Salience = 1.f;
		F.Memory->Episodic.Add(M);
	}
	F.Mind->Witness({});
	TestTrue(TEXT("old consolidated low-score episode survives working eviction"), F.Memory->Episodic.ContainsByPredicate([Protected](const FRAIEpisodicMemory& M) { return M.Id == Protected; }));
	TestTrue(TEXT("working population independently bounded"), F.Memory->Episodic.Num() <= 15);
	F.Memory->LongTermCap = 2;
	F.Services->Advance(60.0);
	int32 Consolidated = 0;
	for (const auto& M : F.Memory->Episodic) Consolidated += M.bConsolidated;
	TestEqual(TEXT("long-term cap independently enforced"), Consolidated, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemorySmall, "RancPriorityTaskAI.Memory.SmallCapNoGrowth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemorySmall::RunTest(const FString&)
{
	FMemoryFixture F;
	F.Memory->WorkingSetCap = 10;
	for (int32 Index = 0; Index < 9; ++Index) F.Mind->Witness({});
	F.Services->Advance(60.0);
	TestEqual(TEXT("automatic consolidation does not grow nine episodes to ten"), F.Memory->Episodic.Num(), 9);
	for (const auto& M : F.Memory->Episodic) TestTrue(TEXT("no fabricated episode"), M.Id.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemoryFact, "RancPriorityTaskAI.Memory.LearnFactUpdatesValue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemoryFact::RunTest(const FString&)
{
	FMemoryFixture F;
	FRAISemanticFact Fact;
	Fact.Subject = RAITags::Outcome_Success; Fact.Predicate = RAITags::Outcome_Failure;
	Fact.Confidence = 0.8f; Fact.Value = FInstancedStruct::Make(FRAITestPayload{ });
	F.Memory->LearnFact(Fact);
	FRAITestPayload Payload; Payload.Value = 42;
	Fact.Value = FInstancedStruct::Make(Payload); Fact.LearnedFrom = F.Pawn; Fact.Confidence = 0.4f;
	F.Memory->LearnFact(Fact);
	TestEqual(TEXT("new payload stored"), F.Memory->Semantic[0].Value.Get<FRAITestPayload>().Value, 42);
	TestTrue(TEXT("new provenance stored"), F.Memory->Semantic[0].LearnedFrom == F.Pawn);
	TestTrue(TEXT("configured confidence blend"), FMath::IsNearlyEqual(F.Memory->Semantic[0].Confidence, 0.68f));
	Payload.Value = 99; Fact.Value = FInstancedStruct::Make(Payload);
	F.Memory->LearnFact(Fact, ERAIFactMergePolicy::KeepHigherConfidence);
	TestEqual(TEXT("weaker evidence preserved under keep-higher policy"), F.Memory->Semantic[0].Value.Get<FRAITestPayload>().Value, 42);
	F.Memory->LearnFact(Fact, ERAIFactMergePolicy::Replace);
	TestEqual(TEXT("replace policy replaces payload"), F.Memory->Semantic[0].Value.Get<FRAITestPayload>().Value, 99);
	TestEqual(TEXT("replace policy replaces confidence"), F.Memory->Semantic[0].Confidence, 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemoryRecall, "RancPriorityTaskAI.Memory.RecallRefsTopK", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemoryRecall::RunTest(const FString&)
{
	FMemoryFixture F;
	for (float Score : {0.1f, 0.9f, 0.3f, 0.7f})
	{
		FRAIEpisodicMemory M; M.Id = FGuid::NewGuid(); M.Salience = Score; M.Event.Actor = F.Pawn;
		F.Memory->Episodic.Add(M);
	}
	const TArray<int32> Top = F.Memory->RecallRefs({}, 2);
	TestEqual(TEXT("top K only"), Top.Num(), 2);
	if (Top.Num() == 2) { TestEqual(TEXT("best index"), Top[0], 1); TestEqual(TEXT("second index"), Top[1], 3); }
	const auto Copies = F.Memory->RecallAbout(F.Pawn, {}, 2);
	TestEqual(TEXT("copy API agrees"), Copies.Num(), Top.Num());
	for (int32 Index = 0; Index < Copies.Num(); ++Index) TestEqual(TEXT("same ranking"), Copies[Index].Id, F.Memory->Episodic[Top[Index]].Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemoryPawn, "RancPriorityTaskAI.Memory.OwnerAgnosticOnPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemoryPawn::RunTest(const FString&)
{
	FMemoryFixture F;
	F.Services->Time = 10000000000.125;
	TestTrue(TEXT("BeginPlay resolves sibling memory on pawn"), F.Mind->Memory == F.Memory);
	F.Mind->Witness({});
	TestEqual(TEXT("production Witness pipeline encodes once"), F.Memory->Episodic.Num(), 1);
	if (F.Memory->Episodic.Num() == 1)
	{
		TestEqual(TEXT("double timestamp preserves precision"), F.Memory->Episodic[0].Event.WorldTime, F.Services->Time);
		F.Services->Time += 600.0;
		TestTrue(TEXT("injected time drives decay"), FMath::IsNearlyEqual(F.Memory->CurrentConfidence(F.Memory->Episodic[0]), 0.5f));
		F.Memory->Touch(F.Memory->Episodic[0].Id);
		TestEqual(TEXT("touch uses same clock"), F.Memory->Episodic[0].LastRecalledTime, F.Services->Time);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAIMemoryLifecycle, "RancPriorityTaskAI.Memory.ScheduledMaintenancePauseAndTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAIMemoryLifecycle::RunTest(const FString&)
{
	FMemoryFixture F;
	TestEqual(TEXT("BeginPlay schedules maintenance"), F.Services->Work.Num(), 1);
	F.Services->Advance(20.0);
	F.Memory->SetConsolidationPaused(true);
	TestEqual(TEXT("pause cancels pending maintenance"), F.Services->Work.Num(), 0);
	F.Services->Advance(100.0);
	F.Memory->SetConsolidationPaused(false);
	TestEqual(TEXT("resume restores remaining delay"), F.Services->Work.CreateConstIterator().Value().Due, 160.0);
	F.Services->Advance(40.0);
	TestEqual(TEXT("normal maintenance callback reschedules once"), F.Services->Work.Num(), 1);
	F.Pawn->Destroy();
	TestEqual(TEXT("owner teardown cancels maintenance"), F.Services->Work.Num(), 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRAILegacyKnowledge, "RancPriorityTaskAI.Memory.LegacyKnowledgeLocalWeakReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRAILegacyKnowledge::RunTest(const FString&)
{
	FMemoryFixture F;
	URAIKnowledgeComponent* Knowledge = NewObject<URAIKnowledgeComponent>(F.Pawn);
	AActor* Subject = F.World->SpawnActor<AActor>();
	FRelationshipFact Fact; Fact.Relation = RAITags::Outcome_Success;
	Knowledge->ServerAddRelation(Subject, Fact);
	TestTrue(TEXT("legacy name routes to local authority storage"), Knowledge->HasRelation(Subject, Fact.Relation));
	TestFalse(TEXT("legacy function no longer claims to be an RPC"), Knowledge->FindFunction(TEXT("ServerAddRelation"))->HasAnyFunctionFlags(FUNC_Net));
	Knowledge->RemoveRelationMulticast(Subject, Fact.Relation);
	TestFalse(TEXT("legacy remove wrapper works locally"), Knowledge->HasRelation(Subject, Fact.Relation));
	Knowledge->AddRelation(Subject, Fact);
	Subject->Destroy();
	TestEqual(TEXT("destroyed actor facts pruned"), Knowledge->GetAllRelations(Subject).Num(), 0);
	return true;
}
#endif
