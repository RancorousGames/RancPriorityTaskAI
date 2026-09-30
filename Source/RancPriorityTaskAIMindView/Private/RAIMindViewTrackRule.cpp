// Copyright Rancorous Games, 2026

#include "RAIMindViewTrackRule.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "RAIMindViewSubsystem.h"
#include "RAIMindViewTags.h"

void URAIMindViewTrackRule::ForEachSubjectPawn(const FRAIMindViewRuleContext& Context, TFunctionRef<void(APawn*)> Func) const
{
	if (!Context.World) return;
	for (TActorIterator<APawn> It(Context.World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!bIncludePlayerControlled && Pawn->IsPlayerControlled()) continue;
		if (URAIMindViewSubsystem::IsSubject(Pawn)) Func(Pawn);
	}
}

void URAIMindViewRule_AllSubjects::Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const
{
	ForEachSubjectPawn(Context, [&Out](APawn* Pawn) { Out.Add(Pawn); });
}

void URAIMindViewRule_OfKind::Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const
{
	URAIMindViewSubsystem* Subsystem = URAIMindViewSubsystem::Get(Context.World);
	ForEachSubjectPawn(Context, [this, Subsystem, &Out](APawn* Pawn)
	{
		FRAIMindViewHeader Header;
		Header.Kind = RAIMindViewTags::Kind_Default;
		if (Subsystem && Subsystem->DescribeSubject) Subsystem->DescribeSubject(Pawn, Header);
		if (Header.Kind.MatchesTag(Kind)) Out.Add(Pawn);
	});
}

void URAIMindViewRule_NearView::Evaluate(const FRAIMindViewRuleContext& Context, TArray<AActor*>& Out) const
{
	const float RadiusSquared = FMath::Square(Radius);
	ForEachSubjectPawn(Context, [&Context, &Out, RadiusSquared](APawn* Pawn)
	{
		if (FVector::DistSquared(Pawn->GetActorLocation(), Context.ViewLocation) <= RadiusSquared) Out.Add(Pawn);
	});
}
