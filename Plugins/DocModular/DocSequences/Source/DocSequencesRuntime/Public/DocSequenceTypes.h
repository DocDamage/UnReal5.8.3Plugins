#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/Interface.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocSequenceTypes.generated.h"

class ULevelSequence;
class ULocalPlayer;
class UDocSequenceDefinition;

namespace DocSequenceTags
{
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Sequence);
	/** Capability: bound participants' AI is controlled through IDocSequenceParticipant. */
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Control_AI);
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event);
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_DoorOpen);
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Explosion);
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_GameplayResume);
	DOCSEQUENCESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Role);
}

/** Session lifecycle (handoff 11.2). */
UENUM(BlueprintType)
enum class EDocSequenceState : uint8
{
	None,
	Requested,
	Queued,
	Loading,
	ResolvingBindings,
	Preparing,
	Playing,
	Paused,
	Completed,
	Skipped,
	Interrupted,
	Failed,
	Restoring,
	Finished
};

/** What a new request does when another session is active (handoff 11.2). */
UENUM(BlueprintType)
enum class EDocSequenceArbitration : uint8
{
	/** Refuse while anything plays. */
	Reject,
	/** Wait in the bounded queue (priority, then FIFO). */
	Queue,
	/** Interrupt the active session if it is Interruptible; otherwise queue. */
	Interrupt,
	/** Interrupt only a lower-priority, interruptible session; otherwise queue. */
	ReplaceLowerPriority
};

UENUM(BlueprintType)
enum class EDocSequenceSkipPolicy : uint8
{
	/** Jump to the end state; effects marked CommitOnSkip are committed. */
	FinishAtEndState,
	/** Stop and restore pre-sequence state; no remaining effect is committed. */
	CancelAndRestore,
	/** Stop, then let OnReconcileSkip decide; effects marked CommitOnSkip are committed unless the handler vetoes. */
	Reconcile
};

/** Participant missing or destroyed (handoff 11.3). */
UENUM(BlueprintType)
enum class EDocMissingParticipantPolicy : uint8
{
	Fail,
	/** Wait (paused if already playing) until the role can be bound again, up to the role's timeout. */
	WaitWithTimeout,
	/** Continue without the participant (optional roles). */
	MissingOptional
};

UENUM(BlueprintType)
enum class EDocSequenceEffectClass : uint8
{
	/** Cosmetic; runs on every machine. */
	PresentationOnly,
	/** Gameplay mutation; runs only with authority. */
	Authoritative
};

USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequenceRole
{
	GENERATED_BODY()

	/** Role tag (Sequence.Role.*); participants advertise the roles they can fill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role", meta = (Categories = "Sequence.Role"))
	FGameplayTag Role;

	/** Binding tag in the Level Sequence (Sequencer "binding tags"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role")
	FName BindingTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role")
	EDocMissingParticipantPolicy MissingPolicy = EDocMissingParticipantPolicy::Fail;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role", meta = (ClampMin = "0.0"))
	float WaitTimeoutSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Role", meta = (ClampMin = "1"))
	int32 MaxActors = 1;
};

/** A gameplay effect mapped to the timeline or to a named sequencer event (handoff 11.3). */
USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequenceEffect
{
	GENERATED_BODY()

	/** Stable within the definition; part of the idempotency key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FName EffectId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (Categories = "Sequence.Event"))
	FGameplayTag EventTag;

	/** Fire when forward playback crosses this time (seconds). Negative = only via NotifySequenceEvent(EventTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	float TriggerTimeSeconds = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	EDocSequenceEffectClass Class = EDocSequenceEffectClass::PresentationOnly;

	/** Applied at most once per sequence per world/campaign (not per session): replay and late join never re-apply it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	bool bIrreversible = false;

	/** Commit this effect when the session is skipped before it fired (a required milestone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	bool bCommitOnSkip = false;

	/** Commit when JumpToMarker/seek moves past it. Default: seeking does not execute crossed effects. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	bool bCommitWhenSeekedPast = false;
};

/** Logical sequence (handoff 11.1). */
UCLASS(BlueprintType)
class DOCSEQUENCESRUNTIME_API UDocSequenceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence", meta = (Categories = "Sequence"))
	FGameplayTag SequenceTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	TSoftObjectPtr<ULevelSequence> Sequence;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arbitration")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arbitration")
	EDocSequenceArbitration Arbitration = EDocSequenceArbitration::Queue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arbitration")
	bool bInterruptible = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	bool bCanSkip = true;

	/** False: a second play is rejected when this world (or the history provider) records a completed play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	bool bCanReplay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocSequenceSkipPolicy SkipPolicy = EDocSequenceSkipPolicy::FinishAtEndState;

	/** Restore animated state when the session ends (Sequencer completion mode). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	bool bRestoreStateOnFinish = false;

	/** Control.Camera / Control.Input.* go through each player's control provider; Sequence.Control.AI through participants. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	FGameplayTagContainer RequestedControl;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	int32 ControlPriority = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	TArray<FDocSequenceRole> Roles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FDocSequenceEffect> Effects;

	/** Named prerequisites resolved by registered prerequisite providers (e.g. the Streaming bridge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
	FGameplayTagContainer Prerequisites;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites", meta = (ClampMin = "0.0"))
	float PrerequisiteTimeoutSeconds = 30.f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequenceBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Sequence")
	FGameplayTag Role;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Sequence")
	TArray<TObjectPtr<AActor>> Actors;
};

USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequencePlayParams
{
	GENERATED_BODY()

	/** Request owner; a queued request whose owner dies is removed. Null = the subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Sequence")
	TObjectPtr<UObject> Owner;

	/** Explicit bindings; take precedence over registered participants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Sequence")
	TArray<FDocSequenceBinding> Bindings;

	/** Players whose camera/input are claimed. Empty = every local player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Sequence")
	TArray<TObjectPtr<ULocalPlayer>> Players;
};

USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequenceSessionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") FDocRequestHandle Session;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") FGameplayTag SequenceTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") EDocSequenceState State = EDocSequenceState::None;
	/** Completed / Skipped / Interrupted / Failed once ended. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") EDocSequenceState Terminal = EDocSequenceState::None;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") FDocSystemResult Result;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") float PositionSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") float DurationSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") int32 QueuePosition = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") TArray<FName> FiredEffects;
};

USTRUCT(BlueprintType)
struct DOCSEQUENCESRUNTIME_API FDocSequenceRequestInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") FDocRequestHandle Session;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") FDocSystemResult Result;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Sequence") EDocSequenceState State = EDocSequenceState::None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocSequenceStateEvent, FDocRequestHandle, Session, FGameplayTag, SequenceTag, EDocSequenceState, State);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocSequenceStateNative, FDocRequestHandle, FGameplayTag, EDocSequenceState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocSequenceFinishedEvent, FDocRequestHandle, Session, EDocSequenceState, Terminal, const FDocSystemResult&, Result);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocSequenceFinishedNative, FDocRequestHandle, EDocSequenceState, const FDocSystemResult&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDocSequenceEffectEvent, FDocRequestHandle, Session, FName, EffectId, FGameplayTag, EventTag, bool, bAuthoritative);
DECLARE_MULTICAST_DELEGATE_FourParams(FDocSequenceEffectNative, FDocRequestHandle, FName, FGameplayTag, bool);

/** Participant contract (actor or component). Hosts implement AI/animation hand-over in an owned, composable way. */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocSequenceParticipant : public UInterface
{
	GENERATED_BODY()
};

class DOCSEQUENCESRUNTIME_API IDocSequenceParticipant
{
	GENERATED_BODY()

public:
	/** Roles this participant can fill. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Sequence")
	FGameplayTagContainer GetDocSequenceRoles() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Sequence")
	void OnDocSequenceBound(FDocRequestHandle Session, FGameplayTag Role);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Sequence")
	void OnDocSequenceUnbound(FDocRequestHandle Session, FGameplayTag Role, EDocSequenceState Terminal);

	/** Suspend (true) or return (false) this participant's own AI for the session. Only called in pairs. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Sequence")
	void SetDocSequenceAIControlled(FDocRequestHandle Session, bool bControlled);
};

/** Generic prerequisite provider (bridges: streaming residency etc.). */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocSequencePrerequisiteProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCSEQUENCESRUNTIME_API IDocSequencePrerequisiteProvider
{
	GENERATED_BODY()

public:
	/** Can this provider satisfy the prerequisite tag? */
	virtual bool HandlesDocSequencePrerequisite(const FGameplayTag& Prerequisite) const = 0;
	/** Begin acquiring; call OnReady(true/false) exactly once (possibly synchronously). Hold resources until Release. */
	virtual void AcquireDocSequencePrerequisite(const FGameplayTag& Prerequisite, int64 SessionId, TFunction<void(bool /*bReady*/)> OnReady) = 0;
	/** Release everything acquired for the session (every terminal path). Idempotent. */
	virtual void ReleaseDocSequencePrerequisites(int64 SessionId) = 0;
};

/** Optional persistent history for replay restrictions (supplied by a save bridge or host). */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocSequenceHistoryProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCSEQUENCESRUNTIME_API IDocSequenceHistoryProvider
{
	GENERATED_BODY()

public:
	virtual bool HasDocSequenceCompleted(const FGameplayTag& SequenceTag) const = 0;
	virtual void RecordDocSequenceCompleted(const FGameplayTag& SequenceTag) = 0;
};

/** Project Settings → Plugins → Doc Sequences. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Sequences"))
class DOCSEQUENCESRUNTIME_API UDocSequencesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "Queue", meta = (ClampMin = "0"))
	int32 MaxQueueLength = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Loading", meta = (ClampMin = "0.0"))
	float LoadTimeoutSeconds = 15.f;

	/** Fail a session that requests camera/input control when a player has no control provider (default: continue and report). */
	UPROPERTY(Config, EditAnywhere, Category = "Control")
	bool bRequireControlProvider = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "1"))
	int32 FinishedSessionHistory = 32;
};
