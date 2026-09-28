#include "Tests/DocActivationTestTypes.h"
#include "DocWorldActivationSubsystem.h"
#include "Components/SceneComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocActivationTestTypes)

ADocActivationTestActor::ADocActivationTestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Activation = CreateDefaultSubobject<UDocWorldActivationComponent>(TEXT("Activation"));
	Participant = CreateDefaultSubobject<UDocActivationTestParticipant>(TEXT("Participant"));
}
