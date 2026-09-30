#include "Tests/RAITestTypes.h"
#include "RAIManagerComponent.h"
#include "RAITags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

ARAITestController::ARAITestController(const FObjectInitializer& Initializer) : Super(Initializer)
{
	AutoHandleSensoryInput = false;
	ManagerComponent = CreateDefaultSubobject<URAIManagerComponent>(TEXT("Manager"));
	CreateDefaultSubobject<URAITestTask>(TEXT("Root"));
	CreateDefaultSubobject<URAITestChild>(TEXT("Child"))->IsPrimaryTask = false;
	CreateDefaultSubobject<URAITestGrandchild>(TEXT("Grandchild"))->IsPrimaryTask = false;
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace
{
struct FRAITestServices : IRAITimeSource, IRAIScheduler
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
		for (auto It = Pending.CreateIterator(); It; ++It)
			if (It.Value().Owner.Get() == Owner) It.RemoveCurrent();
	}
	void Advance(double Seconds)
	{
		Time += Seconds;
		TArray<uint64> Due;
		for (const auto& Pair : Pending) if (Pair.Value.Due <= Time) Due.Add(Pair.Key);
		Due.Sort();
		for (uint64 Id : Due)
		{
			FPending Entry;
			if (Pending.RemoveAndCopyValue(Id, Entry) && Entry.Owner.IsValid()) Entry.Callback();
		}
	}
};
struct FRAISafetyFixture
{
	TStrongObjectPtr<UWorld> World;
	TStrongObjectPtr<UGameInstance> GameInstance;
	ARAITestController* Controller;
	URAIManagerComponent* Manager;
	URAITestTask* Root;
	URAITestChild* Child;
	URAITestGrandchild* Grandchild;

	FRAISafetyFixture()
		: World(UWorld::CreateWorld(EWorldType::Game, false))
		, GameInstance(NewObject<UGameInstance>(World.Get()))
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
		World->SetGameInstance(GameInstance.Get());
		GameInstance->Init();
		World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Controller = World->SpawnActor<ARAITestController>();
		Manager = Controller->ManagerComponent;
		Root = Controller->FindComponentByClass<URAITestTask>();
		Child = Controller->FindComponentByClass<URAITestChild>();
		Grandchild = Controller->FindComponentByClass<URAITestGrandchild>();
		World->BeginPlay();
		Controller->Possess(World->SpawnActor<ACharacter>());
	}
	~FRAISafetyFixture()
	{
		Root->OnBegin = nullptr;
		Root->OnEnd = nullptr;
		Root->OnCompleted = nullptr;
		Child->OnBegin = nullptr;
		Child->OnEnd = nullptr;
		Child->OnCompleted = nullptr;
		Grandchild->OnBegin = nullptr;
		Grandchild->OnEnd = nullptr;
		Grandchild->OnCompleted = nullptr;
		World->EndPlay(EEndPlayReason::Quit);
		GameInstance->Shutdown();
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World.Get());
	}
	void Start() { Manager->UpdateActiveTasks(); }
	void InvokeChild() { Root->InvokeTask(URAITestChild::StaticClass(), {}); }
	TSharedRef<FRAITestServices> UseFakeServices()
	{
		auto Services = MakeShared<FRAITestServices>();
		APawn* Pawn = Controller->GetPawn();
		Manager->SetServices(Services, Services);
		Manager->Initialize(Controller, Pawn);
		return Services;
	}
	void Tick()
	{
		for (int32 Index = 0; Index < 4; ++Index) World->Tick(LEVELTICK_All, 0.05f);
	}
};

void AfterWorldTicks(FAutomationTestBase& Test, TSharedRef<FRAISafetyFixture> Fixture, TFunction<void()> Verify)
{
	Test.AddCommand(new FFunctionLatentCommand([Fixture, Verify = MoveTemp(Verify), Ticks = 0]() mutable
	{
		Fixture->World->Tick(LEVELTICK_All, 0.05f);
		if (++Ticks < 4) return false;
		Verify();
		return true;
	}));
}
}

#define RAI_SAFETY_TEST(Class, Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "RancPriorityTaskAI." Name, EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter) \
	bool Class::RunTest(const FString& Parameters)

RAI_SAFETY_TEST(FRAIHigherPriorityTest, "Arbitration.HigherPriorityWins")
{
	FRAISafetyFixture F;
	F.Child->IsPrimaryTask = true;
	F.Child->Score = 100.f;
	// Repossession also rebuilds the task inventory after configuration changes.
	F.Controller->UnPossess();
	F.Controller->Possess(F.World->SpawnActor<ACharacter>());
	F.Start();
	TestTrue(TEXT("higher priority wins"), F.Manager->ActiveTask == F.Child);
	return true;
}

RAI_SAFETY_TEST(FRAIReturnTest, "Invoke.ReturnsToParentOnSuccess")
{
	FRAISafetyFixture F;
	F.Start(); F.InvokeChild(); F.Child->EndTask(true); F.Tick();
	TestTrue(TEXT("parent is active"), F.Manager->ActiveTask == F.Root && F.Root->IsTaskActive);
	TestEqual(TEXT("one completion"), F.Root->Completions, 1);
	TestTrue(TEXT("success delivered"), F.Root->bLastSuccess);
	return true;
}

RAI_SAFETY_TEST(FRAIZeroTest, "Arbitration.InterruptIfReachesZero")
{
	FRAISafetyFixture F;
	F.Start(); F.Root->Score = 0.f; F.Manager->UpdateActiveTasks();
	TestFalse(TEXT("zero priority root ends"), F.Root->IsTaskActive);
	TestNull(TEXT("no active chain"), F.Manager->ActiveTask);
	return true;
}

RAI_SAFETY_TEST(FRAIRunningTest, "Run.TaskRunningWithoutWait_NotReinvoked")
{
	FRAISafetyFixture F;
	F.Start(); F.Manager->UpdateActiveTasks(); F.Manager->UpdateActiveTasks();
	TestEqual(TEXT("running task begins once"), F.Root->Begins, 1);
	return true;
}

RAI_SAFETY_TEST(FRAIChildTimeoutTest, "Wait.ChildTimeout_ParentReceivesFailure_NoOrphan")
{
	auto F = MakeShared<FRAISafetyFixture>();
	F->Start(); F->InvokeChild(); F->Child->BeginWaiting(0.01);
	AfterWorldTicks(*this, F, [this, F]
	{
		TestTrue(TEXT("parent remains tracked"), F->Manager->ActiveTask == F->Root && F->Root->IsTaskActive);
		TestFalse(TEXT("child ended"), F->Child->IsTaskActive);
		TestEqual(TEXT("one failure completion"), F->Root->Completions, 1);
		TestFalse(TEXT("timeout fails"), F->Root->bLastSuccess);
		TestTrue(TEXT("timeout reason delivered"), F->Root->GetLastChildOutcome() == RAITags::Outcome_Timeout);
	});
	return true;
}

RAI_SAFETY_TEST(FRAIParentTimeoutTest, "Wait.ParentTimeout_EndsSubtree")
{
	auto F = MakeShared<FRAISafetyFixture>();
	F->Start(); F->InvokeChild(); F->Root->BeginWaiting(0.01);
	AfterWorldTicks(*this, F, [this, F]
	{
		TestFalse(TEXT("parent ended"), F->Root->IsTaskActive);
		TestFalse(TEXT("child interrupted"), F->Child->IsTaskActive);
		TestTrue(TEXT("child sees interruption"), F->Child->bLastInterrupted);
		TestNull(TEXT("no orphan"), F->Manager->ActiveTask);
	});
	return true;
}

RAI_SAFETY_TEST(FRAIParentEndTest, "End.ParentEndsWithActiveChild_ChildInterruptedNoReturn")
{
	FRAISafetyFixture F;
	F.Start(); F.InvokeChild(); F.Root->EndTask(true); F.Tick();
	TestTrue(TEXT("child interrupted"), F.Child->bLastInterrupted);
	TestEqual(TEXT("dead parent is not resumed"), F.Root->Completions, 0);
	TestNull(TEXT("chain cleared"), F.Manager->ActiveTask);
	return true;
}

RAI_SAFETY_TEST(FRAIImmediateEndTest, "Invoke.ChildEndsImmediately_NoReentrancyCorruption")
{
	FRAISafetyFixture F;
	F.Start();
	F.Child->OnBegin = [&F] { F.Child->EndTask(true); };
	bool bInsideInvoke = true;
	bool bReentrant = false;
	F.Root->OnCompleted = [&] { bReentrant = bInsideInvoke; };
	F.InvokeChild(); bInsideInvoke = false; F.Tick();
	TestFalse(TEXT("completion runs after invoke returns"), bReentrant);
	TestEqual(TEXT("completion delivered once"), F.Root->Completions, 1);
	TestTrue(TEXT("parent tracked"), F.Manager->ActiveTask == F.Root);
	return true;
}

RAI_SAFETY_TEST(FRAIStaleWaitTest, "Wait.EndCancelsTimeoutBeforeNextRun")
{
	auto F = MakeShared<FRAISafetyFixture>();
	F->Start(); F->Root->BeginWaiting(0.01); F->Root->EndTask(); F->Manager->UpdateActiveTasks();
	AfterWorldTicks(*this, F, [this, F]
	{
		TestTrue(TEXT("old wait cannot end new run"), F->Root->IsTaskActive);
		TestEqual(TEXT("only intended end"), F->Root->Ends, 1);
	});
	return true;
}

RAI_SAFETY_TEST(FRAIRepossessTest, "Lifecycle.RepossessReinitializes")
{
	FRAISafetyFixture F;
	F.Start(); F.Controller->UnPossess();
	TestFalse(TEXT("old task stopped"), F.Root->IsTaskActive);
	ACharacter* Replacement = F.World->SpawnActor<ACharacter>();
	F.Controller->Possess(Replacement);
	TestTrue(TEXT("manager changed pawn"), F.Manager->Character == Replacement);
	TestEqual(TEXT("task inventory rebuilt"), F.Manager->AllTasks.Num(), 3);
	return true;
}

RAI_SAFETY_TEST(FRAIGapTest, "Arbitration.InterruptGapRespected")
{
	FRAISafetyFixture F;
	F.Child->IsPrimaryTask = true; F.Child->Score = 0.f;
	F.Manager->RebuildPrimaryTasks(); F.Start();
	F.Root->InterruptType = ERAIInterruptionType::WaitASec;
	F.Child->Score = F.Root->Score + F.Manager->WaitASecInterruptPriorityGap;
	F.Manager->UpdateActiveTasks();
	TestTrue(TEXT("equal gap does not interrupt"), F.Manager->ActiveTask == F.Root);
	F.Child->Score += 1.f; F.Manager->UpdateActiveTasks();
	TestTrue(TEXT("exceeding gap interrupts"), F.Manager->ActiveTask == F.Child);
	return true;
}

RAI_SAFETY_TEST(FRAINeverTest, "Arbitration.NeverNotInterrupted")
{
	FRAISafetyFixture F;
	F.Child->IsPrimaryTask = true; F.Child->Score = 0.f;
	F.Manager->RebuildPrimaryTasks(); F.Start();
	F.Root->InterruptType = ERAIInterruptionType::Never;
	F.Child->Score = 10000.f; F.Manager->UpdateActiveTasks();
	TestTrue(TEXT("Never keeps its root"), F.Manager->ActiveTask == F.Root);
	return true;
}

RAI_SAFETY_TEST(FRAIInvokedWinnerTest, "Arbitration.InvokedPrimaryWinsAsRootWhenRootDropsToZero")
{
	FRAISafetyFixture F;
	F.Child->IsPrimaryTask = true; F.Child->Score = 0.f;
	F.Manager->RebuildPrimaryTasks(); F.Start(); F.InvokeChild();
	F.Child->Score = 100.f; F.Root->Score = 0.f; F.Manager->UpdateActiveTasks();
	TestTrue(TEXT("winner restarts as root"), F.Manager->ActiveTask == F.Child && !F.Child->ParentInvokingTask);
	TestEqual(TEXT("child started twice"), F.Child->Begins, 2);
	TestFalse(TEXT("old root inactive"), F.Root->IsTaskActive);
	return true;
}

RAI_SAFETY_TEST(FRAICycleTest, "Invoke.CycleRejected")
{
	FRAISafetyFixture F; F.Start(); F.InvokeChild();
	ERAIInvokeRejectReason Reason;
	TestTrue(TEXT("ancestor invocation rejected"), F.Child->InvokeTaskWithResult(URAITestTask::StaticClass(), {}, Reason) == ERAIInvokeResult::Rejected);
	TestTrue(TEXT("cycle reason"), Reason == ERAIInvokeRejectReason::IsAncestorOfInvoker);
	TestTrue(TEXT("chain unchanged"), F.Manager->ActiveTask == F.Child && F.Child->ParentInvokingTask == F.Root);
	TestTrue(TEXT("invariants hold"), F.Manager->ValidateInvariants());
	return true;
}

RAI_SAFETY_TEST(FRAIDepthTest, "Invoke.DepthCapRejected")
{
	FRAISafetyFixture F; F.Manager->MaxInvokeDepth = 2; F.Start(); F.InvokeChild();
	ERAIInvokeRejectReason Reason;
	TestTrue(TEXT("depth rejected"), F.Child->InvokeTaskWithResult(URAITestGrandchild::StaticClass(), {}, Reason) == ERAIInvokeResult::Rejected);
	TestTrue(TEXT("depth reason"), Reason == ERAIInvokeRejectReason::MaxDepthExceeded);
	TestFalse(TEXT("target untouched"), F.Grandchild->IsTaskActive);
	return true;
}

RAI_SAFETY_TEST(FRAIActiveTargetTest, "Invoke.ActiveInstanceRejected")
{
	FRAISafetyFixture F; F.Start(); F.InvokeChild();
	ERAIInvokeRejectReason Reason;
	TestTrue(TEXT("active target rejected"), F.Root->InvokeTaskWithResult(URAITestChild::StaticClass(), {}, Reason) == ERAIInvokeResult::Rejected);
	TestTrue(TEXT("active reason"), Reason == ERAIInvokeRejectReason::TargetAlreadyActive);
	return true;
}

RAI_SAFETY_TEST(FRAIInterruptChainTest, "Invoke.ChainEndsOnInterrupt")
{
	FRAISafetyFixture F; F.Start(); F.InvokeChild();
	F.Child->InvokeTask(URAITestGrandchild::StaticClass(), {});
	F.Root->EndTask(false, 0.f, true);
	TestNull(TEXT("chain gone"), F.Manager->ActiveTask);
	TestTrue(TEXT("all interrupted"), F.Root->bLastInterrupted && F.Child->bLastInterrupted && F.Grandchild->bLastInterrupted);
	TestEqual(TEXT("no return to parent"), F.Root->Completions + F.Child->Completions, 0);
	return true;
}

RAI_SAFETY_TEST(FRAIDeinitializeTest, "Lifecycle.DeinitializeClearsEverything")
{
	FRAISafetyFixture F; auto Services = F.UseFakeServices();
	F.Start(); F.InvokeChild(); F.Child->BeginWaiting(2.0);
	F.Manager->Deinitialize();
	TestNull(TEXT("no owner"), F.Manager->OwningController);
	TestNull(TEXT("no pawn"), F.Manager->Pawn);
	TestNull(TEXT("no active task"), F.Manager->ActiveTask);
	TestEqual(TEXT("no tasks"), F.Manager->AllTasks.Num(), 0);
	TestEqual(TEXT("no owned callbacks"), Services->Pending.Num(), 0);
	TestNull(TEXT("task detached"), F.Root->ManagerComponent);
	Services->Advance(3.0);
	TestEqual(TEXT("no late completion"), F.Root->Completions, 0);
	return true;
}

RAI_SAFETY_TEST(FRAIDeactivateTest, "Lifecycle.SetRAIActiveFalseEndsChain")
{
	FRAISafetyFixture F; F.Start(); F.InvokeChild(); F.Controller->SetRAIActive(false);
	TestNull(TEXT("chain stopped"), F.Manager->ActiveTask);
	TestFalse(TEXT("root stopped"), F.Root->IsTaskActive);
	F.Manager->UpdateActiveTasks();
	TestEqual(TEXT("inactive update ignored"), F.Root->Begins, 1);
	F.Controller->SetRAIActive(true); F.Manager->UpdateActiveTasks();
	TestEqual(TEXT("resumed update"), F.Root->Begins, 2);
	return true;
}

RAI_SAFETY_TEST(FRAIDoubleActivateTest, "Lifecycle.SetRAIActiveTwiceSafe")
{
	FRAISafetyFixture F;
	F.Controller->SetRAIActive(true); F.Controller->SetRAIActive(true);
	F.Start(); F.Controller->SetRAIActive(false); F.Controller->SetRAIActive(false);
	TestEqual(TEXT("one end"), F.Root->Ends, 1);
	return true;
}

RAI_SAFETY_TEST(FRAIPolicyTest, "Policy.CustomInterruptPolicyUsed")
{
	FRAISafetyFixture F;
	F.Child->IsPrimaryTask = true; F.Child->Score = 0.f;
	F.Manager->RebuildPrimaryTasks(); F.Start(); F.Root->InterruptType = ERAIInterruptionType::Never;
	F.Manager->InterruptPolicy = NewObject<URAITestInterruptPolicy>(F.Manager);
	F.Child->Score = 100.f; F.Manager->UpdateActiveTasks();
	TestTrue(TEXT("custom policy overrides Never"), F.Manager->ActiveTask == F.Child);
	return true;
}

RAI_SAFETY_TEST(FRAIPayloadTest, "Payload.TypedPayloadRoundTrip")
{
	FRAISafetyFixture F; F.Start();
	FRAITaskInvokeArguments Args; FRAITestPayload Payload; Payload.Value = 42; Args.SetPayload(Payload);
	F.Root->InvokeTask(URAITestChild::StaticClass(), Args);
	const FRAITestPayload* Read = F.Child->GetInvokeArgs().GetPayload<FRAITestPayload>();
	TestTrue(TEXT("payload reaches real invoked task"), Read && Read->Value == 42);
	return true;
}

RAI_SAFETY_TEST(FRAIOutcomeTest, "Outcome.ReasonDeliveredToParent")
{
	FRAISafetyFixture F; F.Start(); F.InvokeChild(); F.Child->EndTaskWithReason(false, RAITags::Outcome_Timeout);
	TestTrue(TEXT("reason retained"), F.Root->GetLastChildOutcome() == RAITags::Outcome_Timeout);
	TestEqual(TEXT("legacy completion preserved"), F.Root->Completions, 1);
	return true;
}

RAI_SAFETY_TEST(FRAIEventsTest, "Events.EnterExitPairedOrdered")
{
	FRAISafetyFixture F; TArray<FString> Events;
	F.Manager->OnTaskBeginNative.AddLambda([&](URAITaskComponent* T) { Events.Add(TEXT("Begin") + T->GetName()); });
	F.Manager->OnTaskEndNative.AddLambda([&](URAITaskComponent* T, bool, FGameplayTag, bool) { Events.Add(TEXT("End") + T->GetName()); });
	F.Start(); F.InvokeChild(); F.Root->EndTask();
	const TArray<FString> Expected{TEXT("BeginRoot"), TEXT("BeginChild"), TEXT("EndChild"), TEXT("EndRoot")};
	TestTrue(TEXT("paired and child-first exits"), Events == Expected);
	F.Manager->OnTaskBeginNative.Clear(); F.Manager->OnTaskEndNative.Clear();
	return true;
}

RAI_SAFETY_TEST(FRAIArbitrationEventTest, "Events.ArbitrationEventOnChangeOnly")
{
	FRAISafetyFixture F; int32 Events = 0;
	F.Manager->OnArbitrationNative.AddLambda([&](const FRAIArbitrationResult&) { ++Events; });
	F.Start(); F.Manager->UpdateActiveTasks();
	const int32 Before = Events;
	F.Manager->UpdateActiveTasks(); F.Manager->UpdateActiveTasks();
	TestEqual(TEXT("unchanged decision is not repeated"), Events, Before);
	F.Manager->OnArbitrationNative.Clear();
	return true;
}

RAI_SAFETY_TEST(FRAICooldownTest, "Cooldown.NextBeginMeasuredFromEnd")
{
	FRAISafetyFixture F; auto Clock = F.UseFakeServices();
	F.Start(); Clock->Advance(10.0); F.Root->EndTask(true, 4.f);
	TestFalse(TEXT("end starts cooldown"), F.Root->IsTaskReady());
	Clock->Advance(3.99); TestFalse(TEXT("not ready early"), F.Root->IsTaskReady());
	Clock->Advance(0.01); TestTrue(TEXT("ready at end plus duration"), F.Root->IsTaskReady());
	return true;
}

RAI_SAFETY_TEST(FRAILoopPenaltyTest, "Cooldown.LoopPenaltyTemporary")
{
	FRAISafetyFixture F; auto Clock = F.UseFakeServices();
	F.Root->MaxTaskLoopCount = 2; F.Start();
	AddExpectedError(TEXT("infinite loop detected"), EAutomationExpectedErrorFlags::Contains, 1);
	F.Root->Restart(); F.Root->EndTask();
	TestEqual(TEXT("configured cooldown unchanged"), F.Root->Cooldown, 0.f);
	TestFalse(TEXT("temporary penalty blocks ready"), F.Root->IsTaskReady());
	Clock->Advance(1.0); TestTrue(TEXT("penalty expires"), F.Root->IsTaskReady());
	return true;
}

RAI_SAFETY_TEST(FRAILoopEventTest, "Cooldown.LoopEventFired")
{
	FRAISafetyFixture F; auto Clock = F.UseFakeServices(); F.Root->MaxTaskLoopCount = 2; F.Manager->bAlwaysRecordTrace = true;
	IConsoleVariable* Penalty = IConsoleManager::Get().FindConsoleVariable(TEXT("rai.LoopPenalty"));
	const int32 Previous = Penalty->GetInt(); Penalty->Set(0);
	AddExpectedError(TEXT("infinite loop detected"), EAutomationExpectedErrorFlags::Contains, 1);
	F.Start(); F.Root->Restart(); F.Root->EndTask();
	int32 Loops = 0;
	for (const auto& Entry : F.Manager->GetTrace()) if (Entry.Type == ERAITraceType::LoopDetected) ++Loops;
	TestEqual(TEXT("event traced with penalty off"), Loops, 1);
	TestTrue(TEXT("penalty is off"), F.Root->IsTaskReady());
	Penalty->Set(Previous);
	return true;
}

RAI_SAFETY_TEST(FRAIDeterminismTest, "Determinism.SameInputsSameTrace")
{
	auto Run = []
	{
		FRAISafetyFixture F; auto Clock = F.UseFakeServices(); F.Manager->bAlwaysRecordTrace = true;
		F.Start(); Clock->Advance(1.0); F.InvokeChild(); F.Child->EndTask(true); F.Root->EndTask();
		return F.Manager->GetTrace();
	};
	TestTrue(TEXT("identical trace under injected time"), Run() == Run());
	return true;
}

RAI_SAFETY_TEST(FRAIStaleDeferredEndTest, "End.DeferredCompletionCannotEndReplacementRun")
{
	FRAISafetyFixture F; auto Clock = F.UseFakeServices(); F.Start();
	F.Child->OnBegin = [&] { F.Child->EndTask(); };
	F.InvokeChild();
	TestTrue(TEXT("first run is ending"), F.Child->GetRunState() == ERAITaskRunState::Ending);
	F.Root->EndTask(false, 0.f, true);
	F.Child->OnBegin = nullptr; F.Start(); F.InvokeChild();
	Clock->Advance(0.0);
	TestTrue(TEXT("new child survives old queued completion"), F.Child->IsTaskActive);
	TestEqual(TEXT("old run does not complete new parent"), F.Root->Completions, 0);
	return true;
}

RAI_SAFETY_TEST(FRAINonCharacterTest, "Lifecycle.NonCharacterPawn")
{
	FRAISafetyFixture F; F.Controller->UnPossess();
	APawn* Pawn = F.World->SpawnActor<APawn>(); F.Controller->Possess(Pawn); F.Start();
	TestTrue(TEXT("task receives generic pawn"), F.Root->Pawn == Pawn && F.Manager->Pawn == Pawn);
	TestNull(TEXT("character cast optional"), F.Root->Character);
	TestTrue(TEXT("arbitration runs for generic pawn"), F.Root->IsTaskActive);
	return true;
}

RAI_SAFETY_TEST(FRAIExplanationTest, "Events.PriorityExplanationCaptured")
{
	FRAISafetyFixture F;
	F.Manager->bCaptureExplanations = true;
	bool Captured = false;
	F.Manager->OnArbitrationNative.AddLambda([&Captured](const FRAIArbitrationResult& Result)
	{
		Captured = !Result.Candidates.IsEmpty() && Result.Candidates[0].Explanation.Terms.Num() == 1
			&& Result.Candidates[0].Explanation.Terms[0].Name == FName(TEXT("Constant"));
	});
	F.Start();
	TestTrue(TEXT("named priority terms delivered through arbitration"), Captured);
	F.Manager->OnArbitrationNative.Clear();
	return true;
}

RAI_SAFETY_TEST(FRAIPerfTest, "Perf.UpdateActiveTasks16Tasks")
{
	FRAISafetyFixture F;
	F.Manager->Deinitialize();
	F.Child->IsPrimaryTask = true;
	F.Grandchild->IsPrimaryTask = true;
	for (int32 Index = 0; Index < 13; ++Index)
	{
		URAITestTask* Task = NewObject<URAITestTask>(F.Controller);
		F.Controller->AddInstanceComponent(Task);
		Task->RegisterComponent();
	}
	F.Manager->Initialize(F.Controller, F.Controller->GetPawn());
	TestEqual(TEXT("sixteen primary task benchmark"), F.Manager->PrimaryTasks.Num(), 16);
	for (int32 Index = 0; Index < 100; ++Index) F.Start();
	constexpr int32 Iterations = 10000;
	const double Begin = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Iterations; ++Index) F.Start();
	const double Microseconds = (FPlatformTime::Seconds() - Begin) * 1000000.0 / Iterations;
	AddInfo(FString::Printf(TEXT("16 native constant tasks, invariants enabled: %.3f us/update; budget 5 us, tolerance 15 us"), Microseconds));
	TestTrue(TEXT("under three times the five-microsecond budget"), Microseconds < 15.0);
	return true;
}

#undef RAI_SAFETY_TEST
#endif
