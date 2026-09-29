// Copyright Rancorous Games, 2026

#include "RAIScheduling.h"

#include "Engine/World.h"
#include "TimerManager.h"

FRAIWorldTimeSource::FRAIWorldTimeSource(const UWorld* InWorld)
	: World(InWorld)
{
}

double FRAIWorldTimeSource::Now() const
{
	const UWorld* Resolved = World.Get();
	return Resolved ? Resolved->GetTimeSeconds() : 0.0;
}

FRAITimerManagerScheduler::FRAITimerManagerScheduler(UWorld* InWorld)
	: World(InWorld)
{
}

FRAITimerManagerScheduler::~FRAITimerManagerScheduler()
{
	*AliveToken = false;
	if (UWorld* Resolved = World.Get())
	{
		FTimerManager& Timers = Resolved->GetTimerManager();
		for (TPair<uint64, FEntry>& Pair : Entries)
		{
			Timers.ClearTimer(Pair.Value.Timer);
		}
	}
	Entries.Reset();
	Callbacks.Reset();
}

FRAIScheduleHandle FRAITimerManagerScheduler::ScheduleOnce(UObject* Owner, double DelaySeconds, TFunction<void()> Callback)
{
	FRAIScheduleHandle Handle;
	UWorld* Resolved = World.Get();
	if (!Resolved || !Callback)
	{
		return Handle;
	}

	const uint64 Id = NextId++;
	TWeakPtr<bool> WeakAlive = AliveToken;
	FTimerDelegate Delegate = FTimerDelegate::CreateLambda([this, WeakAlive, Id]()
	{
		const TSharedPtr<bool> Alive = WeakAlive.Pin();
		if (Alive.IsValid() && *Alive)
		{
			Fire(Id);
		}
	});

	FEntry& Entry = Entries.Add(Id);
	Entry.Owner = Owner;
	Callbacks.Add(Id, MoveTemp(Callback));

	FTimerManager& Timers = Resolved->GetTimerManager();
	if (DelaySeconds > 0.0)
	{
		Timers.SetTimer(Entry.Timer, Delegate, static_cast<float>(DelaySeconds), false);
	}
	else
	{
		Entry.Timer = Timers.SetTimerForNextTick(Delegate);
	}

	Handle.Id = Id;
	return Handle;
}

void FRAITimerManagerScheduler::Fire(uint64 Id)
{
	FEntry Entry;
	if (!Entries.RemoveAndCopyValue(Id, Entry))
	{
		return;
	}
	TFunction<void()> Callback;
	Callbacks.RemoveAndCopyValue(Id, Callback);

	// Owner explicitly supplied and since destroyed: drop the callback.
	if (!Entry.Owner.IsExplicitlyNull() && !Entry.Owner.IsValid())
	{
		return;
	}
	if (Callback)
	{
		Callback();
	}
}

void FRAITimerManagerScheduler::Cancel(FRAIScheduleHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	FEntry Entry;
	if (Entries.RemoveAndCopyValue(Handle.Id, Entry))
	{
		Callbacks.Remove(Handle.Id);
		if (UWorld* Resolved = World.Get())
		{
			Resolved->GetTimerManager().ClearTimer(Entry.Timer);
		}
	}
	Handle.Invalidate();
}

void FRAITimerManagerScheduler::CancelAll(const UObject* Owner)
{
	UWorld* Resolved = World.Get();
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<const UObject>& EntryOwner = It.Value().Owner;
		const bool bMatches = EntryOwner.Get() == Owner && Owner != nullptr;
		const bool bStale = !EntryOwner.IsExplicitlyNull() && !EntryOwner.IsValid();
		if (bMatches || bStale)
		{
			if (Resolved)
			{
				Resolved->GetTimerManager().ClearTimer(It.Value().Timer);
			}
			Callbacks.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}
