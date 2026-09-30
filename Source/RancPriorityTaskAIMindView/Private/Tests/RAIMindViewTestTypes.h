// Copyright Rancorous Games, 2026

#pragma once

#include "RAIController.h"
#include "RAITaskNative.h"
#include "RAIMindViewTestTypes.generated.h"

UCLASS()
class URAIMindViewTestTask : public URAITaskNative
{
	GENERATED_BODY()
public:
	float Score = 10.f;
	virtual float NativeCalculatePriority() override { return Score; }
	virtual void DescribePriority(FRAIPriorityExplanation& Out) const override { Out.Add(TEXT("Constant"), Score); }
};

UCLASS()
class URAIMindViewTestTaskB : public URAIMindViewTestTask
{
	GENERATED_BODY()
};

UCLASS()
class URAIMindViewTestChild : public URAIMindViewTestTask
{
	GENERATED_BODY()
};

UCLASS()
class ARAIMindViewTestController : public ARAIController
{
	GENERATED_BODY()
public:
	ARAIMindViewTestController(const FObjectInitializer& Initializer);
};
