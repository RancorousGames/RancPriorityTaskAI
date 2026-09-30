// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "RAIMindViewTypes.h"
#include "UObject/Object.h"
#include "RAIMindViewSection.generated.h"

class APawn;
class URAIManagerComponent;
class URAIMemoryComponent;
class URAIMindViewRecorder;

/** Read access to one subject during capture. Authority only; never stored. */
struct FRAIMindViewCaptureContext
{
	AActor* Subject = nullptr;
	APawn* Pawn = nullptr;
	URAIManagerComponent* Manager = nullptr;
	URAIMemoryComponent* Memory = nullptr;
	const URAIMindViewRecorder* Recorder = nullptr;
	double Now = 0.0;
	ERAIMindViewDetail Detail = ERAIMindViewDetail::Full;
	/** Row count for compact lists. */
	int32 Count = 5;
};

/**
 * Produces one section of a MindView snapshot. Subclass in game modules for domain state
 * (register the class with URAIMindViewSubsystem::RegisterSectionClass at module startup).
 */
UCLASS(Abstract)
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection : public UObject
{
	GENERATED_BODY()

public:
	virtual FName GetSectionId() const PURE_VIRTUAL(URAIMindViewSection::GetSectionId, return NAME_None;);
	virtual ERAIMindViewTruth GetTruth() const { return ERAIMindViewTruth::Process; }
	virtual bool Supports(const FRAIMindViewCaptureContext& Context) const { return true; }
	/** Fill Out.Data and Out.Revision. Id, Truth and Detail are set by the subsystem. */
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const PURE_VIRTUAL(URAIMindViewSection::Capture,);
	/** Contribution to the subject's state revision for the repeat-without-change detector (0 when not applicable). */
	virtual uint32 GetStateRevision(const FRAIMindViewCaptureContext& Context) const { return 0; }
	/** Bind extra hooks only while the subject is demanded. */
	virtual void OnDemandBegin(AActor* Subject) {}
	virtual void OnDemandEnd(AActor* Subject) {}
};

namespace RAIMindViewSections
{
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName Decisions;
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName Chain;
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName History;
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName Thoughts;
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName Detectors;
	RANCPRIORITYTASKAIMINDVIEW_API extern const FName Memory;
}

/** Ranked purposes with priority terms. Read live from the manager so values stay current between arbitration events. */
UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_Decisions : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::Decisions; }
	virtual bool Supports(const FRAIMindViewCaptureContext& Context) const override { return Context.Manager != nullptr; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_Chain : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::Chain; }
	virtual bool Supports(const FRAIMindViewCaptureContext& Context) const override { return Context.Manager != nullptr; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_History : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::History; }
	virtual bool Supports(const FRAIMindViewCaptureContext& Context) const override { return Context.Manager != nullptr; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_Thoughts : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::Thoughts; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_Detectors : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::Detectors; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
	/** A detection newer than this marks the subject with a detector flag. */
	UPROPERTY(EditAnywhere, Category = "RAI|MindView") double ActiveSeconds = 30.0;
};

UCLASS()
class RANCPRIORITYTASKAIMINDVIEW_API URAIMindViewSection_Memory : public URAIMindViewSection
{
	GENERATED_BODY()
public:
	virtual FName GetSectionId() const override { return RAIMindViewSections::Memory; }
	virtual ERAIMindViewTruth GetTruth() const override { return ERAIMindViewTruth::Belief; }
	virtual bool Supports(const FRAIMindViewCaptureContext& Context) const override { return Context.Memory != nullptr; }
	virtual void Capture(const FRAIMindViewCaptureContext& Context, FRAIMindViewSectionData& Out) const override;
	virtual uint32 GetStateRevision(const FRAIMindViewCaptureContext& Context) const override;
};
