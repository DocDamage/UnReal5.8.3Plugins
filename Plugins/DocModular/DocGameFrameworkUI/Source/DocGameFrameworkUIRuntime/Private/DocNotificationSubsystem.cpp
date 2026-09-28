#include "DocNotificationSubsystem.h"
#include "Engine/LocalPlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocNotificationSubsystem)

UDocNotificationSubsystem* UDocNotificationSubsystem::Get(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? LocalPlayer->GetSubsystem<UDocNotificationSubsystem>() : nullptr;
}

void UDocNotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GetDefault<UDocUISettings>()->bAutoTick)
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDocNotificationSubsystem::Tick));
	}
}

void UDocNotificationSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Entries.Reset(); // timers and actions never outlive the player
	Handlers.Reset();
	SoundPlayer = nullptr;
	Super::Deinitialize();
}

bool UDocNotificationSubsystem::Tick(float DeltaSeconds)
{
	AdvanceClock(DeltaSeconds);
	return true;
}

void UDocNotificationSubsystem::RegisterActionHandler(FName Key, TSharedPtr<IDocNotificationActionHandler> Handler)
{
	if (Handler.IsValid()) { Handlers.Add(Key, Handler); } else { Handlers.Remove(Key); }
}

int32 UDocNotificationSubsystem::EffectivePriority(const FDocNotificationEntry& E) const
{
	const float Aging = GetDefault<UDocUISettings>()->NotificationAgingSeconds;
	const int32 Boost = (!E.bVisible && Aging > 0.f) ? FMath::FloorToInt(E.QueuedSeconds / Aging) : 0;
	return E.Request.Priority + Boost;
}

FDocNotificationEntry* UDocNotificationSubsystem::Find(const FGuid& Id)
{
	return Entries.FindByPredicate([&Id](const FDocNotificationEntry& E) { return E.Request.RequestId == Id; });
}

bool UDocNotificationSubsystem::FindEntry(const FGuid& Id, FDocNotificationEntry& OutEntry) const
{
	if (const FDocNotificationEntry* E = Entries.FindByPredicate([&Id](const FDocNotificationEntry& X) { return X.Request.RequestId == Id; }))
	{
		OutEntry = *E;
		return true;
	}
	return false;
}

bool UDocNotificationSubsystem::IsDroppable(const FDocNotificationEntry& E) const
{
	return E.Request.Policy != EDocNotificationPolicy::Persistent;
}

void UDocNotificationSubsystem::Changed()
{
	OnNotificationsChangedNative.Broadcast();
	OnNotificationsChanged.Broadcast();
}

void UDocNotificationSubsystem::Show(FDocNotificationEntry& E)
{
	E.bVisible = true;
	if (!Announced.Contains(E.Request.RequestId))
	{
		Announced.Add(E.Request.RequestId);
		if (SoundPlayer && !E.Request.Sound.IsNull())
		{
			SoundPlayer(E);
		}
	}
}

void UDocNotificationSubsystem::Promote()
{
	const int32 MaxVisible = GetDefault<UDocUISettings>()->MaxVisibleNotifications;
	for (;;)
	{
		int32 Visible = 0;
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FDocNotificationEntry& E = Entries[i];
			if (E.bVisible) { ++Visible; continue; }
			if (Best == INDEX_NONE)
			{
				Best = i;
				continue;
			}
			const int32 P = EffectivePriority(E);
			const int32 BP = EffectivePriority(Entries[Best]);
			if (P > BP || (P == BP && E.Sequence < Entries[Best].Sequence)) { Best = i; }
		}
		if (Visible >= MaxVisible || Best == INDEX_NONE)
		{
			return;
		}
		Show(Entries[Best]);
	}
}

FDocSystemResult UDocNotificationSubsystem::Enqueue(FDocNotificationRequest Request, FGuid& OutId)
{
	const UDocUISettings* Settings = GetDefault<UDocUISettings>();
	if (Request.Title.IsEmpty() && Request.Message.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A notification needs a title or message"), DocUITags::Error_UI_Notification);
	}
	if (!FMath::IsFinite(Request.Duration) || !FMath::IsFinite(Request.ExpireSeconds) || Request.ExpireSeconds < 0.f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid duration"), DocUITags::Error_UI_Notification);
	}
	if (!Request.RequestId.IsValid())
	{
		Request.RequestId = FGuid::NewGuid();
	}
	OutId = Request.RequestId;
	if (SeenIds.Contains(Request.RequestId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Duplicate notification request"));
	}
	SeenIds.Add(Request.RequestId);
	SeenOrder.Add(Request.RequestId);
	while (SeenOrder.Num() > 1024)
	{
		SeenIds.Remove(SeenOrder[0]);
		SeenOrder.RemoveAt(0);
	}
	if (Request.Duration <= 0.f)
	{
		Request.Duration = Settings->DefaultNotificationSeconds;
	}

	// Merge: presentation only (count/text/timer); the underlying grant is never repeated.
	if (Request.Policy == EDocNotificationPolicy::Merge && !Request.MergeKey.IsNone())
	{
		if (FDocNotificationEntry* Existing = Entries.FindByPredicate([&Request](const FDocNotificationEntry& E)
			{ return E.Request.MergeKey == Request.MergeKey && E.Request.Owner == Request.Owner; }))
		{
			++Existing->MergeCount;
			Existing->Request.Title = Request.Title;
			Existing->Request.Message = Request.Message;
			Existing->RemainingSeconds = Existing->Request.Duration;
			OutId = Existing->Request.RequestId;
			Changed();
			return FDocSystemResult::MakeNoChange(TEXT("Merged"));
		}
	}

	bool bTakeVisibleSlot = false;
	if (Request.Policy == EDocNotificationPolicy::Replace && Request.NotificationTag.IsValid())
	{
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			if (Entries[i].Request.NotificationTag == Request.NotificationTag && Entries[i].Request.Owner == Request.Owner)
			{
				bTakeVisibleSlot |= Entries[i].bVisible;
				Entries.RemoveAt(i);
			}
		}
	}

	FDocNotificationEntry& New = Entries.AddDefaulted_GetRef();
	New.Request = Request;
	New.Sequence = ++NextSequence;
	New.RemainingSeconds = Request.Duration;
	const FGuid NewId = Request.RequestId;
	if (bTakeVisibleSlot)
	{
		Show(New);
	}

	// Priority interrupt: preempt the lowest-priority visible entry back into the queue.
	if (Request.Policy == EDocNotificationPolicy::PriorityInterrupt && !Find(NewId)->bVisible)
	{
		int32 Visible = 0;
		int32 Lowest = INDEX_NONE;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (!Entries[i].bVisible) { continue; }
			++Visible;
			if (Lowest == INDEX_NONE || Entries[i].Request.Priority < Entries[Lowest].Request.Priority
				|| (Entries[i].Request.Priority == Entries[Lowest].Request.Priority && Entries[i].Sequence > Entries[Lowest].Sequence))
			{
				Lowest = i;
			}
		}
		if (Visible >= Settings->MaxVisibleNotifications && Lowest != INDEX_NONE && Entries[Lowest].Request.Priority < Request.Priority)
		{
			Entries[Lowest].bVisible = false; // keeps its remaining time
			Show(*Find(NewId));
		}
	}

	// Overflow: drop the lowest-priority, oldest droppable queued entry.
	int32 Queued = 0;
	for (const FDocNotificationEntry& E : Entries) { Queued += E.bVisible ? 0 : 1; }
	if (Queued > Settings->MaxQueuedNotifications)
	{
		int32 Victim = INDEX_NONE;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FDocNotificationEntry& E = Entries[i];
			if (E.bVisible || !IsDroppable(E)) { continue; }
			if (Victim == INDEX_NONE || EffectivePriority(E) < EffectivePriority(Entries[Victim])
				|| (EffectivePriority(E) == EffectivePriority(Entries[Victim]) && E.Sequence < Entries[Victim].Sequence))
			{
				Victim = i;
			}
		}
		if (Victim == INDEX_NONE)
		{
			Entries.RemoveAll([&NewId](const FDocNotificationEntry& E) { return E.Request.RequestId == NewId; });
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Notification queue is full of persistent entries"), DocUITags::Error_UI_Notification);
		}
		const bool bDroppedNew = Entries[Victim].Request.RequestId == NewId;
		Entries.RemoveAt(Victim);
		++Dropped;
		if (bDroppedNew)
		{
			Changed();
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Dropped by queue overflow"), DocUITags::Error_UI_Notification);
		}
	}
	Promote();
	Changed();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocNotificationSubsystem::EnqueueDefinition(const UDocNotificationDefinition* Definition, const FDocOwnerScope& Owner, FGuid& OutId)
{
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No definition"), DocUITags::Error_UI_Notification);
	}
	FDocNotificationRequest Request = Definition->Template;
	Request.RequestId = FGuid::NewGuid();
	Request.Owner = Owner;
	return Enqueue(Request, OutId);
}

FDocSystemResult UDocNotificationSubsystem::Dismiss(const FGuid& Id)
{
	const int32 Removed = Entries.RemoveAll([&Id](const FDocNotificationEntry& E) { return E.Request.RequestId == Id; });
	if (Removed == 0)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unknown or already dismissed"));
	}
	Promote();
	Changed();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocNotificationSubsystem::InvokeAction(const FGuid& Id)
{
	FDocNotificationEntry* E = Find(Id);
	if (!E)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown notification"), DocUITags::Error_UI_Notification);
	}
	if (E->bActionConsumed)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Action already executed"));
	}
	if (E->Request.ActionHandlerKey.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("This notification has no action"), DocUITags::Error_UI_Notification);
	}
	if (bInAction)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Another notification action is running"), DocUITags::Error_UI_Notification);
	}
	const TSharedPtr<IDocNotificationActionHandler> Handler = Handlers.FindRef(E->Request.ActionHandlerKey);
	if (!Handler.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No approved handler for this action"), DocUITags::Error_UI_Notification);
	}
	const FDocSystemResult Can = Handler->CanExecute(*E); // revalidate the current context
	if (!Can.IsSuccess())
	{
		return Can;
	}
	E->bActionConsumed = true; // exactly once, even if the handler re-enters
	const FDocNotificationEntry Snapshot = *E;
	bInAction = true;
	const FDocSystemResult Done = Handler->Execute(Snapshot);
	bInAction = false;
	if (!Done.IsSuccess())
	{
		if (FDocNotificationEntry* Again = Find(Id)) { Again->bActionConsumed = false; } // nothing happened; may retry
		return Done;
	}
	Dismiss(Id);
	return Done;
}

void UDocNotificationSubsystem::ClearAll()
{
	if (!Entries.IsEmpty())
	{
		Entries.Reset();
		Changed();
	}
}

TArray<FDocNotificationEntry> UDocNotificationSubsystem::GetVisible() const
{
	TArray<FDocNotificationEntry> Out = Entries.FilterByPredicate([](const FDocNotificationEntry& E) { return E.bVisible; });
	Out.Sort([](const FDocNotificationEntry& A, const FDocNotificationEntry& B)
	{
		return A.Request.Priority != B.Request.Priority ? A.Request.Priority > B.Request.Priority : A.Sequence < B.Sequence;
	});
	return Out;
}

TArray<FDocNotificationEntry> UDocNotificationSubsystem::GetQueued() const
{
	TArray<FDocNotificationEntry> Out = Entries.FilterByPredicate([](const FDocNotificationEntry& E) { return !E.bVisible; });
	Out.Sort([this](const FDocNotificationEntry& A, const FDocNotificationEntry& B)
	{
		const int32 PA = EffectivePriority(A);
		const int32 PB = EffectivePriority(B);
		return PA != PB ? PA > PB : A.Sequence < B.Sequence;
	});
	return Out;
}

void UDocNotificationSubsystem::AdvanceClock(double Seconds)
{
	if (!(Seconds > 0.0) || Entries.IsEmpty())
	{
		return;
	}
	bool bChanged = false;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		FDocNotificationEntry& E = Entries[i];
		E.AgeSeconds += static_cast<float>(Seconds);
		if (!E.bVisible) { E.QueuedSeconds += static_cast<float>(Seconds); }
		const bool bPersistent = E.Request.Policy == EDocNotificationPolicy::Persistent;
		bool bExpire = !bPersistent && E.Request.ExpireSeconds > 0.f && E.AgeSeconds >= E.Request.ExpireSeconds; // including while hidden
		if (E.bVisible && !bPersistent)
		{
			E.RemainingSeconds -= static_cast<float>(Seconds);
			bExpire |= E.RemainingSeconds <= 0.f;
		}
		if (bExpire)
		{
			Entries.RemoveAt(i);
			bChanged = true;
		}
	}
	const int32 VisibleBefore = Entries.FilterByPredicate([](const FDocNotificationEntry& E) { return E.bVisible; }).Num();
	Promote();
	const int32 VisibleAfter = Entries.FilterByPredicate([](const FDocNotificationEntry& E) { return E.bVisible; }).Num();
	if (bChanged || VisibleAfter != VisibleBefore)
	{
		Changed();
	}
}
