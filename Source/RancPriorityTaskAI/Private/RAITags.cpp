// Copyright Rancorous Games, 2026

#include "RAITags.h"

namespace RAITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Success, "RAI.Outcome.Success", "Task ended successfully");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Failure, "RAI.Outcome.Failure", "Task ended unsuccessfully");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Interrupted, "RAI.Outcome.Interrupted", "Task was interrupted");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Timeout, "RAI.Outcome.Timeout", "Task wait timed out");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Forced, "RAI.Outcome.Forced", "Task was force-ended");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_ParentEnded, "RAI.Outcome.ParentEnded", "Invoking parent ended");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_EndedByParent, "RAI.Outcome.EndedByParent", "Invoking parent ended this child");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Replaced, "RAI.Outcome.Replaced", "Arbitration replaced the chain");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_PriorityZero, "RAI.Outcome.PriorityZero", "Root priority reached the threshold");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Deinitialized, "RAI.Outcome.Deinitialized", "Manager deinitialized");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Outcome_Deactivated, "RAI.Outcome.Deactivated", "RAI deactivated");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reevaluate_TaskEnabledChanged, "RAI.Reevaluate.TaskEnabledChanged", "A task was enabled or disabled");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reevaluate_ChainEnded, "RAI.Reevaluate.ChainEnded", "The active chain ended");
}
