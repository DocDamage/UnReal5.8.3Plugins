#include "DocInteractionSubsystem.h"
#include "DocInteractionComponents.h"
#include "DocInteractionRules.h"
#include "DocInteractionLog.h"
#include "DocCoreTags.h"
#include "DocGameplayContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionSubsystem)

UDocInteractionSubsystem* UDocInteractionSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocInteractionSubsystem>() : nullptr;
}

bool UDocInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDocInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocInteractionSubsystem, STATGROUP_Tickables);
}

double UDocInteractionSubsystem::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UDocInteractionSubsystem::Deinitialize()
{
	bShuttingDown = true;
	// Cancel every running session so reservations and listeners are released exactly once.
	TArray<FDocRequestHandle> Active;
	Sessions.ForEach([&Active](const FDocRequestHandle& Handle, const FSession&) { Active.Add(Handle); });
	for (const FDocRequestHandle& Handle : Active)
	{
		FinishSession(Handle, EDocInteractionSessionState::Cancelled,
			FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("World teardown")));
	}
	Sessions.Reset();
	Interactables.Reset();
	LastCompletion.Reset();
	Super::Deinitialize();
}

void UDocInteractionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	AdvanceSessions(DeltaTime);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

void UDocInteractionSubsystem::RegisterInteractable(UDocInteractableComponent* Interactable)
{
	if (Interactable)
	{
		Interactables.AddUnique(Interactable);
	}
}

void UDocInteractionSubsystem::UnregisterInteractable(UDocInteractableComponent* Interactable)
{
	Interactables.RemoveAll([Interactable](const TWeakObjectPtr<UDocInteractableComponent>& P) { return !P.IsValid() || P.Get() == Interactable; });

	// Sessions targeting a removed interactable end as Failed (target lost).
	TArray<FDocRequestHandle> Affected;
	Sessions.ForEach([&](const FDocRequestHandle& Handle, const FSession& S) { if (S.Target.Get() == Interactable) { Affected.Add(Handle); } });
	for (const FDocRequestHandle& Handle : Affected)
	{
		FinishSession(Handle, EDocInteractionSessionState::Failed,
			FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Target removed"), DocInteractionTags::Error_TargetLost));
	}
}

void UDocInteractionSubsystem::GetInteractablesInRadius(const FVector& Center, float Radius, TArray<UDocInteractableComponent*>& Out) const
{
	Out.Reset();
	const float RadiusSq = Radius * Radius;
	for (const TWeakObjectPtr<UDocInteractableComponent>& Weak : Interactables)
	{
		UDocInteractableComponent* Interactable = Weak.Get();
		if (Interactable && FVector::DistSquared(Center, Interactable->GetInteractionLocation()) <= RadiusSq)
		{
			Out.Add(Interactable);
		}
	}
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

FDocInteractionContext UDocInteractionSubsystem::MakeContext(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target,
	const FDocInteractionDefinition& Definition, const FDocRequestHandle& Session) const
{
	FDocInteractionContext Context;
	Context.World = GetWorld();
	Context.InteractorComponent = Interactor;
	Context.Interactor = Interactor ? Interactor->GetOwner() : nullptr;
	Context.Interactable = Target;
	Context.Target = Target ? Target->GetOwner() : nullptr;
	Context.DefinitionId = Definition.DefinitionId;
	Context.InteractionTag = Definition.InteractionTag;
	Context.Session = Session;
	Context.Subsystem = const_cast<UDocInteractionSubsystem*>(this);
	return Context;
}

FDocConditionResult UDocInteractionSubsystem::Validate(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target,
	const FDocInteractionDefinition& Definition, bool bRunningCheck) const
{
	const AActor* InteractorActor = Interactor ? Interactor->GetOwner() : nullptr;
	const AActor* TargetActor = Target ? Target->GetOwner() : nullptr;
	if (!InteractorActor || !TargetActor || !IsValid(TargetActor) || !IsValid(InteractorActor))
	{
		return FDocConditionResult::Unavailable(DocInteractionTags::Error_TargetLost, TEXT("Interactor or target no longer exists"));
	}
	if (!Definition.bEnabled || !Target->IsInteractionEnabled())
	{
		return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_Disabled, TEXT("Interaction disabled"));
	}
	if (Definition.bRequiresAuthority)
	{
		const FDocGameplayContext Core = FDocGameplayContext::Make(GetWorld(), const_cast<AActor*>(InteractorActor));
		if (!Core.HasAuthority())
		{
			return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_NoAuthority, TEXT("Requires authority"));
		}
	}
	if (Definition.MaxDistance > 0.f)
	{
		const float Distance = FVector::Dist(InteractorActor->GetActorLocation(), Target->GetInteractionLocation());
		if (Distance > Definition.MaxDistance)
		{
			return FDocConditionResult::Unsatisfied(DocInteractionTags::Error_OutOfRange,
				FString::Printf(TEXT("Distance %.1f > MaxDistance %.1f"), Distance, Definition.MaxDistance));
		}
	}

	const FDocInteractionContext Context = MakeContext(Interactor, Target, Definition, FDocRequestHandle());
	TArray<FDocConditionResult> Results;
	for (const TObjectPtr<UDocInteractionCondition>& Condition : Definition.Conditions)
	{
		if (!Condition)
		{
			Results.Add(FDocConditionResult::Unavailable(DocCoreTags::Error_InvalidConfiguration, TEXT("Empty condition slot")));
			continue;
		}
		if (bRunningCheck && !Condition->bRevalidateWhileRunning)
		{
			continue;
		}
		Results.Add(Condition->Evaluate(Context));
		// Fail fast on the first non-satisfied condition so the reason is the first failing rule.
		if (!Results.Last().IsSatisfied())
		{
			break;
		}
	}
	return FDocConditionResult::CombineAll(Results);
}

FDocInteractionOption UDocInteractionSubsystem::EvaluateOption(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target,
	const FDocInteractionDefinition& Definition) const
{
	FDocInteractionOption Option;
	Option.DefinitionId = Definition.DefinitionId;
	Option.InteractionTag = Definition.InteractionTag;
	Option.DisplayName = Definition.DisplayName;
	Option.Prompt = Definition.Prompt;
	Option.Icon = Definition.Icon;
	Option.InputHint = Definition.InputHint;
	Option.Mode = Definition.Mode;

	const FDocConditionResult Check = Validate(Interactor, Target, Definition, false);
	Option.bAvailable = Check.IsSatisfied();
	if (!Option.bAvailable)
	{
		Option.FailureTag = Check.ReasonTag;
		Option.FailureReason = Check.UserReason;
		Option.Diagnostic = Check.Diagnostic;
	}
	else if (FindReservationConflict(Interactor, Target, Definition).IsSet())
	{
		Option.bAvailable = false;
		Option.FailureTag = DocInteractionTags::Error_Reserved;
		Option.Diagnostic = TEXT("Reserved by another session");
	}
	return Option;
}

TArray<FDocInteractionOption> UDocInteractionSubsystem::GetAvailableInteractions(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target) const
{
	TArray<FDocInteractionOption> Options;
	if (!Target)
	{
		return Options;
	}
	TArray<const FDocInteractionDefinition*> Definitions;
	Target->GetEffectiveDefinitions(Definitions);
	for (const FDocInteractionDefinition* Def : Definitions)
	{
		if (Def && Def->bEnabled)
		{
			Options.Add(EvaluateOption(Interactor, Target, *Def));
		}
	}
	return Options;
}

FDocRequestHandle UDocInteractionSubsystem::FindReservationConflict(const UDocInteractorComponent* Interactor, const UDocInteractableComponent* Target,
	const FDocInteractionDefinition& Definition) const
{
	FDocRequestHandle Conflict;
	Sessions.ForEach([&](const FDocRequestHandle& Handle, const FSession& S)
	{
		if (Conflict.IsSet() || S.Info.State != EDocInteractionSessionState::Running)
		{
			return;
		}
		// One session per interactor, always.
		if (S.Interactor.Get() == Interactor)
		{
			Conflict = Handle;
			return;
		}
		if (S.Target.Get() != Target)
		{
			return;
		}
		switch (Definition.Concurrency)
		{
		case EDocInteractionConcurrency::ExclusiveTarget:
			Conflict = Handle;
			break;
		case EDocInteractionConcurrency::ExclusiveDefinition:
			if (S.Info.DefinitionId == Definition.DefinitionId) { Conflict = Handle; }
			break;
		default:
			break;
		}
	});
	return Conflict;
}

bool UDocInteractionSubsystem::GetSessionInfo(const FDocRequestHandle& Session, FDocInteractionSessionInfo& OutInfo) const
{
	if (const FSession* S = Sessions.Find(Session, this))
	{
		OutInfo = S->Info;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

FDocInteractionSessionInfo UDocInteractionSubsystem::RequestInteraction(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target, FName DefinitionId)
{
	FDocInteractionSessionInfo Rejected;
	Rejected.State = EDocInteractionSessionState::Rejected;
	Rejected.DefinitionId = DefinitionId;
	Rejected.Interactor = Interactor ? Interactor->GetOwner() : nullptr;
	Rejected.Target = Target ? Target->GetOwner() : nullptr;

	auto Reject = [&](FDocSystemResult Result)
	{
		Rejected.Result = MoveTemp(Result);
		if (Interactor)
		{
			Interactor->NotifySessionEnded(Rejected);
		}
		OnSessionEndedNative.Broadcast(Rejected);
		return Rejected;
	};

	if (bShuttingDown)
	{
		return Reject(FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Interaction subsystem shutting down")));
	}
	if (!Interactor || !Target)
	{
		return Reject(FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Interactor and target are required")));
	}
	const FDocInteractionDefinition* Definition = Target->FindDefinition(DefinitionId);
	if (!Definition)
	{
		return Reject(FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
			FString::Printf(TEXT("%s has no interaction '%s'"), *GetNameSafe(Target->GetOwner()), *DefinitionId.ToString())));
	}

	const FDocConditionResult Check = Validate(Interactor, Target, *Definition, false);
	if (!Check.IsSatisfied())
	{
		const EDocResultOutcome Outcome = Check.IsUnavailable() ? EDocResultOutcome::Unavailable
			: (Check.ReasonTag == DocInteractionTags::Error_NoAuthority ? EDocResultOutcome::PermissionDenied : EDocResultOutcome::Failed);
		return Reject(FDocSystemResult::MakeFailure(Outcome, Check.Diagnostic, Check.ReasonTag, Check.UserReason));
	}

	if (FindReservationConflict(Interactor, Target, *Definition).IsSet())
	{
		return Reject(FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Target or interactor already has an active session"), DocInteractionTags::Error_Reserved));
	}

	// Reserve.
	FSession NewSession;
	NewSession.Interactor = Interactor;
	NewSession.Target = Target;
	NewSession.Info.State = EDocInteractionSessionState::Running;
	NewSession.Info.Mode = Definition->Mode;
	NewSession.Info.DefinitionId = DefinitionId;
	NewSession.Info.Interactor = Interactor->GetOwner();
	NewSession.Info.Target = Target->GetOwner();
	NewSession.Info.bProgressKnown = Definition->Mode == EDocInteractionMode::HoldToComplete
		|| (Definition->Mode == EDocInteractionMode::Repeated && Definition->MaxRepeats > 0);
	const FDocRequestHandle Handle = Sessions.Add(this, MoveTemp(NewSession));
	FSession* Session = Sessions.Find(Handle, this);
	Session->Info.Handle = Handle;

	const FDocInteractionContext Context = MakeContext(Interactor, Target, *Definition, Handle);
	Interactor->NotifySessionStarted(Session->Info);
	NotifyReceiver(Target->GetOwner(), Context, DocInteractionTags::Phase_Started);

	// Session may have been cancelled by a listener.
	Session = Sessions.Find(Handle, this);
	if (!Session)
	{
		FDocInteractionSessionInfo Gone;
		Gone.Handle = Handle;
		Gone.State = EDocInteractionSessionState::Cancelled;
		Gone.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Cancelled during start"));
		return Gone;
	}

	switch (Definition->Mode)
	{
	case EDocInteractionMode::Instant:
	{
		const FDocSystemResult ActionResult = ExecuteActions(*Session, *Definition);
		const FDocInteractionSessionInfo Snapshot = Session->Info;
		FinishSession(Handle, ActionResult.IsSuccess() ? EDocInteractionSessionState::Completed : EDocInteractionSessionState::Failed, ActionResult);
		FDocInteractionSessionInfo Out = Snapshot;
		Out.State = ActionResult.IsSuccess() ? EDocInteractionSessionState::Completed : EDocInteractionSessionState::Failed;
		Out.Result = ActionResult;
		Out.Progress = 1.f;
		return Out;
	}
	case EDocInteractionMode::HoldToComplete:
		if (Definition->HoldDuration <= 0.f)
		{
			// Zero-duration hold: commit immediately (after the same revalidation).
			AdvanceSessions(0.f);
			FDocInteractionSessionInfo Out;
			if (!GetSessionInfo(Handle, Out))
			{
				Out.Handle = Handle;
				Out.State = EDocInteractionSessionState::Completed;
			}
			return Out;
		}
		return Session->Info;
	case EDocInteractionMode::Continuous:
	case EDocInteractionMode::Repeated:
	{
		const FDocSystemResult ActionResult = ExecuteActions(*Session, *Definition);
		if (!ActionResult.IsSuccess())
		{
			FDocInteractionSessionInfo Out = Session->Info;
			FinishSession(Handle, EDocInteractionSessionState::Failed, ActionResult);
			Out.State = EDocInteractionSessionState::Failed;
			Out.Result = ActionResult;
			return Out;
		}
		Session->Info.Repetitions = 1;
		if (Definition->Mode == EDocInteractionMode::Repeated && Definition->MaxRepeats > 0)
		{
			Session->Info.Progress = 1.f / Definition->MaxRepeats;
			if (Session->Info.Repetitions >= Definition->MaxRepeats)
			{
				FDocInteractionSessionInfo Out = Session->Info;
				FinishSession(Handle, EDocInteractionSessionState::Completed, FDocSystemResult::MakeSuccess(Handle.GetOperationId()));
				Out.State = EDocInteractionSessionState::Completed;
				return Out;
			}
		}
		return Session->Info;
	}
	default:
		return Session->Info;
	}
}

FDocSystemResult UDocInteractionSubsystem::ExecuteActions(FSession& Session, const FDocInteractionDefinition& Definition)
{
	UDocInteractorComponent* Interactor = Session.Interactor.Get();
	UDocInteractableComponent* Target = Session.Target.Get();
	FDocInteractionContext Context = MakeContext(Interactor, Target, Definition, Session.Info.Handle);
	Context.Repetition = Session.Info.Repetitions;

	TArray<const UDocInteractionAction*> Executed;
	for (const TObjectPtr<UDocInteractionAction>& Action : Definition.Actions)
	{
		if (!Action)
		{
			continue;
		}
		const FDocSystemResult Result = Action->Execute(Context);
		if (!Result.IsSuccess())
		{
			// Report partial execution honestly, then run only declared compensation.
			if (Definition.bCompensateOnFailure)
			{
				for (int32 i = Executed.Num() - 1; i >= 0; --i)
				{
					const UDocInteractionAction* Done = Executed[i];
					if (Done->IsReversible())
					{
						const FDocSystemResult Undo = Done->Compensate(Context);
						if (Undo.IsSuccess())
						{
							Session.Info.CompensatedActions.Add(Done->GetEffectiveActionId());
						}
						else
						{
							UE_LOG(LogDocInteraction, Warning, TEXT("Compensation of %s failed: %s"), *Done->GetEffectiveActionId().ToString(), *Undo.ToString());
						}
					}
				}
			}
			FDocSystemResult Failure = FDocSystemResult::MakeFailure(Result.Outcome == EDocResultOutcome::Unset ? EDocResultOutcome::Failed : Result.Outcome,
				FString::Printf(TEXT("Action %s failed after %d executed action(s): %s"), *Action->GetEffectiveActionId().ToString(), Executed.Num(), *Result.Diagnostic),
				DocInteractionTags::Error_ActionFailed, Result.UserMessage, Session.Info.Handle.GetOperationId());
			if (Failure.Outcome == EDocResultOutcome::Succeeded || Failure.Outcome == EDocResultOutcome::NoChange)
			{
				Failure.Outcome = EDocResultOutcome::Failed;
			}
			return Failure;
		}
		Executed.Add(Action.Get());
		Session.Info.ExecutedActions.Add(Action->GetEffectiveActionId());
	}
	return FDocSystemResult::MakeSuccess(Session.Info.Handle.GetOperationId());
}

void UDocInteractionSubsystem::AdvanceSessions(float DeltaSeconds)
{
	TArray<FDocRequestHandle> Handles;
	Sessions.ForEach([&Handles](const FDocRequestHandle& Handle, const FSession& S)
	{
		if (S.Info.State == EDocInteractionSessionState::Running) { Handles.Add(Handle); }
	});

	for (const FDocRequestHandle& Handle : Handles)
	{
		FSession* Session = Sessions.Find(Handle, this);
		if (!Session)
		{
			continue;
		}
		UDocInteractorComponent* Interactor = Session->Interactor.Get();
		UDocInteractableComponent* Target = Session->Target.Get();
		const FDocInteractionDefinition* Definition = Target ? Target->FindDefinition(Session->Info.DefinitionId) : nullptr;
		if (!Interactor || !Target || !Definition)
		{
			FinishSession(Handle, EDocInteractionSessionState::Failed,
				FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Interactor, target or definition lost"), DocInteractionTags::Error_TargetLost));
			continue;
		}

		Session->Info.ElapsedSeconds += DeltaSeconds;
		Session->TimeSinceRevalidate += DeltaSeconds;
		Session->TimeSinceRepeat += DeltaSeconds;

		// Periodic revalidation of range/conditions/reservation.
		const bool bHoldDue = Definition->Mode == EDocInteractionMode::HoldToComplete && Session->Info.ElapsedSeconds >= Definition->HoldDuration;
		if (bHoldDue || Session->TimeSinceRevalidate >= Definition->RevalidateInterval)
		{
			Session->TimeSinceRevalidate = 0.f;
			const FDocConditionResult Check = Validate(Interactor, Target, *Definition, true);
			if (!Check.IsSatisfied())
			{
				FinishSession(Handle, EDocInteractionSessionState::Cancelled,
					FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, FString::Printf(TEXT("Revalidation failed: %s"), *Check.Diagnostic), Check.ReasonTag, Check.UserReason));
				continue;
			}
		}

		if (Definition->MaxDuration > 0.f && Session->Info.ElapsedSeconds >= Definition->MaxDuration && !bHoldDue)
		{
			if (Definition->Mode == EDocInteractionMode::HoldToComplete)
			{
				FinishSession(Handle, EDocInteractionSessionState::Cancelled, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Hold exceeded MaxDuration")));
			}
			else
			{
				FinishSession(Handle, EDocInteractionSessionState::Completed, FDocSystemResult::MakeSuccess(Handle.GetOperationId()));
			}
			continue;
		}

		switch (Definition->Mode)
		{
		case EDocInteractionMode::HoldToComplete:
			Session->Info.Progress = Definition->HoldDuration > 0.f ? FMath::Clamp(Session->Info.ElapsedSeconds / Definition->HoldDuration, 0.f, 1.f) : 1.f;
			if (bHoldDue)
			{
				// Critical revalidation already ran above; commit now.
				const FDocSystemResult Result = ExecuteActions(*Session, *Definition);
				Session->Info.Progress = 1.f;
				FinishSession(Handle, Result.IsSuccess() ? EDocInteractionSessionState::Completed : EDocInteractionSessionState::Failed, Result);
				continue;
			}
			break;
		case EDocInteractionMode::Repeated:
			while (Session && Session->TimeSinceRepeat >= Definition->RepeatInterval)
			{
				Session->TimeSinceRepeat -= Definition->RepeatInterval;
				const FDocSystemResult Result = ExecuteActions(*Session, *Definition);
				if (!Result.IsSuccess())
				{
					FinishSession(Handle, EDocInteractionSessionState::Failed, Result);
					Session = nullptr;
					break;
				}
				++Session->Info.Repetitions;
				NotifyReceiver(Target->GetOwner(), MakeContext(Interactor, Target, *Definition, Handle), DocInteractionTags::Phase_Repeated);
				Session = Sessions.Find(Handle, this); // a receiver may have cancelled
				if (Session && Definition->MaxRepeats > 0)
				{
					Session->Info.Progress = FMath::Clamp(static_cast<float>(Session->Info.Repetitions) / Definition->MaxRepeats, 0.f, 1.f);
					if (Session->Info.Repetitions >= Definition->MaxRepeats)
					{
						FinishSession(Handle, EDocInteractionSessionState::Completed, FDocSystemResult::MakeSuccess(Handle.GetOperationId()));
						Session = nullptr;
					}
				}
			}
			if (!Session)
			{
				continue;
			}
			break;
		default:
			break;
		}

		if (FSession* Still = Sessions.Find(Handle, this))
		{
			Interactor->NotifySessionProgress(Still->Info);
		}
	}
}

FDocSystemResult UDocInteractionSubsystem::CancelInteraction(const FDocRequestHandle& Session)
{
	if (!Sessions.Find(Session, this))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Session already finished or unknown"));
	}
	FinishSession(Session, EDocInteractionSessionState::Cancelled, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Cancelled by request")));
	return FDocSystemResult::MakeSuccess(Session.GetOperationId());
}

FDocSystemResult UDocInteractionSubsystem::CompleteInteraction(const FDocRequestHandle& Session)
{
	const FSession* S = Sessions.Find(Session, this);
	if (!S)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Session already finished or unknown"));
	}
	if (S->Info.Mode == EDocInteractionMode::HoldToComplete)
	{
		FinishSession(Session, EDocInteractionSessionState::Cancelled, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Hold released early")));
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Hold released before completion"));
	}
	FinishSession(Session, EDocInteractionSessionState::Completed, FDocSystemResult::MakeSuccess(Session.GetOperationId()));
	return FDocSystemResult::MakeSuccess(Session.GetOperationId());
}

void UDocInteractionSubsystem::FinishSession(const FDocRequestHandle& Handle, EDocInteractionSessionState State, const FDocSystemResult& Result)
{
	FSession Removed;
	if (!Sessions.Remove(Handle, this, &Removed))
	{
		return; // already terminal: exactly one terminal result
	}
	Removed.Info.State = State;
	Removed.Info.Result = Result;

	UDocInteractorComponent* Interactor = Removed.Interactor.Get();
	UDocInteractableComponent* Target = Removed.Target.Get();

	if (State == EDocInteractionSessionState::Completed)
	{
		RecordUse(Removed);
	}

	if (Target && !bShuttingDown)
	{
		if (const FDocInteractionDefinition* Definition = Target->FindDefinition(Removed.Info.DefinitionId))
		{
			const FGameplayTag Phase = State == EDocInteractionSessionState::Completed ? DocInteractionTags::Phase_Completed : DocInteractionTags::Phase_Cancelled;
			NotifyReceiver(Target->GetOwner(), MakeContext(Interactor, Target, *Definition, Handle), Phase);
		}
	}
	if (Interactor)
	{
		Interactor->NotifySessionEnded(Removed.Info);
	}
	OnSessionEndedNative.Broadcast(Removed.Info);
}

void UDocInteractionSubsystem::RecordUse(const FSession& Session)
{
	const double Now = GetNow();
	FUseKey Global{ Session.Target, Session.Info.DefinitionId, nullptr };
	LastCompletion.Add(Global, Now);
	if (const UDocInteractorComponent* Interactor = Session.Interactor.Get())
	{
		FUseKey PerInteractor{ Session.Target, Session.Info.DefinitionId, Interactor->GetOwner() };
		LastCompletion.Add(PerInteractor, Now);
	}
}

double UDocInteractionSubsystem::GetLastCompletionTime(const UDocInteractableComponent* Target, FName DefinitionId, const AActor* InteractorOrNull) const
{
	const FUseKey Key{ Target, DefinitionId, InteractorOrNull };
	const double* Found = LastCompletion.Find(Key);
	return Found ? *Found : -1.0;
}

bool UDocInteractionSubsystem::HasCompleted(const UDocInteractableComponent* Target, FName DefinitionId, const AActor* InteractorOrNull) const
{
	return GetLastCompletionTime(Target, DefinitionId, InteractorOrNull) >= 0.0;
}

void UDocInteractionSubsystem::ResetUseRecords(UDocInteractableComponent* Target)
{
	for (auto It = LastCompletion.CreateIterator(); It; ++It)
	{
		if (It.Key().Target.Get() == Target || !It.Key().Target.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UDocInteractionSubsystem::NotifyReceiver(AActor* Actor, const FDocInteractionContext& Context, FGameplayTag Phase) const
{
	if (Actor && Actor->Implements<UDocInteractionReceiver>())
	{
		IDocInteractionReceiver::Execute_OnDocInteraction(Actor, Context, Phase);
	}
}
