// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"

#include "RAITaskInvokeArguments.generated.h"

USTRUCT(BlueprintType)
struct FRAITaskInvokeArguments
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Invoke")
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Invoke")
	FVector TargetLocation = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Invoke")
	bool ContinueUntilSuccess = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Invoke")
	FString CustomInstruction = FString("");
};
