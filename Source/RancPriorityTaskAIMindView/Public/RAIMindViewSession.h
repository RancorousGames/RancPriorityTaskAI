// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "RAIMindViewTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "RAIMindViewSession.generated.h"

class APlayerController;
class URAIMindViewSubsystem;
class URAIMindViewTrackRule;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRAIMindViewSessionEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FRAIMindViewSessionSnapshot, const FRAIMindViewSnapshot&);

/**
 * One local player's MindView state: primary subject, tracked set, aggregate rules, global pins, time controls.
 * Turns UI state into demand on the world's URAIMindViewSubsystem. Disabled sessions hold no demand and do not tick.
 * Views read GetSnapshot()/OnSnapshot and never touch mind components directly.
 */
UCLASS(Config = GameUserSettings)
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSession : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	URAIMindViewSession();

	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void SetEnabled(bool bInEnabled);
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") bool IsEnabled() const { return bEnabled; }

	/** Make Subject the inspected NPC; by default it is also added to the tracked set. */
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void SelectPrimary(AActor* Subject, bool bAlsoTrack = true);
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") AActor* GetPrimary() const { return Primary.Get(); }
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void Track(AActor* Subject);
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void Untrack(AActor* Subject);
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void ToggleTracked(AActor* Subject);
	/** Clears manual tracking and all rules. The primary stays inspected in the panel. */
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void ClearTracked();
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") bool IsTracked(const AActor* Subject) const;
	/** Manual plus rule results, primary excluded unless tracked. */
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") TArray<AActor*> GetTracked() const;
	/** The tracked subjects that currently get head cards (closest MaxCards). */
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") TArray<AActor*> GetCarded() const;

	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void AddRule(URAIMindViewTrackRule* Rule);
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void RemoveRule(URAIMindViewTrackRule* Rule);
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") const TArray<URAIMindViewTrackRule*>& GetRules() const { return ObjectPtrDecay(Rules); }

	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void SetPins(const TArray<FRAIMindViewPin>& InPins);
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void TogglePin(FName SectionId);
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") bool IsPinned(FName SectionId) const;
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") const TArray<FRAIMindViewPin>& GetPins() const { return Pins; }

	/** Sections the main panel currently shows for the primary (the open tab). */
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void SetPanelSections(const TArray<FName>& Sections);

	/** Subject under a viewport position: a visibility hit, else the nearest subject within FallbackRadius pixels. */
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") AActor* PickSubjectAt(FVector2D ScreenPosition, float FallbackRadius = 40.f) const;

	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void TogglePause();
	/** Unpause for Seconds of game time, then pause again. */
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void Step(float Seconds = 0.1f);
	UFUNCTION(BlueprintCallable, Category = "RAI|MindView") void SetSlowMotion(float Scale);
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") bool IsGamePaused() const;

	/** Latest snapshot for a subject, if demanded. */
	const FRAIMindViewSnapshot* GetSnapshot(const AActor* Subject) const;
	/** Supported sections of a subject (for tabs); empty when the world has no MindView subsystem. */
	UFUNCTION(BlueprintPure, Category = "RAI|MindView") TArray<FName> GetSupportedSections(AActor* Subject) const;
	/** Normally resolved from the local player; tests and custom hosts may bind a world explicitly. */
	void BindWorld(UWorld* World);

	UPROPERTY(BlueprintAssignable, Category = "RAI|MindView") FRAIMindViewSessionEvent OnTrackingChanged;
	UPROPERTY(BlueprintAssignable, Category = "RAI|MindView") FRAIMindViewSessionEvent OnPrimaryChanged;
	FRAIMindViewSessionSnapshot OnSnapshot;

	UPROPERTY(EditAnywhere, Config, Category = "RAI|MindView") int32 MaxCards = 25;
	UPROPERTY(EditAnywhere, Config, Category = "RAI|MindView") float PrimaryRateHz = 4.f;
	UPROPERTY(EditAnywhere, Config, Category = "RAI|MindView") float CardRateHz = 2.f;
	UPROPERTY(EditAnywhere, Config, Category = "RAI|MindView") float RuleIntervalSeconds = 1.f;

	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual UWorld* GetWorld() const override;

	/** Rebuild tracking and demand now (normally done by Tick). */
	void RefreshNow();

private:
	APlayerController* GetPlayerController() const;
	bool GetViewPoint(FVector& OutLocation) const;
	void EvaluateRules();
	void PushDemand();
	FName GetConsumerId() const;
	void HandleSnapshot(const FRAIMindViewSnapshot& Snapshot);
	void HandleSubjectLost(FObjectKey Key);
	void BindSubsystem();
	void UnbindSubsystem();
	void SetTicking(bool bTick);

	bool bEnabled = false;
	TWeakObjectPtr<AActor> Primary;
	TArray<TWeakObjectPtr<AActor>> Manual;
	TArray<TWeakObjectPtr<AActor>> FromRules;
	TArray<TWeakObjectPtr<AActor>> Carded;
	TArray<FName> PanelSections;
	TMap<FObjectKey, FRAIMindViewSnapshot> Snapshots;
	TMap<FObjectKey, TWeakObjectPtr<AActor>> Demanded;
	TMap<FObjectKey, FRAIMindViewDemand> LastSent;

	UPROPERTY(Transient) TArray<TObjectPtr<URAIMindViewTrackRule>> Rules;
	UPROPERTY(Config) TArray<FRAIMindViewPin> Pins;

	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<URAIMindViewSubsystem> Subsystem;
	FDelegateHandle SnapshotHandle, LostHandle;
	double NextRuleRealTime = 0.0;
	double StepUntilGameTime = -1.0;
};
