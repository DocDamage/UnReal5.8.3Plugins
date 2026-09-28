#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocPuzzleTypes.h"
#include "DocPuzzleRule.h"
#include "DocPuzzleDefinition.generated.h"

/** Primary data asset authoring a puzzle's expected inputs, rules, and policies. */
UCLASS(BlueprintType)
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	FName DefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	TArray<FDocPuzzleInputDefinition> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Doc|Puzzle")
	TObjectPtr<UDocPuzzleRule> RootRule = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	bool bAllowReset = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle")
	bool bResetOnFailure = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Puzzle", meta = (ClampMin = "0.0"))
	float TimeoutSeconds = 0.0f;

	static constexpr int32 MaxGraphDepth = 16;
	static constexpr int32 MaxGraphNodes = 256;

	/**
	 * Validates ids, inputs and the rule graph: no cycles, depth <= MaxGraphDepth, at most MaxGraphNodes rules,
	 * unique RuleIds (rule state is keyed by RuleId), and, when Inputs are declared, every referenced input declared.
	 * RegisterPuzzle refuses definitions that fail.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	bool ValidateDefinition(TArray<FText>& OutErrors) const;

	const FDocPuzzleInputDefinition* FindInput(FName InputId) const
	{
		return Inputs.FindByPredicate([InputId](const FDocPuzzleInputDefinition& D) { return D.InputId == InputId; });
	}
};
