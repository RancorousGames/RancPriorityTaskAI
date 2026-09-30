// Copyright Rancorous Games, 2026

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RAIDataStructures.h"

class AActor;

/** Arguments of one debug thought. Text is formatted by the viewer from Kind, never by the caller. */
struct FRAIThoughtArgs
{
	FGameplayTag Kind;
	ERAIThoughtTone Tone = ERAIThoughtTone::Neutral;
	const AActor* A = nullptr;
	const AActor* B = nullptr;
	float V0 = 0.f;
	float V1 = 0.f;
	FName P;

	FRAIThoughtArgs(FGameplayTag InKind, ERAIThoughtTone InTone = ERAIThoughtTone::Neutral, const AActor* InA = nullptr,
		const AActor* InB = nullptr, float InV0 = 0.f, float InV1 = 0.f, FName InP = NAME_None)
		: Kind(InKind), Tone(InTone), A(InA), B(InB), V0(InV0), V1(InV1), P(InP) {}
};

/**
 * Dormant entry points used by the optional MindView module (RancPriorityTaskAIMindView).
 * Nothing is recorded unless a MindView consumer has demand; IsActive() is a single static bool read.
 * Thoughts are a write-only explanatory trace: gameplay code must never read them back.
 */
struct RANCPRIORITYTASKAI_API FRAIMindViewHooks
{
#if !UE_BUILD_SHIPPING
	using FThoughtSink = TFunction<void(const AActor* Subject, const FRAIThoughtArgs& Args, const FString* LegacyText)>;

	static bool IsActive() { return bActive; }
	static void EmitThought(const AActor* Subject, const FRAIThoughtArgs& Args);
	static void EmitLegacyThought(const AActor* Subject, const FString& Text);

	/** Installed once by the MindView module. */
	static void SetSink(FThoughtSink InSink);
	/** Reference-counted by MindView subsystems that currently have demand. */
	static void AddActivation();
	static void RemoveActivation();
	static int32 GetActivationCount() { return ActivationCount; }

private:
	static void Refresh();
	static bool bActive;
	static int32 ActivationCount;
	static FThoughtSink Sink;
#else
	static constexpr bool IsActive() { return false; }
#endif
};

/**
 * Record a typed debug thought for a subject (usually the pawn). Arguments are not evaluated while MindView is inactive,
 * and the macro compiles to nothing in Shipping.
 * Example: RAI_THOUGHT(Pawn, WTTags::Thought_Defer, ERAIThoughtTone::Neutral, OtherPerson);
 */
#if !UE_BUILD_SHIPPING
#define RAI_THOUGHT(Subject, ...) do { if (FRAIMindViewHooks::IsActive()) { FRAIMindViewHooks::EmitThought((Subject), FRAIThoughtArgs(__VA_ARGS__)); } } while (0)
#else
#define RAI_THOUGHT(Subject, ...) do { } while (0)
#endif
