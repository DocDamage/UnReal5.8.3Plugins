#include "DocBroadcastSubsystem.h"
#include "Engine/World.h"

namespace DocBroadcastPrivate
{
	constexpr int32 MaxHistoryPerChannel = 64;
	constexpr int32 MaxRefreshDepth = 16;
	constexpr double MaxClockValue = 1.0e12;
	constexpr double CompletionEpsilon = 1.0e-9;

	static bool IsSaneTime(double Value)
	{
		return FMath::IsFinite(Value) && FMath::Abs(Value) <= MaxClockValue;
	}
}

// ---------------------------------------------------------------------------------------------
// Lifecycle and provider
// ---------------------------------------------------------------------------------------------

void UDocBroadcastSubsystem::Deinitialize()
{
	for (TPair<FName, FChannelRuntime>& Kvp : Channels)
	{
		CancelPendingOpen(Kvp.Value);
	}
	Channels.Empty();
	Receivers.Empty();
	RegisteredDefinitions.Empty();
	InterruptionPrograms.Empty();
	Super::Deinitialize();
}

void UDocBroadcastSubsystem::SetPlaybackProvider(UDocBroadcastPlaybackProvider* Provider)
{
	for (TPair<FName, FChannelRuntime>& Kvp : Channels)
	{
		CancelPendingOpen(Kvp.Value);
	}
	PlaybackProvider = Provider;
	for (TPair<FName, FChannelRuntime>& Kvp : Channels)
	{
		RefreshContent(Kvp.Key, Kvp.Value, /*bEmitEvents*/ false, /*bForceReopen*/ true);
	}
}

UDocBroadcastPlaybackProvider* UDocBroadcastSubsystem::GetPlaybackProvider()
{
	if (!PlaybackProvider)
	{
		PlaybackProvider = NewObject<UDocBroadcastSyntheticProvider>(this);
	}
	return PlaybackProvider;
}

FDocSystemResult UDocBroadcastSubsystem::CheckProgramCapabilities(const UDocBroadcastProgramDefinition* Program, bool bRequireSeek)
{
	const FDocBroadcastProviderCapabilities Caps = GetPlaybackProvider()->GetCapabilities(Program);
	const FString Id = Program ? Program->ProgramId.ToString() : TEXT("<null>");
	if (!Caps.bSupportsAudio)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Provider cannot play audio for %s"), *Id));
	}
	if (!Caps.bKnownDuration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Provider cannot report a reliable duration for %s"), *Id));
	}
	if (bRequireSeek && !Caps.bCanSeek)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Provider cannot seek %s"), *Id));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocBroadcastSubsystem::RegisterChannel(UDocBroadcastChannelDefinition* ChannelDef, double StartEpoch)
{
	if (!ChannelDef)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No channel definition"));
	}
	const FDocSystemResult Valid = ChannelDef->ValidateChannel();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	if (!DocBroadcastPrivate::IsSaneTime(StartEpoch))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Start offset must be finite"));
	}
	if (const FChannelRuntime* Existing = Channels.Find(ChannelDef->ChannelId))
	{
		return Existing->Definition == ChannelDef
			? FDocSystemResult::MakeNoChange(TEXT("Channel already registered"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("ChannelId %s already registered"), *ChannelDef->ChannelId.ToString()));
	}
	for (const FDocBroadcastScheduleEntry& Entry : ChannelDef->Schedule)
	{
		const FDocSystemResult Caps = CheckProgramCapabilities(Entry.Program, /*bRequireSeek*/ true);
		if (!Caps.IsSuccess())
		{
			return Caps;
		}
	}

	FChannelRuntime Runtime;
	Runtime.Definition = ChannelDef;
	// StartEpoch is the schedule offset the channel starts at (clock 0 == offset StartEpoch).
	Runtime.AnchorEpochSeconds = -StartEpoch;
	Runtime.FictionalSeconds = StartEpoch;
	RegisteredDefinitions.Add(ChannelDef);
	FChannelRuntime& Stored = Channels.Add(ChannelDef->ChannelId, MoveTemp(Runtime));
	RefreshContent(ChannelDef->ChannelId, Stored, /*bEmitEvents*/ false);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::UnregisterChannel(FName ChannelId)
{
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	CancelPendingOpen(*Channel);
	TArray<FGuid> Live;
	for (const FDocBroadcastInterruption& Item : Channel->Interruptions)
	{
		Live.Add(Item.InterruptionId);
	}
	for (const FGuid& Id : Live)
	{
		EndInterruption(ChannelId, *Channel, Id, EDocBroadcastInterruptionStatus::Cancelled, TEXT("ChannelUnregistered"));
	}
	RegisteredDefinitions.Remove(Channel->Definition);
	Channels.Remove(ChannelId);
	for (TPair<FGuid, TWeakObjectPtr<UDocBroadcastReceiverComponent>>& Kvp : Receivers)
	{
		if (UDocBroadcastReceiverComponent* Receiver = Kvp.Value.Get())
		{
			if (Receiver->TunedChannelId == ChannelId)
			{
				SyncReceiver(*Receiver);
			}
		}
	}
	return FDocSystemResult::MakeSuccess();
}

double UDocBroadcastSubsystem::UnderlyingOffset(const FChannelRuntime& Channel)
{
	return Channel.ClockSeconds - Channel.AnchorEpochSeconds - Channel.HeldSeconds;
}

void UDocBroadcastSubsystem::ComputeNowPlaying(const FChannelRuntime& Channel, FName& OutProgramId, double& OutCursor, bool& bOutInterruption) const
{
	OutProgramId = NAME_None;
	OutCursor = 0.0;
	bOutInterruption = Channel.Current.bIsInterruption;
	if (Channel.Current.bIsInterruption)
	{
		for (const FDocBroadcastInterruption& Item : Channel.Interruptions)
		{
			if (Item.InterruptionId == Channel.Current.InterruptionId)
			{
				OutProgramId = Item.Program ? Item.Program->ProgramId : NAME_None;
				OutCursor = Item.PlayedSeconds;
				return;
			}
		}
		return;
	}
	if (Channel.Current.EntryIndex == INDEX_NONE || !Channel.Definition)
	{
		return;
	}
	FDocBroadcastResolvedSlot Slot;
	if (Channel.Definition->ResolveSchedule(UnderlyingOffset(Channel), Slot) && Slot.bHasProgram)
	{
		const UDocBroadcastProgramDefinition* Program = Channel.Definition->GetEntryProgram(Slot.EntryIndex);
		OutProgramId = Program ? Program->ProgramId : NAME_None;
		OutCursor = Slot.ProgramCursorSeconds;
	}
}

bool UDocBroadcastSubsystem::QueryNowPlaying(FName ChannelId, FName& OutProgramId, double& OutProgramCursor, bool& bOutIsInterruption) const
{
	OutProgramId = NAME_None;
	OutProgramCursor = 0.0;
	bOutIsInterruption = false;
	const FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return false;
	}
	ComputeNowPlaying(*Channel, OutProgramId, OutProgramCursor, bOutIsInterruption);
	return !OutProgramId.IsNone();
}

bool UDocBroadcastSubsystem::QueryTransport(FName ChannelId, FDocBroadcastTransport& OutTransport) const
{
	const FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return false;
	}
	OutTransport = CaptureTransportState(ChannelId);
	return true;
}

FDocBroadcastTransport UDocBroadcastSubsystem::CaptureTransportState(FName ChannelId) const
{
	FDocBroadcastTransport Out;
	const FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel || !Channel->Definition)
	{
		return Out;
	}
	Out.ChannelId = ChannelId;
	Out.ScheduleVersion = Channel->Definition->ScheduleVersion;
	Out.AnchorEpochSeconds = Channel->AnchorEpochSeconds;
	Out.ClockSeconds = Channel->ClockSeconds;
	Out.HeldSeconds = Channel->HeldSeconds;
	Out.FictionalSeconds = Channel->FictionalSeconds;
	Out.ClockScale = Channel->ClockScale;
	Out.bPaused = Channel->bPaused;
	Out.State = Channel->State;
	bool bInterruption = false;
	ComputeNowPlaying(*Channel, Out.CurrentProgramId, Out.ProgramCursorSeconds, bInterruption);
	Out.bInterrupted = bInterruption;
	Out.ScheduleOffsetSeconds = UnderlyingOffset(*Channel);
	Out.TotalScheduleDuration = Channel->Definition->GetTotalDuration();
	Out.ActiveInterruptionCount = Channel->Interruptions.Num();
	Out.TransportGeneration = Channel->Generation;
	Out.ScheduleRevision = Channel->ScheduleRevision;
	Out.SourceFailureCount = Channel->SourceFailureCount;
	return Out;
}

// ---------------------------------------------------------------------------------------------
// Receivers
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocBroadcastSubsystem::RegisterReceiver(UDocBroadcastReceiverComponent* Receiver)
{
	if (!Receiver)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No receiver"));
	}
	if (!Receiver->ReceiverId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Receiver has no ReceiverId"));
	}
	if (const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Existing = Receivers.Find(Receiver->ReceiverId))
	{
		if (Existing->Get() == Receiver)
		{
			SyncReceiver(*Receiver);
			return FDocSystemResult::MakeNoChange(TEXT("Receiver already registered"));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Another live receiver uses this ReceiverId"));
		}
	}
	if (!FMath::IsFinite(Receiver->Volume))
	{
		Receiver->Volume = 1.0f;
	}
	Receiver->Volume = FMath::Clamp(Receiver->Volume, 0.0f, 1.0f);
	Receivers.Add(Receiver->ReceiverId, Receiver);
	// Rejoin the channel's current cursor; the channel itself is untouched.
	SyncReceiver(*Receiver);
	return FDocSystemResult::MakeSuccess();
}

void UDocBroadcastSubsystem::UnregisterReceiver(UDocBroadcastReceiverComponent* Receiver)
{
	if (!Receiver)
	{
		return;
	}
	const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Existing = Receivers.Find(Receiver->ReceiverId);
	if (Existing && Existing->Get() == Receiver)
	{
		Receivers.Remove(Receiver->ReceiverId);
	}
}

FDocSystemResult UDocBroadcastSubsystem::TuneReceiver(const FGuid& ReceiverId, FName ChannelId)
{
	const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Found = Receivers.Find(ReceiverId);
	UDocBroadcastReceiverComponent* Receiver = Found ? Found->Get() : nullptr;
	if (!Receiver)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown receiver"));
	}
	if (!ChannelId.IsNone() && !Channels.Contains(ChannelId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	if (Receiver->TunedChannelId == ChannelId)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Receiver->TunedChannelId = ChannelId;
	SyncReceiver(*Receiver);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::SetReceiverVolume(const FGuid& ReceiverId, float Volume)
{
	const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Found = Receivers.Find(ReceiverId);
	UDocBroadcastReceiverComponent* Receiver = Found ? Found->Get() : nullptr;
	if (!Receiver)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown receiver"));
	}
	if (!FMath::IsFinite(Volume))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Volume must be finite"));
	}
	const float Clamped = FMath::Clamp(Volume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Receiver->Volume, Clamped))
	{
		return FDocSystemResult::MakeNoChange();
	}
	Receiver->Volume = Clamped;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::MuteReceiver(const FGuid& ReceiverId, bool bMuted)
{
	const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Found = Receivers.Find(ReceiverId);
	UDocBroadcastReceiverComponent* Receiver = Found ? Found->Get() : nullptr;
	if (!Receiver)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown receiver"));
	}
	if (Receiver->bIsMuted == bMuted)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Receiver->bIsMuted = bMuted;
	return FDocSystemResult::MakeSuccess();
}

bool UDocBroadcastSubsystem::QueryReceiver(const FGuid& ReceiverId, FDocBroadcastReceiverState& OutState) const
{
	const TWeakObjectPtr<UDocBroadcastReceiverComponent>* Found = Receivers.Find(ReceiverId);
	const UDocBroadcastReceiverComponent* Receiver = Found ? Found->Get() : nullptr;
	if (!Receiver)
	{
		return false;
	}
	OutState = Receiver->GetReceiverState();
	return true;
}

void UDocBroadcastSubsystem::SyncReceiver(UDocBroadcastReceiverComponent& Receiver) const
{
	const FChannelRuntime* Channel = Receiver.TunedChannelId.IsNone() ? nullptr : Channels.Find(Receiver.TunedChannelId);
	if (!Channel)
	{
		Receiver.LastObservedProgramId = NAME_None;
		Receiver.LastObservedCursor = 0.0;
		Receiver.LastObservedState = EDocBroadcastTransportState::NoProgram;
		return;
	}
	bool bInterruption = false;
	ComputeNowPlaying(*Channel, Receiver.LastObservedProgramId, Receiver.LastObservedCursor, bInterruption);
	Receiver.LastObservedState = Channel->State;
}

void UDocBroadcastSubsystem::SyncReceivers(FName ChannelId, const FChannelRuntime& Channel)
{
	for (TPair<FGuid, TWeakObjectPtr<UDocBroadcastReceiverComponent>>& Kvp : Receivers)
	{
		if (UDocBroadcastReceiverComponent* Receiver = Kvp.Value.Get())
		{
			if (Receiver->TunedChannelId == ChannelId)
			{
				SyncReceiver(*Receiver);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocBroadcastSubsystem::SetTransportPaused(FName ChannelId, bool bPaused)
{
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	if (Channel->bPaused == bPaused)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Channel->bPaused = bPaused;
	if (bPaused)
	{
		CancelPendingOpen(*Channel);
		Channel->State = EDocBroadcastTransportState::Paused;
		++Channel->Generation;
		SyncReceivers(ChannelId, *Channel);
	}
	else
	{
		// Reopen at the held cursor.
		RefreshContent(ChannelId, *Channel, /*bEmitEvents*/ false, /*bForceReopen*/ true);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::SeekChannelAuthorized(FName ChannelId, double TargetProgramCursor)
{
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	if (!FMath::IsFinite(TargetProgramCursor))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Seek target must be finite"));
	}
	if (FindActiveInterruption(*Channel))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Cannot seek the schedule while an interruption plays"));
	}
	FDocBroadcastResolvedSlot Slot;
	if (!Channel->Definition->ResolveSchedule(UnderlyingOffset(*Channel), Slot) || !Slot.bHasProgram)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("No scheduled program to seek"));
	}
	const UDocBroadcastProgramDefinition* Program = Channel->Definition->GetEntryProgram(Slot.EntryIndex);
	if (!Program || !Program->bCanSeek || !GetPlaybackProvider()->GetCapabilities(Program).bCanSeek)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Program source cannot seek"));
	}
	const double SlotLength = Slot.SlotEndOffset - Slot.SlotStartOffset;
	if (TargetProgramCursor < 0.0 || TargetProgramCursor >= SlotLength)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Seek target outside the current slot"));
	}
	const double NewOffset = Slot.SlotStartOffset + TargetProgramCursor;
	Channel->AnchorEpochSeconds = Channel->ClockSeconds - Channel->HeldSeconds - NewOffset;
	++Channel->ScheduleRevision;
	RefreshContent(ChannelId, *Channel, /*bEmitEvents*/ false, /*bForceReopen*/ true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::SetScheduleClockScale(FName ChannelId, float Scale)
{
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	if (!FMath::IsFinite(Scale) || Scale < 0.0f || Scale > 10000.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Clock scale must be in [0, 10000]"));
	}
	Channel->ClockScale = Scale;
	return FDocSystemResult::MakeSuccess();
}

void UDocBroadcastSubsystem::AdvanceTime(float DeltaSeconds)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	GetPlaybackProvider()->Poll();

	TArray<FName> Ids;
	Channels.GetKeys(Ids);
	for (const FName& ChannelId : Ids)
	{
		FChannelRuntime* Channel = Channels.Find(ChannelId);
		if (!Channel || Channel->bPaused)
		{
			continue;
		}
		double Remaining = DeltaSeconds;
		Channel->FictionalSeconds += static_cast<double>(DeltaSeconds) * static_cast<double>(Channel->ClockScale);

		FDocBroadcastResolvedSlot Before;
		const bool bHadBefore = Channel->Definition->ResolveSchedule(UnderlyingOffset(*Channel), Before);

		for (int32 Guard = 0; Remaining > 0.0 && Guard < 64; ++Guard)
		{
			ExpireQueued(ChannelId, *Channel);
			FDocBroadcastInterruption* Active = FindActiveInterruptionMutable(*Channel);
			if (!Active)
			{
				Channel->ClockSeconds += Remaining;
				Remaining = 0.0;
				break;
			}
			Active->bHasStarted = true;
			Active->Status = EDocBroadcastInterruptionStatus::Active;
			const double Left = FMath::Max(0.0, Active->DurationSeconds - Active->PlayedSeconds);
			const double Step = FMath::Min(Remaining, Left);
			Active->PlayedSeconds += Step;
			Channel->ClockSeconds += Step;
			if (Active->ResumePolicy == EDocBroadcastInterruptionPolicy::PauseUnderlyingCursor)
			{
				Channel->HeldSeconds += Step;
			}
			Remaining -= Step;
			if (Active->PlayedSeconds >= Active->DurationSeconds - DocBroadcastPrivate::CompletionEpsilon)
			{
				EndInterruption(ChannelId, *Channel, Active->InterruptionId, EDocBroadcastInterruptionStatus::Completed, FString());
			}
		}
		ExpireQueued(ChannelId, *Channel);

		if (Channel->Definition->ClockPolicy == EDocBroadcastClockPolicy::ReanchorAtScheduleBoundary && !FindActiveInterruption(*Channel))
		{
			FDocBroadcastResolvedSlot After;
			if (bHadBefore && Channel->Definition->ResolveSchedule(UnderlyingOffset(*Channel), After)
				&& !FMath::IsNearlyEqual(After.SlotStartOffset, Before.SlotStartOffset))
			{
				ApplyReanchor(*Channel);
			}
		}
		RefreshContent(ChannelId, *Channel, /*bEmitEvents*/ true);
	}
}

void UDocBroadcastSubsystem::ApplyReanchor(FChannelRuntime& Channel)
{
	FDocBroadcastResolvedSlot Fictional;
	if (!Channel.Definition->ResolveSchedule(Channel.FictionalSeconds, Fictional))
	{
		return;
	}
	// Start the entry the fictional clock indicates from its beginning, at normal playback speed.
	const double Target = Fictional.SlotStartOffset;
	Channel.AnchorEpochSeconds = Channel.ClockSeconds - Channel.HeldSeconds - Target;
	++Channel.ScheduleRevision;
}

// ---------------------------------------------------------------------------------------------
// Content selection and asynchronous open
// ---------------------------------------------------------------------------------------------

const FDocBroadcastInterruption* UDocBroadcastSubsystem::FindActiveInterruption(const FChannelRuntime& Channel)
{
	const FDocBroadcastInterruption* Best = nullptr;
	for (const FDocBroadcastInterruption& Item : Channel.Interruptions)
	{
		if (!Best || Item.Priority > Best->Priority || (Item.Priority == Best->Priority && Item.RequestOrdinal < Best->RequestOrdinal))
		{
			Best = &Item;
		}
	}
	return Best;
}

FDocBroadcastInterruption* UDocBroadcastSubsystem::FindActiveInterruptionMutable(FChannelRuntime& Channel)
{
	return const_cast<FDocBroadcastInterruption*>(FindActiveInterruption(Channel));
}

void UDocBroadcastSubsystem::ExpireQueued(FName ChannelId, FChannelRuntime& Channel)
{
	const FDocBroadcastInterruption* Active = FindActiveInterruption(Channel);
	const FGuid ActiveId = Active ? Active->InterruptionId : FGuid();
	TArray<FGuid> Expired;
	for (const FDocBroadcastInterruption& Item : Channel.Interruptions)
	{
		if (Item.InterruptionId != ActiveId && Item.ExpiryTimeSeconds > 0.0 && Channel.ClockSeconds >= Item.ExpiryTimeSeconds)
		{
			Expired.Add(Item.InterruptionId);
		}
	}
	for (const FGuid& Id : Expired)
	{
		EndInterruption(ChannelId, Channel, Id, EDocBroadcastInterruptionStatus::Expired, TEXT("Expired before it could play"));
	}
}

void UDocBroadcastSubsystem::ReleaseInterruptionProgram(const FDocBroadcastInterruption& Record)
{
	const int32 Index = InterruptionPrograms.Find(Record.Program);
	if (Index != INDEX_NONE)
	{
		InterruptionPrograms.RemoveAt(Index);
	}
}

void UDocBroadcastSubsystem::EndInterruption(FName ChannelId, FChannelRuntime& Channel, const FGuid& InterruptionId, EDocBroadcastInterruptionStatus Status, const FString& Reason)
{
	const int32 Index = Channel.Interruptions.IndexOfByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.InterruptionId == InterruptionId; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	FDocBroadcastInterruption Record = Channel.Interruptions[Index];
	Channel.Interruptions.RemoveAt(Index);
	Record.Status = Status;
	Record.TerminalReason = Reason;
	if (Channel.Current.bIsInterruption && Channel.Current.InterruptionId == InterruptionId)
	{
		// Its pending open (if any) must never start it later.
		CancelPendingOpen(Channel);
	}
	ReleaseInterruptionProgram(Record);
	Channel.History.Add(Record);
	if (Channel.History.Num() > DocBroadcastPrivate::MaxHistoryPerChannel)
	{
		Channel.History.RemoveAt(0, Channel.History.Num() - DocBroadcastPrivate::MaxHistoryPerChannel);
	}
	OnInterruptionEnded.Broadcast(ChannelId, InterruptionId, Status);
}

void UDocBroadcastSubsystem::CancelPendingOpen(FChannelRuntime& Channel)
{
	if (Channel.PendingTicket != 0)
	{
		if (PlaybackProvider)
		{
			PlaybackProvider->CancelOpen(Channel.PendingTicket);
		}
		Channel.PendingTicket = 0;
	}
}

void UDocBroadcastSubsystem::RefreshContent(FName ChannelId, FChannelRuntime& Channel, bool bEmitEvents, bool bForceReopen)
{
	if (RefreshDepth >= DocBroadcastPrivate::MaxRefreshDepth)
	{
		// Every candidate failed in a row (e.g. Skip across an all-failing schedule): go silent instead of looping.
		CancelPendingOpen(Channel);
		Channel.State = EDocBroadcastTransportState::UnavailableSource;
		Channel.bCurrentSourceFailed = true;
		return;
	}
	++RefreshDepth;

	// Keep statuses truthful: exactly one live request is Active.
	const FDocBroadcastInterruption* Active = FindActiveInterruption(Channel);
	for (FDocBroadcastInterruption& Item : Channel.Interruptions)
	{
		Item.Status = (Active && Item.InterruptionId == Active->InterruptionId) ? EDocBroadcastInterruptionStatus::Active : EDocBroadcastInterruptionStatus::Queued;
	}

	FContentKey Desired;
	if (Active)
	{
		Desired.bIsInterruption = true;
		Desired.InterruptionId = Active->InterruptionId;
	}
	else
	{
		FDocBroadcastResolvedSlot Slot;
		if (Channel.Definition && Channel.Definition->ResolveSchedule(UnderlyingOffset(Channel), Slot) && Slot.bHasProgram)
		{
			Desired.EntryIndex = Slot.EntryIndex;
			Desired.LoopIndex = Slot.LoopIndex;
		}
	}

	const bool bChanged = Desired != Channel.Current;
	if (bChanged || bForceReopen)
	{
		CancelPendingOpen(Channel);
		Channel.Current = Desired;
		++Channel.Generation;
		Channel.OpenAttempts = 0;
		Channel.bCurrentSourceFailed = false;

		if (bChanged && bEmitEvents)
		{
			FName ProgramId;
			double Cursor = 0.0;
			bool bInterruption = false;
			ComputeNowPlaying(Channel, ProgramId, Cursor, bInterruption);
			OnProgramChanged.Broadcast(ChannelId, ProgramId, bInterruption);
		}

		if (Desired.IsNone())
		{
			Channel.State = Channel.bPaused ? EDocBroadcastTransportState::Paused : EDocBroadcastTransportState::NoProgram;
		}
		else if (Channel.bPaused)
		{
			Channel.State = EDocBroadcastTransportState::Paused;
		}
		else
		{
			BeginOpen(ChannelId, Channel);
		}
	}
	else if (Channel.bPaused)
	{
		Channel.State = EDocBroadcastTransportState::Paused;
	}

	SyncReceivers(ChannelId, Channel);
	--RefreshDepth;
}

void UDocBroadcastSubsystem::BeginOpen(FName ChannelId, FChannelRuntime& Channel)
{
	const UDocBroadcastProgramDefinition* Program = nullptr;
	double Cursor = 0.0;
	if (Channel.Current.bIsInterruption)
	{
		for (const FDocBroadcastInterruption& Item : Channel.Interruptions)
		{
			if (Item.InterruptionId == Channel.Current.InterruptionId)
			{
				Program = Item.Program;
				Cursor = Item.PlayedSeconds;
			}
		}
	}
	else
	{
		FDocBroadcastResolvedSlot Slot;
		if (Channel.Definition->ResolveSchedule(UnderlyingOffset(Channel), Slot) && Slot.bHasProgram)
		{
			Program = Channel.Definition->GetEntryProgram(Slot.EntryIndex);
			Cursor = Slot.ProgramCursorSeconds;
		}
	}
	if (!Program)
	{
		Channel.State = EDocBroadcastTransportState::NoProgram;
		return;
	}

	const int64 Ticket = NextTicket++;
	Channel.PendingTicket = Ticket;
	Channel.State = EDocBroadcastTransportState::Loading;
	++Channel.OpenAttempts;
	const FDocSystemResult Opened = GetPlaybackProvider()->OpenProgram(Ticket, Program, Cursor, this);
	if (!Opened.IsSuccess())
	{
		FChannelRuntime* Live = Channels.Find(ChannelId);
		if (Live && Live->PendingTicket == Ticket)
		{
			Live->PendingTicket = 0;
			if (Opened.Outcome == EDocResultOutcome::Unsupported)
			{
				Live->State = EDocBroadcastTransportState::Faulted;
				++Live->SourceFailureCount;
			}
			else
			{
				++Live->SourceFailureCount;
				HandleOpenFailure(ChannelId, *Live, Opened.ToString());
			}
		}
	}
}

bool UDocBroadcastSubsystem::NotifyOpenResult(int64 Ticket, bool bSuccess, const FString& Reason)
{
	if (Ticket == 0)
	{
		return false;
	}
	for (TPair<FName, FChannelRuntime>& Kvp : Channels)
	{
		FChannelRuntime& Channel = Kvp.Value;
		if (Channel.PendingTicket != Ticket)
		{
			continue;
		}
		Channel.PendingTicket = 0;
		if (Channel.bPaused)
		{
			Channel.State = EDocBroadcastTransportState::Paused;
		}
		else if (bSuccess)
		{
			Channel.bCurrentSourceFailed = false;
			Channel.State = Channel.Current.bIsInterruption ? EDocBroadcastTransportState::Interrupted : EDocBroadcastTransportState::Playing;
		}
		else
		{
			++Channel.SourceFailureCount;
			HandleOpenFailure(Kvp.Key, Channel, Reason);
		}
		SyncReceivers(Kvp.Key, Channel);
		return true;
	}
	return false; // stale: replaced, cancelled, released or unknown
}

void UDocBroadcastSubsystem::HandleOpenFailure(FName ChannelId, FChannelRuntime& Channel, const FString& Reason)
{
	const UDocBroadcastChannelDefinition* Def = Channel.Definition;
	const EDocBroadcastSourceFailurePolicy Policy = Def->SourceFailurePolicy;

	if (Policy == EDocBroadcastSourceFailurePolicy::RetryBounded && Channel.OpenAttempts <= Def->MaxOpenRetries)
	{
		BeginOpen(ChannelId, Channel);
		return;
	}

	if (Channel.Current.bIsInterruption)
	{
		EndInterruption(ChannelId, Channel, Channel.Current.InterruptionId, EDocBroadcastInterruptionStatus::SourceFailed, Reason);
		RefreshContent(ChannelId, Channel, /*bEmitEvents*/ true);
		return;
	}

	if (Policy == EDocBroadcastSourceFailurePolicy::Skip)
	{
		FDocBroadcastResolvedSlot Slot;
		if (Def->ResolveSchedule(UnderlyingOffset(Channel), Slot) && Slot.bHasProgram)
		{
			Channel.AnchorEpochSeconds = Channel.ClockSeconds - Channel.HeldSeconds - Slot.SlotEndOffset;
			++Channel.ScheduleRevision;
			RefreshContent(ChannelId, Channel, /*bEmitEvents*/ true);
			return;
		}
	}

	// SilenceGap (and exhausted retries): stay silent until the slot ends; schedule time keeps running.
	Channel.State = EDocBroadcastTransportState::UnavailableSource;
	Channel.bCurrentSourceFailed = true;
	SyncReceivers(ChannelId, Channel);
}

// ---------------------------------------------------------------------------------------------
// Interruptions
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocBroadcastSubsystem::PushInterruption(FName ChannelId, const FDocBroadcastInterruption& Request, FGuid& OutInterruptionId)
{
	OutInterruptionId.Invalidate();
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	if (!Request.OwnerId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Interruption needs an owner handle"));
	}
	if (!Request.Program)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Interruption needs a program"));
	}
	const FDocSystemResult ProgramValid = Request.Program->ValidateProgram(/*bRequireSeek*/ false);
	if (!ProgramValid.IsSuccess())
	{
		return ProgramValid;
	}
	const FDocSystemResult Caps = CheckProgramCapabilities(Request.Program, /*bRequireSeek*/ false);
	if (!Caps.IsSuccess())
	{
		return Caps;
	}
	if (!FMath::IsFinite(Request.DurationSeconds) || !FMath::IsFinite(Request.ExpiryTimeSeconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Duration/expiry must be finite"));
	}

	FDocBroadcastInterruption Record = Request;
	if (!Record.InterruptionId.IsValid())
	{
		Record.InterruptionId = FGuid::NewGuid();
	}
	const bool bDuplicate = Channel->Interruptions.ContainsByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.InterruptionId == Record.InterruptionId; })
		|| Channel->History.ContainsByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.InterruptionId == Record.InterruptionId; });
	if (bDuplicate)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("InterruptionId already used"));
	}
	Record.DurationSeconds = Record.DurationSeconds <= 0.0 ? Request.Program->DurationSeconds : FMath::Min(Record.DurationSeconds, Request.Program->DurationSeconds);
	Record.RequestOrdinal = Channel->NextOrdinal++;
	Record.PlayedSeconds = 0.0;
	Record.bHasStarted = false;
	Record.TerminalReason.Reset();
	OutInterruptionId = Record.InterruptionId;

	auto RecordTerminal = [&](EDocBroadcastInterruptionStatus Status, const FString& Reason)
	{
		Record.Status = Status;
		Record.TerminalReason = Reason;
		Channel->History.Add(Record);
		if (Channel->History.Num() > DocBroadcastPrivate::MaxHistoryPerChannel)
		{
			Channel->History.RemoveAt(0);
		}
	};

	if (Record.ExpiryTimeSeconds > 0.0 && Channel->ClockSeconds >= Record.ExpiryTimeSeconds)
	{
		RecordTerminal(EDocBroadcastInterruptionStatus::Expired, TEXT("Expired before queueing"));
		return FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Interruption already expired"));
	}

	if (Record.Arbitration == EDocBroadcastArbitration::Reject)
	{
		const bool bBlocked = Channel->Interruptions.ContainsByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.Priority >= Record.Priority; });
		if (bBlocked)
		{
			RecordTerminal(EDocBroadcastInterruptionStatus::Rejected, TEXT("Equal or higher priority request present"));
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Rejected: equal or higher priority interruption present"));
		}
	}
	else if (Record.Arbitration == EDocBroadcastArbitration::Replace)
	{
		TArray<FGuid> Replaced;
		for (const FDocBroadcastInterruption& Item : Channel->Interruptions)
		{
			if (Item.Priority <= Record.Priority)
			{
				Replaced.Add(Item.InterruptionId);
			}
		}
		for (const FGuid& Id : Replaced)
		{
			EndInterruption(ChannelId, *Channel, Id, EDocBroadcastInterruptionStatus::Replaced, TEXT("Replaced by a newer request"));
		}
	}

	Record.Status = EDocBroadcastInterruptionStatus::Queued;
	Channel->Interruptions.Add(Record);
	InterruptionPrograms.Add(Record.Program);
	RefreshContent(ChannelId, *Channel, /*bEmitEvents*/ true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocBroadcastSubsystem::ReleaseInterruption(FName ChannelId, const FGuid& InterruptionId, const FGuid& OwnerId)
{
	FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	const FDocBroadcastInterruption* Live = Channel->Interruptions.FindByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.InterruptionId == InterruptionId; });
	if (!Live)
	{
		const FDocBroadcastInterruption* Old = Channel->History.FindByPredicate([&](const FDocBroadcastInterruption& Item) { return Item.InterruptionId == InterruptionId; });
		if (Old)
		{
			return Old->OwnerId == OwnerId
				? FDocSystemResult::MakeNoChange(TEXT("Interruption already ended"))
				: FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Not the owner of this interruption"));
		}
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown interruption"));
	}
	if (Live->OwnerId != OwnerId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Not the owner of this interruption"));
	}
	EndInterruption(ChannelId, *Channel, InterruptionId, EDocBroadcastInterruptionStatus::Released, TEXT("Released by owner"));
	RefreshContent(ChannelId, *Channel, /*bEmitEvents*/ true);
	return FDocSystemResult::MakeSuccess();
}

bool UDocBroadcastSubsystem::QueryInterruption(FName ChannelId, const FGuid& InterruptionId, FDocBroadcastInterruption& OutRecord) const
{
	const FChannelRuntime* Channel = Channels.Find(ChannelId);
	if (!Channel)
	{
		return false;
	}
	for (const FDocBroadcastInterruption& Item : Channel->Interruptions)
	{
		if (Item.InterruptionId == InterruptionId)
		{
			OutRecord = Item;
			return true;
		}
	}
	for (int32 Index = Channel->History.Num() - 1; Index >= 0; --Index)
	{
		if (Channel->History[Index].InterruptionId == InterruptionId)
		{
			OutRecord = Channel->History[Index];
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Persistence and measurement
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocBroadcastSubsystem::StageRestoreTransportState(const FDocBroadcastTransport& InTransport)
{
	FChannelRuntime* Channel = Channels.Find(InTransport.ChannelId);
	if (!Channel)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Register the channel before restoring it"));
	}
	using namespace DocBroadcastPrivate;
	if (!IsSaneTime(InTransport.AnchorEpochSeconds) || !IsSaneTime(InTransport.ClockSeconds) || !IsSaneTime(InTransport.HeldSeconds)
		|| !IsSaneTime(InTransport.FictionalSeconds) || InTransport.HeldSeconds < 0.0
		|| !FMath::IsFinite(InTransport.ClockScale) || InTransport.ClockScale < 0.0f || InTransport.ClockScale > 10000.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Saved transport has invalid time values"));
	}

	// Live leases belong to the pre-restore session; end them quietly (no historical event replay).
	CancelPendingOpen(*Channel);
	for (const FDocBroadcastInterruption& Item : Channel->Interruptions)
	{
		FDocBroadcastInterruption Record = Item;
		Record.Status = EDocBroadcastInterruptionStatus::Cancelled;
		Record.TerminalReason = TEXT("Restore");
		ReleaseInterruptionProgram(Record);
		Channel->History.Add(Record);
	}
	Channel->Interruptions.Reset();
	if (Channel->History.Num() > MaxHistoryPerChannel)
	{
		Channel->History.RemoveAt(0, Channel->History.Num() - MaxHistoryPerChannel);
	}

	Channel->AnchorEpochSeconds = InTransport.AnchorEpochSeconds;
	Channel->ClockSeconds = InTransport.ClockSeconds;
	Channel->HeldSeconds = InTransport.HeldSeconds;
	Channel->FictionalSeconds = InTransport.FictionalSeconds;
	Channel->ClockScale = InTransport.ClockScale;
	Channel->bPaused = InTransport.bPaused;
	++Channel->ScheduleRevision;
	Channel->Current = FContentKey();
	RefreshContent(InTransport.ChannelId, *Channel, /*bEmitEvents*/ false, /*bForceReopen*/ true);

	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	if (InTransport.ScheduleVersion != Channel->Definition->ScheduleVersion)
	{
		Result.Diagnostic = FString::Printf(TEXT("ScheduleVersionMigrated %d -> %d: saved offset resolved against the current schedule"),
			InTransport.ScheduleVersion, Channel->Definition->ScheduleVersion);
	}
	return Result;
}

FDocSystemResult UDocBroadcastSubsystem::MeasureLocalSyncTolerance(FName ChannelId, double& OutToleranceSeconds)
{
	OutToleranceSeconds = 0.0;
	if (!Channels.Contains(ChannelId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown channel"));
	}
	const FDocBroadcastProviderCapabilities Caps = GetPlaybackProvider()->GetCapabilities(nullptr);
	if (!Caps.bAudible)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			TEXT("The active playback provider is logic-only; there is no audible output to measure"));
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
		TEXT("No measurement harness is shipped; local sync tolerance must be measured in the cooked-audio manual gate"));
}
