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
#include "DocUnlockTypes.generated.h"

/**
 * DocUnlocksProgression data model (Modules 11-20 handoff, Section 11; Module 19).
 *
 * Name mapping to the handoff's semantic contracts:
 *   UnlockSubsystem     -> UDocUnlockSubsystem (GameInstance)
 *   UnlockDefinition    -> UDocUnlockDefinition
 *   UnlockCondition     -> FDocUnlockCondition inside an FDocUnlockExpression
 *   UnlockAction        -> FDocUnlockAction
 *   UnlockRuntimeState  -> FDocUnlockRecord (facts) / FDocUnlockSnapshot (derived view)
 */
namespace DocUnlockTags
{
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Ability);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Area);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Feature);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Item);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Mode);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Travel);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Interaction);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock_Custom);

	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_UnknownUnlock);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_MissingPrerequisite);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_PrerequisiteLocked);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_ConditionUnsatisfied);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_ProviderMissing);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_Disabled);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_NoGrant);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_Gated);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_Cycle);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_StaleRevision);
	DOCUNLOCKSPROGRESSIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Unlock_Private);
}

// ---------------------------------------------------------------------------
// Conditions as bounded typed data
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocUnlockConditionType : uint8
{
	/** Another unlock (UnlockId) is effectively available (or entitled, see bRequireEntitlementOnly). */
	Prerequisite,
	/** Owner tag state through the tag condition provider (Settings.TagProviderId) or granted tag claims. */
	GameplayTag,
	/** Owner progress value ProgressKey compared with Threshold. */
	NumericThreshold,
	/** Through the provider Settings.EventProviderId. */
	WorldEvent,
	/** Through the provider Settings.TimeProviderId. */
	Time,
	/** Registered provider by ProviderId (custom Blueprint host, or bridges: quest, knowledge, item, region, dialogue). */
	Custom
};

UENUM(BlueprintType)
enum class EDocUnlockCompare : uint8
{
	GreaterOrEqual,
	Greater,
	LessOrEqual,
	Less,
	Equal
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EDocUnlockConditionType Type = EDocUnlockConditionType::Prerequisite;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName UnlockId;

	/** Prerequisite: satisfied by the permanent entitlement only (not temporary/live availability). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bRequireEntitlementOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName ProgressKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EDocUnlockCompare Compare = EDocUnlockCompare::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	double Threshold = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName ProviderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName QueryId;
};

UENUM(BlueprintType)
enum class EDocUnlockExprOp : uint8
{
	Leaf,
	And,
	Or,
	Not
};

/** Node of a bounded expression tree stored as a flat array (children by index). */
USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockExprNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EDocUnlockExprOp Op = EDocUnlockExprOp::Leaf;

	/** Indices into FDocUnlockExpression::Nodes (And/Or: 1+, Not: exactly 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	TArray<int32> Children;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FDocUnlockCondition Condition;
};

/**
 * AND/OR/NOT expression over conditions. Empty = Satisfied. Unavailable combines per
 * Core rules (AND: Unavailable dominates; OR: any Satisfied wins) and is never read as satisfied.
 */
USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockExpression
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	TArray<FDocUnlockExprNode> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 Root = 0;

	bool IsEmpty() const { return Nodes.IsEmpty(); }

	/** Structural validation: indices in range, arities, acyclic, depth <= MaxDepth. */
	bool Validate(FString& OutError, int32 MaxDepth = 16) const;

	/** Every leaf condition. */
	void CollectLeaves(TArray<const FDocUnlockCondition*>& Out) const;

	static FDocUnlockExpression Leaf(const FDocUnlockCondition& Condition);
	static FDocUnlockExpression Combine(EDocUnlockExprOp Op, const TArray<FDocUnlockExpression>& Parts);
};

// ---------------------------------------------------------------------------
// Actions, temporary sources, definitions
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocUnlockActionType : uint8
{
	BroadcastEvent,
	/** Adds a source-owned tag claim (the unlock is the source). */
	GrantGameplayTag,
	/** Removes this unlock's tag claim only; other sources keep the tag. */
	RemoveGameplayTag,
	Custom
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName ActionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EDocUnlockActionType Type = EDocUnlockActionType::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName ProviderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FString Payload;
};

UENUM(BlueprintType)
enum class EDocUnlockEvaluationPolicy : uint8
{
	/** Only EvaluateUnlock re-evaluates it. */
	Manual,
	/** Re-evaluated immediately when a relevant input changes. */
	EventDriven,
	/** Marked dirty; evaluated by EvaluateDirty (budgeted). */
	Batch,
	/** EventDriven, and dependents cascade within the same batch. */
	Auto
};

UENUM(BlueprintType)
enum class EDocUnlockTemporaryKind : uint8
{
	DurationBased,
	ConditionBased,
	SessionBased,
	RegionBased,
	Custom
};

/** What happens when a condition-based grant's provider is missing. */
UENUM(BlueprintType)
enum class EDocUnlockProviderLossPolicy : uint8
{
	FailClosed,
	/** Kept but reported Unavailable (not available until the provider returns). */
	Suspend
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockTemporarySource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	EDocUnlockTemporaryKind Kind = EDocUnlockTemporaryKind::DurationBased;

	/** Stable id of the system/item/area granting it (claims are per source). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock", meta = (ClampMin = "0"))
	float DurationSeconds = 0.f;

	/** Default runtime clock: does not age while the application is closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	EDocClockDomain Clock = EDocClockDomain::RealTime;

	/** Duration/Condition/Region grants only: survive save/load (as descriptors, never pointers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	bool bPersist = false;

	/** ConditionBased: valid while this expression is satisfied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	FDocUnlockExpression Condition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	EDocUnlockProviderLossPolicy ProviderLoss = EDocUnlockProviderLossPolicy::FailClosed;

	/** SessionBased: expires with EndSession(SessionId). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	FGuid SessionId;

	/** RegionBased: valid while the owner is inside (NotifyRegionMembership). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unlock")
	FName RegionId;
};

UENUM(BlueprintType)
enum class EDocUnlockVisibility : uint8
{
	/** Visible unless HiddenUntilAvailable and not available. */
	Default,
	ForceVisible,
	ForceHidden
};

/** UnlockDefinition. Immutable at runtime. */
UCLASS(BlueprintType)
class DOCUNLOCKSPROGRESSIONRUNTIME_API UDocUnlockDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName UnlockId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 ContentVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock", meta = (Categories = "Unlock"))
	FGameplayTag Category;

	/** Eligibility over other unlocks (usually Prerequisite leaves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FDocUnlockExpression Prerequisites;

	/** Further eligibility conditions (thresholds, tags, providers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FDocUnlockExpression Conditions;

	/** Ongoing live gate: an earned entitlement may be temporarily unusable without being deleted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FDocUnlockExpression AvailabilityGate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EDocUnlockEvaluationPolicy EvaluationPolicy = EDocUnlockEvaluationPolicy::Auto;

	/** The original AutoEvaluate flag: false forces Manual. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bAutoEvaluate = true;

	/** True: eligibility latches into a permanent entitlement. False: available only while eligible (reversible). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bPermanent = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bHiddenUntilAvailable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	TArray<FDocUnlockAction> UnlockActions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	TArray<FDocUnlockAction> LockActions;

	/** Temporary sources this unlock accepts (empty = any). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	TArray<EDocUnlockTemporaryKind> AllowedTemporaryKinds;

	EDocUnlockEvaluationPolicy GetEffectivePolicy() const { return bAutoEvaluate ? EvaluationPolicy : EDocUnlockEvaluationPolicy::Manual; }
	void FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;
	/** UnlockIds referenced by Prerequisite leaves in any expression. */
	TArray<FName> GetPrerequisiteIds() const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocUnlockDefinition"), GetFName()); }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

// ---------------------------------------------------------------------------
// Runtime facts (also the save schema) and derived views
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockTemporaryGrant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FGuid GrantId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocUnlockTemporarySource Source;
	/** Duration grants: seconds left on Source.Clock (-1 = not duration-based). */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") double RemainingSeconds = -1.0;
	/** Region grants: current membership (recomputed; false after load until notified). */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bRegionInside = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 AcquiredAtUtcTicks = 0;
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockAuditEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName UnlockId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FString Operation;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FString Reason;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 AtUtcTicks = 0;
};

UENUM(BlueprintType)
enum class EDocUnlockIntentState : uint8
{
	Pending,
	Delivered,
	Failed
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockActionIntent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocEffectKey Key;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName UnlockId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocUnlockAction Action;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") EDocUnlockIntentState State = EDocUnlockIntentState::Pending;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int32 Attempts = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocSystemResult LastResult;
};

/** Independent facts for one unlock and owner (UnlockRuntimeState). */
USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName UnlockId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bPermanentEntitlement = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 EntitledAtUtcTicks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FString EntitlementSource;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockTemporaryGrant> TemporaryGrants;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bAdministrativelyDisabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") EDocUnlockVisibility Visibility = EDocUnlockVisibility::Default;
	/** Last committed effective availability (baseline for transition-only actions). */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bLastEffective = false;
	/** Monotonic transition ordinal (effect keys). */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 TransitionOrdinal = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 LastEvaluatedRevision = 0;
};

UENUM(BlueprintType)
enum class EDocUnlockDisplayLabel : uint8
{
	Locked,
	Unlocked,
	TemporarilyUnlocked,
	Disabled
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockBlockingReason
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FGameplayTag ReasonTag;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") EDocConditionState State = EDocConditionState::Unsatisfied;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FString Diagnostic;
};

/** Derived view (pure). Labels report the underlying reason; no contradictory booleans. */
USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName UnlockId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocUnlockRecord Facts;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocConditionResult Eligibility;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocConditionResult Gate;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bHasValidTemporaryGrant = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bEffectiveAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bVisible = true;
	/** Dirty and not yet evaluated (budgeted batch). */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bPending = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") EDocUnlockDisplayLabel Label = EDocUnlockDisplayLabel::Locked;
	/** Owner revision; pass to ValidateGate. */
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int64 OwnerRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int32 PendingActions = 0;
};

UENUM(BlueprintType)
enum class EDocUnlockProgressMode : uint8
{
	SetValue,
	AddDelta,
	/** Observed current state (e.g. a bridge's live count); same as SetValue but recorded as observed. */
	Observed
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockProgressValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName Key;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") double Value = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bObserved = false;
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockTagClaim
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FGameplayTag Tag;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FName> Sources;
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockOwnerSave
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockRecord> Records;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockProgressValue> Progress;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockTagClaim> TagClaims;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockActionIntent> Intents;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockAuditEntry> Audit;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocEffectReceipt> Receipts;
};

USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockSaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") int32 SchemaVersion = CurrentSchemaVersion;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") TArray<FDocUnlockOwnerSave> Owners;
};

/** Change notification after a committed batch. */
USTRUCT(BlueprintType)
struct DOCUNLOCKSPROGRESSIONRUNTIME_API FDocUnlockChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FName UnlockId;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bEffectiveAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") bool bPermanentEntitlement = false;
	UPROPERTY(BlueprintReadOnly, Category = "Unlock") FString Cause;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Unlocks Progression"))
class DOCUNLOCKSPROGRESSIONRUNTIME_API UDocUnlockSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "Evaluation", meta = (ClampMin = "1"))
	int32 DefaultEvaluationBudget = 256;

	UPROPERTY(Config, EditAnywhere, Category = "Evaluation", meta = (ClampMin = "1"))
	int32 MaxExpressionDepth = 16;

	/** Progress values are clamped to +/- this magnitude; beyond it input is rejected. */
	UPROPERTY(Config, EditAnywhere, Category = "Evaluation")
	double MaxProgressMagnitude = 1.0e15;

	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName TagProviderId = TEXT("Tags");

	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName EventProviderId = TEXT("Event");

	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName TimeProviderId = TEXT("Time");

	UPROPERTY(Config, EditAnywhere, Category = "Persistence", meta = (ClampMin = "0"))
	int32 MaxAuditEntriesPerOwner = 256;
};
