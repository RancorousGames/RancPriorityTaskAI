// Copyright Rancorous Games, 2026

#include "Modules/ModuleManager.h"
#include "RAIMindViewHooks.h"
#include "RAIMindViewSubsystem.h"
#include "RAIMindViewTags.h"
#include "RAIMindViewTypes.h"

namespace RAIMindViewTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kind_Default, "RAI.MindView.Kind.Default", "MindView subject without a game-specific kind");
}

void FRAIMindViewDemand::MergeFrom(const FRAIMindViewDemand& Other)
{
	FullSections.Append(Other.FullSections);
	CompactSections.Append(Other.CompactSections);
	RateHz = FMath::Max(RateHz, Other.RateHz);
	CompactCount = FMath::Max(CompactCount, Other.CompactCount);
}

bool FRAIMindViewDemand::operator==(const FRAIMindViewDemand& Other) const
{
	return RateHz == Other.RateHz && CompactCount == Other.CompactCount
		&& FullSections.Num() == Other.FullSections.Num() && FullSections.Includes(Other.FullSections)
		&& CompactSections.Num() == Other.CompactSections.Num() && CompactSections.Includes(Other.CompactSections);
}

class FRancPriorityTaskAIMindViewModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if !UE_BUILD_SHIPPING
		// Thoughts reach the subject's world subsystem; the hook stays inactive until a subsystem has demand.
		FRAIMindViewHooks::SetSink([](const AActor* Subject, const FRAIThoughtArgs& Args, const FString* LegacyText)
		{
			if (URAIMindViewSubsystem* Subsystem = URAIMindViewSubsystem::Get(Subject))
				Subsystem->ReceiveThought(Subject, Args, LegacyText);
		});
#endif
	}

	virtual void ShutdownModule() override
	{
#if !UE_BUILD_SHIPPING
		FRAIMindViewHooks::SetSink(nullptr);
#endif
	}
};

IMPLEMENT_MODULE(FRancPriorityTaskAIMindViewModule, RancPriorityTaskAIMindView)
