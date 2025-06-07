// Copyright Rancorous Games, 2023

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RAIController.generated.h"

class URAIManagerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnThoughtTrace, FString, Thought);

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
	ARAIController(const FObjectInitializer& ObjectInitializer): ManagerComponent(nullptr), AIPerceptionComponent(nullptr){}
	ARAIController(): ManagerComponent(nullptr), AIPerceptionComponent(nullptr){}

protected:
	virtual void BeginPlay() override;
	
	virtual void OnPossess(APawn* InPawn) override;
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
	
	UPROPERTY(BlueprintAssignable, Category = RAI)
	FOnThoughtTrace OnThoughtTrace;
	
	/*  Add a thought to Thoughts */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void TraceThought(FString Thought);

	/* Triggers a custom event on the task of the specified class, react to it by overloading OnCustomTrigger in Task 
	 * Payload may be any object you want to pass to the task */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void TriggerCustom(TSubclassOf<URAITaskComponent> Task, FGameplayTag Trigger, UObject* Payload);
	
	/* Triggers a custom event on all tasks, react to it by overloading OnCustomTrigger in Task
	 * Payload may be any object you want to pass to the task */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void TriggerCustomAll(FGameplayTag Trigger, UObject* Payload);
	
	/*  Enable Disable the AI, e.g. call on death of the character */
	UFUNCTION(BlueprintCallable, Category = RAI)
	void SetRAIActive(bool ShouldBeActive);
	
	/*  Whether the AI is enabled/disabled, set by calling SetRAIActive */
	UFUNCTION(BlueprintCallable, Category = RAI)
	bool IsRAIActive();

	//~ Smooth Path AI Functions
	//----------------------------------------------------------------------//
protected:
	/**
	 * Overridden to intercept the movement request and generate a custom, curved path.
	 * If the curve generation fails, it falls back to the parent AAIController's MoveTo logic.
	 */
	virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath = nullptr) override;

	/**
	 * The core function that generates the curved path. It creates a list of candidate points
	 * and then validates and stitches path segments between them.
	 * @param MoveRequest The original request from the behavior tree or game logic.
	 * @return A valid, stitched path if successful, or a null pointer if it fails.
	 */
	FNavPathSharedPtr GenerateSmoothPath(const FAIMoveRequest& MoveRequest) const;

private:
	/**
	 * A helper function to append a new path segment to an existing composite path.
	 * @param InOutBasePath The path to be extended. This will be modified by appending points.
	 * @param PathToAdd The new path segment to append.
	 */
	void StitchPathSegments(FNavPathSharedPtr& InOutBasePath, const FNavPathSharedPtr& PathToAdd) const;

public:
	//~ Smooth Path Configuration Parameters
	//----------------------------------------------------------------------//

	UPROPERTY(EditAnywhere, Category = "AI|Smooth Path")
	bool bEnableSmoothPaths = false;
	
	UPROPERTY(EditAnywhere, Category = "AI|Smooth Path")
    bool bDebugSmoothPath = false;
	
	/** The maximum angle (in degrees) the character can turn in a single path segment. Smaller values create wider, smoother curves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smooth Path", meta = (ClampMin = "5.0", ClampMax = "90.0"))
	float CurveAngleThreshold = 45.0f;

	/** The maximum number of segments to generate for the curve. Prevents infinite loops and performance issues in complex situations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smooth Path", meta = (ClampMin = "2", ClampMax = "20"))
	int32 MaxCurveSegments = 8;

	/** The minimum length for any generated curve segment. Prevents generating very small, twitchy segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smooth Path", meta = (ClampMin = "50.0"))
	float MinCurveSegmentLength = 100.0f;

private:
	
	UPROPERTY()
	UAIPerceptionComponent* AIPerceptionComponent;

	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	bool bRAIActive = true;
};
