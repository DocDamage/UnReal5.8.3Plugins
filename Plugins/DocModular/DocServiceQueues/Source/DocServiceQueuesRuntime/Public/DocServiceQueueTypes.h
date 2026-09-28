#pragma once

#include "CoreMinimal.h"
#include "DocOwnerScope.h"
#include "DocSystemResult.h"
#include "GameplayTagContainer.h"
#include "DocServiceQueueTypes.generated.h"

/** Lifecycle admission state for a ticket in a service queue. */
UENUM(BlueprintType)
enum class EDocQueueTicketState : uint8
{
	Waiting,
	Offered,
	Reserved,
	Arriving,
	Ready,
	InService,
	Completed,
	Cancelled,
	Expired,
	Rejected,
	/** Restored while InService: needs the service provider to confirm before any seat is charged again. */
	ReconciliationRequired
};

/** Ordering strategy used by a service queue. */
UENUM(BlueprintType)
enum class EDocQueueOrderingPolicy : uint8
{
	StrictFIFO,
	PriorityThenFIFO,
	FirstFitWithBypassLimit
};

/** Operational state of a service station. */
UENUM(BlueprintType)
enum class EDocStationOperationalState : uint8
{
	Open,
	Closing,
	Closed,
	/** Emergency abort with occupants not yet confirmed evacuated: seats stay occupied, no admissions. */
	Blocked
};

/** Policy applied to existing tickets when a station is closed. */
UENUM(BlueprintType)
enum class EDocStationClosePolicy : uint8
{
	DrainCurrentService,
	CancelPendingReservations,
	EmergencyAbort
};

/** Active participant or group ticket in a service queue. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocQueueTicket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid TicketId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName QueueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid ParticipantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue", meta = (ClampMin = "1"))
	int32 PartySize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int64 JoinOrdinal = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 PriorityClass = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	EDocQueueTicketState State = EDocQueueTicketState::Waiting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double JoinTimestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double OfferDeadline = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName AssignedStationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid AssignedOfferId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 BypassCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 Revision = 1;

	/** Idempotency key for JoinQueue (defaults to the participant id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName JoinKey = NAME_None;

	/** Priority the participant asked for; PriorityClass is what the authority granted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 RequestedPriority = 0;

	/** The opaque lease holding this ticket's seats while Reserved/Arriving/Ready. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid ReservationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid BatchId;

	/** Why the ticket is waiting, blocked or ended (diagnostics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StatusReason = NAME_None;
};

/** Time-bounded offer for admission to a station. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocAdmissionOffer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid OfferId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid TicketId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName QueueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 ReservedSeats = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double OfferTimestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double OfferDeadline = 0.0;
};

/** Committed station reservation holding seats for an arriving or ready ticket. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocStationReservation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid ReservationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid TicketId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 ReservedSeats = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double ExpirationTimestamp = 0.0;
};

/** Batch of tickets assembled together for concurrent service. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocServiceBatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid BatchId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	TArray<FGuid> AdmittedTicketIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 TotalSeats = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	double FormedTimestamp = 0.0;
};

/** Summary information about an operational service station. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocStationInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue", meta = (ClampMin = "1"))
	int32 Capacity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	EDocStationOperationalState State = EDocStationOperationalState::Open;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGameplayTagContainer ServiceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 ReservedSeats = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 InServiceSeats = 0;

	int32 AvailableCapacity() const
	{
		return FMath::Max(0, Capacity - ReservedSeats - InServiceSeats);
	}
};

/** Inspection view for an active service queue. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocQueueView
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName QueueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 TotalWaitingTickets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 TotalWaitingSeats = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid HeadTicketId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	bool bClosed = false;

	/** Set when the last admission attempt could not serve the head, e.g. OversizedForStation or BypassLimitReached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName HeadBlockedReason = NAME_None;
};

/** Detached snapshot for persistence. */
USTRUCT(BlueprintType)
struct DOCSERVICEQUEUESRUNTIME_API FDocQueueSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int32 SchemaVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	TArray<FDocQueueTicket> Tickets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	bool bClosed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	int64 NextOrdinal = 1;
};
