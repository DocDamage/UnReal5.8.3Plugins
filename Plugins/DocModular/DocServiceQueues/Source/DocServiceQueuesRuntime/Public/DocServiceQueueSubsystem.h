#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocServiceQueueTypes.h"
#include "DocServiceQueueDefinition.h"
#include "DocServiceQueueSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueTicketJoinedSignature, FName, QueueId, const FDocQueueTicket&, Ticket);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueOfferCreatedSignature, FName, QueueId, const FDocAdmissionOffer&, Offer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueOfferExpiredSignature, FName, QueueId, const FDocAdmissionOffer&, Offer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueArrivalConfirmedSignature, FName, StationId, const FGuid&, TicketId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueServiceStartedSignature, FName, StationId, const FGuid&, TicketId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueServiceCompletedSignature, FName, StationId, const FGuid&, TicketId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocQueueStationStateChangedSignature, FName, StationId, EDocStationOperationalState, NewState);

DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueTicketJoinedNative, FName, const FDocQueueTicket&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueOfferCreatedNative, FName, const FDocAdmissionOffer&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueOfferExpiredNative, FName, const FDocAdmissionOffer&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueArrivalConfirmedNative, FName, const FGuid&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueServiceStartedNative, FName, const FGuid&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueServiceCompletedNative, FName, const FGuid&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocQueueStationStateChangedNative, FName, EDocStationOperationalState);

/** World subsystem managing waiting order, service stations, capacity allocation, offers, and admissions. */
UCLASS()
class DOCSERVICEQUEUESRUNTIME_API UDocServiceQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Register an authored service queue definition. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult RegisterQueue(const UDocServiceQueueDefinition* Definition);

	/** Unregister a queue: every ticket is cancelled and every offer, reservation and in-service seat it held is released. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult UnregisterQueue(FName QueueId);

	/** A closed queue accepts no joins and makes no new admissions; existing admissions continue. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult SetQueueClosed(FName QueueId, bool bClosed);

	/** Register an operational service station. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult RegisterStation(FName StationId, int32 Capacity, const FGameplayTagContainer& ServiceTags);

	/** Unregister a station (destruction/unload): its offers and reservations return tickets to Waiting; in-service tickets are cancelled. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult UnregisterStation(FName StationId, EDocStationClosePolicy ClosePolicy = EDocStationClosePolicy::CancelPendingReservations);

	/**
	 * Change station operational state. Closing/Closed with DrainCurrentService keeps admissions; CancelPendingReservations
	 * releases offers and reservations; EmergencyAbort also releases them and, if anyone is in service, leaves the station
	 * Blocked with its seats occupied until ConfirmEvacuated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult SetStationOperationalState(FName StationId, EDocStationOperationalState NewState, EDocStationClosePolicy ClosePolicy = EDocStationClosePolicy::CancelPendingReservations);

	/** Host confirmation that a Blocked station's occupants are out: cancels their tickets and frees the seats. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult ConfirmEvacuated(FName StationId);

	/**
	 * Join with an indivisible party. PriorityClass is a request, clamped to the definition's MaxSelfAssignedPriority.
	 * The same JoinKey (or participant) with the same party returns the existing active ticket; a different party is a Conflict.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult JoinQueue(FName QueueId, const FGuid& ParticipantId, int32 PartySize, int32 PriorityClass, FDocQueueTicket& OutTicket, FName JoinKey = NAME_None);

	/** Authority-granted priority (not participant-supplied). Waiting tickets only. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult SetTicketPriority(const FGuid& TicketId, int32 PriorityClass);

	/** Leave or cancel a ticket, releasing exactly the capacity it holds (offer, reservation or in-service seats). Repeat-safe. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult LeaveQueue(const FGuid& TicketId);

	/** Inspect status of an individual ticket. */
	UFUNCTION(BlueprintPure, Category = "Doc|Queue")
	FDocSystemResult QueryTicket(const FGuid& TicketId, FDocQueueTicket& OutTicket) const;

	/** Inspect summary view of a queue. */
	UFUNCTION(BlueprintPure, Category = "Doc|Queue")
	FDocSystemResult QueryQueueView(FName QueueId, FDocQueueView& OutView) const;

	/** Inspect status and capacity of a station. */
	UFUNCTION(BlueprintPure, Category = "Doc|Queue")
	FDocSystemResult QueryStation(FName StationId, FDocStationInfo& OutInfo) const;

	/** Form and reserve an admission offer for an eligible ticket at a station. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult CreateAdmissionOffer(FName QueueId, FName StationId, FDocAdmissionOffer& OutOffer);

	/** Accept an admission offer, converting temporary hold to a committed reservation. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult AcceptOffer(const FGuid& OfferId, FDocStationReservation& OutReservation);

	/** Decline an admission offer and return the ticket to waiting status. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult DeclineOffer(const FGuid& OfferId);

	/** Confirm arrival of participant at the station. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult ConfirmArrival(const FGuid& TicketId);

	/** Begin service for an arrived/ready ticket, consuming reserved capacity. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult StartService(FName StationId, const FGuid& TicketId);

	/** Complete service, freeing station capacity and finalizing the ticket. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult CompleteService(FName StationId, const FGuid& TicketId);

	/**
	 * Reserve a batch in one commit: waiting tickets in policy order that fit the station. Starts when the station is full,
	 * when the batch reaches MinBatchSize seats, or when the oldest selected ticket has waited BatchWaitSeconds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult FormBatch(FName QueueId, FName StationId, FDocServiceBatch& OutBatch);

	/** Offer and arrival (no-show) deadlines on the queue clock. Expired tickets release exactly their own seats. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	int32 CheckExpirations(double CurrentTimeOverride = -1.0);

	/** Resolve a ReconciliationRequired ticket: InService (charges seats at StationId), Completed, Cancelled or Waiting. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult ReconcileTicket(const FGuid& TicketId, EDocQueueTicketState Resolution, FName StationId);

	/** Replaces the queue clock (world time by default). Intended for tests and replays. */
	void SetClockOverride(TFunction<double()> InClock) { ClockOverride = MoveTemp(InClock); }

	/** Capture detached snapshot for persistence. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult CaptureQueue(FName QueueId, FDocQueueSnapshot& OutSnapshot) const;

	/**
	 * Restore tickets. Holds of the current tickets are released first. Offered/Reserved/Arriving/Ready tickets come back
	 * Waiting (new leases are made by later offers); InService tickets come back ReconciliationRequired with no seat charged.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult StageRestore(FName QueueId, const FDocQueueSnapshot& Snapshot);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueTicketJoinedSignature OnTicketJoined;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueOfferCreatedSignature OnOfferCreated;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueOfferExpiredSignature OnOfferExpired;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueArrivalConfirmedSignature OnArrivalConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueServiceStartedSignature OnServiceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueServiceCompletedSignature OnServiceCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Queue|Events")
	FDocQueueStationStateChangedSignature OnStationStateChanged;

	FDocQueueTicketJoinedNative OnTicketJoinedNative;
	FDocQueueOfferCreatedNative OnOfferCreatedNative;
	FDocQueueOfferExpiredNative OnOfferExpiredNative;
	FDocQueueArrivalConfirmedNative OnArrivalConfirmedNative;
	FDocQueueServiceStartedNative OnServiceStartedNative;
	FDocQueueServiceCompletedNative OnServiceCompletedNative;
	FDocQueueStationStateChangedNative OnStationStateChangedNative;

private:
	double GetCurrentTimeSeconds() const;

	struct FQueueRuntimeData
	{
		TObjectPtr<const UDocServiceQueueDefinition> Definition = nullptr;
		TArray<FDocQueueTicket> Tickets;
		int64 NextOrdinal = 1;
		bool bClosed = false;
		FName HeadBlockedReason = NAME_None;
	};

	FDocQueueTicket* FindTicket(const FGuid& TicketId);
	const FDocQueueTicket* FindTicket(const FGuid& TicketId) const;

	/** Releases whatever capacity the ticket holds and moves it to NewState. */
	void ReleaseTicketHolds(FDocQueueTicket& Ticket, EDocQueueTicketState NewState, FName Reason);

	/** Policy-ordered admission candidates for one station. Returns false with a reason when the head blocks. */
	bool SelectCandidates(FQueueRuntimeData& Queue, const FDocStationInfo& Station, int32 SeatBudget, bool bMany, TArray<int32>& OutSelected, TArray<int32>& OutBypassed, FName& OutBlockedReason) const;

	static bool IsTerminal(EDocQueueTicketState State);

	TFunction<double()> ClockOverride;

	TMap<FName, FQueueRuntimeData> Queues;
	TMap<FName, FDocStationInfo> Stations;
	TMap<FGuid, FDocAdmissionOffer> ActiveOffers;
	TMap<FGuid, FDocStationReservation> ActiveReservations;
};
