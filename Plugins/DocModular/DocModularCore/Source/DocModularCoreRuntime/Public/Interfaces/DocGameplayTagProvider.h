#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocGameplayContext.h"
#include "DocGameplayTagProvider.generated.h"

/**
 * Read-only tag provider (Blueprint-implementable).
 *
 * The engine's IGameplayTagAssetInterface cannot be implemented in Blueprint, so
 * this interface exists for Blueprint actors. Readers should use
 * UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(), which checks this interface
 * first and falls back to IGameplayTagAssetInterface, so C++/GAS actors need not
 * implement both.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocGameplayTagProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCMODULARCORERUNTIME_API IDocGameplayTagProvider
{
	GENERATED_BODY()

public:
	/** Append (do not replace) this object's current tags into OutTags. Must be side-effect free. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Tags")
	void GetDocOwnedTags(FGameplayTagContainer& OutTags) const;
};

/**
 * Explicit, authorized tag mutation. Gameplay Tags alone do not imply an actor has
 * mutable tag storage; callers that need to change tags require this interface and
 * must receive an actionable Unsupported result when it is missing.
 *
 * Ownership rule: a mutation is attributed to Context (its instigator / owner).
 * Implementations must not let one owner remove tags granted by another owner;
 * a reference-counted or per-owner store is recommended.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocMutableGameplayTagProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCMODULARCORERUNTIME_API IDocMutableGameplayTagProvider
{
	GENERATED_BODY()

public:
	/**
	 * Apply an owner-attributed tag delta. Return PermissionDenied when Context lacks
	 * authority, InvalidConfiguration for malformed input, Succeeded otherwise.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Tags")
	FDocSystemResult ApplyDocTagDelta(const FDocGameplayContext& Context, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove);
};
