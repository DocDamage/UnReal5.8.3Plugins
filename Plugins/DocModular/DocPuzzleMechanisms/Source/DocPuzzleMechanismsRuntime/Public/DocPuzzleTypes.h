#pragma once

#include "CoreMinimal.h"
#include "DocOwnerScope.h"
#include "DocSystemResult.h"
#include "DocEffectKey.h"
#include "GameplayTagContainer.h"
#include "DocPuzzleTypes.generated.h"

/** Operational lifecycle state of a puzzle instance. */
UENUM(BlueprintType)
enum class EDocPuzzleState : uint8
{
	Inactive,
	Ready,
	AttemptActive,
	Solved,
	Failed,
	Resetting
};

/** Categorization of accepted input value types. */
UENUM(BlueprintType)
enum class EDocPuzzleInputKind : uint8
{
	Trigger,
	Boolean,
	Scalar,
	DiscreteSymbol
};

/** Policy when an input violates the expected sequence in an ordered rule. */
UENUM(BlueprintType)
enum class EDocPuzzleOrderedMismatchPolicy : uint8
{
	ResetProgress,
	FailAttempt,
	IgnoreUnexpected
};

/** Composition operation for combining multiple rules. */
UENUM(BlueprintType)
enum class EDocPuzzleCompositionMode : uint8
{
	All,
	Any
};

/** Closed variant representation of an authored or submitted input value. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleInputValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleInputKind Kind = EDocPuzzleInputKind::Trigger;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	bool bBoolValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float ScalarValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName SymbolValue = NAME_None;

	FDocPuzzleInputValue() = default;

	static FDocPuzzleInputValue MakeTrigger()
	{
		FDocPuzzleInputValue V;
		V.Kind = EDocPuzzleInputKind::Trigger;
		return V;
	}

	static FDocPuzzleInputValue MakeBoolean(bool bInVal)
	{
		FDocPuzzleInputValue V;
		V.Kind = EDocPuzzleInputKind::Boolean;
		V.bBoolValue = bInVal;
		return V;
	}

	static FDocPuzzleInputValue MakeScalar(float InVal)
	{
		FDocPuzzleInputValue V;
		V.Kind = EDocPuzzleInputKind::Scalar;
		V.ScalarValue = InVal;
		return V;
	}

	static FDocPuzzleInputValue MakeSymbol(FName InSymbol)
	{
		FDocPuzzleInputValue V;
		V.Kind = EDocPuzzleInputKind::DiscreteSymbol;
		V.SymbolValue = InSymbol;
		return V;
	}

	bool Matches(const FDocPuzzleInputValue& Other, float ScalarTolerance = KINDA_SMALL_NUMBER) const
	{
		if (Kind != Other.Kind)
		{
			return false;
		}
		switch (Kind)
		{
		case EDocPuzzleInputKind::Trigger:
			return true;
		case EDocPuzzleInputKind::Boolean:
			return bBoolValue == Other.bBoolValue;
		case EDocPuzzleInputKind::Scalar:
			return FMath::IsNearlyEqual(ScalarValue, Other.ScalarValue, ScalarTolerance);
		case EDocPuzzleInputKind::DiscreteSymbol:
			return SymbolValue == Other.SymbolValue;
		default:
			return false;
		}
	}

	bool operator==(const FDocPuzzleInputValue& Other) const
	{
		return Matches(Other);
	}

	bool operator!=(const FDocPuzzleInputValue& Other) const
	{
		return !(*this == Other);
	}
};

/** Authoring descriptor for an expected input on a puzzle. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleInputDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleInputKind AcceptedKind = EDocPuzzleInputKind::Trigger;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGameplayTagContainer SemanticTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float MinScalar = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float MaxScalar = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float DebounceSeconds = 0.0f;
};

/** Input event delivered to a puzzle instance. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	int64 SourceSequenceNumber = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid AttemptId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	double AcceptedTimestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FDocPuzzleInputValue Value;
};

/** Contributor-owned level state for simultaneous/weighted inputs. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleInputContributor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FDocPuzzleInputValue Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	double Timestamp = 0.0;
};

/** Progress metrics for an individual puzzle rule. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleRuleProgress
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName RuleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	bool bSatisfied = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	float NormalizedProgress = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FText UnmetReason;

	/** For Any-composites: the alternative that completed (first satisfied child in authored order). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName WinningChildRuleId = NAME_None;

	/** Descendant rules that are not yet satisfied, for explanations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FName> UnsatisfiedChildRuleIds;
};

/** Coherent side-effect-free evaluation snapshot of a puzzle instance. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleEvaluation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleState State = EDocPuzzleState::Inactive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid ActiveAttemptId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	int32 StateRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocPuzzleRuleProgress> RuleProgress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FName> UnsatisfiedRuleIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName WinningRuleId = NAME_None;

	bool IsSolved() const { return State == EDocPuzzleState::Solved; }
	bool IsFailed() const { return State == EDocPuzzleState::Failed; }
};

/** Versioned detached snapshot for save/restore. */
USTRUCT(BlueprintType)
struct DOCPUZZLEMECHANISMSRUNTIME_API FDocPuzzleSnapshot
{
	GENERATED_BODY()

	/** 2 = rule state, remaining timers and submitted input state are captured. 1 = legacy (progress counters only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	int32 SchemaVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName DefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid AttemptId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	int32 ResetEpoch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	EDocPuzzleState State = EDocPuzzleState::Inactive;

	/** Legacy (schema 1) progress counters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TMap<FName, float> RuleProgressValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TMap<FName, int32> RuleStateInts;

	/** Rule timers as time already elapsed at capture; restore re-anchors them to the restoring clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TMap<FName, double> RuleTimerElapsedSeconds;

	/** Level inputs set through SubmitInput. Contributor-owned levels are not captured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TMap<FName, FDocPuzzleInputValue> InputStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	double AttemptElapsedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName WinningRuleId = NAME_None;

	/** Not captured or restored: live contributors re-register themselves after load. Kept for schema compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocPuzzleInputContributor> DurableContributors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TArray<FDocEffectKey> EffectReceipts;
};
