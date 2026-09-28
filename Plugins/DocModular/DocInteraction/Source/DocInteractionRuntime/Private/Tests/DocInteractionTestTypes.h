#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DocInteractionComponents.h"
#include "DocInteractionRules.h"
#include "Interfaces/DocGameplayTagProvider.h"
#include "DocInteractionTestTypes.generated.h"

/**
 * Test-only actor: records receiver notifications and stores owner-attributed tags.
 * Compiled in all configurations (UHT cannot see test guards) but only used by tests.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient, HideDropdown)
class ADocInteractionTestActor : public AActor, public IDocInteractionReceiver, public IDocGameplayTagProvider, public IDocMutableGameplayTagProvider
{
	GENERATED_BODY()

public:
	ADocInteractionTestActor();

	TArray<FGameplayTag> Received;
	FGameplayTagContainer Tags;

	virtual void OnDocInteraction_Implementation(const FDocInteractionContext& Context, FGameplayTag EventTag) override { Received.Add(EventTag); }
	virtual void GetDocOwnedTags_Implementation(FGameplayTagContainer& OutTags) const override { OutTags.AppendTags(Tags); }
	virtual FDocSystemResult ApplyDocTagDelta_Implementation(const FDocGameplayContext& Context, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove) override
	{
		Tags.RemoveTags(TagsToRemove);
		Tags.AppendTags(TagsToAdd);
		return FDocSystemResult::MakeSuccess();
	}
};

/** Test-only action that always fails. */
UCLASS(NotBlueprintable, HideDropdown)
class UDocInteractionTestFailAction : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Test failure"));
	}
};
