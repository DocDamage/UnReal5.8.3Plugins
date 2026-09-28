#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocMaterialReactionTypes.generated.h"

/** How samples on one channel combine. Applied per channel, then capped. */
UENUM(BlueprintType)
enum class EDocChannelCombinationRule : uint8
{
	Sum UMETA(DisplayName = "Sum"),
	Max UMETA(DisplayName = "Max"),
	/** Weighted mean: sum(Intensity * Weight) / sum(Weight). */
	Weighted UMETA(DisplayName = "Weighted")
};

/** Declares what a channel value means. A channel never mixes both. */
UENUM(BlueprintType)
enum class EDocExposureUnits : uint8
{
	/** Authored gameplay intensity, usually 0..N. Not a physical quantity. */
	NormalizedIntensity UMETA(DisplayName = "Normalized Intensity"),
	/** A physical unit named by the channel spec (for example degrees C). The model still does not conserve heat. */
	Physical UMETA(DisplayName = "Physical Units")
};

UENUM(BlueprintType)
enum class EDocReactionState : uint8
{
	Inactive UMETA(DisplayName = "Inactive"),
	PendingDwell UMETA(DisplayName = "Pending Dwell"),
	Active UMETA(DisplayName = "Active"),
	Depleted UMETA(DisplayName = "Depleted"),
	Extinguished UMETA(DisplayName = "Extinguished"),
	Suppressed UMETA(DisplayName = "Suppressed"),
	/** Eligible, but a higher-priority reaction in the same exclusive group owns the resource this step. */
	Outcompeted UMETA(DisplayName = "Outcompeted"),
	/** Blocked by material facts (for example moisture at or above the inhibit level). */
	Inhibited UMETA(DisplayName = "Inhibited")
};

/** Per-channel declaration on a profile. */
USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocExposureChannelSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	EDocExposureUnits Units = EDocExposureUnits::NormalizedIntensity;

	/** Free-text unit label for Physical channels (for example "C"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	FName UnitLabel = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	EDocChannelCombinationRule Rule = EDocChannelCombinationRule::Sum;

	/** Combined value is clamped to [0, MaxValue] before evaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (ClampMin = "0"))
	float MaxValue = 100.0f;
};

/** A source-owned exposure contribution. Removing one source removes only its contribution. */
USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocExposureSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	FName SourceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float Intensity = 0.0f;

	/** Used only by the Weighted rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float Weight = 1.0f;

	/** Simulation-clock time after which the sample is dropped. 0 = no expiry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	double ExpiryTimestamp = 0.0;

	/** Source-side sequence. A sample with a lower sequence than the stored one for the same source is stale and refused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	int32 Sequence = 0;

	/** Incremented by the component on each accepted update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	int32 Revision = 0;

	/** Propagation depth: 0 for direct sources, N for the Nth hop of spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	int32 Generation = 0;
};

/** Material facts. Independent quantities, so an object can be wet and charred at once. */
USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocMaterialState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FName MaterialId = NAME_None;

	/** [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float Moisture = 0.0f;

	/** [0, MaxFuel]. Never regenerates through extinguish or reignite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float Fuel = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaxFuel = 100.0f;

	/** [0, 1]. 0 = solid, 1 = fully melted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float PhaseFraction = 0.0f;

	/** [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float CharAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FGameplayTagContainer StateTags;

	/** Incremented once per committed change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	int64 Revision = 0;
};

USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocReactionInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	FName ReactionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	FName GroupId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	EDocReactionState State = EDocReactionState::Inactive;

	/** Seconds spent at or above the activation threshold while not yet active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	float DwellProgress = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	float ActiveDuration = 0.0f;

	/** Committed transition that started the current activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	FGuid TransitionId;

	/** Propagation generation that caused this activation (0 = direct). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	int32 Generation = 0;

	/** Why the reaction is in its current state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	FName StatusReason = NAME_None;

	/** Totals over the current activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	float ConsumedFuel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	float ConsumedMoisture = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	float DeltaPhase = 0.0f;

	/** Receipt of the start transition; retryable gameplay effects key on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	FGuid ReceiptId;
};

USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocReactionTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FGuid TransitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FName ReactionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FName Cause = NAME_None;

	/** State the reaction entered with this transition (Active for a start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	EDocReactionState NewState = EDocReactionState::Inactive;

	/** Material revision this transition was committed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	int64 MaterialRevision = 0;

	/** Totals over the activation, filled on end transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float ConsumedFuel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float ConsumedMoisture = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float DeltaPhase = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	double Timestamp = 0.0;

	/** Receipt for retryable gameplay effects keyed to this transition. Stable across save/restore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FGuid ReceiptId;
};

USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocPropagationBudget
{
	GENERATED_BODY()

	/** Transfers one source reaction may issue per step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget")
	int32 MaxNeighborsPerSource = 8;

	/** Transfers committed per step across all sources. Extra transfers are deferred to the next step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget")
	int32 MaxQueuedTransfersPerStep = 32;

	/** Candidate checks (distance or contact) per step. Sources not reached are processed first next step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget")
	int32 MaxWorkPerStep = 64;

	/** Reactions allowed to be Active per object. Further starts are rejected with ActiveReactionLimit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget")
	int32 MaxActiveReactionsPerObject = 8;
};

/** Counters for the last propagation pass. */
USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocPropagationStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 SourcesProcessed = 0;

	/** Sources skipped because the work or transfer budget ran out; they go first next step. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 SourcesDeferred = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 CandidatesEvaluated = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 TransfersIssued = 0;

	/** Repeated contact or request for the same source/target/reaction in one step. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 TransfersDeduplicated = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 TransfersRejectedGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 TransfersSkippedCooldown = 0;

	/** Target refused the sample (invalid, stale, or unloaded). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 TransfersRejectedByTarget = 0;
};

USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocSuppressionRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression")
	FName ReactionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression")
	FName SourceId = NAME_None;

	/** Seconds left; negative = permanent until revoked by its source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression")
	double RemainingSeconds = -1.0;
};

/** Saved facts for one reactive object. Exposure samples are live source handles and are not saved. */
USTRUCT(BlueprintType)
struct DOCMATERIALREACTIONSRUNTIME_API FDocMaterialSnapshot
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snapshot")
	int32 SchemaVersion = CurrentSchemaVersion;

	/** Profile ContentVersion at capture time. Restore refuses a mismatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snapshot")
	int32 ContentVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snapshot")
	FDocMaterialState State;

	/** Non-inactive reaction instances, with progress and the TransitionId/receipt of the current activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snapshot")
	TArray<FDocReactionInstance> Reactions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snapshot")
	TArray<FDocSuppressionRecord> Suppressions;
};
