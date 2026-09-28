#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/StrongObjectPtr.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "DocRhythmChallengeSubsystem.h"
#include "DocRhythmChart.h"
#include "DocRhythmJudgmentProfile.h"
#include "DocRhythmAudioPlayback.h"
#include "Tests/DocRhythmTestTypes.h"

namespace DocRhythmTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr int64 Sec = 1000000;

	FDocRhythmNote Tap(FName Id, int64 TimeUs)
	{
		FDocRhythmNote N;
		N.NoteId = Id;
		N.Kind = EDocRhythmNoteKind::Tap;
		N.TimeOffsetUs = TimeUs;
		return N;
	}

	FDocRhythmNote Hold(FName Id, int64 TimeUs, int64 DurationUs)
	{
		FDocRhythmNote N = Tap(Id, TimeUs);
		N.Kind = EDocRhythmNoteKind::Hold;
		N.DurationUs = DurationUs;
		return N;
	}

	UDocRhythmChart* Chart(FName Id, int64 DurationUs, const TArray<FDocRhythmNote>& Notes)
	{
		UDocRhythmChart* C = NewObject<UDocRhythmChart>(GetTransientPackage());
		C->ChartId = Id;
		C->DurationUs = DurationUs;
		C->LaneCount = 1;
		C->BPM = 120.0f;
		C->AudioSourceIdentifier = TEXT("/Game/Test/Track.Track");
		C->Notes = Notes;
		return C;
	}

	UDocRhythmJudgmentProfile* Profile(FName Id = TEXT("Standard"))
	{
		UDocRhythmJudgmentProfile* P = NewObject<UDocRhythmJudgmentProfile>(GetTransientPackage());
		P->ProfileId = Id;
		return P;
	}

	struct FFixture
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocRhythmChallengeSubsystem> Subsystem;
		TStrongObjectPtr<UDocRhythmMockClock> Clock;
		TStrongObjectPtr<UDocRhythmMockPlayback> Playback;
		int64 Base = 0;

		explicit FFixture(bool bWithPlayback = false)
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			Subsystem.Reset(NewObject<UDocRhythmChallengeSubsystem>(Player.Get()));
			Clock.Reset(NewObject<UDocRhythmMockClock>());
			Playback.Reset(NewObject<UDocRhythmMockPlayback>());
			Subsystem->SetClockProvider(Clock.Get());
			if (bWithPlayback)
			{
				Subsystem->SetPlaybackProvider(Playback.Get());
			}
		}

		~FFixture()
		{
			Subsystem->CancelChallenge();
		}

		UDocRhythmChallengeSubsystem* operator->() const { return Subsystem.Get(); }

		bool Begin()
		{
			Base = Clock->MockTimeUs;
			return Subsystem->BeginChallenge().IsSuccess();
		}

		/** Move the clock to chart time T (plus any extra pause time already added to Base). */
		void At(int64 ChartUs) { Clock->MockTimeUs = FMath::Max(Clock->MockTimeUs, Base + ChartUs); }

		FDocSystemResult Input(EDocRhythmInputType Type, int64 ChartUs, FDocRhythmJudgmentResult& Out, FGuid EventId = FGuid(), int64 Attempt = 0)
		{
			At(ChartUs);
			FDocRhythmInputSample S;
			S.InputType = Type;
			S.MonotonicTimestampUs = Base + ChartUs;
			S.InputEventId = EventId;
			S.AttemptId = Attempt;
			return Subsystem->SubmitInput(S, Out);
		}

		EDocRhythmHitJudgment Press(int64 ChartUs)
		{
			FDocRhythmJudgmentResult Out;
			Input(EDocRhythmInputType::Press, ChartUs, Out);
			return Out.Judgment;
		}

		FDocRhythmJudgmentResult Release(int64 ChartUs)
		{
			FDocRhythmJudgmentResult Out;
			Input(EDocRhythmInputType::Release, ChartUs, Out);
			return Out;
		}

		void Update(int64 ChartUs)
		{
			At(ChartUs);
			Subsystem->UpdateTimeline(Base + ChartUs);
		}

		FDocRhythmResult Result() const
		{
			FDocRhythmResult R;
			Subsystem->GetResult(R);
			return R;
		}
	};
}

// RHY-01
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmNoteWindowsTest, FAutomationTestBase, "Doc.Rhythm.NoteWindows", DocRhythmTests::Flags)
bool FDocRhythmNoteWindowsTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	UDocRhythmJudgmentProfile* P = Profile();
	TestEqual(TEXT("0 Perfect"), P->EvaluateError(0), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("+30000 Perfect (inclusive)"), P->EvaluateError(30000), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("-30000 Perfect (inclusive)"), P->EvaluateError(-30000), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("+30001 Great"), P->EvaluateError(30001), EDocRhythmHitJudgment::Great);
	TestEqual(TEXT("+60000 Great"), P->EvaluateError(60000), EDocRhythmHitJudgment::Great);
	TestEqual(TEXT("+60001 Good"), P->EvaluateError(60001), EDocRhythmHitJudgment::Good);
	TestEqual(TEXT("+100000 Good"), P->EvaluateError(100000), EDocRhythmHitJudgment::Good);
	TestEqual(TEXT("+100001 Miss"), P->EvaluateError(100001), EDocRhythmHitJudgment::Miss);
	TestEqual(TEXT("+150000 Miss"), P->EvaluateError(150000), EDocRhythmHitJudgment::Miss);
	TestEqual(TEXT("+150001 selects nothing"), P->EvaluateError(150001), EDocRhythmHitJudgment::None);
	TestEqual(TEXT("-150001 selects nothing"), P->EvaluateError(-150001), EDocRhythmHitJudgment::None);

	UDocRhythmJudgmentProfile* Asym = Profile(TEXT("Asym"));
	Asym->bUseAsymmetricWindows = true;
	Asym->EarlyWindows.PerfectUs = 10000; Asym->EarlyWindows.GreatUs = 20000; Asym->EarlyWindows.GoodUs = 30000; Asym->EarlyWindows.MissUs = 40000;
	TestTrue(TEXT("Asymmetric profile validates"), Asym->ValidateProfile().IsSuccess());
	TestEqual(TEXT("Early -10000 Perfect"), Asym->EvaluateError(-10000), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Early -10001 Great"), Asym->EvaluateError(-10001), EDocRhythmHitJudgment::Great);
	TestEqual(TEXT("Early -40001 nothing"), Asym->EvaluateError(-40001), EDocRhythmHitJudgment::None);
	TestEqual(TEXT("Late +30000 Perfect"), Asym->EvaluateError(30000), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Late side keeps its own wider windows: +40001 is Great"), Asym->EvaluateError(40001), EDocRhythmHitJudgment::Great);

	FFixture F;
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Windows"), 4 * Sec, { Tap(TEXT("n1"), 1 * Sec), Tap(TEXT("n2"), 2 * Sec), Tap(TEXT("n3"), 3 * Sec) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());
	TestEqual(TEXT("Edge +30000 Perfect in play"), F.Press(1 * Sec + 30000), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Edge +30001 Great in play"), F.Press(2 * Sec + 30001), EDocRhythmHitJudgment::Great);
	TestEqual(TEXT("Beyond the miss window selects nothing"), F.Press(3 * Sec + 150001), EDocRhythmHitJudgment::None);
	F.Update(3 * Sec + 150001);
	const FDocRhythmResult R = F.Result();
	TestEqual(TEXT("Perfect count"), R.PerfectCount, 1);
	TestEqual(TEXT("Great count"), R.GreatCount, 1);
	TestEqual(TEXT("Timeline miss"), R.MissCount, 1);
	TestEqual(TEXT("Integer score"), R.TotalScore, static_cast<int64>(1750));
	return true;
}

// RHY-02
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmInputDeduplicationTest, FAutomationTestBase, "Doc.Rhythm.InputDeduplication", DocRhythmTests::Flags)
bool FDocRhythmInputDeduplicationTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Dedup"), 4 * Sec,
		{ Tap(TEXT("a"), 1000000), Tap(TEXT("b"), 1100000), Tap(TEXT("c"), 2000000), Tap(TEXT("d"), 2120000) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());

	const FGuid EventId = FGuid::NewGuid();
	FDocRhythmJudgmentResult Out;
	TestTrue(TEXT("First press"), F.Input(EDocRhythmInputType::Press, 1050000, Out, EventId).IsChanged());
	TestEqual(TEXT("Earliest hittable note"), Out.NoteId, FName(TEXT("a")));
	TestEqual(TEXT("Same event id is ignored"), F.Input(EDocRhythmInputType::Press, 1050000, Out, EventId).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Same timestamp re-sent is ignored"), F.Input(EDocRhythmInputType::Press, 1050000, Out).Outcome, EDocResultOutcome::NoChange);
	FDocRhythmTimelineInfo Info;
	F->QueryTimeline(Info);
	TestEqual(TEXT("b still pending"), Info.RemainingNotes, 3);
	TestEqual(TEXT("b judged once by its own press"), F.Press(1100000), EDocRhythmHitJudgment::Perfect);

	// A late press for c that is a clean hit for d picks d, not c.
	TestTrue(TEXT("Press"), F.Input(EDocRhythmInputType::Press, 2125000, Out).IsChanged());
	TestEqual(TEXT("Hit preferred over an older note's miss window"), Out.NoteId, FName(TEXT("d")));
	TestEqual(TEXT("d Perfect"), Out.Judgment, EDocRhythmHitJudgment::Perfect);

	TestEqual(TEXT("Wrong attempt refused"), F.Input(EDocRhythmInputType::Press, 2200000, Out, FGuid(), F->GetAttemptId() + 7).Outcome, EDocResultOutcome::Conflict);
	FDocRhythmInputSample Future;
	Future.MonotonicTimestampUs = F.Clock->MockTimeUs + Sec;
	TestEqual(TEXT("Future timestamp refused"), F->SubmitInput(Future, Out).Outcome, EDocResultOutcome::InvalidInput);

	F.Update(3 * Sec);
	const FDocRhythmResult R = F.Result();
	TestEqual(TEXT("Three hits"), R.PerfectCount + R.GreatCount + R.GoodCount, 3);
	TestEqual(TEXT("c missed once"), R.MissCount, 1);
	return true;
}

// RHY-03
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmHoldLifecycleTest, FAutomationTestBase, "Doc.Rhythm.HoldLifecycle", DocRhythmTests::Flags)
bool FDocRhythmHoldLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Holds"), 9 * Sec,
		{ Hold(TEXT("h1"), 1 * Sec, 1 * Sec), Hold(TEXT("h2"), 3 * Sec, 1 * Sec), Hold(TEXT("h3"), 5 * Sec, 1 * Sec), Hold(TEXT("h4"), 7 * Sec, 1 * Sec) }), Profile()).IsSuccess());
	int32 Terminal = 0;
	F->OnHitJudgedNative.AddLambda([&Terminal](const FDocRhythmJudgmentResult& R) { Terminal += R.bHoldStarted ? 0 : 1; });
	TestTrue(TEXT("Begin"), F.Begin());

	FDocRhythmJudgmentResult Out;
	TestEqual(TEXT("Stray release does nothing"), F.Input(EDocRhythmInputType::Release, 500000, Out).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("h1 start"), F.Press(1 * Sec), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Early release breaks the hold"), F.Release(1500000).Judgment, EDocRhythmHitJudgment::Miss);
	TestEqual(TEXT("Second release ignored"), F.Input(EDocRhythmInputType::Release, 1600000, Out).Outcome, EDocResultOutcome::NoChange);
	F.Update(2100000);

	TestEqual(TEXT("h2 start"), F.Press(3 * Sec), EDocRhythmHitJudgment::Perfect);
	const FDocRhythmJudgmentResult H2 = F.Release(3800000);
	TestEqual(TEXT("Release at the 80% mark completes"), H2.Judgment, EDocRhythmHitJudgment::Perfect);
	TestTrue(TEXT("Flagged completed"), H2.bHoldCompleted);

	TestEqual(TEXT("h3 start"), F.Press(5 * Sec), EDocRhythmHitJudgment::Perfect);
	F.Update(6 * Sec);

	TestEqual(TEXT("h4 start"), F.Press(7 * Sec), EDocRhythmHitJudgment::Perfect);
	F->OnFocusLost();
	TestEqual(TEXT("Release after focus loss ignored"), F.Input(EDocRhythmInputType::Release, 7500000, Out).Outcome, EDocResultOutcome::NoChange);
	F.Update(9 * Sec);

	const FDocRhythmResult R = F.Result();
	TestEqual(TEXT("Two completed holds"), R.PerfectCount, 2);
	TestEqual(TEXT("Two broken holds, no per-frame extras"), R.MissCount, 2);
	TestEqual(TEXT("Exactly one terminal judgment per hold"), Terminal, 4);
	TestEqual(TEXT("Completed"), F->GetState(), EDocRhythmChallengeState::Completed);
	return true;
}

// RHY-04
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmCalibrationSignTest, FAutomationTestBase, "Doc.Rhythm.CalibrationSign", DocRhythmTests::Flags)
bool FDocRhythmCalibrationSignTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	FDocRhythmCalibrationSettings Cal;
	const FDocSystemResult Few = F->RunCalibration({ 40000, 41000, 39000, 40000, 40000 }, TEXT("Pad"), Cal);
	TestEqual(TEXT("Too few samples"), Few.Outcome, EDocResultOutcome::NotReady);
	TestTrue(TEXT("Reported as CalibrationInsufficient"), Few.Diagnostic.StartsWith(TEXT("CalibrationInsufficient")));
	TestFalse(TEXT("Nothing invented"), F->GetCalibrationSettings().bCalibrated);
	TestEqual(TEXT("Erratic samples refused"), F->RunCalibration({ -80000, -60000, -40000, -20000, 0, 20000, 40000, 60000, 80000 }, TEXT("Pad"), Cal).Outcome, EDocResultOutcome::NotReady);

	// Device reports inputs ~40 ms late, with two wild outliers.
	TestTrue(TEXT("Calibrate"), F->RunCalibration({ 39000, 41000, 40000, 40500, 39500, 40200, 39800, 40100, 250000, -200000 }, TEXT("Pad"), Cal).IsSuccess());
	TestEqual(TEXT("Positive offset = late inputs"), Cal.InputOffsetUs, static_cast<int64>(40050));
	TestEqual(TEXT("Outliers rejected"), Cal.RejectedSampleCount, 2);
	TestEqual(TEXT("Accepted samples"), Cal.SampleCount, 8);
	TestTrue(TEXT("Uncertainty reported and small"), Cal.UncertaintyUs > 0 && Cal.UncertaintyUs < 5000);

	F->SetVisualOffsetUs(50000);
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Cal"), 3 * Sec, { Tap(TEXT("n1"), 1 * Sec), Tap(TEXT("n2"), 2 * Sec) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());
	FDocRhythmJudgmentResult Out;
	F.Input(EDocRhythmInputType::Press, 1040000, Out);
	TestEqual(TEXT("A 40 ms late tap is corrected to on-time"), Out.Judgment, EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Signed error after correction"), Out.ErrorUs, static_cast<int64>(-50));
	FDocRhythmTimelineInfo Info;
	F->QueryTimeline(Info);
	TestEqual(TEXT("Visual offset only moves presentation"), Info.VisualChartTimeUs - Info.CurrentChartTimeUs, static_cast<int64>(50000));
	FDocRhythmCalibrationSettings Blocked;
	TestEqual(TEXT("No calibration mid-attempt"), F->RunCalibration({ 1, 2, 3, 4, 5, 6, 7, 8 }, TEXT("Pad"), Blocked).Outcome, EDocResultOutcome::Conflict);
	F->CancelChallenge();

	TestTrue(TEXT("Early device"), F->RunCalibration({ -25000, -25000, -25000, -25000, -25000, -25000, -25000, -25000 }, TEXT("Pad"), Cal).IsSuccess());
	TestEqual(TEXT("Negative offset = early inputs"), Cal.InputOffsetUs, static_cast<int64>(-25000));
	F->NotifyInputDeviceChanged(TEXT("OtherPad"));
	TestFalse(TEXT("Other device invalidates the measurement"), F->GetCalibrationSettings().bCalibrated);
	TestEqual(TEXT("Offset cleared"), F->GetCalibrationSettings().InputOffsetUs, static_cast<int64>(0));
	return true;
}

// RHY-05
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmClockAndHitchTest, FAutomationTestBase, "Doc.Rhythm.ClockAndHitch", DocRhythmTests::Flags)
bool FDocRhythmClockAndHitchTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Hitch"), 4 * Sec, { Tap(TEXT("n1"), 1 * Sec), Tap(TEXT("n2"), 2 * Sec), Tap(TEXT("n3"), 2500000) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());

	// 600 ms hitch: the input arrives late but carries its real timestamp.
	F.At(1600000);
	FDocRhythmInputSample Sample;
	Sample.MonotonicTimestampUs = F.Base + 1 * Sec;
	Sample.Source = EDocRhythmTimestampSource::InputSubsystem;
	FDocRhythmJudgmentResult Out;
	TestTrue(TEXT("Late-processed input accepted"), F->SubmitInput(Sample, Out).IsChanged());
	TestEqual(TEXT("Judged at its timestamp, not processing time"), Out.Judgment, EDocRhythmHitJudgment::Perfect);
	F->UpdateTimeline(F.Base + 1600000);
	TestTrue(TEXT("Hitch flagged"), (F.Result().TimingQualityFlags & static_cast<int32>(EDocRhythmTimingQualityFlags::ClockHitchDetected)) != 0);

	// No timestamp: processing time is used and flagged as low precision.
	F.At(2 * Sec);
	FDocRhythmInputSample NoStamp;
	TestTrue(TEXT("Unstamped input accepted"), F->SubmitInput(NoStamp, Out).IsChanged());
	TestTrue(TEXT("Low precision flagged"), (F.Result().TimingQualityFlags & static_cast<int32>(EDocRhythmTimingQualityFlags::LowPrecisionInput)) != 0);

	// If the timeline already passed a note's deadline, a late input cannot be recovered (no invented times).
	F.Update(2800000);
	FDocRhythmInputSample TooLate;
	TooLate.MonotonicTimestampUs = F.Base + 2500000;
	TestEqual(TEXT("Deadline passed before the input arrived"), F->SubmitInput(TooLate, Out).Outcome, EDocResultOutcome::NoChange);
	const FDocRhythmResult R = F.Result();
	TestEqual(TEXT("n3 missed"), R.MissCount, 1);
	TestEqual(TEXT("Degraded timing is its own category"), R.Category, EDocRhythmResultCategory::Degraded);
	return true;
}

// RHY-06
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmPauseResumeTest, FAutomationTestBase, "Doc.Rhythm.PauseResume", DocRhythmTests::Flags)
bool FDocRhythmPauseResumeTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F(/*bWithPlayback*/ true);
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Pause"), 5 * Sec, { Tap(TEXT("n1"), 1 * Sec), Tap(TEXT("n2"), 3 * Sec) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());
	TestEqual(TEXT("Audio started"), F.Playback->StartCount, 1);

	F.Update(500000);
	TestTrue(TEXT("Pause"), F->PauseChallenge().IsChanged());
	TestTrue(TEXT("Audio paused with the chart"), F.Playback->bPaused);
	FDocRhythmJudgmentResult Out;
	TestEqual(TEXT("No input while paused"), F.Input(EDocRhythmInputType::Press, 600000, Out).Outcome, EDocResultOutcome::Conflict);
	F.Clock->MockTimeUs += 10 * Sec;
	const int64 PauseEnd = F.Clock->MockTimeUs;
	TestTrue(TEXT("Resume"), F->ResumeChallenge().IsChanged());
	TestFalse(TEXT("Audio resumed"), F.Playback->bPaused);
	FDocRhythmTimelineInfo Info;
	F->QueryTimeline(Info);
	TestEqual(TEXT("Chart time excludes the pause"), Info.CurrentChartTimeUs, static_cast<int64>(500000));

	FDocRhythmInputSample DuringPause;
	DuringPause.MonotonicTimestampUs = PauseEnd - 5 * Sec;
	TestEqual(TEXT("Late event stamped inside the pause is refused"), F->SubmitInput(DuringPause, Out).Outcome, EDocResultOutcome::Conflict);

	F.Base += 10100000; // the pause lasted 10.1 s (0.5 s -> 10.6 s), so chart time maps that much later
	TestEqual(TEXT("Aligned after resume"), F.Press(1 * Sec), EDocRhythmHitJudgment::Perfect);

	// Audio drift beyond tolerance re-anchors the transport explicitly.
	const int32 Generation = F->GetTransportGeneration();
	F.At(2 * Sec);
	F.Playback->PositionUs = 2 * Sec + 50000;
	F->UpdateTimeline(F.Base + 2 * Sec);
	TestEqual(TEXT("New transport generation"), F->GetTransportGeneration(), Generation + 1);
	F->QueryTimeline(Info);
	TestEqual(TEXT("Chart follows the audio"), Info.CurrentChartTimeUs, static_cast<int64>(2 * Sec + 50000));
	F.Playback->PositionUs = -1;

	F.Playback->bSupportPause = false;
	TestEqual(TEXT("Imprecise backend refuses pause"), F->PauseChallenge().Outcome, EDocResultOutcome::Unsupported);
	TestEqual(TEXT("Still playing"), F->GetState(), EDocRhythmChallengeState::Playing);

	F.Playback->bActive = false;
	F->UpdateTimeline(F.Clock->MockTimeUs + 1000);
	TestEqual(TEXT("Audio loss faults the attempt"), F->GetState(), EDocRhythmChallengeState::TimingFault);
	TestTrue(TEXT("Marked non-comparable"), (F.Result().TimingQualityFlags & static_cast<int32>(EDocRhythmTimingQualityFlags::NonComparable)) != 0);
	return true;
}

// RHY-07
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmRestartGenerationTest, FAutomationTestBase, "Doc.Rhythm.RestartGeneration", DocRhythmTests::Flags)
bool FDocRhythmRestartGenerationTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F(/*bWithPlayback*/ true);
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Restart"), 5 * Sec, { Hold(TEXT("h1"), 1 * Sec, 2 * Sec), Tap(TEXT("t2"), 4 * Sec) }), Profile()).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());
	const int64 OldAttempt = F->GetAttemptId();
	const int32 OldGeneration = F->GetSessionGeneration();
	const int64 OldBase = F.Base;
	TestEqual(TEXT("Hold started"), F.Press(1 * Sec), EDocRhythmHitJudgment::Perfect);
	TestEqual(TEXT("Begin while running is refused"), F->BeginChallenge().Outcome, EDocResultOutcome::Conflict);

	F.At(1500000);
	F.Base = F.Clock->MockTimeUs;
	TestTrue(TEXT("Restart"), F->RestartChallenge().IsSuccess());
	TestTrue(TEXT("New attempt id"), F->GetAttemptId() != OldAttempt);
	TestEqual(TEXT("New generation"), F->GetSessionGeneration(), OldGeneration + 1);
	TestEqual(TEXT("Audio restarted"), F.Playback->StartCount, 2);

	FDocRhythmJudgmentResult Out;
	FDocRhythmInputSample OldRelease;
	OldRelease.InputType = EDocRhythmInputType::Release;
	OldRelease.MonotonicTimestampUs = OldBase + 1200000;
	OldRelease.AttemptId = OldAttempt;
	TestEqual(TEXT("Old-attempt callback refused"), F->SubmitInput(OldRelease, Out).Outcome, EDocResultOutcome::Conflict);
	OldRelease.AttemptId = 0;
	TestEqual(TEXT("Input stamped before the restart refused"), F->SubmitInput(OldRelease, Out).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Release in the new attempt has no hold to affect"), F.Input(EDocRhythmInputType::Release, 100000, Out).Outcome, EDocResultOutcome::NoChange);

	FDocRhythmTimelineInfo Info;
	F->QueryTimeline(Info);
	TestEqual(TEXT("All notes pending again"), Info.RemainingNotes, 2);
	TestEqual(TEXT("Score cleared"), Info.CurrentScore, static_cast<int64>(0));
	TestEqual(TEXT("No misses carried over"), F.Result().MissCount, 0);
	TestEqual(TEXT("New attempt plays normally"), F.Press(1 * Sec), EDocRhythmHitJudgment::Perfect);
	return true;
}

// RHY-08
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmResultIdentityTest, FAutomationTestBase, "Doc.Rhythm.ResultIdentity", DocRhythmTests::Flags)
bool FDocRhythmResultIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	UDocRhythmChart* C = Chart(TEXT("Chart.Identity"), 2 * Sec, { Tap(TEXT("n1"), 1 * Sec) });
	C->ContentHash = TEXT("v3");
	FDocRhythmCalibrationSettings Cal;
	Cal.InputOffsetUs = 1234;
	Cal.bCalibrated = true;
	Cal.DeviceScopeId = TEXT("Pad");
	TestTrue(TEXT("Set calibration"), F->SetCalibrationSettings(Cal).IsSuccess());

	int32 Completions = 0;
	F->OnChallengeCompletedNative.AddLambda([&Completions](const FDocRhythmResult&) { ++Completions; });
	UDocRhythmJudgmentProfile* Strict = Profile(TEXT("Strict"));
	Strict->HitchThresholdUs = 10 * Sec; // this test updates the timeline sparsely on purpose
	TestTrue(TEXT("Load strict"), F->LoadChart(C, Strict).IsSuccess());
	TestTrue(TEXT("Begin"), F.Begin());
	TestEqual(TEXT("Hit"), F.Press(1 * Sec + 1234), EDocRhythmHitJudgment::Perfect);
	F.Update(2 * Sec);
	F.Update(2 * Sec + 500000);
	const FDocRhythmResult R = F.Result();
	TestEqual(TEXT("One completion event"), Completions, 1);
	TestTrue(TEXT("Completed"), R.bCompleted);
	TestEqual(TEXT("Chart id"), R.ChartId, FName(TEXT("Chart.Identity")));
	TestEqual(TEXT("Computed hash present"), R.ContentHash.Len(), 32);
	TestEqual(TEXT("Declared version kept separately"), R.DeclaredChartVersion, FString(TEXT("v3")));
	TestEqual(TEXT("Profile id"), R.ProfileId, FName(TEXT("Strict")));
	TestFalse(TEXT("Profile hash present"), R.ProfileHash.IsEmpty());
	TestEqual(TEXT("Calibration recorded"), R.CalibrationUsed.InputOffsetUs, static_cast<int64>(1234));
	TestEqual(TEXT("Calibration device recorded"), R.CalibrationUsed.DeviceScopeId, FName(TEXT("Pad")));
	TestEqual(TEXT("Strict category"), R.Category, EDocRhythmResultCategory::Strict);
	TestFalse(TEXT("No audible reference claimed"), R.bAudibleReference);
	TestEqual(TEXT("Max score"), R.MaxPossibleScore, static_cast<int64>(1000));
	TestEqual(TEXT("Accuracy"), R.AccuracyPercent, 100.0f);
	TestEqual(TEXT("Attempt id"), R.AttemptId, F->GetAttemptId());

	UDocRhythmJudgmentProfile* Assisted = Profile(TEXT("Assisted"));
	Assisted->bIsAssisted = true;
	TestTrue(TEXT("Load assisted"), F->LoadChart(C, Assisted).IsSuccess());
	TestTrue(TEXT("Begin assisted"), F.Begin());
	F.Press(1 * Sec + 1234);
	F.Update(2 * Sec);
	const FDocRhythmResult A = F.Result();
	TestEqual(TEXT("Assisted category"), A.Category, EDocRhythmResultCategory::Assisted);
	TestTrue(TEXT("Assistance recorded"), A.bHasAssistance);
	TestTrue(TEXT("Different profile hash"), A.ProfileHash != R.ProfileHash);

	UDocRhythmChart* Moved = Chart(TEXT("Chart.Identity"), 2 * Sec, { Tap(TEXT("n1"), 1 * Sec + 1) });
	TestTrue(TEXT("Hash follows note timing"), Moved->ComputeContentHash() != C->ComputeContentHash());
	return true;
}

// RHY-09
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmEmptyInvalidChartTest, FAutomationTestBase, "Doc.Rhythm.EmptyInvalidChart", DocRhythmTests::Flags)
bool FDocRhythmEmptyInvalidChartTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	auto Invalid = [](UDocRhythmChart* C) { return C->ValidateChart().Outcome == EDocResultOutcome::InvalidConfiguration; };
	TestTrue(TEXT("Empty chart"), Invalid(Chart(TEXT("E"), Sec, {})));
	TestTrue(TEXT("Duplicate ids"), Invalid(Chart(TEXT("D"), 3 * Sec, { Tap(TEXT("x"), Sec), Tap(TEXT("x"), 2 * Sec) })));
	FDocRhythmNote WrongLane = Tap(TEXT("l"), Sec);
	WrongLane.LaneId = 3;
	TestTrue(TEXT("Lane out of range"), Invalid(Chart(TEXT("L"), 3 * Sec, { WrongLane })));
	TestTrue(TEXT("Hold must end after start"), Invalid(Chart(TEXT("H"), 3 * Sec, { Hold(TEXT("h"), Sec, 0) })));
	TestTrue(TEXT("Tap inside a same-lane hold is ambiguous"), Invalid(Chart(TEXT("O"), 5 * Sec, { Hold(TEXT("h"), Sec, 2 * Sec), Tap(TEXT("t"), 2 * Sec) })));
	TestTrue(TEXT("Simultaneous taps are ambiguous"), Invalid(Chart(TEXT("S"), 3 * Sec, { Tap(TEXT("a"), Sec), Tap(TEXT("b"), Sec) })));
	TestTrue(TEXT("Hold past the end"), Invalid(Chart(TEXT("P"), 2 * Sec, { Hold(TEXT("h"), Sec, 2 * Sec) })));
	UDocRhythmChart* Mismatch = Chart(TEXT("M"), 3 * Sec, { Tap(TEXT("a"), Sec) });
	Mismatch->AudioDurationUs = 5 * Sec;
	TestTrue(TEXT("Audio duration mismatch is a validation issue"), Invalid(Mismatch));
	TArray<FDocRhythmNote> TooMany;
	TooMany.SetNum(UDocRhythmChart::MaxNotes + 1);
	TestTrue(TEXT("Note count bound"), Invalid(Chart(TEXT("Big"), 3 * Sec, TooMany)));

	UDocRhythmJudgmentProfile* Overflow = Profile(TEXT("Overflow"));
	Overflow->PerfectPoints = 2000000000;
	TestEqual(TEXT("Points bound prevents overflow"), Overflow->ValidateProfile().Outcome, EDocResultOutcome::InvalidConfiguration);
	UDocRhythmJudgmentProfile* Descending = Profile(TEXT("Desc"));
	Descending->GreatHalfWindowUs = 10000;
	TestEqual(TEXT("Windows must not shrink"), Descending->ValidateProfile().Outcome, EDocResultOutcome::InvalidConfiguration);

	FFixture F;
	TestEqual(TEXT("Null chart"), F->LoadChart(nullptr).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Invalid chart refused"), F->LoadChart(Chart(TEXT("E"), Sec, {})).Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Invalid profile refused"), F->LoadChart(Chart(TEXT("Ok"), 2 * Sec, { Tap(TEXT("a"), Sec) }), Overflow).Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Nothing loaded"), F->GetState(), EDocRhythmChallengeState::Inactive);
	TestEqual(TEXT("Cannot begin"), F->BeginChallenge().Outcome, EDocResultOutcome::NotReady);
	FDocRhythmResult R;
	TestEqual(TEXT("No result without a chart"), F->GetResult(R).Outcome, EDocResultOutcome::NotReady);
	return true;
}

// RHY-10
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRhythmCookedAudibleRunTest, FAutomationTestBase, "Doc.Rhythm.CookedAudibleRun", DocRhythmTests::Flags)
bool FDocRhythmCookedAudibleRunTest::RunTest(const FString& Parameters)
{
	using namespace DocRhythmTests;
	FFixture F;
	UDocRhythmAudioComponentPlayback* Audio = NewObject<UDocRhythmAudioComponentPlayback>(GetTransientPackage());
	F->SetPlaybackProvider(Audio);
	TestTrue(TEXT("Load"), F->LoadChart(Chart(TEXT("Chart.Audible"), 2 * Sec, { Tap(TEXT("a"), Sec) }), Profile()).IsSuccess());
	TestEqual(TEXT("Without a world/sound the native path fails explicitly"), F->BeginChallenge().Outcome, EDocResultOutcome::Unavailable);
	TestEqual(TEXT("Attempt did not start"), F->GetState(), EDocRhythmChallengeState::Ready);
	TestFalse(TEXT("Failure reason recorded"), Audio->LastError.IsEmpty());
	TestFalse(TEXT("Native path does not claim precise pause"), Audio->SupportsPauseResume());
	AddInfo(TEXT("RHY-10: a packaged, audible tap/hold run with recorded timing evidence is a manual gate and is not verified by automation."));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
