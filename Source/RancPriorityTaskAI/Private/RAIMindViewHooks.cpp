// Copyright Rancorous Games, 2026

#include "RAIMindViewHooks.h"
#include "RAITags.h"

#if !UE_BUILD_SHIPPING
bool FRAIMindViewHooks::bActive = false;
int32 FRAIMindViewHooks::ActivationCount = 0;
FRAIMindViewHooks::FThoughtSink FRAIMindViewHooks::Sink;

void FRAIMindViewHooks::EmitThought(const AActor* Subject, const FRAIThoughtArgs& Args)
{
	if (bActive && Subject) Sink(Subject, Args, nullptr);
}

void FRAIMindViewHooks::EmitLegacyThought(const AActor* Subject, const FString& Text)
{
	if (bActive && Subject) Sink(Subject, FRAIThoughtArgs(RAITags::Thought_Legacy), &Text);
}

void FRAIMindViewHooks::SetSink(FThoughtSink InSink)
{
	check(IsInGameThread());
	Sink = MoveTemp(InSink);
	Refresh();
}

void FRAIMindViewHooks::AddActivation()
{
	check(IsInGameThread());
	++ActivationCount;
	Refresh();
}

void FRAIMindViewHooks::RemoveActivation()
{
	check(IsInGameThread());
	ActivationCount = FMath::Max(ActivationCount - 1, 0);
	Refresh();
}

void FRAIMindViewHooks::Refresh()
{
	bActive = ActivationCount > 0 && static_cast<bool>(Sink);
}
#endif
