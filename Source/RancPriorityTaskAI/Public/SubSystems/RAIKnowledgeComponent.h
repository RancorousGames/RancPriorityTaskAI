// Copyright Rancorous Games, 2024

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "RAIKnowledgeComponent.generated.h"

/**
 * Struct representing a relationship fact.
 */
USTRUCT(BlueprintType)
struct FRelationshipFact
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge|Relationships")
    float TotalDuration = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge|Relationships")
    float RemainingDuration = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge|Relationships")
    FGameplayTag Relation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge|Relationships")
    FGameplayTag Category;
};

/**
 * Actor Component for handling AI knowledge, focusing on relationships.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup=(RAI), meta=(BlueprintSpawnableComponent, DeprecatedNode, DeprecationMessage="Legacy relationship store; use a mind/memory component for new cognition."))
class RANCPRIORITYTASKAI_API URAIKnowledgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URAIKnowledgeComponent();
    virtual void BeginPlay() override;

protected:
    // Map storing relationship facts for each actor.
    TMap<TWeakObjectPtr<AActor>, TArray<FRelationshipFact>> RelationshipFacts;
    void PruneInvalidActors();

public:
    // Blueprint-accessible methods.
    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    bool HasRelation(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    TArray<FRelationshipFact> GetAllRelations(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    TArray<FRelationshipFact> GetAllRelationsOfCategory(AActor* Actor, FGameplayTag Category);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    void AddRelation(AActor* Actor, const FRelationshipFact& RelationshipFact);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void AddRelationMulticast(AActor* Actor, const FRelationshipFact& RelationshipFact);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void ServerAddRelation(AActor* Actor, const FRelationshipFact& RelationshipFact);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    void RemoveRelation(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void RemoveRelationMulticast(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void ServerRemoveRelation(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    void RemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void RemoveAllRelationsOfCategoryMulticast(AActor* Actor, FGameplayTag Category);

    UFUNCTION(BlueprintCallable, Category="Knowledge|Relationships", meta=(DeprecatedFunction, DeprecationMessage="Local authority-only wrapper; does not replicate."))
    void ServerRemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category);

    UE_DEPRECATED(5.8, "Use AddRelation; legacy replication is unsupported.")
    virtual void AddRelationMulticast_Implementation(AActor* Actor, const FRelationshipFact& Fact) { AddRelation(Actor, Fact); }
    UE_DEPRECATED(5.8, "Use AddRelation; legacy replication is unsupported.")
    virtual void ServerAddRelation_Implementation(AActor* Actor, const FRelationshipFact& Fact) { AddRelation(Actor, Fact); }
    UE_DEPRECATED(5.8, "Use RemoveRelation; legacy replication is unsupported.")
    virtual void RemoveRelationMulticast_Implementation(AActor* Actor, FGameplayTag Relation) { RemoveRelation(Actor, Relation); }
    UE_DEPRECATED(5.8, "Use RemoveRelation; legacy replication is unsupported.")
    virtual void ServerRemoveRelation_Implementation(AActor* Actor, FGameplayTag Relation) { RemoveRelation(Actor, Relation); }
    UE_DEPRECATED(5.8, "Use RemoveAllRelationsOfCategory; legacy replication is unsupported.")
    virtual void RemoveAllRelationsOfCategoryMulticast_Implementation(AActor* Actor, FGameplayTag Category) { RemoveAllRelationsOfCategory(Actor, Category); }
    UE_DEPRECATED(5.8, "Use RemoveAllRelationsOfCategory; legacy replication is unsupported.")
    virtual void ServerRemoveAllRelationsOfCategory_Implementation(AActor* Actor, FGameplayTag Category) { RemoveAllRelationsOfCategory(Actor, Category); }

};
