#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "DocWorldStateProvider.generated.h"

/** Three-valued answer; Unknown is distinct from false (not tracked, not loaded, no coverage). */
UENUM(BlueprintType)
enum class EDocTriState : uint8
{
	Unknown,
	No,
	Yes
};

/** Exact tag match vs. match including descendants of the queried tag. */
UENUM(BlueprintType)
enum class EDocTagMatchMode : uint8
{
	Exact,
	IncludeChildren
};

UINTERFACE(MinimalAPI, BlueprintType)
class UDocWorldStateProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Narrow world-state query contract. This is not a state store: implementers
 * (a project's quest/state system, a bridge, a test double) decide where state
 * lives. Consumers (conditions, sequence replay rules, audio inputs) query it.
 */
class DOCMODULARCORERUNTIME_API IDocWorldStateProvider
{
	GENERATED_BODY()

public:
	/** Is StateTag currently set? Return Unknown when this provider does not track it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|WorldState")
	EDocTriState QueryDocWorldState(FGameplayTag StateTag, EDocTagMatchMode MatchMode) const;
};
