﻿// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "ISortableElement.generated.h"

UINTERFACE(BlueprintType)
class RANCPRIORITYTASKAI_API USortableElement : public UInterface
{
	GENERATED_BODY()
};

class RANCPRIORITYTASKAI_API ISortableElement
{
	GENERATED_BODY()

public:
	// Function to compare this object with another sortable object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Sorting")
	bool IsLessThan(const UObject* Other) const;
};