# RancPriorityTaskAI — Refactor & Expansion Proposal

> Scope: review of the entire `RancPriorityTaskAI` plugin (v1.3.0) with concrete proposals to make it (a) easy to use, (b) infinitely extensible, and (c) as smooth in C++ as it is in Blueprint. Includes drop-in code snippets and a full advanced sample task at the bottom.

---

## 0. Executive Summary

The architecture is solid: `URAIManagerComponent` orchestrates, `URAITaskComponent`s self-score via `CalculatePriority()`, an interruption gradient (`Always` → `IfLifeOrDeath` → `Never`) decides preemption, and tasks can invoke sub-tasks forming a chain. The mind-view debug widget is a great selling point.

But the rough edges are real:

| Area | Problem | Severity |
|---|---|---|
| **C++ ergonomics** | Every override is a `BlueprintNativeEvent` `_Implementation` — feels alien in C++; no latent helpers; no compile-time task discovery. | High |
| **Payload extensibility** | `FRAITaskInvokeArguments` is a fixed quad (Actor/Vector/bool/String). Anything richer means stuffing data into `CustomInstruction` or globals. | High |
| **Priority authoring** | `CalculatePriority()` returns a raw float. No response curves, no named considerations, no shared scorers — every task hand-rolls its math. | High |
| **State & lifetime bugs** | `IsTaskReady()` precedence bug; `KnowledgeComponent` stores raw `AActor*` keys (dangling pointers); `BP-only` properties that should be `EditAnywhere` are `VisibleAnywhere`; controller has `inline void BeginPlay()` in a `.cpp`. | Med-High |
| **Encapsulation** | Almost every member is public `UPROPERTY` — including `ChildInvokedTask`, `ParentInvokingTask`, `ManagerComponent`. Prevents safe internal refactors. | Med |
| **Smooth path code** | Lives on `ARAIController`, can't be turned off without editing the controller; no reason it isn't a `UPathFollowingComponent` subclass or movement modifier. | Med |
| **Field LOD** | `ERAIField` exists, `GetSimulationField()` returns a stub. Promised in README but not implemented. | Med |
| **Perf** | `GetTaskByClass` is O(n) linear; called from `InvokeTask`, `TriggerCustom`, etc. No `TMap` cache. `CalculatePriority` runs every tick for every primary task even when its inputs haven't changed. | Med |
| **Tooling** | No visual debug log of *why* a task scored what it did; no in-editor validation; no automated test harness. | Med |

The rest of the document proposes concrete changes for each, with snippets you can paste in.

---

## 1. Critical Bugs & Quick Wins

These are small, contained, and worth doing first.

### 1.1 `IsTaskReady()` operator-precedence bug

`RAITaskComponent.cpp:82`

```cpp
if (NextBeginCooldown <= 0 && Cooldown <= 0.0f || WorldTimeBegun <= 0.0f)
```

`&&` binds tighter than `||`, so this reads `(NextBeginCooldown <= 0 && Cooldown <= 0.0f) || WorldTimeBegun <= 0.0f`. Probably what you wanted — but unparenthesised it's a tripwire for the next reader. Rewrite explicit:

```cpp
const bool bNoCooldownsConfigured = NextBeginCooldown <= 0.f && Cooldown <= 0.f;
const bool bNeverRunYet           = WorldTimeBegun <= 0.f;
if (bNoCooldownsConfigured || bNeverRunYet)
{
    return true;
}
```

### 1.2 `inline` on out-of-line member function

`RAIController.cpp:22` — `inline void ARAIController::BeginPlay()` in a `.cpp` is a footgun. Drop `inline`.

### 1.3 `MaxThoughtMemoryCount` and `IsPrimaryTask` are `VisibleAnywhere`

Both are configuration. Promote to `EditAnywhere` so designers can change them per AI:

```cpp
// RAIController.h
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|Configuration")
int MaxThoughtMemoryCount = 30;

// RAITaskComponent.h
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RAI|Configuration")
bool IsPrimaryTask = true;
```

### 1.4 `KnowledgeComponent` stores raw `AActor*` as `TMap` key

`RAIKnowledgeComponent.h:45` — `TMap<AActor*, TArray<FRelationshipFact>>` will leak stale pointers when actors are destroyed and can collide with newly allocated actors at the same address. Switch to weak pointers and prune lazily:

```cpp
TMap<TWeakObjectPtr<AActor>, TArray<FRelationshipFact>> RelationshipFacts;
```

Add a `PruneStale()` method called on a 2-5s timer or whenever `RelationshipFacts.Num()` crosses a threshold. Also: this struct is `FRelationshipFact` — there's a `RemainingDuration` field that nothing in the plugin decrements. Either tick it down in a timer or remove it.

### 1.5 Replication of relationships is wrong

`AddRelationMulticast` is multicast-reliable for what should be game state. If this component lives on an AI controller (server-only authority), there are no clients to multicast to. If it lives on a replicated pawn, you're paying multicast overhead for state most clients don't need. Recommendation: drop the multicast and rely on standard `UPROPERTY(Replicated)` on a `TArray<FReplicatedRelationshipFact>` — see §9.

### 1.6 `PrimaryTasks` includes disabled tasks

`URAIManagerComponent::Initialize` builds `PrimaryTasks` once at startup and never updates. If a task gets `IsEnabled = false`, it still iterates in `UpdateTaskPriorities` (skipped via `continue`) but `CalculatePriority` blueprint event has dispatch overhead. Lazy-rebuild on `SetEnabled` or sort `PrimaryTasks` so disabled tasks live at the back and you can early-out.

### 1.7 `ReturnToInvokingTask` doesn't restart the active task

When a child invoked task ends successfully, `ActiveTask` is set to the parent and `OnInvokedTaskCompleted` is fired — but the parent's `BeginTask` is never called again. The parent now needs to drive itself from inside the BP event. That's fine if documented, but a `Restart()` or `ResumeFromInvoke()` semantic would make C++ tasks much cleaner.

---

## 2. C++ Task Ergonomics (the big one)

The current pattern in C++ looks like:

```cpp
UCLASS()
class URAIMyTask : public URAITaskComponent
{
    virtual float CalculatePriority_Implementation() override;
    virtual void  BeginTask_Implementation(const FRAITaskInvokeArguments& Args) override;
    virtual void  EndTask_Implementation(bool Success, float Cooldown, bool WasInterrupted) override;
    virtual void  OnPerceptionStimulus_Implementation(AActor*, FAIStimulus) override;
};
```

That works but it leaks the `_Implementation` Blueprint suffix into every C++ file, and you can't cleanly `Super::BeginTask(Args)` because the public API is the wrapper. You also have no latent helpers — to wait you have to call `BeginWaiting`, set up your own timer, then call `DoneWaiting` from a callback, and split your function into pieces.

### 2.1 Add a C++-first base class

Don't change `URAITaskComponent` — add a sibling that bridges the BP events to clean virtuals:

```cpp
// Public/RAITaskNative.h
#pragma once
#include "RAITaskComponent.h"
#include "RAITaskNative.generated.h"

/** C++-first base. Override Native* virtuals; default impls forward to BP. */
UCLASS(Abstract, NotBlueprintable)
class RANCPRIORITYTASKAI_API URAITaskNative : public URAITaskComponent
{
    GENERATED_BODY()
public:
    // ---- Override these in C++ ----
    virtual float NativeCalculatePriority()                           { return 0.f; }
    virtual void  NativeBeginTask(const FRAITaskInvokeArguments&)     {}
    virtual void  NativeEndTask(bool /*Success*/, bool /*Interrupted*/) {}
    virtual void  NativeTick(float /*DeltaTime*/)                     {}     // optional, see §11
    virtual void  NativePerception(AActor*, const FAIStimulus&)       {}
    virtual void  NativeCustomTrigger(FGameplayTag, UObject*)         {}
    virtual void  NativeInvokedReturned(bool /*Success*/)             {}

    // ---- Bridges (don't override) ----
    virtual float CalculatePriority_Implementation() override final
    { return NativeCalculatePriority(); }
    virtual void  BeginTask_Implementation(const FRAITaskInvokeArguments& A) override final
    { NativeBeginTask(A); }
    virtual void  OnPerceptionStimulus_Implementation(AActor* A, FAIStimulus S) override final
    { NativePerception(A, S); }
    virtual void  OnCustomTrigger_Implementation(FGameplayTag T, UObject* P) override final
    { NativeCustomTrigger(T, P); }
};
```

Now a C++ task is just:

```cpp
UCLASS(ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class URAITask_PatrolPerimeter : public URAITaskNative
{
    GENERATED_BODY()
public:
    virtual float NativeCalculatePriority() override;
    virtual void  NativeBeginTask(const FRAITaskInvokeArguments& Args) override;
};
```

### 2.2 Latent helpers — coroutine-style waits

Tasks frequently want: *"move there, wait until you arrive, then do X"*. Today that's three callbacks. With UE5's `TCoroutine`/`TFuture` or the simpler `FAwaitable` pattern, it can be one function.

If you don't want to depend on UE5Coro, ship a tiny `TFutureLatent` helper:

```cpp
// Public/RAILatent.h
USTRUCT()
struct FRAILatentHandle
{
    GENERATED_BODY()
    TWeakObjectPtr<URAITaskComponent> Task;
    FTimerHandle Timer;
};

class FRAILatent
{
public:
    /** Returns once the task either reaches the goal or is interrupted. */
    static TFuture<EPathFollowingResult::Type> MoveTo(URAITaskComponent* Task, FVector Goal, float AcceptanceRadius = 50.f);
    static TFuture<bool> WaitSeconds(URAITaskComponent* Task, float Seconds);
    static TFuture<AActor*> WaitForPerception(URAITaskComponent* Task, FGameplayTag SenseTag);
};
```

Implementation sketch (pseudo):

```cpp
TFuture<EPathFollowingResult::Type> FRAILatent::MoveTo(URAITaskComponent* Task, FVector Goal, float Acc)
{
    auto Promise = MakeShared<TPromise<EPathFollowingResult::Type>>();
    Task->BeginWaiting(0.f); // indefinite wait
    auto Req = Task->OwnerController->MoveToLocation(Goal, Acc);
    Task->OwnerController->ReceiveMoveCompleted.AddLambda(
      [Promise, Task](FAIRequestID, EPathFollowingResult::Type R)
      {
          EDoneWaitingExecutionStates Branch;
          Task->DoneWaiting(ERAIInterruptionType::Always, Branch);
          Promise->SetValue(R);
      });
    return Promise->GetFuture();
}
```

Combined with UE5Coro (recommended; tiny dep), the actual task becomes:

```cpp
FAsyncCoroutine URAITask_FetchWater::NativeBeginTask(const FRAITaskInvokeArguments& Args)
{
    co_await FRAILatent::MoveTo(this, NearestWaterSource()->GetActorLocation());
    co_await FRAILatent::WaitSeconds(this, 2.f);     // animation
    Character->FindComponentByClass<UInventory>()->Add(EItem::Water, 1);
    EndTask(true);
}
```

This is the single biggest UX win for C++ users.

### 2.3 Macro to register and reflect on tasks

Auto-collect every `URAITaskNative` subclass into a registry so tooling (debugger, validation, tests) can iterate them without component-walking:

```cpp
// Public/RAITaskRegistry.h
class FRAITaskRegistry
{
public:
    static FRAITaskRegistry& Get();
    void Register(UClass* TaskClass);
    const TArray<UClass*>& All() const { return Tasks; }
private:
    TArray<UClass*> Tasks;
};

#define RAI_REGISTER_TASK(ClassName) \
    static void Register_##ClassName() { FRAITaskRegistry::Get().Register(ClassName::StaticClass()); } \
    static FAutoConsoleCommand AutoReg_##ClassName(TEXT("rai.register." #ClassName), TEXT(""), \
        FConsoleCommandDelegate::CreateStatic(&Register_##ClassName));
```

(Better: do it via a `FDelayedAutoRegisterHelper` in module startup so it happens automatically without a console command.)

### 2.4 Builder pattern for inline tasks

For tests, prototypes, or one-off behaviour you don't want a whole class for:

```cpp
// Public/RAITaskBuilder.h
class FRAITaskBuilder
{
public:
    FRAITaskBuilder& WithName(FName);
    FRAITaskBuilder& Priority(TFunction<float(URAITaskComponent*)>);
    FRAITaskBuilder& OnBegin(TFunction<void(URAITaskComponent*, const FRAITaskInvokeArguments&)>);
    FRAITaskBuilder& OnTick(TFunction<void(URAITaskComponent*, float)>);
    FRAITaskBuilder& InterruptType(ERAIInterruptionType);
    FRAITaskBuilder& Cooldown(float);
    URAITaskComponent* AttachTo(ARAIController* Controller);
};

// Usage:
FRAITaskBuilder()
    .WithName("HoldGround")
    .Priority([](auto* T){ return T->Character->GetVelocity().IsNearlyZero() ? 30.f : 0.f; })
    .OnBegin([](auto* T, auto&){ T->BeginWaiting(5.f); })
    .Cooldown(10.f)
    .AttachTo(AIController);
```

Lambdas are stored on a generic `URAITask_Lambda` subclass.

---

## 3. Polymorphic Payloads — `FRAITaskInvokeArguments` v2

Today's struct is four hard-coded fields. The moment you want to invoke `Hunt` with `{ Prey, FleeStrategy, MinHealthThreshold }` you're stuffing JSON into `CustomInstruction`. Fix with `TInstancedStruct` (UE 5.5+) or `UInstancedStruct` (5.0–5.4 via the StructUtils plugin).

```cpp
// Public/RAITaskInvokeArguments.h (extended)
USTRUCT(BlueprintType)
struct FRAITaskInvokeArguments
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) AActor*  TargetActor    = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector  TargetLocation = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool     ContinueUntilSuccess = true;

    /** Payload for task-specific data. Each task type defines its own struct. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(BaseStruct="/Script/RancPriorityTaskAI.RAITaskPayload"))
    TInstancedStruct<FRAITaskPayload> Payload;
};

USTRUCT(BlueprintType)
struct FRAITaskPayload { GENERATED_BODY() };

// Per-task payload:
USTRUCT(BlueprintType)
struct FRAIHuntPayload : public FRAITaskPayload
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) AActor* Prey = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float   MinHealthThreshold = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Bitmask, BitmaskEnum="EHuntFlags")) int32 Flags = 0;
};
```

In C++:

```cpp
FRAITaskInvokeArguments Args;
Args.TargetActor = Prey;
Args.Payload.InitializeAs<FRAIHuntPayload>({ .Prey = Prey, .MinHealthThreshold = 0.1f });
InvokeTask(URAITask_Hunt::StaticClass(), Args);

// In Hunt::NativeBeginTask:
if (const auto* P = Args.Payload.GetPtr<FRAIHuntPayload>())
{
    Prey = P->Prey;
}
```

Keep the legacy fields for backwards compat — old BPs keep working.

---

## 4. Data-Driven Priority — Considerations & Curves

`CalculatePriority()` returning a single float is great for power but bad for designer-friendliness. Adopt the *Considerations* pattern (popularised by Mark Dunn / The Sims):

> Each task has N considerations. Each consideration reads a normalised input (0–1) and runs it through a `UCurveFloat`. The task's score is the *product* of all considerations × a base weight. Single zero kills the task.

```cpp
// Public/RAIConsideration.h
UCLASS(EditInlineNew, Abstract, Blueprintable)
class RANCPRIORITYTASKAI_API URAIConsideration : public UObject
{
    GENERATED_BODY()
public:
    /** Designer-set curve — X axis is normalised input, Y axis is weight 0..1. */
    UPROPERTY(EditAnywhere, Category = "RAI|Consideration")
    TObjectPtr<UCurveFloat> ResponseCurve;

    UFUNCTION(BlueprintNativeEvent)
    float GetNormalizedInput(URAITaskComponent* Task) const;
    virtual float GetNormalizedInput_Implementation(URAITaskComponent* Task) const { return 0.f; }

    float Evaluate(URAITaskComponent* Task) const
    {
        const float X = FMath::Clamp(GetNormalizedInput(Task), 0.f, 1.f);
        return ResponseCurve ? ResponseCurve->GetFloatValue(X) : X;
    }
};

UCLASS()
class URAIConsideration_DistanceToFocus : public URAIConsideration
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) float MaxDistance = 2000.f;
public:
    virtual float GetNormalizedInput_Implementation(URAITaskComponent* T) const override
    {
        const float D = T->ManagerComponent->DistanceToFocus;
        return FMath::Clamp(D / MaxDistance, 0.f, 1.f);
    }
};
```

Then `URAITaskComponent` gets:

```cpp
UPROPERTY(EditAnywhere, Instanced, Category = "RAI|Priority")
TArray<TObjectPtr<URAIConsideration>> Considerations;

UPROPERTY(EditAnywhere, Category = "RAI|Priority")
float BaseWeight = 100.f;

float URAITaskComponent::CalculatePriority_Implementation()
{
    if (Considerations.Num() == 0) return 0.f;
    float Score = BaseWeight;
    // Compensation factor — the more considerations, the more we counteract product decay.
    const float Mod = 1.f - (1.f / Considerations.Num());
    for (auto* C : Considerations)
    {
        if (!C) continue;
        const float V = C->Evaluate(this);
        const float Compensated = V + (1.f - V) * Mod * V;
        Score *= Compensated;
        if (Score <= 0.f) return 0.f; // early-out
    }
    return Score;
}
```

Now designers compose tasks visually. C++ users can still override `NativeCalculatePriority` for exotic logic. **Both worlds win.**

Bonus: store the per-consideration scores from the last evaluation in a `TArray<float> LastScores` and surface them in the mind-view widget — instantly answers "why did this task win?".

---

## 5. Task Sets, Composition & Runtime Authoring

Today: tasks are `UActorComponent`s on the controller. Pros: easy to drop in a BP. Cons: no sharing, no archetypes, no runtime composition. AI A and AI B have to each get their own copies, and you can't define "all wolves know these 5 tasks" except by subclassing the controller BP.

### 5.1 Task sets as Data Assets

```cpp
UCLASS(BlueprintType)
class URAITaskSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "RAI")
    TArray<TSubclassOf<URAITaskComponent>> Tasks;

    UPROPERTY(EditAnywhere, Category = "RAI")
    TArray<TObjectPtr<URAITaskSet>> InheritedSets;

    void Apply(ARAIController* Controller) const;
};
```

`URAIManagerComponent` gets a `TArray<URAITaskSet*> InitialTaskSets` — at `Initialize` it spawns task components from the sets and the existing per-actor components, deduplicating by class.

Now you can author "BasicSurvival" once and every NPC inherits it.

### 5.2 Hot-add/remove tasks at runtime

```cpp
URAITaskComponent* URAIManagerComponent::AddTaskAtRuntime(TSubclassOf<URAITaskComponent> TaskClass);
bool                URAIManagerComponent::RemoveTaskAtRuntime(URAITaskComponent* Task);
```

Useful for: equipping a weapon enables the `Shoot` task; entering a quest area enables `EscortPlayer`; difficulty modifiers add advanced tasks above level 10.

---

## 6. GOAP-style Pre/Post Conditions (optional layer)

Right now, "this task can run if X" lives inside `CalculatePriority` returning 0. That's hidden logic. Make it first-class so the planner — and the debugger — can see it.

```cpp
USTRUCT(BlueprintType)
struct FRAIWorldStateKey
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FGameplayTag Key;
    UPROPERTY(EditAnywhere) bool         bExpected = true;
};

UCLASS()
class URAITaskComponent { ...
    UPROPERTY(EditAnywhere, Category = "RAI|GOAP")
    TArray<FRAIWorldStateKey> Preconditions;

    UPROPERTY(EditAnywhere, Category = "RAI|GOAP")
    TArray<FRAIWorldStateKey> Effects;
};
```

`URAIManagerComponent` keeps a `FGameplayTagContainer WorldState` (or one with values via a `TMap<FGameplayTag, bool>`). Before a task is even considered for priority, preconditions are checked; on `EndTask(success=true)`, effects are applied.

This is a **layered** addition — tasks with empty conditions behave exactly as today.

---

## 7. Knowledge / Memory Component

`URAIKnowledgeComponent` is solid in concept but limited:

1. Only models *relationships* (`AActor → tag`). Real knowledge has more shapes.
2. Stores raw pointers (see §1.4).
3. Replication is awkward (see §1.5).
4. `RemainingDuration` decays nowhere.

Suggested redesign:

```cpp
USTRUCT(BlueprintType)
struct FRAIFact
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag                 Subject;     // e.g. Actor.Character.Wolf
    UPROPERTY() TWeakObjectPtr<AActor>       SubjectActor; // optional concrete subject
    UPROPERTY() FGameplayTag                 Predicate;   // e.g. State.Aggressive
    UPROPERTY() FInstancedStruct             Value;       // optional structured payload
    UPROPERTY() float                        Confidence  = 1.f;  // 0..1
    UPROPERTY() float                        WorldTimeAcquired = 0.f;
    UPROPERTY() float                        DecayHalfLife = 60.f;
};

class URAIKnowledgeComponent : public UActorComponent
{
    void Remember(const FRAIFact&);
    bool Knows(FGameplayTag Predicate, AActor* Subject = nullptr) const;
    TArray<FRAIFact> Query(FGameplayTagQuery) const;
    float ConfidenceIn(FGameplayTag Predicate, AActor* Subject) const; // applies decay
    void  Tick(float Dt); // prune low-confidence facts
};
```

Replication: a single `UPROPERTY(Replicated) TArray<FRAIFact>` with `bNetUseOwnerRelevancy = true` is simpler than the current multicast-per-write approach.

---

## 8. Field-based Simulation (LOD)

`ERAIField { NearField, DistantField }` and `GetSimulationField()` exist but the field is hard-coded to `NearField`. Wire it up:

```cpp
// URAIManagerComponent
UPROPERTY(EditAnywhere, Category = "RAI|LOD") float NearFieldRadius    = 5000.f;
UPROPERTY(EditAnywhere, Category = "RAI|LOD") float NearFieldUpdateHz  = 10.f;
UPROPERTY(EditAnywhere, Category = "RAI|LOD") float DistantUpdateHz    = 1.f;

void URAIManagerComponent::TickComponent(float Dt, ...) override
{
    AccumulatedTime += Dt;
    const float TargetHz = GetCurrentField() == ERAIField::NearField
        ? NearFieldUpdateHz : DistantUpdateHz;
    if (AccumulatedTime < 1.f / TargetHz) return;
    AccumulatedTime = 0.f;
    UpdateActiveTasks();
}

ERAIField URAIManagerComponent::GetCurrentField() const
{
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        if (auto* Pawn = PC->GetPawn())
        {
            const float D = FVector::Dist(Pawn->GetActorLocation(), Character->GetActorLocation());
            return D < NearFieldRadius ? ERAIField::NearField : ERAIField::DistantField;
        }
    }
    return ERAIField::NearField;
}
```

Per-task overrides via `URAITaskComponent::GetSimulationField()` then mean: a `BreathAnimation` task can opt out of distant simulation entirely, while a `MigrateHerd` task can opt to *only* run when distant (cheap simulation of the migration without per-step pathing).

---

## 9. Performance Quick Wins

1. **Cache `GetTaskByClass`**: `TMap<UClass*, URAITaskComponent*> TaskByClass` filled in `Initialize()`. O(1) lookup.
2. **Throttle `CalculatePriority`**: per-task `MinReevalInterval` field; if a task's last score is younger than this, reuse it. For event-driven AIs (perception-driven), make priority *event-driven* — recompute only on `OnPerceptionStimulus`, `OnCustomTrigger`, or `WorldState` change.
3. **Avoid `UClass::IsA` in `GetTaskByClass`** loop — `IsA` is a chain walk. Use exact `GetClass() == TaskClass` plus an explicit fallback for derived if needed.
4. **`Sort PrimaryTasks` by static priority hint** so likely-winners are evaluated first; combined with a "current best" early-exit you can sometimes skip half the list.
5. **`UpdateActiveTasks` should not run when the controller is unpossessed or `bRAIActive=false`** — currently still runs `UpdateTaskPriorities` which traverses the task array.
6. **Loop detection logic** in `CheckForInfLoop` is silently mutating sibling tasks' cooldowns when an infinite loop is detected — that's a great safety net but log a `LogRAI Error` (not `Warning`) and fire a multicast delegate so designers actually see it.

---

## 10. Encapsulation & API Cleanup

Move these to `protected` and provide accessors:
- `URAITaskComponent::ManagerComponent` (protected; expose `GetManager()` BP-pure)
- `URAITaskComponent::OwnerController` (protected; `GetController()`)
- `URAITaskComponent::ChildInvokedTask`, `ParentInvokingTask`, `InvokeArgs` (read-only accessors)
- `URAITaskComponent::IsTaskActive`, `IsWaiting` (mutators only via internal API)
- `URAIManagerComponent::PrimaryTasks` (private)

These are public today and game code probably reaches into them. Plan: keep deprecated public aliases for one version, log a deprecation warning on access, then remove.

While you're in there:
- Spell `OnPerceptionStimulus_Implementation(AActor* actor, ...)` → `Actor` to match style.
- Mark `URAITaskComponent::URAITaskComponent()` `= default` if there's nothing to do (currently sets `bCanEverTick = false` which is the default for `UActorComponent`).
- The `Restart()` timer reuses `RestartTimerHandle` without checking; if `Restart` is called twice rapidly the previous timer is silently overwritten.

---

## 11. Optional Per-Task Tick

Some tasks legitimately want `Tick`. Currently they have to spin a timer or use `BeginWaiting` + `OnWaitTimeout`. Add an opt-in:

```cpp
UPROPERTY(EditAnywhere, Category = "RAI|Tick")
bool bWantsTick = false;

UPROPERTY(EditAnywhere, Category = "RAI|Tick", meta=(EditCondition="bWantsTick"))
float TickInterval = 0.f; // 0 = every frame
```

`URAIManagerComponent` collects ticking active tasks and pumps them from its own tick — no separate timers, predictable order, easy to LOD.

---

## 12. Validation & Tooling

- **Editor validation**: implement `FCustomizeChildrenDelegate`/`UDataValidation` so missing references on tasks (e.g., a `URAIConsideration` with no curve) get a red squiggle in the editor before runtime.
- **Visual logger integration**: add `UE_VLOG` for every task transition. The smooth-path code already uses VLog — extend the rest. Frame-by-frame replay is invaluable for AI debugging.
- **Automation tests**: `URAIManagerComponent` is testable in isolation. Add `Tests/` with at least:
  - "Higher-priority task interrupts lower at expected gap"
  - "Invoked task chain returns to oldest ancestor on success"
  - "Disabled task is never selected"
  - "Cooldown is honoured"
- **Console commands**: `rai.dump <ActorName>` prints the full task table with current priorities, considerations, and reasons. Power user feature; trivial to write.

---

## 13. Smooth Path Refactor (small)

Currently mixed into `ARAIController`. Two reasons to extract:
1. Other AIControllers in the project can't reuse it without inheriting from `ARAIController`.
2. It bypasses the standard `UPathFollowingComponent` abstractions — testing it means PIE.

Move `GenerateSmoothPath`/`StitchPathSegments` to a free function namespace `RAI::SmoothPath::Generate(...)` that takes `UWorld*`, position, dir, request. Then `ARAIController::MoveTo` is just dispatch. Other controllers can opt in by calling the same helper.

---

## 14. Documentation

- Replace the Miro link with a markdown-based design doc in the repo (Miro is offline-hostile and behind login).
- Move `GEMINI.md` content into the README under "Releasing".
- Add `Docs/Tasks.md` with the C++ flow, the Considerations system, and a "first task in 5 minutes" tutorial.
- The current README's `Future Features` line should match what's actually shipping.

---

## 15. Advanced Sample Task — `URAITask_AmbushHunt`

Below is an end-to-end C++ task that exercises *every* improvement in this doc. It demonstrates:

- C++-first base (`URAITaskNative`)
- `TInstancedStruct` payload
- Considerations + response curves authoring
- Latent move/wait via `FRAILatent`
- Invoking a sub-task (`URAITask_Strike`) with structured args
- Custom triggers, perception stimulus reaction, and graceful interruption
- Knowledge writes (remembers a sighting), GOAP effects (`State.WeaponBloodied`)

```cpp
// AmbushHunt.h
#pragma once
#include "RAITaskNative.h"
#include "RAILatent.h"
#include "AmbushHunt.generated.h"

UENUM(BlueprintType)
enum class EAmbushPhase : uint8 { Stalking, Positioning, Holding, Striking, Recovering };

USTRUCT(BlueprintType)
struct FRAIAmbushPayload : public FRAITaskPayload
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) AActor* Quarry              = nullptr;
    UPROPERTY(EditAnywhere) float   PreferredAttackDist = 250.f;
    UPROPERTY(EditAnywhere) float   AbortIfQuarryHealthAbove = 1.f; // 0..1
    UPROPERTY(EditAnywhere) FGameplayTagContainer DesiredQuarryTags;
};

UCLASS(ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class WARTRIBES_API URAITask_AmbushHunt : public URAITaskNative
{
    GENERATED_BODY()
public:
    URAITask_AmbushHunt();

    // ---- Designer-tunable ----
    UPROPERTY(EditAnywhere, Category="Ambush") float StalkSpeed     = 250.f;
    UPROPERTY(EditAnywhere, Category="Ambush") float PositionRadius = 600.f;
    UPROPERTY(EditAnywhere, Category="Ambush") float HoldTimeMin    = 1.5f;
    UPROPERTY(EditAnywhere, Category="Ambush") float HoldTimeMax    = 4.f;
    UPROPERTY(EditAnywhere, Category="Ambush") FGameplayTag SignalToPack;
    UPROPERTY(EditAnywhere, Category="Ambush") TSubclassOf<URAITaskComponent> StrikeTask;

protected:
    // ---- URAITaskNative overrides ----
    virtual float NativeCalculatePriority() override;
    virtual void  NativeBeginTask(const FRAITaskInvokeArguments& Args) override;
    virtual void  NativeEndTask(bool Success, bool Interrupted) override;
    virtual void  NativePerception(AActor* A, const FAIStimulus& S) override;
    virtual void  NativeCustomTrigger(FGameplayTag Trigger, UObject* Payload) override;
    virtual void  NativeInvokedReturned(bool Success) override;

private:
    UPROPERTY() AActor*       CurrentQuarry = nullptr;
    UPROPERTY() EAmbushPhase  Phase         = EAmbushPhase::Stalking;
    UPROPERTY() FVector       AmbushPoint   = FVector::ZeroVector;
    FRAIAmbushPayload         CachedPayload;

    // Coroutine drives the whole flow.
    FAsyncCoroutine RunHunt();
    bool            PickAmbushPoint();
    bool            IsQuarryStillValid() const;
};
```

```cpp
// AmbushHunt.cpp
#include "AmbushHunt.h"
#include "RAIManagerComponent.h"
#include "RAIController.h"
#include "SubSystems/RAIKnowledgeComponent.h"
#include "NavigationSystem.h"

URAITask_AmbushHunt::URAITask_AmbushHunt()
{
    IsPrimaryTask        = true;
    DefaultInterruptType = ERAIInterruptionType::PreferablyNot;
    InterruptType        = DefaultInterruptType;
    Cooldown             = 8.f; // don't re-hunt the same target instantly
    InterruptIfReachesZero = true;

    BaseWeight = 120.f;

    // Considerations (shown configured here in C++; designers would do this in BP):
    auto AddCons = [&]<typename T>(TSubclassOf<T> = nullptr) -> T* {
        T* C = NewObject<T>(this);
        Considerations.Add(C);
        return C;
    };

    // 1) The quarry must exist and be visible/known recently.
    AddCons.template operator()<URAIConsideration_QuarryKnown>();
    // 2) Hunger drives priority hard.
    if (auto* H = AddCons.template operator()<URAIConsideration_OwnerStat>()) H->StatName = "Hunger";
    // 3) Inverse health — wounded predators don't ambush.
    if (auto* HP = AddCons.template operator()<URAIConsideration_OwnerStat>())
    { HP->StatName = "Health"; HP->bInvert = true; }
    // 4) Pack support nearby — multiplies chance.
    AddCons.template operator()<URAIConsideration_PackPresence>();
}

float URAITask_AmbushHunt::NativeCalculatePriority()
{
    // Default Considerations multiplication via Super.
    const float Base = Super::CalculatePriority_Implementation();

    // C++ override: extra short-circuit. If we have *zero* knowledge of any prey, kill score.
    auto* K = OwnerController->FindComponentByClass<URAIKnowledgeComponent>();
    if (!K || K->Query(FGameplayTagQuery::MakeQuery_MatchTag(
        FGameplayTag::RequestGameplayTag("Knowledge.Sighting.Prey"))).Num() == 0)
    {
        return 0.f;
    }
    return Base;
}

void URAITask_AmbushHunt::NativeBeginTask(const FRAITaskInvokeArguments& Args)
{
    Super::NativeBeginTask(Args);

    if (const auto* P = Args.Payload.GetPtr<FRAIAmbushPayload>())
    {
        CachedPayload = *P;
        CurrentQuarry = P->Quarry;
    }

    if (!CurrentQuarry)
    {
        // Fall back to best-known prey from knowledge.
        auto* K = OwnerController->FindComponentByClass<URAIKnowledgeComponent>();
        auto Sightings = K->Query(FGameplayTagQuery::MakeQuery_MatchTag(
            FGameplayTag::RequestGameplayTag("Knowledge.Sighting.Prey")));
        Sightings.Sort([](const FRAIFact& A, const FRAIFact& B){ return A.Confidence > B.Confidence; });
        CurrentQuarry = Sightings.Num() ? Sightings[0].SubjectActor.Get() : nullptr;
    }

    if (!CurrentQuarry) { EndTask(false); return; }

    Phase = EAmbushPhase::Stalking;
    TraceThought(FString::Printf(TEXT("Stalking %s"), *CurrentQuarry->GetName()));
    RunHunt(); // fire-and-forget coroutine; uses BeginWaiting/DoneWaiting under the hood.
}

FAsyncCoroutine URAITask_AmbushHunt::RunHunt()
{
    // 1. Pick an ambush point downwind / behind cover.
    if (!PickAmbushPoint()) { EndTask(false); co_return; }

    // 2. Stalk to ambush point.
    Phase = EAmbushPhase::Positioning;
    auto Result = co_await FRAILatent::MoveTo(this, AmbushPoint, 80.f);
    if (Result != EPathFollowingResult::Success || !IsQuarryStillValid())
    { EndTask(false); co_return; }

    // 3. Hold and wait — but don't be interruptible while crouched.
    Phase = EAmbushPhase::Holding;
    InterruptType = ERAIInterruptionType::OnlyIfNeeded;

    // Signal the pack to converge.
    if (SignalToPack.IsValid())
    {
        OwnerController->TriggerCustomAll(SignalToPack, CurrentQuarry);
    }

    co_await FRAILatent::WaitSeconds(this, FMath::FRandRange(HoldTimeMin, HoldTimeMax));

    if (!IsQuarryStillValid()) { EndTask(false); co_return; }

    // 4. Strike — invoke the sub-task with a typed payload.
    Phase = EAmbushPhase::Striking;
    InterruptType = ERAIInterruptionType::IfPanic;

    FRAITaskInvokeArguments StrikeArgs;
    StrikeArgs.TargetActor = CurrentQuarry;
    StrikeArgs.TargetLocation = CurrentQuarry->GetActorLocation();
    InvokeTask(StrikeTask, StrikeArgs);
    // Control returns through NativeInvokedReturned().
}

void URAITask_AmbushHunt::NativeInvokedReturned(bool Success)
{
    Phase = EAmbushPhase::Recovering;

    // Update knowledge & GOAP world state.
    if (Success)
    {
        auto* K = OwnerController->FindComponentByClass<URAIKnowledgeComponent>();
        K->Remember({
            .Subject       = FGameplayTag::RequestGameplayTag("Knowledge.Killed"),
            .SubjectActor  = CurrentQuarry,
            .Confidence    = 1.f,
            .DecayHalfLife = 600.f
        });
        ManagerComponent->WorldState.AddTag(
            FGameplayTag::RequestGameplayTag("State.WeaponBloodied"));
    }

    EndTask(Success, /*BeginAgainCooldown*/ Success ? 30.f : 5.f);
}

void URAITask_AmbushHunt::NativePerception(AActor* A, const FAIStimulus& S)
{
    // Update knowledge from senses, regardless of phase.
    if (A == CurrentQuarry && !S.WasSuccessfullySensed())
    {
        // Lost sight while holding — bail out of the ambush.
        if (Phase == EAmbushPhase::Holding || Phase == EAmbushPhase::Positioning)
        {
            TraceThought(TEXT("Lost the quarry — abandoning ambush."));
            EndTask(false);
        }
    }
}

void URAITask_AmbushHunt::NativeCustomTrigger(FGameplayTag Trigger, UObject* Payload)
{
    static const FGameplayTag PreyFled = FGameplayTag::RequestGameplayTag("RAI.Event.PreyFled");
    if (Trigger == PreyFled) { EndTask(false); }
}

void URAITask_AmbushHunt::NativeEndTask(bool Success, bool Interrupted)
{
    InterruptType = DefaultInterruptType;
    Phase         = EAmbushPhase::Stalking;
    CurrentQuarry = nullptr;
}

bool URAITask_AmbushHunt::PickAmbushPoint()
{
    if (!CurrentQuarry || !Character) return false;
    auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!Nav) return false;

    const FVector QuarryLoc = CurrentQuarry->GetActorLocation();
    const FVector ToMe      = (Character->GetActorLocation() - QuarryLoc).GetSafeNormal2D();

    // Try points behind the quarry, on the side opposite the quarry's view.
    for (int i = 0; i < 8; ++i)
    {
        const float Angle = FMath::DegreesToRadians(135.f + FMath::FRandRange(-30.f, 30.f));
        const FVector Dir = ToMe.RotateAngleAxis(FMath::RadiansToDegrees(Angle), FVector::UpVector);
        const FVector Candidate = QuarryLoc + Dir * CachedPayload.PreferredAttackDist;

        FNavLocation Out;
        if (Nav->ProjectPointToNavigation(Candidate, Out, FVector(200.f, 200.f, 400.f)))
        {
            AmbushPoint = Out.Location;
            return true;
        }
    }
    return false;
}

bool URAITask_AmbushHunt::IsQuarryStillValid() const
{
    if (!IsValid(CurrentQuarry)) return false;
    if (auto* HpIface = Cast<IRAIManagerToPawnInterface>(CurrentQuarry))
    {
        // Reuse the existing pawn interface for stat queries.
        const float QHealth = IRAIManagerToPawnInterface::Execute_GetNormalizedStat(CurrentQuarry, "Health");
        if (QHealth > CachedPayload.AbortIfQuarryHealthAbove) return false;
    }
    return true;
}
```

This single file is **~150 lines** and covers stalking, positioning, holding, signalling pack-mates, invoking a strike, decaying interrupt rules per phase, knowledge writes, and graceful failure. Doing the same thing today would require either two BPs and a custom struct, or about 400 lines of `_Implementation`s with manual timer management.

---

## 16. Suggested Roadmap

| Phase | Effort | Items |
|---|---|---|
| **A. Polish** (1–2 days) | low | §1 bugs, §10 encapsulation pass, §13 smooth-path extraction, README/docs cleanup |
| **B. C++ ergonomics** (3–5 days) | medium | §2 `URAITaskNative`, latent helpers, builder, registry |
| **C. Designer power** (3–5 days) | medium | §3 instanced payloads, §4 considerations + curves, §5 task sets |
| **D. Depth** (1–2 weeks) | high | §6 GOAP layer, §7 knowledge rewrite, §8 field LOD, §11 per-task tick, §12 validation/tools |

Each phase is independently shippable and backwards-compatible if you keep the legacy `URAITaskComponent` API intact.

---

## 17. Open Questions for the Author

1. Is anyone outside Wartribes shipping with this plugin currently? If yes — phase A and the encapsulation pass need a `RANCPRIORITY_DEPRECATED` macro pass before public removal.
2. Multiplayer expectations: today only `KnowledgeComponent` has any RPC paths and they're broken (§1.5). Is the AI run server-only? If yes, strip the multicast altogether.
3. UE5Coro dependency acceptable? It's MIT and tiny; cuts ~40% of code in async tasks. Alternative is hand-rolled `TFuture` chains (uglier but no dep).
4. `Curves/CurveFloat.h` is already included in `RAIDataStructures.h` but unused — was a curves-based system planned and shelved?
