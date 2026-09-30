// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "RAIMindViewTrackRule.generated.h"

class APawn;
class UWorld;

struct FRAIMindViewRuleContext
{
	UWorld* World = nullptr;
	FVector ViewLocation = FVector::ZeroVector;
	/** The viewing player's pawn, if any. */
	APawn* ViewerPawn = nullptr;
};

/** A live rule that adds subjects to the tracked set. Evaluated periodically while MindView is enabled. */
UCLASS(Abstract, EditInlineNew, BlueprintType)
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewTrackRule : public UObject
{
	GENERATED_BODY()

public:
	virtual void Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const PURE_VIRTUAL(URAIMindViewTrackRule::Evaluate,);
	virtual FText GetLabel() const { return FText::FromName(GetClass()->GetFName()); }

	/** Player-controlled bodies are excluded unless this is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|MindView")
	bool bIncludePlayerControlled = false;

protected:
	/** Iterates pawns in the world that are MindView subjects, honouring bIncludePlayerControlled. */
	void ForEachSubjectPawn(const FRAIMindViewRuleContext& Context, TFunctionRef<void(APawn*)> Func) const;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewRule_AllSubjects : public URAIMindViewTrackRule
{
	GENERATED_BODY()
public:
	virtual void Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const override;
	virtual FText GetLabel() const override { return NSLOCTEXT("RAIMindView", "RuleAll", "All"); }
};

/** Subjects whose described kind (header Kind) matches. */
UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewRule_OfKind : public URAIMindViewTrackRule
{
	GENERATED_BODY()
public:
	virtual void Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const override;
	virtual FText GetLabel() const override { return FText::FromName(Kind.GetTagName()); }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|MindView") FGameplayTag Kind;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewRule_NearView : public URAIMindViewTrackRule
{
	GENERATED_BODY()
public:
	virtual void Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const override;
	virtual FText GetLabel() const override { return NSLOCTEXT("RAIMindView", "RuleNear", "Nearby"); }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RAI|MindView", meta = (ClampMin = "0")) float Radius = 3000.f;
};
