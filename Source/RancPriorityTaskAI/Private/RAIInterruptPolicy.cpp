// Copyright Rancorous Games, 2026

#include "RAIInterruptPolicy.h"

#include "RAIManagerComponent.h"
#include "RAITaskComponent.h"

float URAIInterruptPolicy::GetPriorityGap(const URAIManagerComponent& Manager, ERAIInterruptionType Type)
{
	switch (Type)
	{
	case ERAIInterruptionType::Always:        return 0.01f;
	case ERAIInterruptionType::WaitASec:      return Manager.WaitASecInterruptPriorityGap;
	case ERAIInterruptionType::PreferablyNot: return Manager.PreferablyNotInterruptPriorityGap;
	case ERAIInterruptionType::OnlyIfNeeded:  return Manager.OnlyIfNeededInterruptPriorityGap;
	case ERAIInterruptionType::IfPanic:       return Manager.IfPanicInterruptPriorityGap;
	case ERAIInterruptionType::IfLifeOrDeath: return Manager.IfLifeOrDeathInterruptPriorityGap;
	case ERAIInterruptionType::Never:
	default:                                  return -1.f;
	}
}

bool URAIInterruptPolicy::ShouldInterrupt(const FRAIArbitrationContext& Context, const URAITaskComponent* Active,
                                          const URAITaskComponent* Candidate) const
{
	if (!Context.Manager || !Active || !Candidate)
	{
		return false;
	}

	const float Gap = GetPriorityGap(*Context.Manager, Active->InterruptType);
	if (Gap < 0.f)
	{
		return false;
	}

	return (Context.CandidatePriority - Context.ActivePriority) > Gap;
}
