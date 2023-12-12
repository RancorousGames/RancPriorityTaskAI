// Copyright Rancorous Games, 2023

#pragma once
#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Engine/DataTable.h"
//#include "Math/IntPoint.h"
#include "Curves/CurveFloat.h"
#include "RAIDataStructures.generated.h"

/*
Always = This task can always be interrupted by another task. Interruption will occur when a better task to do is found.
Never = This task can never be interrupted by another task, no matter what.
Priority = This task can only be interrupted by tasks with a greater priority number.
*/
UENUM(BlueprintType)
enum class ERAIInterruptionType : uint8
{
	Always,
	WaitASec,
	PreferablyNot,
	OnlyIfNeeded,
	IfPanic,
	IfLifeOrDeath,
	Never
};

UENUM(BlueprintType)
enum class ERAIField : uint8
{
	NearField,
	DistantField
	// ProbabilisticField
};

UENUM(BlueprintType)
enum class EDoneWaitingExecutionStates : uint8
{
	Continue,
	TaskEnded
};
