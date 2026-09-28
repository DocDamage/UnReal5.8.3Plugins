#include "DocGameplayEventSubsystem.h"
#include "DocEventsSettings.h"
#include "DocEventsLog.h"
#include "DocCoreTags.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocGameplayEventSubsystem)

FString FDocEventScope::ToString() const
{
	const UEnum* KindEnum = StaticEnum<EDocEventScopeKind>();
	const FString KindName = KindEnum ? KindEnum->GetNameStringByValue(static_cast<int64>(Kind)) : TEXT("?");
	switch (Kind)
	{
	case EDocEventScopeKind::World: return KindName;
	case EDocEventScopeKind::Actor:
	case EDocEventScopeKind::Component: return FString::Printf(TEXT("%s:%s"), *KindName, *GetNameSafe(Object.Get()));
	default: return FString::Printf(TEXT("%s:%s"), *KindName, *Key.ToString());
	}
}

UDocGameplayEventSubsystem* UDocGameplayEventSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocGameplayEventSubsystem>() : nullptr;
}

bool UDocGameplayEventSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

bool UDocGameplayEventSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocGameplayEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UDocEventsSettings* Settings = GetDefault<UDocEventsSettings>();
	Limits.MaxEventsPerDrain = FMath::Max(1, Settings->MaxEventsPerDrain);
	Limits.MaxQueuedEvents = FMath::Max(1, Settings->MaxQueuedEvents);
	Limits.MaxRetainedValues = FMath::Max(0, Settings->MaxRetainedValues);
	Limits.RetainedTimeToLiveSeconds = FMath::Max(0.f, Settings->RetainedTimeToLiveSeconds);
	Limits.HistoryCapacity = FMath::Max(0, Settings->HistoryCapacity);
	Limits.AutoRetainTags = Settings->AutoRetainTags;
	Limits.HistoryTrackedTags = Settings->HistoryTrackedTags;
	Limits.bCaptureDebugHistory = Settings->bCaptureDebugHistory;
	Limits.DebugHistoryCapacity = FMath::Max(1, Settings->DebugHistoryCapacity);

	for (const TPair<FGameplayTag, TSoftObjectPtr<UScriptStruct>>& Pair : Settings->PayloadSchemas)
	{
		// Schema structs are native script structs; resolving the path does not load content packages.
		if (const UScriptStruct* Struct = Pair.Value.LoadSynchronous())
		{
			Schemas.Add(Pair.Key, Struct);
		}
		else if (!Pair.Value.IsNull())
		{
			UE_LOG(LogDocEvents, Warning, TEXT("Payload schema for %s could not be resolved: %s"), *Pair.Key.ToString(), *Pair.Value.ToString());
		}
	}
	bShutdown = false;
}

void UDocGameplayEventSubsystem::Deinitialize()
{
	bShutdown = true;
	OnBusShutdown.Broadcast();
	OnBusShutdown.Clear();

	Subscriptions.Reset();
	ExactIndex.Reset();
	HierarchicalIndex.Reset();
	Queue.Reset();
	Scheduled.Reset();
	Retained.Reset();
	History.Reset();
	DebugRing.Reset();
	Super::Deinitialize();
}

TStatId UDocGameplayEventSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocGameplayEventSubsystem, STATGROUP_Tickables);
}

void UDocGameplayEventSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessPendingWork();
}

double UDocGameplayEventSubsystem::NowFor(EDocClockDomain Clock) const
{
	if (Clock == EDocClockDomain::RealTime || Clock == EDocClockDomain::WallClock)
	{
		return FPlatformTime::Seconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

// ---------------------------------------------------------------------------
// Broadcast
// ---------------------------------------------------------------------------

FDocSystemResult UDocGameplayEventSubsystem::ValidateEvent(const FDocGameplayEvent& Event) const
{
	if (bShutdown)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Event bus is shut down"));
	}
	if (!Event.EventTag.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Event has no EventTag"));
	}
	if (const TWeakObjectPtr<const UScriptStruct>* Expected = Schemas.Find(Event.EventTag))
	{
		const UScriptStruct* ExpectedStruct = Expected->Get();
		const UScriptStruct* Actual = Event.Payload.GetScriptStruct();
		if (ExpectedStruct && Actual != ExpectedStruct)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Payload type mismatch for %s: expected %s, got %s"),
					*Event.EventTag.ToString(), *ExpectedStruct->GetName(), Actual ? *Actual->GetName() : TEXT("<none>")));
		}
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocGameplayEventSubsystem::StampEvent(FDocGameplayEvent& Event)
{
	Event.Sequence = NextSequence++;
	Event.TimeDomain = EDocClockDomain::WorldGameplay;
	Event.Timestamp = NowFor(EDocClockDomain::WorldGameplay);
}

FDocSystemResult UDocGameplayEventSubsystem::BroadcastEvent(const FDocGameplayEvent& InEvent)
{
	FDocSystemResult Valid = ValidateEvent(InEvent);
	if (!Valid.IsSuccess())
	{
		UE_LOG(LogDocEvents, Warning, TEXT("BroadcastEvent rejected: %s"), *Valid.ToString());
		return Valid;
	}

	FDocGameplayEvent Event = InEvent;
	StampEvent(Event);
	const int64 Sequence = Event.Sequence;

	if (Queue.Num() >= Limits.MaxQueuedEvents)
	{
		++Stats.DroppedEvents;
		UE_LOG(LogDocEvents, Warning, TEXT("Event queue full (%d); dropped %s"), Limits.MaxQueuedEvents, *Event.EventTag.ToString());
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Event queue full; event dropped"), DocCoreTags::Error_Unavailable);
	}

	Enqueue(MoveTemp(Event));
	if (!bDraining)
	{
		Drain();
	}
	return FDocSystemResult::MakeSuccess(Sequence);
}

void UDocGameplayEventSubsystem::Enqueue(FDocGameplayEvent&& Event)
{
	Queue.Add(MoveTemp(Event));
}

void UDocGameplayEventSubsystem::Drain()
{
	if (bDraining || bShutdown)
	{
		return;
	}
	TGuardValue<bool> DrainGuard(bDraining, true);

	int32 Processed = 0;
	while (Queue.Num() > 0)
	{
		if (Processed >= Limits.MaxEventsPerDrain)
		{
			Stats.DeferredEvents += Queue.Num();
			UE_LOG(LogDocEvents, Warning, TEXT("Event drain limit (%d) reached; %d event(s) deferred to next tick. Possible event storm."),
				Limits.MaxEventsPerDrain, Queue.Num());
			break;
		}
		// FIFO: take the oldest. Copy out first so listeners may enqueue safely.
		FDocGameplayEvent Event = MoveTemp(Queue[0]);
		Queue.RemoveAt(0, EAllowShrinking::No);
		DeliverOne(Event);
		++Processed;
		if (bShutdown)
		{
			break;
		}
	}
}

void UDocGameplayEventSubsystem::GatherListeners(const FDocGameplayEvent& Event, TArray<FDocRequestHandle>& OutHandles)
{
	OutHandles.Reset();
	const UObject* Scope = this;

	auto Consider = [&](const FDocRequestHandle& Handle)
	{
		const FSubscription* Sub = Subscriptions.Find(Handle, Scope);
		if (!Sub || Sub->bRemoved)
		{
			return;
		}
		if (Sub->Options.bFilterByScope && Sub->Options.ScopeFilter != Event.Scope)
		{
			return;
		}
		OutHandles.AddUnique(Handle);
	};

	if (const TArray<FDocRequestHandle>* Exact = ExactIndex.Find(Event.EventTag))
	{
		for (const FDocRequestHandle& H : *Exact) { Consider(H); }
	}
	// Hierarchical subscribers of the tag itself or any of its parents.
	const FGameplayTagContainer SelfAndParents = Event.EventTag.GetGameplayTagParents();
	for (const FGameplayTag& Tag : SelfAndParents)
	{
		if (const TArray<FDocRequestHandle>* Hier = HierarchicalIndex.Find(Tag))
		{
			for (const FDocRequestHandle& H : *Hier) { Consider(H); }
		}
	}

	OutHandles.Sort([this, Scope](const FDocRequestHandle& A, const FDocRequestHandle& B)
	{
		const FSubscription* SA = Subscriptions.Find(A, Scope);
		const FSubscription* SB = Subscriptions.Find(B, Scope);
		if (SA->Options.Priority != SB->Options.Priority)
		{
			return SA->Options.Priority > SB->Options.Priority;
		}
		return SA->Order < SB->Order;
	});
}

void UDocGameplayEventSubsystem::DeliverOne(const FDocGameplayEvent& Event)
{
	++Stats.TotalBroadcast;
	RecordHistory(Event);
	MaybeAutoRetain(Event);

	TArray<FDocRequestHandle> Snapshot;
	GatherListeners(Event, Snapshot);
	RecordDebug(Event, Snapshot.Num());

	TGuardValue<int32> DepthGuard(DispatchDepth, DispatchDepth + 1);
	for (const FDocRequestHandle& Handle : Snapshot)
	{
		FSubscription* Sub = Subscriptions.Find(Handle, this);
		if (!Sub || Sub->bRemoved)
		{
			continue; // unsubscribed during this dispatch
		}
		if (Sub->bOwnerWasSet && !Sub->Options.Owner.IsValid())
		{
			Unsubscribe(Handle);
			continue;
		}

		// Copy delegates: the listener may unsubscribe itself (freeing Sub).
		FDocGameplayEventNativeDelegate Native = Sub->Native;
		FDocGameplayEventDynamicDelegate Dynamic = Sub->Dynamic;
		const bool bOneShot = Sub->Options.bOneShot;
		if (bOneShot)
		{
			Unsubscribe(Handle);
		}

		if (Native.IsBound())
		{
			Native.Execute(Event);
		}
		else if (Dynamic.IsBound())
		{
			Dynamic.Execute(Event);
		}
		else if (!bOneShot)
		{
			// Dynamic delegate target died.
			Unsubscribe(Handle);
		}

		if (bShutdown)
		{
			return;
		}
	}
}

FDocRequestHandle UDocGameplayEventSubsystem::BroadcastNextFrame(const FDocGameplayEvent& Event)
{
	return BroadcastAfterDelay(Event, 0.f, EDocClockDomain::WorldGameplay);
}

FDocRequestHandle UDocGameplayEventSubsystem::BroadcastAfterDelay(const FDocGameplayEvent& Event, float DelaySeconds, EDocClockDomain Clock)
{
	const FDocSystemResult Valid = ValidateEvent(Event);
	if (!Valid.IsSuccess())
	{
		UE_LOG(LogDocEvents, Warning, TEXT("Scheduled broadcast rejected: %s"), *Valid.ToString());
		return FDocRequestHandle();
	}
	if (Clock == EDocClockDomain::Simulation || Clock == EDocClockDomain::WallClock)
	{
		UE_LOG(LogDocEvents, Warning, TEXT("BroadcastAfterDelay: clock %d unsupported by the base bus; using WorldGameplay"), static_cast<int32>(Clock));
		Clock = EDocClockDomain::WorldGameplay;
	}

	FDocScheduledEvent Entry;
	Entry.Event = Event;
	Entry.OperationId = FDocHandleAllocator::NextOperationId();
	Entry.Clock = Clock;
	Entry.bNextFrame = DelaySeconds <= 0.f;
	Entry.DueTime = NowFor(Clock) + FMath::Max(0.f, DelaySeconds);
	Scheduled.Add(MoveTemp(Entry));
	// Handles for scheduled events use epoch 1 of the scheduled list; validity is tracked by OperationId presence.
	return FDocHandleAllocator::MakeHandle(Scheduled.Last().OperationId, Subscriptions.GetEpoch());
}

FDocSystemResult UDocGameplayEventSubsystem::CancelScheduledEvent(const FDocRequestHandle& Handle)
{
	if (!Handle.IsSet() || Handle.GetEpoch() != Subscriptions.GetEpoch())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Handle does not belong to this event bus"), DocCoreTags::Error_Handle_WrongScope);
	}
	const int32 Removed = Scheduled.RemoveAll([&Handle](const FDocScheduledEvent& E) { return E.OperationId == Handle.GetOperationId(); });
	return Removed > 0 ? FDocSystemResult::MakeSuccess(Handle.GetOperationId()) : FDocSystemResult::MakeNoChange(TEXT("Already fired or cancelled"));
}

void UDocGameplayEventSubsystem::ProcessPendingWork()
{
	if (bShutdown)
	{
		return;
	}

	ExpireRetained();

	if (Scheduled.Num() > 0)
	{
		TArray<FDocGameplayEvent> Due;
		for (int32 i = 0; i < Scheduled.Num();)
		{
			const FDocScheduledEvent& Entry = Scheduled[i];
			if (Entry.bNextFrame || NowFor(Entry.Clock) >= Entry.DueTime)
			{
				Due.Add(Entry.Event);
				Scheduled.RemoveAt(i, EAllowShrinking::No);
			}
			else
			{
				++i;
			}
		}
		for (FDocGameplayEvent& Event : Due)
		{
			BroadcastEvent(Event);
		}
	}

	if (Queue.Num() > 0 && !bDraining)
	{
		Drain();
	}
}

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------

FDocRequestHandle UDocGameplayEventSubsystem::AddSubscription(FSubscription&& Subscription)
{
	if (bShutdown || !Subscription.Tag.IsValid())
	{
		return FDocRequestHandle();
	}
	Subscription.Order = NextOrder++;
	Subscription.bOwnerWasSet = !Subscription.Options.Owner.IsExplicitlyNull();
	const FGameplayTag Tag = Subscription.Tag;
	const EDocEventTagMatch Match = Subscription.Options.Match;
	const bool bReplay = Subscription.Options.bReplayRetained;
	const FDocEventListenOptions Options = Subscription.Options;

	const FDocRequestHandle Handle = Subscriptions.Add(this, MoveTemp(Subscription));
	(Match == EDocEventTagMatch::Exact ? ExactIndex : HierarchicalIndex).FindOrAdd(Tag).Add(Handle);

	if (bReplay)
	{
		// Queue matching retained values for this listener only.
		for (const FDocRetainedEventValue& Value : Retained)
		{
			const FGameplayTag& ETag = Value.Event.EventTag;
			const bool bTagMatch = Match == EDocEventTagMatch::Exact ? ETag == Tag : ETag.MatchesTag(Tag);
			const bool bScopeMatch = !Options.bFilterByScope || Options.ScopeFilter == Value.Event.Scope;
			if (bTagMatch && bScopeMatch)
			{
				// Deliver directly to this subscription, outside the shared queue, to avoid waking other listeners.
				FSubscription* Sub = Subscriptions.Find(Handle, this);
				if (!Sub) { break; }
				FDocGameplayEventNativeDelegate Native = Sub->Native;
				FDocGameplayEventDynamicDelegate Dynamic = Sub->Dynamic;
				if (Native.IsBound()) { Native.Execute(Value.Event); }
				else if (Dynamic.IsBound()) { Dynamic.Execute(Value.Event); }
				if (Options.bOneShot) { Unsubscribe(Handle); break; }
			}
		}
	}
	return Handle;
}

FDocRequestHandle UDocGameplayEventSubsystem::SubscribeNative(FGameplayTag EventTag, const FDocEventListenOptions& Options, FDocGameplayEventNativeDelegate Delegate)
{
	if (!Delegate.IsBound())
	{
		return FDocRequestHandle();
	}
	FSubscription Sub;
	Sub.Tag = EventTag;
	Sub.Options = Options;
	Sub.Native = MoveTemp(Delegate);
	return AddSubscription(MoveTemp(Sub));
}

FDocRequestHandle UDocGameplayEventSubsystem::Subscribe(FGameplayTag EventTag, FDocEventListenOptions Options, FDocGameplayEventDynamicDelegate Delegate)
{
	if (!Delegate.IsBound())
	{
		return FDocRequestHandle();
	}
	if (Options.Owner.IsExplicitlyNull())
	{
		Options.Owner = Delegate.GetUObject();
	}
	FSubscription Sub;
	Sub.Tag = EventTag;
	Sub.Options = Options;
	Sub.Dynamic = Delegate;
	return AddSubscription(MoveTemp(Sub));
}

FDocSystemResult UDocGameplayEventSubsystem::Unsubscribe(const FDocRequestHandle& Handle)
{
	FSubscription Removed;
	if (!Subscriptions.Remove(Handle, this, &Removed))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Subscription already removed or unknown"));
	}
	TMap<FGameplayTag, TArray<FDocRequestHandle>>& Index = Removed.Options.Match == EDocEventTagMatch::Exact ? ExactIndex : HierarchicalIndex;
	if (TArray<FDocRequestHandle>* List = Index.Find(Removed.Tag))
	{
		List->RemoveSingle(Handle);
		if (List->Num() == 0)
		{
			Index.Remove(Removed.Tag);
		}
	}
	return FDocSystemResult::MakeSuccess(Handle.GetOperationId());
}

int32 UDocGameplayEventSubsystem::UnsubscribeAllForOwner(const UObject* Owner)
{
	if (!Owner)
	{
		return 0;
	}
	TArray<FDocRequestHandle> ToRemove;
	Subscriptions.ForEach([&](const FDocRequestHandle& Handle, const FSubscription& Sub)
	{
		if (Sub.Options.Owner.Get() == Owner)
		{
			ToRemove.Add(Handle);
		}
	});
	for (const FDocRequestHandle& Handle : ToRemove)
	{
		Unsubscribe(Handle);
	}
	return ToRemove.Num();
}

bool UDocGameplayEventSubsystem::IsSubscribed(const FDocRequestHandle& Handle) const
{
	return Subscriptions.Validate(Handle, this) == EDocHandleStatus::Active;
}

// ---------------------------------------------------------------------------
// Retained values
// ---------------------------------------------------------------------------

bool UDocGameplayEventSubsystem::PayloadHoldsStrongObjectRefs(const UScriptStruct* Struct)
{
	if (!Struct)
	{
		return false;
	}
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Prop = *It;
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop)) { Prop = ArrayProp->Inner; }
		else if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop)) { Prop = SetProp->ElementProp; }
		else if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			if (CastField<FObjectProperty>(MapProp->KeyProp) || CastField<FObjectProperty>(MapProp->ValueProp)) { return true; }
			continue;
		}

		if (CastField<FObjectProperty>(Prop) || CastField<FInterfaceProperty>(Prop))
		{
			// FObjectProperty covers hard object/class pointers; weak/soft/lazy are separate property types.
			return true;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (PayloadHoldsStrongObjectRefs(StructProp->Struct))
			{
				return true;
			}
		}
	}
	return false;
}

FDocSystemResult UDocGameplayEventSubsystem::UpdateRetainedValue(const FDocGameplayEvent& InEvent, bool bBroadcast)
{
	FDocSystemResult Valid = ValidateEvent(InEvent);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	if (PayloadHoldsStrongObjectRefs(InEvent.Payload.GetScriptStruct()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			FString::Printf(TEXT("Retained payload %s holds strong object references; use weak pointers or persistent IDs"),
				*GetNameSafe(InEvent.Payload.GetScriptStruct())));
	}

	FDocGameplayEvent Event = InEvent;
	StampEvent(Event);
	StoreRetained(Event);

	if (bBroadcast)
	{
		if (Queue.Num() >= Limits.MaxQueuedEvents)
		{
			++Stats.DroppedEvents;
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Value retained, but broadcast dropped: queue full"));
		}
		Enqueue(MoveTemp(Event));
		if (!bDraining)
		{
			Drain();
		}
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocGameplayEventSubsystem::StoreRetained(const FDocGameplayEvent& Event)
{
	if (Limits.MaxRetainedValues <= 0)
	{
		return;
	}
	for (FDocRetainedEventValue& Value : Retained)
	{
		if (Value.Event.EventTag == Event.EventTag && Value.Event.Scope == Event.Scope)
		{
			Value.Event = Event;
			++Value.Revision;
			Value.UpdatedAt = NowFor(EDocClockDomain::WorldGameplay);
			return;
		}
	}

	if (Retained.Num() >= Limits.MaxRetainedValues)
	{
		int32 OldestIndex = 0;
		for (int32 i = 1; i < Retained.Num(); ++i)
		{
			if (Retained[i].UpdatedAt < Retained[OldestIndex].UpdatedAt)
			{
				OldestIndex = i;
			}
		}
		Retained.RemoveAt(OldestIndex);
		++Stats.EvictedRetained;
	}

	FDocRetainedEventValue& Added = Retained.AddDefaulted_GetRef();
	Added.Event = Event;
	Added.Revision = 1;
	Added.UpdatedAt = NowFor(EDocClockDomain::WorldGameplay);
}

void UDocGameplayEventSubsystem::MaybeAutoRetain(const FDocGameplayEvent& Event)
{
	if (Limits.AutoRetainTags.IsEmpty() || !Event.EventTag.MatchesAny(Limits.AutoRetainTags))
	{
		return;
	}
	if (PayloadHoldsStrongObjectRefs(Event.Payload.GetScriptStruct()))
	{
		UE_LOG(LogDocEvents, Warning, TEXT("Auto-retain skipped for %s: payload holds strong object references"), *Event.EventTag.ToString());
		return;
	}
	StoreRetained(Event);
}

void UDocGameplayEventSubsystem::ExpireRetained()
{
	if (Limits.RetainedTimeToLiveSeconds <= 0.f || Retained.Num() == 0)
	{
		return;
	}
	const double Now = NowFor(EDocClockDomain::WorldGameplay);
	const int32 Removed = Retained.RemoveAll([&](const FDocRetainedEventValue& V)
	{
		return Now - V.UpdatedAt > Limits.RetainedTimeToLiveSeconds;
	});
	Stats.EvictedRetained += Removed;
}

bool UDocGameplayEventSubsystem::GetLatestPayload(FGameplayTag EventTag, const FDocEventScope& Scope, FDocRetainedEventValue& OutValue) const
{
	for (const FDocRetainedEventValue& Value : Retained)
	{
		if (Value.Event.EventTag == EventTag && Value.Event.Scope == Scope)
		{
			OutValue = Value;
			return true;
		}
	}
	OutValue = FDocRetainedEventValue();
	return false;
}

FDocSystemResult UDocGameplayEventSubsystem::ClearRetainedValue(FGameplayTag EventTag, const FDocEventScope& Scope)
{
	const int32 Removed = Retained.RemoveAll([&](const FDocRetainedEventValue& V)
	{
		return V.Event.EventTag == EventTag && V.Event.Scope == Scope;
	});
	return Removed > 0 ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("No retained value"));
}

EDocTriState UDocGameplayEventSubsystem::QueryDocWorldState_Implementation(FGameplayTag StateTag, EDocTagMatchMode MatchMode) const
{
	if (!StateTag.IsValid())
	{
		return EDocTriState::Unknown;
	}
	for (const FDocRetainedEventValue& Value : Retained)
	{
		if (Value.Event.Scope.Kind != EDocEventScopeKind::World)
		{
			continue;
		}
		const bool bMatch = MatchMode == EDocTagMatchMode::Exact ? Value.Event.EventTag == StateTag : Value.Event.EventTag.MatchesTag(StateTag);
		if (bMatch)
		{
			return EDocTriState::Yes;
		}
	}
	// Retention is not an authoritative state store: absence means "not retained", which may be eviction.
	return Stats.EvictedRetained > 0 ? EDocTriState::Unknown : EDocTriState::No;
}

// ---------------------------------------------------------------------------
// History and diagnostics
// ---------------------------------------------------------------------------

void UDocGameplayEventSubsystem::RecordHistory(const FDocGameplayEvent& Event)
{
	if (Limits.HistoryCapacity <= 0)
	{
		return;
	}
	if (!Limits.HistoryTrackedTags.IsEmpty() && !Event.EventTag.MatchesAny(Limits.HistoryTrackedTags))
	{
		return;
	}
	FHistoryEntry Entry{ Event.EventTag, Event.Scope, Event.Sequence };
	if (History.Num() < Limits.HistoryCapacity)
	{
		History.Add(MoveTemp(Entry));
	}
	else
	{
		History[HistoryHead] = MoveTemp(Entry);
		HistoryHead = (HistoryHead + 1) % Limits.HistoryCapacity;
		++Stats.EvictedHistory;
	}
	++HistoryRecorded;
}

EDocTriState UDocGameplayEventSubsystem::HasEventOccurred(FGameplayTag EventTag, EDocEventTagMatch Match, bool bFilterByScope, const FDocEventScope& Scope) const
{
	if (Limits.HistoryCapacity <= 0 || !EventTag.IsValid())
	{
		return EDocTriState::Unknown;
	}
	if (!Limits.HistoryTrackedTags.IsEmpty() && !EventTag.MatchesAny(Limits.HistoryTrackedTags))
	{
		return EDocTriState::Unknown; // not tracked
	}
	for (const FHistoryEntry& Entry : History)
	{
		const bool bTagMatch = Match == EDocEventTagMatch::Exact ? Entry.Tag == EventTag : Entry.Tag.MatchesTag(EventTag);
		if (bTagMatch && (!bFilterByScope || Entry.Scope == Scope))
		{
			return EDocTriState::Yes;
		}
	}
	// Not found: only a definite No if nothing was ever evicted.
	return Stats.EvictedHistory > 0 ? EDocTriState::Unknown : EDocTriState::No;
}

void UDocGameplayEventSubsystem::RecordDebug(const FDocGameplayEvent& Event, int32 ListenerCount)
{
	if (!Limits.bCaptureDebugHistory)
	{
		return;
	}
	FDocEventDebugRecord Record;
	Record.EventTag = Event.EventTag;
	Record.Scope = Event.Scope.ToString();
	Record.Sequence = Event.Sequence;
	Record.Timestamp = Event.Timestamp;
	Record.SenderName = GetNameSafe(Event.Sender.Get());
	Record.TargetName = GetNameSafe(Event.Target.Get());
	Record.PayloadType = GetNameSafe(Event.Payload.GetScriptStruct());
	Record.ListenerCount = ListenerCount;
	Record.CorrelationId = Event.CorrelationId;

	if (DebugRing.Num() < Limits.DebugHistoryCapacity)
	{
		DebugRing.Add(MoveTemp(Record));
	}
	else
	{
		DebugRing[DebugHead] = MoveTemp(Record);
		DebugHead = (DebugHead + 1) % Limits.DebugHistoryCapacity;
	}
}

TArray<FDocEventDebugRecord> UDocGameplayEventSubsystem::GetDebugHistory() const
{
	TArray<FDocEventDebugRecord> Out;
	Out.Reserve(DebugRing.Num());
	for (int32 i = 0; i < DebugRing.Num(); ++i)
	{
		Out.Add(DebugRing[(DebugHead + i) % DebugRing.Num()]);
	}
	return Out;
}

FDocEventBusStats UDocGameplayEventSubsystem::GetStats() const
{
	FDocEventBusStats Out = Stats;
	Out.ActiveSubscriptions = Subscriptions.Num();
	Out.QueuedEvents = Queue.Num();
	Out.ScheduledEvents = Scheduled.Num();
	Out.RetainedValues = Retained.Num();
	return Out;
}

void UDocGameplayEventSubsystem::RegisterPayloadSchema(FGameplayTag EventTag, const UScriptStruct* PayloadStruct)
{
	if (!EventTag.IsValid())
	{
		return;
	}
	if (PayloadStruct)
	{
		Schemas.Add(EventTag, PayloadStruct);
	}
	else
	{
		Schemas.Remove(EventTag);
	}
}
