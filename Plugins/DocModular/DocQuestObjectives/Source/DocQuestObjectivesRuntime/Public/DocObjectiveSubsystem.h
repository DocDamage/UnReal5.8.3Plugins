#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocQuestTypes.h"
#include "DocObjectiveSubsystem.generated.h"

// ---------------------------------------------------------------------------
// Providers (registered explicitly; bridges implement them)
// ---------------------------------------------------------------------------

struct FDocQuestConditionQuery
{
	const FDocQuestCondition* Condition = nullptr;
	FDocOwnerScope Owner;
	/** Quest being evaluated (prerequisite or objective eligibility); None for ad-hoc queries. */
	FName QuestId;
};

/** Pure condition provider. Return Unavailable when the data is not loaded; never guess. */
class DOCQUESTOBJECTIVESRUNTIME_API IDocQuestConditionProvider
{
public:
	virtual ~IDocQuestConditionProvider() = default;
	virtual FDocConditionResult EvaluateQuestCondition(const FDocQuestConditionQuery& Query) const = 0;
};

/** Current-state source for CurrentState objectives (e.g. an inventory bridge: "currently own N"). */
class DOCQUESTOBJECTIVESRUNTIME_API IDocQuestStateProvider
{
public:
	virtual ~IDocQuestStateProvider() = default;
	/** False when the state is unavailable. Must be side-effect free. */
	virtual bool QueryCurrentCount(const FDocOwnerScope& Owner, const FDocObjectiveDefinition& Objective, int32& OutCount) const = 0;
};

struct FDocQuestEffectRequest
{
	const FDocQuestAction* Action = nullptr;
	/** Consumers store a receipt keyed by this and return their original result for a repeat. */
	FDocEffectKey Key;
	FDocOwnerScope Owner;
	FName QuestId;
	FGuid QuestInstanceId;
	FName Source;
};

/**
 * Effect consumer (reward/completion action). Success or NoChange = delivered.
 * InvalidInput/InvalidConfiguration/Unsupported/PermissionDenied are permanent; other
 * failures (a full inventory: Conflict/Unavailable/Failed...) leave the intent Pending.
 */
class DOCQUESTOBJECTIVESRUNTIME_API IDocQuestEffectProvider
{
public:
	virtual ~IDocQuestEffectProvider() = default;
	virtual FDocSystemResult DeliverEffect(const FDocQuestEffectRequest& Request) = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocQuestChangeEvent, const FDocQuestChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocQuestRewardEvent, const FDocOwnerScope&, Owner, FName, QuestId, bool, bRewardDeliveryPending);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocQuestTrackingEvent, const FDocOwnerScope&, Viewer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocQuestRefreshEvent);

DECLARE_MULTICAST_DELEGATE_OneParam(FDocQuestChangeNative, const FDocQuestChange&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocQuestRewardNative, const FDocOwnerScope&, FName, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocQuestTrackingNative, const FDocOwnerScope&);

/**
 * Quests and objectives for every owner scope in the game instance (UObjectiveSubsystem,
 * handoff Section 7). Records are partitioned by owner scope (which carries the campaign
 * namespace); shared and per-player quests are authored policies. Records survive world
 * travel; the world facade attaches/detaches worlds and drives timers.
 *
 * Processing order for an observation: commit objective changes -> evaluate stage ->
 * evaluate quest -> stage effect intents -> publish ordered change events -> deliver
 * intents. Observers always see a complete committed revision; nested calls from
 * handlers run after the current commit.
 *
 * Owner authorization: callers pass owner scopes from trusted host context (a client's
 * claim is not proof). Observations must come from registered trusted sources on the authority.
 */
UCLASS()
class DOCQUESTOBJECTIVESRUNTIME_API UDocObjectiveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocObjectiveSubsystem* Get(const UObject* WorldContextObject);

	// ---- Definitions / sources / providers ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult RegisterQuestDefinition(UDocQuestDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult RegisterObjectiveAsset(UDocObjectiveAsset* Objective);

	const UDocQuestDefinition* FindDefinition(FName QuestId) const;

	void RegisterTrustedSource(FName SourceId) { TrustedSources.Add(SourceId); }
	void UnregisterTrustedSource(FName SourceId) { TrustedSources.Remove(SourceId); }
	void RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocQuestConditionProvider> Provider);
	void RegisterStateProvider(FName ProviderId, TSharedPtr<IDocQuestStateProvider> Provider);
	void RegisterEffectProvider(FName ProviderId, TSharedPtr<IDocQuestEffectProvider> Provider);

	/** Declares an owner present (enables bounded auto-activation for it). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	void RegisterOwner(const FDocOwnerScope& Owner);

	// ---- Commands ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult ActivateQuest(const FDocOwnerScope& Owner, FName QuestId, FGuid& OutQuestInstanceId);

	/** Standalone objective (registered with RegisterObjectiveAsset). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult ActivateObjective(const FDocOwnerScope& Owner, FName ObjectiveId, FGuid& OutInstanceId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult CancelQuest(const FDocOwnerScope& Owner, FName QuestId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult SubmitObservation(const FDocQuestObservation& Observation);

	/** Re-query CurrentState/CustomCondition objectives and eligibility for Owner. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult RefreshCurrentState(const FDocOwnerScope& Owner);

	/** Deliver pending intents again with their original effect keys (retry or claim). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult RetryRewardDelivery(const FDocOwnerScope& Owner, FName QuestId);

	/** Advance timers on one clock (the world facade calls this; also used for explicit time jumps). */
	void AdvanceClock(EDocClockDomain Domain, double Seconds);

	/** Opt-in offline progress: only objectives with bAllowOfflineProgress. */
	void ApplyOfflineElapsed(const FDocOwnerScope& Owner, double Seconds);

	// ---- Queries ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	bool GetQuestSnapshot(const FDocOwnerScope& Owner, FName QuestId, FDocQuestOwnerRecord& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	TArray<FName> GetActiveQuests(const FDocOwnerScope& Owner) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocConditionResult EvaluatePrerequisites(const FDocOwnerScope& Owner, FName QuestId) const;

	// ---- Tracking (local per viewer; never changes progress) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult SetTrackedQuest(const FDocOwnerScope& Viewer, const FDocOwnerScope& ProgressOwner, FName QuestId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocSystemResult SetTrackedObjective(const FDocOwnerScope& Viewer, const FDocOwnerScope& ProgressOwner, FName QuestId, FName ObjectiveId, bool bTracked);

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	FDocTrackedQuestRef GetTrackedQuest(const FDocOwnerScope& Viewer) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Quest")
	TArray<FDocTrackedQuestRef> GetTrackedObjectives(const FDocOwnerScope& Viewer) const;

	// ---- Persistence ----
	FDocQuestSaveData CaptureState(bool bIncludeTracking = true) const;
	/** Validate -> migrate -> stage -> apply under a restore barrier -> rebuild -> refresh event. Never replays effects. */
	FDocSystemResult RestoreState(const FDocQuestSaveData& Data, bool bRestoreTracking = true);

	// ---- World binding ----
	void AttachWorld(UWorld* World);
	void DetachWorld(UWorld* World);
	UFUNCTION(BlueprintPure, Category = "Doc|Quest")
	int32 GetWorldGeneration() const { return WorldGeneration; }
	bool IsAttachedTo(const UWorld* World) const { return World && AttachedWorld.Get() == World; }

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Quest") FDocQuestChangeEvent OnQuestChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Quest") FDocQuestChangeEvent OnObjectiveChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Quest") FDocQuestRewardEvent OnRewardDeliveryChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Quest") FDocQuestTrackingEvent OnTrackingChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Quest") FDocQuestRefreshEvent OnStateRefreshed;
	FDocQuestChangeNative OnQuestChangedNative;
	FDocQuestChangeNative OnObjectiveChangedNative;
	FDocQuestRewardNative OnRewardDeliveryChangedNative;
	FDocQuestTrackingNative OnTrackingChangedNative;
	FSimpleMulticastDelegate OnStateRefreshedNative;

	// ---- Tests ----
	static void SetSubsystemOverrideForTesting(UDocObjectiveSubsystem* Override);
	int32 GetIndexedTagCountForTesting(const FDocOwnerScope& Owner) const;
	int32 GetActiveTimerCountForTesting() const;

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FQuestKey
	{
		FDocOwnerScope Owner;
		FName QuestId;
		friend bool operator==(const FQuestKey& A, const FQuestKey& B) { return A.Owner == B.Owner && A.QuestId == B.QuestId; }
		friend uint32 GetTypeHash(const FQuestKey& K) { return HashCombine(GetTypeHash(K.Owner), GetTypeHash(K.QuestId)); }
	};

	struct FTimerRef
	{
		FQuestKey Key;
		FGuid ObjectiveInstanceId;
		EDocClockDomain Clock = EDocClockDomain::WorldGameplay;
	};

	struct FDeliveryRef
	{
		FQuestKey Key;
		FDocEffectKey EffectKey;
	};

	enum class EFailKind : uint8 { None, Stage, Quest, Branch };

	bool HasAuthority() const;
	const UDocQuestSettings* Settings() const;
	FDocQuestOwnerRecord* FindRecord(const FQuestKey& Key) { return Records.Find(Key); }
	const FDocObjectiveDefinition* FindObjectiveDef(const UDocQuestDefinition& Def, FName StageId, FName ObjectiveId) const;
	FDocSystemResult CheckOwnerPolicy(const UDocQuestDefinition& Def, const FDocOwnerScope& Owner) const;

	// Activation / stage machine
	FDocSystemResult ActivateInternal(const FDocOwnerScope& Owner, const UDocQuestDefinition& Def, bool bStandalone, FGuid& OutInstanceId);
	void EnterStage(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, FName StageId, const FString& Cause);
	bool ActivateEligible(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestStage& Stage, const FString& Cause);
	void ActivateObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause);
	void CompleteObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause);
	void FailObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause);
	bool RefreshObjectiveState(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause);
	bool IsStageComplete(const FDocQuestStage& Stage, const FDocQuestRuntimeState& Q) const;
	void CloseStage(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestStage& Stage, bool bSucceeded, const FString& Cause);
	void EndQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, EDocQuestState Final, const FString& Cause);
	void EvaluateQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FString& Cause, TArray<const FDocQuestFailureRule*> Rules);
	void AddIntents(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const TArray<FDocQuestAction>& Actions, FName Source);
	void RecomputeRewardPending(FDocQuestRuntimeState& Q) const;

	// Observations
	int32 ProcessForQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestObservation& Obs, TArray<FString>& OutIgnored);
	int32 ApplyToSnapshot(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestObservation& Obs, const TSet<FGuid>& Snapshot);
	bool CheckAndRecordDedup(FDocQuestRuntimeState& Q, const FDocQuestObservation& Obs, FString& OutReason) const;

	// Conditions
	FDocConditionResult EvaluateCondition(const FDocOwnerScope& Owner, FName QuestId, const FDocQuestCondition& C) const;
	FDocConditionResult EvaluateConditions(const FDocOwnerScope& Owner, FName QuestId, const TArray<FDocQuestCondition>& Conditions) const;

	// Delivery
	FDocSystemResult DeliverIntent(const FQuestKey& Key, const FDocEffectKey& EffectKey);
	IDocQuestEffectProvider* FindEffectProvider(const FDocQuestAction& Action) const;

	// Timers
	void AdvanceTimers(EDocClockDomain Domain, double Seconds, const FDocOwnerScope* OnlyOwner, bool bOfflineOnly);

	// Index / events
	void MarkIndexDirty() { bIndexDirty = true; }
	void RebuildIndexIfDirty();
	void RunAutoActivation(const FDocOwnerScope& Owner);
	void QueueQuestChange(const FDocQuestOwnerRecord& R, const FString& Cause);
	void QueueObjectiveChange(const FDocQuestOwnerRecord& R, const FDocObjectiveRuntimeState& O, const FString& Cause);
	void QueueRewardChange(const FDocQuestOwnerRecord& R);
	void QueueTrackingChange(const FDocOwnerScope& Viewer);
	void DropTrackingFor(const FDocOwnerScope& Owner, FName QuestId);
	void EnterApi() { ++ApiDepth; }
	void LeaveApi();

	struct FApiScope
	{
		UDocObjectiveSubsystem& Owner;
		explicit FApiScope(UDocObjectiveSubsystem& In) : Owner(In) { Owner.EnterApi(); }
		~FApiScope() { Owner.LeaveApi(); }
	};

	TMap<FQuestKey, FDocQuestOwnerRecord> Records;
	TMap<FName, TObjectPtr<UDocQuestDefinition>> Definitions;
	UPROPERTY() TArray<TObjectPtr<UDocQuestDefinition>> DefinitionRefs;
	TMap<FName, FName> StandaloneRecordIds; // ObjectiveId -> record QuestId
	TSet<FName> TrustedSources;
	TMap<FName, TSharedPtr<IDocQuestConditionProvider>> ConditionProviders;
	TMap<FName, TSharedPtr<IDocQuestStateProvider>> StateProviders;
	TMap<FName, TSharedPtr<IDocQuestEffectProvider>> EffectProviders;
	TMap<FDocOwnerScope, FDocQuestTrackingState> Tracking;
	TSet<FDocOwnerScope> KnownOwners;

	// Derived indices (rebuilt lazily): active listeners per owner and tag, active timers.
	TMap<FDocOwnerScope, TMap<FGameplayTag, TArray<FQuestKey>>> EventIndex;
	TArray<FTimerRef> Timers;
	bool bIndexDirty = true;

	TWeakObjectPtr<UWorld> AttachedWorld;
	int32 WorldGeneration = 0;

	TArray<TFunction<void()>> PendingEvents;
	TArray<FDeliveryRef> PendingDeliveries;
	TSet<FDocOwnerScope> PendingAutoActivation;
	int32 ApiDepth = 0;
	bool bRestoring = false;
};

/**
 * World-bound facade: attaches its world to the objective service (a new world generation,
 * so observations stamped for an old world are rejected), detaches on teardown (records are
 * retained), and drives WorldGameplay/RealTime (and optionally Simulation) timers. Only
 * active timers are touched; inactive quests are never polled.
 */
UCLASS()
class DOCQUESTOBJECTIVESRUNTIME_API UDocObjectiveWorldFacade : public UTickableWorldSubsystem
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
