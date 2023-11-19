#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"

#include "RAITaskInvokeArguments.generated.h"

USTRUCT(BlueprintType)
struct FRAITaskInvokeArguments
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector TargetLocation = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool ContinueUntilSuccess = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CustomInstruction = FString("");
};
