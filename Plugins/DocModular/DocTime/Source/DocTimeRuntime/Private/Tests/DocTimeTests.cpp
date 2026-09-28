// DocTime automation tests (TIM-01..TIM-06). Expected values are hand-computed.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocTimeSubsystem.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"

namespace DocTimeTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Day, "Test.DocTime.Period.Day");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Night, "Test.DocTime.Period.Night");

	/** Reference: whole simulated ms for a single integer input (no chunking, no remainder carry). */
	int64 ReferenceMs(int64 TotalMicros, int64 ScaleNumerator)
	{
		// Values chosen by the tests keep TotalMicros * ScaleNumerator within int64.
		return (TotalMicros * ScaleNumerator) / (FDocTimeMath::ScaleDenominator * 1000);
	}

	FDocCalendar SmallCalendar()
	{
		FDocCalendar C;
		C.CalendarId = TEXT("Small");
		C.SecondsPerMinute = 10;
		C.MinutesPerHour = 10;
		C.HoursPerDay = 10;
		C.DaysPerWeek = 5;
		C.MonthLengths = { 3, 5, 2 };
		C.EpochYear = 1;
		C.MaxYear = 1000;
		return C;
	}
}

using namespace DocTimeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeAdvancementTest, "Doc.Time.Advancement", Flags)
bool FDocTimeAdvancementTest::RunTest(const FString& Parameters)
{
	const int64 Max = MAX_int64 / 4;
	struct FCase { int64 TotalMicros; double Scale; };
	const FCase Cases[] = {
		{ 10000000, 60.0 },          // 10 s at 60x
		{ 10000000, 1.37 },          // non-integer scale
		{ 7777777, 0.333333 },       // fractional everything
		{ 1000000000000, 1.5 },      // ~11.6 real days
	};
	for (const FCase& Case : Cases)
	{
		const int64 Scale = FDocTimeMath::ScaleToNumerator(Case.Scale);
		const int64 Expected = ReferenceMs(Case.TotalMicros, Scale);

		// One advance.
		int64 TotalA = 0, RemA = 0;
		TestTrue(TEXT("Single advance ok"), FDocTimeMath::Advance(TotalA, RemA, Case.TotalMicros, Scale, Max));

		// Many uneven partitions of the same input (60 fps-like frames plus odd pieces).
		int64 TotalB = 0, RemB = 0, Left = Case.TotalMicros, Step = 16667;
		while (Left > 0)
		{
			const int64 Piece = FMath::Min(Left, Step);
			FDocTimeMath::Advance(TotalB, RemB, Piece, Scale, Max);
			Left -= Piece;
			Step = Step == 16667 ? 16666 : (Step == 16666 ? 3 : 16667); // vary pieces
			if (Case.TotalMicros > 100000000) { Step *= 1000; }        // keep long cases fast
		}

		TestEqual(FString::Printf(TEXT("Single advance matches reference (%lld us @ %.6f)"), Case.TotalMicros, Case.Scale), TotalA, Expected);
		TestEqual(TEXT("Partitioned advance matches single advance"), TotalB, TotalA);
		TestEqual(TEXT("Remainders match"), RemB, RemA);
	}

	int64 T = 0, R = 0;
	TestFalse(TEXT("Negative elapsed rejected"), FDocTimeMath::Advance(T, R, -1, FDocTimeMath::ScaleDenominator, Max));
	TestEqual(TEXT("Negative scale invalid"), FDocTimeMath::ScaleToNumerator(-1.0), static_cast<int64>(-1));
	TestFalse(TEXT("Overflow beyond max total rejected"), FDocTimeMath::Advance(T, R, 1000000, FDocTimeMath::ScaleDenominator, 10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeCalendarTest, "Doc.Time.CalendarRollover", Flags)
bool FDocTimeCalendarTest::RunTest(const FString& Parameters)
{
	const FDocCalendar C = SmallCalendar();
	TestTrue(TEXT("Small calendar valid"), C.IsValid());
	// ms/min 10,000; ms/hour 100,000; ms/day 1,000,000; days/year 10.
	TestEqual(TEXT("ms per day"), C.MsPerDay(), static_cast<int64>(1000000));

	// 13 days + 4h + 7m + 3s = 13,473,000 ms.
	const FDocCalendarTime T = FDocTimeMath::ToCalendar(C, 13473000);
	TestEqual(TEXT("Year"), T.Year, 2);
	TestEqual(TEXT("DayOfYear"), T.DayOfYear, 3);
	TestEqual(TEXT("Month (3-day first month rolled over)"), T.Month, 2);
	TestEqual(TEXT("Day"), T.Day, 1);
	TestEqual(TEXT("DayOfWeek"), T.DayOfWeek, 3);
	TestEqual(TEXT("Hour"), T.Hour, 4);
	TestEqual(TEXT("Minute"), T.Minute, 7);
	TestEqual(TEXT("Second"), T.Second, 3);

	int64 Back = 0;
	TestTrue(TEXT("Inverse conversion"), FDocTimeMath::FromCalendar(C, 2, 2, 1, 4, 7, 3, Back));
	TestEqual(TEXT("Inverse matches"), Back, static_cast<int64>(13473000));
	TestFalse(TEXT("Day 3 of a 2-day month rejected"), FDocTimeMath::FromCalendar(C, 1, 3, 3, 0, 0, 0, Back));

	// Last ms of the year rolls to year 2, month 1, day 1 at +1 ms.
	const FDocCalendarTime End = FDocTimeMath::ToCalendar(C, 9999999);
	const FDocCalendarTime Next = FDocTimeMath::ToCalendar(C, 10000000);
	TestEqual(TEXT("End of year month"), End.Month, 3);
	TestEqual(TEXT("End of year day"), End.Day, 2);
	TestEqual(TEXT("Rollover year"), Next.Year, 2);
	TestTrue(TEXT("Rollover to month 1 day 1"), Next.Month == 1 && Next.Day == 1);

	FDocCalendar Bad = C;
	Bad.MonthLengths = { 3, 0 };
	TestFalse(TEXT("Zero-length month invalid"), Bad.IsValid());
	Bad = C;
	Bad.HoursPerDay = 0;
	TestFalse(TEXT("Zero hours per day invalid"), Bad.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeBoundaryTest, "Doc.Time.EventBoundary", Flags)
bool FDocTimeBoundaryTest::RunTest(const FString& Parameters)
{
	// Half-open (old, new] counting.
	TestEqual(TEXT("Exactly on boundary counts once"), FDocTimeMath::CountBoundaries(59999, 60000, 60000), static_cast<int64>(1));
	TestEqual(TEXT("Starting on boundary does not recount"), FDocTimeMath::CountBoundaries(60000, 119999, 60000), static_cast<int64>(0));
	TestEqual(TEXT("Backward counts nothing"), FDocTimeMath::CountBoundaries(120000, 60000, 60000), static_cast<int64>(0));

	FDocScopedTestWorld TW;
	UDocWorldTimeSubsystem* Time = TW.GetSubsystem<UDocWorldTimeSubsystem>();
	if (!TestNotNull(TEXT("Time subsystem"), Time)) { return false; }

	int32 Minutes = 0, Hours = 0, Days = 0, CoalescedMinutes = 0, Jumps = 0;
	Time->OnBoundaryNative.AddLambda([&](EDocTimeUnit Unit, const FDocCalendarTime&, int32 Count)
	{
		if (Unit == EDocTimeUnit::Minute) { Minutes += 1; CoalescedMinutes += Count; }
		if (Unit == EDocTimeUnit::Hour) { Hours += Count; }
		if (Unit == EDocTimeUnit::Day) { Days += Count; }
	});
	Time->OnTimeJumpedNative.AddLambda([&](int64, int64, EDocTimeJumpPolicy) { ++Jumps; });

	// Default calendar (24h). Set 00:59:30 silently, then run 60 real seconds at 1x.
	Time->SetTime(59 * 60000 + 30000, EDocTimeJumpPolicy::SuppressAndNotifyJump);
	Minutes = Hours = Days = CoalescedMinutes = 0;
	Time->SetTimeScale(1.f);
	Time->AdvanceByRealMicroseconds(60000000);
	TestEqual(TEXT("One minute boundary (01:00:00)"), Minutes, 1);
	TestEqual(TEXT("One hour boundary"), Hours, 1);
	TestEqual(TEXT("No day boundary"), Days, 0);

	// Large skip: coalesced, not millions of events.
	Minutes = Hours = Days = CoalescedMinutes = 0;
	const int64 Year = 360LL * 24 * 60 * 60000;
	Time->AdvanceTime(Year, EDocTimeJumpPolicy::CoalesceWithCount);
	TestEqual(TEXT("One coalesced minute event"), Minutes, 1);
	TestEqual(TEXT("Coalesced minute count"), CoalescedMinutes, 360 * 24 * 60);
	TestEqual(TEXT("Coalesced day count"), Days, 360);

	// Backward seek emits no boundaries, only a jump.
	Minutes = 0;
	const int32 JumpsBefore = Jumps;
	Time->SetTime(0, EDocTimeJumpPolicy::FireCrossedBoundaries);
	TestEqual(TEXT("Backward seek: no boundaries"), Minutes, 0);
	TestEqual(TEXT("Backward seek: one jump"), Jumps, JumpsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeScaleTest, "Doc.Time.TimeScale", Flags)
bool FDocTimeScaleTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWorldTimeSubsystem* Time = TW.GetSubsystem<UDocWorldTimeSubsystem>();
	if (!TestNotNull(TEXT("Time subsystem"), Time)) { return false; }

	TestEqual(TEXT("Negative scale rejected"), Time->SetTimeScale(-2.f).Outcome, EDocResultOutcome::InvalidInput);

	// Remainder survives a scale change: 0.4 ms + 0.6 ms = 1 ms across the change.
	Time->SetTime(0);
	Time->SetTimeScale(1.f);
	Time->AdvanceByRealMicroseconds(400);  // 0.4 ms
	TestEqual(TEXT("Sub-ms not yet visible"), Time->GetTotalMilliseconds(), static_cast<int64>(0));
	Time->SetTimeScale(2.f);
	Time->AdvanceByRealMicroseconds(300);  // 0.6 ms at 2x
	TestEqual(TEXT("Remainder carried across scale change"), Time->GetTotalMilliseconds(), static_cast<int64>(1));

	// Nested pause tokens.
	const FDocRequestHandle A = Time->PauseTime(TEXT("Menu"));
	const FDocRequestHandle B = Time->PauseTime(TEXT("Cutscene"));
	TestTrue(TEXT("Paused"), Time->IsPaused());
	Time->ResumeTime(A);
	TestTrue(TEXT("Still paused by the other owner"), Time->IsPaused());
	TestEqual(TEXT("Releasing twice is NoChange"), Time->ResumeTime(A).Outcome, EDocResultOutcome::NoChange);
	Time->ResumeTime(B);
	Time->SetRunning(true);
	TestFalse(TEXT("Resumed after all tokens"), Time->IsPaused());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeSnapshotTest, "Doc.Time.SaveRestore", Flags)
bool FDocTimeSnapshotTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWorldTimeSubsystem* Time = TW.GetSubsystem<UDocWorldTimeSubsystem>();
	if (!TestNotNull(TEXT("Time subsystem"), Time)) { return false; }

	Time->SetTime(123456);
	Time->SetTimeScale(1.f);
	Time->AdvanceByRealMicroseconds(700); // leaves a 0.7 ms remainder
	const FDocClockSnapshot Saved = Time->CaptureClockState();

	int32 Boundaries = 0;
	Time->OnBoundaryNative.AddLambda([&](EDocTimeUnit, const FDocCalendarTime&, int32) { ++Boundaries; });
	Time->AdvanceTime(3600000);
	Boundaries = 0;
	TestTrue(TEXT("Restore ok"), Time->RestoreClockState(Saved).IsSuccess());
	TestEqual(TEXT("Restored time"), Time->GetTotalMilliseconds(), Saved.TotalMilliseconds);
	TestEqual(TEXT("Restore replays no boundaries"), Boundaries, 0);
	Time->AdvanceByRealMicroseconds(300);
	TestEqual(TEXT("Restored remainder completes the ms"), Time->GetTotalMilliseconds(), Saved.TotalMilliseconds + 1);

	FDocClockSnapshot Wrong = Saved;
	Wrong.CalendarId = TEXT("SomeOtherCalendar");
	TestEqual(TEXT("Calendar mismatch rejected"), Time->RestoreClockState(Wrong).Outcome, EDocResultOutcome::InvalidConfiguration);
	FDocClockSnapshot Future = Saved;
	Future.SchemaVersion = 99;
	TestEqual(TEXT("Future schema rejected"), Time->RestoreClockState(Future).Outcome, EDocResultOutcome::Unsupported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocTimeScheduleTest, "Doc.Time.Schedules", Flags)
bool FDocTimeScheduleTest::RunTest(const FString& Parameters)
{
	const FDocCalendar C = FDocCalendar(); // 24h, 60 min, 7-day weeks, 12x30 days
	const int64 Day = C.MsPerDay();
	FDocScheduleEntry Daily;
	Daily.EntryId = TEXT("Open");
	Daily.Recurrence = EDocRecurrence::Daily;
	Daily.MinuteOfDay = 9 * 60;
	TestEqual(TEXT("Next daily after midnight"), FDocTimeMath::NextOccurrence(C, Daily, 0), 9 * 60 * 60000LL);
	TestEqual(TEXT("Exactly at occurrence → next day"), FDocTimeMath::NextOccurrence(C, Daily, 9 * 60 * 60000LL), Day + 9 * 60 * 60000LL);

	FDocScheduleEntry Weekly = Daily;
	Weekly.Recurrence = EDocRecurrence::Weekly;
	Weekly.DayOfWeek = 3;
	TestEqual(TEXT("Weekly on day 3"), FDocTimeMath::NextOccurrence(C, Weekly, 0), 3 * Day + 9 * 60 * 60000LL);

	FDocScheduleEntry Yearly = Daily;
	Yearly.Recurrence = EDocRecurrence::Yearly;
	Yearly.Month = 2;
	Yearly.Day = 5;
	TestEqual(TEXT("Yearly Feb 5 (day index 34)"), FDocTimeMath::NextOccurrence(C, Yearly, 0), 34 * Day + 9 * 60 * 60000LL);

	FDocScheduleEntry Impossible = Yearly;
	Impossible.Day = 31; // months are 30 days
	TestEqual(TEXT("Impossible date → none"), FDocTimeMath::NextOccurrence(C, Impossible, 0), static_cast<int64>(-1));

	// Periods: wraparound and empty.
	FDocTimePeriod Night;
	Night.StartMinute = 20 * 60;
	Night.EndMinute = 6 * 60;
	TestTrue(TEXT("Night contains 23:00"), Night.ContainsMinute(23 * 60));
	TestTrue(TEXT("Night contains 02:00"), Night.ContainsMinute(2 * 60));
	TestFalse(TEXT("Night excludes 12:00"), Night.ContainsMinute(12 * 60));
	TestFalse(TEXT("Night end is exclusive"), Night.ContainsMinute(6 * 60));
	FDocTimePeriod Empty;
	Empty.StartMinute = Empty.EndMinute = 100;
	TestFalse(TEXT("Empty period contains nothing"), Empty.ContainsMinute(100));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
