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
UCLASS(Blueprintable, BlueprintType, ClassGroup=(RAI), meta=(BlueprintSpawnableComponent))
class RANCPRIORITYTASKAI_API URAIKnowledgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URAIKnowledgeComponent();

protected:
    // Map storing relationship facts for each actor.
    TMap<AActor*, TArray<FRelationshipFact>> RelationshipFacts;

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

    UFUNCTION(NetMulticast, Reliable)
    void AddRelationMulticast(AActor* Actor, const FRelationshipFact& RelationshipFact);

    UFUNCTION(Server, Reliable)
    void ServerAddRelation(AActor* Actor, const FRelationshipFact& RelationshipFact);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    void RemoveRelation(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(NetMulticast, Reliable)
    void RemoveRelationMulticast(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(Server, Reliable)
    void ServerRemoveRelation(AActor* Actor, FGameplayTag Relation);

    UFUNCTION(BlueprintCallable, Category = "Knowledge|Relationships")
    void RemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category);

    UFUNCTION(NetMulticast, Reliable)
    void RemoveAllRelationsOfCategoryMulticast(AActor* Actor, FGameplayTag Category);

    UFUNCTION(Server, Reliable)
    void ServerRemoveAllRelationsOfCategory(AActor* Actor, FGameplayTag Category);

};