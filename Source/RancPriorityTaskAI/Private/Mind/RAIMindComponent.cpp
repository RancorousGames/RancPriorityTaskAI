// Copyright Rancorous Games, 2024

#include "Mind/RAIMindComponent.h"
#include "Mind/RAIMemoryComponent.h"
#include "Engine/World.h"

URAIMindComponent::URAIMindComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URAIMindComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!Memory)
	{
		Memory = GetOwner()->FindComponentByClass<URAIMemoryComponent>();
	}

	// Wire sub-components to the event bus.
	if (Memory)
	{
		OnLifeEvent.AddUniqueDynamic(Memory, &URAIMemoryComponent::EncodeEpisodic);
	}
}

void URAIMindComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Memory) OnLifeEvent.RemoveDynamic(Memory, &URAIMemoryComponent::EncodeEpisodic);
	Super::EndPlay(Reason);
}

void URAIMindComponent::Witness(FRAILifeEvent Event)
{
	Event.WorldTime = TimeSource ? TimeSource->Now() : (Memory ? Memory->GetNow() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0));
	if (!Event.OriginId.IsValid()) Event.OriginId = FGuid::NewGuid();
	OnLifeEvent.Broadcast(Event);
}

float URAIMindComponent::ValenceTowards(AActor* Subject) const
{
	return Memory ? Memory->ValenceTowards(Subject) : 0.f;
}

bool URAIMindComponent::Knows(FGameplayTag Subject, FGameplayTag Predicate) const
{
	return Memory ? Memory->Knows(Subject, Predicate) : false;
}
