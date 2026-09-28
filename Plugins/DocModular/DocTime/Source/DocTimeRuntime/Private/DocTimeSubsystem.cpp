#include "DocTimeSubsystem.h"
#include "DocTimeLog.h"
#include "DocCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocTimeSubsystem)

// ---------------------------------------------------------------------------
// Configuration validation
// ---------------------------------------------------------------------------

bool UDocTimeConfiguration::Validate(TArray<FString>& OutErrors) const
{
	FString CalendarError;
	if (!Calendar.IsValid(&CalendarError))
	{
		OutErrors.Add(CalendarError);
		return false;
	}
	const int32 MinutesPerDay = Calendar.MinutesPerDay();
	for (const FDocTimePeriod& Period : Periods)
	{
		if (Period.StartMinute == Period.EndMinute)
		{
			OutErrors.Add(FString::Printf(TEXT("Period %s is empty (Start == End)"), *Period.PeriodTag.ToString()));
		}
		if (Period.StartMinute < 0 || Period.StartMinute >= MinutesPerDay || Period.EndMinute < 0 || Period.EndMinute > MinutesPerDay)
		{
			OutErrors.Add(FString::Printf(TEXT("Period %s outside the day (0..%d)"), *Period.PeriodTag.ToString(), MinutesPerDay));
		}
	}
	TSet<FName> Ids;
	for (const FDocScheduleEntry& Entry : Schedules)
	{
		if (Entry.EntryId.IsNone() || Ids.Contains(Entry.EntryId))
		{
			OutErrors.Add(FString::Printf(TEXT("Schedule entry id '%s' missing or duplicated"), *Entry.EntryId.ToString()));
		}
		Ids.Add(Entry.EntryId);
		if (Entry.Recurrence != EDocRecurrence::Once && (Entry.MinuteOfDay < 0 || Entry.MinuteOfDay >= MinutesPerDay))
		{
			OutErrors.Add(FString::Printf(TEXT("Schedule '%s' MinuteOfDay out of range"), *Entry.EntryId.ToString()));
		}
		if (Entry.Recurrence == EDocRecurrence::Weekly && (Entry.DayOfWeek < 0 || Entry.DayOfWeek >= Calendar.DaysPerWeek))
		{
			OutErrors.Add(FString::Printf(TEXT("Schedule '%s' DayOfWeek out of range"), *Entry.EntryId.ToString()));
		}
	}
	int64 StartMs = 0;
	if (!FDocTimeMath::FromCalendar(Calendar, StartYear, StartMonth, StartDay, StartHour, StartMinute, 0, StartMs))
	{
		OutErrors.Add(TEXT("Start date/time is not valid in this calendar"));
	}
	if (DefaultTimeScale < 0.f)
	{
		OutErrors.Add(TEXT("DefaultTimeScale must not be negative"));
	}
	return OutErrors.Num() == 0;
}

#if WITH_EDITOR
EDataValidationResult UDocTimeConfiguration::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FString> Errors;
	if (!Validate(Errors))
	{
		for (const FString& Error : Errors)
		{
			Context.AddError(FText::FromString(Error));
		}
		return EDataValidationResult::Invalid;
	}
	return Super::IsDataValid(Context);
}
#endif

// ---------------------------------------------------------------------------
// Subsystem lifecycle
// ---------------------------------------------------------------------------

UDocWorldTimeSubsystem* UDocWorldTimeSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocWorldTimeSubsystem>() : nullptr;
}

bool UDocWorldTimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDocWorldTimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocWorldTimeSubsystem, STATGROUP_Tickables);
}

void UDocWorldTimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UDocTimeSettings* Settings = GetDefault<UDocTimeSettings>();
	UDocTimeConfiguration* Config = Settings->DefaultConfiguration.IsNull() ? nullptr : Settings->DefaultConfiguration.LoadSynchronous();
	if (Config)
	{
		const FDocSystemResult Result = SetConfiguration(Config, true);
		if (!Result.IsSuccess())
		{
			UE_LOG(LogDocTime, Error, TEXT("Default time configuration rejected: %s"), *Result.ToString());
			ApplyConfigurationDefaults();
		}
	}
	else
	{
		ApplyConfigurationDefaults();
	}
}

void UDocWorldTimeSubsystem::ApplyConfigurationDefaults()
{
	Configuration = nullptr;
	Calendar = FDocCalendar();
	Periods.Reset();
	Schedules.Reset();
	MaxBoundaryEvents = 64;
	ScaleNumerator = 60 * FDocTimeMath::ScaleDenominator;
	FDocTimeMath::FromCalendar(Calendar, 1, 1, 1, 8, 0, 0, TotalMs);
	Remainder = 0;
	CurrentPeriod = FGameplayTag();
}

void UDocWorldTimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = GetDefault<UDocTimeSettings>()->bRunOnBeginPlay;
	CurrentPeriod = GetPeriodAt(TotalMs);
}

void UDocWorldTimeSubsystem::Deinitialize()
{
	PauseTokens.Reset();
	Configuration = nullptr;
	Super::Deinitialize();
}

void UDocWorldTimeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsPaused() || DeltaTime <= 0.f)
	{
		return;
	}
	// Quantize the frame delta to integer microseconds, carrying the fraction.
	const double Micros = double(DeltaTime) * 1000000.0 + FrameMicroCarry;
	const double Whole = FMath::FloorToDouble(Micros);
	FrameMicroCarry = Micros - Whole;
	AdvanceByRealMicroseconds(static_cast<int64>(Whole));
}

FDocSystemResult UDocWorldTimeSubsystem::SetConfiguration(UDocTimeConfiguration* InConfiguration, bool bResetToStartTime)
{
	if (!InConfiguration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null configuration"));
	}
	TArray<FString> Errors;
	if (!InConfiguration->Validate(Errors))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(Errors, TEXT("; ")));
	}
	Configuration = InConfiguration;
	Calendar = InConfiguration->Calendar;
	Periods = InConfiguration->Periods;
	Schedules = InConfiguration->Schedules;
	MaxBoundaryEvents = InConfiguration->MaxBoundaryEventsPerAdvance;
	ScaleNumerator = FDocTimeMath::ScaleToNumerator(InConfiguration->DefaultTimeScale);
	if (bResetToStartTime)
	{
		FDocTimeMath::FromCalendar(Calendar, InConfiguration->StartYear, InConfiguration->StartMonth, InConfiguration->StartDay,
			InConfiguration->StartHour, InConfiguration->StartMinute, 0, TotalMs);
		Remainder = 0;
	}
	CurrentPeriod = GetPeriodAt(TotalMs);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

FDocCalendarTime UDocWorldTimeSubsystem::GetCalendarTime() const
{
	return FDocTimeMath::ToCalendar(Calendar, TotalMs);
}

FGameplayTag UDocWorldTimeSubsystem::GetPeriod() const
{
	return GetPeriodAt(TotalMs);
}

FGameplayTag UDocWorldTimeSubsystem::GetPeriodAt(int64 TotalMilliseconds) const
{
	const int32 Minute = FDocTimeMath::ToCalendar(Calendar, TotalMilliseconds).MinuteOfDay;
	const FDocTimePeriod* Best = nullptr;
	for (const FDocTimePeriod& Period : Periods)
	{
		if (Period.ContainsMinute(Minute) && (!Best || Period.Priority > Best->Priority))
		{
			Best = &Period;
		}
	}
	return Best ? Best->PeriodTag : FGameplayTag();
}

float UDocWorldTimeSubsystem::GetTimeScale() const
{
	return static_cast<float>(double(ScaleNumerator) / double(FDocTimeMath::ScaleDenominator));
}

FDocCelestialState UDocWorldTimeSubsystem::GetCelestialState() const
{
	FDocCelestialState State;
	const FDocCalendarTime Now = GetCalendarTime();
	const float MinutesPerDay = float(FMath::Max(1, Calendar.MinutesPerDay()));
	const int32 SunriseMin = Configuration ? Configuration->SunriseMinute : 6 * 60;
	const int32 SunsetMin = Configuration ? Configuration->SunsetMinute : 18 * 60;
	const float MaxElevation = Configuration ? Configuration->MaxSunElevationDegrees : 70.f;
	const float Lunar = Configuration ? Configuration->LunarCycleDays : 29.5f;

	const float T = Now.NormalizedDayTime;
	const float Rise = SunriseMin / MinutesPerDay;
	const float Set = SunsetMin / MinutesPerDay;
	State.NormalizedSolarTime = T;
	State.NormalizedDay = T;

	const float DayLength = Set > Rise ? Set - Rise : (1.f - Rise) + Set;
	float DayProgress = T >= Rise ? T - Rise : (1.f - Rise) + T;
	if (DayLength > 0.f && DayProgress <= DayLength)
	{
		const float P = DayProgress / DayLength;
		State.SunElevationDegrees = MaxElevation * FMath::Sin(PI * P);
		State.SunAzimuthDegrees = 90.f + 180.f * P;
	}
	else
	{
		const float NightLength = FMath::Max(KINDA_SMALL_NUMBER, 1.f - DayLength);
		const float P = FMath::Clamp((DayProgress - DayLength) / NightLength, 0.f, 1.f);
		State.SunElevationDegrees = -MaxElevation * FMath::Sin(PI * P);
		State.SunAzimuthDegrees = FMath::Fmod(270.f + 180.f * P, 360.f);
	}
	const double Days = double(TotalMs) / double(FMath::Max<int64>(1, Calendar.MsPerDay()));
	State.MoonPhase = static_cast<float>(FMath::Fmod(Days, double(Lunar)) / double(Lunar));
	State.bStylized = true;
	return State;
}

int64 UDocWorldTimeSubsystem::GetNextScheduleOccurrence(FName EntryId) const
{
	for (const FDocScheduleEntry& Entry : Schedules)
	{
		if (Entry.EntryId == EntryId)
		{
			return FDocTimeMath::NextOccurrence(Calendar, Entry, TotalMs);
		}
	}
	return -1;
}

TArray<FDocScheduleEntry> UDocWorldTimeSubsystem::GetScheduleOccurrencesInRange(int64 FromMs, int64 ToMs, int32 MaxResults, TArray<int64>& OutOccurrenceMs) const
{
	struct FHit { int64 Ms; int32 Index; };
	TArray<FHit> Hits;
	MaxResults = FMath::Max(0, MaxResults);
	for (int32 i = 0; i < Schedules.Num(); ++i)
	{
		int64 Cursor = FromMs;
		while (Hits.Num() < MaxResults * 4 + 16)
		{
			const int64 Next = FDocTimeMath::NextOccurrence(Calendar, Schedules[i], Cursor);
			if (Next < 0 || Next > ToMs)
			{
				break;
			}
			Hits.Add({ Next, i });
			Cursor = Next;
		}
	}
	Hits.Sort([this](const FHit& A, const FHit& B)
	{
		return A.Ms != B.Ms ? A.Ms < B.Ms : Schedules[A.Index].EntryId.LexicalLess(Schedules[B.Index].EntryId);
	});
	TArray<FDocScheduleEntry> Out;
	OutOccurrenceMs.Reset();
	for (int32 i = 0; i < Hits.Num() && Out.Num() < MaxResults; ++i)
	{
		Out.Add(Schedules[Hits[i].Index]);
		OutOccurrenceMs.Add(Hits[i].Ms);
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Advancement and notifications
// ---------------------------------------------------------------------------

void UDocWorldTimeSubsystem::AdvanceByRealMicroseconds(int64 ElapsedMicros)
{
	int64 NewTotal = TotalMs;
	int64 NewRemainder = Remainder;
	if (!FDocTimeMath::Advance(NewTotal, NewRemainder, ElapsedMicros, ScaleNumerator, Calendar.MaxSupportedMs()))
	{
		UE_LOG(LogDocTime, Warning, TEXT("Clock advance rejected (overflow or end of supported calendar range); time stopped"));
		bRunning = false;
		return;
	}
	Remainder = NewRemainder;
	if (NewTotal != TotalMs)
	{
		MoveTo(NewTotal, EDocTimeJumpPolicy::FireCrossedBoundaries, /*bIsJump*/ false);
	}
}

void UDocWorldTimeSubsystem::MoveTo(int64 NewMs, EDocTimeJumpPolicy Policy, bool bIsJump)
{
	const int64 OldMs = TotalMs;
	TotalMs = NewMs;

	if (NewMs > OldMs && Policy != EDocTimeJumpPolicy::SuppressAndNotifyJump)
	{
		EmitBoundaries(OldMs, NewMs, Policy);
		EmitSchedules(OldMs, NewMs, Policy);
	}
	if (bIsJump || NewMs < OldMs)
	{
		OnTimeJumped.Broadcast(OldMs, NewMs, Policy);
		OnTimeJumpedNative.Broadcast(OldMs, NewMs, Policy);
	}
	UpdatePeriod();
}

void UDocWorldTimeSubsystem::EmitBoundaries(int64 OldMs, int64 NewMs, EDocTimeJumpPolicy Policy)
{
	const int64 MinuteMs = Calendar.MsPerMinute();
	const int64 HourMs = Calendar.MsPerHour();
	const int64 DayMs = Calendar.MsPerDay();
	const int64 Minutes = FDocTimeMath::CountBoundaries(OldMs, NewMs, MinuteMs);
	const int64 Hours = FDocTimeMath::CountBoundaries(OldMs, NewMs, HourMs);
	const int64 Days = FDocTimeMath::CountBoundaries(OldMs, NewMs, DayMs);
	if (Minutes == 0)
	{
		return;
	}

	if (Policy == EDocTimeJumpPolicy::FireCrossedBoundaries && Minutes <= MaxBoundaryEvents)
	{
		// Chronological: each minute boundary, with hour/day boundaries at the same instant.
		const int64 FirstBoundary = (OldMs / MinuteMs + 1) * MinuteMs;
		for (int64 B = FirstBoundary; B <= NewMs; B += MinuteMs)
		{
			const FDocCalendarTime At = FDocTimeMath::ToCalendar(Calendar, B);
			OnMinuteChanged.Broadcast(At, 1);
			OnBoundaryNative.Broadcast(EDocTimeUnit::Minute, At, 1);
			if (B % HourMs == 0) { OnHourChanged.Broadcast(At, 1); OnBoundaryNative.Broadcast(EDocTimeUnit::Hour, At, 1); }
			if (B % DayMs == 0) { OnDayChanged.Broadcast(At, 1); OnBoundaryNative.Broadcast(EDocTimeUnit::Day, At, 1); }
		}
		return;
	}

	// Coalesce (explicitly requested, or over the per-advance cap).
	const FDocCalendarTime At = FDocTimeMath::ToCalendar(Calendar, NewMs);
	const int32 MinuteCount = static_cast<int32>(FMath::Min<int64>(Minutes, MAX_int32));
	OnMinuteChanged.Broadcast(At, MinuteCount);
	OnBoundaryNative.Broadcast(EDocTimeUnit::Minute, At, MinuteCount);
	if (Hours > 0)
	{
		const int32 HourCount = static_cast<int32>(FMath::Min<int64>(Hours, MAX_int32));
		OnHourChanged.Broadcast(At, HourCount);
		OnBoundaryNative.Broadcast(EDocTimeUnit::Hour, At, HourCount);
	}
	if (Days > 0)
	{
		const int32 DayCount = static_cast<int32>(FMath::Min<int64>(Days, MAX_int32));
		OnDayChanged.Broadcast(At, DayCount);
		OnBoundaryNative.Broadcast(EDocTimeUnit::Day, At, DayCount);
	}
}

void UDocWorldTimeSubsystem::EmitSchedules(int64 OldMs, int64 NewMs, EDocTimeJumpPolicy Policy)
{
	for (const FDocScheduleEntry& Entry : Schedules)
	{
		int64 Cursor = OldMs;
		int32 Emitted = 0;
		int64 Last = -1;
		while (true)
		{
			const int64 Next = FDocTimeMath::NextOccurrence(Calendar, Entry, Cursor);
			if (Next < 0 || Next > NewMs)
			{
				break;
			}
			Last = Next;
			Cursor = Next;
			if (Policy == EDocTimeJumpPolicy::FireCrossedBoundaries && Emitted < MaxBoundaryEvents)
			{
				OnScheduleReached.Broadcast(Entry.EntryId, Entry.Tag, Next);
				OnScheduleReachedNative.Broadcast(Entry.EntryId, Entry.Tag, Next);
				++Emitted;
			}
			else if (Policy != EDocTimeJumpPolicy::FireCrossedBoundaries)
			{
				continue; // coalesce: only the latest occurrence is reported below
			}
			else
			{
				break; // cap reached: consumers reconcile with GetScheduleOccurrencesInRange
			}
		}
		if (Policy == EDocTimeJumpPolicy::CoalesceWithCount && Last >= 0)
		{
			OnScheduleReached.Broadcast(Entry.EntryId, Entry.Tag, Last);
			OnScheduleReachedNative.Broadcast(Entry.EntryId, Entry.Tag, Last);
		}
	}
}

void UDocWorldTimeSubsystem::UpdatePeriod()
{
	const FGameplayTag NewPeriod = GetPeriodAt(TotalMs);
	if (NewPeriod != CurrentPeriod)
	{
		const FGameplayTag Old = CurrentPeriod;
		CurrentPeriod = NewPeriod;
		OnPeriodChanged.Broadcast(NewPeriod, Old);
		OnPeriodChangedNative.Broadcast(NewPeriod, Old);
	}
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

FDocSystemResult UDocWorldTimeSubsystem::SetTime(int64 NewTotalMilliseconds, EDocTimeJumpPolicy Policy)
{
	if (NewTotalMilliseconds < 0 || NewTotalMilliseconds > Calendar.MaxSupportedMs())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Time outside the supported calendar range"));
	}
	Remainder = 0;
	MoveTo(NewTotalMilliseconds, Policy, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocWorldTimeSubsystem::AdvanceTime(int64 DeltaMilliseconds, EDocTimeJumpPolicy Policy)
{
	if (DeltaMilliseconds < 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("AdvanceTime is forward only; use SetTime to seek backward"));
	}
	if (TotalMs > Calendar.MaxSupportedMs() - DeltaMilliseconds)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Advance exceeds the supported calendar range"));
	}
	MoveTo(TotalMs + DeltaMilliseconds, Policy, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocWorldTimeSubsystem::SkipToTime(int32 Hour, int32 Minute, EDocTimeJumpPolicy Policy)
{
	if (Hour < 0 || Hour >= Calendar.HoursPerDay || Minute < 0 || Minute >= Calendar.MinutesPerHour)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Hour/minute out of range"));
	}
	FDocScheduleEntry Target;
	Target.Recurrence = EDocRecurrence::Daily;
	Target.MinuteOfDay = Hour * Calendar.MinutesPerHour + Minute;
	const int64 Next = FDocTimeMath::NextOccurrence(Calendar, Target, TotalMs);
	if (Next < 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No such time within range"));
	}
	Remainder = 0;
	MoveTo(Next, Policy, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocWorldTimeSubsystem::SkipToNextPeriod(EDocTimeJumpPolicy Policy)
{
	if (Periods.Num() == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("No periods configured"));
	}
	const FGameplayTag Current = GetPeriodAt(TotalMs);
	const int64 MinuteMs = Calendar.MsPerMinute();
	int64 Probe = (TotalMs / MinuteMs + 1) * MinuteMs;
	const int64 Limit = TotalMs + Calendar.MsPerDay() + MinuteMs;
	for (; Probe <= Limit; Probe += MinuteMs)
	{
		if (GetPeriodAt(Probe) != Current)
		{
			Remainder = 0;
			MoveTo(Probe, Policy, true);
			return FDocSystemResult::MakeSuccess();
		}
	}
	return FDocSystemResult::MakeNoChange(TEXT("Only one period covers the whole day"));
}

FDocRequestHandle UDocWorldTimeSubsystem::PauseTime(FName Reason)
{
	return PauseTokens.Add(this, Reason);
}

FDocSystemResult UDocWorldTimeSubsystem::ResumeTime(const FDocRequestHandle& PauseToken)
{
	return PauseTokens.Remove(PauseToken, this) ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("Pause token already released or unknown"));
}

FDocSystemResult UDocWorldTimeSubsystem::SetTimeScale(float NewScale)
{
	const int64 Numerator = FDocTimeMath::ScaleToNumerator(NewScale);
	if (Numerator < 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Negative or non-finite time scale; reverse time is not supported"));
	}
	// Remainder is kept: it is in scale-independent units (real micro × scale units), so no time is lost.
	ScaleNumerator = Numerator;
	return FDocSystemResult::MakeSuccess();
}

FDocClockSnapshot UDocWorldTimeSubsystem::CaptureClockState() const
{
	FDocClockSnapshot Snapshot;
	Snapshot.TotalMilliseconds = TotalMs;
	Snapshot.Remainder = Remainder;
	Snapshot.ScaleNumerator = ScaleNumerator;
	Snapshot.CalendarId = Calendar.CalendarId;
	return Snapshot;
}

FDocSystemResult UDocWorldTimeSubsystem::RestoreClockState(const FDocClockSnapshot& Snapshot)
{
	if (Snapshot.SchemaVersion > FDocClockSnapshot::CurrentSchemaVersion || Snapshot.AlgorithmVersion != 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Clock snapshot from a newer schema/algorithm"));
	}
	if (Snapshot.CalendarId != Calendar.CalendarId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Snapshot calendar '%s' does not match active calendar '%s'"), *Snapshot.CalendarId.ToString(), *Calendar.CalendarId.ToString()));
	}
	if (Snapshot.TotalMilliseconds < 0 || Snapshot.TotalMilliseconds > Calendar.MaxSupportedMs()
		|| Snapshot.Remainder < 0 || Snapshot.Remainder >= FDocTimeMath::RemainderModulus
		|| Snapshot.ScaleNumerator < 0 || Snapshot.ScaleNumerator > FDocTimeMath::MaxScaleNumerator)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Clock snapshot values out of range"));
	}
	TotalMs = Snapshot.TotalMilliseconds;
	Remainder = Snapshot.Remainder;
	ScaleNumerator = Snapshot.ScaleNumerator;
	// Restore is not gameplay: update the period silently and announce the restore.
	CurrentPeriod = GetPeriodAt(TotalMs);
	OnTimeRestored.Broadcast(Snapshot);
	return FDocSystemResult::MakeSuccess();
}
