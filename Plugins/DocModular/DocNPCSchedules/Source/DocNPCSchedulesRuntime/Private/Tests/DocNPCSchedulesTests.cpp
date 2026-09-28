// DocNPCSchedules automation tests (SCH-01..08, SCH-10, SCH-12 base logic) with the
// manual clock and a delegate-driven mock executor. No Time, StateTree, BT, Mass or
// Smart Objects involved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocNPCScheduleSubsystem.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"

namespace DocScheduleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr double H = 3600.0;
	constexpr double Day = 24.0 * H;

	FDocNPCScheduleEntry MakeEntry(FName Id, double StartH, double EndH, const FGameplayTag& Activity, int32 Priority = 10)
	{
		FDocNPCScheduleEntry E;
		E.EntryId = Id;
		E.StartSeconds = StartH * H;
		E.EndSeconds = EndH * H;
		E.ActivityTag = Activity;
		E.Priority = Priority;
		return E;
	}

	UDocNPCScheduleDefinition* MakeSchedule(FName Id = TEXT("Villager"))
	{
		UDocNPCScheduleDefinition* S = NewObject<UDocNPCScheduleDefinition>(GetTransientPackage());
		S->ScheduleId = Id;
		S->DefaultActivity = DocScheduleTags::Activity_Idle;
		FDocNPCScheduleEntry Sleep = MakeEntry(TEXT("Sleep"), 22, 6, DocScheduleTags::Activity_Sleep);
		FDocNPCScheduleEntry Work = MakeEntry(TEXT("Work"), 9, 17, DocScheduleTags::Activity_Work);
		Work.DaysOfWeek = { 0, 1, 2, 3, 4 };
		Work.Target.bHasLocation = true;
		Work.Target.Location = FVector(1000, 0, 0);
		FDocNPCScheduleEntry Lunch = MakeEntry(TEXT("Lunch"), 12, 13, DocScheduleTags::Activity_Eat, 20);
		FDocNPCScheduleEntry Empty = MakeEntry(TEXT("Empty"), 15, 15, DocScheduleTags::Activity_Flee, 999);
		S->Entries = { Sleep, Work, Lunch, Empty };
		return S;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocNPCScheduleSubsystem* S = nullptr;
		FDocManualScheduleClock* Clock = nullptr;
		FFixture()
		{
			S = TW.GetSubsystem<UDocNPCScheduleSubsystem>();
			Clock = S ? S->GetManualClock() : nullptr;
		}
		void At(double Absolute) { Clock->SetAbsoluteSeconds(Absolute); S->AdvanceForTesting(0.f); }
		FGameplayTag Desired(FName NPC) const { return S->GetCurrentActivity(NPC).DesiredActivity; }

		UDocNPCScheduleComponent* SpawnNPC(FName NPC, UDocNPCScheduleDefinition* Schedule, TArray<FDocNPCActivityRequest>* Requests = nullptr, TArray<int64>* Cancels = nullptr)
		{
			AActor* Actor = TW.Spawn<AActor>();
			UDocNPCScheduleComponent* C = NewObject<UDocNPCScheduleComponent>(Actor);
			C->NPCId = NPC;
			C->Schedule = Schedule;
			if (Requests) { C->OnActivityRequestedNative.AddLambda([Requests](const FDocNPCActivityRequest& R) { Requests->Add(R); }); }
			if (Cancels) { C->OnActivityCancelledNative.AddLambda([Cancels](FName, int64 Id) { Cancels->Add(Id); }); }
			C->RegisterComponent();
			if (!Actor->HasActorBegunPlay())
			{
				Actor->DispatchBeginPlay();
			}
			return C;
		}
	};

	class FProvider final : public IDocScheduleConditionProvider
	{
	public:
		EDocConditionState State = EDocConditionState::Unavailable;
		virtual FDocConditionResult Evaluate(FName, const FGameplayTagQuery&) const override
		{
			FDocConditionResult R;
			R.State = State;
			return R;
		}
	};
}

using namespace DocScheduleTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleTimeTest, "Doc.Schedule.TimeSelection", Flags)
bool FDocScheduleTimeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	TestTrue(TEXT("Register"), F.S->RegisterNPC(TEXT("Ann"), MakeSchedule()).IsSuccess());
	TestEqual(TEXT("Duplicate NPC"), F.S->RegisterNPC(TEXT("Ann"), MakeSchedule()).Outcome, EDocResultOutcome::Conflict);
	auto At = [&F](double T) { return F.S->PreviewAt(TEXT("Ann"), T).DesiredActivity; };
	TestEqual(TEXT("23:00 sleep"), At(23 * H), DocScheduleTags::Activity_Sleep.GetTag());
	TestEqual(TEXT("Prior-day span after midnight"), At(Day + 3 * H), DocScheduleTags::Activity_Sleep.GetTag());
	TestEqual(TEXT("End is exclusive (06:00)"), At(Day + 6 * H), DocScheduleTags::Activity_Idle.GetTag());
	TestEqual(TEXT("Start is inclusive (09:00)"), At(9 * H), DocScheduleTags::Activity_Work.GetTag());
	TestEqual(TEXT("Higher priority lunch"), At(12.5 * H), DocScheduleTags::Activity_Eat.GetTag());
	TestEqual(TEXT("Back to work"), At(13 * H), DocScheduleTags::Activity_Work.GetTag());
	TestEqual(TEXT("Weekend (day 5) no work"), At(5 * Day + 10 * H), DocScheduleTags::Activity_Idle.GetTag());
	TestEqual(TEXT("Empty interval never selected"), At(15 * H), DocScheduleTags::Activity_Work.GetTag());

	// Custom calendar: 20-hour days, 5-day weeks (no 24h/7-day assumption).
	F.Clock->Calendar.DayLengthSeconds = 20 * H;
	F.Clock->Calendar.DaysPerWeek = 5;
	TestEqual(TEXT("Custom day: day 5 is weekday 0"), At(5 * 20 * H + 10 * H), DocScheduleTags::Activity_Work.GetTag());
	TestEqual(TEXT("Entries outside the custom day length are invalid (never active)"), At(20 * H + 2 * H), DocScheduleTags::Activity_Idle.GetTag());

	UDocNPCScheduleDefinition* Festival = MakeSchedule(TEXT("Festival"));
	FDocNPCScheduleEntry Fest = MakeEntry(TEXT("Fest"), 0, 0, DocScheduleTags::Activity_Eat, 5);
	Fest.bAllDay = true;
	Fest.Source = EDocScheduleSource::SpecialEvent;
	Fest.SpecialDay = 3;
	Festival->Entries.Add(Fest);
	F.S->RegisterNPC(TEXT("Bob"), Festival);
	TestEqual(TEXT("Special event day wins by class"), F.S->PreviewAt(TEXT("Bob"), 3 * 20 * H + 10 * H).DesiredActivity, DocScheduleTags::Activity_Eat.GetTag());
	TestEqual(TEXT("Not on other days"), F.S->PreviewAt(TEXT("Bob"), 2 * 20 * H + 10 * H).DesiredActivity, DocScheduleTags::Activity_Work.GetTag());
	TestTrue(TEXT("Empty interval reported to authors"), MakeSchedule()->FindProblems().Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSchedulePriorityTest, "Doc.Schedule.PriorityOverride", Flags)
bool FDocSchedulePriorityTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocNPCScheduleDefinition* S = NewObject<UDocNPCScheduleDefinition>(GetTransientPackage());
	S->ScheduleId = TEXT("Tie");
	FDocNPCScheduleEntry First = MakeEntry(TEXT("Z_First"), 8, 18, DocScheduleTags::Activity_Work);
	FDocNPCScheduleEntry Second = MakeEntry(TEXT("A_Second"), 8, 18, DocScheduleTags::Activity_Eat);
	FDocNPCScheduleEntry Guarded = MakeEntry(TEXT("Guarded"), 8, 18, DocScheduleTags::Activity_Flee, 50);
	Guarded.Conditions = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(DocScheduleTags::Activity_Flee));
	Guarded.FallbackActivity = DocScheduleTags::Activity_Idle;
	S->Entries = { First, Second, Guarded };
	F.S->RegisterNPC(TEXT("Cy"), S);
	TestEqual(TEXT("Unavailable condition → declared fallback"), F.S->PreviewAt(TEXT("Cy"), 10 * H).DesiredActivity, DocScheduleTags::Activity_Idle.GetTag());
	TSharedPtr<FProvider> Provider = MakeShared<FProvider>();
	F.S->SetConditionProvider(Provider);
	Provider->State = EDocConditionState::Unsatisfied;
	TestEqual(TEXT("Equal priority: authored order (not id)"), F.S->PreviewAt(TEXT("Cy"), 10 * H).DesiredActivity, DocScheduleTags::Activity_Work.GetTag());
	Provider->State = EDocConditionState::Satisfied;
	TestEqual(TEXT("Satisfied condition selects"), F.S->PreviewAt(TEXT("Cy"), 10 * H).DesiredActivity, DocScheduleTags::Activity_Flee.GetTag());
	TestEqual(TEXT("Editable default priorities"), GetDefault<UDocNPCSchedulesSettings>()->Cinematic, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleOverrideTest, "Doc.Schedule.OverridePop", Flags)
bool FDocScheduleOverrideTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	UDocNPCScheduleDefinition* S = MakeSchedule();
	UDocNPCActivityDefinition* Busy = NewObject<UDocNPCActivityDefinition>(GetTransientPackage());
	Busy->ActivityTag = DocScheduleTags::Activity_Work;
	Busy->bInterruptible = false;
	S->Entries[1].Activity = Busy;
	TArray<FDocNPCActivityRequest> Requests;
	TArray<int64> Cancels;
	UDocNPCScheduleComponent* C = F.SpawnNPC(TEXT("Dee"), S, &Requests, &Cancels);
	TestEqual(TEXT("Working"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Work.GetTag());
	TestEqual(TEXT("Request sent"), Requests.Num(), 1);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Started, TEXT(""));

	AActor* OwnerA = F.TW.Spawn<AActor>();
	AActor* OwnerB = F.TW.Spawn<AActor>();
	FDocSystemResult Result;
	F.S->PushScheduleOverride(TEXT("Dee"), OwnerA, EDocScheduleSource::TemporaryOverride, 40, DocScheduleTags::Activity_Flee, FDocScheduleTarget(), 0.f,
		EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::RejectIfBusy, false, Result);
	TestEqual(TEXT("RejectIfBusy while non-interruptible runs"), Result.Outcome, EDocResultOutcome::Conflict);
	const FDocRequestHandle Wait = F.S->PushScheduleOverride(TEXT("Dee"), OwnerA, EDocScheduleSource::TemporaryOverride, 40, DocScheduleTags::Activity_Flee,
		FDocScheduleTarget(), 0.f, EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::WaitForInterruptible, false, Result);
	TestEqual(TEXT("Desired changes"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Flee.GetTag());
	TestEqual(TEXT("No interruption of a non-interruptible activity"), Requests.Num(), 1);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Completed, TEXT(""));
	TestEqual(TEXT("Dispatched after it ended"), Requests.Num(), 2);
	TestEqual(TEXT("Override activity requested"), Requests.Last().ActivityTag, DocScheduleTags::Activity_Flee.GetTag());

	TestEqual(TEXT("Other owner cannot pop"), F.S->PopScheduleOverride(Wait, OwnerB).Outcome, EDocResultOutcome::PermissionDenied);
	F.At(12.5 * H); // time moved on while overridden
	TestTrue(TEXT("Pop"), F.S->PopScheduleOverride(Wait, OwnerA).IsSuccess());
	TestEqual(TEXT("Recomputed at current time (lunch), not the old snapshot"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Eat.GetTag());
	TestEqual(TEXT("Pop twice is NoChange"), F.S->PopScheduleOverride(Wait, OwnerA).Outcome, EDocResultOutcome::NoChange);

	F.S->PushScheduleOverride(TEXT("Dee"), OwnerB, EDocScheduleSource::EmergencyOverride, 40, DocScheduleTags::Activity_Flee, FDocScheduleTarget(), 60.f,
		EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::Preempt, false, Result);
	TestEqual(TEXT("Emergency applies"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Flee.GetTag());
	F.At(12.5 * H + 61.0);
	TestEqual(TEXT("Expired override removed"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Eat.GetTag());

	F.S->PushScheduleOverride(TEXT("Dee"), OwnerA, EDocScheduleSource::TemporaryOverride, 40, DocScheduleTags::Activity_Flee, FDocScheduleTarget(), 0.f,
		EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::Preempt, false, Result);
	F.S->PushScheduleOverride(TEXT("Dee"), OwnerB, EDocScheduleSource::TemporaryOverride, 30, DocScheduleTags::Activity_Sleep, FDocScheduleTarget(), 0.f,
		EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::Preempt, false, Result);
	TestEqual(TEXT("ClearOverrides removes only the caller's"), F.S->ClearOverrides(TEXT("Dee"), OwnerA), 1);
	TestEqual(TEXT("B's override remains"), F.Desired(TEXT("Dee")), DocScheduleTags::Activity_Sleep.GetTag());
	TestEqual(TEXT("Admin clear"), F.S->ClearAllOverridesAdmin(TEXT("Dee")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleJumpTest, "Doc.Schedule.TimeJump", Flags)
bool FDocScheduleJumpTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	F.S->RegisterNPC(TEXT("Eve"), MakeSchedule());
	const int32 Before = F.S->GetEvaluationCount();
	F.At(30 * Day + 23 * H);
	TestEqual(TEXT("Forward jump: coherent state"), F.Desired(TEXT("Eve")), DocScheduleTags::Activity_Sleep.GetTag());
	TestTrue(TEXT("Forward jump: bounded work (no replay)"), F.S->GetEvaluationCount() - Before <= 2);
	const int32 Epoch = F.S->GetCurrentActivity(TEXT("Eve")).Epoch;
	F.At(10 * H);
	TestEqual(TEXT("Backward jump: recomputed"), F.Desired(TEXT("Eve")), DocScheduleTags::Activity_Work.GetTag());
	TestTrue(TEXT("Epoch unchanged for the record; global epoch advanced"), F.S->GetCurrentActivity(TEXT("Eve")).Epoch == Epoch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleOfflineTest, "Doc.Schedule.OfflineSimulation", Flags)
bool FDocScheduleOfflineTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocNPCScheduleDefinition* S = MakeSchedule();
	constexpr int32 Count = 500;
	for (int32 i = 0; i < Count; ++i)
	{
		F.S->RegisterNPC(FName(*FString::Printf(TEXT("Npc%d"), i)), S);
	}
	const int32 Before = F.S->GetEvaluationCount();
	constexpr int32 Minutes = 34 * 60; // until 10:00 on day 1, ticked every simulated minute
	for (int32 Tick = 1; Tick <= Minutes; ++Tick)
	{
		F.At(Tick * 60.0);
	}
	const int32 Evaluations = F.S->GetEvaluationCount() - Before;
	TestTrue(TEXT("Boundary-indexed: a handful of evaluations per NPC per day"), Evaluations <= Count * 14);
	TestTrue(TEXT("Not per-tick polling"), Evaluations < Count * Minutes / 10);
	TestEqual(TEXT("State is current"), F.Desired(TEXT("Npc42")), DocScheduleTags::Activity_Work.GetTag());
	TestEqual(TEXT("Expected location from schedule while unloaded"), F.S->GetExpectedLocation(TEXT("Npc42")).Confidence, EDocLocationConfidence::ScheduledTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleSaveTest, "Doc.Schedule.SaveRestore", Flags)
bool FDocScheduleSaveTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	UDocNPCScheduleDefinition* S = MakeSchedule();
	TArray<FDocNPCActivityRequest> Requests;
	F.SpawnNPC(TEXT("Fay"), S, &Requests);
	AActor* Owner = F.TW.Spawn<AActor>();
	FDocSystemResult Result;
	const FDocRequestHandle Durable = F.S->PushScheduleOverride(TEXT("Fay"), Owner, EDocScheduleSource::ManualOverride, 80, DocScheduleTags::Activity_Sleep,
		FDocScheduleTarget(), 0.f, EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::Preempt, true, Result);
	F.S->PushScheduleOverride(TEXT("Fay"), Owner, EDocScheduleSource::TemporaryOverride, 10, DocScheduleTags::Activity_Flee,
		FDocScheduleTarget(), 0.f, EDocOverrideResume::RecomputeCurrent, EDocOverridePreemption::Preempt, false, Result);
	const FDocScheduleSaveData Saved = F.S->CaptureState();
	TestEqual(TEXT("Only durable overrides saved"), Saved.Records[0].DurableOverrides.Num(), 1);

	TestTrue(TEXT("Restore"), F.S->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("Durable override restored"), F.Desired(TEXT("Fay")), DocScheduleTags::Activity_Sleep.GetTag());
	TestEqual(TEXT("Transient override gone"), F.S->GetCurrentActivity(TEXT("Fay")).OverrideCount, 1);
	TestTrue(TEXT("Fresh request flagged as restored (no duplicate start rewards)"), Requests.Num() > 0 && Requests.Last().bRestored);
	TestEqual(TEXT("Stale pre-restore handle"), F.S->PopScheduleOverride(Durable, Owner).Outcome, EDocResultOutcome::NoChange);

	FDocScheduleSaveData Unknown = Saved;
	Unknown.Records[0].ScheduleId = TEXT("Missing");
	TestEqual(TEXT("Unknown schedule rejected before applying"), F.S->RestoreState(Unknown).Outcome, EDocResultOutcome::NotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleExecutorTest, "Doc.Schedule.ExecutorLifecycle", Flags)
bool FDocScheduleExecutorTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	UDocNPCScheduleDefinition* S = MakeSchedule();
	UDocNPCActivityDefinition* Work = NewObject<UDocNPCActivityDefinition>(GetTransientPackage());
	Work->ActivityTag = DocScheduleTags::Activity_Work;
	Work->MaxRetries = 2;
	Work->RetryBackoffSeconds = 10.f;
	Work->FallbackActivity = DocScheduleTags::Activity_Idle;
	S->Entries[1].Activity = Work;
	TArray<FDocNPCActivityRequest> Requests;
	UDocNPCScheduleComponent* C = F.SpawnNPC(TEXT("Gus"), S, &Requests);
	if (!TestTrue(TEXT("Has activity request"), Requests.Num() > 0)) { return false; }
	const int64 First = Requests.Last().RequestId;
	TestEqual(TEXT("Desired work, requested, no arrival claimed"), F.S->GetCurrentActivity(TEXT("Gus")).Status, EDocActivityStatus::Requested);
	C->ReportStatus(First, EDocActivityStatus::Travelling, TEXT(""));
	TestEqual(TEXT("Observed travelling"), F.S->GetCurrentActivity(TEXT("Gus")).Status, EDocActivityStatus::Travelling);

	C->ReportStatus(First, EDocActivityStatus::Failed, TEXT("Path blocked"));
	TestEqual(TEXT("Retry scheduled"), F.S->GetCurrentActivity(TEXT("Gus")).RetryCount, 1);
	F.At(10 * H + 11.0);
	TestEqual(TEXT("Retried after backoff"), Requests.Num(), 2);
	TestEqual(TEXT("Late callback for the old request ignored"), C->ReportStatus(First, EDocActivityStatus::Completed, TEXT("")).Outcome, EDocResultOutcome::NoChange);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Failed, TEXT("Path blocked"));
	F.At(10 * H + 40.0);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Failed, TEXT("Path blocked"));
	TestEqual(TEXT("Fallback after bounded retries"), Requests.Last().ActivityTag, DocScheduleTags::Activity_Idle.GetTag());
	TestTrue(TEXT("Fallback flagged"), Requests.Last().bIsFallback);
	const int32 AfterFallback = Requests.Num();
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Failed, TEXT("No slot"));
	F.At(10 * H + 200.0);
	TestEqual(TEXT("No busy loop after the fallback fails"), Requests.Num(), AfterFallback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleTargetTest, "Doc.Schedule.TargetUnavailable", Flags)
bool FDocScheduleTargetTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	UDocNPCScheduleDefinition* S = MakeSchedule();
	UDocNPCActivityDefinition* Work = NewObject<UDocNPCActivityDefinition>(GetTransientPackage());
	Work->ActivityTag = DocScheduleTags::Activity_Work;
	Work->ArrivalDeadlineSeconds = 120.f;
	Work->FallbackActivity = DocScheduleTags::Activity_Idle;
	Work->MaxRetries = 0;
	S->Entries[1].Activity = Work;
	TArray<FDocNPCActivityRequest> Requests;
	UDocNPCScheduleComponent* C = F.SpawnNPC(TEXT("Hal"), S, &Requests);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Travelling, TEXT(""));
	F.At(10 * H + 121.0);
	TestEqual(TEXT("Missed arrival deadline → fallback, not a false arrival"), Requests.Last().ActivityTag, DocScheduleTags::Activity_Idle.GetTag());

	F.At(Day + 10 * H);
	C->ReportStatus(Requests.Last().RequestId, EDocActivityStatus::Unavailable, TEXT("Region missing"));
	TestEqual(TEXT("Unavailable target → configured fallback"), Requests.Last().ActivityTag, DocScheduleTags::Activity_Idle.GetTag());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocScheduleSwapTest, "Doc.Schedule.RepresentationSwap", Flags)
bool FDocScheduleSwapTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.At(10 * H);
	UDocNPCScheduleDefinition* S = MakeSchedule();
	TArray<FDocNPCActivityRequest> RequestsA, RequestsB;
	TArray<int64> CancelsA;
	UDocNPCScheduleComponent* A = F.SpawnNPC(TEXT("Ivy"), S, &RequestsA, &CancelsA);
	UDocNPCScheduleComponent* B = F.SpawnNPC(TEXT("Ivy"), S, &RequestsB);
	TestEqual(TEXT("Old representation cancelled"), CancelsA.Num(), 1);
	TestEqual(TEXT("New representation requested"), RequestsB.Num(), 1);
	TestTrue(TEXT("Bound"), F.S->GetCurrentActivity(TEXT("Ivy")).bRepresentationBound);
	B->GetOwner()->Destroy();
	TestFalse(TEXT("Unloaded: record kept, no representation"), F.S->GetCurrentActivity(TEXT("Ivy")).bRepresentationBound);
	TestTrue(TEXT("Logical record survives"), F.S->HasNPC(TEXT("Ivy")));
	F.S->ReportObservedLocation(TEXT("Ivy"), FVector(5, 5, 5), false);
	TestFalse(TEXT("Expected is not safe-to-spawn"), F.S->GetExpectedLocation(TEXT("Ivy")).bPlacementValidated);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
