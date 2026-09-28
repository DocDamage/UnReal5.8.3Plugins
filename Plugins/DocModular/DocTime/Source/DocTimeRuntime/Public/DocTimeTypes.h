#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocTimeTypes.generated.h"

/**
 * Calendar definition (handoff 9.3). Validated: all units positive, at least one
 * month, month lengths positive.
 */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocCalendar
{
	GENERATED_BODY()

	/** Stable identity recorded in clock snapshots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	FName CalendarId = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar", meta = (ClampMin = "1"))
	int32 SecondsPerMinute = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar", meta = (ClampMin = "1"))
	int32 MinutesPerHour = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar", meta = (ClampMin = "1"))
	int32 HoursPerDay = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar", meta = (ClampMin = "1"))
	int32 DaysPerWeek = 7;

	/** Days in each month; the number of entries is the number of months per year. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	TArray<int32> MonthLengths = { 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	TArray<FText> MonthNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	TArray<FText> WeekdayNames;

	/** Year number of simulated time 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	int32 EpochYear = 1;

	/** Weekday index (0-based) of day 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar", meta = (ClampMin = "0"))
	int32 EpochWeekday = 0;

	/** Latest supported year; time beyond it is rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	int32 MaxYear = 100000;

	bool IsValid(FString* OutError = nullptr) const;
	int64 MsPerMinute() const { return int64(SecondsPerMinute) * 1000; }
	int64 MsPerHour() const { return MsPerMinute() * MinutesPerHour; }
	int64 MsPerDay() const { return MsPerHour() * HoursPerDay; }
	int32 DaysPerYear() const;
	int32 MinutesPerDay() const { return MinutesPerHour * HoursPerDay; }
	int64 MaxSupportedMs() const;
};

/** Derived calendar time (handoff 9.3). All fields 0-based except Year, Month, Day (1-based). */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocCalendarTime
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Time") int64 TotalMilliseconds = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int64 TotalDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Year = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Month = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Day = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 DayOfYear = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 DayOfWeek = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Hour = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Minute = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Second = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 Millisecond = 0;
	/** 0 at midnight, approaching 1 before the next midnight. */
	UPROPERTY(BlueprintReadOnly, Category = "Time") float NormalizedDayTime = 0.f;
	/** Minute index within the day (0 .. MinutesPerDay-1). */
	UPROPERTY(BlueprintReadOnly, Category = "Time") int32 MinuteOfDay = 0;
};

/** Configurable gameplay period (Dawn, Day...). Wraps midnight when End <= Start. */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocTimePeriod
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Period", meta = (Categories = "Time.Period"))
	FGameplayTag PeriodTag;

	/** Inclusive start, minute of day. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Period", meta = (ClampMin = "0"))
	int32 StartMinute = 0;

	/** Exclusive end, minute of day. End <= Start wraps past midnight. Start == End is an empty (invalid) period. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Period", meta = (ClampMin = "0"))
	int32 EndMinute = 0;

	/** Overlap resolution: higher wins, then earlier array entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Period")
	int32 Priority = 0;

	bool ContainsMinute(int32 MinuteOfDay) const;
};

UENUM(BlueprintType)
enum class EDocRecurrence : uint8
{
	/** Fires once at AbsoluteMilliseconds. */
	Once,
	Daily,
	/** On DayOfWeek. */
	Weekly,
	/** On Day of every month (skipped for months shorter than Day). */
	Monthly,
	/** On Month/Day each year. */
	Yearly
};

/** Schedule entry (handoff 9.4). The Time plugin only answers when; consumers act. */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocScheduleEntry
{
	GENERATED_BODY()

	/** Stable ID. Required, unique within a configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule")
	FName EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule")
	EDocRecurrence Recurrence = EDocRecurrence::Daily;

	/** Minute of day for recurring entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0"))
	int32 MinuteOfDay = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0"))
	int32 DayOfWeek = 0;

	/** 1-based. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "1"))
	int32 Month = 1;

	/** 1-based. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "1"))
	int32 Day = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule")
	int64 AbsoluteMilliseconds = 0;
};

UENUM(BlueprintType)
enum class EDocTimeJumpPolicy : uint8
{
	/** Emit every crossed boundary up to the per-advance cap; excess is coalesced with a count. */
	FireCrossedBoundaries,
	/** One event per boundary kind carrying the crossed count. */
	CoalesceWithCount,
	/** No boundary events; one TimeJumped notification only. */
	SuppressAndNotifyJump
};

/** Serializable clock state (handoff 9.5). Transient pause owners are not included. */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocClockSnapshot
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") int32 SchemaVersion = CurrentSchemaVersion;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") int64 TotalMilliseconds = 0;
	/** Sub-millisecond remainder, in units of 1/ScaleDenominator microseconds (see FDocTimeMath). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") int64 Remainder = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") int64 ScaleNumerator = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") FName CalendarId;
	/** Algorithm version of the advancement arithmetic. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Time") int32 AlgorithmVersion = 1;
};

/** Stylized celestial output (labelled as such). Physically meaningful output needs a real provider. */
USTRUCT(BlueprintType)
struct DOCTIMERUNTIME_API FDocCelestialState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Time") float NormalizedSolarTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Time") float SunElevationDegrees = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Time") float SunAzimuthDegrees = 0.f;
	/** 0 = new moon, 0.5 = full moon. */
	UPROPERTY(BlueprintReadOnly, Category = "Time") float MoonPhase = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Time") float NormalizedDay = 0.f;
	/** Always true for the built-in model (not astronomy). */
	UPROPERTY(BlueprintReadOnly, Category = "Time") bool bStylized = true;
};

/**
 * Exact integer clock arithmetic (handoff 9.2).
 *
 * Time scale is a fixed-point rational: Scale = ScaleNumerator / ScaleDenominator
 * with ScaleDenominator = 1,000,000 (micro-precision; e.g. 60x = 60,000,000).
 *
 * Advance by ElapsedMicros (integer real microseconds):
 *   Product = ElapsedMicros * ScaleNumerator + Remainder
 *   Total  += Product / (ScaleDenominator * 1000)    // whole simulated ms
 *   Remainder = Product % (ScaleDenominator * 1000)
 * Because the remainder is carried, any partition of the same integer input
 * produces the same result as one advance. Large inputs are processed in chunks
 * of at most MaxChunkMicros to stay within int64.
 */
struct DOCTIMERUNTIME_API FDocTimeMath
{
	static constexpr int64 ScaleDenominator = 1000000;
	static constexpr int64 RemainderModulus = ScaleDenominator * 1000;
	static constexpr int64 MaxScaleNumerator = int64(100000) * ScaleDenominator; // 100,000x
	static constexpr int64 MaxChunkMicros = 60 * 1000000; // 60 real seconds per chunk

	/** Returns false (and leaves state unchanged) on invalid input or overflow of MaxTotal. */
	static bool Advance(int64& InOutTotalMs, int64& InOutRemainder, int64 ElapsedMicros, int64 ScaleNumerator, int64 MaxTotalMs);

	/** Convert a float scale to the fixed-point numerator (rounded). Negative → -1 (invalid). */
	static int64 ScaleToNumerator(double Scale);

	static FDocCalendarTime ToCalendar(const FDocCalendar& Calendar, int64 TotalMs);
	/** Inverse of ToCalendar for valid dates; false when out of range. */
	static bool FromCalendar(const FDocCalendar& Calendar, int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute, int32 Second, int64& OutTotalMs);

	/** Number of multiples of UnitMs in the half-open interval (OldMs, NewMs]. 0 when NewMs <= OldMs. */
	static int64 CountBoundaries(int64 OldMs, int64 NewMs, int64 UnitMs);

	/** Next occurrence strictly after AfterMs; INDEX_NONE (-1) when none. */
	static int64 NextOccurrence(const FDocCalendar& Calendar, const FDocScheduleEntry& Entry, int64 AfterMs);
};

UENUM(BlueprintType)
enum class EDocTimeUnit : uint8
{
	Minute,
	Hour,
	Day
};

/** Native (C++) mirrors of the Blueprint events. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocTimeBoundaryNative, EDocTimeUnit /*Unit*/, const FDocCalendarTime& /*Time*/, int32 /*CrossedCount*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocScheduleReachedNative, FName /*EntryId*/, FGameplayTag /*Tag*/, int64 /*OccurrenceMs*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocTimePeriodNative, FGameplayTag /*NewPeriod*/, FGameplayTag /*OldPeriod*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocTimeJumpNative, int64 /*OldMs*/, int64 /*NewMs*/, EDocTimeJumpPolicy /*Policy*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocTimeBoundaryEvent, const FDocCalendarTime&, Time, int32, CrossedCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocTimePeriodEvent, FGameplayTag, NewPeriod, FGameplayTag, OldPeriod);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocTimeJumpEvent, int64, OldMilliseconds, int64, NewMilliseconds, EDocTimeJumpPolicy, Policy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocScheduleReachedEvent, FName, EntryId, FGameplayTag, Tag, int64, OccurrenceMilliseconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocTimeRestoredEvent, const FDocClockSnapshot&, Snapshot);
