#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "DocInteractionComponents.h"
#include "NonCharacterInteractionGameMode.generated.h"

UCLASS()
class NONCHARACTERINTERACTION_API ANonCharacterInteractor : public AActor
{
	GENERATED_BODY()

public:
	ANonCharacterInteractor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UDocInteractorComponent> Interactor;
};

UCLASS()
class NONCHARACTERINTERACTION_API AInteractionTargetActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractionTargetActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UDocInteractableComponent> Interactable;
};

UCLASS()
class NONCHARACTERINTERACTION_API ANonCharacterInteractionGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void StartPlay() override;
};
