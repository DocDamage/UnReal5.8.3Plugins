#include "DocInteractionRules.h"
#include "DocInteractionComponents.h"
#include "DocInteractionSubsystem.h"
#include "DocInteractionLog.h"
#include "DocCoreBlueprintLibrary.h"
#include "DocCoreTags.h"
#include "Interfaces/DocGameplayTagProvider.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionRules)

namespace DocInteractionRulesPrivate
{
	AActor* Resolve(const FDocInteractionContext& Context, EDocInteractionTagSubject Subject)
	{
		return Subject == EDocInteractionTagSubject::Interactor ? Context.Interactor.Get() : Context.Target.Get();
	}

	/** Tags from the actor or any of its components that provide tags. False = no provider at all. */
	bool GatherTags(const AActor* Actor, FGameplayTagContainer& OutTags)
	{
		OutTags.Reset();
		if (!Actor)
		{
			return false;
		}
		bool bAny = false;
		FGameplayTagContainer Scratch;
		if (UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(Actor, Scratch))
		{
			OutTags.AppendTags(Scratch);
			bAny = true;
		}
		for (const UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(Component, Scratch))
			{
				OutTags.AppendTags(Scratch);
				bAny = true;
			}
		}
		return bAny;
	}

	/** First object (actor or component) implementing the mutable provider. */
	UObject* FindMutableProvider(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (Actor->Implements<UDocMutableGameplayTagProvider>())
		{
			return Actor;
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->Implements<UDocMutableGameplayTagProvider>())
			{
				return Component;
			}
		}
		return nullptr;
	}

	FVector TargetPoint(const FDocInteractionContext& Context)
	{
		if (const UDocInteractableComponent* Interactable = Context.Interactable.Get())
		{
			return Interactable->GetInteractionLocation();
		}
		const AActor* Target = Context.Target.Get();
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}

	FDocGameplayContext ToCoreContext(const FDocInteractionContext& Context)
	{
		FDocGameplayContext Core = FDocGameplayContext::Make(Context.World.Get(), Context.Interactor.Get(), Context.Target.Get());
		Core.CorrelationId = Context.Session.GetOperationId();
		return Core;
	}
}

using namespace DocInteractionRulesPrivate;

// ---- Conditions ----

FDocConditionResult UDocInteractionCondition::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	return FDocConditionResult::Satisfied();
}

FDocConditionResult UDocInteractionCondition_RequiredTags::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	FGameplayTagContainer Owned;
	if (!GatherTags(Resolve(Context, Subject), Owned))
	{
		return FDocConditionResult::Unavailable(DocCoreTags::Error_Unavailable, TEXT("Subject has no gameplay tag provider"));
	}
	const bool bOk = bRequireAll ? Owned.HasAll(Tags) : Owned.HasAny(Tags);
	return bOk ? FDocConditionResult::Satisfied()
		: FDocConditionResult::Unsatisfied(DocInteractionTags::Error_MissingTags, FString::Printf(TEXT("Requires %s"), *Tags.ToStringSimple()));
}

FDocConditionResult UDocInteractionCondition_BlockedTags::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	FGameplayTagContainer Owned;
	if (!GatherTags(Resolve(Context, Subject), Owned))
	{
		// No tag store: nothing can be blocking. This is a legitimate empty set, not unknown,
		// only when the subject exists.
		return Resolve(Context, Subject) ? FDocConditionResult::Satisfied()
			: FDocConditionResult::Unavailable(DocInteractionTags::Error_TargetLost, TEXT("Subject missing"));
	}
	return Owned.HasAny(Tags)
		? FDocConditionResult::Unsatisfied(DocInteractionTags::Error_BlockedTags, FString::Printf(TEXT("Blocked by %s"), *Tags.ToStringSimple()))
		: FDocConditionResult::Satisfied();
}

FDocConditionResult UDocInteractionCondition_Distance::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const AActor* Interactor = Context.Interactor.Get();
	if (!Interactor || !Context.Target.IsValid())
	{
		return FDocConditionResult::Unavailable(DocInteractionTags::Error_TargetLost, TEXT("Interactor or target missing"));
	}
	const float Distance = FVector::Dist(Interactor->GetActorLocation(), TargetPoint(Context));
	if (Distance < MinDistance || Distance > MaxDistance)
	{
		return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_OutOfRange,
			FString::Printf(TEXT("Distance %.1f outside [%.1f, %.1f]"), Distance, MinDistance, MaxDistance));
	}
	return FDocConditionResult::Satisfied();
}

FDocConditionResult UDocInteractionCondition_Facing::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const AActor* Interactor = Context.Interactor.Get();
	if (!Interactor || !Context.Target.IsValid())
	{
		return FDocConditionResult::Unavailable(DocInteractionTags::Error_TargetLost, TEXT("Interactor or target missing"));
	}
	FVector EyeLocation;
	FRotator EyeRotation;
	Interactor->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector ToTarget = (TargetPoint(Context) - EyeLocation).GetSafeNormal();
	if (ToTarget.IsNearlyZero())
	{
		return FDocConditionResult::Satisfied();
	}
	const float Dot = FVector::DotProduct(EyeRotation.Vector(), ToTarget);
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	return Angle <= MaxAngleDegrees ? FDocConditionResult::Satisfied()
		: FDocConditionResult::Unsatisfied(DocInteractionTags::Error_NotFacing, FString::Printf(TEXT("Facing angle %.1f > %.1f"), Angle, MaxAngleDegrees));
}

FDocConditionResult UDocInteractionCondition_Cooldown::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const UDocInteractionSubsystem* Subsystem = Context.Subsystem.Get();
	if (!Subsystem)
	{
		return FDocConditionResult::Unavailable(DocCoreTags::Error_Unavailable, TEXT("Interaction subsystem unavailable"));
	}
	const AActor* Who = Scope == EDocInteractionUseScope::PerInteractor ? Context.Interactor.Get() : nullptr;
	const double Last = Subsystem->GetLastCompletionTime(Context.Interactable.Get(), Context.DefinitionId, Who);
	if (Last < 0.0)
	{
		return FDocConditionResult::Satisfied();
	}
	const double Remaining = (Last + CooldownSeconds) - Subsystem->GetNow();
	return Remaining > 0.0
		? FDocConditionResult::Unsatisfied(DocInteractionTags::Error_Cooldown, FString::Printf(TEXT("Cooldown %.2fs remaining"), Remaining))
		: FDocConditionResult::Satisfied();
}

FDocConditionResult UDocInteractionCondition_OneTime::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const UDocInteractionSubsystem* Subsystem = Context.Subsystem.Get();
	if (!Subsystem)
	{
		return FDocConditionResult::Unavailable(DocCoreTags::Error_Unavailable, TEXT("Interaction subsystem unavailable"));
	}
	const AActor* Who = Scope == EDocInteractionUseScope::PerInteractor ? Context.Interactor.Get() : nullptr;
	return Subsystem->HasCompleted(Context.Interactable.Get(), Context.DefinitionId, Who)
		? FDocConditionResult::Unsatisfied(DocInteractionTags::Error_AlreadyUsed, TEXT("One-time interaction already used"))
		: FDocConditionResult::Satisfied();
}

FDocConditionResult UDocInteractionCondition_Authority::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const FDocGameplayContext Core = ToCoreContext(Context);
	return Core.HasAuthority() ? FDocConditionResult::Satisfied()
		: FDocConditionResult::Unsatisfied(DocInteractionTags::Error_NoAuthority, TEXT("Requires authority"));
}

FDocConditionResult UDocInteractionCondition_RequiredClass::Evaluate_Implementation(const FDocInteractionContext& Context) const
{
	const AActor* Actor = Resolve(Context, Subject);
	if (!Actor)
	{
		return FDocConditionResult::Unavailable(DocInteractionTags::Error_TargetLost, TEXT("Subject missing"));
	}
	if (RequiredClass && !Actor->IsA(RequiredClass))
	{
		return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_MissingInterface, FString::Printf(TEXT("Requires class %s"), *RequiredClass->GetName()));
	}
	if (RequiredInterface && !Actor->GetClass()->ImplementsInterface(RequiredInterface))
	{
		return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_MissingInterface, FString::Printf(TEXT("Requires interface %s"), *RequiredInterface->GetName()));
	}
	return FDocConditionResult::Satisfied();
}

// ---- Actions ----

FDocSystemResult UDocInteractionAction::Execute_Implementation(const FDocInteractionContext& Context) const
{
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("%s does not implement Execute"), *GetClass()->GetName()));
}

FDocSystemResult UDocInteractionAction::Compensate_Implementation(const FDocInteractionContext& Context) const
{
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Action is not reversible"));
}

FDocSystemResult UDocInteractionAction_TagDelta::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* SubjectActor = Resolve(Context, Subject);
	UObject* Provider = FindMutableProvider(SubjectActor);
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			FString::Printf(TEXT("%s has no IDocMutableGameplayTagProvider; tag changes need an explicit mutable provider"), *GetNameSafe(SubjectActor)));
	}
	return IDocMutableGameplayTagProvider::Execute_ApplyDocTagDelta(Provider, ToCoreContext(Context), TagsToAdd, TagsToRemove);
}

FDocSystemResult UDocInteractionAction_TagDelta::Compensate_Implementation(const FDocInteractionContext& Context) const
{
	AActor* SubjectActor = Resolve(Context, Subject);
	UObject* Provider = FindMutableProvider(SubjectActor);
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Mutable tag provider gone"));
	}
	// Inverse delta, attributed to the same owner context.
	return IDocMutableGameplayTagProvider::Execute_ApplyDocTagDelta(Provider, ToCoreContext(Context), TagsToRemove, TagsToAdd);
}

FDocSystemResult UDocInteractionAction_NotifyReceiver::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* SubjectActor = Resolve(Context, Subject);
	if (SubjectActor && SubjectActor->Implements<UDocInteractionReceiver>())
	{
		IDocInteractionReceiver::Execute_OnDocInteraction(SubjectActor, Context, EventTag);
		return FDocSystemResult::MakeSuccess();
	}
	return bRequireReceiver
		? FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("%s does not implement IDocInteractionReceiver"), *GetNameSafe(SubjectActor)), DocInteractionTags::Error_MissingInterface)
		: FDocSystemResult::MakeNoChange(TEXT("No receiver"));
}

namespace DocInteractionRulesPrivate
{
	void SetActorEnabled(AActor* Actor, bool bEnabled)
	{
		Actor->SetActorHiddenInGame(!bEnabled);
		Actor->SetActorEnableCollision(bEnabled);
		Actor->SetActorTickEnabled(bEnabled);
	}
}

FDocSystemResult UDocInteractionAction_SetEnabled::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* Target = Context.Target.Get();
	if (!Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Target missing"), DocInteractionTags::Error_TargetLost);
	}
	SetActorEnabled(Target, bEnabled);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInteractionAction_SetEnabled::Compensate_Implementation(const FDocInteractionContext& Context) const
{
	AActor* Target = Context.Target.Get();
	if (!Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Target missing"));
	}
	SetActorEnabled(Target, !bEnabled);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInteractionAction_SpawnActor::Execute_Implementation(const FDocInteractionContext& Context) const
{
	UWorld* World = Context.World.Get();
	AActor* Target = Context.Target.Get();
	if (!World || !Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("World or target missing"), DocInteractionTags::Error_TargetLost);
	}
	UClass* Class = ActorClass.Get();
	if (!Class)
	{
		// Do not block the game thread on a substantial load; the class must be loaded (e.g. referenced by the level or a bundle).
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady,
			FString::Printf(TEXT("Spawn class %s is not loaded"), *ActorClass.ToString()));
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = CollisionHandling;
	switch (Owner)
	{
	case EDocSpawnOwner::Interactor: Params.Owner = Context.Interactor.Get(); break;
	case EDocSpawnOwner::Target: Params.Owner = Target; break;
	default: break;
	}
	Params.Instigator = Cast<APawn>(Context.Interactor.Get());
	const FTransform SpawnTransform = RelativeTransform * Target->GetActorTransform();
	AActor* Spawned = World->SpawnActor<AActor>(Class, SpawnTransform, Params);
	if (!Spawned)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("SpawnActor returned null (collision handling or class)"), DocInteractionTags::Error_ActionFailed);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInteractionAction_DestroyTarget::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* Target = Context.Target.Get();
	if (!Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Target missing"), DocInteractionTags::Error_TargetLost);
	}
	return Target->Destroy() ? FDocSystemResult::MakeSuccess()
		: FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Destroy refused (authority or indestructible)"), DocInteractionTags::Error_ActionFailed);
}

FDocSystemResult UDocInteractionAction_Attach::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* Interactor = Context.Interactor.Get();
	AActor* Target = Context.Target.Get();
	if (!Interactor || !Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Interactor or target missing"), DocInteractionTags::Error_TargetLost);
	}
	if (bAttach)
	{
		const FAttachmentTransformRules Rules = bSnapToTarget ? FAttachmentTransformRules::SnapToTargetNotIncludingScale : FAttachmentTransformRules::KeepWorldTransform;
		return Interactor->AttachToActor(Target, Rules, SocketName) ? FDocSystemResult::MakeSuccess()
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("AttachToActor failed"), DocInteractionTags::Error_ActionFailed);
	}
	if (Interactor->GetAttachParentActor() == Target)
	{
		Interactor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeNoChange(TEXT("Interactor not attached to this target"));
}

FDocSystemResult UDocInteractionAction_Attach::Compensate_Implementation(const FDocInteractionContext& Context) const
{
	AActor* Interactor = Context.Interactor.Get();
	AActor* Target = Context.Target.Get();
	if (!Interactor || !Target)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Interactor or target missing"));
	}
	if (bAttach)
	{
		if (Interactor->GetAttachParentActor() == Target)
		{
			Interactor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		return FDocSystemResult::MakeSuccess();
	}
	Interactor->AttachToActor(Target, FAttachmentTransformRules::KeepWorldTransform, SocketName);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInteractionAction_PlaySound::Execute_Implementation(const FDocInteractionContext& Context) const
{
	UWorld* World = Context.World.Get();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No audio on dedicated server"));
	}
	USoundBase* Loaded = Sound.Get();
	if (!Loaded)
	{
		return bFailIfMissing
			? FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, FString::Printf(TEXT("Sound %s not loaded"), *Sound.ToString()))
			: FDocSystemResult::MakeNoChange(TEXT("Sound not loaded; skipped"));
	}
	UGameplayStatics::PlaySoundAtLocation(World, Loaded, TargetPoint(Context), VolumeMultiplier);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInteractionAction_RequestAnimation::Execute_Implementation(const FDocInteractionContext& Context) const
{
	AActor* SubjectActor = Resolve(Context, Subject);
	if (SubjectActor && SubjectActor->Implements<UDocInteractionReceiver>())
	{
		IDocInteractionReceiver::Execute_OnDocInteraction(SubjectActor, Context, AnimationTag);
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
		FString::Printf(TEXT("%s has no IDocInteractionReceiver animation adapter"), *GetNameSafe(SubjectActor)), DocInteractionTags::Error_MissingInterface);
}
