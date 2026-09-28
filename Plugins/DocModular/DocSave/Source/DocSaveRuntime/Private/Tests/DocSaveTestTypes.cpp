#include "Tests/DocSaveTestTypes.h"
#include "DocSaveableComponent.h"
#include "Components/SceneComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSaveTestTypes)

void UDocSaveTestStateComponent::CaptureDocSaveState_Implementation(TArray<FDocSavePayload>& OutPayloads) const
{
	FDocSavePayload Payload;
	Payload.ComponentKey = TEXT("State");
	Payload.Version = PayloadVersion;
	FDocSaveTestState Data;
	Data.Counter = Counter;
	Data.Label = Label;
	Data.Friend = Friend;
	Payload.Data.InitializeAs<FDocSaveTestState>(Data);
	OutPayloads.Add(MoveTemp(Payload));
}

bool UDocSaveTestStateComponent::RestoreDocSaveState_Implementation(const FDocSavePayload& Payload)
{
	if (Payload.ComponentKey != TEXT("State"))
	{
		return false;
	}
	const FDocSaveTestState* Data = Payload.Data.GetPtr<FDocSaveTestState>();
	if (!Data)
	{
		return false;
	}
	Counter = Data->Counter;
	Label = Data->Label;
	Friend = Data->Friend;
	++RestoreCalls;
	return true;
}

void UDocSaveTestStateComponent::ResolveDocSaveReferences_Implementation()
{
	++ResolveCalls;
}

ADocSaveTestActor::ADocSaveTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Saveable = CreateDefaultSubobject<UDocSaveableComponent>(TEXT("Saveable"));
	State = CreateDefaultSubobject<UDocSaveTestStateComponent>(TEXT("State"));
}
