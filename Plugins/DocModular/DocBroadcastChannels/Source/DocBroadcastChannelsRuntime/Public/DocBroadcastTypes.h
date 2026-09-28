#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocSharedTypes.h"
#include "DocBroadcastTypes.generated.h"

class UDocBroadcastProgramDefinition;

/** Distinguishable channel transport states (spec 14.4). */
UENUM(BlueprintType)
enum class EDocBroadcastTransportState : uint8
{
	NoProgram,
	Loading,
	Playing,
	Paused,
	Interrupted,
	UnavailableSource,
	Faulted
};

/** How the underlying schedule behaves while an interruption plays. */
UENUM(BlueprintType)
enum class EDocBroadcastInterruptionPolicy : uint8
{
	/** The underlying program cursor is held and resumes where it stopped. */
	PauseUnderlyingCursor,
	/** The underlying schedule keeps advancing; playback returns to wherever it now is. */
	ContinueUnderlyingSchedule
};

/** What a new interruption does when others are already present. */
UENUM(BlueprintType)
enum class EDocBroadcastArbitration : uint8
{
	/** Wait behind higher/equal priority requests (FIFO within a priority). */
	Queue,
	/** End every existing request of lower or equal priority, then play. */
	Replace,
	/** Refuse if any request of equal or higher priority is present. */
	Reject
};

UENUM(BlueprintType)
enum class EDocBroadcastInterruptionStatus : uint8
{
	Queued,
	Active,
	Completed,
	Released,
	Expired,
	Replaced,
	Rejected,
	SourceFailed,
	Cancelled
};

/** What to do when a program source fails to open. */
UENUM(BlueprintType)
enum class EDocBroadcastSourceFailurePolicy : uint8
{
	/** Jump the channel to the next schedule entry. */
	Skip,
	/** Stay silent (UnavailableSource) until the failed entry's slot ends; the schedule keeps its time. */
	SilenceGap,
	/** Retry up to MaxOpenRetries, then behave like SilenceGap. */
	RetryBounded
};

/** How an accelerated fictional clock maps onto normal-speed program playback (spec 14.3). */
UENUM(BlueprintType)
enum class EDocBroadcastClockPolicy : uint8
{
	/** Programs play in real transport time; the fictional clock scale is ignored. */
	FollowRealTransport,
	/** Programs play in real time, but at each program boundary the channel jumps to the entry the fictional clock indicates. */
	ReanchorAtScheduleBoundary
};

USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastScheduleEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FGuid EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	TObjectPtr<UDocBroadcastProgramDefinition> Program = nullptr;

	/** Start of the half-open slot [Start, Start + Duration) within one schedule loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double StartOffsetSeconds = 0.0;

	/** Slot length. Must be > 0 and no longer than the program. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double DurationSeconds = 60.0;
};

/** Result of resolving a schedule offset. */
USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastResolvedSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bHasProgram = false;

	/** Index into the channel schedule, or INDEX_NONE during a gap. */
	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 EntryIndex = INDEX_NONE;

	/** Which repetition of a looping schedule this offset falls in. */
	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int64 LoopIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double ProgramCursorSeconds = 0.0;

	/** Absolute schedule offsets of the slot containing this offset (gap slots included). */
	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double SlotStartOffset = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double SlotEndOffset = 0.0;
};

USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastInterruption
{
	GENERATED_BODY()

	/** Assigned by the subsystem when left invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FGuid InterruptionId;

	/** Owner handle; only the owner may release. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FGuid OwnerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	int32 Priority = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	EDocBroadcastArbitration Arbitration = EDocBroadcastArbitration::Queue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	EDocBroadcastInterruptionPolicy ResumePolicy = EDocBroadcastInterruptionPolicy::ContinueUnderlyingSchedule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	TObjectPtr<UDocBroadcastProgramDefinition> Program = nullptr;

	/** Seconds to play; <= 0 means the whole program. Clamped to the program duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double DurationSeconds = 0.0;

	/** Channel clock time after which a request that has not started is discarded. 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double ExpiryTimeSeconds = 0.0;

	// Runtime (assigned by the subsystem)

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int64 RequestOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	EDocBroadcastInterruptionStatus Status = EDocBroadcastInterruptionStatus::Queued;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double PlayedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bHasStarted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	FString TerminalReason;
};

/** Capture/restore record and live transport view. Interruptions are transient leases and are not persisted. */
USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastTransport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FName ChannelId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	int32 ScheduleVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double AnchorEpochSeconds = 0.0;

	/** Channel transport clock (real playback seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double ClockSeconds = 0.0;

	/** Accumulated held time (authorized pause or pause-underlying interruptions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double HeldSeconds = 0.0;

	/** Fictional clock elapsed, used by ReanchorAtScheduleBoundary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double FictionalSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	float ClockScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	bool bPaused = false;

	// Read-only view fields (ignored on restore)

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	EDocBroadcastTransportState State = EDocBroadcastTransportState::NoProgram;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	FName CurrentProgramId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double ProgramCursorSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double ScheduleOffsetSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double TotalScheduleDuration = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 ActiveInterruptionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bInterrupted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 TransportGeneration = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 ScheduleRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 SourceFailureCount = 0;
};

USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastReceiverState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FGuid ReceiverId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FName TunedChannelId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	float Volume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	bool bIsMuted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FName ObservedProgramId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	double ObservedCursorSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	EDocBroadcastTransportState ObservedState = EDocBroadcastTransportState::NoProgram;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	bool bIsReceiverEnabled = false;
};

/** What a playback backend can do for a program (spec 14.5). */
USTRUCT(BlueprintType)
struct DOCBROADCASTCHANNELSRUNTIME_API FDocBroadcastProviderCapabilities
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bCanSeek = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bKnownDuration = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bSupportsAudio = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bSupportsVideo = false;

	/** True only when the backend produces real, measurable sound. */
	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	bool bAudible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	double SeekPrecisionSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
	int32 MaxConcurrentStreams = 0;
};
