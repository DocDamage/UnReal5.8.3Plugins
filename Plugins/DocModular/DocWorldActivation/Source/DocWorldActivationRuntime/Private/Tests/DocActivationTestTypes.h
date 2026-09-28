#pragma once

// Test-only types for DocWorldActivation automation tests (always compiled; hidden).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "DocActivationTypes.h"
#include "DocActivationTestTypes.generated.h"

class UDocWorldActivationComponent;

UCLASS(NotBlueprintable, HideDropdown, NotPlaceable)
class UDocActivationTestParticipant : public UActorComponent, public IDocActivationParticipant
{
	GENERATED_BODY()

public:
	UDocActivationTestParticipant() { PrimaryComponentTick.bCanEverTick = true; }

	int32 Changes = 0;
	EDocActivationTier LastTier = EDocActivationTier::Active;

	virtual void OnActivationTierChanged_Implementation(EDocActivationTier OldTier, EDocActivationTier NewTier) override { ++Changes; LastTier = NewTier; }
	virtual EDocActivationTier GetCustomDesiredTier_Implementation(EDocActivationTier Current) const override { return EDocActivationTier::Lightweight; }
};

UCLASS(NotBlueprintable, HideDropdown, NotPlaceable)
class ADocActivationTestActor : public AActor
{
	GENERATED_BODY()

public:
	ADocActivationTestActor();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDocWorldActivationComponent> Activation;
	UPROPERTY() TObjectPtr<UDocActivationTestParticipant> Participant;
};
