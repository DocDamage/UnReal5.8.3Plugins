// DocQuestObjectives automation tests (OBJ-01..11 base logic, timers, validation).
// The GameInstance service is created directly and exposed through the test
// override; no Events, Inventory, Dialogue or Map feature is involved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocObjectiveSubsystem.h"
#include "Engine/GameInstance.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocQuestTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Kill, "Test.Quest.Kill");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KillWolf, "Test.Quest.Kill.Wolf");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Collect, "Test.Quest.Collect");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Herb, "Test.Quest.Collect.Herb");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Talk, "Test.Quest.Talk");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Reach, "Test.Quest.Reach");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Alarm, "Test.Quest.Alarm");

	const FGuid Campaign(7, 7, 7, 7);
	FDocOwnerScope Player(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0xA11CE, 1, 2, static_cast<uint32>(N)), Campaign); }
	FDocOwnerScope Shared() { return FDocOwnerScope(EDocOwnerScopeKind::SharedWorld, FGuid(0x5AED, 1, 2, 3), Campaign); }

	FDocObjectiveDefinition MakeObjective(FName Id, const FGameplayTag& Tag, int32 Target = 1, bool bOptional = false)
	{
		FDocObjectiveDefinition O;
		O.ObjectiveId = Id;
		O.DisplayName = FText::FromName(Id);
		O.EventTag = Tag;
		O.TargetCount = Target;
		O.bOptional = bOptional;
		return O;
	}

	FDocObjectiveDefinition MakeWait(FName Id, float Seconds)
	{
		FDocObjectiveDefinition O = MakeObjective(Id, FGameplayTag());
		O.Evaluator = EDocObjectiveEvaluator::Wait;
		O.WaitSeconds = Seconds;
		return O;
	}

	FDocQuestStage MakeStage(FName Id, const TArray<FDocObjectiveDefinition>& Objectives,
		EDocStageActivation Activation = EDocStageActivation::Parallel, EDocStageCompletion Completion = EDocStageCompletion::AllRequired)
	{
		FDocQuestStage S;
		S.StageId = Id;
		S.Objectives = Objectives;
		S.Activation = Activation;
		S.Completion = Completion;
		return S;
	}

	UDocQuestDefinition* MakeQuest(FName Id, const TArray<FDocQuestStage>& Stages)
	{
		UDocQuestDefinition* Q = NewObject<UDocQuestDefinition>(GetTransientPackage());
		Q->QuestId = Id;
		Q->Title = FText::FromName(Id);
		Q->Stages = Stages;
		return Q;
	}

	FDocQuestAction MakeAction(FName Id, FName Provider, EDocQuestActionType Type = EDocQuestActionType::Custom)
	{
		FDocQuestAction A;
		A.ActionId = Id;
		A.Type = Type;
		A.ProviderId = Provider;
		return A;
	}

	FDocQuestCondition Completed(FName QuestId)
	{
		FDocQuestCondition C;
		C.Type = EDocQuestConditionType::QuestCompleted;
		C.QuestId = QuestId;
		return C;
	}

	FDocQuestObservation Obs(const FDocOwnerScope& Owner, const FGameplayTag& Tag, int32 Amount = 1, const FGuid& EventId = FGuid::NewGuid())
	{
		FDocQuestObservation O;
		O.SourceId = TEXT("Test");
		O.EventId = EventId;
		O.Owner = Owner;
		O.EventTag = Tag;
		O.Amount = Amount;
		return O;
	}

	FDocQuestObservation SeqObs(const FDocOwnerScope& Owner, const FGameplayTag& Tag, int64 Epoch, int64 Sequence)
	{
		FDocQuestObservation O = Obs(Owner, Tag, 1, FGuid());
		O.SourceEpoch = Epoch;
		O.Sequence = Sequence;
		return O;
	}

	class FRecordingEffects final : public IDocQuestEffectProvider
	{
	public:
		TArray<FDocEffectKey> Calls;
		TSet<FDocEffectKey> Receipts;
		int32 FailNext = 0;
		EDocResultOutcome FailOutcome = EDocResultOutcome::Conflict;
		bool bLoseResponseOnce = false;

		virtual FDocSystemResult DeliverEffect(const FDocQuestEffectRequest& Request) override
		{
			Calls.Add(Request.Key);
			if (Receipts.Contains(Request.Key))
			{
				return FDocSystemResult::MakeNoChange(TEXT("Receipt exists"));
			}
			if (FailNext > 0)
			{
				--FailNext;
				return FDocSystemResult::MakeFailure(FailOutcome, TEXT("Destination full"));
			}
			Receipts.Add(Request.Key);
			if (bLoseResponseOnce)
			{
				bLoseResponseOnce = false;
				return FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Response lost"));
			}
			return FDocSystemResult::MakeSuccess();
		}
		int32 Applied() const { return Receipts.Num(); }
	};

	class FCountState final : public IDocQuestStateProvider
	{
	public:
		int32 Value = 0;
		bool bAvailable = true;
		virtual bool QueryCurrentCount(const FDocOwnerScope&, const FDocObjectiveDefinition&, int32& OutCount) const override
		{
			OutCount = Value;
			return bAvailable;
		}
	};

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocObjectiveSubsystem> Service;
		UDocObjectiveSubsystem* S = nullptr;
		TArray<FDocQuestChange> QuestChanges;
		TArray<FDocQuestChange> ObjectiveChanges;
		int32 Refreshes = 0;
		// Declared last: destroyed first.
		FDocScopedTestWorld TW;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			S = NewService();
			Service.Reset(S);
			UDocObjectiveSubsystem::SetSubsystemOverrideForTesting(S);
			S->AttachWorld(TW.World);
			S->OnQuestChangedNative.AddLambda([this](const FDocQuestChange& C) { QuestChanges.Add(C); });
			S->OnObjectiveChangedNative.AddLambda([this](const FDocQuestChange& C) { ObjectiveChanges.Add(C); });
			S->OnStateRefreshedNative.AddLambda([this]() { ++Refreshes; });
		}

		~FFixture()
		{
			S->OnQuestChangedNative.Clear();
			S->OnObjectiveChangedNative.Clear();
			S->OnStateRefreshedNative.Clear();
			UDocObjectiveSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}

		UDocObjectiveSubsystem* NewService() const
		{
			UDocObjectiveSubsystem* X = NewObject<UDocObjectiveSubsystem>(GameInstance.Get());
			X->RegisterTrustedSource(TEXT("Test"));
			return X;
		}

		FDocQuestOwnerRecord Record(const FDocOwnerScope& Owner, FName QuestId, UDocObjectiveSubsystem* On = nullptr) const
		{
			FDocQuestOwnerRecord R;
			(On ? On : S)->GetQuestSnapshot(Owner, QuestId, R);
			return R;
		}

		EDocQuestState State(const FDocOwnerScope& Owner, FName QuestId) const { return Record(Owner, QuestId).Current.State; }

		const FDocObjectiveRuntimeState* FindObjective(const FDocQuestOwnerRecord& R, FName ObjectiveId) const { return R.Current.FindObjective(ObjectiveId); }

		int32 Count(const FDocOwnerScope& Owner, FName QuestId, FName ObjectiveId, UDocObjectiveSubsystem* On = nullptr) const
		{
			const FDocQuestOwnerRecord R = Record(Owner, QuestId, On);
			const FDocObjectiveRuntimeState* O = R.Current.FindObjective(ObjectiveId);
			return O ? O->Count : -1;
		}

		EDocObjectiveState ObjState(const FDocOwnerScope& Owner, FName QuestId, FName ObjectiveId) const
		{
			const FDocQuestOwnerRecord R = Record(Owner, QuestId);
			const FDocObjectiveRuntimeState* O = R.Current.FindObjective(ObjectiveId);
			return O ? O->State : EDocObjectiveState::Inactive;
		}

		FDocSystemResult Activate(const FDocOwnerScope& Owner, FName QuestId, UDocObjectiveSubsystem* On = nullptr)
		{
			FGuid Instance;
			return (On ? On : S)->ActivateQuest(Owner, QuestId, Instance);
		}
	};
}

using namespace DocQuestTests;

// ---------------------------------------------------------------------------
// OBJ-01
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestActivationTest, "Doc.Quest.Activation", DocQuestTests::Flags)
bool FDocQuestActivationTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocQuestDefinition* A = MakeQuest(TEXT("A"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("KillOne"), TAG_Kill) }) });
	UDocQuestDefinition* B = MakeQuest(TEXT("B"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Anything"), TAG_Alarm) }) });
	B->Prerequisites = { Completed(TEXT("A")) };
	UDocQuestDefinition* C = MakeQuest(TEXT("C"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Gather"), TAG_Collect) }) });
	C->Prerequisites = { Completed(TEXT("A")) };
	C->bAutoActivate = true;
	UDocQuestDefinition* Rep = MakeQuest(TEXT("Rep"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Arrive"), TAG_Reach) }) });
	Rep->bRepeatable = true;
	UDocObjectiveAsset* Talk = NewObject<UDocObjectiveAsset>(GetTransientPackage());
	Talk->Objective = MakeObjective(TEXT("TalkToSmith"), TAG_Talk);
	for (UDocQuestDefinition* Q : { A, B, C, Rep })
	{
		TestTrue(*FString::Printf(TEXT("Register %s"), *Q->QuestId.ToString()), F.S->RegisterQuestDefinition(Q).IsSuccess());
	}
	TestTrue(TEXT("Register standalone"), F.S->RegisterObjectiveAsset(Talk).IsSuccess());
	F.S->RegisterOwner(Player(1));

	FDocSystemResult R = F.Activate(Player(1), TEXT("B"));
	TestTrue(TEXT("Prerequisite unmet"), R.Outcome == EDocResultOutcome::NotReady && R.ErrorTag == DocQuestTags::Error_Quest_PrerequisitesUnmet);
	TestTrue(TEXT("Activate A"), F.Activate(Player(1), TEXT("A")).IsSuccess());
	R = F.Activate(Shared(), TEXT("A"));
	TestTrue(TEXT("Owner policy enforced"), R.ErrorTag == DocQuestTags::Error_Quest_OwnerPolicy);

	TestTrue(TEXT("Other owner's event does not count"), F.S->SubmitObservation(Obs(Player(2), TAG_Kill)).Outcome == EDocResultOutcome::NoChange);
	TestTrue(TEXT("Still active"), F.State(Player(1), TEXT("A")) == EDocQuestState::Active);
	F.S->SubmitObservation(Obs(Player(1), TAG_Kill));
	TestTrue(TEXT("A completed"), F.State(Player(1), TEXT("A")) == EDocQuestState::Completed);
	TestTrue(TEXT("C auto-activated after its prerequisite"), F.S->GetActiveQuests(Player(1)).Contains(FName(TEXT("C"))));
	TestFalse(TEXT("Nothing leaked to player 2"), F.S->GetActiveQuests(Player(2)).Num() > 0);
	TestTrue(TEXT("B now allowed"), F.Activate(Player(1), TEXT("B")).IsSuccess());
	R = F.Activate(Player(1), TEXT("A"));
	TestTrue(TEXT("Completed non-repeatable quest stays terminal"), R.ErrorTag == DocQuestTags::Error_Quest_NotRepeatable);

	FGuid Standalone;
	TestTrue(TEXT("Standalone objective"), F.S->ActivateObjective(Player(1), TEXT("TalkToSmith"), Standalone).IsSuccess());
	F.S->SubmitObservation(Obs(Player(1), TAG_Talk));
	TestTrue(TEXT("Standalone completed"), F.State(Player(1), Talk->GetRecordId()) == EDocQuestState::Completed);

	FGuid Run1, Run2;
	F.S->ActivateQuest(Player(1), TEXT("Rep"), Run1);
	const FGuid SameEvent = FGuid::NewGuid();
	F.S->SubmitObservation(Obs(Player(1), TAG_Reach, 1, SameEvent));
	TestTrue(TEXT("Repeat run 1 done"), F.State(Player(1), TEXT("Rep")) == EDocQuestState::Completed);
	TestTrue(TEXT("Repeat run 2"), F.S->ActivateQuest(Player(1), TEXT("Rep"), Run2).IsSuccess());
	TestNotEqual(TEXT("New instance id"), Run1, Run2);
	const FDocQuestOwnerRecord Rec = F.Record(Player(1), TEXT("Rep"));
	TestEqual(TEXT("Repeat ordinal"), Rec.Current.RepeatOrdinal, 2);
	TestEqual(TEXT("Fresh progress"), F.Count(Player(1), TEXT("Rep"), TEXT("Arrive")), 0);
	F.S->SubmitObservation(Obs(Player(1), TAG_Reach, 1, SameEvent));
	TestEqual(TEXT("Runs do not share dedup windows"), F.Record(Player(1), TEXT("Rep")).TimesCompleted, 2);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-02
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestEventCountTest, "Doc.Quest.EventCount", DocQuestTests::Flags)
bool FDocQuestEventCountTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocObjectiveDefinition Exact = MakeObjective(TEXT("Exact"), TAG_Kill, 5);
	Exact.Filter.bExactEventTag = true;
	UDocQuestDefinition* K = MakeQuest(TEXT("K"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Wolves"), TAG_Kill, 100), Exact }) });
	F.S->RegisterQuestDefinition(K);
	const FDocOwnerScope P = Player(1);

	TestTrue(TEXT("Inactive quests do not count"), F.S->SubmitObservation(Obs(P, TAG_KillWolf)).Outcome == EDocResultOutcome::NoChange);
	F.Activate(P, TEXT("K"));
	TestEqual(TEXT("Nothing retroactive"), F.Count(P, TEXT("K"), TEXT("Wolves")), 0);

	const FGuid E1 = FGuid::NewGuid();
	F.S->SubmitObservation(Obs(P, TAG_KillWolf, 1, E1));
	TestEqual(TEXT("Hierarchical match"), F.Count(P, TEXT("K"), TEXT("Wolves")), 1);
	TestEqual(TEXT("Exact filter ignores child tag"), F.Count(P, TEXT("K"), TEXT("Exact")), 0);
	TestTrue(TEXT("Duplicate event id ignored"), F.S->SubmitObservation(Obs(P, TAG_KillWolf, 1, E1)).Outcome == EDocResultOutcome::NoChange);
	TestEqual(TEXT("Count unchanged"), F.Count(P, TEXT("K"), TEXT("Wolves")), 1);
	F.S->SubmitObservation(Obs(P, TAG_Kill));
	TestEqual(TEXT("Exact match"), F.Count(P, TEXT("K"), TEXT("Exact")), 1);
	TestEqual(TEXT("Parent tag counts for hierarchical"), F.Count(P, TEXT("K"), TEXT("Wolves")), 2);

	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 10));
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 10));
	TestEqual(TEXT("Sequence dedup"), F.Count(P, TEXT("K"), TEXT("Wolves")), 3);
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 8));
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 8));
	TestEqual(TEXT("Out-of-order accepted once"), F.Count(P, TEXT("K"), TEXT("Wolves")), 4);
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 110));
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 1, 20));
	TestEqual(TEXT("Outside the bounded window is rejected, not assumed new"), F.Count(P, TEXT("K"), TEXT("Wolves")), 5);
	F.S->SubmitObservation(SeqObs(P, TAG_KillWolf, 0, 500));
	TestEqual(TEXT("Older source epoch ignored"), F.Count(P, TEXT("K"), TEXT("Wolves")), 5);

	TestTrue(TEXT("Zero amount rejected"), F.S->SubmitObservation(Obs(P, TAG_KillWolf, 0)).Outcome == EDocResultOutcome::InvalidInput);
	FDocQuestObservation Forged = Obs(P, TAG_KillWolf);
	Forged.SourceId = TEXT("Client");
	TestTrue(TEXT("Untrusted source rejected"), F.S->SubmitObservation(Forged).ErrorTag == DocQuestTags::Error_Quest_UntrustedSource);
	F.S->SubmitObservation(Obs(P, TAG_KillWolf, MAX_int32));
	TestEqual(TEXT("Overflow clamps at target"), F.Count(P, TEXT("K"), TEXT("Wolves")), 100);
	TestTrue(TEXT("Wolves completed"), F.ObjState(P, TEXT("K"), TEXT("Wolves")) == EDocObjectiveState::Completed);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-03
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestSequentialStageTest, "Doc.Quest.SequentialStage", DocQuestTests::Flags)
bool FDocQuestSequentialStageTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocQuestDefinition* S = MakeQuest(TEXT("Seq"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("First"), TAG_Talk) }), MakeStage(TEXT("S2"), { MakeObjective(TEXT("Second"), TAG_Talk) }) });
	UDocQuestDefinition* C = MakeQuest(TEXT("Cascade"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("First"), TAG_Talk) }), MakeStage(TEXT("S2"), { MakeObjective(TEXT("Second"), TAG_Talk) }) });
	C->EvaluationMode = EDocQuestEvaluationMode::BoundedCascade;
	C->MaxCascadeDepth = 1;
	F.S->RegisterQuestDefinition(S);
	F.S->RegisterQuestDefinition(C);

	F.Activate(Player(1), TEXT("Seq"));
	F.S->SubmitObservation(Obs(Player(1), TAG_Talk));
	const FDocQuestOwnerRecord R = F.Record(Player(1), TEXT("Seq"));
	TestEqual(TEXT("Advanced to S2"), R.Current.StageId, FName(TEXT("S2")));
	TestEqual(TEXT("Same event did not count for the new stage"), F.Count(Player(1), TEXT("Seq"), TEXT("Second")), 0);
	TestTrue(TEXT("S1 recorded"), R.Current.CompletedStages.Contains(FName(TEXT("S1"))));
	F.S->SubmitObservation(Obs(Player(1), TAG_Talk));
	TestTrue(TEXT("Second event completes"), F.State(Player(1), TEXT("Seq")) == EDocQuestState::Completed);

	F.Activate(Player(2), TEXT("Cascade"));
	F.S->SubmitObservation(Obs(Player(2), TAG_Talk));
	TestTrue(TEXT("Authored bounded cascade consumes the event in both stages"), F.State(Player(2), TEXT("Cascade")) == EDocQuestState::Completed);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-04
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestParallelObjectivesTest, "Doc.Quest.ParallelObjectives", DocQuestTests::Flags)
bool FDocQuestParallelObjectivesTest::RunTest(const FString& Parameters)
{
	FFixture F;
	const TArray<FDocObjectiveDefinition> Two = { MakeObjective(TEXT("O1"), TAG_Kill), MakeObjective(TEXT("O2"), TAG_Collect) };
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("All"), { MakeStage(TEXT("S1"), Two) }));
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Any"), { MakeStage(TEXT("S1"), Two, EDocStageActivation::Parallel, EDocStageCompletion::AnyRequired) }));
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Ordered"), { MakeStage(TEXT("S1"), Two, EDocStageActivation::Ordered) }));

	F.Activate(Player(1), TEXT("All"));
	TestTrue(TEXT("Parallel activates both"), F.ObjState(Player(1), TEXT("All"), TEXT("O1")) == EDocObjectiveState::Active && F.ObjState(Player(1), TEXT("All"), TEXT("O2")) == EDocObjectiveState::Active);
	F.S->SubmitObservation(Obs(Player(1), TAG_Kill));
	TestTrue(TEXT("AllRequired waits"), F.State(Player(1), TEXT("All")) == EDocQuestState::Active);
	F.S->SubmitObservation(Obs(Player(1), TAG_Collect));
	TestTrue(TEXT("AllRequired completes"), F.State(Player(1), TEXT("All")) == EDocQuestState::Completed);

	F.Activate(Player(2), TEXT("Any"));
	F.S->SubmitObservation(Obs(Player(2), TAG_Kill));
	TestTrue(TEXT("AnyRequired completes on one"), F.State(Player(2), TEXT("Any")) == EDocQuestState::Completed);
	TestTrue(TEXT("Remaining objective closed"), F.ObjState(Player(2), TEXT("Any"), TEXT("O2")) == EDocObjectiveState::Cancelled);

	F.Activate(Player(3), TEXT("Ordered"));
	TestTrue(TEXT("Ordered: only the first"), F.ObjState(Player(3), TEXT("Ordered"), TEXT("O2")) == EDocObjectiveState::Inactive);
	TestTrue(TEXT("Second is not listening yet"), F.S->SubmitObservation(Obs(Player(3), TAG_Collect)).Outcome == EDocResultOutcome::NoChange);
	F.S->SubmitObservation(Obs(Player(3), TAG_Kill));
	TestTrue(TEXT("Next activated"), F.ObjState(Player(3), TEXT("Ordered"), TEXT("O2")) == EDocObjectiveState::Active);
	F.S->SubmitObservation(Obs(Player(3), TAG_Collect));
	TestTrue(TEXT("Ordered completes"), F.State(Player(3), TEXT("Ordered")) == EDocQuestState::Completed);

	FDocQuestStage Legacy;
	TestTrue(TEXT("Ordered preset maps fully"), UDocQuestDefinition::ApplyLegacyStageMode(Legacy, EDocQuestLegacyStageMode::Ordered)
		&& Legacy.Activation == EDocStageActivation::Ordered && Legacy.Completion == EDocStageCompletion::AllRequired);
	TestFalse(TEXT("Parallel preset requires an explicit completion rule"), UDocQuestDefinition::ApplyLegacyStageMode(Legacy, EDocQuestLegacyStageMode::Parallel));
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-05
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestOptionalObjectiveTest, "Doc.Quest.OptionalObjective", DocQuestTests::Flags)
bool FDocQuestOptionalObjectiveTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocObjectiveDefinition Timed = MakeObjective(TEXT("Bonus"), TAG_Collect, 1, true);
	Timed.TimeLimitSeconds = 5.f;
	UDocQuestDefinition* Q = MakeQuest(TEXT("Opt"), {
		MakeStage(TEXT("S1"), { MakeObjective(TEXT("Main"), TAG_Kill), Timed, MakeObjective(TEXT("Chat"), TAG_Talk, 1, true) }),
		MakeStage(TEXT("S2"), { MakeObjective(TEXT("Final"), TAG_Reach) }) });
	F.S->RegisterQuestDefinition(Q);
	F.Activate(Player(1), TEXT("Opt"));

	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 6.0);
	TestTrue(TEXT("Optional failed on its time limit"), F.ObjState(Player(1), TEXT("Opt"), TEXT("Bonus")) == EDocObjectiveState::Failed);
	TestTrue(TEXT("Optional failure does not fail the quest"), F.State(Player(1), TEXT("Opt")) == EDocQuestState::Active);
	F.S->SubmitObservation(Obs(Player(1), TAG_Kill));
	const FDocQuestOwnerRecord R = F.Record(Player(1), TEXT("Opt"));
	TestEqual(TEXT("Required work advanced the stage"), R.Current.StageId, FName(TEXT("S2")));
	const bool bChatClosed = F.ObjectiveChanges.ContainsByPredicate([](const FDocQuestChange& C)
	{
		return C.ObjectiveId == FName(TEXT("Chat")) && C.ObjectiveState == EDocObjectiveState::Cancelled;
	});
	TestTrue(TEXT("Open optional work closed by policy"), bChatClosed);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-06
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestFailureTest, "Doc.Quest.Failure", DocQuestTests::Flags)
bool FDocQuestFailureTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocQuestFailureRule Alarm;
	Alarm.RuleId = TEXT("Alarm");
	Alarm.EventTag = TAG_Alarm;
	Alarm.Action = EDocQuestFailureAction::FailQuest;

	UDocQuestDefinition* FailWins = MakeQuest(TEXT("FailWins"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Grab"), TAG_Alarm) }) });
	FailWins->FailureRules = { Alarm };
	UDocQuestDefinition* WinWins = MakeQuest(TEXT("WinWins"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Grab"), TAG_Alarm) }) });
	WinWins->FailureRules = { Alarm };
	WinWins->FailurePrecedence = EDocQuestFailurePrecedence::CompletionWins;
	FDocObjectiveDefinition Timed = MakeObjective(TEXT("Timed"), TAG_Kill);
	Timed.TimeLimitSeconds = 3.f;
	UDocQuestDefinition* Deadline = MakeQuest(TEXT("Deadline"), { MakeStage(TEXT("S1"), { Timed }) });
	FDocQuestStage Main = MakeStage(TEXT("Main"), { MakeObjective(TEXT("Kill"), TAG_Kill) });
	Main.bCompletesQuest = true;
	UDocQuestDefinition* Branchy = MakeQuest(TEXT("Branchy"), { Main, MakeStage(TEXT("Recover"), { MakeObjective(TEXT("Regroup"), TAG_Collect) }) });
	FDocQuestFailureRule ToRecover = Alarm;
	ToRecover.Action = EDocQuestFailureAction::BranchToStage;
	ToRecover.BranchStageId = TEXT("Recover");
	Branchy->FailureRules = { ToRecover };
	for (UDocQuestDefinition* Q : { FailWins, WinWins, Deadline, Branchy })
	{
		TestTrue(*FString::Printf(TEXT("Register %s"), *Q->QuestId.ToString()), F.S->RegisterQuestDefinition(Q).IsSuccess());
	}

	F.Activate(Player(1), TEXT("FailWins"));
	F.S->SubmitObservation(Obs(Player(1), TAG_Alarm));
	TestTrue(TEXT("Failure precedence"), F.State(Player(1), TEXT("FailWins")) == EDocQuestState::Failed);
	const int32 Terminals = F.QuestChanges.FilterByPredicate([](const FDocQuestChange& C) { return C.QuestId == FName(TEXT("FailWins")) && C.QuestState == EDocQuestState::Failed; }).Num();
	TestEqual(TEXT("Exactly one terminal transition"), Terminals, 1);
	TestTrue(TEXT("Terminal cannot be cancelled"), F.S->CancelQuest(Player(1), TEXT("FailWins")).ErrorTag == DocQuestTags::Error_Quest_Terminal);
	TestFalse(TEXT("Terminal cannot be reactivated"), F.Activate(Player(1), TEXT("FailWins")).IsSuccess());

	F.Activate(Player(2), TEXT("WinWins"));
	F.S->SubmitObservation(Obs(Player(2), TAG_Alarm));
	TestTrue(TEXT("Completion precedence"), F.State(Player(2), TEXT("WinWins")) == EDocQuestState::Completed);

	F.Activate(Player(3), TEXT("Deadline"));
	TestTrue(TEXT("Cancel"), F.S->CancelQuest(Player(3), TEXT("Deadline")).IsSuccess());
	TestTrue(TEXT("Cancelled"), F.State(Player(3), TEXT("Deadline")) == EDocQuestState::Cancelled);
	TestTrue(TEXT("Cancelled quest ignores events"), F.S->SubmitObservation(Obs(Player(3), TAG_Kill)).Outcome == EDocResultOutcome::NoChange);

	F.Activate(Player(4), TEXT("Deadline"));
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 4.0);
	TestTrue(TEXT("Required objective timeout fails the quest"), F.State(Player(4), TEXT("Deadline")) == EDocQuestState::Failed);

	F.Activate(Player(5), TEXT("Branchy"));
	F.S->SubmitObservation(Obs(Player(5), TAG_Alarm));
	const FDocQuestOwnerRecord R = F.Record(Player(5), TEXT("Branchy"));
	TestTrue(TEXT("Branch keeps the quest active"), R.Current.State == EDocQuestState::Active && R.Current.StageId == FName(TEXT("Recover")));
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-07
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestSaveRestoreTest, "Doc.Quest.SaveRestore", DocQuestTests::Flags)
bool FDocQuestSaveRestoreTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TSharedPtr<FRecordingEffects> Bag = MakeShared<FRecordingEffects>();
	auto Build = [&](UDocObjectiveSubsystem* S, bool bV2)
	{
		S->RegisterEffectProvider(TEXT("Bag"), Bag);
		UDocQuestDefinition* Main = MakeQuest(TEXT("Main"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kills"), TAG_Kill, 3), MakeWait(TEXT("Rest"), 10.f) }) });
		Main->Rewards = { MakeAction(TEXT("Gift"), TEXT("Bag")) };
		UDocQuestDefinition* Done = MakeQuest(TEXT("Done"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Once"), TAG_Talk) }) });
		Done->Rewards = { MakeAction(TEXT("Coin"), TEXT("Bag")) };
		UDocQuestDefinition* Rep = MakeQuest(TEXT("Rep"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Arrive"), TAG_Reach) }) });
		Rep->bRepeatable = true;
		UDocQuestDefinition* Mig = bV2
			? MakeQuest(TEXT("Mig"), { MakeStage(TEXT("New"), { MakeObjective(TEXT("KeepMe2"), TAG_Collect, 2) }) })
			: MakeQuest(TEXT("Mig"), { MakeStage(TEXT("Old"), { MakeObjective(TEXT("KeepMe"), TAG_Collect, 2), MakeObjective(TEXT("OptGone"), TAG_Alarm, 1, true) }) });
		if (bV2)
		{
			Mig->ContentVersion = 2;
			Mig->StageRedirects.Add(TEXT("Old"), TEXT("New"));
			Mig->ObjectiveRedirects.Add(TEXT("KeepMe"), TEXT("KeepMe2"));
		}
		UDocQuestDefinition* Crit = bV2
			? MakeQuest(TEXT("Crit"), { MakeStage(TEXT("Other"), { MakeObjective(TEXT("X"), TAG_Alarm) }) })
			: MakeQuest(TEXT("Crit"), { MakeStage(TEXT("Gone"), { MakeObjective(TEXT("X"), TAG_Alarm) }) });
		for (UDocQuestDefinition* Q : { Main, Done, Rep, Mig, Crit })
		{
			S->RegisterQuestDefinition(Q);
		}
		if (!bV2)
		{
			S->RegisterQuestDefinition(MakeQuest(TEXT("Ghost"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Boo"), TAG_Alarm) }) }));
		}
	};
	Build(F.S, false);
	const FDocOwnerScope P = Player(1);
	F.Activate(P, TEXT("Main"));
	F.Activate(P, TEXT("Done"));
	F.Activate(P, TEXT("Rep"));
	F.Activate(P, TEXT("Mig"));
	F.Activate(P, TEXT("Crit"));
	F.Activate(P, TEXT("Ghost"));
	F.S->SubmitObservation(Obs(P, TAG_Talk));
	F.S->SubmitObservation(Obs(P, TAG_Reach));
	F.Activate(P, TEXT("Rep"));
	const FGuid E1 = FGuid::NewGuid();
	F.S->SubmitObservation(Obs(P, TAG_Kill, 1, E1));
	F.S->SubmitObservation(Obs(P, TAG_Kill));
	F.S->SubmitObservation(Obs(P, TAG_Collect));
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 4.0);
	TestEqual(TEXT("Done's reward delivered once"), Bag->Applied(), 1);
	const FDocQuestOwnerRecord Before = F.Record(P, TEXT("Main"));

	TArray<uint8> Bytes;
	DocCoreSerialization::Encode(F.S->CaptureState(), Bytes);
	FDocQuestSaveData Saved;
	TestTrue(TEXT("Round trip"), DocCoreSerialization::Decode(Saved, Bytes));

	UDocObjectiveSubsystem* S2 = F.NewService();
	TStrongObjectPtr<UDocObjectiveSubsystem> Keep2(S2);
	Build(S2, false);
	const int32 CallsBefore = Bag->Calls.Num();
	TestTrue(TEXT("Restore"), S2->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("No reward replayed on restore"), Bag->Calls.Num(), CallsBefore);
	const FDocQuestOwnerRecord After = F.Record(P, TEXT("Main"), S2);
	TestEqual(TEXT("Instance id"), After.Current.QuestInstanceId, Before.Current.QuestInstanceId);
	TestEqual(TEXT("Progress"), F.Count(P, TEXT("Main"), TEXT("Kills"), S2), 2);
	const FDocObjectiveRuntimeState* Rest = After.Current.FindObjective(TEXT("Rest"));
	TestTrue(TEXT("Timer remaining restored"), Rest && FMath::IsNearlyEqual(Rest->WaitRemaining, 6.0));
	TestEqual(TEXT("Repeat identity"), F.Record(P, TEXT("Rep"), S2).Current.RepeatOrdinal, 2);
	TestTrue(TEXT("Dedup state restored"), S2->SubmitObservation(Obs(P, TAG_Kill, 1, E1)).Outcome == EDocResultOutcome::NoChange);
	S2->SubmitObservation(Obs(P, TAG_Kill));
	S2->AdvanceClock(EDocClockDomain::WorldGameplay, 6.0);
	TestTrue(TEXT("Completes after restore"), F.Record(P, TEXT("Main"), S2).Current.State == EDocQuestState::Completed);
	TestEqual(TEXT("Main's reward delivered once"), Bag->Applied(), 2);

	// Migration against v2 content.
	UDocObjectiveSubsystem* S3 = F.NewService();
	TStrongObjectPtr<UDocObjectiveSubsystem> Keep3(S3);
	Build(S3, true);
	TestTrue(TEXT("Restore into v2"), S3->RestoreState(Saved).IsSuccess());
	const FDocQuestOwnerRecord Mig = F.Record(P, TEXT("Mig"), S3);
	TestEqual(TEXT("Stage redirected"), Mig.Current.StageId, FName(TEXT("New")));
	TestEqual(TEXT("Objective redirected with progress"), F.Count(P, TEXT("Mig"), TEXT("KeepMe2"), S3), 1);
	const FDocObjectiveRuntimeState* Gone = Mig.Current.FindObjective(TEXT("OptGone"));
	TestTrue(TEXT("Removed optional objective quarantined"), Gone && Gone->bQuarantined);
	const FDocQuestOwnerRecord Crit = F.Record(P, TEXT("Crit"), S3);
	TestTrue(TEXT("Missing critical stage quarantines, never completes"), Crit.bQuarantined && Crit.Current.State == EDocQuestState::Active);
	TestTrue(TEXT("Missing definition quarantined"), F.Record(P, TEXT("Ghost"), S3).bQuarantined);
	const bool bPreserved = S3->CaptureState().Records.ContainsByPredicate([](const FDocQuestOwnerRecord& R) { return R.QuestId == FName(TEXT("Ghost")); });
	TestTrue(TEXT("Quarantined record preserved in saves"), bPreserved);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-08
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestCurrentVersusCumulativeTest, "Doc.Quest.CurrentVersusCumulative", DocQuestTests::Flags)
bool FDocQuestCurrentVersusCumulativeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TSharedPtr<FCountState> Inventory = MakeShared<FCountState>();
	F.S->RegisterStateProvider(TEXT("Inventory"), Inventory);
	FDocObjectiveDefinition Picked = MakeObjective(TEXT("Picked"), TAG_Herb, 3);
	Picked.Evaluator = EDocObjectiveEvaluator::Collect;
	FDocObjectiveDefinition Holding = MakeObjective(TEXT("Holding"), TAG_Herb, 3);
	Holding.Evaluator = EDocObjectiveEvaluator::Collect;
	Holding.ProgressMode = EDocObjectiveProgressMode::CurrentState;
	Holding.ProviderId = TEXT("Inventory");
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Herbs"), { MakeStage(TEXT("S1"), { Picked, Holding }) }));
	const FDocOwnerScope P = Player(1);

	Inventory->Value = 1;
	F.Activate(P, TEXT("Herbs"));
	TestEqual(TEXT("Current state queried on activation"), F.Count(P, TEXT("Herbs"), TEXT("Holding")), 1);
	F.S->SubmitObservation(Obs(P, TAG_Herb));
	F.S->SubmitObservation(Obs(P, TAG_Herb));
	TestEqual(TEXT("Cumulative counts events"), F.Count(P, TEXT("Herbs"), TEXT("Picked")), 2);
	TestEqual(TEXT("Events do not change current possession"), F.Count(P, TEXT("Herbs"), TEXT("Holding")), 1);
	Inventory->Value = 0;
	F.S->RefreshCurrentState(P);
	TestEqual(TEXT("Current state may regress before completion"), F.Count(P, TEXT("Herbs"), TEXT("Holding")), 0);
	Inventory->bAvailable = false;
	F.S->RefreshCurrentState(P);
	const FDocQuestOwnerRecord R = F.Record(P, TEXT("Herbs"));
	const FDocObjectiveRuntimeState* H = R.Current.FindObjective(TEXT("Holding"));
	TestTrue(TEXT("Unavailable provider is reported, not guessed"), H && H->Count == 0 && H->LastIgnoredReason.Contains(TEXT("unavailable")));
	Inventory->bAvailable = true;

	const int32 ChangesBefore = F.ObjectiveChanges.Num();
	TestTrue(TEXT("Restore snapshot"), F.S->RestoreState(F.S->CaptureState()).IsSuccess());
	TestEqual(TEXT("Restoring creates no collect events"), F.ObjectiveChanges.Num(), ChangesBefore);
	TestEqual(TEXT("Cumulative count unchanged by restore"), F.Count(P, TEXT("Herbs"), TEXT("Picked")), 2);
	TestEqual(TEXT("One refresh notification"), F.Refreshes, 1);

	Inventory->Value = 5;
	F.S->RefreshCurrentState(P);
	TestTrue(TEXT("Holding complete (clamped)"), F.ObjState(P, TEXT("Herbs"), TEXT("Holding")) == EDocObjectiveState::Completed);
	F.S->SubmitObservation(Obs(P, TAG_Herb));
	TestTrue(TEXT("Both satisfied"), F.State(P, TEXT("Herbs")) == EDocQuestState::Completed);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-09
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestRewardRetryTest, "Doc.Quest.RewardRetry", DocQuestTests::Flags)
bool FDocQuestRewardRetryTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TSharedPtr<FRecordingEffects> Bag = MakeShared<FRecordingEffects>();
	TSharedPtr<FRecordingEffects> Tags = MakeShared<FRecordingEffects>();
	TSharedPtr<FRecordingEffects> Broken = MakeShared<FRecordingEffects>();
	Broken->FailNext = 100;
	Broken->FailOutcome = EDocResultOutcome::InvalidInput;
	F.S->RegisterEffectProvider(TEXT("Bag"), Bag);
	F.S->RegisterEffectProvider(TEXT("Tags"), Tags);
	F.S->RegisterEffectProvider(TEXT("Broken"), Broken);

	UDocQuestDefinition* Q = MakeQuest(TEXT("Hunt"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kill"), TAG_Kill) }) });
	Q->Rewards = { MakeAction(TEXT("Sword"), TEXT("Bag")), MakeAction(TEXT("Title"), NAME_None, EDocQuestActionType::GrantTag) };
	UDocQuestDefinition* Lost = MakeQuest(TEXT("Lost"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kill"), TAG_Kill) }) });
	Lost->Rewards = { MakeAction(TEXT("Gem"), TEXT("Bag")) };
	UDocQuestDefinition* Bad = MakeQuest(TEXT("Bad"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kill"), TAG_Kill) }) });
	Bad->Rewards = { MakeAction(TEXT("Nope"), TEXT("Broken")) };
	UDocQuestDefinition* Claim = MakeQuest(TEXT("Claim"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kill"), TAG_Kill) }) });
	Claim->Rewards = { MakeAction(TEXT("Chest"), TEXT("Bag")) };
	Claim->RewardPolicy = EDocQuestRewardPolicy::ManualClaim;
	for (UDocQuestDefinition* D : { Q, Lost, Bad, Claim })
	{
		F.S->RegisterQuestDefinition(D);
	}

	Bag->FailNext = 1; // full inventory
	F.Activate(Player(1), TEXT("Hunt"));
	F.S->SubmitObservation(Obs(Player(1), TAG_Kill));
	FDocQuestOwnerRecord R = F.Record(Player(1), TEXT("Hunt"));
	TestTrue(TEXT("Completed stays completed"), R.Current.State == EDocQuestState::Completed);
	TestTrue(TEXT("Reward pending is a separate fact"), R.Current.bRewardDeliveryPending);
	TestEqual(TEXT("Partial: the other reward delivered"), Tags->Applied(), 1);
	TestEqual(TEXT("Nothing in the bag yet"), Bag->Applied(), 0);
	TestTrue(TEXT("Retry"), F.S->RetryRewardDelivery(Player(1), TEXT("Hunt")).IsSuccess());
	TestFalse(TEXT("Delivered"), F.Record(Player(1), TEXT("Hunt")).Current.bRewardDeliveryPending);
	TestTrue(TEXT("Second retry is a no-op"), F.S->RetryRewardDelivery(Player(1), TEXT("Hunt")).Outcome == EDocResultOutcome::NoChange);
	TestEqual(TEXT("Delivered once"), Bag->Applied(), 1);
	TestTrue(TEXT("Same effect key on every attempt"), Bag->Calls.Num() == 2 && Bag->Calls[0] == Bag->Calls[1]);

	Bag->bLoseResponseOnce = true;
	F.Activate(Player(2), TEXT("Lost"));
	F.S->SubmitObservation(Obs(Player(2), TAG_Kill));
	TestTrue(TEXT("Lost response leaves the intent pending"), F.Record(Player(2), TEXT("Lost")).Current.bRewardDeliveryPending);
	TestTrue(TEXT("Receipt recovery"), F.S->RetryRewardDelivery(Player(2), TEXT("Lost")).IsSuccess());
	TestEqual(TEXT("Consumer applied it once"), Bag->Applied(), 2);

	F.Activate(Player(3), TEXT("Bad"));
	F.S->SubmitObservation(Obs(Player(3), TAG_Kill));
	R = F.Record(Player(3), TEXT("Bad"));
	TestTrue(TEXT("Permanent refusal is visible, not dropped"), R.Current.Intents.Num() == 1 && R.Current.Intents[0].State == EDocRewardIntentState::Failed);

	F.Activate(Player(4), TEXT("Claim"));
	F.S->SubmitObservation(Obs(Player(4), TAG_Kill));
	TestTrue(TEXT("Claim flow keeps the reward pending"), F.Record(Player(4), TEXT("Claim")).Current.bRewardDeliveryPending);
	TestTrue(TEXT("Claim"), F.S->RetryRewardDelivery(Player(4), TEXT("Claim")).IsSuccess());
	TestEqual(TEXT("Claimed"), Bag->Applied(), 3);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-10
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestTrackingTest, "Doc.Quest.Tracking", DocQuestTests::Flags)
bool FDocQuestTrackingTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocObjectiveDefinition Manual = MakeObjective(TEXT("B"), TAG_Collect);
	Manual.TrackingMode = EDocObjectiveTrackingMode::Manual;
	FDocObjectiveDefinition Never = MakeObjective(TEXT("C"), TAG_Talk);
	Never.TrackingMode = EDocObjectiveTrackingMode::NotTrackable;
	UDocQuestDefinition* Q = MakeQuest(TEXT("Siege"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("A"), TAG_Kill), Manual, Never }) });
	Q->OwnerPolicy = EDocQuestOwnerPolicy::SharedWorld;
	F.S->RegisterQuestDefinition(Q);
	TestTrue(TEXT("Shared quest"), F.Activate(Shared(), TEXT("Siege")).IsSuccess());
	const int64 Revision = F.Record(Shared(), TEXT("Siege")).Current.Revision;

	TestTrue(TEXT("P1 tracks the quest"), F.S->SetTrackedQuest(Player(1), Shared(), TEXT("Siege")).IsSuccess());
	TArray<FDocTrackedQuestRef> P1 = F.S->GetTrackedObjectives(Player(1));
	TestTrue(TEXT("Auto objective only"), P1.Num() == 1 && P1[0].ObjectiveId == FName(TEXT("A")));
	TestTrue(TEXT("P2 tracks one objective"), F.S->SetTrackedObjective(Player(2), Shared(), TEXT("Siege"), TEXT("B"), true).IsSuccess());
	TArray<FDocTrackedQuestRef> P2 = F.S->GetTrackedObjectives(Player(2));
	TestTrue(TEXT("P2 differs"), P2.Num() == 1 && P2[0].ObjectiveId == FName(TEXT("B")));
	TestFalse(TEXT("P2 has no tracked quest"), F.S->GetTrackedQuest(Player(2)).IsSet());
	TestTrue(TEXT("Not trackable"), F.S->SetTrackedObjective(Player(2), Shared(), TEXT("Siege"), TEXT("C"), true).ErrorTag == DocQuestTags::Error_Quest_NotTrackable);

	TestTrue(TEXT("Untrack"), F.S->SetTrackedQuest(Player(1), Shared(), NAME_None).IsSuccess());
	TestTrue(TEXT("Untracking does not cancel"), F.State(Shared(), TEXT("Siege")) == EDocQuestState::Active);
	TestEqual(TEXT("Tracking never changes progress"), F.Record(Shared(), TEXT("Siege")).Current.Revision, Revision);

	F.S->SubmitObservation(Obs(Shared(), TAG_Collect));
	TestEqual(TEXT("Completed objective leaves P2's tracked list"), F.S->GetTrackedObjectives(Player(2)).Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// OBJ-11 (local part; the network profile needs a transport bridge)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestWorldAndAuthorityTest, "Doc.Quest.WorldAndAuthority", DocQuestTests::Flags)
bool FDocQuestWorldAndAuthorityTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Travel"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Kills"), TAG_Kill, 3) }) }));
	const FDocOwnerScope P = Player(1);
	F.Activate(P, TEXT("Travel"));

	const int32 G1 = F.S->GetWorldGeneration();
	FDocQuestObservation InWorld = Obs(P, TAG_Kill);
	InWorld.WorldGeneration = G1;
	TestTrue(TEXT("Current world accepted"), F.S->SubmitObservation(InWorld).IsSuccess());

	F.S->DetachWorld(F.TW.World);
	TestTrue(TEXT("Records retained across travel"), F.State(P, TEXT("Travel")) == EDocQuestState::Active);
	FDocQuestObservation Late = Obs(P, TAG_Kill);
	Late.WorldGeneration = G1;
	TestTrue(TEXT("Old-world event rejected"), F.S->SubmitObservation(Late).ErrorTag == DocQuestTags::Error_Quest_WrongWorld);
	{
		FDocScopedTestWorld Next; // its facade attaches the new world through the service
		TestTrue(TEXT("Facade rebinds"), F.S->IsAttachedTo(Next.World));
		FDocQuestObservation NewWorld = Obs(P, TAG_Kill);
		NewWorld.WorldGeneration = F.S->GetWorldGeneration();
		TestTrue(TEXT("New world accepted"), F.S->SubmitObservation(NewWorld).IsSuccess());
	}
	TestEqual(TEXT("Progress carried"), F.Count(P, TEXT("Travel"), TEXT("Kills")), 2);

	F.S->UnregisterTrustedSource(TEXT("Test"));
	TestTrue(TEXT("Forged observation rejected"), F.S->SubmitObservation(Obs(P, TAG_Kill)).Outcome == EDocResultOutcome::PermissionDenied);
	F.S->RegisterTrustedSource(TEXT("Test"));
	TestEqual(TEXT("Indexed while active"), F.S->GetIndexedTagCountForTesting(P), 1);
	F.S->SubmitObservation(Obs(P, TAG_Kill));
	TestTrue(TEXT("Completed"), F.State(P, TEXT("Travel")) == EDocQuestState::Completed);
	TestEqual(TEXT("Completed work unsubscribed"), F.S->GetIndexedTagCountForTesting(P), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestWaitTest, "Doc.Quest.Wait", DocQuestTests::Flags)
bool FDocQuestWaitTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Rest"), { MakeStage(TEXT("S1"), { MakeWait(TEXT("Sleep"), 10.f) }) }));
	FDocObjectiveDefinition Offline = MakeWait(TEXT("Brew"), 10.f);
	Offline.bAllowOfflineProgress = true;
	F.S->RegisterQuestDefinition(MakeQuest(TEXT("Brew"), { MakeStage(TEXT("S1"), { Offline }) }));

	F.Activate(Player(1), TEXT("Rest"));
	TestEqual(TEXT("One active timer"), F.S->GetActiveTimerCountForTesting(), 1);
	F.S->AdvanceClock(EDocClockDomain::RealTime, 20.0);
	TestTrue(TEXT("Other clocks do not advance it"), F.State(Player(1), TEXT("Rest")) == EDocQuestState::Active);
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 4.0);
	const FDocObjectiveRuntimeState* Sleep = F.Record(Player(1), TEXT("Rest")).Current.FindObjective(TEXT("Sleep"));
	TestTrue(TEXT("Remaining tracked"), Sleep && FMath::IsNearlyEqual(Sleep->WaitRemaining, 6.0));
	const int32 Before = F.QuestChanges.Num();
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 1000.0);
	TestTrue(TEXT("Time jump completes once"), F.State(Player(1), TEXT("Rest")) == EDocQuestState::Completed);
	TestEqual(TEXT("One coherent transition"), F.QuestChanges.Num() - Before, 1);
	TestEqual(TEXT("Completed timers unindexed"), F.S->GetActiveTimerCountForTesting(), 0);

	F.Activate(Player(2), TEXT("Rest"));
	F.Activate(Player(2), TEXT("Brew"));
	F.S->ApplyOfflineElapsed(Player(2), 20.0);
	TestTrue(TEXT("Offline progress is opt-in"), F.State(Player(2), TEXT("Rest")) == EDocQuestState::Active);
	TestTrue(TEXT("Opted-in objective advanced"), F.State(Player(2), TEXT("Brew")) == EDocQuestState::Completed);
	return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocQuestValidationTest, "Doc.Quest.Validation", DocQuestTests::Flags)
bool FDocQuestValidationTest::RunTest(const FString& Parameters)
{
	auto Errors = [](UDocQuestDefinition* Q)
	{
		TArray<FString> E, W;
		Q->FindProblems(E, W);
		return E;
	};
	auto Any = [](const TArray<FString>& Lines, const TCHAR* Needle) { return Lines.ContainsByPredicate([Needle](const FString& L) { return L.Contains(Needle); }); };

	UDocQuestDefinition* Empty = MakeQuest(TEXT("Empty"), { MakeStage(TEXT("S1"), { MakeObjective(TEXT("Opt"), TAG_Kill, 1, true) }) });
	TestTrue(TEXT("Accidental empty required set"), Any(Errors(Empty), TEXT("no required objectives")));
	Empty->Stages[0].bPassThrough = true;
	TestEqual(TEXT("Explicit pass-through is fine"), Errors(Empty).Num(), 0);

	FDocObjectiveDefinition Zero = MakeObjective(TEXT("Zero"), TAG_Kill, 0);
	FDocObjectiveDefinition NoTag = MakeObjective(TEXT("NoTag"), FGameplayTag());
	FDocQuestStage Broken = MakeStage(TEXT("S1"), { Zero, NoTag, MakeObjective(TEXT("Zero"), TAG_Kill) });
	Broken.NextStage = TEXT("Nowhere");
	UDocQuestDefinition* Bad = MakeQuest(TEXT("Bad"), { Broken });
	const TArray<FString> E = Errors(Bad);
	TestTrue(TEXT("Positive target"), Any(E, TEXT("TargetCount must be positive")));
	TestTrue(TEXT("Event tag required"), Any(E, TEXT("needs EventTag")));
	TestTrue(TEXT("Duplicate objective id"), Any(E, TEXT("missing or duplicated")));
	TestTrue(TEXT("Broken stage link"), Any(E, TEXT("unknown stage Nowhere")));

	FFixture F;
	TestTrue(TEXT("Invalid definitions are refused"), F.S->RegisterQuestDefinition(Bad).Outcome == EDocResultOutcome::InvalidConfiguration);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
