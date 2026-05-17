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

	// Resolve the sibling URAIMemoryComponent that should have been created
	// alongside this one via CreateDefaultSubobject in the controller.
	if (!Memory)
	{
		Memory = GetOwner()->FindComponentByClass<URAIMemoryComponent>();
	}

	// Wire sub-components to the event bus.
	if (Memory)
	{
		OnLifeEvent.AddDynamic(Memory, &URAIMemoryComponent::EncodeEpisodic);
	}
}

void URAIMindComponent::Witness(FRAILifeEvent Event)
{
	Event.WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
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
