#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocRaceTimingTypes.h"
#include "DocRaceCourseDefinition.h"
#include "DocRaceComponents.h"
#include "DocRaceTimingSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocRaceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	static FDocRaceGateDefinition MakeGate(FName Id, float X)
	{
		FDocRaceGateDefinition Gate;
		Gate.GateId = Id;
		Gate.Location = FVector(X, 0.0f, 0.0f);
		Gate.ForwardDirection = FVector(1.0f, 0.0f, 0.0f);
		Gate.Width = 1000.0f;
		Gate.Height = 500.0f;
		return Gate;
	}

	// Three +X gates at X = 0, 1000, 2000.
	static UDocRaceCourseDefinition* MakeCourse(int32 Version = 1)
	{
		UDocRaceCourseDefinition* CourseDef = NewObject<UDocRaceCourseDefinition>();
		CourseDef->CourseId = TEXT("Course_Speedway");
		CourseDef->CourseVersion = Version;
		FDocRaceGateDefinition Start = MakeGate(TEXT("Gate_Start"), 0.0f);
		Start.bIsStartGate = true;
		FDocRaceGateDefinition Finish = MakeGate(TEXT("Gate_Finish"), 2000.0f);
		Finish.bIsFinishGate = true;
		CourseDef->OrderedGates.Add(Start);
		CourseDef->OrderedGates.Add(MakeGate(TEXT("Gate_Mid"), 1000.0f));
		CourseDef->OrderedGates.Add(Finish);
		return CourseDef;
	}

	struct FFixture
	{
		FDocScopedTestWorld ScopedWorld;
		UDocRaceTimingSubsystem* Subsystem = nullptr;
		UDocRaceCourseDefinition* CourseDef = nullptr;

		FFixture()
		{
			UWorld* World = ScopedWorld.World;
			check(World);
			Subsystem = World->GetSubsystem<UDocRaceTimingSubsystem>();
			check(Subsystem);
			CourseDef = MakeCourse();
		}
	};

	// One clean lap starting at T0: start crossed at T0+0.5, mid at T0+2.5, finish at T0+4.5.
	static void DriveLap(UDocRaceTimingSubsystem* S, const FGuid& RunId, double T0)
	{
		S->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), T0);
		S->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), T0 + 1.0);
		S->SubmitPositionSample(RunId, FVector(950.0f, 0.0f, 0.0f), T0 + 2.0);
		S->SubmitPositionSample(RunId, FVector(1050.0f, 0.0f, 0.0f), T0 + 3.0);
		S->SubmitPositionSample(RunId, FVector(1950.0f, 0.0f, 0.0f), T0 + 4.0);
		S->SubmitPositionSample(RunId, FVector(2050.0f, 0.0f, 0.0f), T0 + 5.0);
	}
}

// RAC-01: Forward/reverse/inside-start cases follow declared crossing rules
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceDirectionalGateTest, FAutomationTestBase, "Doc.Race.DirectionalGate", DocRaceTests::Flags)
bool FDocRaceDirectionalGateTest::RunTest(const FString& Parameters)
{
	FDocRaceGateDefinition Gate;
	Gate.GateId = TEXT("Gate_Test");
	Gate.Location = FVector(0.0f, 0.0f, 0.0f);
	Gate.ForwardDirection = FVector(1.0f, 0.0f, 0.0f); // facing +X
	Gate.Width = 500.0f;
	Gate.Height = 500.0f;

	float Frac = 0.0f;
	FVector CrossPoint;

	TestTrue("Forward crossing succeeds", Gate.TestSweptCrossing(FVector(-50.0f, 0.0f, 0.0f), FVector(50.0f, 0.0f, 0.0f), Frac, CrossPoint));
	TestTrue("Crossing fraction is 0.5", FMath::IsNearlyEqual(Frac, 0.5f, 0.01f));
	TestTrue("Crossing point at origin", CrossPoint.Equals(FVector::ZeroVector, 0.1f));
	TestFalse("Reverse crossing rejected", Gate.TestSweptCrossing(FVector(50.0f, 0.0f, 0.0f), FVector(-50.0f, 0.0f, 0.0f), Frac, CrossPoint));
	TestFalse("No crossing plane rejected", Gate.TestSweptCrossing(FVector(-50.0f, 0.0f, 0.0f), FVector(-10.0f, 0.0f, 0.0f), Frac, CrossPoint));
	TestFalse("Crossing outside gate width rejected", Gate.TestSweptCrossing(FVector(-50.0f, 1000.0f, 0.0f), FVector(50.0f, 1000.0f, 0.0f), Frac, CrossPoint));

	// Inside-start: a segment that begins on the plane or already in front of it is not a crossing.
	TestFalse("Start exactly on the plane is not a crossing", Gate.TestSweptCrossing(FVector(0.0f, 0.0f, 0.0f), FVector(50.0f, 0.0f, 0.0f), Frac, CrossPoint));
	TestFalse("Start already past the plane is not a crossing", Gate.TestSweptCrossing(FVector(10.0f, 0.0f, 0.0f), FVector(80.0f, 0.0f, 0.0f), Frac, CrossPoint));
	TestFalse("Non-finite sample rejected", Gate.TestSweptCrossing(FVector(-50.0f, 0.0f, 0.0f), FVector(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f), Frac, CrossPoint));

	// A run whose participant spawns inside the start plane does not start until a real forward crossing.
	DocRaceTests::FFixture F;
	FGuid RunId;
	TestTrue("BeginRun", F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId));
	F.Subsystem->SubmitPositionSample(RunId, FVector(0.0f, 0.0f, 0.0f), 1.0);
	F.Subsystem->SubmitPositionSample(RunId, FVector(40.0f, 0.0f, 0.0f), 2.0);
	FDocRaceRun Run;
	F.Subsystem->QueryRun(RunId, Run);
	TestEqual("Inside start does not start the run", Run.State, EDocRaceState::Ready);
	TestEqual("Inside start grants no split", Run.Splits.Num(), 0);
	return true;
}

// RAC-02: A valid high-speed segment crossing is detected without overlap events
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceSweptHighSpeedTest, FAutomationTestBase, "Doc.Race.SweptHighSpeed", DocRaceTests::Flags)
bool FDocRaceSweptHighSpeedTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FGuid RunId;
	TestTrue("BeginRun", F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId));

	TestTrue("Initial sample", F.Subsystem->SubmitPositionSample(RunId, FVector(-500.0f, 0.0f, 0.0f), 1.0));
	TestTrue("High speed jump across gate 0", F.Subsystem->SubmitPositionSample(RunId, FVector(500.0f, 0.0f, 0.0f), 1.1));

	FDocRaceRun Run;
	TestTrue("QueryRun", F.Subsystem->QueryRun(RunId, Run));
	TestEqual(TEXT("Running start begins on the start-plane crossing"), Run.State, EDocRaceState::Running);
	TestEqual(TEXT("Gate 0 crossed"), Run.ExpectedGateIndex, 1);
	TestEqual(TEXT("One split recorded"), Run.Splits.Num(), 1);
	TestEqual(TEXT("Split gate is Gate_Start"), Run.Splits[0].GateId, FName(TEXT("Gate_Start")));
	TestTrue(TEXT("Clock starts at the interpolated crossing time (1.05)"), FMath::IsNearlyEqual(Run.RunStartTime, 1.05, 1e-6));
	TestTrue(TEXT("Elapsed is on the same clock as the samples"), FMath::IsNearlyEqual(Run.ElapsedTime, 0.05f, 1e-4f));

	// Out-of-order and non-finite timestamps are refused rather than corrupting the clock.
	TestFalse(TEXT("Sample older than the last one refused"), F.Subsystem->SubmitPositionSample(RunId, FVector(600.0f, 0.0f, 0.0f), 1.05));
	TestFalse(TEXT("Duplicate timestamp refused"), F.Subsystem->SubmitPositionSample(RunId, FVector(600.0f, 0.0f, 0.0f), 1.1));
	TestFalse(TEXT("NaN position refused"), F.Subsystem->SubmitPositionSample(RunId, FVector(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f), 1.2));
	return true;
}

// RAC-03: Multiple crossings are ordered by segment fraction and validated course order
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceMultiGateSegmentTest, FAutomationTestBase, "Doc.Race.MultiGateSegment", DocRaceTests::Flags)
bool FDocRaceMultiGateSegmentTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FGuid RunId;
	TestTrue("BeginRun", F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId));

	F.Subsystem->SubmitPositionSample(RunId, FVector(-100.0f, 0.0f, 0.0f), 1.0);
	// One segment spanning gate 0 (fraction 1/16) and gate 1 (fraction 11/16).
	TestTrue("Sweep across Gate 0 and Gate 1", F.Subsystem->SubmitPositionSample(RunId, FVector(1500.0f, 0.0f, 0.0f), 2.0));

	TArray<FDocRaceSplit> Splits;
	TestTrue("QuerySplits", F.Subsystem->QuerySplits(RunId, Splits));
	TestEqual(TEXT("Two splits recorded"), Splits.Num(), 2);
	if (Splits.Num() == 2)
	{
		TestEqual(TEXT("First split is Gate_Start"), Splits[0].GateId, FName(TEXT("Gate_Start")));
		TestEqual(TEXT("Second split is Gate_Mid"), Splits[1].GateId, FName(TEXT("Gate_Mid")));
		TestTrue(TEXT("Start split is time zero"), FMath::IsNearlyEqual(Splits[0].SplitTimeSeconds, 0.0f, 1e-4f));
		TestTrue(TEXT("Mid split is interpolated (0.625 s)"), FMath::IsNearlyEqual(Splits[1].SplitTimeSeconds, 0.625f, 1e-4f));
		TestTrue(TEXT("Segment duration is split-to-split"), FMath::IsNearlyEqual(Splits[1].SegmentDurationSeconds, 0.625f, 1e-4f));
	}
	return true;
}

// RAC-04: Teleport/respawn/rebase cannot grant intervening checkpoints
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceTeleportDiscontinuityTest, FAutomationTestBase, "Doc.Race.TeleportDiscontinuity", DocRaceTests::Flags)
bool FDocRaceTeleportDiscontinuityTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FGuid RunId;
	TestTrue("BeginRun", F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId));

	// Start properly, then teleport across the rest of the track.
	F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 1.0);
	F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 2.0);
	TestTrue("Notify discontinuity", F.Subsystem->NotifyDiscontinuity(RunId));
	TestTrue("Submit post-teleport sample", F.Subsystem->SubmitPositionSample(RunId, FVector(2500.0f, 0.0f, 0.0f), 2.1));

	FDocRaceRun Run;
	F.Subsystem->QueryRun(RunId, Run);
	TestEqual("No gates granted by teleport", Run.ExpectedGateIndex, 1);
	TestEqual("Only the start split exists", Run.Splits.Num(), 1);
	TestEqual("Teleport does not finish the run", Run.State, EDocRaceState::Running);
	return true;
}

// RAC-05: Countdown, false start, and practice/competitive pause policies are distinct
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceStartAndPauseTest, FAutomationTestBase, "Doc.Race.StartAndPause", DocRaceTests::Flags)
bool FDocRaceStartAndPauseTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FDocRaceRun Run;

	// Running-start courses have no countdown.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("New run is Ready", Run.State, EDocRaceState::Ready);
		TestFalse("Countdown refused on a running-start course", F.Subsystem->StartCountdown(RunId, 10.0));
	}

	auto MakeStanding = [](EDocFalseStartPolicy Policy)
	{
		UDocRaceCourseDefinition* C = DocRaceTests::MakeCourse();
		C->bRunningStart = false;
		C->FalseStartPolicy = Policy;
		C->FalseStartPenaltySeconds = 5.0f;
		return C;
	};

	// Standing start, RejectStart: an early crossing is not counted; the clock starts at go.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), MakeStanding(EDocFalseStartPolicy::RejectStart), RunId);
		TestTrue("Countdown starts", F.Subsystem->StartCountdown(RunId, 10.0));
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 8.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 9.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Reject: still in countdown", Run.State, EDocRaceState::Countdown);
		TestEqual("Reject: early crossing not counted", Run.Splits.Num(), 0);
		TestEqual("Reject: false start recorded", Run.FalseStartCount, 1);

		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 10.5); // back behind the line, after go
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 11.5);  // crosses at 11.0
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Reject: running after go", Run.State, EDocRaceState::Running);
		TestTrue("Reject: clock started at the go boundary", FMath::IsNearlyEqual(Run.RunStartTime, 10.0, 1e-6));
		TestTrue("Reject: start split measured from go (1.0 s)", Run.Splits.Num() == 1 && FMath::IsNearlyEqual(Run.Splits[0].SplitTimeSeconds, 1.0f, 1e-4f));
		TestEqual("Reject: no penalty", Run.AppliedPenalties.Num(), 0);
	}

	// Standing start, Penalty: the crossing counts and exactly one penalty is recorded.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), MakeStanding(EDocFalseStartPolicy::Penalty), RunId);
		F.Subsystem->StartCountdown(RunId, 10.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 8.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 9.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Penalty: crossing counted", Run.ExpectedGateIndex, 1);
		TestEqual("Penalty: one penalty", Run.AppliedPenalties.Num(), 1);
		TestTrue("Penalty: 5 s", Run.AppliedPenalties.Num() == 1 && FMath::IsNearlyEqual(Run.AppliedPenalties[0].PenaltySeconds, 5.0f));
		TestTrue("Penalty: clock still starts at go", FMath::IsNearlyEqual(Run.RunStartTime, 10.0, 1e-6));
	}

	// Standing start, Invalidate: the run ends Invalid.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), MakeStanding(EDocFalseStartPolicy::Invalidate), RunId);
		F.Subsystem->StartCountdown(RunId, 10.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 8.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 9.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Invalidate: run Invalid", Run.State, EDocRaceState::Invalid);
		TestEqual("Invalidate: reason", Run.StatusReason, FName(TEXT("FalseStart")));
	}

	// Competitive runs refuse pause: the clock keeps running.
	{
		FGuid RunId;
		TestEqual("Default pause policy is competitive", F.CourseDef->PausePolicy, EDocRacePausePolicy::CompetitiveContinuous);
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId, 1, TEXT("Pro"));
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 1.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 2.0);
		TestFalse("Competitive pause refused", F.Subsystem->PauseRun(RunId, 2.5));
	}

	// Practice runs pause; the paused time is excluded and the result is a separate practice category.
	{
		UDocRaceCourseDefinition* Practice = DocRaceTests::MakeCourse();
		Practice->PausePolicy = EDocRacePausePolicy::PracticeAllowPause;
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), Practice, RunId, 1, TEXT("Practice"));
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 1.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 2.0); // start at 1.5
		TestTrue("Practice pause", F.Subsystem->PauseRun(RunId, 2.0));
		TestFalse("Samples refused while paused", F.Subsystem->SubmitPositionSample(RunId, FVector(900.0f, 0.0f, 0.0f), 50.0));
		TestTrue("Practice resume after 100 s", F.Subsystem->ResumeRun(RunId, 102.0));
		F.Subsystem->SubmitPositionSample(RunId, FVector(950.0f, 0.0f, 0.0f), 103.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1050.0f, 0.0f, 0.0f), 104.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1950.0f, 0.0f, 0.0f), 105.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(2050.0f, 0.0f, 0.0f), 106.0); // finish at 105.5
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Practice run finished", Run.State, EDocRaceState::Finished);
		TestTrue("Paused 100 s excluded (104.0 - 100 = 4.0)", FMath::IsNearlyEqual(Run.ElapsedTime, 4.0f, 1e-3f));
		FDocRaceResult Res;
		TestTrue("Practice result committed", F.Subsystem->FinalizeResult(RunId, Res));
		TestTrue("Result is practice category", Res.bPracticeResult);
		FDocRaceResult Best;
		TestFalse("No competitive PB from a paused run", F.Subsystem->QueryPersonalBest(Practice->CourseId, 1, TEXT("Practice"), Best, false));
		TestTrue("Practice PB kept separately", F.Subsystem->QueryPersonalBest(Practice->CourseId, 1, TEXT("Practice"), Best, true));
	}
	return true;
}

// RAC-06: Finish/lap cannot bypass required checkpoints
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceLapAndMissingGateTest, FAutomationTestBase, "Doc.Race.LapAndMissingGate", DocRaceTests::Flags)
bool FDocRaceLapAndMissingGateTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FDocRaceRun Run;

	// Skipping the middle gate invalidates the run at the finish.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 1.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 2.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(500.0f, 2000.0f, 0.0f), 3.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1500.0f, 2000.0f, 0.0f), 4.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1950.0f, 0.0f, 0.0f), 5.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(2050.0f, 0.0f, 0.0f), 6.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Run marked Invalid due to missed gate", Run.State, EDocRaceState::Invalid);
		TestEqual("Reason is MissedGate", Run.StatusReason, FName(TEXT("MissedGate")));
	}

	// Jitter back and forth across a gate already passed does not invalidate the run.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 1.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 2.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-5.0f, 0.0f, 0.0f), 2.1);
		F.Subsystem->SubmitPositionSample(RunId, FVector(5.0f, 0.0f, 0.0f), 2.2);
		F.Subsystem->SubmitPositionSample(RunId, FVector(-5.0f, 0.0f, 0.0f), 2.3);
		F.Subsystem->SubmitPositionSample(RunId, FVector(5.0f, 0.0f, 0.0f), 2.4);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Jitter keeps the run Running", Run.State, EDocRaceState::Running);
		TestEqual("Jitter grants no extra split", Run.Splits.Num(), 1);
		F.Subsystem->SubmitPositionSample(RunId, FVector(950.0f, 0.0f, 0.0f), 3.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1050.0f, 0.0f, 0.0f), 4.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(1950.0f, 0.0f, 0.0f), 5.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(2050.0f, 0.0f, 0.0f), 6.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Jittered run still finishes", Run.State, EDocRaceState::Finished);
	}

	// Two laps: the second lap must pass every gate again; skipping one invalidates.
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId, 2);
		DocRaceTests::DriveLap(F.Subsystem, RunId, 1.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("After lap 1 still running", Run.State, EDocRaceState::Running);
		TestEqual("Lap counter advanced", Run.CurrentLap, 2);
		F.Subsystem->NotifyDiscontinuity(RunId);                // respawn to the start
		F.Subsystem->SubmitPositionSample(RunId, FVector(-50.0f, 0.0f, 0.0f), 7.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(50.0f, 0.0f, 0.0f), 8.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(500.0f, 2000.0f, 0.0f), 9.0);   // around gate 1
		F.Subsystem->SubmitPositionSample(RunId, FVector(1950.0f, 0.0f, 0.0f), 10.0);
		F.Subsystem->SubmitPositionSample(RunId, FVector(2050.0f, 0.0f, 0.0f), 11.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Lap 2 bypass invalidates", Run.State, EDocRaceState::Invalid);
	}
	{
		FGuid RunId;
		F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId, 2);
		DocRaceTests::DriveLap(F.Subsystem, RunId, 1.0);
		F.Subsystem->NotifyDiscontinuity(RunId);
		DocRaceTests::DriveLap(F.Subsystem, RunId, 7.0);
		F.Subsystem->QueryRun(RunId, Run);
		TestEqual("Two full laps finish", Run.State, EDocRaceState::Finished);
		TestEqual("Six splits", Run.Splits.Num(), 6);
		TestTrue("Elapsed from first start to last finish (11.5 - 1.5)", FMath::IsNearlyEqual(Run.ElapsedTime, 10.0f, 1e-3f));
	}
	return true;
}

// RAC-07: Duplicate penalties/finish requests do not change committed results twice
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRacePenaltyIdempotencyTest, FAutomationTestBase, "Doc.Race.PenaltyIdempotency", DocRaceTests::Flags)
bool FDocRacePenaltyIdempotencyTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FGuid RunId;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, RunId);

	TestTrue("Apply penalty", F.Subsystem->ApplyPenalty(RunId, TEXT("CornerCut"), 3.0f, TEXT("Corner cut")));
	TestTrue("Duplicate penalty accepted as no-op", F.Subsystem->ApplyPenalty(RunId, TEXT("CornerCut"), 3.0f, TEXT("Corner cut")));
	TestFalse("Negative penalty refused", F.Subsystem->ApplyPenalty(RunId, TEXT("Neg"), -1.0f));
	TestFalse("Non-finite penalty refused", F.Subsystem->ApplyPenalty(RunId, TEXT("Inf"), std::numeric_limits<float>::infinity()));
	TestFalse("Over-limit penalty refused", F.Subsystem->ApplyPenalty(RunId, TEXT("Huge"), 1.0e6f));

	FDocRaceResult Early;
	TestFalse("Finalize refused while the run is live", F.Subsystem->FinalizeResult(RunId, Early));

	DocRaceTests::DriveLap(F.Subsystem, RunId, 1.0);
	FDocRaceRun Run;
	F.Subsystem->QueryRun(RunId, Run);
	TestEqual("Duplicate penalty not applied twice", Run.AppliedPenalties.Num(), 1);
	TestTrue("Finish commits the result", Run.bResultCommitted);

	FDocRaceResult Res1, Res2;
	TestTrue("FinalizeResult first call", F.Subsystem->FinalizeResult(RunId, Res1));
	TestTrue("Penalty after commit refused", !F.Subsystem->ApplyPenalty(RunId, TEXT("Late"), 2.0f));
	TestTrue("FinalizeResult second call", F.Subsystem->FinalizeResult(RunId, Res2));
	TestTrue("Final time is elapsed plus penalties (4 + 3)", FMath::IsNearlyEqual(Res1.FinalTimeSeconds, 7.0f, 1e-3f));
	TestEqual("Finalize is idempotent", Res1.FinalTimeSeconds, Res2.FinalTimeSeconds);
	TestEqual("One stored result", F.Subsystem->CaptureResults().Num(), 1);

	// An aborted run commits once, with no reward.
	FGuid AbortId;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, AbortId);
	TestTrue("Abort", F.Subsystem->AbortRun(AbortId));
	TestFalse("Second abort refused", F.Subsystem->AbortRun(AbortId));
	FDocRaceResult Aborted;
	TestTrue("Aborted run finalizes", F.Subsystem->FinalizeResult(AbortId, Aborted));
	TestFalse("Aborted run grants no reward", Aborted.bRewardGranted);
	TestEqual("Two stored results", F.Subsystem->CaptureResults().Num(), 2);
	return true;
}

// RAC-08: Personal best comparisons respect course/rules/assistance versions
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceRecordCompatibilityTest, FAutomationTestBase, "Doc.Race.RecordCompatibility", DocRaceTests::Flags)
bool FDocRaceRecordCompatibilityTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;

	FGuid Run1Id;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, Run1Id, 1, TEXT("Pro"));
	DocRaceTests::DriveLap(F.Subsystem, Run1Id, 1.0);

	FDocRaceResult PB_V1;
	TestTrue("PB on v1 exists", F.Subsystem->QueryPersonalBest(F.CourseDef->CourseId, 1, TEXT("Pro"), PB_V1));
	TestTrue("PB marked true", PB_V1.bIsPersonalBest);
	TestTrue("PB time is the real elapsed time (4.0 s)", FMath::IsNearlyEqual(PB_V1.FinalTimeSeconds, 4.0f, 1e-3f));

	// A faster run on version 2 of the course does not replace the version 1 record.
	UDocRaceCourseDefinition* V2 = DocRaceTests::MakeCourse(2);
	FGuid Run2Id;
	F.Subsystem->BeginRun(FGuid::NewGuid(), V2, Run2Id, 1, TEXT("Pro"));
	F.Subsystem->SubmitPositionSample(Run2Id, FVector(-50.0f, 0.0f, 0.0f), 20.0);
	F.Subsystem->SubmitPositionSample(Run2Id, FVector(2050.0f, 0.0f, 0.0f), 21.0);
	FDocRaceResult PB_V2;
	TestTrue("v2 has its own PB", F.Subsystem->QueryPersonalBest(V2->CourseId, 2, TEXT("Pro"), PB_V2));
	F.Subsystem->QueryPersonalBest(F.CourseDef->CourseId, 1, TEXT("Pro"), PB_V1);
	TestEqual("v1 PB unchanged by v2 run", PB_V1.RunId, Run1Id);

	// A slower run in the same category is not a PB.
	FGuid Run3Id;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, Run3Id, 1, TEXT("Pro"));
	F.Subsystem->SubmitPositionSample(Run3Id, FVector(-50.0f, 0.0f, 0.0f), 30.0);
	F.Subsystem->SubmitPositionSample(Run3Id, FVector(50.0f, 0.0f, 0.0f), 31.0);
	F.Subsystem->SubmitPositionSample(Run3Id, FVector(1050.0f, 0.0f, 0.0f), 40.0);
	F.Subsystem->SubmitPositionSample(Run3Id, FVector(2050.0f, 0.0f, 0.0f), 50.0);
	FDocRaceResult Slow;
	F.Subsystem->FinalizeResult(Run3Id, Slow);
	TestFalse("Slower run is not a PB", Slow.bIsPersonalBest);

	FDocRaceResult PB_Amateur;
	TestFalse("No PB for Amateur category", F.Subsystem->QueryPersonalBest(F.CourseDef->CourseId, 1, TEXT("Amateur"), PB_Amateur));
	return true;
}

// RAC-09: Restore does not manufacture a valid competitive resume or repeat rewards
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceRestoreNoRewardsTest, FAutomationTestBase, "Doc.Race.RestoreNoRewards", DocRaceTests::Flags)
bool FDocRaceRestoreNoRewardsTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;
	FGuid DoneId;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, DoneId);
	DocRaceTests::DriveLap(F.Subsystem, DoneId, 1.0);

	// A second run is mid-course when the game is saved.
	FGuid MidId;
	F.Subsystem->BeginRun(FGuid::NewGuid(), F.CourseDef, MidId);
	F.Subsystem->SubmitPositionSample(MidId, FVector(-50.0f, 0.0f, 0.0f), 10.0);
	F.Subsystem->SubmitPositionSample(MidId, FVector(50.0f, 0.0f, 0.0f), 11.0);
	F.Subsystem->SubmitPositionSample(MidId, FVector(1050.0f, 0.0f, 0.0f), 12.0);

	TArray<FDocRaceResult> SavedResults = F.Subsystem->CaptureResults();
	TArray<FDocRaceRun> SavedRuns = F.Subsystem->CaptureActiveRuns();
	TestEqual("Saved results count", SavedResults.Num(), 1);
	TestEqual("One live run captured", SavedRuns.Num(), 1);
	TestTrue("Reward was granted on original finish", SavedResults.Num() == 1 && SavedResults[0].bRewardGranted);

	// Restore into a fresh world, with a duplicated result record.
	DocRaceTests::FFixture G;
	TArray<FDocRaceResult> WithDuplicate = SavedResults;
	WithDuplicate.Append(SavedResults);
	G.Subsystem->StageRestore(WithDuplicate, SavedRuns);

	TestEqual("Duplicate result records collapse to one", G.Subsystem->CaptureResults().Num(), 1);
	FDocRaceResult PB;
	TestTrue("PB rebuilt from restored results", G.Subsystem->QueryPersonalBest(F.CourseDef->CourseId, 1, TEXT("Default"), PB));

	FDocRaceRun Restored;
	TestTrue("Mid-run state restored", G.Subsystem->QueryRun(MidId, Restored));
	TestEqual("Mid-run restore is Aborted, not a resume", Restored.State, EDocRaceState::Aborted);
	TestEqual("Reason recorded", Restored.StatusReason, FName(TEXT("RestoredMidRun")));
	TestFalse("Restored run accepts no samples", G.Subsystem->SubmitPositionSample(MidId, FVector(2050.0f, 0.0f, 0.0f), 13.0));

	FDocRaceResult MidResult;
	TestTrue("Aborted run can be finalized", G.Subsystem->FinalizeResult(MidId, MidResult));
	TestFalse("No reward for a restored mid-run", MidResult.bRewardGranted);
	TestFalse("Not a personal best", MidResult.bIsPersonalBest);
	TestEqual("Two results after finalizing", G.Subsystem->CaptureResults().Num(), 2);
	return true;
}

// RAC-10: Two different participant types work without vehicle/movement dependencies
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocRaceMovementAgnosticTest, FAutomationTestBase, "Doc.Race.MovementAgnostic", DocRaceTests::Flags)
bool FDocRaceMovementAgnosticTest::RunTest(const FString& Parameters)
{
	DocRaceTests::FFixture F;

	// Participant A: a plain actor (no movement component) moved by setting its location.
	AActor* DroneActor = F.ScopedWorld.World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(DroneActor);
	DroneActor->SetRootComponent(Root);
	Root->RegisterComponent();
	UDocRaceParticipantComponent* DroneComp = NewObject<UDocRaceParticipantComponent>(DroneActor);
	DroneComp->RegisterComponent();

	// Participant B: a host-controlled participant with no actor at all, fed by position samples.
	const FGuid HostParticipant = FGuid::NewGuid();

	TestTrue("Register Drone", F.Subsystem->RegisterParticipant(DroneComp->ParticipantId));
	TestTrue("Register host participant", F.Subsystem->RegisterParticipant(HostParticipant));

	FGuid RunA, RunB;
	TestTrue("BeginRun Drone", F.Subsystem->BeginRun(DroneComp->ParticipantId, F.CourseDef, RunA));
	TestTrue("BeginRun host", F.Subsystem->BeginRun(HostParticipant, F.CourseDef, RunB));
	TestTrue("Run IDs distinct", RunA != RunB);
	DroneComp->ActiveRunId = RunA;

	const float Xs[] = { -50.0f, 50.0f, 950.0f, 1050.0f, 1950.0f, 2050.0f };
	for (int32 i = 0; i < 6; ++i)
	{
		DroneActor->SetActorLocation(FVector(Xs[i], 0.0f, 0.0f));
		TestTrue(FString::Printf(TEXT("Drone sample %d"), i), DroneComp->SubmitCurrentPosition(1.0 + i));
	}
	DocRaceTests::DriveLap(F.Subsystem, RunB, 1.0);

	FDocRaceRun A, B;
	F.Subsystem->QueryRun(RunA, A);
	F.Subsystem->QueryRun(RunB, B);
	TestEqual("Drone finished", A.State, EDocRaceState::Finished);
	TestEqual("Host participant finished", B.State, EDocRaceState::Finished);
	TestTrue("Same elapsed time for the same path", FMath::IsNearlyEqual(A.ElapsedTime, B.ElapsedTime, 1e-4f));

	// The component's discontinuity reaches the subsystem: a teleport after it grants nothing.
	FGuid RunC;
	F.Subsystem->BeginRun(DroneComp->ParticipantId, F.CourseDef, RunC);
	DroneComp->ActiveRunId = RunC;
	DroneActor->SetActorLocation(FVector(-50.0f, 0.0f, 0.0f));
	DroneComp->SubmitCurrentPosition(20.0);
	TestTrue("Component discontinuity routed", DroneComp->NotifyDiscontinuity());
	DroneActor->SetActorLocation(FVector(2500.0f, 0.0f, 0.0f));
	DroneComp->SubmitCurrentPosition(20.1);
	FDocRaceRun C;
	F.Subsystem->QueryRun(RunC, C);
	TestEqual("Teleported drone gained no gate", C.Splits.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
