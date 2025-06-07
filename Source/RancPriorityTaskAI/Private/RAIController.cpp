// Copyright Rancorous Games, 2023

#include "RAIController.h"

#include "GameplayTagContainer.h"
#include "RAILogCategory.h"
#include "RAIManagerComponent.h"
#include "RAITaskComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "VisualLogger/VisualLogger.h"

class URAITaskComponent;
class URAIManagerComponent;

// Define a log category for smooth path AI functionality
DEFINE_LOG_CATEGORY_STATIC(LogSmoothPathAI, Log, All);

inline void ARAIController::BeginPlay()
{
	Super::BeginPlay();
	
	if (AutoHandleSensoryInput)
	{
		AIPerceptionComponent = GetAIPerceptionComponent();
		if (!AIPerceptionComponent)
		{
			// Create and attach a new perception component if it doesn't exist
			AIPerceptionComponent = NewObject<UAIPerceptionComponent>(this);
			if (AIPerceptionComponent)
			{
				AIPerceptionComponent->RegisterComponent();
			}
		}
	
		AIPerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &ARAIController::OnPerceptionUpdated);
	}
}

void ARAIController::TraceThought(FString Thought)
{
    Thoughts.Add(Thought);
	OnThoughtTrace.Broadcast(Thought);

	// Check if the thoughts count exceeds the maximum allowed
	if (Thoughts.Num() > MaxThoughtMemoryCount)
	{
		// Calculate the number of thoughts to remove
		int32 ThoughtsToRemove = Thoughts.Num() - MaxThoughtMemoryCount;

		// Remove the older half of thoughts
		Thoughts.RemoveAt(0, ThoughtsToRemove);
	}
}

void ARAIController::TriggerCustom(TSubclassOf<URAITaskComponent> Task, FGameplayTag Trigger, UObject* Payload)
{
	if (ManagerComponent)
	{
		if (auto* FoundTask = ManagerComponent->GetTaskByClass(Task))
		{
			FoundTask->OnCustomTrigger(Trigger, Payload);
		}
		else
		{
			UE_LOG(LogRAI, Warning, TEXT("TriggerCustom: Task not found: %s"), *Task->GetName());
		}
	}
}

void ARAIController::TriggerCustomAll(FGameplayTag Trigger, UObject* Payload)
{
	if (ManagerComponent)
	{
		for (auto* Task : ManagerComponent->AllTasks)
		{
			Task->OnCustomTrigger(Trigger, Payload);
		}
	}
}

void ARAIController::SetRAIActive(bool ShouldBeActive)
{
	bRAIActive = ShouldBeActive;
	ManagerComponent->SetActive(ShouldBeActive);

	TraceThought(FString("RAI set to: ") + (ShouldBeActive ? "Active" : "Inactive"));
	if (ShouldBeActive)
	{
		AIPerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &ARAIController::OnPerceptionUpdated);
	}
	else
	{
		AIPerceptionComponent->OnTargetPerceptionUpdated.RemoveDynamic(this, &ARAIController::OnPerceptionUpdated);
	}
}

bool ARAIController::IsRAIActive()
{
	return bRAIActive;
}

void ARAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	
	if (!ManagerComponent)
	{
		ManagerComponent = FindComponentByClass<URAIManagerComponent>();
	}

	if (ManagerComponent)
	{
		ManagerComponent->Initialize(this, InPawn);
	}
}

void ARAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (ManagerComponent)
	{
		ManagerComponent->OnPerceptionStimulus(Actor, Stimulus);
	}
}

//~ Smooth Path AI Implementation
//----------------------------------------------------------------------//

FPathFollowingRequestResult ARAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	if (!bEnableSmoothPaths)
		return Super::MoveTo(MoveRequest, OutPath);
	
	// Correctly construct the result struct by setting members individually.
	FPathFollowingRequestResult ResultData;
	ResultData.Code = EPathFollowingRequestResult::Failed;

	UPathFollowingComponent* PFollowComp = GetPathFollowingComponent();

	if (!MoveRequest.IsValid())
	{
		UE_VLOG(this, LogSmoothPathAI, Error, TEXT("MoveTo request failed: MoveRequest is not valid."));
		return ResultData;
	}

	if (PFollowComp == nullptr)
	{
		UE_VLOG(this, LogSmoothPathAI, Error, TEXT("MoveTo request failed: Missing PathFollowingComponent."));
		return ResultData;
	}
	
	if (PFollowComp->HasReached(MoveRequest))
	{
		UE_VLOG(this, LogSmoothPathAI, Log, TEXT("MoveTo: Already at goal!"));
		ResultData.MoveId = PFollowComp->RequestMoveWithImmediateFinish(EPathFollowingResult::Success);
		ResultData.Code = EPathFollowingRequestResult::AlreadyAtGoal;
		return ResultData;
	}

	// --- Custom Smooth Path Logic ---
	UE_VLOG(this, LogSmoothPathAI, Log, TEXT("Attempting to generate a smooth path..."));
	FNavPathSharedPtr SmoothPath = GenerateSmoothPath(MoveRequest);

	if (SmoothPath.IsValid() && SmoothPath->IsValid() && SmoothPath->GetPathPoints().Num() > 0)
	{
		UE_VLOG(this, LogSmoothPathAI, Log, TEXT("Successfully generated smooth path with %d points."), SmoothPath->GetPathPoints().Num());
		
		if (OutPath)
		{
			*OutPath = SmoothPath;
		}
		
		ResultData.MoveId = RequestMove(MoveRequest, SmoothPath);
		ResultData.Code = EPathFollowingRequestResult::RequestSuccessful;
		return ResultData;
	}

	// --- Fallback to Default Behavior ---
	UE_VLOG(this, LogSmoothPathAI, Warning, TEXT("Failed to generate a smooth path. Falling back to default AAIController::MoveTo."));
	return Super::MoveTo(MoveRequest, OutPath);
}

FNavPathSharedPtr ARAIController::GenerateSmoothPath(const FAIMoveRequest& MoveRequest) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return nullptr;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) return nullptr;

	TArray<FVector> CandidatePoints;
	FVector CurrentPos = ControlledPawn->GetActorLocation();
	FVector CurrentDir = ControlledPawn->GetActorForwardVector().GetSafeNormal2D();
	const FVector GoalLocation = MoveRequest.GetGoalLocation();

	CandidatePoints.Add(CurrentPos);

	const float AngleThresholdDot = FMath::Cos(FMath::DegreesToRadians(CurveAngleThreshold));

	for (int32 i = 0; i < MaxCurveSegments; ++i)
	{
		const FVector ToTarget = (GoalLocation - CurrentPos);
		const float DistToTarget = ToTarget.Size2D();

		if (DistToTarget < MinCurveSegmentLength) break;

		const FVector TargetDir = ToTarget.GetSafeNormal2D();
		if (FVector::DotProduct(CurrentDir, TargetDir) >= AngleThresholdDot) break;

		const FVector CrossProduct = FVector::CrossProduct(CurrentDir, TargetDir);
		const float TurnSign = FMath::Sign(CrossProduct.Z);
		
		const FQuat TurnQuat(FVector::UpVector, TurnSign * FMath::DegreesToRadians(CurveAngleThreshold));
		const FVector NextDir = TurnQuat.RotateVector(CurrentDir);

		const int32 RemainingSegments = FMath::Max(1, MaxCurveSegments - i);
		const float SegmentLength = FMath::Max(MinCurveSegmentLength, DistToTarget / static_cast<float>(RemainingSegments));

		const FVector NextPos = CurrentPos + NextDir * SegmentLength;
		
		CandidatePoints.Add(NextPos);

		UE_VLOG_LOCATION(this, LogSmoothPathAI, Verbose, NextPos, 25.f, FColor::Yellow, TEXT("Candidate Point %d"), i);

		// --- Debug line for intermediate points ---
		if (bDebugSmoothPath && i > 0)
		{
			DrawDebugLine(GetWorld(), CandidatePoints[i], CandidatePoints[i + 1], FColor::Green, false, 5.f, 0, 2.0f);
		}

		CurrentPos = NextPos;
		CurrentDir = NextDir;
	}

	CandidatePoints.Add(GoalLocation);

	// --- Debug line to the final goal ---
	if (bDebugSmoothPath && CandidatePoints.Num() >= 2)
	{
		DrawDebugLine(GetWorld(), CandidatePoints[CandidatePoints.Num() - 2], GoalLocation, FColor::Red, false, 5.f, 0, 3.0f);
		DrawDebugSphere(GetWorld(), GoalLocation, 25.f, 12, FColor::Red, false, 5.f);
	}

	if (CandidatePoints.Num() < 2)
	{
		UE_VLOG(this, LogSmoothPathAI, Warning, TEXT("Not enough candidate points generated to form a path."));
		return nullptr;
	}

	FNavPathSharedPtr CompositePath = nullptr;

	for (int32 i = 0; i < CandidatePoints.Num() - 1; ++i)
	{
		const FVector& StartPoint = CandidatePoints[i];
		const FVector& EndPoint = CandidatePoints[i + 1];

		FPathFindingQuery Query;
		if (!BuildPathfindingQuery(MoveRequest, StartPoint, Query))
		{
			UE_VLOG(this, LogSmoothPathAI, Error, TEXT("Failed to build pathfinding query for segment %d."), i);
			return nullptr;
		}
		Query.EndLocation = EndPoint;

		FPathFindingResult PathResult = NavSys->FindPathSync(Query);

		if (PathResult.IsSuccessful() && PathResult.Path.IsValid())
		{
			UE_VLOG(this, LogSmoothPathAI, Verbose, TEXT("✓ SUCCESS: Found path for segment %d to %d."), i, i + 1);

			if (!CompositePath.IsValid())
			{
				CompositePath = PathResult.Path;
			}
			else
			{
				StitchPathSegments(CompositePath, PathResult.Path);
			}
		}
		else
		{
			UE_VLOG(this, LogSmoothPathAI, Warning, TEXT("✗ FAILED: Could not find path for segment %d to %d. Aborting smooth path generation."), i, i + 1);
			return nullptr;
		}
	}

	if (CompositePath.IsValid() && MoveRequest.GetGoalActor() != nullptr)
	{
		CompositePath->SetGoalActorObservation(*MoveRequest.GetGoalActor(), 100.0f);
	}

	return CompositePath;
}

void ARAIController::StitchPathSegments(FNavPathSharedPtr& InOutBasePath, const FNavPathSharedPtr& PathToAdd) const
{
	if (!InOutBasePath.IsValid() || !PathToAdd.IsValid() || PathToAdd->GetPathPoints().Num() <= 1)
	{
		return;
	}
	
	TArray<FNavPathPoint>& BasePoints = InOutBasePath->GetPathPoints();
	const TArray<FNavPathPoint>& PointsToAdd = PathToAdd->GetPathPoints();

	// Append all points from the new path EXCEPT the first one, which is a duplicate
	// of the last point of the base path.
	BasePoints.Append(PointsToAdd.GetData() + 1, PointsToAdd.Num() - 1);
}
