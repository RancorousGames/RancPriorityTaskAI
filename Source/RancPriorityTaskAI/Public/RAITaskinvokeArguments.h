// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "StructUtils/InstancedStruct.h"

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

	/** Typed payload for the invoked task. Set with SetPayload<T>() in C++ or Make Instanced Struct in Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Invoke")
	FInstancedStruct Payload;

	/** Returns the payload if it is (or derives from) T, otherwise null. */
	template <typename T>
	const T* GetPayload() const
	{
		return Payload.GetPtr<T>();
	}

	template <typename T>
	void SetPayload(const T& Value)
	{
		Payload.InitializeAs<T>(Value);
	}
};
