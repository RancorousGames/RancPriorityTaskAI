// Copyright Rancorous Games, 2026

#pragma once

#include "NativeGameplayTags.h"

/** Native gameplay tags used by the task core. Games may add their own outcome tags (for example Failed.SourceEmpty). */
namespace RAITags
{
	/** Default reason for EndTask(true). */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Success);
	/** Default reason for EndTask(false). */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Failure);
	/** Default reason for EndTask(..., WasInterrupted = true). */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Interrupted);
	/** A BeginWaiting(MaxWaitTime) expired. The task fails and returns to its parent. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Timeout);
	/** ForceInterruptActiveTask was called. The task fails and returns to its parent. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Forced);
	/** The task was ended because its invoking parent ended. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_ParentEnded);
	/** The parent ended its child through EndInvokedChild without a reason. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_EndedByParent);
	/** Arbitration replaced the chain with a better task. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Replaced);
	/** The chain root's priority dropped to TaskThreshold with InterruptIfReachesZero set. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_PriorityZero);
	/** The manager was deinitialized (unpossess, EndPlay). */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Deinitialized);
	/** SetRAIActive(false). */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Outcome_Deactivated);

	/** RequestReevaluation reason when a task is enabled or disabled. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reevaluate_TaskEnabledChanged);
	/** RequestReevaluation reason when a root task ended and the agent is idle. */
	RANCPRIORITYTASKAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reevaluate_ChainEnded);
}
