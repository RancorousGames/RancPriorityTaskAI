// Copyright Rancorous Games, 2024

#include "SubSystems/RAIKnowledgeComponent.h"
#include "GameFramework/Actor.h"
#include "RAILogCategory.h"

URAIKnowledgeComponent::URAIKnowledgeComponent() { PrimaryComponentTick.bCanEverTick = false; }
void URAIKnowledgeComponent::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogRAI, Warning, TEXT("RAIKnowledgeComponent is deprecated; relationships are local authority-only data."));
}
void URAIKnowledgeComponent::PruneInvalidActors()
{
    for (auto It = RelationshipFacts.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
}
bool URAIKnowledgeComponent::HasRelation(AActor* Actor, FGameplayTag Relation)
{
    for (const FRelationshipFact& Fact : GetAllRelations(Actor))
        if (Fact.Relation == Relation) return true;
    return false;
}
TArray<FRelationshipFact> URAIKnowledgeComponent::GetAllRelations(AActor* Actor)
{
    PruneInvalidActors();
    const TArray<FRelationshipFact>* Facts = RelationshipFacts.Find(Actor);
    return IsValid(Actor) && Facts ? *Facts : TArray<FRelationshipFact>{};
}
TArray<FRelationshipFact> URAIKnowledgeComponent::GetAllRelationsOfCategory(AActor* Actor, FGameplayTag Category)
{
    TArray<FRelationshipFact> Result;
    for (const FRelationshipFact& Fact : GetAllRelations(Actor))
        if (Fact.Category == Category) Result.Add(Fact);
    return Result;
}
void URAIKnowledgeComponent::AddRelation(AActor* Actor, const FRelationshipFact& Fact)
{
    PruneInvalidActors();
    if (IsValid(Actor) && GetOwner() && GetOwner()->HasAuthority()) RelationshipFacts.FindOrAdd(Actor).Add(Fact);
}
void URAIKnowledgeComponent::RemoveRelation(AActor* Actor, FGameplayTag Relation)
{
    PruneInvalidActors();
    if (!IsValid(Actor) || !GetOwner() || !GetOwner()->HasAuthority()) return;
    if (TArray<FRelationshipFact>* Facts = RelationshipFacts.Find(Actor))
    {
        const int32 Index = Facts->IndexOfByPredicate([Relation](const FRelationshipFact& Fact) { return Fact.Relation == Relation; });
        if (Index != INDEX_NONE) Facts->RemoveAt(Index);
    }
}
void URAIKnowledgeComponent::RemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category)
{
    PruneInvalidActors();
    if (!IsValid(Actor) || !GetOwner() || !GetOwner()->HasAuthority()) return;
    if (TArray<FRelationshipFact>* Facts = RelationshipFacts.Find(Actor))
        Facts->RemoveAll([Category](const FRelationshipFact& Fact) { return Fact.Category == Category; });
}
void URAIKnowledgeComponent::AddRelationMulticast(AActor* Actor, const FRelationshipFact& Fact) { AddRelation(Actor, Fact); }
void URAIKnowledgeComponent::ServerAddRelation(AActor* Actor, const FRelationshipFact& Fact) { AddRelation(Actor, Fact); }
void URAIKnowledgeComponent::RemoveRelationMulticast(AActor* Actor, FGameplayTag Relation) { RemoveRelation(Actor, Relation); }
void URAIKnowledgeComponent::ServerRemoveRelation(AActor* Actor, FGameplayTag Relation) { RemoveRelation(Actor, Relation); }
void URAIKnowledgeComponent::RemoveAllRelationsOfCategoryMulticast(AActor* Actor, FGameplayTag Category) { RemoveAllRelationsOfCategory(Actor, Category); }
void URAIKnowledgeComponent::ServerRemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category) { RemoveAllRelationsOfCategory(Actor, Category); }
