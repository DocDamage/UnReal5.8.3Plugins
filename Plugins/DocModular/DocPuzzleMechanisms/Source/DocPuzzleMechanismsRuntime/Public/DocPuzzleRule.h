#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DocPuzzleTypes.h"
#include "DocPuzzleRule.generated.h"

class UDocPuzzleRule;

/** Transient mutable state and context passed during rule evaluation and input submission. */
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleRuleEvaluationContext
{
	double CurrentTime = 0.0;
	const TMap<FName, FDocPuzzleInputValue>* CurrentInputStates = nullptr;
	const TArray<FDocPuzzleInputContributor>* ActiveContributors = nullptr;
	TMap<FName, int32>* RuleStateInts = nullptr;
	TMap<FName, double>* RuleStateDoubles = nullptr;
	bool bAttemptFailed = false;

	/** Recursion guard. Definitions are validated as acyclic at registration; this bounds evaluation regardless. */
	mutable int32 EvaluationDepth = 0;
	static constexpr int32 MaxEvaluationDepth = 32;
};

/** Base class for authored, immutable puzzle evaluation rules. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName RuleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FText LocalizedUnmetReason;

	virtual void ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const;

	virtual void OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const;

	/** Commit-time update of timers and level-derived state at Context.CurrentTime (dwell start, window expiry). */
	virtual void UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const;

	/** Read-only evaluation. Must not write rule state. */
	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const;

	/** Validates this node only. UDocPuzzleDefinition::ValidateDefinition walks the graph (cycles, depth, size). */
	virtual void ValidateRule(TArray<FText>& OutErrors) const;

	virtual void GetChildRules(TArray<const UDocPuzzleRule*>& OutChildren) const {}

	virtual void GetReferencedInputIds(TArray<FName>& OutInputIds) const {}
};

/** A single expected step in an ordered sequence rule. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleOrderedStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FDocPuzzleInputValue ExpectedValue;
};

/** Rule requiring inputs to arrive in a strictly authored sequence. */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Ordered Inputs Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_OrderedInputs : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocPuzzleOrderedStep> ExpectedSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleOrderedMismatchPolicy MismatchPolicy = EDocPuzzleOrderedMismatchPolicy::ResetProgress;

	virtual void ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetReferencedInputIds(TArray<FName>& OutInputIds) const override;
};

/** Authoring descriptor for a condition required in a simultaneous rule. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleSimultaneousRequirement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FDocPuzzleInputValue RequiredValue;
};

/** Rule requiring multiple level inputs to be satisfied concurrently for a continuous dwell duration. */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Simultaneous Dwell Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_Simultaneous : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocPuzzleSimultaneousRequirement> RequiredInputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle", meta = (ClampMin = "0.0"))
	float MinDwellSeconds = 0.0f;

	virtual void ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetReferencedInputIds(TArray<FName>& OutInputIds) const override;

private:
	bool AreConditionsMet(const FDocPuzzleRuleEvaluationContext& Context, int32& OutMetCount) const;
};

/** Descriptor for input weighting. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleWeightedInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float WeightMultiplier = 1.0f;
};

/** Rule requiring the sum of active scalar inputs to meet or exceed a threshold. */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Weighted Threshold Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_WeightedThreshold : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocPuzzleWeightedInput> ContributingInputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float RequiredThreshold = 1.0f;

	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetReferencedInputIds(TArray<FName>& OutInputIds) const override;
};

/** Rule requiring symbol inputs to match a static combination. */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Symbol Combination Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_SymbolCombination : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TMap<FName, FName> RequiredSymbols;

	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetReferencedInputIds(TArray<FName>& OutInputIds) const override;
};

/** Rule requiring a child rule to be completed within a half-open time window [start, start + WindowDuration). */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Timed Sequence Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_TimedSequence : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TObjectPtr<UDocPuzzleRule> ChildRule = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle", meta = (ClampMin = "0.01"))
	float WindowDurationSeconds = 5.0f;

	virtual void ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetChildRules(TArray<const UDocPuzzleRule*>& OutChildren) const override;
};

/** Composite rule evaluating multiple children via All (AND) or Any (OR / alternatives). */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Composite Rule"))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleRule_Composite : public UDocPuzzleRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleCompositionMode Mode = EDocPuzzleCompositionMode::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<TObjectPtr<UDocPuzzleRule>> ChildRules;

	virtual void ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual void UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const override;
	virtual bool Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const override;
	virtual void ValidateRule(TArray<FText>& OutErrors) const override;
	virtual void GetChildRules(TArray<const UDocPuzzleRule*>& OutChildren) const override;
};
