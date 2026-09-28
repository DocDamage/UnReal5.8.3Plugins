#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocOwnerScope.h"
#include "DocPuzzleTypes.h"
#include "DocPuzzleDefinition.h"
#include "DocPuzzleSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocPuzzleInputAcceptedSignature, const FGuid&, InstanceId, const FDocPuzzleInputEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocPuzzleInputRejectedSignature, const FGuid&, InstanceId, const FDocPuzzleInputEvent&, Event, const FDocSystemResult&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocPuzzleProgressChangedSignature, const FGuid&, InstanceId, const FDocPuzzleEvaluation&, Evaluation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocPuzzleAttemptFailedSignature, const FGuid&, InstanceId, const FGuid&, AttemptId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocPuzzleSolvedSignature, const FGuid&, InstanceId, const FGuid&, AttemptId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocPuzzleResetCommittedSignature, const FGuid&, InstanceId, int32, NewResetEpoch);

DECLARE_MULTICAST_DELEGATE_TwoParams(FDocPuzzleInputAcceptedNative, const FGuid&, const FDocPuzzleInputEvent&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocPuzzleInputRejectedNative, const FGuid&, const FDocPuzzleInputEvent&, const FDocSystemResult&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocPuzzleProgressChangedNative, const FGuid&, const FDocPuzzleEvaluation&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocPuzzleAttemptFailedNative, const FGuid&, const FGuid&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocPuzzleSolvedNative, const FGuid&, const FGuid&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocPuzzleResetCommittedNative, const FGuid&, int32);

/** Internal runtime tracking record for an active puzzle instance. */
struct FDocPuzzleRuntimeRecord
{
	FGuid InstanceId;
	FDocOwnerScope Scope;
	TObjectPtr<const UDocPuzzleDefinition> Definition = nullptr;
	EDocPuzzleState State = EDocPuzzleState::Inactive;
	FGuid ActiveAttemptId;
	int32 ResetEpoch = 0;
	int32 StateRevision = 0;
	double AttemptStartTime = 0.0;
	TMap<FGuid, int64> LastAcceptedSourceSequence;
	TMap<FName, FDocPuzzleInputValue> CurrentInputStates;
	TArray<FDocPuzzleInputContributor> ActiveContributors;
	TMap<FName, int32> RuleStateInts;
	TMap<FName, double> RuleStateDoubles;
	TMap<FName, double> LastInputTimes;
	TArray<FDocEffectKey> EffectReceipts;
	FName WinningRuleId = NAME_None;
	TArray<FDocPuzzleRuleProgress> CachedProgress;
	TArray<FName> UnsatisfiedRuleIds;
};

/**
 * World subsystem orchestrating authoritative puzzle state machines, input ordering, and evaluation.
 * Ticks to complete dwells and expire timed windows; ProcessTimers does the same on demand.
 */
UCLASS()
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/**
	 * Register (or re-bind) a puzzle instance. The definition must pass ValidateDefinition.
	 * Re-registering an unregistered instance with the same definition restores its retained state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult RegisterPuzzle(const FGuid& InstanceId, const UDocPuzzleDefinition* Definition, const FDocOwnerScope& Scope);

	/** Unbind the live participant. Its state is retained (live contributors are dropped) until re-registered or discarded. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult UnregisterPuzzle(const FGuid& InstanceId);

	/** Explicitly delete retained state of an unregistered instance. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult DiscardRetainedState(const FGuid& InstanceId);

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	bool HasRetainedState(const FGuid& InstanceId) const;

	/** Commits time-driven changes for every active attempt: dwell completion, window expiry, attempt timeout. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	void ProcessTimers();

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	FGuid GetActiveAttemptId(const FGuid& InstanceId) const;

	/** Replaces the authority clock (world time by default). Intended for tests and replays. */
	void SetClockOverride(TFunction<double()> InClock) { ClockOverride = MoveTemp(InClock); }

	/** Start a fresh attempt for a registered puzzle. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult StartAttempt(const FGuid& InstanceId, FGuid& OutAttemptId);

	/**
	 * Submit an input event. The event must carry the active AttemptId. Acceptance time is the subsystem's
	 * authority clock; a caller-supplied AcceptedTimestamp is ignored. Declared inputs are checked for kind,
	 * scalar range and debounce.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitInput(const FDocPuzzleInputEvent& Event);

	/** Register or update a continuous contributor level input (e.g. pressure plate or held switch). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SetInputContributor(const FGuid& InstanceId, const FGuid& SourceId, FName InputId, const FDocPuzzleInputValue& Value);

	/** Remove a continuous contributor input. Retains other contributors on the same input. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult RemoveInputContributor(const FGuid& InstanceId, const FGuid& SourceId, FName InputId);

	/** Request an authoritative reset of the active attempt. Advances reset epoch. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult RequestReset(const FGuid& InstanceId);

	/** Side-effect-free view of the current state at the current time. Never changes state or publishes events. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult EvaluatePuzzle(const FGuid& InstanceId, FDocPuzzleEvaluation& OutEvaluation);

	/** Capture a detached versioned snapshot of the puzzle instance. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult CapturePuzzle(const FGuid& InstanceId, FDocPuzzleSnapshot& OutSnapshot) const;

	/** Stage and restore a snapshot without replaying historical side effects or rewards. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult StageRestore(const FDocPuzzleSnapshot& Snapshot);

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	bool IsPuzzleRegistered(const FGuid& InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	EDocPuzzleState GetPuzzleState(const FGuid& InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	bool IsPuzzleSolved(const FGuid& InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	int32 GetResetEpoch(const FGuid& InstanceId) const;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleInputAcceptedSignature OnInputAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleInputRejectedSignature OnInputRejected;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleProgressChangedSignature OnProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleAttemptFailedSignature OnAttemptFailed;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleSolvedSignature OnPuzzleSolved;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleResetCommittedSignature OnResetCommitted;

	FDocPuzzleInputAcceptedNative OnInputAcceptedNative;
	FDocPuzzleInputRejectedNative OnInputRejectedNative;
	FDocPuzzleProgressChangedNative OnProgressChangedNative;
	FDocPuzzleAttemptFailedNative OnAttemptFailedNative;
	FDocPuzzleSolvedNative OnPuzzleSolvedNative;
	FDocPuzzleResetCommittedNative OnResetCommittedNative;

private:
	/** Commit point: updates timers/level state, then Solved or Failed transitions with their events. */
	void CommitInstance(const FGuid& InstanceId, bool bPublishProgress);
	void BuildEvaluation(const FDocPuzzleRuntimeRecord& Record, double Now, FDocPuzzleEvaluation& OutEvaluation, bool& bOutSatisfied, FName& OutWinningRuleId) const;
	void FailAttempt(const FGuid& InstanceId);
	FDocSystemResult RejectInput(const FDocPuzzleInputEvent& Event, const FDocSystemResult& Result);
	FDocSystemResult ValidateDeclaredInput(const FDocPuzzleRuntimeRecord& Record, FName InputId, const FDocPuzzleInputValue& Value) const;

	double GetCurrentTimeSeconds() const;

	TMap<FGuid, FDocPuzzleRuntimeRecord> RegisteredPuzzles;
	TMap<FGuid, FDocPuzzleRuntimeRecord> RetainedPuzzles;
	TFunction<double()> ClockOverride;
};
