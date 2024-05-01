// Copyright Rancorous Games, 2024

#include "SubSystems/RAIKnowledgeComponent.h"
#include "GameFramework/Actor.h"

// Constructor
URAIKnowledgeComponent::URAIKnowledgeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    // Initialization code here, if needed
}

// Checks if a specific relation exists for an actor
bool URAIKnowledgeComponent::HasRelation(AActor* Actor, FGameplayTag Relation)
{
    if (!Actor || !RelationshipFacts.Contains(Actor))
    {
        return false;
    }

    for (const FRelationshipFact& Fact : RelationshipFacts[Actor])
    {
        if (Fact.Relation == Relation)
        {
            return true;
        }
    }

    return false;
}

// Retrieves all relations for a given actor
TArray<FRelationshipFact> URAIKnowledgeComponent::GetAllRelations(AActor* Actor)
{
    TArray<FRelationshipFact> Result;
    if (Actor && RelationshipFacts.Contains(Actor))
    {
        Result = RelationshipFacts[Actor];
    }
    return Result;
}

// Retrieves all relations of a specific category for a given actor
TArray<FRelationshipFact> URAIKnowledgeComponent::GetAllRelationsOfCategory(AActor* Actor, FGameplayTag Category)
{
    TArray<FRelationshipFact> Result;
    if (!Actor || !RelationshipFacts.Contains(Actor))
    {
        return Result;
    }

    for (const FRelationshipFact& Fact : RelationshipFacts[Actor])
    {
        if (Fact.Category == Category)
        {
            Result.Add(Fact);
        }
    }

    return Result;
}

// Adds a new relation for an actor
void URAIKnowledgeComponent::AddRelation(AActor* Actor, const FRelationshipFact& RelationshipFact)
{
    if (Actor)
    {
        if (GetOwner()->HasAuthority())
        {
            // Directly update on the server
            AddRelationMulticast(Actor, RelationshipFact);
        }
        else
        {
            // Call the server function to replicate
            ServerAddRelation(Actor, RelationshipFact);
        }
    }
}

// Replicates the addition of a new relation to all clients
void URAIKnowledgeComponent::AddRelationMulticast_Implementation(AActor* Actor, const FRelationshipFact& RelationshipFact)
{
    if (Actor)
    {
        RelationshipFacts.FindOrAdd(Actor).Add(RelationshipFact);
    }
}

// Server RPC to add a new relation
void URAIKnowledgeComponent::ServerAddRelation_Implementation(AActor* Actor, const FRelationshipFact& RelationshipFact)
{
    AddRelationMulticast(Actor, RelationshipFact);
}

// Removes a specific relation for an actor
void URAIKnowledgeComponent::RemoveRelation(AActor* Actor, FGameplayTag Relation)
{
    if (Actor)
    {
        if (GetOwner()->HasAuthority())
        {
            RemoveRelationMulticast(Actor, Relation);
        }
        else
        {
            ServerRemoveRelation(Actor, Relation);
        }
    }
}

// Replicates the removal of a relation to all clients
void URAIKnowledgeComponent::RemoveRelationMulticast_Implementation(AActor* Actor, FGameplayTag Relation)
{
    if (Actor && RelationshipFacts.Contains(Actor))
    {
        TArray<FRelationshipFact>& Facts = RelationshipFacts[Actor];
        for (int32 Index = 0; Index < Facts.Num(); ++Index)
        {
            if (Facts[Index].Relation == Relation)
            {
                Facts.RemoveAt(Index);
                break;
            }
        }
    }
}

// Server RPC to remove a relation
void URAIKnowledgeComponent::ServerRemoveRelation_Implementation(AActor* Actor, FGameplayTag Relation)
{
    RemoveRelationMulticast(Actor, Relation);
}

// Removes all relations of a specific category for an actor
void URAIKnowledgeComponent::RemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category)
{
    if (Actor)
    {
        if (GetOwner()->HasAuthority())
        {
            RemoveAllRelationsOfCategoryMulticast(Actor, Category);
        }
        else
        {
            ServerRemoveAllRelationsOfCategory(Actor, Category);
        }
    }
}

// Replicates the removal of all relations of a category to all clients
void URAIKnowledgeComponent::RemoveAllRelationsOfCategoryMulticast_Implementation(AActor* Actor, FGameplayTag Category)
{
    if (Actor && RelationshipFacts.Contains(Actor))
    {
        TArray<FRelationshipFact>& Facts = RelationshipFacts[Actor];
        for (int32 Index = Facts.Num() - 1; Index >= 0; --Index)
        {
            if (Facts[Index].Category == Category)
            {
                Facts.RemoveAt(Index);
            }
        }
    }
}

// Server RPC to remove all relations of a category
void URAIKnowledgeComponent::ServerRemoveAllRelationsOfCategory_Implementation(AActor* Actor, FGameplayTag Category)
{
    RemoveAllRelationsOfCategoryMulticast(Actor, Category);
}
