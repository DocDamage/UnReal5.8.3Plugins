#include "DocDialogueSubsystem.h"
#include "DocDialogueLog.h"
#include "DocCoreBlueprintLibrary.h"
#include "DocCoreTags.h"
#include "DocGameplayContext.h"
#include "Interfaces/DocGameplayTagProvider.h"
#include "Algo/Sort.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocDialogueSubsystem)

namespace DocDialoguePrivate
{
	bool IsTerminal(EDocDialogueSessionState State)
	{
		return State == EDocDialogueSessionState::Completed || State == EDocDialogueSessionState::Cancelled
			|| State == EDocDialogueSessionState::Failed || State == EDocDialogueSessionState::TimedOut;
	}

	FString StateName(EDocDialogueSessionState State)
	{
		return StaticEnum<EDocDialogueSessionState>()->GetNameStringByValue(static_cast<int64>(State));
	}

	FDocDialogueValue ZeroOf(EDocDialogueValueType Type)
	{
		switch (Type)
		{
		case EDocDialogueValueType::Bool: return FDocDialogueValue::MakeBool(false);
		case EDocDialogueValueType::Int: return FDocDialogueValue::MakeInt(0);
		case EDocDialogueValueType::Float: return FDocDialogueValue::MakeFloat(0.0);
		case EDocDialogueValueType::Name: return FDocDialogueValue::MakeName(NAME_None);
		default: return FDocDialogueValue();
		}
	}

	bool ImplementsParticipant(const UObject* Object)
	{
		return Object && Object->GetClass()->ImplementsInterface(UDocDialogueParticipant::StaticClass());
	}

	bool IsInternalAction(EDocDialogueActionType Type)
	{
		return Type == EDocDialogueActionType::SetVariable || Type == EDocDialogueActionType::AddVariable;
	}
}

using namespace DocDialoguePrivate;

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocDialogueSubsystem* UDocDialogueSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocDialogueSubsystem>() : nullptr;
}

bool UDocDialogueSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Default: only an already-loaded sound reports a duration. Never loads.
	VoiceDurationResolver = [](const TSoftObjectPtr<USoundBase>& Voice) -> float
	{
		if (const USoundBase* Sound = Voice.Get())
		{
			const float Duration = Sound->GetDuration();
			return (Duration > 0.f && Duration < INDEFINITELY_LOOPING_DURATION) ? Duration : -1.f;
		}
		return -1.f;
	};
}

void UDocDialogueSubsystem::Deinitialize()
{
	for (TPair<FGuid, TUniquePtr<FSession>>& Pair : Sessions)
	{
		if (Pair.Value)
		{
			ReleaseReservations(*Pair.Value);
		}
	}
	Sessions.Reset();
	SessionHandles.Reset();
	PendingEvents.Reset();
	Reservations.Reset();
	RegisteredParticipants.Reset();
	Super::Deinitialize();
}

TStatId UDocDialogueSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocDialogueSubsystem, STATGROUP_Tickables);
}

void UDocDialogueSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDocDialogueSubsystem* This = CastChecked<UDocDialogueSubsystem>(InThis);
	for (TPair<FGuid, TUniquePtr<FSession>>& Pair : This->Sessions)
	{
		if (Pair.Value)
		{
			Collector.AddReferencedObject(Pair.Value->Graph, This);
		}
	}
	Super::AddReferencedObjects(InThis, Collector);
}

bool UDocDialogueSubsystem::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

double UDocDialogueSubsystem::Now(EDocClockDomain Domain) const
{
	if (TestTime.IsSet())
	{
		return TestTime.GetValue();
	}
	switch (Domain)
	{
	case EDocClockDomain::RealTime:
		return FPlatformTime::Seconds();
	case EDocClockDomain::WallClock:
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
	default:
	{
		// Simulation has no DocTime dependency here: world gameplay time (pauses with the world).
		const UWorld* World = GetWorld();
		return World ? World->GetTimeSeconds() : 0.0;
	}
	}
}

const UDocDialogueSettings* UDocDialogueSubsystem::Settings() const
{
	return GetDefault<UDocDialogueSettings>();
}

void UDocDialogueSubsystem::SetTimeForTesting(double Seconds)
{
	TestTime = Seconds;
}

void UDocDialogueSubsystem::AdvanceTimeForTesting(double Seconds)
{
	TestTime = (TestTime.IsSet() ? TestTime.GetValue() : 0.0) + Seconds;
	Tick(0.f);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void UDocDialogueSubsystem::QueueEvent(TFunction<void()> Event)
{
	PendingEvents.Add(MoveTemp(Event));
}

void UDocDialogueSubsystem::LeaveApi()
{
	if (--ApiDepth > 0)
	{
		return;
	}
	++ApiDepth; // nested API calls from handlers only queue
	while (PendingEvents.Num() > 0)
	{
		TArray<TFunction<void()>> Batch = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		for (TFunction<void()>& Event : Batch)
		{
			Event();
		}
	}
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		if (!It->Value || It->Value->bTerminal)
		{
			if (It->Value)
			{
				SessionHandles.Remove(It->Value->Handle, GetWorld());
			}
			It.RemoveCurrent();
		}
	}
	--ApiDepth;
}

void UDocDialogueSubsystem::SetState(FSession& S, EDocDialogueSessionState NewState)
{
	if (S.State == NewState)
	{
		return;
	}
	S.State = NewState;
	if (NewState == EDocDialogueSessionState::Running)
	{
		return; // transient; not observable between API calls
	}
	QueueEvent([WeakThis = TWeakObjectPtr<UDocDialogueSubsystem>(this), Handle = S.Handle, NewState]()
	{
		if (UDocDialogueSubsystem* This = WeakThis.Get())
		{
			This->OnSessionStateChangedNative.Broadcast(Handle, NewState);
			This->OnSessionStateChanged.Broadcast(Handle, NewState);
		}
	});
}

// ---------------------------------------------------------------------------
// Lookup / authorization / memory
// ---------------------------------------------------------------------------

UDocDialogueSubsystem::FSession* UDocDialogueSubsystem::FindSession(const FDocRequestHandle& Handle, FDocSystemResult* OutFailure)
{
	const FGuid* Id = SessionHandles.Find(Handle, GetWorld());
	TUniquePtr<FSession>* Found = Id ? Sessions.Find(*Id) : nullptr;
	if (!Found || !Found->IsValid() || (*Found)->bTerminal)
	{
		if (OutFailure)
		{
			const EDocHandleStatus Status = SessionHandles.Validate(Handle, GetWorld());
			FGameplayTag Tag = DocCoreTags::Error_Handle_Stale;
			if (Status == EDocHandleStatus::Invalid) { Tag = DocCoreTags::Error_Handle_Invalid; }
			else if (Status == EDocHandleStatus::WrongScope) { Tag = DocCoreTags::Error_Handle_WrongScope; }
			*OutFailure = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
				FString::Printf(TEXT("Dialogue session %s is unknown or has ended"), *Handle.ToString()), Tag);
		}
		return nullptr;
	}
	return Found->Get();
}

const UDocDialogueSubsystem::FSession* UDocDialogueSubsystem::FindSession(const FDocRequestHandle& Handle, FDocSystemResult* OutFailure) const
{
	return const_cast<UDocDialogueSubsystem*>(this)->FindSession(Handle, OutFailure);
}

FDocSystemResult UDocDialogueSubsystem::Authorize(const FSession& S, const UObject* Requester, bool bMutation) const
{
	if (bMutation && S.Authority != EDocDialogueAuthorityMode::LocalOnly && !HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied,
			TEXT("This conversation is authority-driven; submit through the transport bridge"), DocDialogueTags::Error_Dialogue_Unauthorized);
	}
	if (!Requester)
	{
		// Trusted host code (no requester object).
		return FDocSystemResult::MakeSuccess();
	}
	const bool bInitiator = S.Initiator.Get() == Requester;
	const bool bAudience = S.Audience.ContainsByPredicate([Requester](const TWeakObjectPtr<UObject>& W) { return W.Get() == Requester; });
	const bool bAllowed = S.Authority == EDocDialogueAuthorityMode::OwnerAuthoritative ? bInitiator : (bInitiator || bAudience);
	if (!bAllowed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied,
			FString::Printf(TEXT("%s is not allowed to drive this conversation"), *GetNameSafe(Requester)), DocDialogueTags::Error_Dialogue_Unauthorized);
	}
	return FDocSystemResult::MakeSuccess();
}

UDocDialogueSubsystem::FMemoryState& UDocDialogueSubsystem::GetOrCreateMemory(const FMemoryKey& Key, int32 GraphVersion)
{
	TUniquePtr<FMemoryState>& Slot = Memories.FindOrAdd(Key);
	if (!Slot)
	{
		Slot = MakeUnique<FMemoryState>(Settings()->MaxReceiptsPerMemory);
		Slot->Data.GraphId = Key.GraphId;
		Slot->Data.Owner = Key.Owner;
		Slot->Data.CampaignEpoch = FGuid::NewGuid();
		Slot->Data.GraphVersion = GraphVersion;
	}
	return *Slot;
}

UDocDialogueSubsystem::FMemoryState* UDocDialogueSubsystem::FindMemory(const FMemoryKey& Key)
{
	TUniquePtr<FMemoryState>* Found = Memories.Find(Key);
	return Found ? Found->Get() : nullptr;
}

const UDocDialogueSubsystem::FMemoryState* UDocDialogueSubsystem::FindMemory(const FMemoryKey& Key) const
{
	const TUniquePtr<FMemoryState>* Found = Memories.Find(Key);
	return Found ? Found->Get() : nullptr;
}

const FDocDialogueNode* UDocDialogueSubsystem::FindNode(const FSession& S, FName NodeId) const
{
	const int32* Index = S.NodeIndex.Find(NodeId);
	return (Index && S.Graph && S.Graph->Nodes.IsValidIndex(*Index)) ? &S.Graph->Nodes[*Index] : nullptr;
}

int32 UDocDialogueSubsystem::GetReservationCount(FName ParticipantId) const
{
	const FReservation* R = Reservations.Find(ParticipantId);
	return R ? R->Sessions.Num() : 0;
}

bool UDocDialogueSubsystem::GetMemory(FName GraphId, const FDocOwnerScope& Owner, FDocDialogueMemory& OutMemory) const
{
	const FMemoryState* Mem = FindMemory(FMemoryKey{ GraphId, Owner });
	if (!Mem)
	{
		return false;
	}
	OutMemory = Mem->Data;
	OutMemory.Receipts = Mem->Ledger.GetAll();
	return true;
}

FDocSystemResult UDocDialogueSubsystem::ResolveCheckpointAction(UDocDialogueGraph* Graph, const FDocOwnerScope& Owner, bool bWasCommitted)
{
	FMemoryState* Mem = Graph ? FindMemory(FMemoryKey{ Graph->GraphId, Owner }) : nullptr;
	if (!Mem || !Mem->Data.Checkpoint.bValid || Mem->Data.Checkpoint.InFlightActionId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No unresolved in-flight action"));
	}
	FDocDialogueCheckpoint& C = Mem->Data.Checkpoint;
	if (bWasCommitted)
	{
		FName NodeId = C.NodeId;
		if (!Graph->FindNode(NodeId))
		{
			if (const FName* Redirect = Graph->NodeRedirects.Find(NodeId)) { NodeId = *Redirect; }
		}
		const FDocDialogueNode* Node = Graph->FindNode(NodeId);
		const FDocDialogueChoice* Choice = (Node && !C.PendingChoiceId.IsNone())
			? Node->Choices.FindByPredicate([&C](const FDocDialogueChoice& X) { return X.ChoiceId == C.PendingChoiceId; }) : nullptr;
		const TArray<FDocDialogueAction>* Actions = Choice ? &Choice->Actions : (Node ? &Node->Actions : nullptr);
		const FDocDialogueAction* Action = Actions ? Actions->FindByPredicate([&C](const FDocDialogueAction& A) { return A.ActionId == C.InFlightActionId; }) : nullptr;
		if (!Action)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
				FString::Printf(TEXT("Action %s no longer exists in graph %s"), *C.InFlightActionId.ToString(), *Graph->GraphId.ToString()));
		}
		FDocEffectReceipt Receipt;
		Receipt.Key.Owner = Owner;
		Receipt.Key.CampaignEpoch = Mem->Data.CampaignEpoch;
		Receipt.Key.ProducerInstanceId = C.SessionId;
		Receipt.Key.TransitionOrdinal = C.TransitionOrdinal + 1;
		Receipt.Key.ActionId = C.InFlightActionId;
		Receipt.PayloadHash = HashAction(*Action);
		Receipt.Result = FDocSystemResult::MakeSuccess();
		Receipt.Result.Diagnostic = TEXT("Resolved by host");
		Receipt.CommittedRevision = Receipt.Key.TransitionOrdinal;
		Mem->Ledger.Record(Receipt);
	}
	C.InFlightActionId = NAME_None;
	return FDocSystemResult::MakeSuccess();
}

TArray<FDocDialogueActionRecord> UDocDialogueSubsystem::GetActionLog(FDocRequestHandle Handle) const
{
	const FSession* S = FindSession(Handle);
	return S ? S->ActionLog : TArray<FDocDialogueActionRecord>();
}

// ---------------------------------------------------------------------------
// Session setup
// ---------------------------------------------------------------------------

FDocSystemResult UDocDialogueSubsystem::PrepareSession(const FDocDialogueStartRequest& Request, FSession& S)
{
	UDocDialogueGraph* Graph = Request.Graph;
	if (!Graph)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No dialogue graph"));
	}
	TArray<FString> Errors, Warnings;
	Graph->FindProblems(Errors, Warnings);
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Graph %s is invalid: %s"), *Graph->GraphId.ToString(), *FString::Join(Errors, TEXT("; "))));
	}
	if (!Request.Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A valid owner scope is required"));
	}
	if (Request.AuthorityMode != EDocDialogueAuthorityMode::LocalOnly && !HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied,
			TEXT("Authority conversations start on the authority"), DocDialogueTags::Error_Dialogue_Unauthorized);
	}

	S.SessionId = FGuid::NewGuid();
	S.Graph = Graph;
	for (int32 i = 0; i < Graph->Nodes.Num(); ++i)
	{
		S.NodeIndex.Add(Graph->Nodes[i].NodeId, i);
	}
	S.MemoryKey = FMemoryKey{ Graph->GraphId, Request.Owner };

	if (Graph->bSingleSessionPerOwner)
	{
		for (const TPair<FGuid, TUniquePtr<FSession>>& Pair : Sessions)
		{
			if (Pair.Value && !Pair.Value->bTerminal && Pair.Value->MemoryKey == S.MemoryKey)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
					FString::Printf(TEXT("Graph %s already has an active session for owner %s"), *Graph->GraphId.ToString(), *Request.Owner.ToString()));
			}
		}
	}

	S.Initiator = Request.Initiator;
	S.Audience = Request.Audience;
	S.Authority = Request.AuthorityMode;
	S.ContextTags = Request.ContextTags;

	TSet<FName> SeenRoles;
	TSet<FName> SeenParticipants;
	for (const FDocDialogueParticipantBinding& B : Request.Bindings)
	{
		const FDocDialogueRole* Role = Graph->FindRole(B.RoleId);
		if (!Role)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unknown role %s"), *B.RoleId.ToString()));
		}
		bool bDupRole = false;
		SeenRoles.Add(B.RoleId, &bDupRole);
		if (bDupRole)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Role %s bound twice"), *B.RoleId.ToString()));
		}

		UObject* Object = B.Participant.Get();
		FName ParticipantId = B.ParticipantId;
		if (Object)
		{
			if (!ImplementsParticipant(Object))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
					FString::Printf(TEXT("%s does not implement IDocDialogueParticipant"), *GetNameSafe(Object)));
			}
			if (Object->GetWorld() != GetWorld())
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
					FString::Printf(TEXT("%s belongs to another world"), *GetNameSafe(Object)));
			}
			const FName ObjectId = IDocDialogueParticipant::Execute_GetDialogueParticipantId(Object);
			if (ParticipantId.IsNone())
			{
				ParticipantId = ObjectId;
			}
			else if (ParticipantId != ObjectId)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
					FString::Printf(TEXT("Binding id %s does not match participant %s"), *ParticipantId.ToString(), *ObjectId.ToString()));
			}
		}
		else if (!ParticipantId.IsNone())
		{
			if (const TWeakObjectPtr<UObject>* Registered = RegisteredParticipants.Find(ParticipantId))
			{
				Object = Registered->Get();
			}
		}
		if (ParticipantId.IsNone())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Role %s has no participant identity"), *B.RoleId.ToString()));
		}
		bool bDupParticipant = false;
		SeenParticipants.Add(ParticipantId, &bDupParticipant);
		if (bDupParticipant)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Participant %s bound to two roles"), *ParticipantId.ToString()));
		}

		FBinding Binding;
		Binding.RoleId = B.RoleId;
		Binding.ParticipantId = ParticipantId;
		Binding.Participant = Object;
		const bool bShared = Object && IDocDialogueParticipant::Execute_AllowsConcurrentDialogue(Object);
		Binding.bExclusive = Role->bExclusive && !bShared;
		Binding.LossPolicy = Role->LossPolicy;
		Binding.PauseTimeoutSeconds = Role->PauseTimeoutSeconds;
		S.Bindings.Add(Binding);
	}
	for (const FDocDialogueRole& Role : Graph->Roles)
	{
		if (Role.bRequired && !SeenRoles.Contains(Role.RoleId))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Required role %s is not bound"), *Role.RoleId.ToString()), DocDialogueTags::Error_Dialogue_ParticipantMissing);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::AcquireReservations(FSession& S)
{
	// Stable order (by ParticipantId) with all-or-none rollback: never half-reserved.
	TArray<int32> Order;
	for (int32 i = 0; i < S.Bindings.Num(); ++i) { Order.Add(i); }
	Algo::Sort(Order, [&S](int32 A, int32 B) { return S.Bindings[A].ParticipantId.LexicalLess(S.Bindings[B].ParticipantId); });

	TArray<FName> Acquired;
	for (int32 Index : Order)
	{
		const FBinding& B = S.Bindings[Index];
		const FReservation* Existing = Reservations.Find(B.ParticipantId);
		if (Existing && Existing->Sessions.Num() > 0 && (Existing->bExclusive || B.bExclusive))
		{
			for (FName Id : Acquired)
			{
				if (FReservation* R = Reservations.Find(Id))
				{
					R->Sessions.Remove(S.SessionId);
					if (R->Sessions.IsEmpty()) { Reservations.Remove(Id); }
				}
			}
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("Participant %s is engaged in another conversation"), *B.ParticipantId.ToString()),
				DocDialogueTags::Error_Dialogue_ReservationConflict);
		}
		FReservation& R = Reservations.FindOrAdd(B.ParticipantId);
		if (R.Sessions.IsEmpty())
		{
			R.bExclusive = B.bExclusive;
		}
		R.Sessions.Add(S.SessionId);
		Acquired.Add(B.ParticipantId);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocDialogueSubsystem::ReleaseReservations(FSession& S)
{
	for (const FBinding& B : S.Bindings)
	{
		if (FReservation* R = Reservations.Find(B.ParticipantId))
		{
			R->Sessions.Remove(S.SessionId);
			if (R->Sessions.IsEmpty())
			{
				Reservations.Remove(B.ParticipantId);
			}
		}
	}
}

void UDocDialogueSubsystem::InitVariables(FSession& S, const TArray<FDocDialogueVariableEntry>* Overrides)
{
	S.Variables.Reset();
	for (const FDocDialogueVariableDecl& Decl : S.Graph->Variables)
	{
		FDocDialogueValue Value;
		if (!Decl.Default.IsSet() || !Decl.Default.CoerceTo(Decl.Type, Value))
		{
			Value = ZeroOf(Decl.Type);
		}
		S.Variables.Add(Decl.Name, Value);
	}

	auto Apply = [&S](const TArray<FDocDialogueVariableEntry>& Entries, bool bPersistentOnly)
	{
		for (const FDocDialogueVariableEntry& Entry : Entries)
		{
			const FDocDialogueVariableDecl* Decl = S.Graph->FindVariable(Entry.Name);
			if (!Decl || (bPersistentOnly && !Decl->bPersistent))
			{
				continue; // removed or no longer persistent: dropped (versioned missing-value behaviour)
			}
			FDocDialogueValue Value;
			if (Entry.Value.CoerceTo(Decl->Type, Value))
			{
				S.Variables.Add(Entry.Name, Value);
			}
			else
			{
				UE_LOG(LogDocDialogue, Warning, TEXT("Variable %s: saved value %s is incompatible with %s; using default"),
					*Entry.Name.ToString(), *Entry.Value.ToString(), *StaticEnum<EDocDialogueValueType>()->GetNameStringByValue(static_cast<int64>(Decl->Type)));
			}
		}
	};
	if (const FMemoryState* Mem = FindMemory(S.MemoryKey))
	{
		Apply(Mem->Data.PersistentVariables, true);
	}
	if (Overrides)
	{
		Apply(*Overrides, false);
	}
}

FDocRequestHandle UDocDialogueSubsystem::AddSession(TUniquePtr<FSession> Session)
{
	const FGuid Id = Session->SessionId;
	Session->Handle = SessionHandles.Add(GetWorld(), Id);
	const FDocRequestHandle Handle = Session->Handle;
	Sessions.Add(Id, MoveTemp(Session));
	return Handle;
}

FDocRequestHandle UDocDialogueSubsystem::StartDialogue(const FDocDialogueStartRequest& Request, FDocSystemResult& OutResult)
{
	FApiScope Scope(*this);
	TUniquePtr<FSession> Session = MakeUnique<FSession>();
	OutResult = PrepareSession(Request, *Session);
	if (!OutResult.IsSuccess())
	{
		return FDocRequestHandle();
	}
	OutResult = AcquireReservations(*Session);
	if (!OutResult.IsSuccess())
	{
		return FDocRequestHandle();
	}
	GetOrCreateMemory(Session->MemoryKey, Request.Graph->Version).Data.GraphVersion = Request.Graph->Version;
	InitVariables(*Session, nullptr);

	FSession& S = *Session;
	const FDocRequestHandle Handle = AddSession(MoveTemp(Session));
	S.NodeId = S.Graph->StartNode;
	SetState(S, EDocDialogueSessionState::Running);
	Run(S);

	OutResult = S.bTerminal && !S.FinalResult.IsSuccess() ? S.FinalResult : FDocSystemResult::MakeSuccess(Handle.GetOperationId());
	return Handle;
}

FDocRequestHandle UDocDialogueSubsystem::ResumeFromCheckpoint(const FDocDialogueStartRequest& Request, EDocDialogueRestorePolicy Policy, FDocSystemResult& OutResult)
{
	FApiScope Scope(*this);
	if (!Request.Graph)
	{
		OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No dialogue graph"));
		return FDocRequestHandle();
	}
	FMemoryState* Mem = FindMemory(FMemoryKey{ Request.Graph->GraphId, Request.Owner });
	if (!Mem || !Mem->Data.Checkpoint.bValid)
	{
		OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No checkpoint for this graph and owner"));
		return FDocRequestHandle();
	}
	const FDocDialogueCheckpoint Checkpoint = Mem->Data.Checkpoint;
	const int32 SavedVersion = Mem->Data.GraphVersion;

	TUniquePtr<FSession> Session = MakeUnique<FSession>();
	OutResult = PrepareSession(Request, *Session);
	if (!OutResult.IsSuccess())
	{
		return FDocRequestHandle();
	}

	// Resolve the checkpoint node (with content migration redirects).
	FName NodeId = Checkpoint.NodeId;
	if (!FindNode(*Session, NodeId))
	{
		if (const FName* Redirect = Request.Graph->NodeRedirects.Find(NodeId))
		{
			NodeId = *Redirect;
		}
	}
	const FDocDialogueNode* Node = FindNode(*Session, NodeId);
	const FDocDialogueChoice* PendingChoice = nullptr;
	if (Node && Checkpoint.bPendingTransition && !Checkpoint.PendingChoiceId.IsNone())
	{
		PendingChoice = Node->Choices.FindByPredicate([&Checkpoint](const FDocDialogueChoice& C) { return C.ChoiceId == Checkpoint.PendingChoiceId; });
	}
	const bool bResolved = Node && (!Checkpoint.bPendingTransition || Checkpoint.PendingChoiceId.IsNone() || PendingChoice);
	bool bRestart = false;
	if (!bResolved)
	{
		if (Policy != EDocDialogueRestorePolicy::RestartFromStart)
		{
			OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
				FString::Printf(TEXT("Checkpoint node %s (graph %s v%d, saved v%d) no longer exists; choose an explicit restart or add a redirect"),
					*Checkpoint.NodeId.ToString(), *Request.Graph->GraphId.ToString(), Request.Graph->Version, SavedVersion),
				DocDialogueTags::Error_Dialogue_MissingNode);
			return FDocRequestHandle();
		}
		bRestart = true;
	}

	if (!bRestart)
	{
		Session->SessionId = Checkpoint.SessionId; // same producer id -> same effect keys

		// An action in flight at save time with no receipt is ambiguous: retry only if safe.
		if (Checkpoint.bPendingTransition && !Checkpoint.InFlightActionId.IsNone())
		{
			const FDocEffectKey Key = MakeEffectKey(*Session, Checkpoint.TransitionOrdinal + 1, Checkpoint.InFlightActionId);
			if (!Mem->Ledger.Find(Key))
			{
				const TArray<FDocDialogueAction>& Candidates = PendingChoice ? PendingChoice->Actions : Node->Actions;
				const FDocDialogueAction* Action = Candidates.FindByPredicate([&Checkpoint](const FDocDialogueAction& A) { return A.ActionId == Checkpoint.InFlightActionId; });
				IDocDialogueActionProvider* Provider = Action ? FindActionProvider(*Action) : nullptr;
				const bool bSafe = Action && (IsInternalAction(Action->Type) || (Provider && Provider->IsIdempotent()));
				if (!bSafe)
				{
					// The checkpoint is kept so the host can resolve it (ResolveCheckpointAction).
					OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::Failed,
						FString::Printf(TEXT("Action %s was in flight when saved and is not idempotent; resolve it before resuming"), *Checkpoint.InFlightActionId.ToString()),
						DocDialogueTags::Error_Dialogue_UnresolvedAction);
					return FDocRequestHandle();
				}
			}
		}
	}
	OutResult = AcquireReservations(*Session);
	if (!OutResult.IsSuccess())
	{
		return FDocRequestHandle();
	}

	Mem->Data.GraphVersion = Request.Graph->Version;
	if (bRestart)
	{
		Mem->Data.Checkpoint = FDocDialogueCheckpoint();
		InitVariables(*Session, nullptr);
	}
	else
	{
		Session->TransitionOrdinal = Checkpoint.TransitionOrdinal;
		Session->LineOrdinal = Checkpoint.LineOrdinal;
		InitVariables(*Session, &Checkpoint.Variables);
	}

	FSession& S = *Session;
	const FDocRequestHandle Handle = AddSession(MoveTemp(Session));
	OutResult = FDocSystemResult::MakeSuccess(Handle.GetOperationId());

	if (bRestart)
	{
		S.NodeId = S.Graph->StartNode;
		SetState(S, EDocDialogueSessionState::Running);
		Run(S);
	}
	else if (Checkpoint.bPendingTransition)
	{
		S.NodeId = NodeId;
		const FName Destination = PendingChoice ? PendingChoice->Destination : Node->Next;
		const TArray<FDocDialogueAction>& Actions = PendingChoice ? PendingChoice->Actions : Node->Actions;

		const FDocSystemResult R = BeginTransition(S, NodeId, Checkpoint.PendingChoiceId, Destination, Actions, true);
		if (!R.IsSuccess())
		{
			OutResult = R;
		}
	}
	else
	{
		S.NodeId = NodeId;
		SetState(S, EDocDialogueSessionState::Running);
		Run(S);
	}
	if (S.bTerminal && !S.FinalResult.IsSuccess())
	{
		OutResult = S.FinalResult;
	}
	return Handle;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void UDocDialogueSubsystem::Run(FSession& S)
{
	const int32 Budget = S.Graph->MaxStepsPerDispatch > 0 ? S.Graph->MaxStepsPerDispatch : Settings()->DefaultMaxStepsPerDispatch;
	const int32 Guard = S.Graph->MaxNonYieldingSteps > 0 ? S.Graph->MaxNonYieldingSteps : Settings()->DefaultMaxNonYieldingSteps;
	int32 Steps = 0;

	while (S.State == EDocDialogueSessionState::Running && !S.bTerminal)
	{
		if (S.NonYieldingSteps >= Guard)
		{
			Fail(S, EDocResultOutcome::Failed, DocDialogueTags::Error_Dialogue_RunawayLoop,
				FString::Printf(TEXT("Runaway loop: %d steps without waiting in graph %s (at node %s). Add a wait or a terminating condition."),
					S.NonYieldingSteps, *S.Graph->GraphId.ToString(), *S.NodeId.ToString()));
			return;
		}
		if (Steps >= Budget)
		{
			S.bContinuePending = true; // yield; continue next tick
			return;
		}
		++Steps;
		++S.NonYieldingSteps;

		const FDocDialogueNode* Node = FindNode(S, S.NodeId);
		if (!Node)
		{
			Fail(S, EDocResultOutcome::NotFound, DocDialogueTags::Error_Dialogue_MissingNode,
				FString::Printf(TEXT("Node %s not found in graph %s"), *S.NodeId.ToString(), *S.Graph->GraphId.ToString()));
			return;
		}
		FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
		if (!Mem.Visited.Contains(S.NodeId))
		{
			Mem.Visited.Add(S.NodeId);
			Mem.Data.VisitedNodes.Add(S.NodeId);
		}

		switch (Node->Type)
		{
		case EDocDialogueNodeType::Line:
			EnterLine(S, *Node);
			break;

		case EDocDialogueNodeType::Choice:
			EnterChoice(S, *Node);
			break;

		case EDocDialogueNodeType::Branch:
		{
			FName Destination = Node->Next;
			for (const FDocDialogueBranchCase& Case : Node->Cases)
			{
				const FDocConditionResult R = EvaluateAll(S, Case.Conditions);
				if (R.IsUnavailable())
				{
					if (Node->UnavailableDestination.IsNone())
					{
						Fail(S, EDocResultOutcome::Unavailable, R.ReasonTag.IsValid() ? R.ReasonTag : DocCoreTags::Error_Unavailable,
							FString::Printf(TEXT("Branch %s: condition data unavailable (%s)"), *Node->NodeId.ToString(), *R.Diagnostic));
						return;
					}
					Destination = Node->UnavailableDestination;
					break;
				}
				if (R.IsSatisfied())
				{
					Destination = Case.Destination;
					break;
				}
			}
			GoTo(S, Destination);
			break;
		}

		case EDocDialogueNodeType::Condition:
		{
			const FDocConditionResult R = EvaluateAll(S, Node->Conditions);
			if (R.IsUnavailable())
			{
				if (Node->UnavailableDestination.IsNone())
				{
					Fail(S, EDocResultOutcome::Unavailable, R.ReasonTag.IsValid() ? R.ReasonTag : DocCoreTags::Error_Unavailable,
						FString::Printf(TEXT("Condition %s: data unavailable (%s)"), *Node->NodeId.ToString(), *R.Diagnostic));
					return;
				}
				GoTo(S, Node->UnavailableDestination);
			}
			else
			{
				GoTo(S, R.IsSatisfied() ? Node->Next : Node->FailDestination);
			}
			break;
		}

		case EDocDialogueNodeType::Event:
			BeginTransition(S, Node->NodeId, NAME_None, Node->Next, Node->Actions, false);
			break;

		case EDocDialogueNodeType::Jump:
			GoTo(S, Node->Next);
			break;

		case EDocDialogueNodeType::Delay:
			S.DeadlineClock = Node->DelayClock;
			S.Deadline = Now(Node->DelayClock) + FMath::Max(0.f, Node->DelaySeconds);
			EnterWaiting(S, EDocDialogueSessionState::WaitingForDelay);
			break;

		case EDocDialogueNodeType::End:
			if (Node->EndKind == EDocDialogueEndKind::Completed)
			{
				FDocSystemResult Done = FDocSystemResult::MakeSuccess();
				Done.Diagnostic = Node->EndTag.IsValid() ? Node->EndTag.ToString() : FString();
				Terminate(S, EDocDialogueSessionState::Completed, Done);
			}
			else
			{
				Fail(S, EDocResultOutcome::Failed, Node->EndTag.IsValid() ? Node->EndTag : DocDialogueTags::Error_Dialogue,
					FString::Printf(TEXT("Conversation ended in failure at node %s"), *Node->NodeId.ToString()));
			}
			return;

		case EDocDialogueNodeType::Custom:
		{
			const TSharedPtr<IDocDialogueCustomNodeHandler>* Handler = CustomNodeHandlers.Find(Node->CustomType);
			if (!Handler || !Handler->IsValid())
			{
				Fail(S, EDocResultOutcome::Unsupported, DocDialogueTags::Error_Dialogue_ProviderMissing,
					FString::Printf(TEXT("No handler registered for custom node type %s (node %s)"), *Node->CustomType.ToString(), *Node->NodeId.ToString()));
				return;
			}
			const FDocDialogueCustomNodeResult R = (*Handler)->RunNode(*Node, MakeSnapshot(S));
			if (!R.Result.IsSuccess())
			{
				Fail(S, R.Result.Outcome, R.Result.ErrorTag, R.Result.Diagnostic);
				return;
			}
			GoTo(S, R.Next.IsNone() ? Node->Next : R.Next);
			break;
		}
		}
	}
}

void UDocDialogueSubsystem::GoTo(FSession& S, FName Destination)
{
	if (S.bTerminal)
	{
		return;
	}
	if (Destination.IsNone())
	{
		Terminate(S, EDocDialogueSessionState::Completed, FDocSystemResult::MakeSuccess());
		return;
	}
	S.NodeId = Destination;
	++S.Revision;
	SetState(S, EDocDialogueSessionState::Running);
}

void UDocDialogueSubsystem::EnterWaiting(FSession& S, EDocDialogueSessionState State)
{
	S.NonYieldingSteps = 0;
	++S.Revision;
	SetState(S, State);
	WriteCheckpoint(S);
}

float UDocDialogueSubsystem::EstimateTextSeconds(const FText& Text) const
{
	const UDocDialogueSettings* Cfg = Settings();
	const float Raw = Text.ToString().Len() * Cfg->TextSecondsPerCharacter;
	return FMath::Clamp(Raw, Cfg->MinLineSeconds, FMath::Max(Cfg->MinLineSeconds, Cfg->MaxLineSeconds));
}

float UDocDialogueSubsystem::ResolveLineDuration(const FDocDialogueNode& Node, bool& bOutVoiceMissing) const
{
	bOutVoiceMissing = false;
	const float DurationOrEstimate = Node.Duration > 0.f ? Node.Duration : EstimateTextSeconds(Node.Text);
	float Seconds = DurationOrEstimate;
	if (Node.TimingSource == EDocDialogueLineTiming::VoiceDuration)
	{
		const float Voice = (!Node.VoiceAsset.IsNull() && VoiceDurationResolver) ? VoiceDurationResolver(Node.VoiceAsset) : -1.f;
		if (Voice > 0.f)
		{
			Seconds = Voice;
		}
		else
		{
			// Missing/unloaded voice never deadlocks: fall back to authored or estimated timing.
			bOutVoiceMissing = true;
			Seconds = Node.MissingVoicePolicy == EDocDialogueMissingVoicePolicy::UseTextEstimate ? EstimateTextSeconds(Node.Text) : DurationOrEstimate;
		}
	}
	return FMath::Max(Seconds, Node.SubtitleTiming.MinDisplaySeconds + Node.SubtitleTiming.LeadInSeconds);
}

void UDocDialogueSubsystem::EnterLine(FSession& S, const FDocDialogueNode& Node)
{
	++S.LineOrdinal;
	bool bVoiceMissing = false;
	const float Duration = ResolveLineDuration(Node, bVoiceMissing);

	FDocDialogueLineInfo L;
	L.LineOrdinal = S.LineOrdinal;
	L.NodeId = Node.NodeId;
	L.SpeakerRole = Node.SpeakerRole;
	L.Text = Node.Text;
	L.VoiceAsset = Node.VoiceAsset;
	L.bVoiceMissing = bVoiceMissing;
	L.ResolvedDuration = Node.bAutoAdvance ? Duration : 0.f;
	L.SubtitleTiming = Node.SubtitleTiming;
	L.AnimationTag = Node.AnimationTag;
	L.ExpressionTag = Node.ExpressionTag;
	L.CameraTag = Node.CameraTag;
	L.GameplayTags = Node.GameplayTags;
	L.bAutoAdvance = Node.bAutoAdvance;
	L.bSkippable = Node.bSkippable;
	if (const FBinding* B = S.Bindings.FindByPredicate([&Node](const FBinding& X) { return X.RoleId == Node.SpeakerRole; }))
	{
		L.SpeakerParticipantId = B->ParticipantId;
		if (const UObject* Object = B->Participant.Get())
		{
			L.SpeakerName = IDocDialogueParticipant::Execute_GetDialogueDisplayName(Object);
		}
	}
	if (L.SpeakerName.IsEmpty())
	{
		if (const FDocDialogueRole* Role = S.Graph->FindRole(Node.SpeakerRole))
		{
			L.SpeakerName = Role->DisplayName;
		}
	}

	S.CurrentLine = L;
	S.Deadline = -1.0;
	S.LineReadyAt = -1.0;
	S.bAwaitingAck = false;
	if (Node.bAutoAdvance)
	{
		S.DeadlineClock = EDocClockDomain::WorldGameplay;
		const double T = Now(S.DeadlineClock);
		if (Node.TimingSource == EDocDialogueLineTiming::PresentationAck)
		{
			S.bAwaitingAck = true;
			S.AckHoldSeconds = FMath::Max(0.f, Node.Duration);
			S.Deadline = T + FMath::Max(static_cast<double>(Settings()->PresentationAckTimeoutSeconds), static_cast<double>(Duration));
		}
		else
		{
			S.Deadline = T + Duration;
		}
		if (!Node.bSkippable)
		{
			S.LineReadyAt = S.Deadline;
		}
	}
	EnterWaiting(S, EDocDialogueSessionState::WaitingForAdvance);
	QueueEvent([WeakThis = TWeakObjectPtr<UDocDialogueSubsystem>(this), Handle = S.Handle, L]()
	{
		if (UDocDialogueSubsystem* This = WeakThis.Get())
		{
			This->OnLineShownNative.Broadcast(Handle, L);
			This->OnLineShown.Broadcast(Handle, L);
		}
	});
}

void UDocDialogueSubsystem::CompleteLine(FSession& S)
{
	const FDocDialogueLineInfo L = S.CurrentLine;
	S.Deadline = -1.0;
	S.LineReadyAt = -1.0;
	S.bAwaitingAck = false;
	QueueEvent([WeakThis = TWeakObjectPtr<UDocDialogueSubsystem>(this), Handle = S.Handle, L]()
	{
		if (UDocDialogueSubsystem* This = WeakThis.Get())
		{
			This->OnLineCompletedNative.Broadcast(Handle, L);
			This->OnLineCompleted.Broadcast(Handle, L);
		}
	});
	const FDocDialogueNode* Node = FindNode(S, S.NodeId);
	SetState(S, EDocDialogueSessionState::Running);
	GoTo(S, Node ? Node->Next : NAME_None);
	Run(S);
}

void UDocDialogueSubsystem::EnterChoice(FSession& S, const FDocDialogueNode& Node)
{
	const FDocDialogueChoiceList List = BuildChoiceList(S);
	const bool bAnySelectable = List.Choices.ContainsByPredicate([](const FDocDialogueChoiceView& V) { return V.State == EDocDialogueChoiceState::VisibleEnabled; });
	if (!bAnySelectable)
	{
		if (!Node.NoChoiceDestination.IsNone())
		{
			GoTo(S, Node.NoChoiceDestination);
			return;
		}
		Fail(S, EDocResultOutcome::Failed, DocDialogueTags::Error_Dialogue_NoChoices,
			FString::Printf(TEXT("No selectable choice at node %s and no NoChoiceDestination"), *Node.NodeId.ToString()));
		return;
	}
	EnterWaiting(S, EDocDialogueSessionState::WaitingForChoice);
	PresentChoices(S);
}

void UDocDialogueSubsystem::PresentChoices(FSession& S)
{
	const FDocDialogueChoiceList List = BuildChoiceList(S); // stamped with the current revision
	QueueEvent([WeakThis = TWeakObjectPtr<UDocDialogueSubsystem>(this), Handle = S.Handle, List]()
	{
		if (UDocDialogueSubsystem* This = WeakThis.Get())
		{
			This->OnChoicesPresentedNative.Broadcast(Handle, List);
			This->OnChoicesPresented.Broadcast(Handle, List);
		}
	});
}

void UDocDialogueSubsystem::Terminate(FSession& S, EDocDialogueSessionState Final, const FDocSystemResult& Result)
{
	if (S.bTerminal)
	{
		return;
	}
	// Cancellation never silently undoes committed effects; only validated compensations run.
	if (Final == EDocDialogueSessionState::Cancelled || Final == EDocDialogueSessionState::TimedOut)
	{
		for (int32 i = S.Committed.Num() - 1; i >= 0; --i)
		{
			const FCommittedAction& C = S.Committed[i];
			if (!C.Action.bCompensateOnCancel)
			{
				continue;
			}
			IDocDialogueActionProvider* Provider = FindActionProvider(C.Action);
			if (!Provider || !Provider->SupportsCompensation())
			{
				UE_LOG(LogDocDialogue, Log, TEXT("Action %s asks for compensation but its provider advertises none; left committed"), *C.Key.ToString());
				continue;
			}
			FDocDialogueActionRequest Request;
			Request.Action = &C.Action;
			Request.EffectKey = C.Key;
			Request.GraphId = S.Graph->GraphId;
			Request.NodeId = C.NodeId;
			Request.ChoiceId = C.ChoiceId;
			Request.Participants = GetParticipantMap(S);
			Request.World = GetWorld();
			const FDocSystemResult R = Provider->CompensateAction(Request);
			RecordAction(S, C.Key, R.IsSuccess() ? EDocDialogueActionState::Compensated : EDocDialogueActionState::Committed, R);
		}
	}

	S.bTerminal = true;
	S.Deadline = -1.0;
	S.PauseDeadline = -1.0;
	S.bContinuePending = false;
	S.Pending = FPendingTransition();
	ReleaseReservations(S);
	ClearCheckpoint(S);
	S.FinalResult = Result;
	S.Diagnostic = Result.Diagnostic;
	SetState(S, Final);
	QueueEvent([WeakThis = TWeakObjectPtr<UDocDialogueSubsystem>(this), Handle = S.Handle, Final, Result]()
	{
		if (UDocDialogueSubsystem* This = WeakThis.Get())
		{
			This->OnSessionEndedNative.Broadcast(Handle, Final, Result);
			This->OnSessionEnded.Broadcast(Handle, Final, Result);
		}
	});
}

void UDocDialogueSubsystem::Fail(FSession& S, EDocResultOutcome Outcome, const FGameplayTag& Tag, const FString& Diagnostic)
{
	UE_LOG(LogDocDialogue, Warning, TEXT("Dialogue %s (%s) failed: %s"), *S.Graph->GraphId.ToString(), *S.Handle.ToString(), *Diagnostic);
	const EDocResultOutcome Safe = FDocSystemResult::IsSuccessOutcome(Outcome) || Outcome == EDocResultOutcome::Unset ? EDocResultOutcome::Failed : Outcome;
	Terminate(S, EDocDialogueSessionState::Failed, FDocSystemResult::MakeFailure(Safe, Diagnostic, Tag));
}

void UDocDialogueSubsystem::PauseInternal(FSession& S, float TimeoutSeconds, bool bForRebind)
{
	if (S.bTerminal)
	{
		return;
	}
	if (S.State == EDocDialogueSessionState::Paused)
	{
		if (TimeoutSeconds > 0.f)
		{
			const double Deadline = Now(EDocClockDomain::WorldGameplay) + TimeoutSeconds;
			S.PauseDeadline = S.PauseDeadline < 0.0 ? Deadline : FMath::Min(S.PauseDeadline, Deadline);
		}
		S.bPausedForRebind |= bForRebind;
		return;
	}
	S.ResumeState = S.State;
	S.RemainingOnPause = S.Deadline >= 0.0 ? FMath::Max(0.0, S.Deadline - Now(S.DeadlineClock)) : -1.0;
	S.Deadline = -1.0;
	S.PauseDeadline = TimeoutSeconds > 0.f ? Now(EDocClockDomain::WorldGameplay) + TimeoutSeconds : -1.0;
	S.bPausedForRebind = bForRebind;
	++S.Revision;
	SetState(S, EDocDialogueSessionState::Paused);
}

void UDocDialogueSubsystem::ResumeInternal(FSession& S)
{
	if (S.State != EDocDialogueSessionState::Paused || S.bTerminal)
	{
		return;
	}
	const bool bReadyGate = S.LineReadyAt >= 0.0;
	if (S.RemainingOnPause >= 0.0)
	{
		S.Deadline = Now(S.DeadlineClock) + S.RemainingOnPause;
		S.RemainingOnPause = -1.0;
		if (bReadyGate)
		{
			S.LineReadyAt = S.Deadline;
		}
	}
	S.PauseDeadline = -1.0;
	S.bPausedForRebind = false;
	const EDocDialogueSessionState Target = S.ResumeState;
	S.ResumeState = EDocDialogueSessionState::None;
	++S.Revision;
	SetState(S, Target);
	if (Target == EDocDialogueSessionState::Running)
	{
		S.bContinuePending = true;
	}
	else if (Target == EDocDialogueSessionState::WaitingForAction && S.Pending.DeferredCompletion.IsSet())
	{
		const FDocSystemResult Deferred = S.Pending.DeferredCompletion.GetValue();
		S.Pending.DeferredCompletion.Reset();
		ApplyCompletion(S, Deferred);
	}
}

void UDocDialogueSubsystem::OnTimer(FSession& S)
{
	S.Deadline = -1.0;
	if (S.State == EDocDialogueSessionState::WaitingForAdvance)
	{
		CompleteLine(S); // also the PresentationAck safety timeout
	}
	else if (S.State == EDocDialogueSessionState::WaitingForDelay)
	{
		const FDocDialogueNode* Node = FindNode(S, S.NodeId);
		SetState(S, EDocDialogueSessionState::Running);
		GoTo(S, Node ? Node->Next : NAME_None);
		Run(S);
	}
}

void UDocDialogueSubsystem::Tick(float DeltaTime)
{
	if (Sessions.IsEmpty())
	{
		return;
	}
	FApiScope Scope(*this);
	TArray<FGuid> Ids;
	Sessions.GetKeys(Ids);
	for (const FGuid& Id : Ids)
	{
		TUniquePtr<FSession>* Found = Sessions.Find(Id);
		if (!Found || !Found->IsValid() || (*Found)->bTerminal)
		{
			continue;
		}
		FSession& S = **Found;
		if (S.State == EDocDialogueSessionState::Paused)
		{
			if (S.PauseDeadline >= 0.0 && Now(EDocClockDomain::WorldGameplay) >= S.PauseDeadline)
			{
				Terminate(S, EDocDialogueSessionState::TimedOut, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut,
					TEXT("Paused conversation timed out"), S.bPausedForRebind ? DocDialogueTags::Error_Dialogue_ParticipantLost : FGameplayTag()));
			}
			continue;
		}
		if (S.bContinuePending && S.State == EDocDialogueSessionState::Running)
		{
			S.bContinuePending = false;
			Run(S);
			continue;
		}
		if (S.Deadline >= 0.0 && Now(S.DeadlineClock) >= S.Deadline)
		{
			OnTimer(S);
		}
	}
}

// ---------------------------------------------------------------------------
// Public commands
// ---------------------------------------------------------------------------

FDocSystemResult UDocDialogueSubsystem::Advance(FDocRequestHandle Handle, int64 ExpectedRevision, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->State == EDocDialogueSessionState::Paused)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Conversation is paused"), DocDialogueTags::Error_Dialogue_WrongState);
	}
	if (S->State != EDocDialogueSessionState::WaitingForAdvance)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Not waiting for advance (state %s)"), *StateName(S->State)), DocDialogueTags::Error_Dialogue_WrongState);
	}
	if (ExpectedRevision != S->Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Stale revision %lld (current %lld)"), ExpectedRevision, S->Revision), DocDialogueTags::Error_Dialogue_StaleRevision);
	}
	if (S->LineReadyAt >= 0.0 && Now(S->DeadlineClock) < S->LineReadyAt)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Line is not skippable yet"), DocDialogueTags::Error_Dialogue_NotSkippable);
	}
	CompleteLine(*S);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::AcknowledgeLinePresented(FDocRequestHandle Handle, int64 LineOrdinal)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	if (S->State != EDocDialogueSessionState::WaitingForAdvance || S->CurrentLine.LineOrdinal != LineOrdinal)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Line %lld is not the presented line"), LineOrdinal), DocDialogueTags::Error_Dialogue_StaleRevision);
	}
	if (!S->bAwaitingAck)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Line does not wait for acknowledgement"));
	}
	S->bAwaitingAck = false;
	if (S->AckHoldSeconds <= 0.f)
	{
		CompleteLine(*S);
		return FDocSystemResult::MakeSuccess();
	}
	S->Deadline = Now(S->DeadlineClock) + S->AckHoldSeconds;
	if (S->LineReadyAt >= 0.0)
	{
		S->LineReadyAt = S->Deadline;
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::SelectChoice(FDocRequestHandle Handle, FName ChoiceId, int64 ExpectedRevision, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->State == EDocDialogueSessionState::Paused)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Conversation is paused"), DocDialogueTags::Error_Dialogue_WrongState);
	}
	if (S->State != EDocDialogueSessionState::WaitingForChoice)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Not waiting for a choice (state %s)"), *StateName(S->State)), DocDialogueTags::Error_Dialogue_WrongState);
	}
	if (ExpectedRevision != S->Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Stale choice list (revision %lld, current %lld)"), ExpectedRevision, S->Revision), DocDialogueTags::Error_Dialogue_StaleRevision);
	}
	const FDocDialogueNode* Node = FindNode(*S, S->NodeId);
	const FDocDialogueChoice* Choice = Node ? Node->Choices.FindByPredicate([ChoiceId](const FDocDialogueChoice& C) { return C.ChoiceId == ChoiceId; }) : nullptr;
	if (!Choice)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
			FString::Printf(TEXT("Choice %s does not belong to the active node %s"), *ChoiceId.ToString(), *S->NodeId.ToString()), DocDialogueTags::Error_Dialogue_ChoiceNotFound);
	}
	// Authoritative re-evaluation; a cached client list is never trusted.
	const FDocDialogueChoiceView View = EvaluateChoice(*S, *Choice);
	if (View.State == EDocDialogueChoiceState::Hidden)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Choice %s is hidden"), *ChoiceId.ToString()), DocDialogueTags::Error_Dialogue_ChoiceHidden);
	}
	if (View.State == EDocDialogueChoiceState::VisibleDisabled)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Choice %s is disabled"), *ChoiceId.ToString()), DocDialogueTags::Error_Dialogue_ChoiceDisabled, View.FailureReason);
	}
	return BeginTransition(*S, S->NodeId, ChoiceId, Choice->Destination, Choice->Actions, true);
}

FDocSystemResult UDocDialogueSubsystem::Pause(FDocRequestHandle Handle, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->State == EDocDialogueSessionState::Paused)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already paused"));
	}
	PauseInternal(*S, 0.f, false);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::Resume(FDocRequestHandle Handle, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->State != EDocDialogueSessionState::Paused)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Not paused"));
	}
	if (const FBinding* Waiting = S->Bindings.FindByPredicate([](const FBinding& B) { return B.bLost && B.LossPolicy == EDocDialogueLossPolicy::RebindByIdentity; }))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady,
			FString::Printf(TEXT("Waiting for participant %s to rebind"), *Waiting->ParticipantId.ToString()), DocDialogueTags::Error_Dialogue_ParticipantLost);
	}
	ResumeInternal(*S);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::Cancel(FDocRequestHandle Handle, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	Terminate(*S, EDocDialogueSessionState::Cancelled, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Cancelled by request")));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::End(FDocRequestHandle Handle, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->Pending.bActive)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("A transition is in flight; cancel instead or wait for it"), DocDialogueTags::Error_Dialogue_WrongState);
	}
	Terminate(*S, EDocDialogueSessionState::Completed, FDocSystemResult::MakeSuccess());
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocDialogueSubsystem::CompletePendingAction(FDocRequestHandle Handle, FName ActionId, const FDocSystemResult& Result)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	if (!S->Pending.bActive || ActionId.IsNone() || S->Pending.InFlightAction != ActionId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Action %s is not in flight"), *ActionId.ToString()), DocDialogueTags::Error_Dialogue_StaleRevision);
	}
	if (S->State == EDocDialogueSessionState::Paused)
	{
		S->Pending.DeferredCompletion = Result; // applied on resume
		return FDocSystemResult::MakeSuccess();
	}
	if (S->State != EDocDialogueSessionState::WaitingForAction)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Not waiting for an action"), DocDialogueTags::Error_Dialogue_WrongState);
	}
	ApplyCompletion(*S, Result);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Queries / variables
// ---------------------------------------------------------------------------

bool UDocDialogueSubsystem::GetSessionSnapshot(FDocRequestHandle Handle, FDocDialogueSessionSnapshot& OutSnapshot) const
{
	const FSession* S = FindSession(Handle);
	if (!S)
	{
		return false;
	}
	OutSnapshot = MakeSnapshot(*S);
	return true;
}

FDocDialogueSessionSnapshot UDocDialogueSubsystem::MakeSnapshot(const FSession& S) const
{
	FDocDialogueSessionSnapshot Snap;
	Snap.Handle = S.Handle;
	Snap.SessionId = S.SessionId;
	Snap.Node.GraphId = S.Graph ? S.Graph->GraphId : NAME_None;
	Snap.Node.GraphVersion = S.Graph ? S.Graph->Version : 0;
	Snap.Node.NodeId = S.NodeId;
	Snap.State = S.State;
	Snap.ResumeState = S.ResumeState;
	Snap.Revision = S.Revision;
	Snap.TransitionOrdinal = S.TransitionOrdinal;
	const EDocDialogueSessionState Effective = S.State == EDocDialogueSessionState::Paused ? S.ResumeState : S.State;
	if (Effective == EDocDialogueSessionState::WaitingForAdvance)
	{
		Snap.bHasLine = true;
		Snap.Line = S.CurrentLine;
	}
	if (Effective == EDocDialogueSessionState::WaitingForChoice)
	{
		Snap.Choices = BuildChoiceList(S);
	}
	for (const FBinding& B : S.Bindings)
	{
		FDocDialogueParticipantView View;
		View.RoleId = B.RoleId;
		View.ParticipantId = B.ParticipantId;
		const UObject* Object = B.Participant.Get();
		View.bPresent = Object != nullptr;
		if (Object)
		{
			View.DisplayName = IDocDialogueParticipant::Execute_GetDialogueDisplayName(Object);
		}
		if (View.DisplayName.IsEmpty())
		{
			if (const FDocDialogueRole* Role = S.Graph->FindRole(B.RoleId))
			{
				View.DisplayName = Role->DisplayName;
			}
		}
		Snap.Participants.Add(View);
	}
	Snap.Diagnostic = S.Diagnostic;
	return Snap;
}

FDocSystemResult UDocDialogueSubsystem::GetAvailableChoices(FDocRequestHandle Handle, UObject* Requester, FDocDialogueChoiceList& OutChoices) const
{
	FDocSystemResult Failure;
	const FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, false);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->State != EDocDialogueSessionState::WaitingForChoice)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Not waiting for a choice (state %s)"), *StateName(S->State)), DocDialogueTags::Error_Dialogue_WrongState);
	}
	OutChoices = BuildChoiceList(*S);
	return FDocSystemResult::MakeSuccess();
}

bool UDocDialogueSubsystem::GetVariable(FDocRequestHandle Handle, FName Name, FDocDialogueValue& OutValue) const
{
	const FSession* S = FindSession(Handle);
	const FDocDialogueValue* Value = S ? S->Variables.Find(Name) : nullptr;
	if (!Value)
	{
		return false;
	}
	OutValue = *Value;
	return true;
}

FDocSystemResult UDocDialogueSubsystem::SetVariable(FDocRequestHandle Handle, FName Name, const FDocDialogueValue& Value, UObject* Requester)
{
	FApiScope Scope(*this);
	FDocSystemResult Failure;
	FSession* S = FindSession(Handle, &Failure);
	if (!S) { return Failure; }
	const FDocSystemResult Auth = Authorize(*S, Requester, true);
	if (!Auth.IsSuccess()) { return Auth; }
	if (S->Pending.bActive)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("A transition is in flight"), DocDialogueTags::Error_Dialogue_WrongState);
	}
	const FDocDialogueVariableDecl* Decl = S->Graph->FindVariable(Name);
	if (!Decl)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Undeclared variable %s"), *Name.ToString()), DocDialogueTags::Error_Dialogue_VariableMissing);
	}
	FDocDialogueValue Coerced;
	if (!Value.CoerceTo(Decl->Type, Coerced))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("%s is not convertible to the type of %s"), *Value.ToString(), *Name.ToString()), DocDialogueTags::Error_Dialogue_VariableType);
	}
	FDocDialogueValue& Slot = S->Variables.FindOrAdd(Name);
	if (Slot == Coerced)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Same value"));
	}
	Slot = Coerced;
	if (Decl->bPersistent)
	{
		CommitPersistentVariables(*S);
	}
	++S->Revision; // invalidates choice lists built on the old value
	if (S->State == EDocDialogueSessionState::WaitingForChoice)
	{
		PresentChoices(*S);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------

UObject* UDocDialogueSubsystem::GetRoleObject(const FSession& S, FName Role) const
{
	const FBinding* B = S.Bindings.FindByPredicate([Role](const FBinding& X) { return X.RoleId == Role; });
	return B ? B->Participant.Get() : nullptr;
}

bool UDocDialogueSubsystem::GetRoleTags(const FSession& S, FName Role, FGameplayTagContainer& OutTags) const
{
	const UObject* Object = GetRoleObject(S, Role);
	if (!Object)
	{
		return false;
	}
	if (ImplementsParticipant(Object))
	{
		IDocDialogueParticipant::Execute_GetDialogueTags(Object, OutTags);
	}
	else
	{
		FGameplayTagContainer Tags;
		UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(Object, Tags);
		OutTags.AppendTags(Tags);
	}
	return true;
}

TMap<FName, TWeakObjectPtr<UObject>> UDocDialogueSubsystem::GetParticipantMap(const FSession& S) const
{
	TMap<FName, TWeakObjectPtr<UObject>> Map;
	for (const FBinding& B : S.Bindings)
	{
		Map.Add(B.RoleId, B.Participant);
	}
	return Map;
}

FDocConditionResult UDocDialogueSubsystem::EvaluateCondition(const FSession& S, const FDocDialogueCondition& C, const TMap<FName, FDocDialogueValue>& Vars) const
{
	auto Finish = [&C](FDocConditionResult R)
	{
		if (C.bNegate && !R.IsUnavailable())
		{
			const bool bWasSatisfied = R.IsSatisfied();
			R.State = bWasSatisfied ? EDocConditionState::Unsatisfied : EDocConditionState::Satisfied;
			if (bWasSatisfied && !R.ReasonTag.IsValid())
			{
				R.ReasonTag = DocDialogueTags::Error_Dialogue;
			}
		}
		return R;
	};

	switch (C.Type)
	{
	case EDocDialogueConditionType::TagPresent:
	case EDocDialogueConditionType::TagMissing:
	case EDocDialogueConditionType::ParticipantTag:
	{
		FGameplayTagContainer Tags;
		if (C.Role.IsNone())
		{
			if (C.Type == EDocDialogueConditionType::ParticipantTag)
			{
				return FDocConditionResult::Unavailable(DocCoreTags::Error_InvalidConfiguration, TEXT("ParticipantTag without Role"));
			}
			Tags = S.ContextTags;
			if (const UObject* Initiator = S.Initiator.Get())
			{
				FGameplayTagContainer InitiatorTags;
				UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(Initiator, InitiatorTags);
				Tags.AppendTags(InitiatorTags);
			}
		}
		else if (!GetRoleTags(S, C.Role, Tags))
		{
			return FDocConditionResult::Unavailable(DocDialogueTags::Error_Dialogue_ParticipantMissing,
				FString::Printf(TEXT("Participant for role %s is not present"), *C.Role.ToString()));
		}
		const bool bHas = Tags.HasTag(C.Tag);
		const bool bWant = C.Type != EDocDialogueConditionType::TagMissing;
		return Finish(bHas == bWant ? FDocConditionResult::Satisfied()
			: FDocConditionResult::Unsatisfied(C.Tag, FString::Printf(TEXT("Tag %s %s"), *C.Tag.ToString(), bWant ? TEXT("missing") : TEXT("present"))));
	}

	case EDocDialogueConditionType::NumericComparison:
	{
		const FDocDialogueValue* Value = Vars.Find(C.VariableName);
		if (!Value)
		{
			return FDocConditionResult::Unavailable(DocDialogueTags::Error_Dialogue_VariableMissing,
				FString::Printf(TEXT("Variable %s is not declared"), *C.VariableName.ToString()));
		}
		double N = 0.0;
		if (!Value->ToNumber(N))
		{
			return FDocConditionResult::Unavailable(DocDialogueTags::Error_Dialogue_VariableType,
				FString::Printf(TEXT("Variable %s (%s) is not numeric"), *C.VariableName.ToString(), *Value->ToString()));
		}
		bool bResult = false;
		switch (C.Compare)
		{
		case EDocDialogueCompare::Equal: bResult = FMath::IsNearlyEqual(N, C.Value, 1e-6); break;
		case EDocDialogueCompare::NotEqual: bResult = !FMath::IsNearlyEqual(N, C.Value, 1e-6); break;
		case EDocDialogueCompare::Less: bResult = N < C.Value; break;
		case EDocDialogueCompare::LessOrEqual: bResult = N <= C.Value; break;
		case EDocDialogueCompare::Greater: bResult = N > C.Value; break;
		case EDocDialogueCompare::GreaterOrEqual: bResult = N >= C.Value; break;
		}
		return Finish(bResult ? FDocConditionResult::Satisfied()
			: FDocConditionResult::Unsatisfied(DocDialogueTags::Error_Dialogue, FString::Printf(TEXT("%s = %s fails comparison with %s"),
				*C.VariableName.ToString(), *Value->ToString(), *FString::SanitizeFloat(C.Value))));
	}

	case EDocDialogueConditionType::NodeVisited:
	case EDocDialogueConditionType::ChoiceTaken:
	{
		const FMemoryState* Mem = FindMemory(S.MemoryKey);
		bool bResult = false;
		if (Mem)
		{
			bResult = C.Type == EDocDialogueConditionType::NodeVisited
				? Mem->Visited.Contains(C.QueryId)
				: Mem->Data.ChoiceHistory.ContainsByPredicate([&C](const FDocDialogueChoiceRecord& R) { return R.ChoiceId == C.QueryId; });
		}
		return Finish(bResult ? FDocConditionResult::Satisfied() : FDocConditionResult::Unsatisfied(DocDialogueTags::Error_Dialogue, TEXT("History condition not met")));
	}

	case EDocDialogueConditionType::EventState:
	case EDocDialogueConditionType::CustomProvider:
	case EDocDialogueConditionType::InterfaceQuery:
	{
		FName ProviderId = C.ProviderId;
		if (C.Type == EDocDialogueConditionType::EventState && ProviderId.IsNone())
		{
			ProviderId = Settings()->EventProviderId;
		}
		const TSharedPtr<IDocDialogueConditionProvider>* Provider = ConditionProviders.Find(ProviderId);
		if (!Provider || !Provider->IsValid())
		{
			return FDocConditionResult::Unavailable(DocDialogueTags::Error_Dialogue_ProviderMissing,
				FString::Printf(TEXT("No condition provider '%s' registered"), *ProviderId.ToString()));
		}
		FDocDialogueConditionQuery Query;
		Query.Condition = &C;
		Query.GraphId = S.Graph->GraphId;
		Query.SessionId = S.SessionId;
		Query.Owner = S.MemoryKey.Owner;
		Query.Participants = GetParticipantMap(S);
		Query.ContextTags = S.ContextTags;
		Query.World = GetWorld();
		return Finish((*Provider)->EvaluateCondition(Query));
	}
	}
	return FDocConditionResult::Unavailable(DocCoreTags::Error_Unsupported, TEXT("Unknown condition type"));
}

FDocConditionResult UDocDialogueSubsystem::EvaluateAll(const FSession& S, const TArray<FDocDialogueCondition>& Conditions) const
{
	TArray<FDocConditionResult> Results;
	Results.Reserve(Conditions.Num());
	for (const FDocDialogueCondition& C : Conditions)
	{
		Results.Add(EvaluateCondition(S, C, S.Variables));
	}
	return FDocConditionResult::CombineAll(Results);
}

FDocDialogueChoiceView UDocDialogueSubsystem::EvaluateChoice(const FSession& S, const FDocDialogueChoice& Choice) const
{
	FDocDialogueChoiceView V;
	V.ChoiceId = Choice.ChoiceId;
	V.ChoiceText = Choice.ChoiceText;

	const FMemoryState* Mem = FindMemory(S.MemoryKey);
	if (Choice.bOnceOnly && Mem && Mem->OnceTaken.Contains(Choice.ChoiceId))
	{
		V.State = EDocDialogueChoiceState::Hidden;
		V.ReasonTag = DocDialogueTags::Error_Dialogue_ChoiceHidden;
		return V;
	}

	const bool bHideUnavailable = S.Graph->UnavailableChoicePolicy == EDocDialogueUnavailableChoicePolicy::Hide;
	auto Unavailable = [&](const FDocConditionResult& R)
	{
		V.State = bHideUnavailable ? EDocDialogueChoiceState::Hidden : EDocDialogueChoiceState::VisibleDisabled;
		V.ReasonTag = R.ReasonTag;
		V.FailureReason = !R.UserReason.IsEmpty() ? R.UserReason : Choice.FailureReason;
	};

	const FDocConditionResult Visible = FDocConditionResult::CombineAll({ EvaluateAll(S, Choice.Conditions), EvaluateAll(S, Choice.HiddenConditions) });
	if (Visible.State == EDocConditionState::Unsatisfied)
	{
		V.State = EDocDialogueChoiceState::Hidden;
		V.ReasonTag = Visible.ReasonTag;
		return V;
	}
	if (Visible.IsUnavailable())
	{
		Unavailable(Visible);
		return V;
	}
	const FDocConditionResult Enabled = EvaluateAll(S, Choice.DisabledConditions);
	if (Enabled.State == EDocConditionState::Unsatisfied)
	{
		V.State = EDocDialogueChoiceState::VisibleDisabled;
		V.ReasonTag = Enabled.ReasonTag;
		V.FailureReason = !Choice.FailureReason.IsEmpty() ? Choice.FailureReason : Enabled.UserReason;
		return V;
	}
	if (Enabled.IsUnavailable())
	{
		Unavailable(Enabled);
		return V;
	}
	V.State = EDocDialogueChoiceState::VisibleEnabled;
	return V;
}

FDocDialogueChoiceList UDocDialogueSubsystem::BuildChoiceList(const FSession& S) const
{
	FDocDialogueChoiceList List;
	List.NodeId = S.NodeId;
	List.SessionRevision = S.Revision;
	if (const FDocDialogueNode* Node = FindNode(S, S.NodeId))
	{
		for (const FDocDialogueChoice& Choice : Node->Choices)
		{
			FDocDialogueChoiceView View = EvaluateChoice(S, Choice);
			if (View.State != EDocDialogueChoiceState::Hidden)
			{
				List.Choices.Add(MoveTemp(View));
			}
		}
	}
	return List;
}

// ---------------------------------------------------------------------------
// Transitions and actions
// ---------------------------------------------------------------------------

FDocSystemResult UDocDialogueSubsystem::BeginTransition(FSession& S, FName FromNode, FName ChoiceId, FName Destination, const TArray<FDocDialogueAction>& Actions, bool bRunAfter)
{
	S.Pending = FPendingTransition();
	S.Pending.bActive = true;
	S.Pending.FromNode = FromNode;
	S.Pending.ChoiceId = ChoiceId;
	S.Pending.Destination = Destination;
	S.Pending.Actions = Actions;
	S.Pending.Ordinal = S.TransitionOrdinal + 1;
	S.Pending.StagedVariables = S.Variables;
	++S.Revision; // any reentrant submission against the old revision is now stale
	SetState(S, EDocDialogueSessionState::Running);

	FDocSystemResult Result;
	const EPendingProgress Progress = RunPendingActions(S, Result);
	if (Progress == EPendingProgress::Committed && bRunAfter && S.State == EDocDialogueSessionState::Running)
	{
		Run(S);
	}
	if (Progress == EPendingProgress::Waiting)
	{
		FDocSystemResult Accepted = FDocSystemResult::MakeSuccess();
		Accepted.Diagnostic = TEXT("Accepted; waiting for action");
		return Accepted;
	}
	return Result;
}

UDocDialogueSubsystem::EPendingProgress UDocDialogueSubsystem::RunPendingActions(FSession& S, FDocSystemResult& OutResult)
{
	FPendingTransition& P = S.Pending;
	while (P.NextAction < P.Actions.Num())
	{
		const FDocDialogueAction Action = P.Actions[P.NextAction];
		if (Action.Timing == EDocDialogueActionTiming::AfterTransition)
		{
			++P.NextAction;
			continue;
		}
		const FDocDialogueActionOutcome Outcome = ExecuteAction(S, Action, P.StagedVariables, P.Ordinal, P.FromNode, P.ChoiceId);
		if (Outcome.State == EDocDialogueActionState::Pending)
		{
			P.InFlightAction = Action.ActionId;
			EnterWaiting(S, EDocDialogueSessionState::WaitingForAction);
			OutResult = FDocSystemResult::MakeSuccess();
			return EPendingProgress::Waiting;
		}
		if (Outcome.State == EDocDialogueActionState::Failed && Action.bRequired)
		{
			OutResult = Outcome.Result;
			RejectTransition(S, Outcome.Result);
			return EPendingProgress::Rejected;
		}
		++P.NextAction;
	}
	CommitTransition(S);
	OutResult = FDocSystemResult::MakeSuccess();
	return EPendingProgress::Committed;
}

void UDocDialogueSubsystem::ApplyCompletion(FSession& S, const FDocSystemResult& Result)
{
	FPendingTransition& P = S.Pending;
	if (!P.bActive || !P.Actions.IsValidIndex(P.NextAction))
	{
		return;
	}
	const FDocDialogueAction Action = P.Actions[P.NextAction];
	const FDocEffectKey Key = MakeEffectKey(S, P.Ordinal, Action.ActionId);
	P.InFlightAction = NAME_None;
	if (Result.IsSuccess())
	{
		FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
		FDocEffectReceipt Receipt;
		Receipt.Key = Key;
		Receipt.PayloadHash = HashAction(Action);
		Receipt.Result = Result;
		Receipt.CommittedRevision = P.Ordinal;
		Mem.Ledger.Record(Receipt);
		RecordAction(S, Key, EDocDialogueActionState::Committed, Result);
		S.Committed.Add(FCommittedAction{ Key, Action, P.FromNode, P.ChoiceId });
	}
	else
	{
		RecordAction(S, Key, EDocDialogueActionState::Failed, Result);
		if (Action.bRequired)
		{
			RejectTransition(S, Result);
			return;
		}
	}
	++P.NextAction;
	SetState(S, EDocDialogueSessionState::Running);
	FDocSystemResult Ignored;
	if (RunPendingActions(S, Ignored) == EPendingProgress::Committed && S.State == EDocDialogueSessionState::Running)
	{
		Run(S);
	}
}

void UDocDialogueSubsystem::CommitTransition(FSession& S)
{
	FPendingTransition P = MoveTemp(S.Pending);
	S.Pending = FPendingTransition();
	S.TransitionOrdinal = P.Ordinal;
	S.Variables = MoveTemp(P.StagedVariables);

	FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
	if (!P.ChoiceId.IsNone())
	{
		FDocDialogueChoiceRecord Record;
		Record.NodeId = P.FromNode;
		Record.ChoiceId = P.ChoiceId;
		Record.SessionId = S.SessionId;
		Record.TransitionOrdinal = P.Ordinal;
		Mem.Data.ChoiceHistory.Add(Record);
		const FDocDialogueNode* From = FindNode(S, P.FromNode);
		const FDocDialogueChoice* Choice = From ? From->Choices.FindByPredicate([&P](const FDocDialogueChoice& C) { return C.ChoiceId == P.ChoiceId; }) : nullptr;
		if (Choice && Choice->bOnceOnly && !Mem.OnceTaken.Contains(P.ChoiceId))
		{
			Mem.OnceTaken.Add(P.ChoiceId);
			Mem.Data.OnceChoicesTaken.Add(P.ChoiceId);
		}
	}

	// Cosmetic/after actions: never block or undo the committed transition.
	for (const FDocDialogueAction& Action : P.Actions)
	{
		if (Action.Timing != EDocDialogueActionTiming::AfterTransition)
		{
			continue;
		}
		TMap<FName, FDocDialogueValue> Staged = S.Variables;
		const FDocDialogueActionOutcome Outcome = ExecuteAction(S, Action, Staged, P.Ordinal, P.FromNode, P.ChoiceId);
		if (IsInternalAction(Action.Type) && Outcome.State == EDocDialogueActionState::Committed)
		{
			S.Variables = MoveTemp(Staged);
		}
		else if (Outcome.State == EDocDialogueActionState::Pending)
		{
			UE_LOG(LogDocDialogue, Verbose, TEXT("After-transition action %s is pending; not awaited"), *Action.ActionId.ToString());
		}
	}
	CommitPersistentVariables(S);
	SetState(S, EDocDialogueSessionState::Running);
	GoTo(S, P.Destination);
}

void UDocDialogueSubsystem::RejectTransition(FSession& S, const FDocSystemResult& Why)
{
	// Earlier committed actions in this transition keep their receipts: a retry of the same
	// transition (same ordinal) finds them as Duplicate instead of applying them twice.
	const FName ChoiceId = S.Pending.ChoiceId;
	const FName FromNode = S.Pending.FromNode;
	S.Pending = FPendingTransition();
	if (!ChoiceId.IsNone())
	{
		S.NodeId = FromNode;
		EnterWaiting(S, EDocDialogueSessionState::WaitingForChoice);
		PresentChoices(S);
		return;
	}
	Fail(S, Why.Outcome, DocDialogueTags::Error_Dialogue_ActionFailed,
		FString::Printf(TEXT("Required action failed at node %s: %s"), *FromNode.ToString(), *Why.Diagnostic));
}

FDocEffectKey UDocDialogueSubsystem::MakeEffectKey(const FSession& S, int64 Ordinal, FName ActionId)
{
	FDocEffectKey Key;
	Key.Owner = S.MemoryKey.Owner;
	Key.CampaignEpoch = GetOrCreateMemory(S.MemoryKey, S.Graph->Version).Data.CampaignEpoch;
	Key.ProducerInstanceId = S.SessionId;
	Key.TransitionOrdinal = Ordinal;
	Key.ActionId = ActionId;
	return Key;
}

int64 UDocDialogueSubsystem::HashAction(const FDocDialogueAction& A)
{
	const FString Canonical = FString::Printf(TEXT("%d|%s|%s|%s|%s|%s|%s|%s"),
		static_cast<int32>(A.Type), *A.ActionId.ToString(), *A.VariableName.ToString(), *A.Value.ToString(),
		*A.Role.ToString(), *A.Tag.ToString(), *A.ProviderId.ToString(), *A.Payload);
	return FDocReceiptLedger::HashString(Canonical);
}

void UDocDialogueSubsystem::RecordAction(FSession& S, const FDocEffectKey& Key, EDocDialogueActionState State, const FDocSystemResult& Result)
{
	FDocDialogueActionRecord* Existing = S.ActionLog.FindByPredicate([&Key](const FDocDialogueActionRecord& R) { return R.Key == Key; });
	if (!Existing)
	{
		Existing = &S.ActionLog.AddDefaulted_GetRef();
		Existing->Key = Key;
	}
	Existing->State = State;
	Existing->Result = Result;
}

IDocDialogueActionProvider* UDocDialogueSubsystem::FindActionProvider(const FDocDialogueAction& Action) const
{
	FName ProviderId = Action.ProviderId;
	if (Action.Type == EDocDialogueActionType::BroadcastEvent && ProviderId.IsNone())
	{
		ProviderId = Settings()->EventProviderId;
	}
	const TSharedPtr<IDocDialogueActionProvider>* Provider = ActionProviders.Find(ProviderId);
	return Provider ? Provider->Get() : nullptr;
}

FDocDialogueActionOutcome UDocDialogueSubsystem::ExecuteAction(FSession& S, const FDocDialogueAction& A, TMap<FName, FDocDialogueValue>& Staged, int64 Ordinal, FName NodeId, FName ChoiceId)
{
	const FDocEffectKey Key = MakeEffectKey(S, Ordinal, A.ActionId);

	if (IsInternalAction(A.Type))
	{
		const FDocDialogueVariableDecl* Decl = S.Graph->FindVariable(A.VariableName);
		FDocDialogueActionOutcome Out;
		if (!Decl)
		{
			Out = FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
				FString::Printf(TEXT("Undeclared variable %s"), *A.VariableName.ToString()), DocDialogueTags::Error_Dialogue_VariableMissing));
		}
		else
		{
			FDocDialogueValue NewValue;
			bool bOk = false;
			if (A.Type == EDocDialogueActionType::SetVariable)
			{
				bOk = A.Value.CoerceTo(Decl->Type, NewValue);
			}
			else
			{
				const FDocDialogueValue* Current = Staged.Find(A.VariableName);
				double Base = 0.0, Delta = 0.0;
				if (Current && Current->ToNumber(Base) && A.Value.ToNumber(Delta))
				{
					const FDocDialogueValue Sum = (Decl->Type == EDocDialogueValueType::Int && A.Value.Type != EDocDialogueValueType::Float)
						? FDocDialogueValue::MakeInt(Current->Int + static_cast<int64>(FMath::RoundHalfFromZero(Delta)))
						: FDocDialogueValue::MakeFloat(Base + Delta);
					bOk = Sum.CoerceTo(Decl->Type, NewValue);
				}
			}
			if (bOk)
			{
				Staged.Add(A.VariableName, NewValue);
				Out = FDocDialogueActionOutcome::Committed();
			}
			else
			{
				Out = FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
					FString::Printf(TEXT("Action %s: value %s is not valid for %s"), *A.ActionId.ToString(), *A.Value.ToString(), *A.VariableName.ToString()),
					DocDialogueTags::Error_Dialogue_VariableType));
			}
		}
		RecordAction(S, Key, Out.State, Out.Result);
		return Out;
	}

	// External effects: receipts make retries of the same transition idempotent.
	FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
	const int64 Hash = HashAction(A);
	FDocEffectReceipt Existing;
	const EDocReceiptCheck Check = Mem.Ledger.Check(Key, Hash, &Existing);
	if (Check == EDocReceiptCheck::Duplicate)
	{
		FDocDialogueActionOutcome Dup;
		Dup.State = EDocDialogueActionState::Duplicate;
		Dup.Result = Existing.Result;
		RecordAction(S, Key, EDocDialogueActionState::Duplicate, Existing.Result);
		return Dup;
	}
	if (Check == EDocReceiptCheck::Conflict)
	{
		const FDocDialogueActionOutcome Out = FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Effect key %s reused with a different payload"), *Key.ToString())));
		RecordAction(S, Key, Out.State, Out.Result);
		return Out;
	}

	FDocDialogueActionOutcome Out;
	if (A.Type == EDocDialogueActionType::GrantTag || A.Type == EDocDialogueActionType::RemoveTag)
	{
		UObject* Participant = GetRoleObject(S, A.Role);
		UObject* Target = nullptr;
		if (Participant && Participant->GetClass()->ImplementsInterface(UDocMutableGameplayTagProvider::StaticClass()))
		{
			Target = Participant;
		}
		else if (Participant && ImplementsParticipant(Participant))
		{
			AActor* Actor = IDocDialogueParticipant::Execute_GetDialogueActor(Participant);
			if (Actor && Actor->GetClass()->ImplementsInterface(UDocMutableGameplayTagProvider::StaticClass()))
			{
				Target = Actor;
			}
		}
		if (!Target)
		{
			Out = FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
				FString::Printf(TEXT("Role %s has no IDocMutableGameplayTagProvider"), *A.Role.ToString()), DocDialogueTags::Error_Dialogue_ProviderMissing));
		}
		else
		{
			FDocGameplayContext Context = FDocGameplayContext::Make(GetWorld(), Cast<AActor>(S.Initiator.Get()), Cast<AActor>(Target));
			Context.CorrelationId = Ordinal;
			FGameplayTagContainer Add, Remove;
			(A.Type == EDocDialogueActionType::GrantTag ? Add : Remove).AddTag(A.Tag);
			const FDocSystemResult R = IDocMutableGameplayTagProvider::Execute_ApplyDocTagDelta(Target, Context, Add, Remove);
			Out = R.IsSuccess() ? FDocDialogueActionOutcome::Committed() : FDocDialogueActionOutcome::Failed(R);
			if (R.IsSuccess()) { Out.Result = R; }
		}
	}
	else
	{
		IDocDialogueActionProvider* Provider = FindActionProvider(A);
		if (!Provider)
		{
			Out = FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable,
				FString::Printf(TEXT("No action provider for %s"), *A.ActionId.ToString()), DocDialogueTags::Error_Dialogue_ProviderMissing));
		}
		else
		{
			FDocDialogueActionRequest Request;
			Request.Action = &A;
			Request.EffectKey = Key;
			Request.GraphId = S.Graph->GraphId;
			Request.NodeId = NodeId;
			Request.ChoiceId = ChoiceId;
			Request.Participants = GetParticipantMap(S);
			Request.World = GetWorld();
			Out = Provider->ExecuteAction(Request);
			if (Out.State == EDocDialogueActionState::Committed && !Out.Result.IsSuccess())
			{
				Out.State = EDocDialogueActionState::Failed; // a failure result is never a commit
			}
		}
	}

	if (Out.State == EDocDialogueActionState::Committed)
	{
		FDocEffectReceipt Receipt;
		Receipt.Key = Key;
		Receipt.PayloadHash = Hash;
		Receipt.Result = Out.Result;
		Receipt.CommittedRevision = Ordinal;
		Mem.Ledger.Record(Receipt);
		S.Committed.Add(FCommittedAction{ Key, A, NodeId, ChoiceId });
	}
	else if (Out.State == EDocDialogueActionState::Failed)
	{
		UE_LOG(LogDocDialogue, Log, TEXT("Dialogue action %s failed: %s"), *A.ActionId.ToString(), *Out.Result.ToString());
	}
	RecordAction(S, Key, Out.State, Out.Result);
	return Out;
}

// ---------------------------------------------------------------------------
// Participants
// ---------------------------------------------------------------------------

void UDocDialogueSubsystem::RegisterParticipant(UObject* Participant)
{
	if (!ImplementsParticipant(Participant))
	{
		return;
	}
	const FName Id = IDocDialogueParticipant::Execute_GetDialogueParticipantId(Participant);
	if (Id.IsNone())
	{
		return;
	}
	FApiScope Scope(*this);
	RegisteredParticipants.Add(Id, Participant);
	for (TPair<FGuid, TUniquePtr<FSession>>& Pair : Sessions)
	{
		FSession* S = Pair.Value.Get();
		if (!S || S->bTerminal)
		{
			continue;
		}
		bool bRebound = false;
		for (FBinding& B : S->Bindings)
		{
			if (B.ParticipantId == Id && (B.bLost || !B.Participant.IsValid()))
			{
				B.Participant = Participant; // same world only: this subsystem is per world
				B.bLost = false;
				bRebound = true;
			}
		}
		const bool bStillWaiting = S->Bindings.ContainsByPredicate([](const FBinding& B) { return B.bLost && B.LossPolicy == EDocDialogueLossPolicy::RebindByIdentity; });
		if (bRebound && S->State == EDocDialogueSessionState::Paused && S->bPausedForRebind && !bStillWaiting)
		{
			ResumeInternal(*S);
		}
	}
}

void UDocDialogueSubsystem::UnregisterParticipant(UObject* Participant, FGameplayTag Reason)
{
	if (!ImplementsParticipant(Participant))
	{
		return;
	}
	const FName Id = IDocDialogueParticipant::Execute_GetDialogueParticipantId(Participant);
	if (const TWeakObjectPtr<UObject>* Registered = RegisteredParticipants.Find(Id))
	{
		if (Registered->Get() == Participant || !Registered->IsValid())
		{
			RegisteredParticipants.Remove(Id);
		}
	}
	HandleLoss(Id, Participant, Reason);
}

void UDocDialogueSubsystem::NotifyParticipantLost(FName ParticipantId, FGameplayTag Reason)
{
	HandleLoss(ParticipantId, nullptr, Reason);
}

void UDocDialogueSubsystem::HandleLoss(FName ParticipantId, const UObject* OnlyObject, FGameplayTag Reason)
{
	if (ParticipantId.IsNone())
	{
		return;
	}
	FApiScope Scope(*this);
	TArray<FGuid> Ids;
	Sessions.GetKeys(Ids);
	for (const FGuid& Id : Ids)
	{
		TUniquePtr<FSession>* Found = Sessions.Find(Id);
		FSession* S = Found ? Found->Get() : nullptr;
		if (!S || S->bTerminal)
		{
			continue;
		}
		for (FBinding& B : S->Bindings)
		{
			if (B.ParticipantId != ParticipantId || (OnlyObject && B.Participant.IsValid() && B.Participant.Get() != OnlyObject))
			{
				continue;
			}
			B.Participant = nullptr;
			const FString Why = FString::Printf(TEXT("Participant %s lost (%s)"), *ParticipantId.ToString(), Reason.IsValid() ? *Reason.ToString() : TEXT("unloaded"));
			switch (B.LossPolicy)
			{
			case EDocDialogueLossPolicy::Cancel:
				B.bLost = true;
				Terminate(*S, EDocDialogueSessionState::Cancelled,
					FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, Why, DocDialogueTags::Error_Dialogue_ParticipantLost));
				break;
			case EDocDialogueLossPolicy::PauseWithTimeout:
				B.bLost = true;
				PauseInternal(*S, B.PauseTimeoutSeconds, false);
				break;
			case EDocDialogueLossPolicy::ContinueWithoutActor:
				B.bLost = false; // logical participant continues; conditions on it report Unavailable
				break;
			case EDocDialogueLossPolicy::RebindByIdentity:
				B.bLost = true;
				PauseInternal(*S, B.PauseTimeoutSeconds, true);
				break;
			}
			if (S->bTerminal)
			{
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Providers
// ---------------------------------------------------------------------------

void UDocDialogueSubsystem::RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocDialogueConditionProvider> Provider)
{
	if (Provider.IsValid()) { ConditionProviders.Add(ProviderId, Provider); } else { ConditionProviders.Remove(ProviderId); }
}

void UDocDialogueSubsystem::RegisterActionProvider(FName ProviderId, TSharedPtr<IDocDialogueActionProvider> Provider)
{
	if (Provider.IsValid()) { ActionProviders.Add(ProviderId, Provider); } else { ActionProviders.Remove(ProviderId); }
}

void UDocDialogueSubsystem::RegisterCustomNodeHandler(FName CustomType, TSharedPtr<IDocDialogueCustomNodeHandler> Handler)
{
	if (Handler.IsValid()) { CustomNodeHandlers.Add(CustomType, Handler); } else { CustomNodeHandlers.Remove(CustomType); }
}

void UDocDialogueSubsystem::SetVoiceDurationResolver(TFunction<float(const TSoftObjectPtr<USoundBase>&)> Resolver)
{
	if (Resolver)
	{
		VoiceDurationResolver = MoveTemp(Resolver);
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void UDocDialogueSubsystem::CommitPersistentVariables(FSession& S)
{
	FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
	for (const FDocDialogueVariableDecl& Decl : S.Graph->Variables)
	{
		if (!Decl.bPersistent)
		{
			continue;
		}
		const FDocDialogueValue* Value = S.Variables.Find(Decl.Name);
		if (!Value)
		{
			continue;
		}
		FDocDialogueVariableEntry* Entry = Mem.Data.PersistentVariables.FindByPredicate([&Decl](const FDocDialogueVariableEntry& E) { return E.Name == Decl.Name; });
		if (!Entry)
		{
			Entry = &Mem.Data.PersistentVariables.AddDefaulted_GetRef();
			Entry->Name = Decl.Name;
		}
		Entry->Value = *Value;
	}
}

void UDocDialogueSubsystem::WriteCheckpoint(FSession& S)
{
	if (!S.Graph->bCheckpointsEnabled || !S.MemoryKey.Owner.IsPersistable())
	{
		return;
	}
	FMemoryState& Mem = GetOrCreateMemory(S.MemoryKey, S.Graph->Version);
	FDocDialogueCheckpoint C;
	C.bValid = true;
	C.SessionId = S.SessionId;
	C.TransitionOrdinal = S.TransitionOrdinal;
	// Re-entering a line on resume increments the ordinal again; store the pre-line value.
	C.LineOrdinal = S.State == EDocDialogueSessionState::WaitingForAdvance ? S.LineOrdinal - 1 : S.LineOrdinal;
	C.NodeId = S.Pending.bActive ? S.Pending.FromNode : S.NodeId;
	for (const TPair<FName, FDocDialogueValue>& Pair : S.Variables)
	{
		FDocDialogueVariableEntry& Entry = C.Variables.AddDefaulted_GetRef();
		Entry.Name = Pair.Key;
		Entry.Value = Pair.Value; // committed values only (staged changes are not in S.Variables)
	}
	for (const FBinding& B : S.Bindings)
	{
		FDocDialogueRoleBinding& RB = C.RoleBindings.AddDefaulted_GetRef();
		RB.RoleId = B.RoleId;
		RB.ParticipantId = B.ParticipantId;
	}
	if (S.Pending.bActive)
	{
		C.bPendingTransition = true;
		C.PendingChoiceId = S.Pending.ChoiceId;
		C.InFlightActionId = S.Pending.InFlightAction;
	}
	Mem.Data.Checkpoint = C;
}

void UDocDialogueSubsystem::ClearCheckpoint(FSession& S)
{
	if (FMemoryState* Mem = FindMemory(S.MemoryKey))
	{
		if (Mem->Data.Checkpoint.SessionId == S.SessionId)
		{
			Mem->Data.Checkpoint = FDocDialogueCheckpoint();
		}
	}
}

FDocDialogueSaveData UDocDialogueSubsystem::CaptureState() const
{
	FDocDialogueSaveData Data;
	for (const TPair<FMemoryKey, TUniquePtr<FMemoryState>>& Pair : Memories)
	{
		if (!Pair.Value || !Pair.Key.Owner.IsPersistable())
		{
			continue; // session-scoped memories are never persisted
		}
		FDocDialogueMemory Memory = Pair.Value->Data;
		Memory.Receipts = Pair.Value->Ledger.GetAll();
		Data.Memories.Add(MoveTemp(Memory));
	}
	Data.Memories.Sort([](const FDocDialogueMemory& A, const FDocDialogueMemory& B)
	{
		const int32 ByGraph = A.GraphId.Compare(B.GraphId);
		return ByGraph != 0 ? ByGraph < 0 : A.Owner.ToString() < B.Owner.ToString();
	});
	return Data;
}

FDocSystemResult UDocDialogueSubsystem::RestoreState(const FDocDialogueSaveData& Data)
{
	FApiScope Scope(*this);
	for (const TPair<FGuid, TUniquePtr<FSession>>& Pair : Sessions)
	{
		if (Pair.Value && !Pair.Value->bTerminal)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("End active dialogue sessions before restoring"));
		}
	}
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocDialogueSaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unsupported dialogue schema %d"), Data.SchemaVersion));
	}
	TSet<FMemoryKey> Seen;
	for (const FDocDialogueMemory& M : Data.Memories)
	{
		if (M.GraphId.IsNone() || !M.Owner.IsPersistable())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Memory without graph or persistable owner"));
		}
		bool bDup = false;
		Seen.Add(FMemoryKey{ M.GraphId, M.Owner }, &bDup);
		if (bDup)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Duplicate memory for graph %s"), *M.GraphId.ToString()));
		}
		if (M.Checkpoint.bValid && (M.Checkpoint.NodeId.IsNone() || !M.Checkpoint.SessionId.IsValid()))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Malformed checkpoint for graph %s"), *M.GraphId.ToString()));
		}
	}

	// Validated: apply.
	Memories.Reset();
	for (const FDocDialogueMemory& M : Data.Memories)
	{
		TUniquePtr<FMemoryState> State = MakeUnique<FMemoryState>(Settings()->MaxReceiptsPerMemory);
		State->Data = M;
		State->Data.Receipts.Reset();
		if (!State->Data.CampaignEpoch.IsValid())
		{
			State->Data.CampaignEpoch = FGuid::NewGuid();
		}
		State->Ledger.RestoreAll(M.Receipts);
		State->Visited.Append(M.VisitedNodes);
		State->OnceTaken.Append(M.OnceChoicesTaken);
		Memories.Add(FMemoryKey{ M.GraphId, M.Owner }, MoveTemp(State));
	}
	SessionHandles.Reset(); // every older handle is stale
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// UDocDialogueParticipantComponent
// ---------------------------------------------------------------------------

UDocDialogueParticipantComponent::UDocDialogueParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocDialogueParticipantComponent::GetDialogueTags_Implementation(FGameplayTagContainer& OutTags) const
{
	OutTags.AppendTags(Tags);
	if (const AActor* Owner = GetOwner())
	{
		FGameplayTagContainer OwnerTags;
		UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(Owner, OwnerTags);
		OutTags.AppendTags(OwnerTags);
	}
}

void UDocDialogueParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocDialogueSubsystem* Subsystem = UDocDialogueSubsystem::Get(this))
	{
		Subsystem->RegisterParticipant(this);
	}
}

void UDocDialogueParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocDialogueSubsystem* Subsystem = UDocDialogueSubsystem::Get(this))
	{
		Subsystem->UnregisterParticipant(this, FGameplayTag());
	}
	Super::EndPlay(EndPlayReason);
}
