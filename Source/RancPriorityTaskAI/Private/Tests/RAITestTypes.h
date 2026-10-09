#pragma once

#include "RAIController.h"
#include "RAITaskNative.h"
#include "RAIInterruptPolicy.h"
#include "RAITestTypes.generated.h"

USTRUCT()
struct FRAITestPayload
{
	GENERATED_BODY()
	UPROPERTY() int32 Value = 0;
};

UCLASS()
class URAITestInterruptPolicy : public URAIInterruptPolicy
{
	GENERATED_BODY()
public:
	virtual bool ShouldInterrupt(const FRAIArbitrationContext&, const URAITaskComponent*, const URAITaskComponent*) const override { return true; }
};

UCLASS()
class URAITestTask : public URAITaskNative
{
	GENERATED_BODY()
public:
	float Score = 10.f;
	int32 Begins = 0;
	int32 Ends = 0;
	int32 Completions = 0;
	bool bLastSuccess = false;
	bool bLastInterrupted = false;
	TFunction<void()> OnBegin;
	TFunction<void()> OnEnd;
	TFunction<void()> OnCompleted;
	bool bAllowEquivalentHandoff = false;
	TWeakObjectPtr<AActor> ExpectedAdoptTarget;
	int32 Adoptions = 0;
	virtual bool CanRelinquishInvokedChain(const URAITaskComponent*) const override { return bAllowEquivalentHandoff; }
	virtual bool CanAdoptInvokedChain(const URAITaskComponent* PreviousRoot, const URAITaskComponent* DirectChild) const override
	{
		return bAllowEquivalentHandoff && PreviousRoot && DirectChild
			&& DirectChild->GetInvokeArgs().TargetActor == ExpectedAdoptTarget.Get();
	}
	virtual void OnInvokedChainAdopted(const URAITaskComponent*, URAITaskComponent*) override { ++Adoptions; }
	virtual float NativeCalculatePriority() override { return Score; }
	virtual void DescribePriority(FRAIPriorityExplanation& Out) const override { Out.Add(TEXT("Constant"), Score); }
	virtual void NativeBeginTask(const FRAITaskInvokeArguments&) override
	{
		++Begins;
		if (OnBegin) OnBegin();
	}
	virtual void NativeEndTask(bool bSuccess, bool bInterrupted) override
	{
		++Ends;
		bLastInterrupted = bInterrupted;
		if (OnEnd) OnEnd();
	}
	virtual void NativeOnInvokedTaskCompleted(bool bSuccess) override
	{
		++Completions;
		bLastSuccess = bSuccess;
		if (OnCompleted) OnCompleted();
	}
};

UCLASS()
class URAITestChild : public URAITestTask
{
	GENERATED_BODY()
};

UCLASS()
class URAITestGrandchild : public URAITestTask
{
	GENERATED_BODY()
};

UCLASS()
class ARAITestController : public ARAIController
{
	GENERATED_BODY()
public:
	ARAITestController(const FObjectInitializer& Initializer);
};
