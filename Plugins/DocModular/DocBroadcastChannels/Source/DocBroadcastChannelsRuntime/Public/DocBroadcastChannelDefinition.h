#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocSystemResult.h"
#include "DocBroadcastTypes.h"
#include "DocBroadcastProgramDefinition.h"
#include "DocBroadcastChannelDefinition.generated.h"

/**
 * A logical channel: a schedule of non-overlapping half-open program slots, optionally looping,
 * followed by an optional trailing gap.
 */
UCLASS(BlueprintType)
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastChannelDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FName ChannelId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FText DisplayName;

	/** Bump when the schedule changes so saved anchors can be migrated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	int32 ScheduleVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	TArray<FDocBroadcastScheduleEntry> Schedule;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	bool bIsLooping = true;

	/** Silence after the last slot before the loop restarts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	double GapDurationSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	EDocBroadcastSourceFailurePolicy SourceFailurePolicy = EDocBroadcastSourceFailurePolicy::SilenceGap;

	/** Extra open attempts for RetryBounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxOpenRetries = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	EDocBroadcastClockPolicy ClockPolicy = EDocBroadcastClockPolicy::FollowRealTransport;

	/** Validates ids, slot ordering/overlap, lengths against programs, and totals. */
	FDocSystemResult ValidateChannel() const;

	/** Last slot end plus trailing gap. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	double GetTotalDuration() const;

	/**
	 * Resolves an absolute schedule offset. Returns false if the offset is not finite, out of range for a
	 * non-looping schedule, or the schedule is empty. A gap returns true with bHasProgram = false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool ResolveSchedule(double ScheduleOffset, FDocBroadcastResolvedSlot& OutSlot) const;

	const UDocBroadcastProgramDefinition* GetEntryProgram(int32 EntryIndex) const;
};
