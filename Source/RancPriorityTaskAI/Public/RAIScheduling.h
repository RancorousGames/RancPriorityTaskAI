// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtr.h"

class UObject;
class UWorld;

/**
 * Time source used by every plugin decision path (task timing, cooldowns, loop detection, memory decay).
 * The default reads world time. Games can inject simulation time; tests inject a fake clock.
 * Inject before timed work is created (before possession for a manager, before BeginPlay for memory).
 */
class RANCPRIORITYTASKAI_API IRAITimeSource
{
public:
	virtual ~IRAITimeSource() = default;
	virtual double Now() const = 0;
};

/** Opaque handle to a callback scheduled through IRAIScheduler. */
struct RANCPRIORITYTASKAI_API FRAIScheduleHandle
{
	uint64 Id = 0;

	bool IsValid() const { return Id != 0; }
	void Invalidate() { Id = 0; }
};

/**
 * All plugin-internal delayed work (wait timeouts, delayed restarts, deferred end processing) goes through this
 * interface, so a game can provide a budgeted scheduler running on simulation time. The plugin never requires its
 * own tick: without an injected scheduler, FRAITimerManagerScheduler uses the world's FTimerManager.
 *
 * Callbacks must not run synchronously inside ScheduleOnce. A callback whose Owner has been destroyed must not run.
 */
class RANCPRIORITYTASKAI_API IRAIScheduler
{
public:
	virtual ~IRAIScheduler() = default;

	/** Run Callback once after DelaySeconds (<= 0 means as soon as possible, but not synchronously). */
	virtual FRAIScheduleHandle ScheduleOnce(UObject* Owner, double DelaySeconds, TFunction<void()> Callback) = 0;

	/** Cancel one pending callback and invalidate the handle. Safe to call with an invalid or already fired handle. */
	virtual void Cancel(FRAIScheduleHandle& Handle) = 0;

	/** Cancel every pending callback owned by Owner. */
	virtual void CancelAll(const UObject* Owner) = 0;
};

/** Default time source: world time (UWorld::GetTimeSeconds) as double. Returns 0 when the world is gone. */
class RANCPRIORITYTASKAI_API FRAIWorldTimeSource final : public IRAITimeSource
{
public:
	explicit FRAIWorldTimeSource(const UWorld* InWorld);
	virtual double Now() const override;

private:
	TWeakObjectPtr<const UWorld> World;
};

/** Default scheduler: one FTimerManager timer per scheduled callback. */
class RANCPRIORITYTASKAI_API FRAITimerManagerScheduler final : public IRAIScheduler
{
public:
	explicit FRAITimerManagerScheduler(UWorld* InWorld);
	virtual ~FRAITimerManagerScheduler() override;

	virtual FRAIScheduleHandle ScheduleOnce(UObject* Owner, double DelaySeconds, TFunction<void()> Callback) override;
	virtual void Cancel(FRAIScheduleHandle& Handle) override;
	virtual void CancelAll(const UObject* Owner) override;

	int32 GetPendingCount() const { return Entries.Num(); }

private:
	struct FEntry
	{
		TWeakObjectPtr<const UObject> Owner;
		FTimerHandle Timer;
	};

	void Fire(uint64 Id);

	TWeakObjectPtr<UWorld> World;
	TMap<uint64, FEntry> Entries;
	TMap<uint64, TFunction<void()>> Callbacks;
	uint64 NextId = 1;
	/** Shared with the timer lambdas so a destroyed scheduler never runs a callback. */
	TSharedRef<bool> AliveToken = MakeShared<bool>(true);
};
