#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocTimeTypes.h"
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "DocTimeSubsystem.generated.h"

/** Time configuration asset (handoff 9.1). Immutable at runtime. */
UCLASS(BlueprintType)
class DOCTIMERUNTIME_API UDocTimeConfiguration : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	FDocCalendar Calendar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Periods")
	TArray<FDocTimePeriod> Periods;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedules")
	TArray<FDocScheduleEntry> Schedules;

	/** Starting simulated time (calendar fields, 1-based date). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start")
	int32 StartYear = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start", meta = (ClampMin = "1"))
	int32 StartMonth = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start", meta = (ClampMin = "1"))
	int32 StartDay = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start", meta = (ClampMin = "0"))
	int32 StartHour = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Start", meta = (ClampMin = "0"))
	int32 StartMinute = 0;

	/** Simulated seconds per real second. Negative is invalid (no reverse time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clock", meta = (ClampMin = "0.0"))
	float DefaultTimeScale = 60.f;

	/** Per-advance cap on individually emitted boundary events (FireCrossedBoundaries). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clock", meta = (ClampMin = "1"))
	int32 MaxBoundaryEventsPerAdvance = 64;

	/** Stylized sun model: minute of day of sunrise and sunset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Celestial", meta = (ClampMin = "0"))
	int32 SunriseMinute = 6 * 60;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Celestial", meta = (ClampMin = "0"))
	int32 SunsetMinute = 18 * 60;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Celestial", meta = (ClampMin = "0.1"))
	float LunarCycleDays = 29.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Celestial", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSunElevationDegrees = 70.f;

	bool Validate(TArray<FString>& OutErrors) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Project Settings → Plugins → Doc Time. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Time"))
class DOCTIMERUNTIME_API UDocTimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Configuration applied to new game worlds. Unset = built-in defaults (24h, 12x30 days, 60x). */
	UPROPERTY(Config, EditAnywhere, Category = "Time")
	TSoftObjectPtr<UDocTimeConfiguration> DefaultConfiguration;

	/** Start the clock automatically when the world begins play. */
	UPROPERTY(Config, EditAnywhere, Category = "Time")
	bool bRunOnBeginPlay = true;
};

/**
 * World simulation clock (handoff Section 9).
 *
 * Updates on the world tick (does not tick while the world is paused). Frame
 * deltas are quantized to integer microseconds with the fraction carried to the
 * next frame, then advanced with FDocTimeMath (exact, remainder carried).
 *
 * Boundaries use (old, new] and fire once per forward crossing. Explicit jumps
 * (SetTime/AdvanceTime/SkipTo*) take a policy; RestoreClockState never replays
 * boundaries (it emits OnTimeRestored). Pause uses owner tokens: time resumes
 * only when every token is released.
 */
UCLASS()
class DOCTIMERUNTIME_API UDocWorldTimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocWorldTimeSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult SetConfiguration(UDocTimeConfiguration* Configuration, bool bResetToStartTime);

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	const FDocCalendar& GetCalendar() const { return Calendar; }

	// ---- Queries ----
	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	int64 GetTotalMilliseconds() const { return TotalMs; }

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	FDocCalendarTime GetCalendarTime() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	FGameplayTag GetPeriod() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	FGameplayTag GetPeriodAt(int64 TotalMilliseconds) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	float GetTimeScale() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	bool IsPaused() const { return PauseTokens.Num() > 0 || !bRunning; }

	UFUNCTION(BlueprintPure, Category = "Doc|Time")
	FDocCelestialState GetCelestialState() const;

	/** Next occurrence of a configured schedule entry strictly after now; -1 if none. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	int64 GetNextScheduleOccurrence(FName EntryId) const;

	/** Occurrences of every entry in (FromMs, ToMs], at most MaxResults, sorted by time then EntryId. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	TArray<FDocScheduleEntry> GetScheduleOccurrencesInRange(int64 FromMs, int64 ToMs, int32 MaxResults, TArray<int64>& OutOccurrenceMs) const;

	// ---- Control ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult SetTime(int64 NewTotalMilliseconds, EDocTimeJumpPolicy Policy = EDocTimeJumpPolicy::SuppressAndNotifyJump);

	/** Forward only. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult AdvanceTime(int64 DeltaMilliseconds, EDocTimeJumpPolicy Policy = EDocTimeJumpPolicy::CoalesceWithCount);

	/** Jump forward to the next occurrence of Hour:Minute. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult SkipToTime(int32 Hour, int32 Minute, EDocTimeJumpPolicy Policy = EDocTimeJumpPolicy::CoalesceWithCount);

	/** Jump forward to the start of the next different period. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult SkipToNextPeriod(EDocTimeJumpPolicy Policy = EDocTimeJumpPolicy::CoalesceWithCount);

	/** Returns a pause token; time runs only when no tokens remain. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocRequestHandle PauseTime(FName Reason);

	/** Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult ResumeTime(const FDocRequestHandle& PauseToken);

	/** Negative scales are rejected (InvalidInput). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult SetTimeScale(float NewScale);

	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	void SetRunning(bool bInRunning) { bRunning = bInRunning; }

	// ---- Persistence ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocClockSnapshot CaptureClockState() const;

	/** Validates schema and calendar identity; no boundary replay. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Time")
	FDocSystemResult RestoreClockState(const FDocClockSnapshot& Snapshot);

	// ---- Events ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimeBoundaryEvent OnMinuteChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimeBoundaryEvent OnHourChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimeBoundaryEvent OnDayChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimePeriodEvent OnPeriodChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimeJumpEvent OnTimeJumped;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocScheduleReachedEvent OnScheduleReached;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Time") FDocTimeRestoredEvent OnTimeRestored;

	FDocTimeBoundaryNative OnBoundaryNative;
	FDocScheduleReachedNative OnScheduleReachedNative;
	FDocTimePeriodNative OnPeriodChangedNative;
	FDocTimeJumpNative OnTimeJumpedNative;

	/** Advance by real elapsed microseconds with the current scale (tests and Tick use this). */
	void AdvanceByRealMicroseconds(int64 ElapsedMicros);

	//~ Subsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void ApplyConfigurationDefaults();
	/** Move time to NewMs and emit notifications per Policy (forward only emits boundaries). */
	void MoveTo(int64 NewMs, EDocTimeJumpPolicy Policy, bool bIsJump);
	void EmitBoundaries(int64 OldMs, int64 NewMs, EDocTimeJumpPolicy Policy);
	void EmitSchedules(int64 OldMs, int64 NewMs, EDocTimeJumpPolicy Policy);
	void UpdatePeriod();

	UPROPERTY(Transient)
	TObjectPtr<UDocTimeConfiguration> Configuration;

	FDocCalendar Calendar;
	TArray<FDocTimePeriod> Periods;
	TArray<FDocScheduleEntry> Schedules;
	int32 MaxBoundaryEvents = 64;

	int64 TotalMs = 0;
	int64 Remainder = 0;
	int64 ScaleNumerator = 60 * FDocTimeMath::ScaleDenominator;
	double FrameMicroCarry = 0.0;
	bool bRunning = false;
	FGameplayTag CurrentPeriod;

	TDocHandleTable<FName> PauseTokens;
};
