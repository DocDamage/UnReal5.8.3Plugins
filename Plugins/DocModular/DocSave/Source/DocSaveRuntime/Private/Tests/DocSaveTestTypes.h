#pragma once

// Test-only types for DocSave automation tests. Always compiled (UHT needs them);
// hidden from editor pickers.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "DocSaveTypes.h"
#include "DocSaveTestTypes.generated.h"

class UDocSaveableComponent;

USTRUCT()
struct FDocSaveTestState
{
	GENERATED_BODY()

	UPROPERTY() int32 Counter = 0;
	UPROPERTY() FString Label;
	UPROPERTY() FDocPersistentObjectId Friend;
};

/** Not allowlisted anywhere: used to prove the payload allowlist. */
USTRUCT()
struct FDocSaveTestForbiddenState
{
	GENERATED_BODY()

	UPROPERTY() int32 Value = 0;
};

UCLASS(NotBlueprintable, HideDropdown, NotPlaceable)
class UDocSaveTestStateComponent : public UActorComponent, public IDocSaveParticipant
{
	GENERATED_BODY()

public:
	int32 Counter = 0;
	FString Label;
	FDocPersistentObjectId Friend;
	int32 PayloadVersion = 1;
	int32 RestoreCalls = 0;
	int32 ResolveCalls = 0;

	virtual void CaptureDocSaveState_Implementation(TArray<FDocSavePayload>& OutPayloads) const override;
	virtual bool RestoreDocSaveState_Implementation(const FDocSavePayload& Payload) override;
	virtual void ResolveDocSaveReferences_Implementation() override;
};

UCLASS(NotBlueprintable, HideDropdown, NotPlaceable)
class ADocSaveTestActor : public AActor
{
	GENERATED_BODY()

public:
	ADocSaveTestActor();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDocSaveableComponent> Saveable;
	UPROPERTY() TObjectPtr<UDocSaveTestStateComponent> State;
};
