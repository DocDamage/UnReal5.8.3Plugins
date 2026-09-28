#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocInteractionTypes.h"
#include "DocSharedTypes.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "DocInteractionRules.generated.h"

class USoundBase;

// ---------------------------------------------------------------------------
// Conditions — immutable, side-effect free (handoff 5.3, 3.4).
// ---------------------------------------------------------------------------

/** Base condition. Subclass in C++ or Blueprint and override Evaluate. Must not mutate itself. */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition : public UObject
{
	GENERATED_BODY()

public:
	/** When true, the condition is re-checked while a session runs and immediately before commit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	bool bRevalidateWhileRunning = true;

	UFUNCTION(BlueprintNativeEvent, Category = "Doc|Interaction")
	FDocConditionResult Evaluate(const FDocInteractionContext& Context) const;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const;
};

UENUM(BlueprintType)
enum class EDocInteractionTagSubject : uint8
{
	Interactor,
	Target
};

/** Requires (all/any) tags on the interactor or target via IDocGameplayTagProvider / IGameplayTagAssetInterface. */
UCLASS(DisplayName = "Required Tags")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_RequiredTags : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Interactor;
	UPROPERTY(EditAnywhere, Category = "Condition") FGameplayTagContainer Tags;
	UPROPERTY(EditAnywhere, Category = "Condition") bool bRequireAll = true;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Fails if the subject has any of the tags. */
UCLASS(DisplayName = "Blocked Tags")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_BlockedTags : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Interactor;
	UPROPERTY(EditAnywhere, Category = "Condition") FGameplayTagContainer Tags;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Interactor within [MinDistance, MaxDistance] of the target (cm). */
UCLASS(DisplayName = "Distance")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_Distance : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition", meta = (Units = "cm", ClampMin = "0.0")) float MinDistance = 0.f;
	UPROPERTY(EditAnywhere, Category = "Condition", meta = (Units = "cm", ClampMin = "0.0")) float MaxDistance = 200.f;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Interactor's view (eyes viewpoint) within MaxAngleDegrees of the direction to the target. */
UCLASS(DisplayName = "Facing Angle")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_Facing : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "0.0", ClampMax = "180.0")) float MaxAngleDegrees = 60.f;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

UENUM(BlueprintType)
enum class EDocInteractionUseScope : uint8
{
	/** Shared by all interactors. */
	Global,
	/** Tracked separately per interactor actor. */
	PerInteractor
};

/** Blocks re-use until CooldownSeconds (world time) after the last completion. State lives in the subsystem. */
UCLASS(DisplayName = "Cooldown")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_Cooldown : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "0.0", Units = "s")) float CooldownSeconds = 1.f;
	UPROPERTY(EditAnywhere, Category = "Condition") EDocInteractionUseScope Scope = EDocInteractionUseScope::Global;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Allows only one successful completion (runtime; persistence needs a Save bridge). */
UCLASS(DisplayName = "One Time Use")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_OneTime : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition") EDocInteractionUseScope Scope = EDocInteractionUseScope::Global;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Requires the calling world to have authority (standalone, listen or dedicated server). */
UCLASS(DisplayName = "Authority")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_Authority : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Requires the interactor or target to be of a class or implement an interface. */
UCLASS(DisplayName = "Required Class Or Interface")
class DOCINTERACTIONRUNTIME_API UDocInteractionCondition_RequiredClass : public UDocInteractionCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Condition") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Interactor;
	/** Required class (optional). */
	UPROPERTY(EditAnywhere, Category = "Condition") TSubclassOf<UObject> RequiredClass;
	/** Required interface (optional). */
	UPROPERTY(EditAnywhere, Category = "Condition", meta = (MustImplement = "/Script/CoreUObject.Interface")) TSubclassOf<UInterface> RequiredInterface;
	virtual FDocConditionResult Evaluate_Implementation(const FDocInteractionContext& Context) const override;
};

// ---------------------------------------------------------------------------
// Actions — immutable configuration; execution never stores per-session state on the action.
// ---------------------------------------------------------------------------

/** Base action. Subclass in C++ or Blueprint. Actions are not transactional (handoff 5.4). */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class DOCINTERACTIONRUNTIME_API UDocInteractionAction : public UObject
{
	GENERATED_BODY()

public:
	/** Stable ID reported in ExecutedActions. Defaults to the class name when None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	FName ActionId;

	UFUNCTION(BlueprintNativeEvent, Category = "Doc|Interaction")
	FDocSystemResult Execute(const FDocInteractionContext& Context) const;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const;

	/** True when Compensate() can undo this action's effect. Irreversible actions (spawn/destroy) return false. */
	UFUNCTION(BlueprintNativeEvent, Category = "Doc|Interaction")
	bool IsReversible() const;
	virtual bool IsReversible_Implementation() const { return false; }

	/** Undo this action's effect after a later action failed. Only called when IsReversible(). */
	UFUNCTION(BlueprintNativeEvent, Category = "Doc|Interaction")
	FDocSystemResult Compensate(const FDocInteractionContext& Context) const;
	virtual FDocSystemResult Compensate_Implementation(const FDocInteractionContext& Context) const;

	FName GetEffectiveActionId() const { return ActionId.IsNone() ? GetClass()->GetFName() : ActionId; }
};

/** Owner-attributed tag delta through IDocMutableGameplayTagProvider on the subject. Reversible. */
UCLASS(DisplayName = "Apply Tag Delta")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_TagDelta : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Target;
	UPROPERTY(EditAnywhere, Category = "Action") FGameplayTagContainer TagsToAdd;
	UPROPERTY(EditAnywhere, Category = "Action") FGameplayTagContainer TagsToRemove;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
	virtual bool IsReversible_Implementation() const override { return true; }
	virtual FDocSystemResult Compensate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Calls IDocInteractionReceiver::OnDocInteraction on the target (or interactor) with EventTag. */
UCLASS(DisplayName = "Notify Receiver")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_NotifyReceiver : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Target;
	UPROPERTY(EditAnywhere, Category = "Action") FGameplayTag EventTag;
	/** Fail when the subject does not implement the receiver interface. */
	UPROPERTY(EditAnywhere, Category = "Action") bool bRequireReceiver = true;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
};

/** Enable or disable the target actor (visibility + collision + tick). Reversible. */
UCLASS(DisplayName = "Set Target Enabled")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_SetEnabled : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") bool bEnabled = false;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
	virtual bool IsReversible_Implementation() const override { return true; }
	virtual FDocSystemResult Compensate_Implementation(const FDocInteractionContext& Context) const override;
};

UENUM(BlueprintType)
enum class EDocSpawnOwner : uint8
{
	None,
	Interactor,
	Target
};

/** Spawn an actor relative to the target. Irreversible. Collision failure is a Failed result, never a silent success. */
UCLASS(DisplayName = "Spawn Actor")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_SpawnActor : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") TSoftClassPtr<AActor> ActorClass;
	UPROPERTY(EditAnywhere, Category = "Action") FTransform RelativeTransform;
	UPROPERTY(EditAnywhere, Category = "Action") EDocSpawnOwner Owner = EDocSpawnOwner::Target;
	UPROPERTY(EditAnywhere, Category = "Action") ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
};

/** Destroy the target actor. Irreversible. Requires authority when networked. */
UCLASS(DisplayName = "Destroy Target")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_DestroyTarget : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
};

/** Attach the interactor to the target (e.g. sit/enter), or detach. Reversible. */
UCLASS(DisplayName = "Attach / Detach Interactor")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_Attach : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") bool bAttach = true;
	UPROPERTY(EditAnywhere, Category = "Action") FName SocketName;
	UPROPERTY(EditAnywhere, Category = "Action") bool bSnapToTarget = true;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
	virtual bool IsReversible_Implementation() const override { return true; }
	virtual FDocSystemResult Compensate_Implementation(const FDocInteractionContext& Context) const override;
};

/** Play a sound at the target location. Presentation only; skipped on dedicated servers. */
UCLASS(DisplayName = "Play Sound")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_PlaySound : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") TSoftObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0.0")) float VolumeMultiplier = 1.f;
	/** Missing or unloaded sound: fail the interaction (true) or skip with success (false). */
	UPROPERTY(EditAnywhere, Category = "Action") bool bFailIfMissing = false;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
};

/**
 * Animation through an adapter: sends AnimationTag to IDocInteractionReceiver on
 * the chosen subject, which plays whatever the project uses (montage, sequence...).
 */
UCLASS(DisplayName = "Request Animation (Adapter)")
class DOCINTERACTIONRUNTIME_API UDocInteractionAction_RequestAnimation : public UDocInteractionAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Action") EDocInteractionTagSubject Subject = EDocInteractionTagSubject::Interactor;
	UPROPERTY(EditAnywhere, Category = "Action") FGameplayTag AnimationTag;
	virtual FDocSystemResult Execute_Implementation(const FDocInteractionContext& Context) const override;
};
