#include "DocInteractionTestTypes.h"
#include "Components/SceneComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionTestTypes)

ADocInteractionTestActor::ADocInteractionTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
