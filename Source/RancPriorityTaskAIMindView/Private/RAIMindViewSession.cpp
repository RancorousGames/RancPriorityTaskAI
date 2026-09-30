// Copyright Rancorous Games, 2026

#include "RAIMindViewSession.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "RAIMindViewSection.h"
#include "RAIMindViewSubsystem.h"
#include "RAIMindViewTrackRule.h"

URAIMindViewSession::URAIMindViewSession()
	: FTickableGameObject(ETickableTickType::Never)
{
}

void URAIMindViewSession::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (Pins.IsEmpty())
	{
		Pins.Add({RAIMindViewSections::Thoughts, 5});
		Pins.Add({RAIMindViewSections::Decisions, 3});
	}
	PanelSections = {RAIMindViewSections::Decisions, RAIMindViewSections::Chain, RAIMindViewSections::History};
}

void URAIMindViewSession::Deinitialize()
{
	SetEnabled(false);
	UnbindSubsystem();
	Super::Deinitialize();
}

UWorld* URAIMindViewSession::GetWorld() const
{
	if (UWorld* World = BoundWorld.Get()) return World;
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->GetWorld() : nullptr;
}

void URAIMindViewSession::BindWorld(UWorld* World)
{
	if (BoundWorld.Get() == World) return;
	const bool bWasEnabled = bEnabled;
	SetEnabled(false);
	BoundWorld = World;
	SetEnabled(bWasEnabled);
}

TStatId URAIMindViewSession::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URAIMindViewSession, STATGROUP_Tickables);
}

ETickableTickType URAIMindViewSession::GetTickableTickType() const
{
	return !IsTemplate() && bEnabled ? ETickableTickType::Always : ETickableTickType::Never;
}

void URAIMindViewSession::SetTicking(bool bTick)
{
	if (!IsTemplate()) SetTickableTickType(bTick ? ETickableTickType::Always : ETickableTickType::Never);
}

void URAIMindViewSession::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled) return;
	bEnabled = bInEnabled;
	if (bEnabled)
	{
		BindSubsystem();
		NextRuleRealTime = 0.0;
		RefreshNow();
	}
	else
	{
		if (URAIMindViewSubsystem* S = Subsystem.Get()) S->ClearConsumer(GetConsumerId());
		Demanded.Reset();
		LastSent.Reset();
		Snapshots.Reset();
		Carded.Reset();
		UnbindSubsystem();
	}
	SetTicking(bEnabled);
	OnTrackingChanged.Broadcast();
}

FName URAIMindViewSession::GetConsumerId() const
{
	return FName(TEXT("MindViewSession"), GetUniqueID());
}

void URAIMindViewSession::BindSubsystem()
{
	URAIMindViewSubsystem* S = URAIMindViewSubsystem::Get(this);
	if (Subsystem.Get() == S) return;
	UnbindSubsystem();
	Subsystem = S;
	if (S)
	{
		SnapshotHandle = S->OnSnapshot.AddUObject(this, &URAIMindViewSession::HandleSnapshot);
		LostHandle = S->OnSubjectLost.AddUObject(this, &URAIMindViewSession::HandleSubjectLost);
	}
}

void URAIMindViewSession::UnbindSubsystem()
{
	if (URAIMindViewSubsystem* S = Subsystem.Get())
	{
		S->OnSnapshot.Remove(SnapshotHandle);
		S->OnSubjectLost.Remove(LostHandle);
	}
	SnapshotHandle.Reset();
	LostHandle.Reset();
	Subsystem.Reset();
}

void URAIMindViewSession::SelectPrimary(AActor* Subject, bool bAlsoTrack)
{
	if (!URAIMindViewSubsystem::IsSubject(Subject)) return;
	if (bAlsoTrack && !IsTracked(Subject)) Manual.Add(Subject);
	if (Primary.Get() != Subject)
	{
		Primary = Subject;
		OnPrimaryChanged.Broadcast();
	}
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::Track(AActor* Subject)
{
	if (!URAIMindViewSubsystem::IsSubject(Subject) || Manual.Contains(Subject)) return;
	Manual.Add(Subject);
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::Untrack(AActor* Subject)
{
	const int32 Removed = Manual.Remove(Subject) + FromRules.Remove(Subject);
	if (!Removed) return;
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::ToggleTracked(AActor* Subject)
{
	if (Manual.Contains(Subject)) Untrack(Subject);
	else Track(Subject);
}

void URAIMindViewSession::ClearTracked()
{
	Manual.Reset();
	FromRules.Reset();
	Rules.Reset();
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

bool URAIMindViewSession::IsTracked(const AActor* Subject) const
{
	return Subject && (Manual.Contains(Subject) || FromRules.Contains(Subject));
}

TArray<AActor*> URAIMindViewSession::GetTracked() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Weak : Manual) if (AActor* Actor = Weak.Get()) Result.AddUnique(Actor);
	for (const TWeakObjectPtr<AActor>& Weak : FromRules) if (AActor* Actor = Weak.Get()) Result.AddUnique(Actor);
	return Result;
}

TArray<AActor*> URAIMindViewSession::GetCarded() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Weak : Carded) if (AActor* Actor = Weak.Get()) Result.Add(Actor);
	return Result;
}

void URAIMindViewSession::AddRule(URAIMindViewTrackRule* Rule)
{
	if (!Rule || Rules.Contains(Rule)) return;
	Rules.Add(Rule);
	NextRuleRealTime = 0.0;
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::RemoveRule(URAIMindViewTrackRule* Rule)
{
	if (!Rules.Remove(Rule)) return;
	NextRuleRealTime = 0.0;
	RefreshNow();
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::SetPins(const TArray<FRAIMindViewPin>& InPins)
{
	Pins = InPins;
	SaveConfig();
	RefreshNow();
}

void URAIMindViewSession::TogglePin(FName SectionId)
{
	const int32 Index = Pins.IndexOfByPredicate([SectionId](const FRAIMindViewPin& Pin) { return Pin.SectionId == SectionId; });
	if (Index != INDEX_NONE) Pins.RemoveAt(Index);
	else Pins.Add({SectionId, 5});
	SaveConfig();
	RefreshNow();
}

bool URAIMindViewSession::IsPinned(FName SectionId) const
{
	return Pins.ContainsByPredicate([SectionId](const FRAIMindViewPin& Pin) { return Pin.SectionId == SectionId; });
}

void URAIMindViewSession::SetPanelSections(const TArray<FName>& Sections)
{
	PanelSections = Sections;
	RefreshNow();
}

APlayerController* URAIMindViewSession::GetPlayerController() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	UWorld* World = GetWorld();
	return Player && World ? Player->GetPlayerController(World) : nullptr;
}

bool URAIMindViewSession::GetViewPoint(FVector& OutLocation) const
{
	if (const APlayerController* PC = GetPlayerController())
	{
		FRotator Rotation;
		PC->GetPlayerViewPoint(OutLocation, Rotation);
		return true;
	}
	return false;
}

AActor* URAIMindViewSession::PickSubjectAt(FVector2D ScreenPosition, float FallbackRadius) const
{
	APlayerController* PC = GetPlayerController();
	if (!PC) return nullptr;
	FHitResult Hit;
	if (PC->GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, Hit))
	{
		AActor* HitActor = Hit.GetActor();
		if (URAIMindViewSubsystem::IsSubject(HitActor)) return HitActor;
		if (const APawn* Instigator = HitActor ? HitActor->GetInstigator() : nullptr; URAIMindViewSubsystem::IsSubject(Instigator))
			return const_cast<APawn*>(Instigator);
	}
	AActor* Best = nullptr;
	float BestDistance = FallbackRadius;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		FVector2D Screen;
		if (!URAIMindViewSubsystem::IsSubject(*It) || !PC->ProjectWorldLocationToScreen(It->GetActorLocation(), Screen)) continue;
		const float Distance = FVector2D::Distance(Screen, ScreenPosition);
		if (Distance <= BestDistance) { BestDistance = Distance; Best = *It; }
	}
	return Best;
}

void URAIMindViewSession::TogglePause()
{
	StepUntilGameTime = -1.0;
	UGameplayStatics::SetGamePaused(this, !IsGamePaused());
}

void URAIMindViewSession::Step(float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World) return;
	StepUntilGameTime = World->GetTimeSeconds() + FMath::Max(Seconds, 0.f);
	UGameplayStatics::SetGamePaused(this, false);
	SetTicking(true);
}

void URAIMindViewSession::SetSlowMotion(float Scale)
{
	UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(Scale, 0.01f, 1.f));
}

bool URAIMindViewSession::IsGamePaused() const
{
	return UGameplayStatics::IsGamePaused(this);
}

const FRAIMindViewSnapshot* URAIMindViewSession::GetSnapshot(const AActor* Subject) const
{
	return Subject ? Snapshots.Find(FObjectKey(Subject)) : nullptr;
}

TArray<FName> URAIMindViewSession::GetSupportedSections(AActor* Subject) const
{
	const URAIMindViewSubsystem* S = URAIMindViewSubsystem::Get(this);
	return S ? S->GetSupportedSections(Subject) : TArray<FName>();
}

void URAIMindViewSession::HandleSnapshot(const FRAIMindViewSnapshot& Snapshot)
{
	const AActor* Subject = Snapshot.Header.Subject.Get();
	if (!Subject || !Demanded.Contains(FObjectKey(Subject))) return;
	Snapshots.Add(FObjectKey(Subject), Snapshot);
	OnSnapshot.Broadcast(Snapshot);
}

void URAIMindViewSession::HandleSubjectLost(FObjectKey Key)
{
	LastSent.Remove(Key);
	if (!Demanded.Remove(Key)) return;
	Snapshots.Remove(Key);
	Manual.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });
	FromRules.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });
	Carded.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });
	if (!Primary.IsValid() && !Primary.IsExplicitlyNull()) { Primary.Reset(); OnPrimaryChanged.Broadcast(); }
	OnTrackingChanged.Broadcast();
}

void URAIMindViewSession::Tick(float DeltaTime)
{
	if (StepUntilGameTime >= 0.0)
		if (const UWorld* World = GetWorld(); World && World->GetTimeSeconds() >= StepUntilGameTime)
		{
			StepUntilGameTime = -1.0;
			UGameplayStatics::SetGamePaused(this, true);
		}
	if (!bEnabled) { if (StepUntilGameTime < 0.0) SetTicking(false); return; }
	const double Real = FPlatformTime::Seconds();
	if (Real >= NextRuleRealTime) RefreshNow();
	else PushDemand();
}

void URAIMindViewSession::RefreshNow()
{
	if (!bEnabled) return;
	BindSubsystem();
	EvaluateRules();
	NextRuleRealTime = FPlatformTime::Seconds() + FMath::Max(RuleIntervalSeconds, 0.1f);
	PushDemand();
}

void URAIMindViewSession::EvaluateRules()
{
	FromRules.Reset();
	if (Rules.IsEmpty()) return;
	FRAIMindViewRuleContext Context;
	Context.World = GetWorld();
	GetViewPoint(Context.ViewLocation);
	if (const APlayerController* PC = GetPlayerController()) Context.ViewerPawn = PC->GetPawn();
	TArray<AActor*> Found;
	for (const URAIMindViewTrackRule* Rule : Rules) if (Rule) Rule->Evaluate(Context, Found);
	for (AActor* Actor : Found) if (!Manual.Contains(Actor)) FromRules.AddUnique(Actor);
}

void URAIMindViewSession::PushDemand()
{
	URAIMindViewSubsystem* S = Subsystem.Get();
	Manual.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });
	FromRules.RemoveAll([](const TWeakObjectPtr<AActor>& Weak) { return !Weak.IsValid(); });

	// Head cards: the closest MaxCards tracked subjects (all of them when there is no view point).
	TArray<AActor*> Tracked = GetTracked();
	FVector View;
	if (GetViewPoint(View))
		Tracked.Sort([&View](const AActor& A, const AActor& B) { return FVector::DistSquared(A.GetActorLocation(), View) < FVector::DistSquared(B.GetActorLocation(), View); });
	Carded.Reset();
	for (int32 Index = 0; Index < FMath::Min(Tracked.Num(), FMath::Max(MaxCards, 0)); ++Index) Carded.Add(Tracked[Index]);

	TMap<FObjectKey, TPair<TWeakObjectPtr<AActor>, FRAIMindViewDemand>> Wanted;
	auto Add = [&Wanted](AActor* Actor, const FRAIMindViewDemand& Demand)
	{
		auto& Entry = Wanted.FindOrAdd(FObjectKey(Actor));
		if (!Entry.Key.IsValid()) { Entry.Value.RateHz = 0.f; Entry.Value.CompactCount = 0; }
		Entry.Key = Actor;
		Entry.Value.MergeFrom(Demand);
	};
	FRAIMindViewDemand CardDemand;
	CardDemand.RateHz = CardRateHz;
	CardDemand.CompactCount = 0;
	for (const FRAIMindViewPin& Pin : Pins) { CardDemand.CompactSections.Add(Pin.SectionId); CardDemand.CompactCount = FMath::Max(CardDemand.CompactCount, Pin.Count); }
	for (const TWeakObjectPtr<AActor>& Weak : Carded) if (AActor* Actor = Weak.Get()) Add(Actor, CardDemand);
	if (AActor* PrimaryActor = Primary.Get())
	{
		FRAIMindViewDemand PanelDemand;
		PanelDemand.RateHz = PrimaryRateHz;
		PanelDemand.CompactCount = 0;
		PanelDemand.FullSections.Append(PanelSections);
		Add(PrimaryActor, PanelDemand);
	}

	if (S)
	{
		const FName Consumer = GetConsumerId();
		for (auto It = Demanded.CreateIterator(); It; ++It)
			if (!Wanted.Contains(It.Key()))
			{
				if (AActor* Actor = It.Value().Get()) S->SetDemand(Actor, Consumer, FRAIMindViewDemand());
				Snapshots.Remove(It.Key());
				It.RemoveCurrent();
			}
		for (auto& Pair : Wanted)
		{
			AActor* Actor = Pair.Value.Key.Get();
			if (!Actor) continue;
			// Only resend when this consumer's demand changed (the subsystem resets capture pacing on SetDemand).
			if (Demanded.Contains(Pair.Key) && LastSent.Contains(Pair.Key) && LastSent[Pair.Key] == Pair.Value.Value) continue;
			S->SetDemand(Actor, Consumer, Pair.Value.Value);
			Demanded.Add(Pair.Key, Actor);
			LastSent.Add(Pair.Key, Pair.Value.Value);
		}
		for (auto It = LastSent.CreateIterator(); It; ++It) if (!Demanded.Contains(It.Key())) It.RemoveCurrent();
	}
}
