#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "Engine/StreamableManager.h"
#include "DocEffectKey.h"
#include "DocSequenceTypes.h"
#include "DocSequencePlaybackBackend.h"
#include "DocSequenceSubsystem.generated.h"

class UDocSequenceParticipantComponent;

/**
 * Logical sequence orchestration for one world (handoff Section 11).
 *
 * One active session at a time plus a bounded priority queue. A session id is
 * distinct from the sequence tag; replay creates a new session. Every session ends
 * exactly once with Completed / Skipped / Interrupted / Failed, then Restoring
 * (release owned control claims, AI hand-back, prerequisites, playback) → Finished.
 *
 * Gameplay effects are an explicit mapping (definition Effects): fired when forward
 * playback crosses their time or when NotifySequenceEvent is called for their tag.
 * Seeking never executes crossed effects unless an effect opts in. Irreversible
 * effects are keyed by (world, sequence, effect) in a receipt ledger so skip, replay,
 * seek and late join cannot apply them twice.
 */
UCLASS()
class DOCSEQUENCESRUNTIME_API UDocSequenceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocSequenceSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSequenceRequestInfo PlaySequence(UDocSequenceDefinition* Definition, const FDocSequencePlayParams& Params);

	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult PauseSequence(FDocRequestHandle Session);

	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult ResumeSequence(FDocRequestHandle Session);

	/** Ends the session as Interrupted (queued sessions are cancelled). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult StopSequence(FDocRequestHandle Session);

	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult SkipSequence(FDocRequestHandle Session);

	/** Ends the session (Interrupted) and plays the same definition as a new session. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSequenceRequestInfo RestartSequence(FDocRequestHandle Session);

	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult JumpToMarker(FDocRequestHandle Session, const FString& MarkerLabel);

	UFUNCTION(BlueprintPure, Category = "Doc|Sequence")
	bool IsSequencePlaying(FGameplayTag SequenceTag) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Sequence")
	EDocSequenceState GetSessionState(FDocRequestHandle Session) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Sequence")
	bool GetSessionInfo(FDocRequestHandle Session, FDocSequenceSessionInfo& OutInfo) const;

	/** Active session, if any. */
	UFUNCTION(BlueprintPure, Category = "Doc|Sequence")
	FDocRequestHandle GetActiveSession() const;

	/** Sequencer event tracks / director blueprints call this with a mapped Sequence.Event.* tag. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Sequence")
	FDocSystemResult NotifySequenceEvent(FGameplayTag EventTag);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Sequence") FDocSequenceStateEvent OnSessionStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Sequence") FDocSequenceFinishedEvent OnSessionFinished;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Sequence") FDocSequenceEffectEvent OnSequenceEffect;
	FDocSequenceStateNative OnSessionStateChangedNative;
	FDocSequenceFinishedNative OnSessionFinishedNative;
	FDocSequenceEffectNative OnSequenceEffectNative;

	/** Reconcile skip policy: return the effect ids to commit (default: those marked bCommitOnSkip). */
	TFunction<TArray<FName>(const UDocSequenceDefinition* /*Definition*/, const TArray<FName>& /*NotYetFired*/)> OnReconcileSkip;

	// ---- Providers ----
	void RegisterPrerequisiteProvider(UObject* Provider);
	void UnregisterPrerequisiteProvider(UObject* Provider);
	void SetHistoryProvider(UObject* Provider);

	void RegisterParticipant(UObject* Participant);
	void UnregisterParticipant(UObject* Participant);

	// ---- Effect receipts (persist through a save bridge) ----
	TArray<FDocEffectReceipt> GetEffectReceipts() const { return Ledger.GetAll(); }
	void RestoreEffectReceipts(const TArray<FDocEffectReceipt>& Receipts) { Ledger.RestoreAll(Receipts); }

	// ---- Tests / bridges ----
	void SetPlaybackBackend(TSharedPtr<IDocSequencePlaybackBackend> InBackend) { Backend = InBackend; }
	using FLoader = TFunction<void(const FSoftObjectPath& /*Path*/, TFunction<void()> /*OnComplete*/)>;
	void SetLoaderForTesting(FLoader InLoader) { LoaderOverride = MoveTemp(InLoader); }
	void AdvanceForTesting(float DeltaSeconds) { TickSessions(DeltaSeconds); }
	int32 GetQueueLength() const { return Queue.Num(); }

	//~ USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { TickSessions(DeltaTime); }
	virtual TStatId GetStatId() const override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	struct FBoundRole
	{
		FDocSequenceRole Role;
		TArray<TWeakObjectPtr<AActor>> Actors;
		TArray<TWeakObjectPtr<UObject>> Participants;
	};

	struct FSession
	{
		int64 Id = 0;
		FGuid Guid;
		TObjectPtr<UDocSequenceDefinition> Definition;
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerIsSubsystem = false;
		/** Copied as weak references: a session never keeps actors or players alive. */
		TArray<TPair<FGameplayTag, TArray<TWeakObjectPtr<AActor>>>> SuppliedBindings;
		TArray<TWeakObjectPtr<ULocalPlayer>> Players;
		/** Participants that received OnDocSequenceBound (and must receive Unbound). */
		TArray<TPair<TWeakObjectPtr<UObject>, FGameplayTag>> Hooked;
		EDocSequenceState State = EDocSequenceState::None;
		EDocSequenceState Terminal = EDocSequenceState::None;
		FDocSystemResult Result;
		int64 QueueOrder = 0;
		double StateEnteredAt = 0.0;
		double WaitDeadline = 0.0;
		bool bStarted = false;
		bool bPausedByUser = false;
		bool bPausedForParticipant = false;
		int32 PendingPrerequisites = 0;
		bool bPrerequisiteFailed = false;
		TArray<FBoundRole> Roles;
		TArray<TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>> ControlClaims;
		TArray<TWeakObjectPtr<UObject>> AIControlled;
		TArray<TWeakObjectPtr<UObject>> PrerequisiteProviders;
		TSet<FName> FiredEffects;
		float LastPosition = 0.f;
		TSharedPtr<FStreamableHandle> LoadHandle;
		TObjectPtr<ULevelSequence> LoadedSequence;
	};

	FDocRequestHandle MakeHandle(int64 Id) const { return FDocHandleAllocator::MakeHandle(Id, Epoch); }
	FSession* FindSession(const FDocRequestHandle& Handle);
	const FSession* FindSession(const FDocRequestHandle& Handle) const;
	void SetState(FSession& Session, EDocSequenceState NewState);
	void StartSession(TUniquePtr<FSession> Session);
	void BeginLoad(FSession& Session);
	void OnLoaded(int64 SessionId);
	void BeginPrerequisites(FSession& Session);
	void ContinueAfterPrerequisites(FSession& Session);
	/** Returns false when a required role cannot be bound (policy decides). Updates Roles. */
	bool ResolveBindings(FSession& Session, FString& OutMissing);
	TMap<FName, TArray<AActor*>> BuildBindingMap(const FSession& Session) const;
	void BeginPlayback(FSession& Session);
	void HookParticipants(FSession& Session);
	FDocSequencePlayParams RebuildParams(const FSession& Session) const;
	void AcquireControl(FSession& Session, FString& OutProblem);
	void EndActive(EDocSequenceState Terminal, const FDocSystemResult& Result, bool bRestoreState);
	void RestoreAndFinish(FSession& Session, bool bRestoreState);
	void CancelQueued(int32 Index, const FDocSystemResult& Result);
	void PumpQueue();
	void FireEffect(FSession& Session, const FDocSequenceEffect& Effect);
	void FireCrossedEffects(FSession& Session, float From, float To);
	void CommitRemainingOnSkip(FSession& Session);
	bool CheckParticipants(FSession& Session);
	void TickSessions(float DeltaSeconds);
	void RecordFinished(const FSession& Session);
	FDocSequenceSessionInfo MakeInfo(const FSession& Session) const;
	FDocEffectKey MakeEffectKey(const FSession& Session, const FDocSequenceEffect& Effect) const;
	bool HasAuthority() const;
	void FlushEvents();

	TUniquePtr<FSession> Active;
	TArray<TUniquePtr<FSession>> Queue;
	TArray<FDocSequenceSessionInfo> FinishedHistory;
	TSet<FGameplayTag> CompletedThisWorld;
	TArray<TWeakObjectPtr<UObject>> PrerequisiteProviders;
	TWeakObjectPtr<UObject> HistoryProvider;
	TArray<TWeakObjectPtr<UObject>> Participants;
	TSharedPtr<IDocSequencePlaybackBackend> Backend;
	FStreamableManager Streamable;
	FLoader LoaderOverride;
	FDocReceiptLedger Ledger;
	int32 Epoch = 0;
	int64 NextQueueOrder = 1;
	double Clock = 0.0;
	bool bDeinitializing = false;
	bool bInEndActive = false;

	struct FQueuedState { FDocRequestHandle Session; FGameplayTag Tag; EDocSequenceState State; };
	struct FQueuedFinish { FDocRequestHandle Session; EDocSequenceState Terminal; FDocSystemResult Result; };
	struct FQueuedEffect { FDocRequestHandle Session; FName EffectId; FGameplayTag EventTag; bool bAuthoritative; };
	TArray<FQueuedState> StateEvents;
	TArray<FQueuedFinish> FinishEvents;
	TArray<FQueuedEffect> EffectEvents;
	bool bFlushing = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocParticipantBoundEvent, FDocRequestHandle, Session, FGameplayTag, Role);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocParticipantAIEvent, FDocRequestHandle, Session, bool, bControlled);

/** Advertises role tags so sequences can bind this actor without label searches. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCSEQUENCESRUNTIME_API UDocSequenceParticipantComponent : public UActorComponent, public IDocSequenceParticipant
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence", meta = (Categories = "Sequence.Role"))
	FGameplayTagContainer Roles;

	UPROPERTY(BlueprintAssignable, Category = "Sequence") FDocParticipantBoundEvent OnBound;
	UPROPERTY(BlueprintAssignable, Category = "Sequence") FDocParticipantBoundEvent OnUnbound;
	/** Hosts suspend/restore their own AI here (owned change, not a global toggle). */
	UPROPERTY(BlueprintAssignable, Category = "Sequence") FDocParticipantAIEvent OnAIControlChanged;

	UFUNCTION(BlueprintPure, Category = "Doc|Sequence")
	bool IsAIControlledBySequence() const { return AIControlCount > 0; }

	virtual FGameplayTagContainer GetDocSequenceRoles_Implementation() const override { return Roles; }
	virtual void OnDocSequenceBound_Implementation(FDocRequestHandle Session, FGameplayTag Role) override { OnBound.Broadcast(Session, Role); }
	virtual void OnDocSequenceUnbound_Implementation(FDocRequestHandle Session, FGameplayTag Role, EDocSequenceState Terminal) override { OnUnbound.Broadcast(Session, Role); }
	virtual void SetDocSequenceAIControlled_Implementation(FDocRequestHandle Session, bool bControlled) override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	int32 AIControlCount = 0;
};
