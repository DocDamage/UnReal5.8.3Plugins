#include "DocTimeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocTimeTypes)

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

bool FDocCalendar::IsValid(FString* OutError) const
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError) { *OutError = Message; }
		return false;
	};
	if (SecondsPerMinute <= 0 || MinutesPerHour <= 0 || HoursPerDay <= 0 || DaysPerWeek <= 0)
	{
		return Fail(TEXT("Calendar units must be positive"));
	}
	if (MonthLengths.Num() == 0)
	{
		return Fail(TEXT("Calendar needs at least one month"));
	}
	for (const int32 Length : MonthLengths)
	{
		if (Length <= 0)
		{
			return Fail(TEXT("Month lengths must be positive"));
		}
	}
	if (EpochWeekday < 0 || EpochWeekday >= DaysPerWeek)
	{
		return Fail(TEXT("EpochWeekday out of range"));
	}
	if (MaxYear < EpochYear)
	{
		return Fail(TEXT("MaxYear before EpochYear"));
	}
	// Overflow guard: the whole supported range must fit in int64 milliseconds.
	const double TotalMsDouble = double(MsPerDay()) * double(DaysPerYear()) * double(int64(MaxYear) - EpochYear + 1);
	if (TotalMsDouble > double(MAX_int64) / 2.0)
	{
		return Fail(TEXT("Supported date range overflows int64 milliseconds"));
	}
	return true;
}

int32 FDocCalendar::DaysPerYear() const
{
	int32 Sum = 0;
	for (const int32 Length : MonthLengths)
	{
		Sum += FMath::Max(0, Length);
	}
	return Sum;
}

int64 FDocCalendar::MaxSupportedMs() const
{
	return MsPerDay() * int64(DaysPerYear()) * (int64(MaxYear) - EpochYear + 1) - 1;
}

bool FDocTimePeriod::ContainsMinute(int32 MinuteOfDay) const
{
	if (StartMinute == EndMinute)
	{
		return false; // empty / invalid
	}
	if (StartMinute < EndMinute)
	{
		return MinuteOfDay >= StartMinute && MinuteOfDay < EndMinute;
	}
	return MinuteOfDay >= StartMinute || MinuteOfDay < EndMinute; // wraps midnight
}

// ---------------------------------------------------------------------------
// Exact advancement
// ---------------------------------------------------------------------------

bool FDocTimeMath::Advance(int64& InOutTotalMs, int64& InOutRemainder, int64 ElapsedMicros, int64 ScaleNumerator, int64 MaxTotalMs)
{
	if (ElapsedMicros < 0 || ScaleNumerator < 0 || ScaleNumerator > MaxScaleNumerator || InOutRemainder < 0 || InOutRemainder >= RemainderModulus)
	{
		return false;
	}
	int64 Total = InOutTotalMs;
	int64 Remainder = InOutRemainder;
	int64 Left = ElapsedMicros;
	while (Left > 0)
	{
		const int64 Chunk = FMath::Min(Left, MaxChunkMicros);
		Left -= Chunk;
		// Chunk <= 6e7 and ScaleNumerator <= 1e11 → product <= 6e18, plus remainder < 1e9: fits int64.
		const int64 Product = Chunk * ScaleNumerator + Remainder;
		const int64 Whole = Product / RemainderModulus;
		Remainder = Product % RemainderModulus;
		if (Total > MaxTotalMs - Whole)
		{
			return false; // would exceed the supported calendar range
		}
		Total += Whole;
	}
	InOutTotalMs = Total;
	InOutRemainder = Remainder;
	return true;
}

int64 FDocTimeMath::ScaleToNumerator(double Scale)
{
	if (Scale < 0.0 || !FMath::IsFinite(Scale))
	{
		return -1;
	}
	return FMath::Min<int64>(MaxScaleNumerator, static_cast<int64>(FMath::RoundToDouble(Scale * double(ScaleDenominator))));
}

FDocCalendarTime FDocTimeMath::ToCalendar(const FDocCalendar& Calendar, int64 TotalMs)
{
	FDocCalendarTime Out;
	TotalMs = FMath::Max<int64>(0, TotalMs);
	Out.TotalMilliseconds = TotalMs;

	const int64 MsPerDay = Calendar.MsPerDay();
	const int32 DaysPerYear = Calendar.DaysPerYear();
	if (MsPerDay <= 0 || DaysPerYear <= 0)
	{
		return Out;
	}

	Out.TotalDays = TotalMs / MsPerDay;
	const int64 MsOfDay = TotalMs % MsPerDay;

	const int64 YearIndex = Out.TotalDays / DaysPerYear;
	Out.Year = static_cast<int32>(Calendar.EpochYear + YearIndex);
	Out.DayOfYear = static_cast<int32>(Out.TotalDays % DaysPerYear);

	int32 DayCursor = Out.DayOfYear;
	Out.Month = 1;
	for (int32 i = 0; i < Calendar.MonthLengths.Num(); ++i)
	{
		const int32 Length = Calendar.MonthLengths[i];
		if (DayCursor < Length)
		{
			Out.Month = i + 1;
			Out.Day = DayCursor + 1;
			break;
		}
		DayCursor -= Length;
	}

	Out.DayOfWeek = static_cast<int32>((Out.TotalDays + Calendar.EpochWeekday) % Calendar.DaysPerWeek);
	Out.Hour = static_cast<int32>(MsOfDay / Calendar.MsPerHour());
	const int64 MsOfHour = MsOfDay % Calendar.MsPerHour();
	Out.Minute = static_cast<int32>(MsOfHour / Calendar.MsPerMinute());
	const int64 MsOfMinute = MsOfHour % Calendar.MsPerMinute();
	Out.Second = static_cast<int32>(MsOfMinute / 1000);
	Out.Millisecond = static_cast<int32>(MsOfMinute % 1000);
	Out.MinuteOfDay = Out.Hour * Calendar.MinutesPerHour + Out.Minute;
	Out.NormalizedDayTime = static_cast<float>(double(MsOfDay) / double(MsPerDay));
	return Out;
}

bool FDocTimeMath::FromCalendar(const FDocCalendar& Calendar, int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute, int32 Second, int64& OutTotalMs)
{
	if (!Calendar.IsValid() || Year < Calendar.EpochYear || Year > Calendar.MaxYear
		|| Month < 1 || Month > Calendar.MonthLengths.Num()
		|| Day < 1 || Day > Calendar.MonthLengths[Month - 1]
		|| Hour < 0 || Hour >= Calendar.HoursPerDay || Minute < 0 || Minute >= Calendar.MinutesPerHour
		|| Second < 0 || Second >= Calendar.SecondsPerMinute)
	{
		return false;
	}
	int64 Days = int64(Year - Calendar.EpochYear) * Calendar.DaysPerYear();
	for (int32 i = 0; i < Month - 1; ++i)
	{
		Days += Calendar.MonthLengths[i];
	}
	Days += Day - 1;
	OutTotalMs = Days * Calendar.MsPerDay() + int64(Hour) * Calendar.MsPerHour() + int64(Minute) * Calendar.MsPerMinute() + int64(Second) * 1000;
	return true;
}

int64 FDocTimeMath::CountBoundaries(int64 OldMs, int64 NewMs, int64 UnitMs)
{
	if (NewMs <= OldMs || UnitMs <= 0 || OldMs < 0)
	{
		return 0;
	}
	return NewMs / UnitMs - OldMs / UnitMs;
}

int64 FDocTimeMath::NextOccurrence(const FDocCalendar& Calendar, const FDocScheduleEntry& Entry, int64 AfterMs)
{
	if (!Calendar.IsValid())
	{
		return -1;
	}
	if (Entry.Recurrence == EDocRecurrence::Once)
	{
		return Entry.AbsoluteMilliseconds > AfterMs ? Entry.AbsoluteMilliseconds : -1;
	}
	if (Entry.MinuteOfDay < 0 || Entry.MinuteOfDay >= Calendar.MinutesPerDay())
	{
		return -1;
	}

	const int64 MsPerDay = Calendar.MsPerDay();
	const int64 TimeOfDay = int64(Entry.MinuteOfDay) * Calendar.MsPerMinute();
	const int64 StartDay = FMath::Max<int64>(0, AfterMs) / MsPerDay;
	// Search window: one full year plus the longest month covers every recurrence kind.
	int32 LongestMonth = 0;
	for (const int32 L : Calendar.MonthLengths) { LongestMonth = FMath::Max(LongestMonth, L); }
	const int64 SearchDays = int64(Calendar.DaysPerYear()) + LongestMonth + Calendar.DaysPerWeek + 1;
	const int64 MaxMs = Calendar.MaxSupportedMs();

	for (int64 D = StartDay; D <= StartDay + SearchDays; ++D)
	{
		const int64 Candidate = D * MsPerDay + TimeOfDay;
		if (Candidate <= AfterMs)
		{
			continue;
		}
		if (Candidate > MaxMs)
		{
			return -1;
		}
		bool bMatch = false;
		switch (Entry.Recurrence)
		{
		case EDocRecurrence::Daily:
			bMatch = true;
			break;
		case EDocRecurrence::Weekly:
			bMatch = ((D + Calendar.EpochWeekday) % Calendar.DaysPerWeek) == Entry.DayOfWeek;
			break;
		case EDocRecurrence::Monthly:
		{
			const FDocCalendarTime T = ToCalendar(Calendar, D * MsPerDay);
			bMatch = T.Day == Entry.Day;
			break;
		}
		case EDocRecurrence::Yearly:
		{
			const FDocCalendarTime T = ToCalendar(Calendar, D * MsPerDay);
			bMatch = T.Month == Entry.Month && T.Day == Entry.Day;
			break;
		}
		default:
			break;
		}
		if (bMatch)
		{
			return Candidate;
		}
	}
	return -1;
}
