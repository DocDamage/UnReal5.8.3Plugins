#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocUnlockTypes.h"
#include "DocUnlockSubsystem.generated.h"

struct FDocUnlockConditionQuery
{
	const FDocUnlockCondition* Condition = nullptr;
	FDocOwnerScope Owner;
	FName UnlockId;
};

/** Pure, owner-scoped condition provider (tags, world events, time, custom, and bridges). */
class DOCUNLOCKSPROGRESSIONRUNTIME_API IDocUnlockConditionProvider
{
public:
	virtual ~IDocUnlockConditionProvider() = default;
	/** Unavailable when the data is not loaded; never guess. */
	virtual FDocConditionResult EvaluateUnlockCondition(const FDocUnlockConditionQuery& Query) const = 0;
};

struct FDocUnlockEffectRequest
{
	const FDocUnlockAction* Action = nullptr;
	/** Stable per transition; consumers dedupe by it. */
	FDocEffectKey Key;
	FDocOwnerScope Owner;
	FName UnlockId;
	/** True for UnlockActions, false for LockActions. */
	bool bUnlocking = true;
};

/** Effect consumer. Success/NoChange = delivered; InvalidInput/InvalidConfiguration/Unsupported/PermissionDenied = permanent failure; others stay pending. */
class DOCUNLOCKSPROGRESSIONRUNTIME_API IDocUnlockEffectProvider
{
public:
	virtual ~IDocUnlockEffectProvider() = default;
	virtual FDocSystemResult DeliverUnlockEffect(const FDocUnlockEffectRequest& Request) = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocUnlockChangeEvent, const FDocUnlockChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocUnlockRefreshEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocUnlockChangeNative, const FDocUnlockChange&);

/**
 * Entitlements, temporary grants, progress and gates for every owner scope
 * (UnlockSubsystem, handoff Section 11). Unlocking an Area loads nothing and unlocking
 * an Ability implements nothing: consumer adapters own those actions.
 *
 * Effective availability = not administratively disabled AND (permanent entitlement OR a
 * currently valid temporary grant OR, for non-permanent unlocks, current eligibility)
 * AND the availability gate is satisfied (Unavailable fails closed).
 *
 * Evaluation: dirty nodes are processed in deterministic topological order within a
 * budget; changes are published after the batch; only real effective-state transitions
 * run unlock/lock actions.
 */
UCLASS()
class DOCUNLOCKSPROGRESSIONRUNTIME_API UDocUnlockSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocUnlockSubsystem* Get(const UObject* WorldContextObject);

	// ---- Catalog ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult RegisterDefinition(UDocUnlockDefinition* Definition);

	/** Atomic: validates duplicates, expressions and cycles across the combined catalog. */
	FDocSystemResult RegisterDefinitions(const TArray<UDocUnlockDefinition*>& Definitions);

	void ValidateCatalog(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;
	const UDocUnlockDefinition* FindDefinition(FName UnlockId) const;

	void RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocUnlockConditionProvider> Provider);
	void RegisterEffectProvider(FName ProviderId, TSharedPtr<IDocUnlockEffectProvider> Provider);

	/** An owner is present: Auto unlocks are evaluated for it. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	void RegisterOwner(const FDocOwnerScope& Owner);

	/** Provider data changed for Owner: dependent unlocks become dirty (EventDriven/Auto evaluate now). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	void NotifyProviderChanged(const FDocOwnerScope& Owner, FName ProviderId);

	// ---- Evaluation ----
	/** Force evaluation of one unlock (any policy, including Manual). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult EvaluateUnlock(const FDocOwnerScope& Owner, FName UnlockId);

	/** Evaluate up to Budget dirty unlocks (0 = settings default). Returns how many remain pending. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	int32 EvaluateDirty(const FDocOwnerScope& Owner, int32 Budget = 0);

	// ---- Mutations (trusted context) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult SetProgress(const FDocOwnerScope& Owner, FName ProgressKey, EDocUnlockProgressMode Mode, double Value);

	/** ReceiptKey (optional) makes a retried grant return its original result. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult GrantPermanent(const FDocOwnerScope& Owner, FName UnlockId, const FString& Reason, const FDocEffectKey& ReceiptKey);

	/** Authorized revoke/reset with an audit reason. The only way a permanent entitlement is removed. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult RevokePermanent(const FDocOwnerScope& Owner, FName UnlockId, const FString& AuditReason);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult AcquireTemporaryGrant(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockTemporarySource& Source, FGuid& OutGrantId);

	/** Releases only that grant, and only for its own source. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult ReleaseTemporaryGrant(const FDocOwnerScope& Owner, FName UnlockId, const FGuid& GrantId, FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	void EndSession(const FGuid& SessionId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	void NotifyRegionMembership(const FDocOwnerScope& Owner, FName RegionId, bool bInside);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult SetDisabled(const FDocOwnerScope& Owner, FName UnlockId, bool bDisabled, const FString& AuditReason);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult SetVisibility(const FDocOwnerScope& Owner, FName UnlockId, EDocUnlockVisibility Visibility);

	/** Timer step for one clock. Non-positive steps are ignored: expired grants are never resurrected. */
	void AdvanceClock(EDocClockDomain Domain, double Seconds);

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult RetryPendingActions(const FDocOwnerScope& Owner);

	// ---- Queries (pure) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocUnlockSnapshot GetUnlockSnapshot(const FDocOwnerScope& Owner, FName UnlockId) const;

	/** Permanent entitlement or a valid temporary grant (ignores disable/gate). */
	UFUNCTION(BlueprintPure, Category = "Doc|Unlock")
	bool IsUnlocked(const FDocOwnerScope& Owner, FName UnlockId) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Unlock")
	bool IsAvailable(const FDocOwnerScope& Owner, FName UnlockId) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	TArray<FDocUnlockBlockingReason> GetBlockingReasons(const FDocOwnerScope& Owner, FName UnlockId) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FGameplayTagContainer GetGrantedTags(const FDocOwnerScope& Owner) const;

	TArray<FDocUnlockAuditEntry> GetAuditLog(const FDocOwnerScope& Owner) const;
	TArray<FDocUnlockActionIntent> GetActionIntents(const FDocOwnerScope& Owner) const;
	int64 GetOwnerRevision(const FDocOwnerScope& Owner) const;

	// ---- Authoritative gates ----
	/** Authority-side check before the unlocked action runs. UI previews are advisory only. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult ValidateGate(const FDocOwnerScope& Owner, FName UnlockId, int64 ExpectedOwnerRevision) const;

	/** Private entitlements/reasons are only returned to their owner (shared/party scopes are public). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Unlock")
	FDocSystemResult GetSnapshotForViewer(const FDocOwnerScope& Viewer, const FDocOwnerScope& Owner, FName UnlockId, FDocUnlockSnapshot& OutSnapshot) const;

	// ---- Persistence ----
	FDocUnlockSaveData CaptureState() const;
	/** Validates, applies, rebuilds derived caches and establishes the effective baseline silently (no actions). */
	FDocSystemResult RestoreState(const FDocUnlockSaveData& Data);

	// ---- World ----
	void AttachWorld(UWorld* World) { AttachedWorld = World; }
	void DetachWorld(UWorld* World) { if (AttachedWorld.Get() == World) { AttachedWorld.Reset(); } }
	bool IsAttachedTo(const UWorld* World) const { return World && AttachedWorld.Get() == World; }

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Unlock") FDocUnlockChangeEvent OnUnlockChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Unlock") FDocUnlockRefreshEvent OnStateRefreshed;
	FDocUnlockChangeNative OnUnlockChangedNative;
	FSimpleMulticastDelegate OnStateRefreshedNative;

	// ---- Tests ----
	static void SetSubsystemOverrideForTesting(UDocUnlockSubsystem* Override);
	void SetAuthorityOverrideForTesting(TOptional<bool> bAuthority) { AuthorityOverride = bAuthority; }
	int32 GetEvaluationCountForTesting() const { return EvaluationCount; }

	virtual void Deinitialize() override;

private:
	struct FOwnerData
	{
		TMap<FName, FDocUnlockRecord> Records;
		TArray<FDocUnlockRecord> Quarantined; // unknown unlock ids from saves (preserved)
		TMap<FName, FDocUnlockProgressValue> Progress;
		TMap<FGameplayTag, TArray<FName>> TagClaims;
		TArray<FDocUnlockActionIntent> Intents;
		TArray<FDocUnlockAuditEntry> Audit;
		TSharedPtr<FDocReceiptLedger> Ledger;
		TSet<FName> Dirty;
		TMap<FName, bool> RegionInside;
		int64 Revision = 0;
	};

	struct FEvaluation
	{
		FDocConditionResult Eligibility;
		FDocConditionResult Gate;
		bool bTemporaryValid = false;
		bool bEntitled = false;
		bool bEffective = false;
	};

	bool HasAuthority() const;
	const UDocUnlockSettings* Settings() const;
	FOwnerData& GetOwnerData(const FDocOwnerScope& Owner);
	const FOwnerData* FindOwnerData(const FDocOwnerScope& Owner) const { return Owners.Find(Owner); }

	FDocConditionResult EvaluateLeaf(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockCondition& C) const;
	FDocConditionResult EvaluateNode(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockExpression& Expr, int32 Node, int32 Depth, TArray<FDocUnlockBlockingReason>* OutReasons) const;
	FDocConditionResult EvaluateExpression(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockExpression& Expr, TArray<FDocUnlockBlockingReason>* OutReasons = nullptr) const;
	bool IsGrantValid(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockTemporaryGrant& Grant) const;
	FEvaluation Evaluate(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, const FDocUnlockRecord* Record, TArray<FDocUnlockBlockingReason>* OutReasons = nullptr) const;

	/** Re-evaluate and commit one unlock. Returns true if effective availability changed. */
	bool Commit(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, bool bLatchEligibility, const FString& Cause, bool bSilent = false);
	void MarkDependentsDirty(const FDocOwnerScope& Owner, FName UnlockId);
	void MarkDirty(const FDocOwnerScope& Owner, const TArray<FName>& UnlockIds);
	/** bAllPolicies=false: EventDriven/Auto only (immediate). Returns remaining dirty count. */
	int32 RunBatch(const FDocOwnerScope& Owner, int32 Budget, bool bAllPolicies);
	void RunImmediate(const FDocOwnerScope& Owner) { RunBatch(Owner, Settings()->DefaultEvaluationBudget, false); }

	void EnqueueActions(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, FDocUnlockRecord& Record, bool bUnlocking);
	void DeliverIntent(const FDocOwnerScope& Owner, const FDocEffectKey& Key);
	IDocUnlockEffectProvider* FindEffectProvider(const FDocUnlockAction& Action) const;
	static FGuid ProducerIdFor(FName UnlockId);
	void AddAudit(const FDocOwnerScope& Owner, FName UnlockId, const FString& Operation, const FString& Reason);
	void QueueChange(const FDocOwnerScope& Owner, FName UnlockId, const FString& Cause);

	FDocSystemResult ValidateCatalogChange(const TArray<UDocUnlockDefinition*>& Added) const;
	void RebuildIndexes();

	void EnterApi() { ++ApiDepth; }
	void LeaveApi();
	struct FApiScope
	{
		UDocUnlockSubsystem& Owner;
		explicit FApiScope(UDocUnlockSubsystem& In) : Owner(In) { Owner.EnterApi(); }
		~FApiScope() { Owner.LeaveApi(); }
	};

	TMap<FName, TObjectPtr<UDocUnlockDefinition>> Definitions;
	UPROPERTY() TArray<TObjectPtr<UDocUnlockDefinition>> DefinitionRefs;
	TMap<FName, TSharedPtr<IDocUnlockConditionProvider>> ConditionProviders;
	TMap<FName, TSharedPtr<IDocUnlockEffectProvider>> EffectProviders;
	TMap<FDocOwnerScope, FOwnerData> Owners;

	// Derived catalog indexes.
	TMap<FName, TArray<FName>> Dependents;          // unlock -> unlocks referencing it
	TMap<FName, TArray<FName>> ProgressDependents;  // progress key -> unlocks
	TMap<FName, TArray<FName>> ProviderDependents;  // provider id -> unlocks
	TMap<FGameplayTag, TArray<FName>> TagDependents;
	TMap<FName, int32> TopoRank;

	TArray<TFunction<void()>> PendingEvents;
	TArray<TPair<FDocOwnerScope, FDocEffectKey>> PendingDeliveries;
	int32 ApiDepth = 0;
	int32 EvaluationCount = 0;
	TOptional<bool> AuthorityOverride;
	TWeakObjectPtr<UWorld> AttachedWorld;
};

/** Attaches its world (authority context) and drives WorldGameplay/RealTime/Simulation lease timers. */
UCLASS()
class DOCUNLOCKSPROGRESSIONRUNTIME_API UDocUnlockWorldFacade : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	double LastRealSeconds = -1.0;
};
