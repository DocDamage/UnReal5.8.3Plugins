#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "DocDialogueTypes.h"
#include "DocDialogueSubsystem.generated.h"

class UDocDialogueParticipantComponent;

// ---------------------------------------------------------------------------
// Participant interface (IDialogueParticipant)
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, BlueprintType)
class UDocDialogueParticipant : public UInterface
{
	GENERATED_BODY()
};

/**
 * Logical dialogue participant. Identity is ParticipantId (stable, saved), never an
 * actor name or role label: two NPCs playing the same role are different participants.
 */
class DOCDIALOGUERUNTIME_API IDocDialogueParticipant
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	FName GetDialogueParticipantId() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	FText GetDialogueDisplayName() const;

	/** Soft: resolving a portrait never force-loads a level or asset. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	TSoftObjectPtr<UTexture2D> GetDialoguePortrait() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	FSoftObjectPath GetDialogueVoiceProfile() const;

	/** Current actor, if the participant is loaded. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	AActor* GetDialogueActor() const;

	/** Append (do not replace) the participant's current tags. Side-effect free. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	void GetDialogueTags(FGameplayTagContainer& OutTags) const;

	/** True for shared participants (a narrator) that may be in several conversations. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Dialogue")
	bool AllowsConcurrentDialogue() const;
};

// ---------------------------------------------------------------------------
// Native providers (bridges register these; nothing is discovered implicitly)
// ---------------------------------------------------------------------------

struct FDocDialogueConditionQuery
{
	const FDocDialogueCondition* Condition = nullptr;
	FName GraphId;
	FGuid SessionId;
	FDocOwnerScope Owner;
	/** Role -> participant object (may be null for logical or lost participants). */
	TMap<FName, TWeakObjectPtr<UObject>> Participants;
	FGameplayTagContainer ContextTags;
	TWeakObjectPtr<UWorld> World;
};

/** Pure condition provider (EventState, CustomProvider, InterfaceQuery; bridges: quest, inventory, codex, unlock, region, time). */
class DOCDIALOGUERUNTIME_API IDocDialogueConditionProvider
{
public:
	virtual ~IDocDialogueConditionProvider() = default;
	/** Must be side-effect free. Return Unavailable when the data is not loaded. */
	virtual FDocConditionResult EvaluateCondition(const FDocDialogueConditionQuery& Query) const = 0;
};

struct FDocDialogueActionRequest
{
	const FDocDialogueAction* Action = nullptr;
	/** Stable across retries of the same transition; consumers should dedupe by it. */
	FDocEffectKey EffectKey;
	FName GraphId;
	FName NodeId;
	FName ChoiceId;
	TMap<FName, TWeakObjectPtr<UObject>> Participants;
	TWeakObjectPtr<UWorld> World;
};

struct FDocDialogueActionOutcome
{
	/** Pending = the provider completes later through UDocDialogueSubsystem::CompletePendingAction. */
	EDocDialogueActionState State = EDocDialogueActionState::Failed;
	FDocSystemResult Result;

	static FDocDialogueActionOutcome Committed() { FDocDialogueActionOutcome O; O.State = EDocDialogueActionState::Committed; O.Result = FDocSystemResult::MakeSuccess(); return O; }
	static FDocDialogueActionOutcome Pending() { FDocDialogueActionOutcome O; O.State = EDocDialogueActionState::Pending; O.Result = FDocSystemResult::MakeNoChange(TEXT("Pending")); return O; }
	static FDocDialogueActionOutcome Failed(const FDocSystemResult& R) { FDocDialogueActionOutcome O; O.State = EDocDialogueActionState::Failed; O.Result = R; return O; }
};

/** External action provider (BroadcastEvent via the event bridge, Custom, and bridge actions). */
class DOCDIALOGUERUNTIME_API IDocDialogueActionProvider
{
public:
	virtual ~IDocDialogueActionProvider() = default;
	virtual FDocDialogueActionOutcome ExecuteAction(const FDocDialogueActionRequest& Request) = 0;
	/** True if re-executing with the same EffectKey is safe (the consumer dedupes). Used after restore. */
	virtual bool IsIdempotent() const { return false; }
	/** Only providers with a validated compensation contract return true. */
	virtual bool SupportsCompensation() const { return false; }
	virtual FDocSystemResult CompensateAction(const FDocDialogueActionRequest& Request) { return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("No compensation")); }
};

struct FDocDialogueCustomNodeResult
{
	/** Success -> continue at Next (None ends the conversation). Failure fails the session. */
	FDocSystemResult Result;
	FName Next;
};

/** Custom node handler, registered per CustomType. Runs synchronously. */
class DOCDIALOGUERUNTIME_API IDocDialogueCustomNodeHandler
{
public:
	virtual ~IDocDialogueCustomNodeHandler() = default;
	virtual FDocDialogueCustomNodeResult RunNode(const FDocDialogueNode& Node, const FDocDialogueSessionSnapshot& Session) = 0;
};

// ---------------------------------------------------------------------------
// Delegates
// ---------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocDialogueLineEvent, FDocRequestHandle, Session, const FDocDialogueLineInfo&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocDialogueChoicesEvent, FDocRequestHandle, Session, const FDocDialogueChoiceList&, Choices);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocDialogueStateEvent, FDocRequestHandle, Session, EDocDialogueSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocDialogueEndedEvent, FDocRequestHandle, Session, EDocDialogueSessionState, FinalState, const FDocSystemResult&, Result);

DECLARE_MULTICAST_DELEGATE_TwoParams(FDocDialogueLineNative, FDocRequestHandle, const FDocDialogueLineInfo&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocDialogueChoicesNative, FDocRequestHandle, const FDocDialogueChoiceList&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocDialogueStateNative, FDocRequestHandle, EDocDialogueSessionState);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocDialogueEndedNative, FDocRequestHandle, EDocDialogueSessionState, const FDocSystemResult&);

/**
 * Dialogue sessions for one world (UDialogueSubsystem, handoff Section 6). Headless:
 * no NPC class, camera, animation, Sequence or widget dependency. Presentation adapters
 * observe snapshots/delegates and request their own leases; they never decide outcomes.
 *
 * Every mutating call takes the session handle and the expected session revision (where
 * a stale view could act) and returns a typed FDocSystemResult. Events are queued and
 * delivered after bookkeeping, so handlers may call back into the subsystem.
 */
UCLASS()
class DOCDIALOGUERUNTIME_API UDocDialogueSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocDialogueSubsystem* Get(const UObject* WorldContextObject);

	// ---- Session lifecycle ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocRequestHandle StartDialogue(const FDocDialogueStartRequest& Request, FDocSystemResult& OutResult);

	/** Start from the (graph, owner) memory's checkpoint. Committed actions are never replayed. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocRequestHandle ResumeFromCheckpoint(const FDocDialogueStartRequest& Request, EDocDialogueRestorePolicy Policy, FDocSystemResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult Advance(FDocRequestHandle Session, int64 ExpectedRevision, UObject* Requester);

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult SelectChoice(FDocRequestHandle Session, FName ChoiceId, int64 ExpectedRevision, UObject* Requester);

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult Pause(FDocRequestHandle Session, UObject* Requester);

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult Resume(FDocRequestHandle Session, UObject* Requester);

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult Cancel(FDocRequestHandle Session, UObject* Requester);

	/** Explicit normal end (Completed). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult End(FDocRequestHandle Session, UObject* Requester);

	/** Presentation acknowledgement for PresentationAck lines (text reveal finished). Not an Advance. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult AcknowledgeLinePresented(FDocRequestHandle Session, int64 LineOrdinal);

	/** Completion of an action a provider returned as Pending. */
	FDocSystemResult CompletePendingAction(FDocRequestHandle Session, FName ActionId, const FDocSystemResult& Result);

	// ---- Queries ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	bool GetSessionSnapshot(FDocRequestHandle Session, FDocDialogueSessionSnapshot& OutSnapshot) const;

	/** Fresh evaluation for the current revision. Hidden choices are omitted. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult GetAvailableChoices(FDocRequestHandle Session, UObject* Requester, FDocDialogueChoiceList& OutChoices) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	bool GetVariable(FDocRequestHandle Session, FName Name, FDocDialogueValue& OutValue) const;

	/** Authorized write of a declared variable (coerced). Bumps the session revision. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	FDocSystemResult SetVariable(FDocRequestHandle Session, FName Name, const FDocDialogueValue& Value, UObject* Requester);

	UFUNCTION(BlueprintPure, Category = "Doc|Dialogue")
	int32 GetActiveSessionCount() const { return Sessions.Num(); }

	/** Number of sessions holding ParticipantId (exclusive or shared). */
	UFUNCTION(BlueprintPure, Category = "Doc|Dialogue")
	int32 GetReservationCount(FName ParticipantId) const;

	TArray<FDocDialogueActionRecord> GetActionLog(FDocRequestHandle Session) const;

	// ---- Participants ----
	void RegisterParticipant(UObject* Participant);
	void UnregisterParticipant(UObject* Participant, FGameplayTag Reason);
	/** Host-reported loss (range, travel, takeover...). Applies each binding's LossPolicy. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Dialogue")
	void NotifyParticipantLost(FName ParticipantId, FGameplayTag Reason);

	// ---- Providers ----
	void RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocDialogueConditionProvider> Provider);
	void RegisterActionProvider(FName ProviderId, TSharedPtr<IDocDialogueActionProvider> Provider);
	void RegisterCustomNodeHandler(FName CustomType, TSharedPtr<IDocDialogueCustomNodeHandler> Handler);
	/** Voice duration in seconds, or < 0 if unknown. Default: an already-loaded USoundBase's duration (never loads). */
	void SetVoiceDurationResolver(TFunction<float(const TSoftObjectPtr<USoundBase>&)> Resolver);

	// ---- Persistence ----
	FDocDialogueSaveData CaptureState() const;
	/** Validates everything before applying. Rejected (Conflict) while sessions are active. */
	FDocSystemResult RestoreState(const FDocDialogueSaveData& Data);
	bool GetMemory(FName GraphId, const FDocOwnerScope& Owner, FDocDialogueMemory& OutMemory) const;
	/**
	 * Host resolution of an action that was in flight when the checkpoint was saved.
	 * bWasCommitted = true records a receipt (the resumed transition skips it); false
	 * clears the in-flight marker so the resumed transition executes it.
	 */
	FDocSystemResult ResolveCheckpointAction(UDocDialogueGraph* Graph, const FDocOwnerScope& Owner, bool bWasCommitted);

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Dialogue") FDocDialogueLineEvent OnLineShown;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Dialogue") FDocDialogueLineEvent OnLineCompleted;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Dialogue") FDocDialogueChoicesEvent OnChoicesPresented;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Dialogue") FDocDialogueStateEvent OnSessionStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Dialogue") FDocDialogueEndedEvent OnSessionEnded;
	FDocDialogueLineNative OnLineShownNative;
	FDocDialogueLineNative OnLineCompletedNative;
	FDocDialogueChoicesNative OnChoicesPresentedNative;
	FDocDialogueStateNative OnSessionStateChangedNative;
	FDocDialogueEndedNative OnSessionEndedNative;

	// ---- Tests ----
	/** Replaces every clock domain with a manual time (tests). */
	void SetTimeForTesting(double Seconds);
	/** Advance the manual test time and tick once. */
	void AdvanceTimeForTesting(double Seconds);

	//~ USubsystem / FTickableGameObject
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	struct FBinding
	{
		FName RoleId;
		FName ParticipantId;
		TWeakObjectPtr<UObject> Participant;
		bool bExclusive = true;
		bool bLost = false;
		EDocDialogueLossPolicy LossPolicy = EDocDialogueLossPolicy::Cancel;
		float PauseTimeoutSeconds = 30.f;
	};

	struct FPendingTransition
	{
		bool bActive = false;
		FName FromNode;
		FName ChoiceId;
		FName Destination;
		TArray<FDocDialogueAction> Actions;
		int32 NextAction = 0;
		int64 Ordinal = 0;
		TMap<FName, FDocDialogueValue> StagedVariables;
		FName InFlightAction;
		TArray<FDocEffectKey> CommittedKeys;
		TOptional<FDocSystemResult> DeferredCompletion;
	};

	struct FCommittedAction
	{
		FDocEffectKey Key;
		FDocDialogueAction Action;
		FName NodeId;
		FName ChoiceId;
	};

	struct FMemoryKey
	{
		FName GraphId;
		FDocOwnerScope Owner;
		friend bool operator==(const FMemoryKey& A, const FMemoryKey& B) { return A.GraphId == B.GraphId && A.Owner == B.Owner; }
		friend uint32 GetTypeHash(const FMemoryKey& K) { return HashCombine(GetTypeHash(K.GraphId), GetTypeHash(K.Owner)); }
	};

	struct FMemoryState
	{
		FDocDialogueMemory Data; // Receipts live in Ledger while loaded
		FDocReceiptLedger Ledger;
		TSet<FName> Visited;
		TSet<FName> OnceTaken;
		explicit FMemoryState(int32 MaxReceipts) : Ledger(MaxReceipts) {}
	};

	struct FSession
	{
		FDocRequestHandle Handle;
		FGuid SessionId;
		TObjectPtr<UDocDialogueGraph> Graph = nullptr;
		TMap<FName, int32> NodeIndex;
		FMemoryKey MemoryKey;
		TWeakObjectPtr<UObject> Initiator;
		TArray<TWeakObjectPtr<UObject>> Audience;
		EDocDialogueAuthorityMode Authority = EDocDialogueAuthorityMode::OwnerAuthoritative;
		FGameplayTagContainer ContextTags;
		TArray<FBinding> Bindings;

		EDocDialogueSessionState State = EDocDialogueSessionState::PendingBindings;
		EDocDialogueSessionState ResumeState = EDocDialogueSessionState::None;
		int64 Revision = 1;
		FName NodeId;
		int64 TransitionOrdinal = 0;
		int64 LineOrdinal = 0;
		TMap<FName, FDocDialogueValue> Variables;

		// Timers (only sessions with a deadline are checked each tick).
		double Deadline = -1.0;
		EDocClockDomain DeadlineClock = EDocClockDomain::WorldGameplay;
		double RemainingOnPause = -1.0;
		double LineReadyAt = -1.0;      // non-skippable auto lines: earliest manual advance
		bool bAwaitingAck = false;
		float AckHoldSeconds = 0.f;
		double PauseDeadline = -1.0;
		bool bPausedForRebind = false;

		int32 NonYieldingSteps = 0;
		bool bContinuePending = false;
		bool bTerminal = false;

		FPendingTransition Pending;
		TArray<FDocDialogueActionRecord> ActionLog;
		TArray<FCommittedAction> Committed;
		FDocDialogueLineInfo CurrentLine;
		FDocSystemResult FinalResult;
		FString Diagnostic;
	};

	struct FReservation
	{
		TArray<FGuid> Sessions;
		bool bExclusive = false;
	};

	enum class EPendingProgress : uint8 { Committed, Waiting, Rejected };

	// Core helpers
	bool HasAuthority() const;
	double Now(EDocClockDomain Domain) const;
	const UDocDialogueSettings* Settings() const;
	FSession* FindSession(const FDocRequestHandle& Handle, FDocSystemResult* OutFailure = nullptr);
	const FSession* FindSession(const FDocRequestHandle& Handle, FDocSystemResult* OutFailure = nullptr) const;
	FDocSystemResult Authorize(const FSession& S, const UObject* Requester, bool bMutation) const;
	FMemoryState& GetOrCreateMemory(const FMemoryKey& Key, int32 GraphVersion);
	FMemoryState* FindMemory(const FMemoryKey& Key);
	const FMemoryState* FindMemory(const FMemoryKey& Key) const;
	const FDocDialogueNode* FindNode(const FSession& S, FName NodeId) const;

	// Start helpers
	FDocSystemResult PrepareSession(const FDocDialogueStartRequest& Request, FSession& OutSession);
	FDocSystemResult AcquireReservations(FSession& S);
	void ReleaseReservations(FSession& S);
	void InitVariables(FSession& S, const TArray<FDocDialogueVariableEntry>* Overrides);
	FDocRequestHandle AddSession(TUniquePtr<FSession> Session);

	// Execution
	void Run(FSession& S);
	void GoTo(FSession& S, FName Destination);
	void EnterLine(FSession& S, const FDocDialogueNode& Node);
	void CompleteLine(FSession& S);
	void EnterChoice(FSession& S, const FDocDialogueNode& Node);
	void EnterWaiting(FSession& S, EDocDialogueSessionState State);
	float ResolveLineDuration(const FDocDialogueNode& Node, bool& bOutVoiceMissing) const;
	float EstimateTextSeconds(const FText& Text) const;
	void Terminate(FSession& S, EDocDialogueSessionState Final, const FDocSystemResult& Result);
	void Fail(FSession& S, EDocResultOutcome Outcome, const FGameplayTag& Tag, const FString& Diagnostic);
	void PauseInternal(FSession& S, float TimeoutSeconds, bool bForRebind);
	void ResumeInternal(FSession& S);
	void OnTimer(FSession& S);
	void PresentChoices(FSession& S);
	void HandleLoss(FName ParticipantId, const UObject* OnlyObject, FGameplayTag Reason);

	// Conditions
	FDocConditionResult EvaluateCondition(const FSession& S, const FDocDialogueCondition& C, const TMap<FName, FDocDialogueValue>& Vars) const;
	FDocConditionResult EvaluateAll(const FSession& S, const TArray<FDocDialogueCondition>& Conditions) const;
	FDocDialogueChoiceView EvaluateChoice(const FSession& S, const FDocDialogueChoice& Choice) const;
	FDocDialogueChoiceList BuildChoiceList(const FSession& S) const;
	bool GetRoleTags(const FSession& S, FName Role, FGameplayTagContainer& OutTags) const;
	UObject* GetRoleObject(const FSession& S, FName Role) const;
	TMap<FName, TWeakObjectPtr<UObject>> GetParticipantMap(const FSession& S) const;

	// Actions / transitions
	FDocSystemResult BeginTransition(FSession& S, FName FromNode, FName ChoiceId, FName Destination, const TArray<FDocDialogueAction>& Actions, bool bRunAfter);
	EPendingProgress RunPendingActions(FSession& S, FDocSystemResult& OutResult);
	void ApplyCompletion(FSession& S, const FDocSystemResult& Result);
	void CommitTransition(FSession& S);
	void RejectTransition(FSession& S, const FDocSystemResult& Why);
	FDocDialogueActionOutcome ExecuteAction(FSession& S, const FDocDialogueAction& Action, TMap<FName, FDocDialogueValue>& Staged, int64 Ordinal, FName NodeId, FName ChoiceId);
	FDocEffectKey MakeEffectKey(const FSession& S, int64 Ordinal, FName ActionId);
	static int64 HashAction(const FDocDialogueAction& Action);
	void RecordAction(FSession& S, const FDocEffectKey& Key, EDocDialogueActionState State, const FDocSystemResult& Result);
	IDocDialogueActionProvider* FindActionProvider(const FDocDialogueAction& Action) const;

	// Persistence
	void WriteCheckpoint(FSession& S);
	void ClearCheckpoint(FSession& S);
	void CommitPersistentVariables(FSession& S);

	// Snapshots / events
	FDocDialogueSessionSnapshot MakeSnapshot(const FSession& S) const;
	void SetState(FSession& S, EDocDialogueSessionState NewState);
	void QueueEvent(TFunction<void()> Event);
	void EnterApi() { ++ApiDepth; }
	void LeaveApi();

	struct FApiScope
	{
		UDocDialogueSubsystem& Owner;
		explicit FApiScope(UDocDialogueSubsystem& In) : Owner(In) { Owner.EnterApi(); }
		~FApiScope() { Owner.LeaveApi(); }
	};

	TDocHandleTable<FGuid> SessionHandles;
	TMap<FGuid, TUniquePtr<FSession>> Sessions;
	TMap<FMemoryKey, TUniquePtr<FMemoryState>> Memories;
	TMap<FName, FReservation> Reservations;
	TMap<FName, TWeakObjectPtr<UObject>> RegisteredParticipants;
	TMap<FName, TSharedPtr<IDocDialogueConditionProvider>> ConditionProviders;
	TMap<FName, TSharedPtr<IDocDialogueActionProvider>> ActionProviders;
	TMap<FName, TSharedPtr<IDocDialogueCustomNodeHandler>> CustomNodeHandlers;
	TFunction<float(const TSoftObjectPtr<USoundBase>&)> VoiceDurationResolver;
	TArray<TFunction<void()>> PendingEvents;
	int32 ApiDepth = 0;
	TOptional<double> TestTime;
};

/**
 * Binds an actor as a dialogue participant (UDialogueParticipantComponent). Registers
 * with the world's dialogue subsystem on BeginPlay (enabling RebindByIdentity) and
 * reports loss on EndPlay.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCDIALOGUERUNTIME_API UDocDialogueParticipantComponent : public UActorComponent, public IDocDialogueParticipant
{
	GENERATED_BODY()

public:
	UDocDialogueParticipantComponent();

	/** Stable, saved identity. Unique per world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName ParticipantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TSoftObjectPtr<UTexture2D> Portrait;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FSoftObjectPath VoiceProfile;

	/** Participant tags; the owning actor's tags (IDocGameplayTagProvider / IGameplayTagAssetInterface) are added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bAllowConcurrentConversations = false;

	//~ IDocDialogueParticipant
	virtual FName GetDialogueParticipantId_Implementation() const override { return ParticipantId; }
	virtual FText GetDialogueDisplayName_Implementation() const override { return DisplayName; }
	virtual TSoftObjectPtr<UTexture2D> GetDialoguePortrait_Implementation() const override { return Portrait; }
	virtual FSoftObjectPath GetDialogueVoiceProfile_Implementation() const override { return VoiceProfile; }
	virtual AActor* GetDialogueActor_Implementation() const override { return GetOwner(); }
	virtual void GetDialogueTags_Implementation(FGameplayTagContainer& OutTags) const override;
	virtual bool AllowsConcurrentDialogue_Implementation() const override { return bAllowConcurrentConversations; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
