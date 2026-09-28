#include "DocStreamingSubsystem.h"
#include "DocStreamingLog.h"
#include "DocCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocStreamingSubsystem)

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

#if WITH_EDITOR
EDataValidationResult UDocStreamingChunkDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Backend == EDocStreamingBackend::LevelInstance && Level.IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: LevelInstance backend needs a Level"), *GetPathName())));
		Result = EDataValidationResult::Invalid;
	}
	if (UnloadDistance < LoadDistance)
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: UnloadDistance must be >= LoadDistance (hysteresis)"), *GetPathName())));
		Result = EDataValidationResult::Invalid;
	}
	TSet<const UDocStreamingChunkDefinition*> Visiting;
	TFunction<bool(const UDocStreamingChunkDefinition*)> HasCycle = [&](const UDocStreamingChunkDefinition* Def) -> bool
	{
		if (!Def) { return false; }
		if (Visiting.Contains(Def)) { return true; }
		Visiting.Add(Def);
		for (const TObjectPtr<UDocStreamingChunkDefinition>& Dep : Def->Dependencies)
		{
			if (HasCycle(Dep)) { return true; }
		}
		Visiting.Remove(Def);
		return false;
	};
	if (HasCycle(this))
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: dependency cycle"), *GetPathName())));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

const FDocWorldStateVariant* UDocWorldStateGroup::FindVariant(FGameplayTag StateTag) const
{
	return Variants.FindByPredicate([StateTag](const FDocWorldStateVariant& V) { return V.StateTag == StateTag; });
}

// ---------------------------------------------------------------------------
// Subsystem basics
// ---------------------------------------------------------------------------

UDocWorldStreamingSubsystem* UDocWorldStreamingSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocWorldStreamingSubsystem>() : nullptr;
}

bool UDocWorldStreamingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDocWorldStreamingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocWorldStreamingSubsystem, STATGROUP_Tickables);
}

double UDocWorldStreamingSubsystem::Now() const
{
	return FPlatformTime::Seconds();
}

void UDocWorldStreamingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PollNow();
}

void UDocWorldStreamingSubsystem::Deinitialize()
{
	bShuttingDown = true;
	TArray<FDocRequestHandle> All;
	Leases.ForEach([&All](const FDocRequestHandle& H, const FLease& L) { if (!L.bInternal) { All.Add(H); } });
	for (const FDocRequestHandle& H : All)
	{
		ReleaseLeaseInternal(H, EDocStreamingRequestState::Cancelled,
			FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("World teardown")), false);
	}
	Leases.Reset();
	Instances.Reset();
	NativeLevels.Reset();
	OwnedStreamingLevels.Reset(); // the world tears its streaming levels down itself
	WorldStateRequests.Reset();
	Groups.Reset();
	OnRequestChanged.Clear();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Graph validation and lease acquisition
// ---------------------------------------------------------------------------

bool UDocWorldStreamingSubsystem::ValidateGraph(const UDocStreamingChunkDefinition* Root, FString& OutError) const
{
	TSet<const UDocStreamingChunkDefinition*> Visiting;
	TSet<const UDocStreamingChunkDefinition*> Done;
	TFunction<bool(const UDocStreamingChunkDefinition*)> Visit = [&](const UDocStreamingChunkDefinition* Def) -> bool
	{
		if (!Def)
		{
			OutError = TEXT("Missing (null) chunk definition in dependency graph");
			return false;
		}
		if (Done.Contains(Def)) { return true; }
		if (Visiting.Contains(Def))
		{
			OutError = FString::Printf(TEXT("Dependency cycle through %s"), *Def->GetEffectiveChunkId().ToString());
			return false;
		}
		if (Def->Backend == EDocStreamingBackend::BridgeDefined)
		{
			OutError = FString::Printf(TEXT("%s uses a bridge-defined backend; no bridge is installed"), *Def->GetEffectiveChunkId().ToString());
			return false;
		}
		if (Def->Level.IsNull())
		{
			OutError = FString::Printf(TEXT("%s has no Level"), *Def->GetEffectiveChunkId().ToString());
			return false;
		}
		Visiting.Add(Def);
		for (const TObjectPtr<UDocStreamingChunkDefinition>& Dep : Def->Dependencies)
		{
			if (!Visit(Dep)) { return false; }
		}
		Visiting.Remove(Def);
		Done.Add(Def);
		return true;
	};
	return Visit(Root);
}

FDocStreamingRequestInfo UDocWorldStreamingSubsystem::RequestChunk(UDocStreamingChunkDefinition* Definition, UObject* Owner, FGuid InstanceScope, bool bUseTransform, FTransform Transform)
{
	FDocStreamingRequestInfo Info;
	Info.State = EDocStreamingRequestState::Failed;
	if (bShuttingDown)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Streaming subsystem shutting down"));
		return Info;
	}
	if (!Definition)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null chunk definition"));
		return Info;
	}
	Info.Key = FDocChunkInstanceKey{ Definition->GetEffectiveChunkId(), InstanceScope };

	FString GraphError;
	if (!ValidateGraph(Definition, GraphError))
	{
		const EDocResultOutcome Outcome = GraphError.Contains(TEXT("bridge")) ? EDocResultOutcome::Unsupported : EDocResultOutcome::InvalidConfiguration;
		Info.Result = FDocSystemResult::MakeFailure(Outcome, GraphError);
		return Info;
	}

	FDocSystemResult Result;
	const FDocRequestHandle Handle = AcquireLease(Definition, Owner, InstanceScope, bUseTransform ? Transform : Definition->DefaultTransform, false, Result);
	if (!Handle.IsSet())
	{
		Info.Result = Result;
		return Info;
	}
	UpdatePendingLeases();
	if (const FLease* Lease = Leases.Find(Handle, this))
	{
		return MakeInfo(Handle, *Lease);
	}
	// Failed synchronously and was released during UpdatePendingLeases.
	Info.Handle = Handle;
	Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Request failed immediately"));
	return Info;
}

FDocRequestHandle UDocWorldStreamingSubsystem::AcquireLease(UDocStreamingChunkDefinition* Definition, UObject* Owner, const FGuid& Scope,
	const FTransform& Transform, bool bInternal, FDocSystemResult& OutResult)
{
	// Dependencies first (default placement scope), as independent internal leases.
	TArray<FDocRequestHandle> DepLeases;
	for (const TObjectPtr<UDocStreamingChunkDefinition>& Dep : Definition->Dependencies)
	{
		FDocSystemResult DepResult;
		const FDocRequestHandle DepHandle = AcquireLease(Dep, nullptr, FGuid(), Dep->DefaultTransform, true, DepResult);
		if (!DepHandle.IsSet())
		{
			for (const FDocRequestHandle& Acquired : DepLeases)
			{
				ReleaseLeaseInternal(Acquired, EDocStreamingRequestState::Cancelled, DepResult, false);
			}
			OutResult = FDocSystemResult::MakeFailure(DepResult.Outcome == EDocResultOutcome::Unset ? EDocResultOutcome::Failed : DepResult.Outcome,
				FString::Printf(TEXT("Dependency %s failed: %s"), *Dep->GetEffectiveChunkId().ToString(), *DepResult.Diagnostic));
			if (OutResult.IsSuccess()) { OutResult.Outcome = EDocResultOutcome::Failed; }
			return FDocRequestHandle();
		}
		DepLeases.Add(DepHandle);
	}

	const FDocChunkInstanceKey Key{ Definition->GetEffectiveChunkId(), Scope };
	FInstance* Instance = Instances.Find(Key);
	if (Instance)
	{
		const UDocStreamingChunkDefinition* ExistingDef = Instance->Definition.Get();
		if ((ExistingDef && ExistingDef != Definition) || !Instance->Transform.Equals(Transform, 0.1))
		{
			for (const FDocRequestHandle& Acquired : DepLeases)
			{
				ReleaseLeaseInternal(Acquired, EDocStreamingRequestState::Cancelled, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Unwound")), false);
			}
			OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("Instance %s already exists with a different definition or transform; use a distinct InstanceScope"), *Key.ToString()));
			return FDocRequestHandle();
		}
	}
	else
	{
		Instance = &Instances.Add(Key);
		Instance->Key = Key;
		Instance->Definition = Definition;
		Instance->Transform = Transform;
	}

	FLease Lease;
	Lease.Key = Key;
	Lease.Owner = Owner;
	Lease.bOwnerWasSet = Owner != nullptr;
	Lease.DependencyLeases = MoveTemp(DepLeases);
	Lease.bInternal = bInternal;
	Lease.StartTime = Now();
	Lease.Timeout = Definition->TimeoutSeconds;
	const FDocRequestHandle Handle = Leases.Add(this, MoveTemp(Lease));

	Instance = Instances.Find(Key); // re-find: recursion above may have grown the map
	Instance->Leases.Add(Handle);
	Instance->bUnloadRequested = false;
	if (Instance->Observed == EDocChunkObservedState::Unrequested || Instance->Observed == EDocChunkObservedState::Failed
		|| Instance->Observed == EDocChunkObservedState::Unloading)
	{
		BeginNativeLoad(*Instance);
	}
	OutResult = FDocSystemResult::MakeSuccess(Handle.GetOperationId());
	return Handle;
}

// ---------------------------------------------------------------------------
// Native backend
// ---------------------------------------------------------------------------

void UDocWorldStreamingSubsystem::BeginNativeLoad(FInstance& Instance)
{
	Instance.LastFailure.Reset();
	Instance.Observed = EDocChunkObservedState::Loading;
	if (!bNativeBackendEnabled)
	{
		return;
	}
	UDocStreamingChunkDefinition* Definition = Instance.Definition.Get();
	UWorld* World = GetWorld();
	if (!Definition || !World)
	{
		Instance.Observed = EDocChunkObservedState::Failed;
		Instance.LastFailure = TEXT("Definition or world missing");
		return;
	}
	bool bSuccess = false;
	ULevelStreamingDynamic* Level = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(World, Definition->Level, Instance.Transform, bSuccess);
	if (!bSuccess || !Level)
	{
		Instance.Observed = EDocChunkObservedState::Failed;
		Instance.LastFailure = FString::Printf(TEXT("LoadLevelInstance failed for %s"), *Definition->Level.ToString());
		return;
	}
	Level->SetShouldBeVisible(true);
	NativeLevels.Add(Instance.Key, Level);
	OwnedStreamingLevels.Add(Level);
}

void UDocWorldStreamingSubsystem::BeginNativeUnload(FInstance& Instance)
{
	Instance.bUnloadRequested = true;
	Instance.Observed = EDocChunkObservedState::Unloading;
	if (TWeakObjectPtr<ULevelStreamingDynamic>* Weak = NativeLevels.Find(Instance.Key))
	{
		if (ULevelStreamingDynamic* Level = Weak->Get())
		{
			Level->SetShouldBeVisible(false);
			Level->SetShouldBeLoaded(false);
			Level->SetIsRequestingUnloadAndRemoval(true);
			OwnedStreamingLevels.Remove(Level);
		}
		NativeLevels.Remove(Instance.Key);
	}
}

void UDocWorldStreamingSubsystem::RefreshObserved(FInstance& Instance)
{
	if (!bNativeBackendEnabled || Instance.bUnloadRequested)
	{
		return;
	}
	const TWeakObjectPtr<ULevelStreamingDynamic>* Weak = NativeLevels.Find(Instance.Key);
	const ULevelStreamingDynamic* Level = Weak ? Weak->Get() : nullptr;
	if (!Level)
	{
		if (Instance.Observed != EDocChunkObservedState::Failed && Instance.Observed != EDocChunkObservedState::Unrequested)
		{
			Instance.Observed = EDocChunkObservedState::Failed;
			Instance.LastFailure = TEXT("Native streaming level disappeared");
		}
		return;
	}
	switch (Level->GetLevelStreamingState())
	{
	case ELevelStreamingState::FailedToLoad:
		Instance.Observed = EDocChunkObservedState::Failed;
		Instance.LastFailure = TEXT("Native streaming reported FailedToLoad");
		break;
	case ELevelStreamingState::Loading:
	case ELevelStreamingState::Unloaded:
		Instance.Observed = EDocChunkObservedState::Loading;
		break;
	case ELevelStreamingState::LoadedNotVisible:
		Instance.Observed = EDocChunkObservedState::Loaded;
		break;
	case ELevelStreamingState::MakingVisible:
		Instance.Observed = EDocChunkObservedState::MakingVisible;
		break;
	case ELevelStreamingState::LoadedVisible:
		Instance.Observed = EDocChunkObservedState::Ready;
		break;
	case ELevelStreamingState::Removed:
	case ELevelStreamingState::MakingInvisible:
	default:
		Instance.Observed = EDocChunkObservedState::Unloading;
		break;
	}
}

bool UDocWorldStreamingSubsystem::IsInstanceReady(const FInstance& Instance) const
{
	const UDocStreamingChunkDefinition* Definition = Instance.Definition.Get();
	if (Instance.Observed == EDocChunkObservedState::Ready)
	{
		return true;
	}
	return Definition && Definition->Readiness == EDocChunkReadiness::Loaded
		&& (Instance.Observed == EDocChunkObservedState::Loaded || Instance.Observed == EDocChunkObservedState::MakingVisible);
}

bool UDocWorldStreamingSubsystem::AreDependenciesReady(const FLease& Lease) const
{
	for (const FDocRequestHandle& Dep : Lease.DependencyLeases)
	{
		const FLease* DepLease = Leases.Find(Dep, this);
		if (!DepLease || DepLease->State != EDocStreamingRequestState::Ready)
		{
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Request progression
// ---------------------------------------------------------------------------

void UDocWorldStreamingSubsystem::PollNow()
{
	for (TPair<FDocChunkInstanceKey, FInstance>& Pair : Instances)
	{
		RefreshObserved(Pair.Value);
	}
	UpdatePendingLeases();

	// Owner-bound world-state requests whose owner died.
	TArray<FDocRequestHandle> DeadStates;
	WorldStateRequests.ForEach([&](const FDocRequestHandle& H, const FWorldStateRequest& R)
	{
		if (R.bOwnerWasSet && !R.Owner.IsValid()) { DeadStates.Add(H); }
	});
	for (const FDocRequestHandle& H : DeadStates)
	{
		ReleaseWorldState(H);
	}
}

void UDocWorldStreamingSubsystem::UpdatePendingLeases()
{
	// Several passes so dependency readiness propagates to dependents in one poll.
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		TArray<TPair<FDocRequestHandle, EDocStreamingRequestState>> Transitions;
		TArray<FDocRequestHandle> DeadOwners;
		const double Current = Now();
		Leases.ForEach([&](const FDocRequestHandle& Handle, const FLease& Lease)
		{
			if (Lease.bOwnerWasSet && !Lease.Owner.IsValid())
			{
				DeadOwners.Add(Handle);
				return;
			}
			if (Lease.State != EDocStreamingRequestState::Pending)
			{
				return;
			}
			const FInstance* Instance = Instances.Find(Lease.Key);
			if (!Instance || Instance->Observed == EDocChunkObservedState::Failed)
			{
				Transitions.Add({ Handle, EDocStreamingRequestState::Failed });
				return;
			}
			bool bDepFailed = false;
			for (const FDocRequestHandle& Dep : Lease.DependencyLeases)
			{
				const FLease* DepLease = Leases.Find(Dep, this);
				bDepFailed |= !DepLease || DepLease->State == EDocStreamingRequestState::Failed || DepLease->State == EDocStreamingRequestState::TimedOut;
			}
			if (bDepFailed)
			{
				Transitions.Add({ Handle, EDocStreamingRequestState::Failed });
			}
			else if (IsInstanceReady(*Instance) && AreDependenciesReady(Lease))
			{
				Transitions.Add({ Handle, EDocStreamingRequestState::Ready });
			}
			else if (Lease.Timeout > 0.f && Current - Lease.StartTime > Lease.Timeout)
			{
				Transitions.Add({ Handle, EDocStreamingRequestState::TimedOut });
			}
		});

		for (const FDocRequestHandle& Handle : DeadOwners)
		{
			ReleaseLeaseInternal(Handle, EDocStreamingRequestState::Released, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Owner destroyed")), true);
		}
		if (Transitions.Num() == 0)
		{
			break;
		}
		for (const TPair<FDocRequestHandle, EDocStreamingRequestState>& T : Transitions)
		{
			FLease* Lease = Leases.Find(T.Key, this);
			if (!Lease)
			{
				continue;
			}
			if (T.Value == EDocStreamingRequestState::Ready)
			{
				Lease->State = EDocStreamingRequestState::Ready;
				Lease->Result = FDocSystemResult::MakeSuccess(T.Key.GetOperationId());
				if (!Lease->bInternal)
				{
					Notify(MakeInfo(T.Key, *Lease));
				}
			}
			else
			{
				const FInstance* Instance = Instances.Find(Lease->Key);
				const FString Why = T.Value == EDocStreamingRequestState::TimedOut ? TEXT("Timed out waiting for readiness")
					: (Instance && !Instance->LastFailure.IsEmpty() ? Instance->LastFailure : FString(TEXT("Dependency or backend failure")));
				const EDocResultOutcome Outcome = T.Value == EDocStreamingRequestState::TimedOut ? EDocResultOutcome::TimedOut : EDocResultOutcome::Failed;
				// Failure releases exactly this request's leases (dependencies included); others are untouched.
				ReleaseLeaseInternal(T.Key, T.Value, FDocSystemResult::MakeFailure(Outcome, Why, FGameplayTag(), FText::GetEmpty(), T.Key.GetOperationId()), true);
			}
		}
	}
}

FDocStreamingRequestInfo UDocWorldStreamingSubsystem::ReleaseChunk(const FDocRequestHandle& Request)
{
	FDocStreamingRequestInfo Info;
	Info.Handle = Request;
	const FLease* Lease = Leases.Find(Request, this);
	if (!Lease)
	{
		Info.State = EDocStreamingRequestState::Released;
		Info.Result = FDocSystemResult::MakeNoChange(TEXT("Already released or unknown"));
		return Info;
	}
	if (Lease->bInternal)
	{
		Info.State = EDocStreamingRequestState::Failed;
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Internal dependency leases cannot be released directly"));
		return Info;
	}
	Info.Key = Lease->Key;
	const bool bWasPending = Lease->State == EDocStreamingRequestState::Pending;
	const EDocStreamingRequestState Final = bWasPending ? EDocStreamingRequestState::Cancelled : EDocStreamingRequestState::Released;
	const FDocSystemResult Result = bWasPending ? FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Cancelled before ready"))
		: FDocSystemResult::MakeSuccess(Request.GetOperationId());
	ReleaseLeaseInternal(Request, Final, Result, true);

	Info.State = Final;
	Info.Result = Result;
	const FInstance* Instance = Instances.Find(Info.Key);
	Info.bReleasedButResident = Instance != nullptr;
	return Info;
}

void UDocWorldStreamingSubsystem::ReleaseLeaseInternal(const FDocRequestHandle& Handle, EDocStreamingRequestState FinalState, const FDocSystemResult& Result, bool bNotify)
{
	FLease Removed;
	if (!Leases.Remove(Handle, this, &Removed))
	{
		return;
	}
	Removed.State = FinalState;
	Removed.Result = Result;

	for (const FDocRequestHandle& Dep : Removed.DependencyLeases)
	{
		ReleaseLeaseInternal(Dep, EDocStreamingRequestState::Released, FDocSystemResult::MakeSuccess(), false);
	}

	bool bResident = false;
	if (FInstance* Instance = Instances.Find(Removed.Key))
	{
		Instance->Leases.Remove(Handle);
		const UDocStreamingChunkDefinition* Definition = Instance->Definition.Get();
		const bool bKeep = Definition && Definition->bKeepLoaded && Instance->Observed != EDocChunkObservedState::Failed;
		if (Instance->Leases.Num() == 0 && !bKeep)
		{
			BeginNativeUnload(*Instance);
			Instances.Remove(Removed.Key);
		}
		else
		{
			bResident = true;
		}
	}

	if (bNotify && !Removed.bInternal)
	{
		FDocStreamingRequestInfo Info = MakeInfo(Handle, Removed);
		Info.bReleasedButResident = bResident;
		Notify(Info);
	}
}

FDocStreamingRequestInfo UDocWorldStreamingSubsystem::MakeInfo(const FDocRequestHandle& Handle, const FLease& Lease) const
{
	FDocStreamingRequestInfo Info;
	Info.Handle = Handle;
	Info.Key = Lease.Key;
	Info.State = Lease.State;
	Info.Result = Lease.State == EDocStreamingRequestState::Pending ? FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Pending"), DocCoreTags::Error_NotReady, FText::GetEmpty(), Handle.GetOperationId()) : Lease.Result;
	return Info;
}

void UDocWorldStreamingSubsystem::Notify(const FDocStreamingRequestInfo& Info)
{
	OnRequestChanged.Broadcast(Info);
	OnRequestChangedDynamic.Broadcast(Info);
}

FDocChunkStatus UDocWorldStreamingSubsystem::GetChunkStatus(const FDocChunkInstanceKey& Key) const
{
	FDocChunkStatus Status;
	Status.Key = Key;
	if (const FInstance* Instance = Instances.Find(Key))
	{
		const UDocStreamingChunkDefinition* Definition = Instance->Definition.Get();
		Status.LeaseCount = Instance->Leases.Num();
		Status.bDesiredResident = Status.LeaseCount > 0 || (Definition && Definition->bKeepLoaded);
		Status.Observed = Instance->Observed;
		Status.LastFailure = Instance->LastFailure;
	}
	return Status;
}

bool UDocWorldStreamingSubsystem::GetRequestInfo(const FDocRequestHandle& Request, FDocStreamingRequestInfo& OutInfo) const
{
	if (const FLease* Lease = Leases.Find(Request, this))
	{
		OutInfo = MakeInfo(Request, *Lease);
		return true;
	}
	return false;
}

void UDocWorldStreamingSubsystem::DebugForceObservedState(const FDocChunkInstanceKey& Key, EDocChunkObservedState State)
{
	if (FInstance* Instance = Instances.Find(Key))
	{
		Instance->Observed = State;
		if (State == EDocChunkObservedState::Failed)
		{
			Instance->LastFailure = TEXT("Forced failure (test)");
		}
	}
}

// ---------------------------------------------------------------------------
// World states
// ---------------------------------------------------------------------------

FDocRequestHandle UDocWorldStreamingSubsystem::RequestWorldState(UDocWorldStateGroup* Group, FGameplayTag StateTag, int32 Priority, UObject* Owner)
{
	if (!Group || !Group->FindVariant(StateTag))
	{
		UE_LOG(LogDocStreaming, Warning, TEXT("RequestWorldState: unknown group or variant %s"), *StateTag.ToString());
		return FDocRequestHandle();
	}
	FWorldStateRequest Request;
	Request.Group = Group;
	Request.StateTag = StateTag;
	Request.Priority = Priority;
	Request.Sequence = NextWorldStateSequence++;
	Request.Owner = Owner;
	Request.bOwnerWasSet = Owner != nullptr;
	const FDocRequestHandle Handle = WorldStateRequests.Add(this, MoveTemp(Request));
	RecomputeWorldState(Group);
	return Handle;
}

FDocSystemResult UDocWorldStreamingSubsystem::ReleaseWorldState(const FDocRequestHandle& Request)
{
	FWorldStateRequest Removed;
	if (!WorldStateRequests.Remove(Request, this, &Removed))
	{
		return FDocSystemResult::MakeNoChange(TEXT("World-state request already released"));
	}
	RecomputeWorldState(Removed.Group.Get());
	return FDocSystemResult::MakeSuccess();
}

void UDocWorldStreamingSubsystem::RecomputeWorldState(UDocWorldStateGroup* Group)
{
	if (!Group)
	{
		return;
	}
	const FWorldStateRequest* Best = nullptr;
	WorldStateRequests.ForEach([&](const FDocRequestHandle&, const FWorldStateRequest& R)
	{
		if (R.Group.Get() != Group || (R.bOwnerWasSet && !R.Owner.IsValid())) { return; }
		if (!Best || R.Priority > Best->Priority || (R.Priority == Best->Priority && R.Sequence > Best->Sequence)) { Best = &R; }
	});
	const FGameplayTag Effective = Best ? Best->StateTag : Group->DefaultState;

	FGroupState& State = Groups.FindOrAdd(Group);
	if (State.Effective == Effective && (State.ChunkLeases.Num() > 0 || !Effective.IsValid()))
	{
		return;
	}

	// Acquire the new variant first, then release the old one (no atomicity promised).
	TArray<FDocRequestHandle> NewLeases;
	if (const FDocWorldStateVariant* Variant = Group->FindVariant(Effective))
	{
		for (const TObjectPtr<UDocStreamingChunkDefinition>& Chunk : Variant->Chunks)
		{
			FString Error;
			if (!Chunk || !ValidateGraph(Chunk, Error))
			{
				UE_LOG(LogDocStreaming, Error, TEXT("World state %s: %s"), *Effective.ToString(), *Error);
				continue;
			}
			FDocSystemResult Result;
			const FDocRequestHandle H = AcquireLease(Chunk, nullptr, FGuid(), Chunk->DefaultTransform, true, Result);
			if (H.IsSet()) { NewLeases.Add(H); }
		}
	}
	const TArray<FDocRequestHandle> OldLeases = MoveTemp(State.ChunkLeases);
	State.ChunkLeases = MoveTemp(NewLeases);
	State.Effective = Effective;
	for (const FDocRequestHandle& Old : OldLeases)
	{
		ReleaseLeaseInternal(Old, EDocStreamingRequestState::Released, FDocSystemResult::MakeSuccess(), false);
	}
	UpdatePendingLeases();
}

FGameplayTag UDocWorldStreamingSubsystem::GetEffectiveWorldState(UDocWorldStateGroup* Group) const
{
	const FGroupState* State = Groups.Find(Group);
	return State ? State->Effective : (Group ? Group->DefaultState : FGameplayTag());
}

bool UDocWorldStreamingSubsystem::IsWorldStateTransitioning(UDocWorldStateGroup* Group) const
{
	const FGroupState* State = Groups.Find(Group);
	if (!State)
	{
		return false;
	}
	for (const FDocRequestHandle& H : State->ChunkLeases)
	{
		const FLease* Lease = Leases.Find(H, this);
		if (!Lease || Lease->State != EDocStreamingRequestState::Ready)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Source component
// ---------------------------------------------------------------------------

UDocStreamingSourceComponent::UDocStreamingSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDocStreamingSourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TimeSinceEvaluation += DeltaTime;
	if (TimeSinceEvaluation < EvaluationInterval)
	{
		return;
	}
	TimeSinceEvaluation = 0.f;

	UDocWorldStreamingSubsystem* Subsystem = UDocWorldStreamingSubsystem::Get(this);
	const AActor* Owner = GetOwner();
	if (!Subsystem || !Owner)
	{
		return;
	}
	const FVector Location = Owner->GetActorLocation();
	for (const TObjectPtr<UDocStreamingChunkDefinition>& Chunk : Chunks)
	{
		if (!Chunk) { continue; }
		const float Distance = FVector::Dist(Location, Chunk->DefaultTransform.GetLocation());
		FDocRequestHandle* Existing = Active.Find(Chunk.Get());
		if (!Existing && Distance <= Chunk->LoadDistance)
		{
			const FDocStreamingRequestInfo Info = Subsystem->RequestChunk(Chunk, this, FGuid(), false, FTransform::Identity);
			if (Info.Handle.IsSet()) { Active.Add(Chunk.Get(), Info.Handle); }
		}
		else if (Existing && Distance > Chunk->UnloadDistance)
		{
			Subsystem->ReleaseChunk(*Existing);
			Active.Remove(Chunk.Get());
		}
	}
}

void UDocStreamingSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocWorldStreamingSubsystem* Subsystem = UDocWorldStreamingSubsystem::Get(this))
	{
		for (const TPair<TWeakObjectPtr<UDocStreamingChunkDefinition>, FDocRequestHandle>& Pair : Active)
		{
			Subsystem->ReleaseChunk(Pair.Value);
		}
	}
	Active.Reset();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Async action
// ---------------------------------------------------------------------------

UDocWaitChunkReadyAction* UDocWaitChunkReadyAction::RequestChunkAndWait(UObject* WorldContextObject, UDocStreamingChunkDefinition* InDefinition, UObject* InOwner, FGuid InstanceScope)
{
	UDocWaitChunkReadyAction* Action = NewObject<UDocWaitChunkReadyAction>();
	Action->WorldContext = WorldContextObject;
	Action->Definition = InDefinition;
	Action->Owner = InOwner;
	Action->Scope = InstanceScope;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UDocWaitChunkReadyAction::Activate()
{
	UDocWorldStreamingSubsystem* Streaming = UDocWorldStreamingSubsystem::Get(WorldContext.Get());
	if (!Streaming)
	{
		FDocStreamingRequestInfo Info;
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No streaming subsystem"));
		Finish(Info, false);
		return;
	}
	Subsystem = Streaming;
	ChangedHandle = Streaming->OnRequestChanged.AddUObject(this, &UDocWaitChunkReadyAction::HandleChanged);
	const FDocStreamingRequestInfo Info = Streaming->RequestChunk(Definition.Get(), Owner.Get(), Scope, false, FTransform::Identity);
	Handle = Info.Handle;
	if (Info.State == EDocStreamingRequestState::Ready)
	{
		Finish(Info, true);
	}
	else if (Info.State != EDocStreamingRequestState::Pending)
	{
		Finish(Info, false);
	}
}

void UDocWaitChunkReadyAction::HandleChanged(const FDocStreamingRequestInfo& Info)
{
	if (bFinished || Info.Handle != Handle)
	{
		return;
	}
	if (Info.State == EDocStreamingRequestState::Ready)
	{
		Finish(Info, true);
	}
	else if (Info.State != EDocStreamingRequestState::Pending)
	{
		Finish(Info, false);
	}
}

void UDocWaitChunkReadyAction::Finish(const FDocStreamingRequestInfo& Info, bool bReady)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (UDocWorldStreamingSubsystem* Streaming = Subsystem.Get())
	{
		Streaming->OnRequestChanged.Remove(ChangedHandle);
	}
	if (bReady) { OnReady.Broadcast(Info); }
	else { OnFailed.Broadcast(Info); }
	SetReadyToDestroy();
}
