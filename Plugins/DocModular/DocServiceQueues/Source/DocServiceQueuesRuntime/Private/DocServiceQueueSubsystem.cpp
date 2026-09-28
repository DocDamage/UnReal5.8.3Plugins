#include "DocServiceQueueSubsystem.h"
#include "DocServiceQueuesLog.h"
#include "Engine/World.h"

namespace DocQueuePrivate
{
	const FName ReasonLeft(TEXT("Left"));
	const FName ReasonQueueRemoved(TEXT("QueueRemoved"));
	const FName ReasonStationRemoved(TEXT("StationRemoved"));
	const FName ReasonStationClosed(TEXT("StationClosed"));
	const FName ReasonEmergencyAbort(TEXT("EmergencyAbort"));
	const FName ReasonDeclined(TEXT("Declined"));
	const FName ReasonOfferExpired(TEXT("OfferExpired"));
	const FName ReasonNoShow(TEXT("NoShow"));
	const FName ReasonRestoredNewLease(TEXT("RestoredNewLease"));
	const FName ReasonRestoredInService(TEXT("RestoredInService"));
	const FName ReasonReplacedByRestore(TEXT("ReplacedByRestore"));
	const FName ReasonOversized(TEXT("OversizedForStation"));
	const FName ReasonHeadDoesNotFit(TEXT("HeadDoesNotFit"));
	const FName ReasonBypassLimit(TEXT("BypassLimitReached"));
	const FName ReasonNoEligible(TEXT("NoEligibleTicket"));

	static bool HoldsReservation(EDocQueueTicketState State)
	{
		return State == EDocQueueTicketState::Reserved || State == EDocQueueTicketState::Arriving || State == EDocQueueTicketState::Ready;
	}
}

void UDocServiceQueueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDocServiceQueues, Log, TEXT("DocServiceQueueSubsystem initialized."));
}

void UDocServiceQueueSubsystem::Deinitialize()
{
	Queues.Empty();
	Stations.Empty();
	ActiveOffers.Empty();
	ActiveReservations.Empty();
	ClockOverride = nullptr;
	Super::Deinitialize();
}

double UDocServiceQueueSubsystem::GetCurrentTimeSeconds() const
{
	if (ClockOverride)
	{
		return ClockOverride();
	}
	UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UDocServiceQueueSubsystem::IsTerminal(EDocQueueTicketState State)
{
	return State == EDocQueueTicketState::Completed || State == EDocQueueTicketState::Cancelled
		|| State == EDocQueueTicketState::Expired || State == EDocQueueTicketState::Rejected;
}

FDocQueueTicket* UDocServiceQueueSubsystem::FindTicket(const FGuid& TicketId)
{
	for (TPair<FName, FQueueRuntimeData>& QKvp : Queues)
	{
		if (FDocQueueTicket* T = QKvp.Value.Tickets.FindByPredicate([&](const FDocQueueTicket& Item) { return Item.TicketId == TicketId; }))
		{
			return T;
		}
	}
	return nullptr;
}

const FDocQueueTicket* UDocServiceQueueSubsystem::FindTicket(const FGuid& TicketId) const
{
	return const_cast<UDocServiceQueueSubsystem*>(this)->FindTicket(TicketId);
}

void UDocServiceQueueSubsystem::ReleaseTicketHolds(FDocQueueTicket& Ticket, EDocQueueTicketState NewState, FName Reason)
{
	// Only this ticket's own lease is released: an offer by its offer id, a reservation by its reservation id.
	if (Ticket.State == EDocQueueTicketState::Offered)
	{
		FDocAdmissionOffer Offer;
		if (ActiveOffers.RemoveAndCopyValue(Ticket.AssignedOfferId, Offer))
		{
			if (FDocStationInfo* Station = Stations.Find(Offer.StationId))
			{
				Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Offer.ReservedSeats);
			}
		}
	}
	else if (DocQueuePrivate::HoldsReservation(Ticket.State))
	{
		FDocStationReservation Reservation;
		if (ActiveReservations.RemoveAndCopyValue(Ticket.ReservationId, Reservation))
		{
			if (FDocStationInfo* Station = Stations.Find(Reservation.StationId))
			{
				Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Reservation.ReservedSeats);
			}
		}
	}
	else if (Ticket.State == EDocQueueTicketState::InService)
	{
		if (FDocStationInfo* Station = Stations.Find(Ticket.AssignedStationId))
		{
			Station->InServiceSeats = FMath::Max(0, Station->InServiceSeats - Ticket.PartySize);
		}
	}

	Ticket.State = NewState;
	Ticket.AssignedOfferId.Invalidate();
	Ticket.ReservationId.Invalidate();
	Ticket.OfferDeadline = 0.0;
	if (NewState == EDocQueueTicketState::Waiting)
	{
		Ticket.AssignedStationId = NAME_None;
		Ticket.BatchId.Invalidate();
	}
	Ticket.StatusReason = Reason;
	Ticket.Revision++;
}

FDocSystemResult UDocServiceQueueSubsystem::RegisterQueue(const UDocServiceQueueDefinition* Definition)
{
	if (!Definition || Definition->QueueId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid queue definition or QueueId."));
	}
	if (Queues.Contains(Definition->QueueId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Queue is already registered."));
	}

	FQueueRuntimeData Data;
	Data.Definition = Definition;
	Queues.Add(Definition->QueueId, MoveTemp(Data));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::UnregisterQueue(FName QueueId)
{
	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found to unregister."));
	}

	for (FDocQueueTicket& Ticket : Queue->Tickets)
	{
		if (!IsTerminal(Ticket.State))
		{
			ReleaseTicketHolds(Ticket, EDocQueueTicketState::Cancelled, DocQueuePrivate::ReasonQueueRemoved);
		}
	}

	Queues.Remove(QueueId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::SetQueueClosed(FName QueueId, bool bClosed)
{
	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	if (Queue->bClosed == bClosed)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Queue->bClosed = bClosed;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::RegisterStation(FName StationId, int32 Capacity, const FGameplayTagContainer& ServiceTags)
{
	if (StationId.IsNone() || Capacity <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid StationId or Capacity <= 0."));
	}
	if (Stations.Contains(StationId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Station is already registered."));
	}

	FDocStationInfo Info;
	Info.StationId = StationId;
	Info.Capacity = Capacity;
	Info.ServiceTags = ServiceTags;
	Info.State = EDocStationOperationalState::Open;
	Stations.Add(StationId, Info);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::UnregisterStation(FName StationId, EDocStationClosePolicy ClosePolicy)
{
	if (!Stations.Contains(StationId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found to unregister."));
	}

	// The station is going away whatever the policy: no ticket may keep a lease on it.
	for (TPair<FName, FQueueRuntimeData>& QKvp : Queues)
	{
		for (FDocQueueTicket& Ticket : QKvp.Value.Tickets)
		{
			if (Ticket.AssignedStationId != StationId || IsTerminal(Ticket.State))
			{
				continue;
			}
			if (Ticket.State == EDocQueueTicketState::InService)
			{
				ReleaseTicketHolds(Ticket, EDocQueueTicketState::Cancelled, DocQueuePrivate::ReasonStationRemoved);
			}
			else if (Ticket.State == EDocQueueTicketState::Offered || DocQueuePrivate::HoldsReservation(Ticket.State))
			{
				ReleaseTicketHolds(Ticket, EDocQueueTicketState::Waiting, DocQueuePrivate::ReasonStationRemoved);
			}
		}
	}

	// Defensive: drop any lease still naming this station.
	for (auto It = ActiveOffers.CreateIterator(); It; ++It)
	{
		if (It->Value.StationId == StationId)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = ActiveReservations.CreateIterator(); It; ++It)
	{
		if (It->Value.StationId == StationId)
		{
			It.RemoveCurrent();
		}
	}

	Stations.Remove(StationId);
	OnStationStateChanged.Broadcast(StationId, EDocStationOperationalState::Closed);
	OnStationStateChangedNative.Broadcast(StationId, EDocStationOperationalState::Closed);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::SetStationOperationalState(FName StationId, EDocStationOperationalState NewState, EDocStationClosePolicy ClosePolicy)
{
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	if (NewState == EDocStationOperationalState::Blocked)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Blocked is set only by an unconfirmed emergency abort."));
	}
	if (Station->State == EDocStationOperationalState::Blocked)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Station is blocked until ConfirmEvacuated."));
	}

	EDocStationOperationalState FinalState = NewState;
	if (NewState == EDocStationOperationalState::Closed || NewState == EDocStationOperationalState::Closing)
	{
		const bool bCancelPending = ClosePolicy == EDocStationClosePolicy::CancelPendingReservations || ClosePolicy == EDocStationClosePolicy::EmergencyAbort;
		bool bOccupied = false;
		for (TPair<FName, FQueueRuntimeData>& QKvp : Queues)
		{
			for (FDocQueueTicket& Ticket : QKvp.Value.Tickets)
			{
				if (Ticket.AssignedStationId != StationId)
				{
					continue;
				}
				if (bCancelPending && (Ticket.State == EDocQueueTicketState::Offered || DocQueuePrivate::HoldsReservation(Ticket.State)))
				{
					ReleaseTicketHolds(Ticket, EDocQueueTicketState::Waiting, DocQueuePrivate::ReasonStationClosed);
				}
				else if (Ticket.State == EDocQueueTicketState::InService)
				{
					bOccupied = true;
				}
			}
		}

		// A failed or unconfirmed evacuation is a blocked station, not a free seat.
		if (ClosePolicy == EDocStationClosePolicy::EmergencyAbort && bOccupied)
		{
			FinalState = EDocStationOperationalState::Blocked;
		}
	}

	Station = Stations.Find(StationId);
	Station->State = FinalState;
	OnStationStateChanged.Broadcast(StationId, FinalState);
	OnStationStateChangedNative.Broadcast(StationId, FinalState);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::ConfirmEvacuated(FName StationId)
{
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	if (Station->State != EDocStationOperationalState::Blocked)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Station is not blocked."));
	}
	for (TPair<FName, FQueueRuntimeData>& QKvp : Queues)
	{
		for (FDocQueueTicket& Ticket : QKvp.Value.Tickets)
		{
			if (Ticket.AssignedStationId == StationId && Ticket.State == EDocQueueTicketState::InService)
			{
				ReleaseTicketHolds(Ticket, EDocQueueTicketState::Cancelled, DocQueuePrivate::ReasonEmergencyAbort);
			}
		}
	}
	Station = Stations.Find(StationId);
	Station->State = EDocStationOperationalState::Closed;
	OnStationStateChanged.Broadcast(StationId, EDocStationOperationalState::Closed);
	OnStationStateChangedNative.Broadcast(StationId, EDocStationOperationalState::Closed);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::JoinQueue(FName QueueId, const FGuid& ParticipantId, int32 PartySize, int32 PriorityClass, FDocQueueTicket& OutTicket, FName JoinKey)
{
	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	if (Queue->bClosed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Queue is closed."));
	}
	if (PartySize < 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("PartySize must be at least 1."));
	}

	const FGuid Participant = ParticipantId.IsValid() ? ParticipantId : FGuid::NewGuid();
	const FName Key = !JoinKey.IsNone() ? JoinKey : (ParticipantId.IsValid() ? FName(*ParticipantId.ToString()) : NAME_None);

	if (!Key.IsNone())
	{
		const FDocQueueTicket* Existing = Queue->Tickets.FindByPredicate([&](const FDocQueueTicket& T) { return T.JoinKey == Key && !IsTerminal(T.State); });
		if (Existing)
		{
			if (Existing->PartySize == PartySize && Existing->ParticipantId == Participant)
			{
				OutTicket = *Existing;
				return FDocSystemResult::MakeNoChange(TEXT("Existing ticket for this join key."));
			}
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Join key already used with a different party."));
		}
	}

	const int32 MaxSelf = Queue->Definition ? FMath::Max(0, Queue->Definition->MaxSelfAssignedPriority) : 0;

	FDocQueueTicket Ticket;
	Ticket.TicketId = FGuid::NewGuid();
	Ticket.QueueId = QueueId;
	Ticket.ParticipantId = Participant;
	Ticket.PartySize = PartySize;
	Ticket.RequestedPriority = PriorityClass;
	Ticket.PriorityClass = FMath::Clamp(PriorityClass, 0, MaxSelf);
	Ticket.JoinOrdinal = Queue->NextOrdinal++;
	Ticket.State = EDocQueueTicketState::Waiting;
	Ticket.JoinTimestamp = GetCurrentTimeSeconds();
	Ticket.JoinKey = Key;
	Ticket.Revision = 1;
	Queue->Tickets.Add(Ticket);
	OutTicket = Ticket;

	OnTicketJoined.Broadcast(QueueId, Ticket);
	OnTicketJoinedNative.Broadcast(QueueId, Ticket);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::SetTicketPriority(const FGuid& TicketId, int32 PriorityClass)
{
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
	}
	if (Ticket->State != EDocQueueTicketState::Waiting)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Priority can only change while waiting."));
	}
	Ticket->PriorityClass = FMath::Max(0, PriorityClass);
	Ticket->Revision++;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::LeaveQueue(const FGuid& TicketId)
{
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found to leave."));
	}
	if (IsTerminal(Ticket->State))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Ticket already ended."));
	}
	ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Cancelled, DocQueuePrivate::ReasonLeft);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::QueryTicket(const FGuid& TicketId, FDocQueueTicket& OutTicket) const
{
	if (const FDocQueueTicket* Ticket = FindTicket(TicketId))
	{
		OutTicket = *Ticket;
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
}

FDocSystemResult UDocServiceQueueSubsystem::QueryQueueView(FName QueueId, FDocQueueView& OutView) const
{
	const FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}

	const bool bPriority = Queue->Definition && Queue->Definition->OrderingPolicy == EDocQueueOrderingPolicy::PriorityThenFIFO;
	OutView = FDocQueueView();
	OutView.QueueId = QueueId;
	OutView.bClosed = Queue->bClosed;
	OutView.HeadBlockedReason = Queue->HeadBlockedReason;

	const FDocQueueTicket* Head = nullptr;
	for (const FDocQueueTicket& T : Queue->Tickets)
	{
		if (T.State != EDocQueueTicketState::Waiting)
		{
			continue;
		}
		OutView.TotalWaitingTickets++;
		OutView.TotalWaitingSeats += T.PartySize;
		const bool bBetter = !Head
			|| (bPriority && T.PriorityClass != Head->PriorityClass ? T.PriorityClass > Head->PriorityClass : T.JoinOrdinal < Head->JoinOrdinal);
		if (bBetter)
		{
			Head = &T;
		}
	}
	if (Head)
	{
		OutView.HeadTicketId = Head->TicketId;
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::QueryStation(FName StationId, FDocStationInfo& OutInfo) const
{
	const FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	OutInfo = *Station;
	return FDocSystemResult::MakeSuccess();
}

bool UDocServiceQueueSubsystem::SelectCandidates(FQueueRuntimeData& Queue, const FDocStationInfo& Station, int32 SeatBudget, bool bMany, TArray<int32>& OutSelected, TArray<int32>& OutBypassed, FName& OutBlockedReason) const
{
	using namespace DocQueuePrivate;
	OutSelected.Reset();
	OutBypassed.Reset();
	OutBlockedReason = NAME_None;

	const UDocServiceQueueDefinition* Def = Queue.Definition;
	const EDocQueueOrderingPolicy Policy = Def ? Def->OrderingPolicy : EDocQueueOrderingPolicy::StrictFIFO;
	const int32 MaxBypass = Def ? Def->MaxBypassCount : 3;
	const double MaxAge = Def ? (double)Def->MaxBypassAgeSeconds : 0.0;
	const double Now = GetCurrentTimeSeconds();

	TArray<int32> Order;
	for (int32 Index = 0; Index < Queue.Tickets.Num(); ++Index)
	{
		if (Queue.Tickets[Index].State == EDocQueueTicketState::Waiting)
		{
			Order.Add(Index);
		}
	}
	if (Order.IsEmpty())
	{
		OutBlockedReason = ReasonNoEligible;
		return false;
	}
	Order.Sort([&](int32 A, int32 B) {
		const FDocQueueTicket& TA = Queue.Tickets[A];
		const FDocQueueTicket& TB = Queue.Tickets[B];
		if (Policy == EDocQueueOrderingPolicy::PriorityThenFIFO && TA.PriorityClass != TB.PriorityClass)
		{
			return TA.PriorityClass > TB.PriorityClass;
		}
		return TA.JoinOrdinal < TB.JoinOrdinal; // Stable tie: join ordinal.
	});

	int32 Remaining = SeatBudget;
	bool bSawOversized = false;
	TArray<int32> Passed;
	for (int32 Index : Order)
	{
		const FDocQueueTicket& T = Queue.Tickets[Index];
		if (T.PartySize > Station.Capacity)
		{
			// Can never be served here: not a bypass, and it does not block tickets behind it.
			bSawOversized = true;
			continue;
		}
		if (T.PartySize <= Remaining)
		{
			OutSelected.Add(Index);
			OutBypassed.Append(Passed); // Only tickets actually passed over by a selection count as bypassed.
			Passed.Reset();
			Remaining -= T.PartySize;
			if (!bMany || Remaining <= 0)
			{
				break;
			}
			continue;
		}
		if (Policy != EDocQueueOrderingPolicy::FirstFitWithBypassLimit)
		{
			if (OutSelected.IsEmpty())
			{
				OutBlockedReason = ReasonHeadDoesNotFit;
			}
			break; // Strict head of line.
		}
		const bool bProtected = T.BypassCount >= MaxBypass || (MaxAge > 0.0 && Now - T.JoinTimestamp >= MaxAge);
		if (bProtected)
		{
			if (OutSelected.IsEmpty())
			{
				OutBlockedReason = ReasonBypassLimit;
			}
			break;
		}
		Passed.Add(Index);
	}

	if (OutSelected.IsEmpty() && OutBlockedReason.IsNone())
	{
		OutBlockedReason = bSawOversized ? ReasonOversized : ReasonHeadDoesNotFit;
	}
	return !OutSelected.IsEmpty();
}

FDocSystemResult UDocServiceQueueSubsystem::CreateAdmissionOffer(FName QueueId, FName StationId, FDocAdmissionOffer& OutOffer)
{
	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	if (Queue->bClosed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Queue is closed."));
	}
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	if (Station->State != EDocStationOperationalState::Open)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station is not Open."));
	}
	const int32 AvailCapacity = Station->AvailableCapacity();
	if (AvailCapacity <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station has no available capacity."));
	}

	TArray<int32> Selected, Bypassed;
	FName Reason;
	if (!SelectCandidates(*Queue, *Station, AvailCapacity, false, Selected, Bypassed, Reason))
	{
		Queue->HeadBlockedReason = Reason;
		const EDocResultOutcome Outcome = Reason == DocQueuePrivate::ReasonNoEligible ? EDocResultOutcome::NotFound : EDocResultOutcome::Unavailable;
		return FDocSystemResult::MakeFailure(Outcome, FString::Printf(TEXT("No admission: %s"), *Reason.ToString()));
	}
	Queue->HeadBlockedReason = NAME_None;
	for (int32 Index : Bypassed)
	{
		Queue->Tickets[Index].BypassCount++;
	}

	FDocQueueTicket& Ticket = Queue->Tickets[Selected[0]];
	const double Now = GetCurrentTimeSeconds();
	const float TimeoutSec = Queue->Definition ? Queue->Definition->OfferTimeoutSeconds : 10.0f;

	FDocAdmissionOffer Offer;
	Offer.OfferId = FGuid::NewGuid();
	Offer.TicketId = Ticket.TicketId;
	Offer.QueueId = QueueId;
	Offer.StationId = StationId;
	Offer.ReservedSeats = Ticket.PartySize;
	Offer.OfferTimestamp = Now;
	Offer.OfferDeadline = Now + (double)TimeoutSec;

	Ticket.State = EDocQueueTicketState::Offered;
	Ticket.AssignedStationId = StationId;
	Ticket.AssignedOfferId = Offer.OfferId;
	Ticket.OfferDeadline = Offer.OfferDeadline;
	Ticket.StatusReason = NAME_None;
	Ticket.Revision++;

	Station->ReservedSeats += Ticket.PartySize;
	ActiveOffers.Add(Offer.OfferId, Offer);
	OutOffer = Offer;

	OnOfferCreated.Broadcast(QueueId, Offer);
	OnOfferCreatedNative.Broadcast(QueueId, Offer);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::FormBatch(FName QueueId, FName StationId, FDocServiceBatch& OutBatch)
{
	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	if (Queue->bClosed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Queue is closed."));
	}
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	if (Station->State != EDocStationOperationalState::Open)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station is not Open."));
	}
	const int32 AvailCapacity = Station->AvailableCapacity();
	if (AvailCapacity <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station has no available capacity."));
	}

	TArray<int32> Selected, Bypassed;
	FName Reason;
	if (!SelectCandidates(*Queue, *Station, AvailCapacity, true, Selected, Bypassed, Reason))
	{
		Queue->HeadBlockedReason = Reason;
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, FString::Printf(TEXT("No batch: %s"), *Reason.ToString()));
	}

	const double Now = GetCurrentTimeSeconds();
	int32 Seats = 0;
	double OldestJoin = TNumericLimits<double>::Max();
	for (int32 Index : Selected)
	{
		Seats += Queue->Tickets[Index].PartySize;
		OldestJoin = FMath::Min(OldestJoin, Queue->Tickets[Index].JoinTimestamp);
	}
	const int32 MinBatch = Queue->Definition ? FMath::Max(1, Queue->Definition->MinBatchSize) : 1;
	const double BatchWait = Queue->Definition ? (double)Queue->Definition->BatchWaitSeconds : 0.0;
	const bool bFull = Seats >= AvailCapacity;
	const bool bMinimum = Seats >= MinBatch;
	const bool bWaitExpired = BatchWait > 0.0 && Now - OldestJoin >= BatchWait;
	if (!bFull && !bMinimum && !bWaitExpired)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Batch below minimum and wait not expired."));
	}

	// One commit: every selected ticket gets its own reservation, or none does.
	Queue->HeadBlockedReason = NAME_None;
	for (int32 Index : Bypassed)
	{
		Queue->Tickets[Index].BypassCount++;
	}
	const float ArrivalTimeout = Queue->Definition ? Queue->Definition->ArrivalTimeoutSeconds : 15.0f;
	FDocServiceBatch Batch;
	Batch.BatchId = FGuid::NewGuid();
	Batch.StationId = StationId;
	Batch.FormedTimestamp = Now;
	for (int32 Index : Selected)
	{
		FDocQueueTicket& Ticket = Queue->Tickets[Index];
		FDocStationReservation Reservation;
		Reservation.ReservationId = FGuid::NewGuid();
		Reservation.TicketId = Ticket.TicketId;
		Reservation.StationId = StationId;
		Reservation.ReservedSeats = Ticket.PartySize;
		Reservation.ExpirationTimestamp = Now + (double)ArrivalTimeout;
		ActiveReservations.Add(Reservation.ReservationId, Reservation);

		Ticket.State = EDocQueueTicketState::Reserved;
		Ticket.AssignedStationId = StationId;
		Ticket.ReservationId = Reservation.ReservationId;
		Ticket.BatchId = Batch.BatchId;
		Ticket.StatusReason = NAME_None;
		Ticket.Revision++;

		Batch.AdmittedTicketIds.Add(Ticket.TicketId);
		Batch.TotalSeats += Ticket.PartySize;
	}
	Station->ReservedSeats += Batch.TotalSeats;
	OutBatch = Batch;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::AcceptOffer(const FGuid& OfferId, FDocStationReservation& OutReservation)
{
	FDocAdmissionOffer* OfferPtr = ActiveOffers.Find(OfferId);
	if (!OfferPtr)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Offer not found or already processed."));
	}
	const FDocAdmissionOffer Offer = *OfferPtr;
	const double Now = GetCurrentTimeSeconds();
	FDocQueueTicket* Ticket = FindTicket(Offer.TicketId);
	const bool bTicketHoldsOffer = Ticket && Ticket->State == EDocQueueTicketState::Offered && Ticket->AssignedOfferId == OfferId;

	if (Now >= Offer.OfferDeadline)
	{
		if (bTicketHoldsOffer)
		{
			ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Expired, DocQueuePrivate::ReasonOfferExpired);
		}
		else
		{
			ActiveOffers.Remove(OfferId);
			if (FDocStationInfo* Station = Stations.Find(Offer.StationId))
			{
				Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Offer.ReservedSeats);
			}
		}
		OnOfferExpired.Broadcast(Offer.QueueId, Offer);
		OnOfferExpiredNative.Broadcast(Offer.QueueId, Offer);
		return FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Admission offer has expired."));
	}
	if (!bTicketHoldsOffer)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket no longer holds this offer."));
	}

	const FQueueRuntimeData* Queue = Queues.Find(Offer.QueueId);
	const float ArrivalTimeout = (Queue && Queue->Definition) ? Queue->Definition->ArrivalTimeoutSeconds : 15.0f;

	// The offer's held seats become the reservation's; station totals do not change.
	FDocStationReservation Reservation;
	Reservation.ReservationId = FGuid::NewGuid();
	Reservation.TicketId = Ticket->TicketId;
	Reservation.StationId = Offer.StationId;
	Reservation.ReservedSeats = Offer.ReservedSeats;
	Reservation.ExpirationTimestamp = Now + (double)ArrivalTimeout;
	ActiveReservations.Add(Reservation.ReservationId, Reservation);
	ActiveOffers.Remove(OfferId);

	Ticket->State = EDocQueueTicketState::Reserved;
	Ticket->ReservationId = Reservation.ReservationId;
	Ticket->AssignedOfferId.Invalidate();
	Ticket->Revision++;
	OutReservation = Reservation;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::DeclineOffer(const FGuid& OfferId)
{
	const FDocAdmissionOffer* OfferPtr = ActiveOffers.Find(OfferId);
	if (!OfferPtr)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Offer not found."));
	}
	const FDocAdmissionOffer Offer = *OfferPtr;
	FDocQueueTicket* Ticket = FindTicket(Offer.TicketId);
	if (Ticket && Ticket->State == EDocQueueTicketState::Offered && Ticket->AssignedOfferId == OfferId)
	{
		ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Waiting, DocQueuePrivate::ReasonDeclined);
	}
	else
	{
		ActiveOffers.Remove(OfferId);
		if (FDocStationInfo* Station = Stations.Find(Offer.StationId))
		{
			Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Offer.ReservedSeats);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::ConfirmArrival(const FGuid& TicketId)
{
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
	}
	if (Ticket->State == EDocQueueTicketState::Ready)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already arrived."));
	}
	if (Ticket->State != EDocQueueTicketState::Reserved && Ticket->State != EDocQueueTicketState::Arriving)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket is not in Reserved/Arriving state."));
	}
	const FDocStationReservation* Reservation = ActiveReservations.Find(Ticket->ReservationId);
	if (!Reservation || GetCurrentTimeSeconds() >= Reservation->ExpirationTimestamp)
	{
		// A late arrival cannot resurrect an expired reservation.
		ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Expired, DocQueuePrivate::ReasonNoShow);
		return FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Reservation expired before arrival."));
	}

	Ticket->State = EDocQueueTicketState::Ready;
	Ticket->Revision++;
	const FName StationId = Ticket->AssignedStationId;
	OnArrivalConfirmed.Broadcast(StationId, TicketId);
	OnArrivalConfirmedNative.Broadcast(StationId, TicketId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::StartService(FName StationId, const FGuid& TicketId)
{
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	if (Station->State == EDocStationOperationalState::Closed || Station->State == EDocStationOperationalState::Blocked)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station is closed or blocked."));
	}
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
	}
	if (Ticket->AssignedStationId != StationId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket assigned to different station."));
	}
	if (Ticket->State != EDocQueueTicketState::Ready && Ticket->State != EDocQueueTicketState::Reserved)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket is not Ready or Reserved to begin service."));
	}

	// Service start consumes exactly this ticket's reservation.
	FDocStationReservation Reservation;
	if (!ActiveReservations.RemoveAndCopyValue(Ticket->ReservationId, Reservation))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket has no live reservation."));
	}
	Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Reservation.ReservedSeats);
	Station->InServiceSeats += Ticket->PartySize;

	Ticket->State = EDocQueueTicketState::InService;
	Ticket->ReservationId.Invalidate();
	Ticket->Revision++;

	OnServiceStarted.Broadcast(StationId, TicketId);
	OnServiceStartedNative.Broadcast(StationId, TicketId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::CompleteService(FName StationId, const FGuid& TicketId)
{
	FDocStationInfo* Station = Stations.Find(StationId);
	if (!Station)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Station not found."));
	}
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
	}
	if (Ticket->AssignedStationId == StationId && Ticket->State == EDocQueueTicketState::Completed)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Service already completed."));
	}
	if (Ticket->AssignedStationId != StationId || Ticket->State != EDocQueueTicketState::InService)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket is not in service at this station."));
	}

	Station->InServiceSeats = FMath::Max(0, Station->InServiceSeats - Ticket->PartySize);
	Ticket->State = EDocQueueTicketState::Completed;
	Ticket->Revision++;

	OnServiceCompleted.Broadcast(StationId, TicketId);
	OnServiceCompletedNative.Broadcast(StationId, TicketId);
	return FDocSystemResult::MakeSuccess();
}

int32 UDocServiceQueueSubsystem::CheckExpirations(double CurrentTimeOverride)
{
	const double Now = (CurrentTimeOverride >= 0.0) ? CurrentTimeOverride : GetCurrentTimeSeconds();
	int32 ExpiredCount = 0;
	TArray<FDocAdmissionOffer> ExpiredOffers;

	TArray<FGuid> OfferIds;
	ActiveOffers.GetKeys(OfferIds);
	for (const FGuid& OfferId : OfferIds)
	{
		const FDocAdmissionOffer Offer = ActiveOffers.FindChecked(OfferId);
		if (Now < Offer.OfferDeadline)
		{
			continue;
		}
		FDocQueueTicket* Ticket = FindTicket(Offer.TicketId);
		if (Ticket && Ticket->State == EDocQueueTicketState::Offered && Ticket->AssignedOfferId == OfferId)
		{
			ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Expired, DocQueuePrivate::ReasonOfferExpired);
		}
		else
		{
			ActiveOffers.Remove(OfferId);
			if (FDocStationInfo* Station = Stations.Find(Offer.StationId))
			{
				Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Offer.ReservedSeats);
			}
		}
		ExpiredOffers.Add(Offer);
		++ExpiredCount;
	}

	// Arrival deadlines: a no-show releases its own reservation.
	TArray<FGuid> ReservationIds;
	ActiveReservations.GetKeys(ReservationIds);
	for (const FGuid& ReservationId : ReservationIds)
	{
		const FDocStationReservation Reservation = ActiveReservations.FindChecked(ReservationId);
		if (Now < Reservation.ExpirationTimestamp)
		{
			continue;
		}
		FDocQueueTicket* Ticket = FindTicket(Reservation.TicketId);
		if (Ticket && DocQueuePrivate::HoldsReservation(Ticket->State) && Ticket->ReservationId == ReservationId)
		{
			ReleaseTicketHolds(*Ticket, EDocQueueTicketState::Expired, DocQueuePrivate::ReasonNoShow);
		}
		else
		{
			ActiveReservations.Remove(ReservationId);
			if (FDocStationInfo* Station = Stations.Find(Reservation.StationId))
			{
				Station->ReservedSeats = FMath::Max(0, Station->ReservedSeats - Reservation.ReservedSeats);
			}
		}
		++ExpiredCount;
	}

	for (const FDocAdmissionOffer& Offer : ExpiredOffers)
	{
		OnOfferExpired.Broadcast(Offer.QueueId, Offer);
		OnOfferExpiredNative.Broadcast(Offer.QueueId, Offer);
	}
	return ExpiredCount;
}

FDocSystemResult UDocServiceQueueSubsystem::ReconcileTicket(const FGuid& TicketId, EDocQueueTicketState Resolution, FName StationId)
{
	FDocQueueTicket* Ticket = FindTicket(TicketId);
	if (!Ticket)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Ticket not found."));
	}
	if (Ticket->State != EDocQueueTicketState::ReconciliationRequired)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Ticket does not need reconciliation."));
	}

	switch (Resolution)
	{
	case EDocQueueTicketState::InService:
	{
		FDocStationInfo* Station = Stations.Find(StationId);
		if (!Station || Station->State == EDocStationOperationalState::Closed || Station->State == EDocStationOperationalState::Blocked)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Station unavailable for reconciliation."));
		}
		if (Station->AvailableCapacity() < Ticket->PartySize)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Station has no room for the reconciled party."));
		}
		Station->InServiceSeats += Ticket->PartySize;
		Ticket->AssignedStationId = StationId;
		break;
	}
	case EDocQueueTicketState::Completed:
	case EDocQueueTicketState::Cancelled:
		break;
	case EDocQueueTicketState::Waiting:
		Ticket->AssignedStationId = NAME_None;
		break;
	default:
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported reconciliation outcome."));
	}
	Ticket->State = Resolution;
	Ticket->StatusReason = NAME_None;
	Ticket->Revision++;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::CaptureQueue(FName QueueId, FDocQueueSnapshot& OutSnapshot) const
{
	const FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	OutSnapshot = FDocQueueSnapshot();
	OutSnapshot.SchemaVersion = 2;
	OutSnapshot.Tickets = Queue->Tickets;
	OutSnapshot.NextOrdinal = Queue->NextOrdinal;
	OutSnapshot.bClosed = Queue->bClosed;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocServiceQueueSubsystem::StageRestore(FName QueueId, const FDocQueueSnapshot& Snapshot)
{
	using namespace DocQueuePrivate;

	FQueueRuntimeData* Queue = Queues.Find(QueueId);
	if (!Queue)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Queue not found."));
	}
	if (Snapshot.SchemaVersion < 1 || Snapshot.SchemaVersion > 2)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported queue snapshot schema."));
	}

	// Current tickets are replaced: release every hold they have so no seat is orphaned.
	for (FDocQueueTicket& Ticket : Queue->Tickets)
	{
		if (!IsTerminal(Ticket.State))
		{
			ReleaseTicketHolds(Ticket, EDocQueueTicketState::Cancelled, ReasonReplacedByRestore);
		}
	}

	TArray<FDocQueueTicket> Restored;
	TSet<FGuid> Seen;
	int64 MaxOrdinal = 0;
	for (const FDocQueueTicket& Saved : Snapshot.Tickets)
	{
		bool bDuplicate = false;
		Seen.Add(Saved.TicketId, &bDuplicate);
		if (!Saved.TicketId.IsValid() || bDuplicate || Saved.PartySize < 1)
		{
			continue;
		}
		FDocQueueTicket Ticket = Saved;
		Ticket.QueueId = QueueId;
		Ticket.AssignedOfferId.Invalidate();
		Ticket.ReservationId.Invalidate();
		Ticket.OfferDeadline = 0.0;
		if (Ticket.State == EDocQueueTicketState::Offered || HoldsReservation(Ticket.State))
		{
			// Leases are transient: the ticket waits for a new offer instead of assuming its seat is still held.
			Ticket.State = EDocQueueTicketState::Waiting;
			Ticket.AssignedStationId = NAME_None;
			Ticket.BatchId.Invalidate();
			Ticket.StatusReason = ReasonRestoredNewLease;
		}
		else if (Ticket.State == EDocQueueTicketState::InService)
		{
			// Never quietly re-admit or re-charge: the service provider must reconcile.
			Ticket.State = EDocQueueTicketState::ReconciliationRequired;
			Ticket.StatusReason = ReasonRestoredInService;
		}
		Ticket.Revision++;
		MaxOrdinal = FMath::Max(MaxOrdinal, Ticket.JoinOrdinal);
		Restored.Add(Ticket);
	}

	Queue->Tickets = MoveTemp(Restored);
	Queue->NextOrdinal = FMath::Max(Snapshot.NextOrdinal, MaxOrdinal + 1);
	Queue->bClosed = Snapshot.SchemaVersion >= 2 ? Snapshot.bClosed : false;
	Queue->HeadBlockedReason = NAME_None;
	return FDocSystemResult::MakeSuccess();
}
