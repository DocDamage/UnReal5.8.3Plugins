#include "DocSequenceSubsystem.h"
#include "DocSequencesLog.h"
#include "DocCoreTags.h"
#include "DocPlayerControlSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSequenceSubsystem)

namespace DocSequenceTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Sequence, "Sequence", "DocSequences root");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_AI, "Sequence.Control.AI", "Bound participants' AI is handed to the sequence");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event, "Sequence.Event", "Mapped sequencer events");
	UE_DEFINE_GAMEPLAY_TAG(Event_DoorOpen, "Sequence.Event.DoorOpen");
	UE_DEFINE_GAMEPLAY_TAG(Event_Explosion, "Sequence.Event.Explosion");
	UE_DEFINE_GAMEPLAY_TAG(Event_GameplayResume, "Sequence.Event.GameplayResume");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Role, "Sequence.Role", "Participant binding roles");
}

#if WITH_EDITOR
EDataValidationResult UDocSequenceDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!SequenceTag.IsValid())
	{
		Context.AddError(FText::FromString(TEXT("SequenceTag is required")));
		Result = EDataValidationResult::Invalid;
	}
	if (Sequence.IsNull())
	{
		Context.AddError(FText::FromString(TEXT("Level Sequence is required")));
		Result = EDataValidationResult::Invalid;
	}
	TSet<FName> Ids;
	for (const FDocSequenceEffect& Effect : Effects)
	{
		bool bDuplicate = false;
		Ids.Add(Effect.EffectId, &bDuplicate);
		if (Effect.EffectId.IsNone() || bDuplicate)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Effect ids must be unique and set (%s)"), *Effect.EffectId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		if (Effect.TriggerTimeSeconds < 0.f && !Effect.EventTag.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Effect %s needs a trigger time or an event tag"), *Effect.EffectId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const FDocSequenceRole& Role : Roles)
	{
		if (!Role.Role.IsValid() || Role.BindingTag.IsNone())
		{
			Context.AddError(FText::FromString(TEXT("Every role needs a Role tag and a BindingTag")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocSequenceSubsystem* UDocSequenceSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocSequenceSubsystem>() : nullptr;
}

bool UDocSequenceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocSequenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Epoch = FDocHandleAllocator::NextEpoch();
	Backend = MakeShared<FDocLevelSequenceBackend>();
}

void UDocSequenceSubsystem::Deinitialize()
{
	bDeinitializing = true;
	// Every terminal path releases owned resources, including world teardown.
	while (Queue.Num() > 0)
	{
		CancelQueued(Queue.Num() - 1, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("World teardown")));
	}
	EndActive(EDocSequenceState::Interrupted, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("World teardown")), false);
	FlushEvents();
	Super::Deinitialize();
}

TStatId UDocSequenceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocSequenceSubsystem, STATGROUP_Tickables);
}

void UDocSequenceSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDocSequenceSubsystem* This = CastChecked<UDocSequenceSubsystem>(InThis);
	auto AddSession = [&Collector](FSession& Session)
	{
		Collector.AddReferencedObject(Session.Definition);
		Collector.AddReferencedObject(Session.LoadedSequence);
	};
	if (This->Active) { AddSession(*This->Active); }
	for (TUniquePtr<FSession>& Queued : This->Queue) { AddSession(*Queued); }
	Super::AddReferencedObjects(InThis, Collector);
}

bool UDocSequenceSubsystem::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

// ---------------------------------------------------------------------------
// Providers and participants
// ---------------------------------------------------------------------------

void UDocSequenceSubsystem::RegisterPrerequisiteProvider(UObject* Provider)
{
	if (Provider && Cast<IDocSequencePrerequisiteProvider>(Provider))
	{
		PrerequisiteProviders.AddUnique(Provider);
	}
}

void UDocSequenceSubsystem::UnregisterPrerequisiteProvider(UObject* Provider)
{
	PrerequisiteProviders.Remove(Provider);
}

void UDocSequenceSubsystem::SetHistoryProvider(UObject* Provider)
{
	HistoryProvider = (Provider && Cast<IDocSequenceHistoryProvider>(Provider)) ? Provider : nullptr;
}

void UDocSequenceSubsystem::RegisterParticipant(UObject* Participant)
{
	if (Participant && Participant->Implements<UDocSequenceParticipant>())
	{
		Participants.AddUnique(Participant);
	}
}

void UDocSequenceSubsystem::UnregisterParticipant(UObject* Participant)
{
	Participants.Remove(Participant);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

UDocSequenceSubsystem::FSession* UDocSequenceSubsystem::FindSession(const FDocRequestHandle& Handle)
{
	if (!Handle.IsSet() || Handle.GetEpoch() != Epoch)
	{
		return nullptr;
	}
	if (Active && Active->Id == Handle.GetOperationId())
	{
		return Active.Get();
	}
	for (TUniquePtr<FSession>& Queued : Queue)
	{
		if (Queued->Id == Handle.GetOperationId())
		{
			return Queued.Get();
		}
	}
	return nullptr;
}

const UDocSequenceSubsystem::FSession* UDocSequenceSubsystem::FindSession(const FDocRequestHandle& Handle) const
{
	return const_cast<UDocSequenceSubsystem*>(this)->FindSession(Handle);
}

FDocSequenceRequestInfo UDocSequenceSubsystem::PlaySequence(UDocSequenceDefinition* Definition, const FDocSequencePlayParams& Params)
{
	FDocSequenceRequestInfo Info;
	if (bDeinitializing)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("World is tearing down"));
		return Info;
	}
	if (!Definition || !Definition->SequenceTag.IsValid())
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Definition with a SequenceTag required"));
		return Info;
	}
	if (!Definition->bCanReplay)
	{
		const IDocSequenceHistoryProvider* History = Cast<IDocSequenceHistoryProvider>(HistoryProvider.Get());
		if (CompletedThisWorld.Contains(Definition->SequenceTag) || (History && History->HasDocSequenceCompleted(Definition->SequenceTag)))
		{
			Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Sequence cannot be replayed"));
			return Info;
		}
	}

	TUniquePtr<FSession> Session = MakeUnique<FSession>();
	Session->Id = FDocHandleAllocator::NextOperationId();
	Session->Guid = FGuid::NewGuid();
	Session->Definition = Definition;
	Session->Owner = Params.Owner ? Params.Owner.Get() : static_cast<UObject*>(this);
	Session->bOwnerIsSubsystem = Params.Owner == nullptr;
	for (const FDocSequenceBinding& Binding : Params.Bindings)
	{
		TArray<TWeakObjectPtr<AActor>> Actors;
		for (AActor* Actor : Binding.Actors) { if (Actor) { Actors.Add(Actor); } }
		Session->SuppliedBindings.Add(TPair<FGameplayTag, TArray<TWeakObjectPtr<AActor>>>(Binding.Role, MoveTemp(Actors)));
	}
	for (ULocalPlayer* Player : Params.Players) { if (Player) { Session->Players.Add(Player); } }
	Info.Session = MakeHandle(Session->Id);
	SetState(*Session, EDocSequenceState::Requested);

	bool bStartNow = !Active.IsValid();
	if (!bStartNow)
	{
		const UDocSequenceDefinition* Current = Active->Definition;
		switch (Definition->Arbitration)
		{
		case EDocSequenceArbitration::Reject:
			Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("%s is playing"), *Current->SequenceTag.ToString()));
			Info.Session = FDocRequestHandle();
			FlushEvents();
			return Info;
		case EDocSequenceArbitration::Interrupt:
			bStartNow = Current->bInterruptible;
			break;
		case EDocSequenceArbitration::ReplaceLowerPriority:
			bStartNow = Current->bInterruptible && Current->Priority < Definition->Priority;
			break;
		case EDocSequenceArbitration::Queue:
			break;
		}
		if (bStartNow)
		{
			EndActive(EDocSequenceState::Interrupted,
				FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, FString::Printf(TEXT("Interrupted by %s"), *Definition->SequenceTag.ToString())),
				Current->bRestoreStateOnFinish);
		}
	}

	if (bStartNow)
	{
		const int64 Id = Session->Id;
		StartSession(MoveTemp(Session));
		const FSession* Started = FindSession(MakeHandle(Id));
		Info.State = Started ? Started->State : GetSessionState(Info.Session);
		Info.Result = FDocSystemResult::MakeSuccess();
		FlushEvents();
		return Info;
	}

	const int32 MaxQueue = GetDefault<UDocSequencesSettings>()->MaxQueueLength;
	if (Queue.Num() >= MaxQueue)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Sequence queue is full"));
		Info.Session = FDocRequestHandle();
		FlushEvents();
		return Info;
	}
	Session->QueueOrder = NextQueueOrder++;
	SetState(*Session, EDocSequenceState::Queued);
	Queue.Add(MoveTemp(Session));
	// Deterministic order: higher priority first, then first come first served.
	Queue.Sort([](const TUniquePtr<FSession>& A, const TUniquePtr<FSession>& B)
	{
		if (A->Definition->Priority != B->Definition->Priority) { return A->Definition->Priority > B->Definition->Priority; }
		return A->QueueOrder < B->QueueOrder;
	});
	Info.State = EDocSequenceState::Queued;
	Info.Result = FDocSystemResult::MakeSuccess();
	FlushEvents();
	return Info;
}

FDocSystemResult UDocSequenceSubsystem::PauseSequence(FDocRequestHandle Handle)
{
	FSession* Session = FindSession(Handle);
	if (!Session || Session != Active.Get() || !Session->bStarted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Session is not playing"));
	}
	if (Session->bPausedByUser)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Session->bPausedByUser = true;
	if (!Session->bPausedForParticipant)
	{
		Backend->Pause(Session->Id);
	}
	SetState(*Session, EDocSequenceState::Paused);
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSequenceSubsystem::ResumeSequence(FDocRequestHandle Handle)
{
	FSession* Session = FindSession(Handle);
	if (!Session || Session != Active.Get() || !Session->bStarted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Session is not playing"));
	}
	if (!Session->bPausedByUser)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Session->bPausedByUser = false;
	if (Session->bPausedForParticipant)
	{
		FDocSystemResult Waiting = FDocSystemResult::MakeSuccess();
		Waiting.Diagnostic = TEXT("Still waiting for a participant");
		return Waiting;
	}
	Backend->Resume(Session->Id);
	SetState(*Session, EDocSequenceState::Playing);
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSequenceSubsystem::StopSequence(FDocRequestHandle Handle)
{
	FSession* Session = FindSession(Handle);
	if (!Session)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Session already ended or unknown"));
	}
	const FDocSystemResult Stopped = FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Stopped"));
	if (Session == Active.Get())
	{
		EndActive(EDocSequenceState::Interrupted, Stopped, Session->Definition->bRestoreStateOnFinish);
	}
	else
	{
		CancelQueued(Queue.IndexOfByPredicate([Session](const TUniquePtr<FSession>& Q) { return Q.Get() == Session; }), Stopped);
	}
	PumpQueue();
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSequenceSubsystem::SkipSequence(FDocRequestHandle Handle)
{
	FSession* Session = FindSession(Handle);
	if (!Session || Session != Active.Get())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Only the active session can be skipped"));
	}
	const UDocSequenceDefinition* Definition = Session->Definition;
	if (!Definition->bCanSkip)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Sequence cannot be skipped"));
	}
	bool bRestore = Definition->bRestoreStateOnFinish;
	switch (Definition->SkipPolicy)
	{
	case EDocSequenceSkipPolicy::FinishAtEndState:
		CommitRemainingOnSkip(*Session);
		if (Session->bStarted) { Backend->JumpToEndAndStop(Session->Id); }
		break;
	case EDocSequenceSkipPolicy::CancelAndRestore:
		bRestore = true;
		break;
	case EDocSequenceSkipPolicy::Reconcile:
		CommitRemainingOnSkip(*Session);
		break;
	}
	EndActive(EDocSequenceState::Skipped, FDocSystemResult::MakeSuccess(), bRestore);
	PumpQueue();
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

FDocSequencePlayParams UDocSequenceSubsystem::RebuildParams(const FSession& Session) const
{
	FDocSequencePlayParams Params;
	Params.Owner = Session.bOwnerIsSubsystem ? nullptr : Session.Owner.Get();
	for (const TPair<FGameplayTag, TArray<TWeakObjectPtr<AActor>>>& Supplied : Session.SuppliedBindings)
	{
		FDocSequenceBinding Binding;
		Binding.Role = Supplied.Key;
		for (const TWeakObjectPtr<AActor>& Actor : Supplied.Value) { if (Actor.IsValid()) { Binding.Actors.Add(Actor.Get()); } }
		Params.Bindings.Add(Binding);
	}
	for (const TWeakObjectPtr<ULocalPlayer>& Player : Session.Players) { if (Player.IsValid()) { Params.Players.Add(Player.Get()); } }
	return Params;
}

FDocSequenceRequestInfo UDocSequenceSubsystem::RestartSequence(FDocRequestHandle Handle)
{
	FSession* Session = FindSession(Handle);
	if (!Session)
	{
		FDocSequenceRequestInfo Info;
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
		return Info;
	}
	UDocSequenceDefinition* Definition = Session->Definition;
	const FDocSequencePlayParams Params = RebuildParams(*Session);
	const FDocSystemResult Restarted = FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Restarted"));
	if (Session == Active.Get())
	{
		EndActive(EDocSequenceState::Interrupted, Restarted, Definition->bRestoreStateOnFinish);
	}
	else
	{
		CancelQueued(Queue.IndexOfByPredicate([Session](const TUniquePtr<FSession>& Q) { return Q.Get() == Session; }), Restarted);
	}
	return PlaySequence(Definition, Params); // replay is always a new session id
}

FDocSystemResult UDocSequenceSubsystem::JumpToMarker(FDocRequestHandle Handle, const FString& MarkerLabel)
{
	FSession* Session = FindSession(Handle);
	if (!Session || Session != Active.Get() || !Session->bStarted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Session is not playing"));
	}
	const float From = Session->LastPosition;
	if (!Backend->JumpToMarker(Session->Id, MarkerLabel))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No marked frame '%s'"), *MarkerLabel));
	}
	const float To = Backend->GetPosition(Session->Id);
	// Only effects that opted in are executed when seeking past them.
	for (const FDocSequenceEffect& Effect : Session->Definition->Effects)
	{
		if (Effect.bCommitWhenSeekedPast && Effect.TriggerTimeSeconds >= 0.f && Effect.TriggerTimeSeconds > From && Effect.TriggerTimeSeconds <= To)
		{
			FireEffect(*Session, Effect);
		}
	}
	Session->LastPosition = To;
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

bool UDocSequenceSubsystem::IsSequencePlaying(FGameplayTag SequenceTag) const
{
	return Active && Active->Definition->SequenceTag == SequenceTag && Active->bStarted;
}

EDocSequenceState UDocSequenceSubsystem::GetSessionState(FDocRequestHandle Handle) const
{
	if (const FSession* Session = FindSession(Handle))
	{
		return Session->State;
	}
	for (const FDocSequenceSessionInfo& Info : FinishedHistory)
	{
		if (Info.Session == Handle) { return Info.State; }
	}
	return EDocSequenceState::None;
}

bool UDocSequenceSubsystem::GetSessionInfo(FDocRequestHandle Handle, FDocSequenceSessionInfo& OutInfo) const
{
	if (const FSession* Session = FindSession(Handle))
	{
		OutInfo = MakeInfo(*Session);
		return true;
	}
	for (const FDocSequenceSessionInfo& Info : FinishedHistory)
	{
		if (Info.Session == Handle) { OutInfo = Info; return true; }
	}
	return false;
}

FDocRequestHandle UDocSequenceSubsystem::GetActiveSession() const
{
	return Active ? MakeHandle(Active->Id) : FDocRequestHandle();
}

FDocSystemResult UDocSequenceSubsystem::NotifySequenceEvent(FGameplayTag EventTag)
{
	if (!Active || !Active->bStarted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No playing session"));
	}
	bool bAny = false;
	bool bFiredNew = false;
	for (const FDocSequenceEffect& Effect : Active->Definition->Effects)
	{
		if (Effect.EventTag.IsValid() && Effect.EventTag == EventTag)
		{
			bAny = true;
			const bool bWasFired = Active->FiredEffects.Contains(Effect.EffectId);
			FireEffect(*Active, Effect);
			bFiredNew |= !bWasFired;
		}
	}
	FlushEvents();
	if (!bAny)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No effect mapped to %s"), *EventTag.ToString()));
	}
	return bFiredNew ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("Already fired in this session"));
}

// ---------------------------------------------------------------------------
// Session pipeline
// ---------------------------------------------------------------------------

void UDocSequenceSubsystem::SetState(FSession& Session, EDocSequenceState NewState)
{
	Session.State = NewState;
	Session.StateEnteredAt = Clock;
	StateEvents.Add(FQueuedState{ MakeHandle(Session.Id), Session.Definition ? Session.Definition->SequenceTag : FGameplayTag(), NewState });
}

void UDocSequenceSubsystem::StartSession(TUniquePtr<FSession> Session)
{
	check(!Active);
	Active = MoveTemp(Session);
	BeginLoad(*Active);
}

void UDocSequenceSubsystem::BeginLoad(FSession& Session)
{
	SetState(Session, EDocSequenceState::Loading);
	const float Timeout = GetDefault<UDocSequencesSettings>()->LoadTimeoutSeconds;
	Session.WaitDeadline = Timeout > 0.f ? Clock + Timeout : 0.0;
	const TSoftObjectPtr<ULevelSequence>& Soft = Session.Definition->Sequence;
	if (Soft.IsNull())
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Definition has no Level Sequence")), false);
		return;
	}
	const int64 Id = Session.Id;
	if (Soft.Get())
	{
		OnLoaded(Id);
		return;
	}
	TWeakObjectPtr<UDocSequenceSubsystem> WeakThis(this);
	auto Done = [WeakThis, Id]()
	{
		if (UDocSequenceSubsystem* Self = WeakThis.Get())
		{
			Self->OnLoaded(Id);
			Self->FlushEvents();
		}
	};
	if (LoaderOverride)
	{
		LoaderOverride(Soft.ToSoftObjectPath(), Done);
		return;
	}
	TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(Soft.ToSoftObjectPath(), FStreamableDelegate::CreateLambda(Done));
	if (Active && Active->Id == Id)
	{
		Active->LoadHandle = Handle;
	}
}

void UDocSequenceSubsystem::OnLoaded(int64 SessionId)
{
	if (!Active || Active->Id != SessionId || Active->State != EDocSequenceState::Loading)
	{
		return; // stale completion (the session ended or was replaced)
	}
	ULevelSequence* Sequence = Active->Definition->Sequence.Get();
	if (!Sequence)
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
			FString::Printf(TEXT("Level Sequence %s failed to load"), *Active->Definition->Sequence.ToString())), false);
		return;
	}
	Active->LoadedSequence = Sequence;
	Active->LoadHandle.Reset();
	BeginPrerequisites(*Active);
}

void UDocSequenceSubsystem::BeginPrerequisites(FSession& Session)
{
	const UDocSequenceDefinition* Definition = Session.Definition;
	Session.WaitDeadline = Definition->PrerequisiteTimeoutSeconds > 0.f ? Clock + Definition->PrerequisiteTimeoutSeconds : 0.0;
	Session.PendingPrerequisites = 1; // guard against synchronous completion
	Session.bPrerequisiteFailed = false;
	const int64 Id = Session.Id;
	for (const FGameplayTag& Prerequisite : Definition->Prerequisites)
	{
		UObject* ProviderObject = nullptr;
		for (const TWeakObjectPtr<UObject>& Candidate : PrerequisiteProviders)
		{
			const IDocSequencePrerequisiteProvider* Provider = Cast<IDocSequencePrerequisiteProvider>(Candidate.Get());
			if (Provider && Provider->HandlesDocSequencePrerequisite(Prerequisite))
			{
				ProviderObject = Candidate.Get();
				break;
			}
		}
		if (!ProviderObject)
		{
			EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
				FString::Printf(TEXT("No provider for prerequisite %s (bridge not installed?)"), *Prerequisite.ToString())), false);
			return;
		}
		Session.PrerequisiteProviders.AddUnique(ProviderObject);
		++Session.PendingPrerequisites;
		TWeakObjectPtr<UDocSequenceSubsystem> WeakThis(this);
		Cast<IDocSequencePrerequisiteProvider>(ProviderObject)->AcquireDocSequencePrerequisite(Prerequisite, Id, [WeakThis, Id](bool bReady)
		{
			UDocSequenceSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->Active || Self->Active->Id != Id)
			{
				return; // same cancellation generation as the session: stale readiness is ignored
			}
			Self->Active->bPrerequisiteFailed |= !bReady;
			if (--Self->Active->PendingPrerequisites == 0)
			{
				Self->ContinueAfterPrerequisites(*Self->Active);
				Self->FlushEvents();
			}
		});
		if (!Active || Active->Id != Id)
		{
			return;
		}
	}
	if (--Session.PendingPrerequisites == 0)
	{
		ContinueAfterPrerequisites(Session);
	}
}

void UDocSequenceSubsystem::ContinueAfterPrerequisites(FSession& Session)
{
	if (Session.bPrerequisiteFailed)
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("A prerequisite could not be satisfied")), false);
		return;
	}
	SetState(Session, EDocSequenceState::ResolvingBindings);
	FString Missing;
	if (ResolveBindings(Session, Missing))
	{
		BeginPlayback(Session);
		return;
	}
	// Missing required role: Fail now, or wait with the longest applicable timeout.
	float Wait = -1.f;
	for (const FBoundRole& Role : Session.Roles)
	{
		if (Role.Actors.Num() == 0 && Role.Role.bRequired)
		{
			if (Role.Role.MissingPolicy == EDocMissingParticipantPolicy::Fail)
			{
				Wait = -1.f;
				break;
			}
			if (Role.Role.MissingPolicy == EDocMissingParticipantPolicy::WaitWithTimeout)
			{
				Wait = FMath::Max(Wait, Role.Role.WaitTimeoutSeconds);
			}
		}
	}
	if (Wait < 0.f)
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Missing participant(s): %s"), *Missing)), false);
		return;
	}
	Session.WaitDeadline = Clock + Wait;
}

bool UDocSequenceSubsystem::ResolveBindings(FSession& Session, FString& OutMissing)
{
	TArray<FString> Missing;
	Session.Roles.Reset();
	for (const FDocSequenceRole& RoleDef : Session.Definition->Roles)
	{
		FBoundRole Bound;
		Bound.Role = RoleDef;
		// 1) Explicitly supplied objects.
		for (const TPair<FGameplayTag, TArray<TWeakObjectPtr<AActor>>>& Supplied : Session.SuppliedBindings)
		{
			if (Supplied.Key == RoleDef.Role)
			{
				for (const TWeakObjectPtr<AActor>& Actor : Supplied.Value)
				{
					if (Actor.IsValid() && !Actor->IsActorBeingDestroyed() && Bound.Actors.Num() < RoleDef.MaxActors) { Bound.Actors.Add(Actor); }
				}
			}
		}
		// 2) Registered participants advertising the role (registration order; no label searches).
		if (Bound.Actors.Num() == 0)
		{
			for (const TWeakObjectPtr<UObject>& Weak : Participants)
			{
				UObject* Participant = Weak.Get();
				if (!Participant || Bound.Actors.Num() >= RoleDef.MaxActors)
				{
					continue;
				}
				AActor* Actor = Cast<AActor>(Participant);
				if (!Actor)
				{
					if (const UActorComponent* Component = Cast<UActorComponent>(Participant)) { Actor = Component->GetOwner(); }
				}
				if (Actor && !Actor->IsActorBeingDestroyed()
					&& IDocSequenceParticipant::Execute_GetDocSequenceRoles(Participant).HasTagExact(RoleDef.Role))
				{
					Bound.Actors.AddUnique(Actor);
				}
			}
		}
		// Participant objects (actor or its components) for callbacks.
		for (const TWeakObjectPtr<AActor>& Actor : Bound.Actors)
		{
			if (Actor->Implements<UDocSequenceParticipant>()) { Bound.Participants.Add(Actor.Get()); }
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component && Component->Implements<UDocSequenceParticipant>()) { Bound.Participants.Add(Component); }
			}
		}
		if (Bound.Actors.Num() == 0 && RoleDef.bRequired && RoleDef.MissingPolicy != EDocMissingParticipantPolicy::MissingOptional)
		{
			Missing.Add(RoleDef.Role.ToString());
		}
		Session.Roles.Add(MoveTemp(Bound));
	}
	OutMissing = FString::Join(Missing, TEXT(", "));
	return Missing.Num() == 0;
}

TMap<FName, TArray<AActor*>> UDocSequenceSubsystem::BuildBindingMap(const FSession& Session) const
{
	TMap<FName, TArray<AActor*>> Map;
	for (const FBoundRole& Role : Session.Roles)
	{
		TArray<AActor*>& Actors = Map.FindOrAdd(Role.Role.BindingTag);
		for (const TWeakObjectPtr<AActor>& Actor : Role.Actors)
		{
			if (Actor.IsValid()) { Actors.Add(Actor.Get()); }
		}
	}
	return Map;
}

void UDocSequenceSubsystem::HookParticipants(FSession& Session)
{
	const bool bControlAI = Session.Definition->RequestedControl.HasTag(DocSequenceTags::Control_AI);
	const FDocRequestHandle Handle = MakeHandle(Session.Id);
	for (const FBoundRole& Role : Session.Roles)
	{
		for (const TWeakObjectPtr<UObject>& Weak : Role.Participants)
		{
			UObject* Participant = Weak.Get();
			if (!Participant || Session.Hooked.ContainsByPredicate([Participant](const TPair<TWeakObjectPtr<UObject>, FGameplayTag>& H) { return H.Key.Get() == Participant; }))
			{
				continue;
			}
			Session.Hooked.Add(TPair<TWeakObjectPtr<UObject>, FGameplayTag>(Participant, Role.Role.Role));
			IDocSequenceParticipant::Execute_OnDocSequenceBound(Participant, Handle, Role.Role.Role);
			if (bControlAI)
			{
				IDocSequenceParticipant::Execute_SetDocSequenceAIControlled(Participant, Handle, true);
				Session.AIControlled.Add(Participant);
			}
		}
	}
}

void UDocSequenceSubsystem::AcquireControl(FSession& Session, FString& OutProblem)
{
	FGameplayTagContainer Capabilities;
	for (const FGameplayTag& Tag : Session.Definition->RequestedControl)
	{
		if (!Tag.MatchesTag(DocSequenceTags::Sequence))
		{
			Capabilities.AddTag(Tag); // Control.* capabilities go to the players' control providers
		}
	}
	if (Capabilities.IsEmpty())
	{
		return;
	}
	TArray<ULocalPlayer*> Players;
	for (const TWeakObjectPtr<ULocalPlayer>& Player : Session.Players) { if (Player.IsValid()) { Players.Add(Player.Get()); } }
	if (Players.Num() == 0)
	{
		const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (GI) { Players = GI->GetLocalPlayers(); }
	}
	TArray<FString> Problems;
	for (ULocalPlayer* Player : Players)
	{
		UDocPlayerControlSubsystem* Control = UDocPlayerControlSubsystem::Get(Player);
		if (!Control || !Control->HasControlProvider())
		{
			Problems.Add(FString::Printf(TEXT("%s has no control provider"), *GetNameSafe(Player)));
			continue;
		}
		FDocControlClaimRequest Request;
		Request.LocalPlayer = Player;
		Request.Owner = this;
		Request.Capabilities = Capabilities;
		Request.Priority = Session.Definition->ControlPriority;
		Request.DebugReason = FString::Printf(TEXT("Sequence %s"), *Session.Definition->SequenceTag.ToString());
		FDocRequestHandle Claim;
		const FDocSystemResult Result = Control->AcquireControl(Request, Claim);
		if (Result.IsSuccess() && Claim.IsSet())
		{
			Session.ControlClaims.Add(TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>(Player, Claim));
		}
		else
		{
			Problems.Add(Result.ToString());
		}
	}
	OutProblem = FString::Join(Problems, TEXT("; "));
}

void UDocSequenceSubsystem::BeginPlayback(FSession& Session)
{
	SetState(Session, EDocSequenceState::Preparing);
	FString ControlProblem;
	AcquireControl(Session, ControlProblem);
	if (!ControlProblem.IsEmpty())
	{
		if (GetDefault<UDocSequencesSettings>()->bRequireControlProvider)
		{
			EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, ControlProblem), false);
			return;
		}
		UE_LOG(LogDocSequences, Log, TEXT("%s: playing without some control claims: %s"), *Session.Definition->SequenceTag.ToString(), *ControlProblem);
	}
	HookParticipants(Session);

	IDocSequencePlaybackBackend::FStartParams Params;
	Params.World = GetWorld();
	Params.Sequence = Session.LoadedSequence;
	Params.Bindings = BuildBindingMap(Session);
	Params.bRestoreStateOnFinish = Session.Definition->bRestoreStateOnFinish;
	FString Error;
	if (!Backend.IsValid() || !Backend->Start(Session.Id, Params, Error))
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, Error.IsEmpty() ? FString(TEXT("No playback backend")) : Error), false);
		return;
	}
	Session.bStarted = true;
	Session.LastPosition = 0.f;
	SetState(Session, EDocSequenceState::Playing);
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

FDocEffectKey UDocSequenceSubsystem::MakeEffectKey(const FSession& Session, const FDocSequenceEffect& Effect) const
{
	const UWorld* World = GetWorld();
	const FString WorldName = World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
	FDocEffectKey Key;
	Key.Owner = FDocOwnerScope(EDocOwnerScopeKind::SharedWorld, FGuid::NewDeterministicGuid(WorldName));
	Key.ProducerInstanceId = Effect.bIrreversible
		? FGuid::NewDeterministicGuid(FString::Printf(TEXT("DocSequence:%s"), *Session.Definition->SequenceTag.ToString()))
		: Session.Guid;
	Key.ActionId = Effect.EffectId;
	return Key;
}

void UDocSequenceSubsystem::FireEffect(FSession& Session, const FDocSequenceEffect& Effect)
{
	if (Effect.EffectId.IsNone() || Session.FiredEffects.Contains(Effect.EffectId))
	{
		return;
	}
	Session.FiredEffects.Add(Effect.EffectId);
	const bool bAuthoritative = Effect.Class == EDocSequenceEffectClass::Authoritative;
	if (bAuthoritative && !HasAuthority())
	{
		return; // a client's playback (or skip) never mutates authoritative state
	}
	if (Effect.bIrreversible)
	{
		const FDocEffectKey Key = MakeEffectKey(Session, Effect);
		if (Ledger.Check(Key, 0) != EDocReceiptCheck::New)
		{
			return; // already applied by an earlier session (replay, skip, seek or late join)
		}
		FDocEffectReceipt Receipt;
		Receipt.Key = Key;
		Receipt.Result = FDocSystemResult::MakeSuccess();
		Ledger.Record(Receipt);
	}
	EffectEvents.Add(FQueuedEffect{ MakeHandle(Session.Id), Effect.EffectId, Effect.EventTag, bAuthoritative });
}

void UDocSequenceSubsystem::FireCrossedEffects(FSession& Session, float From, float To)
{
	for (const FDocSequenceEffect& Effect : Session.Definition->Effects)
	{
		if (Effect.TriggerTimeSeconds >= 0.f && Effect.TriggerTimeSeconds >= From && Effect.TriggerTimeSeconds <= To)
		{
			FireEffect(Session, Effect);
		}
	}
}

void UDocSequenceSubsystem::CommitRemainingOnSkip(FSession& Session)
{
	const UDocSequenceDefinition* Definition = Session.Definition;
	TArray<FName> NotYetFired;
	for (const FDocSequenceEffect& Effect : Definition->Effects)
	{
		if (!Session.FiredEffects.Contains(Effect.EffectId)) { NotYetFired.Add(Effect.EffectId); }
	}
	TArray<FName> ToCommit;
	if (Definition->SkipPolicy == EDocSequenceSkipPolicy::Reconcile && OnReconcileSkip)
	{
		ToCommit = OnReconcileSkip(Definition, NotYetFired);
	}
	else
	{
		for (const FDocSequenceEffect& Effect : Definition->Effects)
		{
			if (Effect.bCommitOnSkip && NotYetFired.Contains(Effect.EffectId)) { ToCommit.Add(Effect.EffectId); }
		}
	}
	for (const FDocSequenceEffect& Effect : Definition->Effects)
	{
		if (ToCommit.Contains(Effect.EffectId) && NotYetFired.Contains(Effect.EffectId))
		{
			FireEffect(Session, Effect);
		}
	}
}

// ---------------------------------------------------------------------------
// Ending
// ---------------------------------------------------------------------------

void UDocSequenceSubsystem::EndActive(EDocSequenceState Terminal, const FDocSystemResult& Result, bool bRestoreState)
{
	if (!Active || bInEndActive)
	{
		return;
	}
	TGuardValue<bool> Guard(bInEndActive, true);
	TUniquePtr<FSession> Session = MoveTemp(Active);
	Session->Terminal = Terminal;
	Session->Result = Result;
	SetState(*Session, Terminal);
	if (Terminal == EDocSequenceState::Completed)
	{
		CompletedThisWorld.Add(Session->Definition->SequenceTag);
		if (IDocSequenceHistoryProvider* History = Cast<IDocSequenceHistoryProvider>(HistoryProvider.Get()))
		{
			History->RecordDocSequenceCompleted(Session->Definition->SequenceTag);
		}
	}
	RestoreAndFinish(*Session, bRestoreState);
	FinishEvents.Add(FQueuedFinish{ MakeHandle(Session->Id), Terminal, Result });
	RecordFinished(*Session);
}

void UDocSequenceSubsystem::RestoreAndFinish(FSession& Session, bool bRestoreState)
{
	SetState(Session, EDocSequenceState::Restoring);
	if (Session.LoadHandle.IsValid())
	{
		Session.LoadHandle->CancelHandle();
		Session.LoadHandle.Reset();
	}
	if (Session.bStarted && Backend.IsValid())
	{
		Backend->Stop(Session.Id, bRestoreState);
	}
	// Release only what this session acquired; the provider recomputes the effective owner.
	for (const TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>& Claim : Session.ControlClaims)
	{
		if (UDocPlayerControlSubsystem* Control = UDocPlayerControlSubsystem::Get(Claim.Key.Get()))
		{
			Control->ReleaseControl(Claim.Value);
		}
	}
	Session.ControlClaims.Reset();
	const FDocRequestHandle Handle = MakeHandle(Session.Id);
	for (const TWeakObjectPtr<UObject>& Weak : Session.AIControlled)
	{
		if (UObject* Participant = Weak.Get())
		{
			IDocSequenceParticipant::Execute_SetDocSequenceAIControlled(Participant, Handle, false);
		}
	}
	Session.AIControlled.Reset();
	for (const TPair<TWeakObjectPtr<UObject>, FGameplayTag>& Hooked : Session.Hooked)
	{
		if (UObject* Participant = Hooked.Key.Get())
		{
			IDocSequenceParticipant::Execute_OnDocSequenceUnbound(Participant, Handle, Hooked.Value, Session.Terminal);
		}
	}
	Session.Hooked.Reset();
	for (const TWeakObjectPtr<UObject>& Weak : Session.PrerequisiteProviders)
	{
		if (IDocSequencePrerequisiteProvider* Provider = Cast<IDocSequencePrerequisiteProvider>(Weak.Get()))
		{
			Provider->ReleaseDocSequencePrerequisites(Session.Id);
		}
	}
	Session.PrerequisiteProviders.Reset();
	SetState(Session, EDocSequenceState::Finished);
}

void UDocSequenceSubsystem::CancelQueued(int32 Index, const FDocSystemResult& Result)
{
	if (!Queue.IsValidIndex(Index))
	{
		return;
	}
	TUniquePtr<FSession> Session = MoveTemp(Queue[Index]);
	Queue.RemoveAt(Index);
	Session->Terminal = EDocSequenceState::Interrupted;
	Session->Result = Result;
	SetState(*Session, EDocSequenceState::Interrupted);
	SetState(*Session, EDocSequenceState::Finished);
	FinishEvents.Add(FQueuedFinish{ MakeHandle(Session->Id), EDocSequenceState::Interrupted, Result });
	RecordFinished(*Session);
}

void UDocSequenceSubsystem::PumpQueue()
{
	while (!Active && Queue.Num() > 0 && !bDeinitializing)
	{
		TUniquePtr<FSession> Next = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		if (!Next->Owner.IsValid())
		{
			Next->Terminal = EDocSequenceState::Interrupted;
			Next->Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Owner destroyed while queued"));
			SetState(*Next, EDocSequenceState::Interrupted);
			SetState(*Next, EDocSequenceState::Finished);
			FinishEvents.Add(FQueuedFinish{ MakeHandle(Next->Id), EDocSequenceState::Interrupted, Next->Result });
			RecordFinished(*Next);
			continue;
		}
		StartSession(MoveTemp(Next));
	}
}

void UDocSequenceSubsystem::RecordFinished(const FSession& Session)
{
	FinishedHistory.Add(MakeInfo(Session));
	const int32 Max = GetDefault<UDocSequencesSettings>()->FinishedSessionHistory;
	if (FinishedHistory.Num() > Max)
	{
		FinishedHistory.RemoveAt(0, FinishedHistory.Num() - Max);
	}
}

FDocSequenceSessionInfo UDocSequenceSubsystem::MakeInfo(const FSession& Session) const
{
	FDocSequenceSessionInfo Info;
	Info.Session = MakeHandle(Session.Id);
	Info.SequenceTag = Session.Definition ? Session.Definition->SequenceTag : FGameplayTag();
	Info.State = Session.State;
	Info.Terminal = Session.Terminal;
	Info.Result = Session.Result;
	if (Session.bStarted && Backend.IsValid() && Active.Get() == &Session)
	{
		Info.PositionSeconds = Backend->GetPosition(Session.Id);
		Info.DurationSeconds = Backend->GetDuration(Session.Id);
	}
	Info.QueuePosition = Queue.IndexOfByPredicate([&Session](const TUniquePtr<FSession>& Q) { return Q.Get() == &Session; });
	Info.FiredEffects = Session.FiredEffects.Array();
	return Info;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

bool UDocSequenceSubsystem::CheckParticipants(FSession& Session)
{
	bool bLost = false;
	bool bFail = false;
	float Timeout = 0.f;
	FString LostRoles;
	for (FBoundRole& Role : Session.Roles)
	{
		Role.Actors.RemoveAll([](const TWeakObjectPtr<AActor>& A) { return !A.IsValid() || A->IsActorBeingDestroyed(); });
		if (Role.Actors.Num() == 0 && Role.Role.bRequired && Role.Role.MissingPolicy != EDocMissingParticipantPolicy::MissingOptional)
		{
			bLost = true;
			bFail |= Role.Role.MissingPolicy == EDocMissingParticipantPolicy::Fail;
			Timeout = FMath::Max(Timeout, Role.Role.WaitTimeoutSeconds);
			LostRoles += Role.Role.Role.ToString() + TEXT(" ");
		}
	}
	if (!bLost)
	{
		return true;
	}
	if (bFail)
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Participant lost during playback: %s"), *LostRoles)),
			Session.Definition->bRestoreStateOnFinish);
		return false;
	}
	if (!Session.bPausedForParticipant)
	{
		Session.bPausedForParticipant = true;
		Session.WaitDeadline = Clock + Timeout;
		Backend->Pause(Session.Id);
		SetState(Session, EDocSequenceState::Paused);
		return true;
	}
	FString Missing;
	if (ResolveBindings(Session, Missing))
	{
		Backend->Rebind(Session.Id, BuildBindingMap(Session));
		HookParticipants(Session);
		Session.bPausedForParticipant = false;
		if (!Session.bPausedByUser)
		{
			Backend->Resume(Session.Id);
			SetState(Session, EDocSequenceState::Playing);
		}
		return true;
	}
	if (Clock > Session.WaitDeadline)
	{
		EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, FString::Printf(TEXT("Participant did not return: %s"), *Missing)),
			Session.Definition->bRestoreStateOnFinish);
		return false;
	}
	return true;
}

void UDocSequenceSubsystem::TickSessions(float DeltaSeconds)
{
	if (bDeinitializing)
	{
		return;
	}
	Clock += FMath::Max(0.f, DeltaSeconds);

	for (int32 i = Queue.Num() - 1; i >= 0; --i)
	{
		if (!Queue[i]->Owner.IsValid())
		{
			CancelQueued(i, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Owner destroyed while queued")));
		}
	}

	if (Active)
	{
		FSession& Session = *Active;
		if (!Session.Owner.IsValid())
		{
			EndActive(EDocSequenceState::Interrupted, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Owner destroyed")), Session.Definition->bRestoreStateOnFinish);
		}
		else if (Session.State == EDocSequenceState::Loading)
		{
			if (Session.WaitDeadline > 0.0 && Clock > Session.WaitDeadline)
			{
				EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut,
					Session.LoadedSequence ? TEXT("Prerequisites timed out") : TEXT("Level Sequence load timed out")), false);
			}
		}
		else if (Session.State == EDocSequenceState::ResolvingBindings)
		{
			FString Missing;
			if (ResolveBindings(Session, Missing))
			{
				BeginPlayback(Session);
			}
			else if (Clock > Session.WaitDeadline)
			{
				EndActive(EDocSequenceState::Failed, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, FString::Printf(TEXT("Missing participant(s): %s"), *Missing)), false);
			}
		}
		else if (Session.bStarted && (Session.State == EDocSequenceState::Playing || Session.State == EDocSequenceState::Paused))
		{
			if (CheckParticipants(Session) && Active.Get() == &Session && !Session.bPausedByUser && !Session.bPausedForParticipant)
			{
				const float Position = Backend->GetPosition(Session.Id);
				if (Position > Session.LastPosition)
				{
					FireCrossedEffects(Session, Session.LastPosition, Position);
					Session.LastPosition = Position;
				}
				if (Backend->IsFinished(Session.Id))
				{
					FireCrossedEffects(Session, Session.LastPosition, Backend->GetDuration(Session.Id) + KINDA_SMALL_NUMBER);
					EndActive(EDocSequenceState::Completed, FDocSystemResult::MakeSuccess(), Session.Definition->bRestoreStateOnFinish);
				}
			}
		}
	}
	PumpQueue();
	FlushEvents();
}

void UDocSequenceSubsystem::FlushEvents()
{
	if (bFlushing)
	{
		return;
	}
	TGuardValue<bool> Guard(bFlushing, true);
	// Order: state changes, effects, then finish notifications; loop until handlers stop queueing.
	while (StateEvents.Num() || EffectEvents.Num() || FinishEvents.Num())
	{
		TArray<FQueuedState> States = MoveTemp(StateEvents);
		TArray<FQueuedEffect> Effects = MoveTemp(EffectEvents);
		TArray<FQueuedFinish> Finishes = MoveTemp(FinishEvents);
		for (const FQueuedState& E : States)
		{
			OnSessionStateChangedNative.Broadcast(E.Session, E.Tag, E.State);
			OnSessionStateChanged.Broadcast(E.Session, E.Tag, E.State);
		}
		for (const FQueuedEffect& E : Effects)
		{
			OnSequenceEffectNative.Broadcast(E.Session, E.EffectId, E.EventTag, E.bAuthoritative);
			OnSequenceEffect.Broadcast(E.Session, E.EffectId, E.EventTag, E.bAuthoritative);
		}
		for (const FQueuedFinish& E : Finishes)
		{
			OnSessionFinishedNative.Broadcast(E.Session, E.Terminal, E.Result);
			OnSessionFinished.Broadcast(E.Session, E.Terminal, E.Result);
		}
	}
}

// ---------------------------------------------------------------------------
// Participant component
// ---------------------------------------------------------------------------

void UDocSequenceParticipantComponent::SetDocSequenceAIControlled_Implementation(FDocRequestHandle Session, bool bControlled)
{
	const int32 Before = AIControlCount;
	AIControlCount = FMath::Max(0, AIControlCount + (bControlled ? 1 : -1));
	if ((Before == 0) != (AIControlCount == 0))
	{
		OnAIControlChanged.Broadcast(Session, AIControlCount > 0);
	}
}

void UDocSequenceParticipantComponent::OnRegister()
{
	Super::OnRegister();
	if (!IsTemplate())
	{
		if (UDocSequenceSubsystem* Sequences = UDocSequenceSubsystem::Get(this))
		{
			Sequences->RegisterParticipant(this);
		}
	}
}

void UDocSequenceParticipantComponent::OnUnregister()
{
	if (!IsTemplate())
	{
		if (UDocSequenceSubsystem* Sequences = UDocSequenceSubsystem::Get(this))
		{
			Sequences->UnregisterParticipant(this);
		}
	}
	Super::OnUnregister();
}

void UDocSequenceParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocSequenceSubsystem* Sequences = UDocSequenceSubsystem::Get(this))
	{
		Sequences->RegisterParticipant(this);
	}
}

void UDocSequenceParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocSequenceSubsystem* Sequences = UDocSequenceSubsystem::Get(this))
	{
		Sequences->UnregisterParticipant(this);
	}
	Super::EndPlay(EndPlayReason);
}
