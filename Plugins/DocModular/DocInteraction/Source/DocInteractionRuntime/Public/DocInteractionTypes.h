#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtr.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocSharedTypes.h"
#include "NativeGameplayTags.h"
#include "DocInteractionTypes.generated.h"

class AActor;
class UTexture2D;
class UDocInteractionCondition;
class UDocInteractionAction;
class UDocInteractableComponent;
class UDocInteractorComponent;
class UDocInteractionSubsystem;

/** Interaction intent tags (handoff 5.2). A tag describes intent; it implements nothing. */
namespace DocInteractionTags
{
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Use);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activate);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Deactivate);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Toggle);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Open);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Close);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pickup);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Drop);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Push);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pull);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rotate);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Read);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inspect);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Enter);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Exit);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Sit);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Talk);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Purchase);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hold);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Repair);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unlock);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Custom);

	// Failure reasons
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_OutOfRange);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_NotFacing);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_MissingTags);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_BlockedTags);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Cooldown);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_AlreadyUsed);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Reserved);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Disabled);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_TargetLost);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_NoAuthority);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_MissingInterface);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_NoLineOfSight);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_ActionFailed);

	// Receiver notification phases
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Started);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Completed);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Cancelled);
	DOCINTERACTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Repeated);
}

UENUM(BlueprintType)
enum class EDocInteractionMode : uint8
{
	/** Validate, execute actions once, complete. */
	Instant,
	/** Progress over HoldDuration (gameplay clock); actions execute at commit if still valid. */
	HoldToComplete,
	/** Actions execute at start; session stays active until CompleteInteraction, cancellation, or MaxDuration (→ Completed). */
	Continuous,
	/** Actions execute every RepeatInterval (first at start) until MaxRepeats, CompleteInteraction, cancellation, or MaxDuration. */
	Repeated
};

UENUM(BlueprintType)
enum class EDocInteractionConcurrency : uint8
{
	/** One active session per target component (any definition). */
	ExclusiveTarget,
	/** One active session per (target, definition). */
	ExclusiveDefinition,
	/** Any number of sessions on the target. Still one session per interactor. */
	Shared
};

UENUM(BlueprintType)
enum class EDocInteractionSessionState : uint8
{
	Running,
	Completed,
	Rejected,
	Cancelled,
	Failed
};

/** Explicit context passed to conditions and actions (handoff 5.3). Weak references only. */
USTRUCT(BlueprintType)
struct DOCINTERACTIONRUNTIME_API FDocInteractionContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<UWorld> World;
	/** Actor performing the interaction (any actor; not required to be a Character). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<AActor> Interactor;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<UDocInteractorComponent> InteractorComponent;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<AActor> Target;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<UDocInteractableComponent> Interactable;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FName DefinitionId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FGameplayTag InteractionTag;
	/** Session handle; unset during queries. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FDocRequestHandle Session;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<UDocInteractionSubsystem> Subsystem;
	/** Repetition index for Repeated mode (0 for the first execution). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") int32 Repetition = 0;
};

/**
 * Immutable interaction definition (handoff 5.2). Conditions and actions are
 * instanced sub-objects owned by the authoring component/profile and must not be
 * mutated at runtime.
 */
USTRUCT(BlueprintType)
struct DOCINTERACTIONRUNTIME_API FDocInteractionDefinition
{
	GENERATED_BODY()

	/** Stable ID unique within the interactable. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition")
	FName DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (Categories = "Interaction"))
	FGameplayTag InteractionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText Prompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Free-form input hint for the project's prompt UI (e.g. an input action name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText InputHint;

	/** Higher priority definitions sort first when several are available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	EDocInteractionMode Mode = EDocInteractionMode::Instant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0.0", EditCondition = "Mode == EDocInteractionMode::HoldToComplete"))
	float HoldDuration = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0.01", EditCondition = "Mode == EDocInteractionMode::Repeated"))
	float RepeatInterval = 0.5f;

	/** Repeated mode: 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0", EditCondition = "Mode == EDocInteractionMode::Repeated"))
	int32 MaxRepeats = 0;

	/** Continuous/Repeated/Hold: 0 = no maximum. Reaching it completes Continuous/Repeated and cancels Hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0.0"))
	float MaxDuration = 0.f;

	/** How often a running session re-evaluates conditions and target validity (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0.0"))
	float RevalidateInterval = 0.2f;

	/** Maximum interactor-to-target distance, checked at request, while running, and before commit. 0 = unchecked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	EDocInteractionConcurrency Concurrency = EDocInteractionConcurrency::ExclusiveTarget;

	/** Only the authority may execute this interaction (actions change replicated state). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	bool bRequiresAuthority = false;

	/** On action failure, call Compensate() on already-executed reversible actions (reverse order). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	bool bCompensateOnFailure = true;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Rules")
	TArray<TObjectPtr<UDocInteractionCondition>> Conditions;

	/** Executed in order, fail-fast. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Rules")
	TArray<TObjectPtr<UDocInteractionAction>> Actions;
};

/** A detected candidate (handoff 5.2). */
USTRUCT(BlueprintType)
struct DOCINTERACTIONRUNTIME_API FDocInteractionCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<UDocInteractableComponent> Interactable;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<AActor> Actor;
	/** Name of the provider that found it (first provider when several did). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FName Provider;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") float Distance = 0.f;
	/** Provider score (higher is better). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") float Score = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") int32 Priority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") bool bEligible = true;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FGameplayTag RejectReason;
};

/** View-model row for one interaction option on a target (handoff 5.5). */
USTRUCT(BlueprintType)
struct DOCINTERACTIONRUNTIME_API FDocInteractionOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FName DefinitionId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FGameplayTag InteractionTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FText Prompt;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FText InputHint;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") EDocInteractionMode Mode = EDocInteractionMode::Instant;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FGameplayTag FailureTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FText FailureReason;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FString Diagnostic;
};

/** Observable state of a session. */
USTRUCT(BlueprintType)
struct DOCINTERACTIONRUNTIME_API FDocInteractionSessionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") EDocInteractionSessionState State = EDocInteractionSessionState::Rejected;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") EDocInteractionMode Mode = EDocInteractionMode::Instant;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FName DefinitionId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<AActor> Interactor;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TWeakObjectPtr<AActor> Target;
	/** 0..1 for HoldToComplete; also for Repeated with MaxRepeats. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") float Progress = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") bool bProgressKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") float ElapsedSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") int32 Repetitions = 0;
	/** Actions that executed successfully (for partial-failure reporting). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TArray<FName> ExecutedActions;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") TArray<FName> CompensatedActions;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Interaction") FDocSystemResult Result;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocInteractionSessionEvent, const FDocInteractionSessionInfo&, Session);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocInteractionSessionNativeEvent, const FDocInteractionSessionInfo& /*Session*/);
