#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocMaterialReactionTypes.h"
#include "DocMaterialReactionDefinition.h"
#include "DocMaterialProfile.generated.h"

UCLASS(BlueprintType)
class DOCMATERIALREACTIONSRUNTIME_API UDocMaterialProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName MaterialId = NAME_None;

	/** Bump when authored rates or reactions change incompatibly. Saved snapshots with another version are refused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 ContentVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer BaseMaterialTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial State")
	float InitialMoisture = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial State")
	float InitialFuel = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial State")
	float MaxFuel = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial State")
	float InitialPhaseFraction = 0.0f;

	/** Rule for channels without an explicit spec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation")
	EDocChannelCombinationRule ChannelRule = EDocChannelCombinationRule::Sum;

	/** Cap for channels without an explicit spec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0"))
	float DefaultChannelMaxValue = 100.0f;

	/** Optional per-channel units, rule and cap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation")
	TArray<FDocExposureChannelSpec> ChannelSpecs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions")
	TArray<TObjectPtr<UDocMaterialReactionDefinition>> AllowedReactions;

	UFUNCTION(BlueprintCallable, Category = "Validation")
	bool ValidateProfile(FString& OutError) const;

	/** Rule and cap for a channel (spec if declared, else profile defaults). */
	void ResolveChannel(const FGameplayTag& Channel, EDocChannelCombinationRule& OutRule, float& OutMaxValue) const;

	const UDocMaterialReactionDefinition* FindReaction(FName ReactionId) const;

	/** Allowed reactions sorted: wetting-class first, then Priority descending, then ReactionId. */
	TArray<const UDocMaterialReactionDefinition*> GetEvaluationOrder() const;
};
