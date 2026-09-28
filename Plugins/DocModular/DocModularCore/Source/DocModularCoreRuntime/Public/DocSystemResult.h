#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.generated.h"

/**
 * Closed set of outcomes. Extensible detail goes in FDocSystemResult::ErrorTag.
 * Unset is the default and is never a success: an unassigned result must not look
 * like success (handoff 3.7).
 *
 * Append-only: new values go at the end so existing numeric values never change.
 * Success outcomes are Succeeded and NoChange (see IsSuccess / IsChanged).
 */
UENUM(BlueprintType)
enum class EDocResultOutcome : uint8
{
	Unset					UMETA(DisplayName = "Unset (failure)"),
	Succeeded,
	Unsupported,
	NotReady,
	NotFound,
	InvalidConfiguration,
	PermissionDenied,
	Cancelled,
	TimedOut,
	Failed					UMETA(DisplayName = "Failed (execution)"),
	/** Request was valid and already satisfied; nothing was mutated. Counts as success. */
	NoChange				UMETA(DisplayName = "No Change (success)"),
	/** Request values are invalid (bad quantity, malformed ID...). Authored-data errors use InvalidConfiguration. */
	InvalidInput,
	/** An optional provider/dependency is absent or down at runtime. Unsupported = capability not installed/implemented. */
	Unavailable,
	/** Stale revision, concurrent modification, or conflicting reservation. */
	Conflict
};

/**
 * Typed result returned by DocModular operations.
 *
 * - Outcome: closed category for control flow.
 * - ErrorTag: extensible detail (Doc.Error.* or a feature child tag). Empty on success.
 * - UserMessage: optional localized text safe to show players.
 * - Diagnostic: technical text for logs/debuggers. Never shown to players by default.
 * - OperationId: correlation ID of the request this result belongs to (0 = none).
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocSystemResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Result")
	EDocResultOutcome Outcome = EDocResultOutcome::Unset;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Result")
	FGameplayTag ErrorTag;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Result")
	FText UserMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Result")
	FString Diagnostic;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Result")
	int64 OperationId = 0;

	/** True for Succeeded and NoChange. */
	bool IsSuccess() const { return Outcome == EDocResultOutcome::Succeeded || Outcome == EDocResultOutcome::NoChange; }
	bool IsFailure() const { return !IsSuccess(); }

	/** True only when the operation succeeded and actually changed state (Succeeded, not NoChange). */
	bool IsChanged() const { return Outcome == EDocResultOutcome::Succeeded; }

	/** True for outcomes that are successes (Succeeded, NoChange). Unset is not. */
	static bool IsSuccessOutcome(EDocResultOutcome InOutcome)
	{
		return InOutcome == EDocResultOutcome::Succeeded || InOutcome == EDocResultOutcome::NoChange;
	}

	static FDocSystemResult MakeSuccess(int64 InOperationId = 0);

	/** Valid request that was already satisfied. Optional diagnostic explains why nothing changed. */
	static FDocSystemResult MakeNoChange(FString InDiagnostic = FString(), int64 InOperationId = 0);

	/**
	 * Build a failure. If InErrorTag is empty, the default Doc.Error.* tag for the
	 * outcome is used. Passing Succeeded, NoChange, or Unset as a failure outcome
	 * is a programming error and is converted to Failed with an ensure.
	 */
	static FDocSystemResult MakeFailure(
		EDocResultOutcome InOutcome,
		FString InDiagnostic,
		FGameplayTag InErrorTag = FGameplayTag(),
		FText InUserMessage = FText::GetEmpty(),
		int64 InOperationId = 0);

	/** The Core default error tag for an outcome (empty for Succeeded and NoChange). */
	static FGameplayTag GetDefaultErrorTag(EDocResultOutcome InOutcome);

	/** Compact technical description for logs. Does not include UserMessage. */
	FString ToString() const;
};
