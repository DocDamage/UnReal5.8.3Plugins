#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocGameplayContext.h"
#include "DocPersistentObjectId.h"
#include "DocRequestHandle.h"
#include "DocCoreBlueprintLibrary.generated.h"

class AActor;

/** Blueprint access to DocModularCore value types. All Pure nodes are side-effect free. */
UCLASS()
class DOCMODULARCORERUNTIME_API UDocCoreBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Results ----

	/** True for Succeeded and No Change. */
	UFUNCTION(BlueprintPure, Category = "Doc|Result")
	static bool IsResultSuccess(const FDocSystemResult& Result) { return Result.IsSuccess(); }

	/** True only for Succeeded (state actually changed). False for No Change and all failures. */
	UFUNCTION(BlueprintPure, Category = "Doc|Result")
	static bool IsResultChanged(const FDocSystemResult& Result) { return Result.IsChanged(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Result", meta = (DisplayName = "To String (Doc Result)", CompactNodeTitle = "->"))
	static FString ResultToString(const FDocSystemResult& Result) { return Result.ToString(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Result")
	static FDocSystemResult MakeSuccessResult() { return FDocSystemResult::MakeSuccess(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Result")
	static FDocSystemResult MakeNoChangeResult(const FString& Diagnostic) { return FDocSystemResult::MakeNoChange(Diagnostic); }

	/** ErrorTag may be empty; the default Doc.Error.* tag for Outcome is used. */
	UFUNCTION(BlueprintPure, Category = "Doc|Result")
	static FDocSystemResult MakeFailureResult(EDocResultOutcome Outcome, const FString& Diagnostic, FGameplayTag ErrorTag, FText UserMessage);

	// ---- Context ----

	UFUNCTION(BlueprintPure, Category = "Doc|Context", meta = (WorldContext = "WorldContextObject"))
	static FDocGameplayContext MakeDocGameplayContext(const UObject* WorldContextObject, AActor* Instigator, AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Doc|Context")
	static EDocNetAuthority GetContextAuthority(const FDocGameplayContext& Context) { return Context.ResolveAuthority(); }

	// ---- Persistent identity ----

	UFUNCTION(BlueprintPure, Category = "Doc|Identity", meta = (DisplayName = "Is Valid (Doc Persistent Id)"))
	static bool IsPersistentIdValid(const FDocPersistentObjectId& Id) { return Id.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Identity", meta = (DisplayName = "Equal (Doc Persistent Id)", CompactNodeTitle = "==", Keywords = "== equal"))
	static bool EqualPersistentId(const FDocPersistentObjectId& A, const FDocPersistentObjectId& B) { return A == B; }

	UFUNCTION(BlueprintPure, Category = "Doc|Identity", meta = (DisplayName = "To String (Doc Persistent Id)", CompactNodeTitle = "->"))
	static FString PersistentIdToString(const FDocPersistentObjectId& Id) { return Id.ToString(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Identity")
	static bool ParsePersistentId(const FString& Text, FDocPersistentObjectId& OutId);

	UFUNCTION(BlueprintPure, Category = "Doc|Identity")
	static FGuid ComposeInstanceScope(const FGuid& ParentScope, const FGuid& PlacementGuid)
	{
		return FDocPersistentObjectId::ComposeInstanceScope(ParentScope, PlacementGuid);
	}

	/**
	 * Build an ID for a runtime-spawned object with a newly generated local GUID.
	 * Only the authority should call this, once per logical object; store the
	 * result and restore it from saves rather than calling again.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Identity")
	static FDocPersistentObjectId MakeRuntimePersistentId(const FGuid& WorldNamespace, const FGuid& InstanceScope);

	/** Reads IDocPersistentIdentity. Returns an invalid ID if Object is null or does not implement it. */
	UFUNCTION(BlueprintPure, Category = "Doc|Identity")
	static FDocPersistentObjectId GetPersistentIdFromObject(const UObject* Object);

	// ---- Handles ----

	/** True if the handle was ever issued. Does not prove it is still active; ask its owning system. */
	UFUNCTION(BlueprintPure, Category = "Doc|Handle", meta = (DisplayName = "Is Set (Doc Request Handle)"))
	static bool IsHandleSet(const FDocRequestHandle& Handle) { return Handle.IsSet(); }

	UFUNCTION(BlueprintPure, Category = "Doc|Handle", meta = (DisplayName = "Equal (Doc Request Handle)", CompactNodeTitle = "==", Keywords = "== equal"))
	static bool EqualHandle(const FDocRequestHandle& A, const FDocRequestHandle& B) { return A == B; }

	UFUNCTION(BlueprintPure, Category = "Doc|Handle", meta = (DisplayName = "To String (Doc Request Handle)", CompactNodeTitle = "->"))
	static FString HandleToString(const FDocRequestHandle& Handle) { return Handle.ToString(); }

	// ---- Tags ----

	/**
	 * Collect Object's tags via IDocGameplayTagProvider, else IGameplayTagAssetInterface.
	 * Returns false when Object supports neither (caller should treat as "unknown", not "no tags").
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Tags")
	static bool GetOwnedTagsFromObject(const UObject* Object, FGameplayTagContainer& OutTags);
};
