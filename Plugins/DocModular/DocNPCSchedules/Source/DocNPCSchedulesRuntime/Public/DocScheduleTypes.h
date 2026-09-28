#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DocRequestHandle.h"
#include "DocSharedTypes.h"
#include "DocSystemResult.h"
#include "DocScheduleTypes.generated.h"

namespace DocScheduleTags
{
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity);
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity_Idle);
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity_Sleep);
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity_Work);
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity_Eat);
	DOCNPCSCHEDULESRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity_Flee);
}

/** Entry/override class; enum order is the override class rank (higher wins). */
UENUM(BlueprintType)
enum class EDocScheduleSource : uint8
{
	Daily,
	Weekly,
	Seasonal,
	SpecialEvent,
	TemporaryOverride,
	EmergencyOverride,
	ManualOverride
};

/** Executor-reported execution state (handoff 8.1). */
UENUM(BlueprintType)
enum class EDocActivityStatus : uint8
{
	None,
	Requested,
	Accepted,
	Travelling,
	Started,
	Completed,
	Failed,
	Cancelled,
	Unavailable
};

UENUM(BlueprintType)
enum class EDocOverridePreemption : uint8
{
	/** Cancel the current request even if it is running. */
	Preempt,
	/** If the running activity is not interruptible, dispatch after it ends. */
	WaitForInterruptible,
	/** Refuse while a non-interruptible activity runs. */
	RejectIfBusy
};

UENUM(BlueprintType)
enum class EDocOverrideResume : uint8
{
	/** Default: recompute the schedule at the current time when the override ends. */
	RecomputeCurrent,
	/** Resume the interrupted entry only if it is still active and its target still exists; otherwise recompute. */
	ResumeInterrupted
};

UENUM(BlueprintType)
enum class EDocLocationConfidence : uint8
{
	Unknown,
	/** Only a region id is known. */
	RegionOnly,
	/** Authored target location of the desired activity (not a confirmed arrival). */
	ScheduledTarget,
	/** Last location reported by a bound representation. */
	Observed
};

/** Calendar of a schedule clock (never assumes 24h days or 7-day weeks). */
USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocScheduleCalendar
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar", meta = (ClampMin = "1")) double DayLengthSeconds = 86400.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar", meta = (ClampMin = "1")) int32 DaysPerWeek = 7;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar", meta = (ClampMin = "1")) int32 DaysPerSeason = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar", meta = (ClampMin = "1")) int32 SeasonsPerYear = 4;
};

/** Logical activity target: ids and provider-owned locations, never actor pointers. */
USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocScheduleTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target") FName RegionId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target") bool bHasLocation = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (EditCondition = "bHasLocation")) FVector Location = FVector::ZeroVector;
	/** Smart Object search tag (the SmartObjects bridge owns claims). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target") FGameplayTag SmartObjectTag;

	friend bool operator==(const FDocScheduleTarget& A, const FDocScheduleTarget& B)
	{
		return A.RegionId == B.RegionId && A.bHasLocation == B.bHasLocation && (!A.bHasLocation || A.Location.Equals(B.Location)) && A.SmartObjectTag == B.SmartObjectTag;
	}
};

/** Activity behaviour defaults (interruptibility, retries, fallback). */
UCLASS(BlueprintType)
class DOCNPCSCHEDULESRUNTIME_API UDocNPCActivityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (Categories = "Activity")) FGameplayTag ActivityTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity") bool bInterruptible = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0")) int32 MaxRetries = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0")) float RetryBackoffSeconds = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (Categories = "Activity")) FGameplayTag FallbackActivity;
	/** Travelling longer than this (schedule clock seconds, 0 = none) triggers reselection, not a false arrival. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activity", meta = (ClampMin = "0")) float ArrivalDeadlineSeconds = 0.f;
};

/** One schedule entry (handoff 8.2). Times are seconds of day, half-open [Start, End). */
USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocNPCScheduleEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") FName EntryId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") EDocScheduleSource Source = EDocScheduleSource::Daily;
	/** Seconds of day. Start > End spans midnight into the next day. Start == End is empty unless bAllDay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0")) double StartSeconds = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0")) double EndSeconds = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") bool bAllDay = false;
	/** Day-of-week indices (0-based, calendar week length). Empty = every day. The day a span starts on is the one filtered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") TArray<int32> DaysOfWeek;
	/** Season indices. Empty = all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") TArray<int32> Seasons;
	/** SpecialEvent: absolute day index (-1 = not day-specific). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") int64 SpecialDay = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (Categories = "Activity")) FGameplayTag ActivityTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") TObjectPtr<UDocNPCActivityDefinition> Activity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") FDocScheduleTarget Target;
	/** Explicit priority (defaults in settings: Normal 10, Weather 20, WorldState 30, Alert 40, Quest 60, Scripted 80, Cinematic 100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") int32 Priority = 10;
	/** Evaluated against context tags from a condition provider. Unavailable data → FallbackActivity (never a guess). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry") FGameplayTagQuery Conditions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (Categories = "Activity")) FGameplayTag FallbackActivity;
	/** Arrival tolerance (cm) for executors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (Units = "cm")) float ArrivalTolerance = 150.f;
};

UCLASS(BlueprintType)
class DOCNPCSCHEDULESRUNTIME_API UDocNPCScheduleDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") FName ScheduleId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") int32 Version = 1;
	/** Authored order is the final stable tie-break. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") TArray<FDocNPCScheduleEntry> Entries;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (Categories = "Activity")) FGameplayTag DefaultActivity;

	TArray<FString> FindProblems() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Activity request sent to the executor (handoff 8.3). */
USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocNPCActivityRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FName NPCId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int64 RequestId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FGameplayTag ActivityTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FDocScheduleTarget Target;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") float ArrivalTolerance = 150.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int32 Priority = 0;
	/** Absolute schedule-clock seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") double StartTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") double ExpiryTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bInterruptible = true;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bIsFallback = false;
	/** Re-issued after restore/rebind: executors must not grant "start" rewards again. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bRestored = false;
};

/** Desired vs requested vs observed (handoff 8.1) plus debugging data. */
USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocNPCScheduleState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FName NPCId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FGameplayTag DesiredActivity;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FName DesiredEntryId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") EDocScheduleSource DesiredSource = EDocScheduleSource::Daily;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bDesiredFromOverride = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FDocScheduleTarget DesiredTarget;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FGameplayTag RequestedActivity;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int64 RequestId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") EDocActivityStatus Status = EDocActivityStatus::None;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FString StatusReason;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int32 RetryCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int32 OverrideCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") double LastEvaluated = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") double NextEvaluation = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bRepresentationBound = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") int32 Epoch = 0;
};

USTRUCT(BlueprintType)
struct DOCNPCSCHEDULESRUNTIME_API FDocExpectedLocation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") EDocLocationConfidence Confidence = EDocLocationConfidence::Unknown;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") FName RegionId;
	/** Expected is not "safe to spawn": the representation provider validates placement. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Schedule") bool bPlacementValidated = false;
};

USTRUCT()
struct DOCNPCSCHEDULESRUNTIME_API FDocScheduleOverrideRecord
{
	GENERATED_BODY()

	UPROPERTY() int64 OverrideId = 0;
	UPROPERTY() EDocScheduleSource Source = EDocScheduleSource::TemporaryOverride;
	UPROPERTY() int32 Priority = 0;
	UPROPERTY() FGameplayTag ActivityTag;
	UPROPERTY() FDocScheduleTarget Target;
	/** Absolute schedule-clock seconds; 0 = no expiry. */
	UPROPERTY() double Expiry = 0.0;
	UPROPERTY() EDocOverrideResume Resume = EDocOverrideResume::RecomputeCurrent;
	UPROPERTY() EDocOverridePreemption Preemption = EDocOverridePreemption::Preempt;
	/** Durable overrides are saved; transient ones expire with their owner. */
	UPROPERTY() bool bDurable = false;
	UPROPERTY() int64 PushOrder = 0;
};

USTRUCT()
struct DOCNPCSCHEDULESRUNTIME_API FDocNPCSaveRecord
{
	GENERATED_BODY()

	UPROPERTY() FName NPCId;
	UPROPERTY() FName ScheduleId;
	UPROPERTY() int32 ScheduleVersion = 0;
	UPROPERTY() FGameplayTag DesiredActivity;
	UPROPERTY() FName DesiredEntryId;
	UPROPERTY() FDocExpectedLocation Expected;
	UPROPERTY() double LastEvaluated = 0.0;
	UPROPERTY() TArray<FDocScheduleOverrideRecord> DurableOverrides;
};

USTRUCT()
struct DOCNPCSCHEDULESRUNTIME_API FDocScheduleSaveData
{
	GENERATED_BODY()

	UPROPERTY() int32 Version = 1;
	UPROPERTY() FName ClockId;
	UPROPERTY() double ClockTime = 0.0;
	UPROPERTY() TArray<FDocNPCSaveRecord> Records;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDocActivityRequestNative, const FDocNPCActivityRequest&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocActivityCancelNative, FName /*NPCId*/, int64 /*RequestId*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocActivityRequestEvent, const FDocNPCActivityRequest&, Request);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocActivityCancelEvent, int64, RequestId);

/** Project Settings → Plugins → Doc NPC Schedules. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc NPC Schedules"))
class DOCNPCSCHEDULESRUNTIME_API UDocNPCSchedulesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Editable priority defaults (examples from the original; not global laws). */
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Normal = 10;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Weather = 20;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 WorldState = 30;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Alert = 40;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Quest = 60;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Scripted = 80;
	UPROPERTY(Config, EditAnywhere, Category = "Priorities") int32 Cinematic = 100;

	/** Records evaluated per tick (boundary-indexed; unloaded NPCs never tick per second). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1")) int32 MaxEvaluationsPerTick = 128;
	/** Retry backoff when no activity definition is given. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) float DefaultRetryBackoffSeconds = 5.f;
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) int32 DefaultMaxRetries = 2;
};
