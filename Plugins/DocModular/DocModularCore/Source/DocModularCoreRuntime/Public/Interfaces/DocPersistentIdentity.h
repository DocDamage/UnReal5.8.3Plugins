#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocPersistentObjectId.h"
#include "DocPersistentIdentity.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UDocPersistentIdentity : public UInterface
{
	GENERATED_BODY()
};

/**
 * Supplies a durable identity without requiring DocSave.
 *
 * Implementations must return the same ID for the object's whole logical life,
 * including across stream-out/in and save/restore. Authored IDs are serialized
 * with the instance; runtime-spawned objects receive their ID once from the
 * authority and must restore it from saves. An object that has not been assigned
 * an identity returns an invalid ID (never a freshly generated one).
 */
class DOCMODULARCORERUNTIME_API IDocPersistentIdentity
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Identity")
	FDocPersistentObjectId GetDocPersistentId() const;
};
