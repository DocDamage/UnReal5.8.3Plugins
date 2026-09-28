#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocEvidenceTypes.h"
#include "DocEvidenceDefinition.generated.h"

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocEvidenceSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	TArray<FName> RequiredEvidenceIds;
};

UCLASS(BlueprintType)
class DOCEVIDENCEDEDUCTIONRUNTIME_API UDocEvidenceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName EvidenceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bIsSecret = false;
};

UCLASS(BlueprintType)
class DOCEVIDENCEDEDUCTIONRUNTIME_API UDocHypothesisDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName HypothesisId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FText HypothesisStatement;

	/** If multiple sets are authored, satisfying ANY set satisfies the hypothesis (alternative sets). Within each set, all items are required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FDocEvidenceSet> AlternativeRequiredEvidenceSets;

	/** Any discovered established fact in this list directly contradicts the hypothesis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> DisqualifyingEvidenceIds;

	/** If authored, enforces that at most one conclusion in this group can be committed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName MutuallyExclusiveGroupId = NAME_None;

	/** Hidden hypotheses/solutions cannot be leaked in ordinary explanation queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bIsHiddenSolution = false;

	/** Inference chain: every listed hypothesis must itself be Supported. Contradicted there contradicts here. Acyclic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> DependentHypothesisIds;

	/** A typed contradiction involving any supporting observation contradicts the hypothesis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bContradictionsDisqualify = true;

	/** Authored gameplay score reported when Supported (not a probability). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evidence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SupportedScore = 1.0f;
};
