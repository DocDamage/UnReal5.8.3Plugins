#include "NonCharacterInteractionGameMode.h"

#include "DocInteractionTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

ANonCharacterInteractor::ANonCharacterInteractor()
{
	PrimaryActorTick.bCanEverTick = false;
	Interactor = CreateDefaultSubobject<UDocInteractorComponent>(TEXT("Interactor"));
	Interactor->bAutoDetect = false;
}

AInteractionTargetActor::AInteractionTargetActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Interactable = CreateDefaultSubobject<UDocInteractableComponent>(TEXT("Interactable"));

	FDocInteractionDefinition UseDefinition;
	UseDefinition.DefinitionId = TEXT("SampleUse");
	UseDefinition.InteractionTag = DocInteractionTags::Use;
	UseDefinition.DisplayName = FText::FromString(TEXT("Use sample target"));
	UseDefinition.Prompt = FText::FromString(TEXT("Run the consumer interaction smoke flow"));
	UseDefinition.Mode = EDocInteractionMode::Instant;
	Interactable->Definitions.Add(MoveTemp(UseDefinition));
}

void ANonCharacterInteractionGameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DOC_CONSUMER_SMOKE_FAILED: GameMode has no world."));
		return;
	}

	FActorSpawnParameters SpawnParameters;
	ANonCharacterInteractor* InteractorActor = World->SpawnActor<ANonCharacterInteractor>(
		ANonCharacterInteractor::StaticClass(), FTransform::Identity, SpawnParameters);
	AInteractionTargetActor* TargetActor = World->SpawnActor<AInteractionTargetActor>(
		AInteractionTargetActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(100.f, 0.f, 0.f)), SpawnParameters);
	if (!InteractorActor || !TargetActor || !InteractorActor->Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("DOC_CONSUMER_SMOKE_FAILED: Could not spawn sample actors/components."));
		return;
	}

	const FDocInteractionSessionInfo Session = InteractorActor->Interactor->RequestInteractionWithTarget(TargetActor, TEXT("SampleUse"));
	if (Session.State != EDocInteractionSessionState::Completed || InteractorActor->IsA<ACharacter>())
	{
		UE_LOG(LogTemp, Error, TEXT("DOC_CONSUMER_SMOKE_FAILED: state=%d result=%s character=%s"),
			static_cast<int32>(Session.State), *Session.Result.Diagnostic, InteractorActor->IsA<ACharacter>() ? TEXT("true") : TEXT("false"));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("DOC_NONCHARACTER_INTERACTION_PASSED: interactor=ANonCharacterInteractor target=AInteractionTargetActor state=Completed"));
}
