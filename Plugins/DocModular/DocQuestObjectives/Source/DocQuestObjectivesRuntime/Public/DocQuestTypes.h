#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DocEffectKey.h"
#include "DocOwnerScope.h"
#include "DocSharedTypes.h"
#include "DocSystemResult.h"
#include "DocQuestTypes.generated.h"

/**
 * DocQuestObjectives data model (Modules 11-20 handoff, Section 7; Module 15).
 *
 * Name mapping to the handoff's semantic contracts:
 *   UObjectiveSubsystem    -> UDocObjectiveSubsystem (GameInstance)
 *   QuestDefinition        -> UDocQuestDefinition
 *   ObjectiveDefinition    -> FDocObjectiveDefinition (standalone: UDocObjectiveAsset)
 *   ObjectiveCondition     -> FDocQuestCondition
 *   ObjectiveReward        -> FDocQuestAction (delivered through FDocQuestRewardIntent)
 *   QuestRuntimeState      -> FDocQuestRuntimeState
 *   ObjectiveRuntimeState  -> FDocObjectiveRuntimeState
 */
namespace DocQuestTags
{
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_UnknownDefinition);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_PrerequisitesUnmet);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_AlreadyActive);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_NotRepeatable);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_Terminal);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_OwnerPolicy);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_UntrustedSource);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_WrongWorld);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_Quarantined);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_RewardPending);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_ProviderMissing);
	DOCQUESTOBJECTIVESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Quest_NotTrackable);
}

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/** Quest lifecycle. Completed/Failed/Cancelled are terminal and never reset (a repeat is a new instance). */
UENUM(BlueprintType)
enum class EDocQuestState : uint8
{
	Inactive,
	Active,
	Completed,
	Failed,
	Cancelled
};

/** Objective lifecycle. Visibility (bHidden) is a separate dimension. */
UENUM(BlueprintType)
enum class EDocObjectiveState : uint8
{
	Inactive,
	Active,
	Completed,
	Failed,
	Cancelled
};

/** Base evaluator concepts. Each takes provider/observation input; none depends on a sibling feature. */
UENUM(BlueprintType)
enum class EDocObjectiveEvaluator : uint8
{
	EventCount,
	ReachRegion,
	Interact,
	Collect,
	Discover,
	/** Completes after WaitSeconds on the quest's timer clock. */
	Wait,
	Activate,
	/** Completes when a condition provider reports Satisfied (evaluated on RefreshCurrentState). */
	CustomCondition
};

UENUM(BlueprintType)
enum class EDocObjectiveProgressMode : uint8
{
	/** Counts matching observations while active ("collect three, ever"). */
	Cumulative,
	/** Queries a state provider ("currently own three"). Restores never create events. */
	CurrentState
};

UENUM(BlueprintType)
enum class EDocObjectiveTrackingMode : uint8
{
	NotTrackable,
	Manual,
	/** Included in tracked objectives whenever its quest is tracked and it is active. */
	AutoWhenActive
};

UENUM(BlueprintType)
enum class EDocStageActivation : uint8
{
	/** Activate every eligible objective together. */
	Parallel,
	/** Activate the next eligible objective (authored order) when none is active. */
	Ordered
};

UENUM(BlueprintType)
enum class EDocStageCompletion : uint8
{
	AllRequired,
	AnyRequired
};

/** Original single-value stage modes, kept through documented mappings (ApplyLegacyStageMode). */
UENUM(BlueprintType)
enum class EDocQuestLegacyStageMode : uint8
{
	AllRequired,
	AnyRequired,
	Ordered,
	Parallel
};

UENUM(BlueprintType)
enum class EDocQuestFailureAction : uint8
{
	/** The stage fails: branch to Stage.FailureBranchStage, or the quest fails. */
	FailStage,
	FailQuest,
	/** Recorded only; the quest stays active. */
	RemainActive,
	BranchToStage
};

UENUM(BlueprintType)
enum class EDocQuestFailurePrecedence : uint8
{
	FailureWins,
	CompletionWins
};

UENUM(BlueprintType)
enum class EDocOptionalObjectiveClose : uint8
{
	/** Active optional objectives are Cancelled when their stage ends. */
	CancelOnStageEnd,
	/** Active optional objectives are Failed when their stage ends (failure actions do not run). */
	FailOnStageEnd
};

UENUM(BlueprintType)
enum class EDocQuestOwnerPolicy : uint8
{
	/** Owner scope must be a PlayerProfile. */
	PerPlayer,
	/** Owner scope must be SharedWorld. */
	SharedWorld,
	/** Owner scope must be a Party. */
	Party
};

UENUM(BlueprintType)
enum class EDocQuestEvaluationMode : uint8
{
	/** An event counts only against objectives active when its dispatch started. */
	ActiveSnapshot,
	/** Newly activated objectives may also consume the event, up to MaxCascadeDepth. */
	BoundedCascade
};

UENUM(BlueprintType)
enum class EDocQuestReplayPolicy : uint8
{
	/** Historical observations (bHistorical) are ignored. */
	ActiveOnly,
	/** Host-supplied historical observations count while the objective is active. */
	AcceptHistorical
};

UENUM(BlueprintType)
enum class EDocQuestRewardPolicy : uint8
{
	DeliverOnCompletion,
	/** Rewards stay pending until RetryRewardDelivery (claim flow). */
	ManualClaim
};

UENUM(BlueprintType)
enum class EDocRewardIntentState : uint8
{
	/** Not delivered yet (never attempted, or failed retryably). */
	Pending,
	Delivered,
	/** Permanently refused by the consumer; visible, never silently dropped. */
	Failed
};

UENUM(BlueprintType)
enum class EDocQuestConditionType : uint8
{
	QuestCompleted,
	QuestActive,
	QuestNotStarted,
	/** Registered condition provider by ProviderId (bridges: inventory, codex, unlocks, regions, time...). */
	Provider
};

UENUM(BlueprintType)
enum class EDocQuestActionType : uint8
{
	/** Through the effect provider Settings.EventProviderId unless ProviderId is set. */
	BroadcastEvent,
	/** Through the effect provider Settings.TagProviderId unless ProviderId is set. */
	GrantTag,
	Custom
};

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	EDocQuestConditionType Type = EDocQuestConditionType::QuestCompleted;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName QuestId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName ProviderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName QueryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTagContainer Tags;

	/** Inverts Satisfied/Unsatisfied; Unavailable stays Unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bNegate = false;
};

/** Reward/completion/failure action. Delivered through an effect provider with an idempotency key. */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestAction
{
	GENERATED_BODY()

	/** Stable; part of the effect key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName ActionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	EDocQuestActionType Type = EDocQuestActionType::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName ProviderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTag Tag;

	/** Opaque consumer data (item id, amount...). Never a class path to load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FString Payload;
};

/** Observation filter. Tags match hierarchically (MatchesTag) unless the exact flag is set. */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocObjectiveFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bExactEventTag = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTag TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bExactTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTag SenderTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bExactSender = false;

	/** Observation payload tags must contain all of these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTagContainer RequiredPayloadTags;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocObjectiveDefinition
{
	GENERATED_BODY()

	/** Stable within the quest (survives reorder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName ObjectiveId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bOptional = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	EDocObjectiveTrackingMode TrackingMode = EDocObjectiveTrackingMode::AutoWhenActive;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	EDocObjectiveEvaluator Evaluator = EDocObjectiveEvaluator::EventCount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	EDocObjectiveProgressMode ProgressMode = EDocObjectiveProgressMode::Cumulative;

	/** Positive. Counters clamp at this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "1"))
	int32 TargetCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FDocObjectiveFilter Filter;

	/** Eligibility for activation. Unavailable keeps the objective inactive (never treated as satisfied). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	TArray<FDocQuestCondition> Conditions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	TArray<FDocQuestAction> CompletionActions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	TArray<FDocQuestAction> FailureActions;

	/** Feature-neutral key a map/marker bridge may resolve. Not a MapNavigation type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName MapMarkerKey;

	/** Wait evaluator duration (quest TimerClock). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0"))
	float WaitSeconds = 0.f;

	/** 0 = no limit. On expiry the objective fails. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0"))
	float TimeLimitSeconds = 0.f;

	/** CurrentState / CustomCondition provider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName ProviderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName StateQueryId;

	/** CurrentState only: progress may go down before completion. Completion is never undone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bAllowRegression = true;

	/** Opt-in: ApplyOfflineElapsed may advance this objective's timers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bAllowOfflineProgress = false;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestFailureRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName RuleId;

	/** Observation that triggers the rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FDocObjectiveFilter Filter;

	/** Only while this stage is current (None = any stage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName StageId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	EDocQuestFailureAction Action = EDocQuestFailureAction::FailQuest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName BranchStageId;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestStage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	FName StageId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	EDocStageActivation Activation = EDocStageActivation::Parallel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	EDocStageCompletion Completion = EDocStageCompletion::AllRequired;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	TArray<FDocObjectiveDefinition> Objectives;

	/** Explicit empty stage that completes immediately. Without it, an empty required set is a validation error. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	bool bPassThrough = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	EDocQuestFailureAction OnRequiredObjectiveFailed = EDocQuestFailureAction::FailQuest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	FName FailureBranchStage;

	/** None = the next stage in array order; the last stage completes the quest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	FName NextStage;

	/** Completing this stage completes the quest regardless of later stages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	bool bCompletesQuest = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	EDocOptionalObjectiveClose OptionalClose = EDocOptionalObjectiveClose::CancelOnStageEnd;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stage")
	TArray<FDocQuestAction> CompletionActions;

	const FDocObjectiveDefinition* FindObjective(FName ObjectiveId) const
	{
		return Objectives.FindByPredicate([ObjectiveId](const FDocObjectiveDefinition& O) { return O.ObjectiveId == ObjectiveId; });
	}
};

/** QuestDefinition. Immutable at runtime. */
UCLASS(BlueprintType)
class DOCQUESTOBJECTIVESRUNTIME_API UDocQuestDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName QuestId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	int32 ContentVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FGameplayTag Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestStage> Stages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestCondition> Prerequisites;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestAction> Rewards;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestFailureRule> FailureRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bRepeatable = false;

	/** 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest", meta = (ClampMin = "0"))
	int32 MaxRepeats = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bTrackable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bAutoActivate = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bCancellable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocQuestOwnerPolicy OwnerPolicy = EDocQuestOwnerPolicy::PerPlayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocQuestEvaluationMode EvaluationMode = EDocQuestEvaluationMode::ActiveSnapshot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxCascadeDepth = 1;

	/** Higher first when one observation affects several quests. Ties break by QuestId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	int32 TransitionPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocQuestReplayPolicy ReplayPolicy = EDocQuestReplayPolicy::ActiveOnly;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocClockDomain TimerClock = EDocClockDomain::WorldGameplay;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocQuestRewardPolicy RewardPolicy = EDocQuestRewardPolicy::DeliverOnCompletion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocQuestFailurePrecedence FailurePrecedence = EDocQuestFailurePrecedence::FailureWins;

	/** Save migration: removed/renamed StageId -> replacement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Migration")
	TMap<FName, FName> StageRedirects;

	/** Save migration: removed/renamed ObjectiveId -> replacement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Migration")
	TMap<FName, FName> ObjectiveRedirects;

	const FDocQuestStage* FindStage(FName StageId) const;
	int32 FindStageIndex(FName StageId) const;
	void FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;

	/**
	 * Documented mapping of the original modes:
	 *   AllRequired -> Parallel activation + AllRequired completion
	 *   AnyRequired -> Parallel activation + AnyRequired completion
	 *   Ordered     -> Ordered activation + AllRequired completion
	 *   Parallel    -> Parallel activation; completion must be chosen explicitly (returns false)
	 */
	static bool ApplyLegacyStageMode(FDocQuestStage& Stage, EDocQuestLegacyStageMode Mode);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocQuestDefinition"), GetFName()); }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Standalone objective with its own instance/owner keys. */
UCLASS(BlueprintType)
class DOCQUESTOBJECTIVESRUNTIME_API UDocObjectiveAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FDocObjectiveDefinition Objective;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	int32 ContentVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	TArray<FDocQuestAction> Rewards;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bRepeatable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	EDocQuestOwnerPolicy OwnerPolicy = EDocQuestOwnerPolicy::PerPlayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	EDocClockDomain TimerClock = EDocClockDomain::WorldGameplay;

	/** Internal record id: "Objective.<ObjectiveId>". */
	FName GetRecordId() const { return FName(*FString::Printf(TEXT("Objective.%s"), *Objective.ObjectiveId.ToString())); }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocObjectiveAsset"), GetFName()); }
};

// ---------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------

/**
 * Typed progress observation. Produced by trusted sources (registered by SourceId) on the
 * authority: host code, the DocEvents bridge, Regions/Interaction/Inventory/Knowledge/
 * Dialogue/Unlock bridges. An arbitrary client broadcast is not an observation.
 */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestObservation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FName SourceId;

	/** Identity for dedup when the source has no sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGuid EventId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FDocOwnerScope Owner;

	/** World generation the observation belongs to (0 = world-independent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	int32 WorldGeneration = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTag TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTag SenderTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTagContainer PayloadTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	int32 Amount = 1;

	/** Source epoch + sequence (> 0) enable high-water dedup with a bounded out-of-order window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	int64 SourceEpoch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	int64 Sequence = 0;

	/** Replay of history; ignored unless the quest's ReplayPolicy accepts it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	bool bHistorical = false;
};

// ---------------------------------------------------------------------------
// Runtime state (also the save schema)
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocObjectiveRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FName ObjectiveId;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FGuid ObjectiveInstanceId;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	EDocObjectiveState State = EDocObjectiveState::Inactive;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bHidden = false;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bOptional = false;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	int32 TargetCount = 1;

	/** Wait evaluator: seconds left on the quest's timer clock (-1 = none). */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	double WaitRemaining = -1.0;

	/** Time limit: seconds left (-1 = none). */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	double LimitRemaining = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FGuid LastMatchedEventId;

	/** Debug: why the last relevant observation or provider query did not advance it. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FString LastIgnoredReason;

	/** Removed by migration (optional objectives only); excluded from evaluation, kept for inspection. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bQuarantined = false;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestRewardIntent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocEffectKey Key;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocQuestAction Action;

	/** Reward, StageCompletion, ObjectiveCompletion or ObjectiveFailure. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FName Source;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	EDocRewardIntentState State = EDocRewardIntentState::Pending;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 Attempts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocSystemResult LastResult;
};

/** Sequence high-water mark with a 64-entry out-of-order window per (source, epoch). */
USTRUCT()
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestDedupSource
{
	GENERATED_BODY()

	UPROPERTY() FName SourceId;
	UPROPERTY() int64 SourceEpoch = 0;
	UPROPERTY() int64 HighWater = 0;
	/** Bit k = sequence (HighWater - k) seen. */
	UPROPERTY() uint64 WindowBits = 0;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FName QuestId;

	/** New for every activation, including repeats. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FGuid QuestInstanceId;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 RepeatOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocOwnerScope Owner;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	EDocQuestState State = EDocQuestState::Inactive;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FName StageId;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FName> CompletedStages;

	/** Objectives of the current stage. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FDocObjectiveRuntimeState> Objectives;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int64 Revision = 0;

	/** Monotonic per instance; part of every effect key. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int64 TransitionOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 ContentVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int64 ActivatedAtUtcTicks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int64 EndedAtUtcTicks = 0;

	/** Completed and RewardDeliveryPending are separate facts. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bRewardDeliveryPending = false;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestRewardIntent> Intents;

	UPROPERTY() TArray<FGuid> RecentEventIds;
	UPROPERTY() TArray<FDocQuestDedupSource> DedupSources;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FString LastTransitionCause;

	const FDocObjectiveRuntimeState* FindObjective(FName ObjectiveId) const
	{
		return Objectives.FindByPredicate([ObjectiveId](const FDocObjectiveRuntimeState& O) { return O.ObjectiveId == ObjectiveId; });
	}
};

/** One quest for one owner: the current (or last) instance plus lifetime counters. */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestOwnerRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocOwnerScope Owner;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FName QuestId;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocQuestRuntimeState Current;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 TimesCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 TimesFailed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 TimesCancelled = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bStandalone = false;

	/** Kept but not evaluated (missing definition or critical stage). Preserved in saves. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bQuarantined = false;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FString QuarantineReason;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocTrackedQuestRef
{
	GENERATED_BODY()

	/** Owner of the progress (may be a shared scope). */
	UPROPERTY(BlueprintReadWrite, Category = "Quest")
	FDocOwnerScope Owner;

	UPROPERTY(BlueprintReadWrite, Category = "Quest")
	FName QuestId;

	/** None for a quest reference. */
	UPROPERTY(BlueprintReadWrite, Category = "Quest")
	FName ObjectiveId;

	bool IsSet() const { return !QuestId.IsNone(); }
	friend bool operator==(const FDocTrackedQuestRef& A, const FDocTrackedQuestRef& B) { return A.Owner == B.Owner && A.QuestId == B.QuestId && A.ObjectiveId == B.ObjectiveId; }
};

/** Local, per-viewer tracking. Never changes progress. */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestTrackingState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocOwnerScope Viewer;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FDocTrackedQuestRef TrackedQuest;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FDocTrackedQuestRef> TrackedObjectives;
};

/** Ordered change notification (a committed revision). */
USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") FName QuestId;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") FGuid QuestInstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") EDocQuestState QuestState = EDocQuestState::Inactive;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") FName StageId;
	/** None for quest-level changes. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest") FName ObjectiveId;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") EDocObjectiveState ObjectiveState = EDocObjectiveState::Inactive;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") int32 Count = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") int32 TargetCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Quest") FString Cause;
};

USTRUCT(BlueprintType)
struct DOCQUESTOBJECTIVESRUNTIME_API FDocQuestSaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestOwnerRecord> Records;

	/** Optional local tracking. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FDocQuestTrackingState> Tracking;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Quest Objectives"))
class DOCQUESTOBJECTIVESRUNTIME_API UDocQuestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** EventId dedup window per quest instance (entries). Older ids are forgotten: dedup is not indefinite. */
	UPROPERTY(Config, EditAnywhere, Category = "Dedup", meta = (ClampMin = "1"))
	int32 EventIdWindow = 256;

	UPROPERTY(Config, EditAnywhere, Category = "Activation", meta = (ClampMin = "1"))
	int32 MaxAutoActivationPasses = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName EventProviderId = TEXT("Event");

	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName TagProviderId = TEXT("Tags");

	/** The world facade advances Simulation timers from world time unless a time bridge drives them. */
	UPROPERTY(Config, EditAnywhere, Category = "Timers")
	bool bDriveSimulationFromWorldTime = true;
};
