#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RAIController.generated.h"

class URAIManagerComponent;

/**
 * A custom AIController base to expose basic functionality of the TeamSystem to Blueprint, and 
 * provide a way to modify the result from GetFocalPointOnActor() with Blueprints.
 *  
 * This solves:
 * 1) AI always focuses on players feet when Aim offsets are enabled. 
 * 2) A way for the AI to ignore sensing each other if they are on the same team. 
 * AI sensing each other can cause uncessary slowdowns when a bunch of AIs on the same team are together
 * 
 */
UCLASS()
class RANCPRIORITYTASKAI_API ARAIController : public AAIController
{
	GENERATED_BODY()

public:
	ARAIController();

protected:
	virtual void BeginPlay() override;
	
public:
	
//*************************************************************************
//* Configuration
//*************************************************************************

	/* Max count of thoughts before old ones get deleted */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category = Configuration)
	int MaxThoughtMemoryCount = 30;

	/* Whether the RAI system should handle forwarding sensory input  to tasks using the built in sensory system.
	 * Set to false if you dont want input or want to call the manager sensory input methods yourself */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Transient,Category = Configuration)
	bool AutoHandleSensoryInput = true;

	
	
//*************************************************************************
//* Status variables
//*************************************************************************
	
	/* An array of traced thoughts used primarily for debugging */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Status)
	TArray<FString> Thoughts;
	
	
	/* An array of traced thoughts used primarily for debugging */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Status)
	URAIManagerComponent* ManagerComponent;
	
//*************************************************************************
//* Methods
//*************************************************************************
	
	/*  Add a thought to Thoughts */
	UFUNCTION(BlueprintCallable, Category = Basic)
	void TraceThought(FString Thought);

private:
	
	UPROPERTY()
	UAIPerceptionComponent* AIPerceptionComponent;

	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	
/////////////////////// Below are stuff that i dont know if i want to keep //////////////////////////
public:
	/*
	Actor location is typically the root of the objects. 
	Pawns will have their root at the feet, but we want to focus at a point above the
	feet, like a chest or face.  

	To do this, I override GetFocalPointOnActor(const AActor* Actor) const
	If the given actor is a APawn, then the focal point will be
	
	ActorLocation + FVector(0.0f,0.0f,FocusEyeHeight)*ActorScale.Z

	Otherwise, I call the super function
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = FocusSettings)
	float FocusEyeHeight = 80.0f;

	FVector GetFocalPointOnActor(const AActor* Actor) const override;

	
};
