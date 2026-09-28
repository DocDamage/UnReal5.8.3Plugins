#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocPuzzleSubsystem.h"
#include "DocPuzzleComponent.h"
#include "DocPuzzleInputComponent.h"
#include "DocPuzzleDefinition.h"
#include "DocPuzzleRule.h"
#include "UObject/Package.h"
#include "Engine/World.h"

namespace DocPuzzleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UDocPuzzleDefinition* CreateTestDefinition(UDocPuzzleRule* RootRule, FName DefId = TEXT("Def_TestPuzzle"))
	{
		UDocPuzzleDefinition* Def = NewObject<UDocPuzzleDefinition>(GetTransientPackage());
		Def->DefinitionId = DefId;
		Def->RootRule = RootRule;
		Def->bAllowReset = true;
		return Def;
	}

	UDocPuzzleRule_OrderedInputs* CreateOrderedRule(FName RuleId, const TArray<FName>& InputNames, EDocPuzzleOrderedMismatchPolicy MismatchPolicy)
	{
		UDocPuzzleRule_OrderedInputs* Rule = NewObject<UDocPuzzleRule_OrderedInputs>(GetTransientPackage());
		Rule->RuleId = RuleId;
		Rule->MismatchPolicy = MismatchPolicy;
		for (const FName& Name : InputNames)
		{
			FDocPuzzleOrderedStep Step;
			Step.InputId = Name;
			Step.ExpectedValue = FDocPuzzleInputValue::MakeTrigger();
			Rule->ExpectedSequence.Add(Step);
		}
		return Rule;
	}

	UDocPuzzleRule_Composite* CreateComposite(FName RuleId, EDocPuzzleCompositionMode Mode, const TArray<UDocPuzzleRule*>& Children)
	{
		UDocPuzzleRule_Composite* Rule = NewObject<UDocPuzzleRule_Composite>(GetTransientPackage());
		Rule->RuleId = RuleId;
		Rule->Mode = Mode;
		for (UDocPuzzleRule* Child : Children)
		{
			Rule->ChildRules.Add(Child);
		}
		return Rule;
	}

	FDocPuzzleInputEvent MakeEvent(const FGuid& InstanceId, const FGuid& AttemptId, FName InputId, const FDocPuzzleInputValue& Value = FDocPuzzleInputValue::MakeTrigger())
	{
		FDocPuzzleInputEvent Ev;
		Ev.InstanceId = InstanceId;
		Ev.AttemptId = AttemptId;
		Ev.InputId = InputId;
		Ev.Value = Value;
		return Ev;
	}

	/** Test world with a controllable authority clock and event counters. */
	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocPuzzleSubsystem* Subsystem = nullptr;
		TSharedRef<double> Clock = MakeShared<double>(0.0);
		TSharedRef<int32> Solved = MakeShared<int32>(0);
		TSharedRef<int32> Failed = MakeShared<int32>(0);

		FFixture()
		{
			Subsystem = TW.GetSubsystem<UDocPuzzleSubsystem>();
			check(Subsystem);
			TSharedRef<double> C = Clock;
			Subsystem->SetClockOverride([C]() { return *C; });
			TSharedRef<int32> S = Solved;
			TSharedRef<int32> F = Failed;
			Subsystem->OnPuzzleSolvedNative.AddLambda([S](const FGuid&, const FGuid&) { ++(*S); });
			Subsystem->OnAttemptFailedNative.AddLambda([F](const FGuid&, const FGuid&) { ++(*F); });
		}

		void SetTime(double T) { *Clock = T; }

		FGuid Start(UDocPuzzleDefinition* Def, FGuid& OutAttempt)
		{
			const FGuid InstanceId = FGuid::NewGuid();
			Subsystem->RegisterPuzzle(InstanceId, Def, FDocOwnerScope());
			Subsystem->StartAttempt(InstanceId, OutAttempt);
			return InstanceId;
		}
	};
}

// PUZ-01: Doc.Puzzle.OrderedInputs
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleOrderedInputsTest, FAutomationTestBase, "Doc.Puzzle.OrderedInputs", DocPuzzleTests::Flags)
bool FDocPuzzleOrderedInputsTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	// ResetProgress
	{
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Ordered"), { TEXT("A"), TEXT("B"), TEXT("C") }, EDocPuzzleOrderedMismatchPolicy::ResetProgress)), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("A")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("B")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Wrong")));
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestFalse(TEXT("Reset: not solved after mismatch"), Eval.IsSolved());
		TestEqual(TEXT("Reset: progress back to 0"), Eval.RuleProgress[0].NormalizedProgress, 0.0f);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("A")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("B")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("C")));
		TestTrue(TEXT("Reset: solved after the full sequence"), F.Subsystem->IsPuzzleSolved(Id));
	}

	// FailAttempt
	{
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_OrderedFail"), { TEXT("A"), TEXT("B") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt)), Attempt);
		const int32 FailedBefore = *F.Failed;
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Wrong")));
		TestEqual(TEXT("Fail: state Failed"), F.Subsystem->GetPuzzleState(Id), EDocPuzzleState::Failed);
		TestEqual(TEXT("Fail: one failure event"), *F.Failed - FailedBefore, 1);
	}

	// IgnoreUnexpected keeps progress
	{
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Ignore"), { TEXT("A"), TEXT("B") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected)), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("A")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Wrong")));
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestEqual(TEXT("Ignore: progress kept at 1 of 2"), Eval.RuleProgress[0].NormalizedProgress, 0.5f);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("B")));
		TestTrue(TEXT("Ignore: solved"), F.Subsystem->IsPuzzleSolved(Id));
	}

	// A held button cannot satisfy two identical steps: each step needs a new edge.
	{
		UDocPuzzleRule_OrderedInputs* Rule = NewObject<UDocPuzzleRule_OrderedInputs>(GetTransientPackage());
		Rule->RuleId = TEXT("Rule_Held");
		Rule->MismatchPolicy = EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected;
		FDocPuzzleOrderedStep Step;
		Step.InputId = TEXT("Btn");
		Step.ExpectedValue = FDocPuzzleInputValue::MakeBoolean(true);
		Rule->ExpectedSequence.Add(Step);
		Rule->ExpectedSequence.Add(Step);
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(Rule), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Btn"), FDocPuzzleInputValue::MakeBoolean(true)));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Btn"), FDocPuzzleInputValue::MakeBoolean(true)));
		TestFalse(TEXT("Held: re-sent 'pressed' is not a second press"), F.Subsystem->IsPuzzleSolved(Id));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Btn"), FDocPuzzleInputValue::MakeBoolean(false)));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Btn"), FDocPuzzleInputValue::MakeBoolean(true)));
		TestTrue(TEXT("Held: release and press again completes"), F.Subsystem->IsPuzzleSolved(Id));
	}
	return true;
}

// PUZ-02: Doc.Puzzle.DuplicateInput
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleDuplicateInputTest, FAutomationTestBase, "Doc.Puzzle.DuplicateInput", DocPuzzleTests::Flags)
bool FDocPuzzleDuplicateInputTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	{
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Seq"), { TEXT("A"), TEXT("A"), TEXT("A") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected)), Attempt);
		const FGuid SourceId = FGuid::NewGuid();
		FDocPuzzleInputEvent Ev = MakeEvent(Id, Attempt, TEXT("A"));
		Ev.SourceId = SourceId;
		Ev.SourceSequenceNumber = 2;
		TestTrue(TEXT("First sequence submission succeeds"), F.Subsystem->SubmitInput(Ev).IsSuccess());
		const FDocSystemResult Dup = F.Subsystem->SubmitInput(Ev);
		TestFalse(TEXT("Duplicate (source, sequence) is rejected"), Dup.IsSuccess());
		TestEqual(TEXT("Outcome is Conflict"), Dup.Outcome, EDocResultOutcome::Conflict);
		Ev.SourceSequenceNumber = 1;
		TestFalse(TEXT("Older sequence from the same source is rejected"), F.Subsystem->SubmitInput(Ev).IsSuccess());
		Ev.SourceId = FGuid::NewGuid();
		TestTrue(TEXT("Same sequence from another source is accepted"), F.Subsystem->SubmitInput(Ev).IsSuccess());
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestTrue(TEXT("Only two presses counted"), FMath::IsNearlyEqual(Eval.RuleProgress[0].NormalizedProgress, 2.0f / 3.0f, 1e-4f));
	}

	// Declared inputs are enforced: kind, range, declaration and debounce.
	{
		UDocPuzzleRule_OrderedInputs* Rule = NewObject<UDocPuzzleRule_OrderedInputs>(GetTransientPackage());
		Rule->RuleId = TEXT("Rule_Dial");
		Rule->MismatchPolicy = EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected;
		FDocPuzzleOrderedStep Step;
		Step.InputId = TEXT("Dial");
		Step.ExpectedValue = FDocPuzzleInputValue::MakeScalar(7.0f);
		Rule->ExpectedSequence.Add(Step);
		UDocPuzzleDefinition* Def = CreateTestDefinition(Rule);
		FDocPuzzleInputDefinition Dial;
		Dial.InputId = TEXT("Dial");
		Dial.AcceptedKind = EDocPuzzleInputKind::Scalar;
		Dial.MinScalar = 0.0f;
		Dial.MaxScalar = 10.0f;
		Dial.DebounceSeconds = 0.5f;
		Def->Inputs.Add(Dial);

		F.SetTime(1.0);
		FGuid Attempt;
		const FGuid Id = F.Start(Def, Attempt);
		TestEqual(TEXT("Out-of-range scalar refused"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Dial"), FDocPuzzleInputValue::MakeScalar(20.0f))).Outcome, EDocResultOutcome::InvalidInput);
		TestEqual(TEXT("Wrong kind refused"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Dial"))).Outcome, EDocResultOutcome::InvalidInput);
		TestEqual(TEXT("Undeclared input refused"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Other"))).Outcome, EDocResultOutcome::InvalidInput);
		TestTrue(TEXT("In-range scalar accepted"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Dial"), FDocPuzzleInputValue::MakeScalar(5.0f))).IsSuccess());
		F.SetTime(1.2);
		TestFalse(TEXT("Input inside the debounce window refused"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Dial"), FDocPuzzleInputValue::MakeScalar(7.0f))).IsSuccess());
		F.SetTime(1.6);
		TestTrue(TEXT("Input after the debounce window accepted"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Dial"), FDocPuzzleInputValue::MakeScalar(7.0f))).IsSuccess());
		TestTrue(TEXT("Dial puzzle solved"), F.Subsystem->IsPuzzleSolved(Id));
	}
	return true;
}

// PUZ-03: Doc.Puzzle.ContributorOwnership
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleContributorOwnershipTest, FAutomationTestBase, "Doc.Puzzle.ContributorOwnership", DocPuzzleTests::Flags)
bool FDocPuzzleContributorOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	UDocPuzzleRule_WeightedThreshold* Rule = NewObject<UDocPuzzleRule_WeightedThreshold>(GetTransientPackage());
	Rule->RuleId = TEXT("Rule_Plate");
	Rule->RequiredThreshold = 2.0f;
	FDocPuzzleWeightedInput WeightInput;
	WeightInput.InputId = TEXT("PressurePlate");
	WeightInput.WeightMultiplier = 1.0f;
	Rule->ContributingInputs.Add(WeightInput);

	FGuid Attempt;
	const FGuid Id = F.Start(CreateTestDefinition(Rule), Attempt);
	const FGuid SourceA = FGuid::NewGuid();
	const FGuid SourceB = FGuid::NewGuid();

	F.Subsystem->SetInputContributor(Id, SourceA, TEXT("PressurePlate"), FDocPuzzleInputValue::MakeScalar(1.0f));
	TestFalse(TEXT("Weight 1.0 < 2.0 not solved"), F.Subsystem->IsPuzzleSolved(Id));
	F.Subsystem->SetInputContributor(Id, SourceA, TEXT("PressurePlate"), FDocPuzzleInputValue::MakeScalar(1.0f));
	TestFalse(TEXT("Re-sending the same contributor does not double its weight"), F.Subsystem->IsPuzzleSolved(Id));
	F.Subsystem->SetInputContributor(Id, SourceB, TEXT("PressurePlate"), FDocPuzzleInputValue::MakeScalar(1.0f));
	TestTrue(TEXT("Weight 2.0 >= 2.0 solved"), F.Subsystem->IsPuzzleSolved(Id));
	TestEqual(TEXT("One solve event"), *F.Solved, 1);

	TestTrue(TEXT("Contributor A leaves"), F.Subsystem->RemoveInputContributor(Id, SourceA, TEXT("PressurePlate")).IsSuccess());
	TestTrue(TEXT("Removing again is repeat-safe"), F.Subsystem->RemoveInputContributor(Id, SourceA, TEXT("PressurePlate")).IsSuccess());
	FDocPuzzleEvaluation Eval;
	F.Subsystem->EvaluatePuzzle(Id, Eval);
	TestEqual(TEXT("Only A's weight removed (50% progress)"), Eval.RuleProgress[0].NormalizedProgress, 0.5f);
	TestTrue(TEXT("Solved stays solved"), F.Subsystem->IsPuzzleSolved(Id));
	TestFalse(TEXT("Contributor without a source refused"), F.Subsystem->SetInputContributor(Id, FGuid(), TEXT("PressurePlate"), FDocPuzzleInputValue::MakeScalar(1.0f)).IsSuccess());
	return true;
}

// PUZ-04: Doc.Puzzle.SimultaneousDwell
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleSimultaneousDwellTest, FAutomationTestBase, "Doc.Puzzle.SimultaneousDwell", DocPuzzleTests::Flags)
bool FDocPuzzleSimultaneousDwellTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	UDocPuzzleRule_Simultaneous* Rule = NewObject<UDocPuzzleRule_Simultaneous>(GetTransientPackage());
	Rule->RuleId = TEXT("Rule_Dwell");
	Rule->MinDwellSeconds = 2.0f;
	FDocPuzzleSimultaneousRequirement Req1; Req1.InputId = TEXT("Lever1"); Req1.RequiredValue = FDocPuzzleInputValue::MakeBoolean(true);
	FDocPuzzleSimultaneousRequirement Req2; Req2.InputId = TEXT("Lever2"); Req2.RequiredValue = FDocPuzzleInputValue::MakeBoolean(true);
	Rule->RequiredInputs.Add(Req1);
	Rule->RequiredInputs.Add(Req2);

	FGuid Attempt;
	const FGuid Id = F.Start(CreateTestDefinition(Rule), Attempt);
	const FGuid S1 = FGuid::NewGuid();
	const FGuid S2 = FGuid::NewGuid();

	F.SetTime(1.0);
	F.Subsystem->SetInputContributor(Id, S1, TEXT("Lever1"), FDocPuzzleInputValue::MakeBoolean(true));
	F.Subsystem->SetInputContributor(Id, S2, TEXT("Lever2"), FDocPuzzleInputValue::MakeBoolean(true)); // dwell starts at 1.0
	F.SetTime(2.0);
	F.Subsystem->ProcessTimers();
	TestFalse(TEXT("1.0 s of dwell is not enough"), F.Subsystem->IsPuzzleSolved(Id));

	F.SetTime(2.5);
	F.Subsystem->RemoveInputContributor(Id, S1, TEXT("Lever1")); // continuous dwell broken
	F.SetTime(3.0);
	F.Subsystem->SetInputContributor(Id, S1, TEXT("Lever1"), FDocPuzzleInputValue::MakeBoolean(true)); // dwell restarts at 3.0

	F.SetTime(4.9);
	F.Subsystem->ProcessTimers();
	TestFalse(TEXT("Broken dwell does not carry over (1.9 s)"), F.Subsystem->IsPuzzleSolved(Id));

	// EvaluatePuzzle is a view: even when the dwell is complete it never solves or notifies.
	F.SetTime(5.0);
	FDocPuzzleEvaluation Eval;
	F.Subsystem->EvaluatePuzzle(Id, Eval);
	TestEqual(TEXT("View shows the attempt still active"), Eval.State, EDocPuzzleState::AttemptActive);
	TestEqual(TEXT("View does not notify"), *F.Solved, 0);

	F.Subsystem->ProcessTimers();
	TestTrue(TEXT("Timer commit completes the 2.0 s dwell"), F.Subsystem->IsPuzzleSolved(Id));
	TestEqual(TEXT("Exactly one solve event"), *F.Solved, 1);
	return true;
}

// PUZ-05: Doc.Puzzle.DeadlineBoundary
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleDeadlineBoundaryTest, FAutomationTestBase, "Doc.Puzzle.DeadlineBoundary", DocPuzzleTests::Flags)
bool FDocPuzzleDeadlineBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	auto MakeTimedDef = []()
	{
		UDocPuzzleRule_TimedSequence* Timed = NewObject<UDocPuzzleRule_TimedSequence>(GetTransientPackage());
		Timed->RuleId = TEXT("Rule_Timed");
		Timed->ChildRule = CreateOrderedRule(TEXT("ChildSeq"), { TEXT("Tap1"), TEXT("Tap2") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt);
		Timed->WindowDurationSeconds = 2.0f;
		return CreateTestDefinition(Timed);
	};

	// Just inside [1.0, 3.0): solved.
	{
		F.SetTime(1.0);
		FGuid Attempt;
		const FGuid Id = F.Start(MakeTimedDef(), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Tap1")));
		F.SetTime(2.999);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Tap2")));
		TestTrue(TEXT("Input before the deadline solves"), F.Subsystem->IsPuzzleSolved(Id));
	}

	// Exactly at the deadline is late, and the caller's timestamp cannot pull it back inside.
	{
		F.SetTime(1.0);
		FGuid Attempt;
		const FGuid Id = F.Start(MakeTimedDef(), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Tap1")));
		F.SetTime(3.0);
		FDocPuzzleInputEvent Late = MakeEvent(Id, Attempt, TEXT("Tap2"));
		Late.AcceptedTimestamp = 1.5; // untrusted caller time
		F.Subsystem->SubmitInput(Late);
		TestEqual(TEXT("Input exactly at the deadline fails the attempt"), F.Subsystem->GetPuzzleState(Id), EDocPuzzleState::Failed);
	}

	// Timeout callback first, then the input: same result.
	{
		F.SetTime(1.0);
		FGuid Attempt;
		const FGuid Id = F.Start(MakeTimedDef(), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Tap1")));
		F.SetTime(3.0);
		F.Subsystem->ProcessTimers();
		TestEqual(TEXT("Window expiry fails the attempt on the timer"), F.Subsystem->GetPuzzleState(Id), EDocPuzzleState::Failed);
		TestFalse(TEXT("The late input is then rejected"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Tap2"))).IsSuccess());
		TestFalse(TEXT("Never solved"), F.Subsystem->IsPuzzleSolved(Id));
	}

	// Attempt timeout uses the same half-open boundary.
	{
		UDocPuzzleDefinition* Def = CreateTestDefinition(CreateOrderedRule(TEXT("Rule_TO"), { TEXT("X"), TEXT("Y") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected));
		Def->TimeoutSeconds = 5.0f;
		F.SetTime(10.0);
		FGuid Attempt;
		const FGuid Id = F.Start(Def, Attempt);
		F.SetTime(14.9);
		TestTrue(TEXT("Before timeout accepted"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("X"))).IsSuccess());
		F.SetTime(15.0);
		TestEqual(TEXT("At timeout: TimedOut"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Y"))).Outcome, EDocResultOutcome::TimedOut);
		TestEqual(TEXT("Attempt failed"), F.Subsystem->GetPuzzleState(Id), EDocPuzzleState::Failed);
	}
	return true;
}

// PUZ-06: Doc.Puzzle.AlternativeSolutions
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleAlternativeSolutionsTest, FAutomationTestBase, "Doc.Puzzle.AlternativeSolutions", DocPuzzleTests::Flags)
bool FDocPuzzleAlternativeSolutionsTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	{
		UDocPuzzleRule_Composite* Any = CreateComposite(TEXT("Rule_Any"), EDocPuzzleCompositionMode::Any, {
			CreateOrderedRule(TEXT("PathA"), { TEXT("Keycard") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected),
			CreateOrderedRule(TEXT("PathB"), { TEXT("HackTool") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected) });
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(Any), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("HackTool")));
		TestTrue(TEXT("Solved via PathB"), F.Subsystem->IsPuzzleSolved(Id));
		TestEqual(TEXT("One solve notification"), *F.Solved, 1);
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestEqual(TEXT("Winning rule is the branch, not the root"), Eval.WinningRuleId, FName(TEXT("PathB")));
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Keycard")));
		TestEqual(TEXT("PathA afterwards does not re-solve"), *F.Solved, 1);
	}

	// Both alternatives complete in the same revision: one completion, deterministic winner.
	{
		UDocPuzzleRule_Composite* Any = CreateComposite(TEXT("Rule_Any2"), EDocPuzzleCompositionMode::Any, {
			CreateOrderedRule(TEXT("PathA2"), { TEXT("Key") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected),
			CreateOrderedRule(TEXT("PathB2"), { TEXT("Key") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected) });
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(Any), Attempt);
		const int32 Before = *F.Solved;
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Key")));
		TestEqual(TEXT("Two alternatives at once: one completion"), *F.Solved - Before, 1);
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestEqual(TEXT("First authored alternative wins"), Eval.WinningRuleId, FName(TEXT("PathA2")));
	}

	// Explanations name the unsatisfied branches.
	{
		UDocPuzzleRule_Composite* All = CreateComposite(TEXT("Rule_All"), EDocPuzzleCompositionMode::All, {
			CreateOrderedRule(TEXT("NeedX"), { TEXT("X") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected),
			CreateOrderedRule(TEXT("NeedY"), { TEXT("Y") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected) });
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(All), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("X")));
		FDocPuzzleEvaluation Eval;
		F.Subsystem->EvaluatePuzzle(Id, Eval);
		TestTrue(TEXT("Unsatisfied branch listed"), Eval.UnsatisfiedRuleIds.Contains(FName(TEXT("NeedY"))));
		TestFalse(TEXT("Satisfied branch not listed"), Eval.UnsatisfiedRuleIds.Contains(FName(TEXT("NeedX"))));
	}
	return true;
}

// PUZ-07: Doc.Puzzle.ResetEpoch
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleResetEpochTest, FAutomationTestBase, "Doc.Puzzle.ResetEpoch", DocPuzzleTests::Flags)
bool FDocPuzzleResetEpochTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	FGuid Attempt1;
	const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_ResetSeq"), { TEXT("Step1"), TEXT("Step2") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt)), Attempt1);
	TestEqual(TEXT("Initial ResetEpoch is 0"), F.Subsystem->GetResetEpoch(Id), 0);

	FGuid Again;
	TestFalse(TEXT("A second StartAttempt while active is refused"), F.Subsystem->StartAttempt(Id, Again).IsSuccess());

	F.Subsystem->SubmitInput(MakeEvent(Id, Attempt1, TEXT("Step1")));
	TestTrue(TEXT("Reset"), F.Subsystem->RequestReset(Id).IsSuccess());
	TestEqual(TEXT("ResetEpoch advanced to 1"), F.Subsystem->GetResetEpoch(Id), 1);
	TestEqual(TEXT("State is Ready after reset"), F.Subsystem->GetPuzzleState(Id), EDocPuzzleState::Ready);

	FGuid Attempt2;
	TestTrue(TEXT("New attempt"), F.Subsystem->StartAttempt(Id, Attempt2).IsSuccess());
	TestTrue(TEXT("New attempt id"), Attempt2 != Attempt1);

	TestFalse(TEXT("Delayed input from the old attempt rejected"), F.Subsystem->SubmitInput(MakeEvent(Id, Attempt1, TEXT("Step2"))).IsSuccess());
	TestFalse(TEXT("Input without an attempt id rejected"), F.Subsystem->SubmitInput(MakeEvent(Id, FGuid(), TEXT("Step1"))).IsSuccess());

	FDocPuzzleEvaluation Eval;
	F.Subsystem->EvaluatePuzzle(Id, Eval);
	TestEqual(TEXT("Reset cleared the old attempt's progress"), Eval.RuleProgress[0].NormalizedProgress, 0.0f);
	TestEqual(TEXT("Still active: the stale input did not fail the new attempt"), Eval.State, EDocPuzzleState::AttemptActive);
	return true;
}

// PUZ-08: Doc.Puzzle.RestoreNoEffects
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleRestoreNoEffectsTest, FAutomationTestBase, "Doc.Puzzle.RestoreNoEffects", DocPuzzleTests::Flags)
bool FDocPuzzleRestoreNoEffectsTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;

	// Solved state restores without re-broadcasting.
	{
		FFixture F;
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Restore"), { TEXT("Button") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt)), Attempt);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("Button")));
		TestTrue(TEXT("Puzzle solved"), F.Subsystem->IsPuzzleSolved(Id));
		FDocPuzzleSnapshot Snapshot;
		F.Subsystem->CapturePuzzle(Id, Snapshot);
		TestEqual(TEXT("Snapshot state is Solved"), Snapshot.State, EDocPuzzleState::Solved);
		F.Subsystem->RequestReset(Id);
		const int32 Before = *F.Solved;
		TestTrue(TEXT("StageRestore succeeds"), F.Subsystem->StageRestore(Snapshot).IsSuccess());
		TestTrue(TEXT("Restored to Solved"), F.Subsystem->IsPuzzleSolved(Id));
		TestEqual(TEXT("Historical solve not re-broadcast"), *F.Solved - Before, 0);
	}

	// Mid-attempt: rule state and remaining window time survive; live contributors are not invented.
	{
		UDocPuzzleRule_TimedSequence* Timed = NewObject<UDocPuzzleRule_TimedSequence>(GetTransientPackage());
		Timed->RuleId = TEXT("Rule_TimedRestore");
		Timed->ChildRule = CreateOrderedRule(TEXT("Seq3"), { TEXT("A"), TEXT("B"), TEXT("C") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt);
		Timed->WindowDurationSeconds = 10.0f;
		UDocPuzzleDefinition* Def = CreateTestDefinition(Timed);

		FFixture F;
		FGuid Attempt;
		const FGuid Id = FGuid::NewGuid();
		F.Subsystem->RegisterPuzzle(Id, Def, FDocOwnerScope());
		F.Subsystem->StartAttempt(Id, Attempt);
		F.SetTime(1.0);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("A"))); // window [1, 11)
		F.SetTime(2.0);
		F.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("B")));
		F.Subsystem->SetInputContributor(Id, FGuid::NewGuid(), TEXT("Plate"), FDocPuzzleInputValue::MakeBoolean(true));
		F.SetTime(4.0);
		FDocPuzzleSnapshot Snapshot;
		F.Subsystem->CapturePuzzle(Id, Snapshot);
		TestEqual(TEXT("Snapshot carries no contributors"), Snapshot.DurableContributors.Num(), 0);
		TestFalse(TEXT("Snapshot input state excludes contributor levels"), Snapshot.InputStates.Contains(FName(TEXT("Plate"))));
		TestTrue(TEXT("Window timer captured as elapsed time"), Snapshot.RuleTimerElapsedSeconds.Contains(FName(TEXT("Rule_TimedRestore"))));

		// Load into a different world whose clock is far ahead.
		FFixture G;
		G.SetTime(100.0);
		G.Subsystem->RegisterPuzzle(Id, Def, FDocOwnerScope());
		TestTrue(TEXT("Restore mid-attempt"), G.Subsystem->StageRestore(Snapshot).IsSuccess());
		TestEqual(TEXT("Attempt restored active"), G.Subsystem->GetPuzzleState(Id), EDocPuzzleState::AttemptActive);
		FDocPuzzleEvaluation Eval;
		G.Subsystem->EvaluatePuzzle(Id, Eval);
		TestTrue(TEXT("Sequence progress restored (2 of 3)"), FMath::IsNearlyEqual(Eval.RuleProgress[0].NormalizedProgress, 2.0f / 3.0f, 1e-4f));
		G.SetTime(105.0); // 3 s used before save + 5 s now = 8 s of the 10 s window
		G.Subsystem->SubmitInput(MakeEvent(Id, Attempt, TEXT("C")));
		TestTrue(TEXT("Remaining window honoured after restore"), G.Subsystem->IsPuzzleSolved(Id));
		TestEqual(TEXT("Solve event fires once, for the live completion"), *G.Solved, 1);
	}

	// Unsupported schema is refused.
	{
		FFixture F;
		FGuid Attempt;
		const FGuid Id = F.Start(CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Schema"), { TEXT("A") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected)), Attempt);
		FDocPuzzleSnapshot Future;
		F.Subsystem->CapturePuzzle(Id, Future);
		Future.SchemaVersion = 99;
		TestFalse(TEXT("Future schema refused"), F.Subsystem->StageRestore(Future).IsSuccess());
	}
	return true;
}

// PUZ-09: Doc.Puzzle.InvalidGraph
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleInvalidGraphTest, FAutomationTestBase, "Doc.Puzzle.InvalidGraph", DocPuzzleTests::Flags)
bool FDocPuzzleInvalidGraphTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FFixture F;

	{
		UDocPuzzleDefinition* InvalidDef = NewObject<UDocPuzzleDefinition>(GetTransientPackage());
		InvalidDef->DefinitionId = NAME_None;
		InvalidDef->RootRule = nullptr;
		FDocPuzzleInputDefinition BadInput;
		BadInput.InputId = TEXT("BadScalar");
		BadInput.AcceptedKind = EDocPuzzleInputKind::Scalar;
		BadInput.MinScalar = 10.0f;
		BadInput.MaxScalar = 1.0f;
		InvalidDef->Inputs.Add(BadInput);
		TArray<FText> Errors;
		TestFalse(TEXT("Invalid definition fails validation"), InvalidDef->ValidateDefinition(Errors));
		TestTrue(TEXT("Errors captured"), Errors.Num() >= 3);
		TestEqual(TEXT("Registration refuses an invalid definition"), F.Subsystem->RegisterPuzzle(FGuid::NewGuid(), InvalidDef, FDocOwnerScope()).Outcome, EDocResultOutcome::InvalidInput);
	}

	// Cycle: validation terminates and reports it; registration refuses it.
	{
		UDocPuzzleRule_Composite* A = CreateComposite(TEXT("CycA"), EDocPuzzleCompositionMode::All, {});
		UDocPuzzleRule_Composite* B = CreateComposite(TEXT("CycB"), EDocPuzzleCompositionMode::All, { A });
		A->ChildRules.Add(B);
		UDocPuzzleDefinition* Def = CreateTestDefinition(A, TEXT("Def_Cycle"));
		TArray<FText> Errors;
		TestFalse(TEXT("Cyclic graph invalid"), Def->ValidateDefinition(Errors));
		TestEqual(TEXT("Cyclic graph refused"), F.Subsystem->RegisterPuzzle(FGuid::NewGuid(), Def, FDocOwnerScope()).Outcome, EDocResultOutcome::InvalidInput);

		// Even if evaluated directly, the depth guard stops the recursion.
		FDocPuzzleRuleEvaluationContext Context;
		FDocPuzzleRuleProgress Progress;
		TestFalse(TEXT("Direct evaluation of a cycle terminates unsatisfied"), A->Evaluate(Context, Progress));
	}

	// Too deep.
	{
		UDocPuzzleRule* Node = CreateOrderedRule(TEXT("Leaf"), { TEXT("A") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected);
		for (int32 i = 0; i < UDocPuzzleDefinition::MaxGraphDepth + 2; ++i)
		{
			Node = CreateComposite(*FString::Printf(TEXT("Deep%d"), i), EDocPuzzleCompositionMode::All, { Node });
		}
		TArray<FText> Errors;
		TestFalse(TEXT("Graph deeper than the limit invalid"), CreateTestDefinition(Node, TEXT("Def_Deep"))->ValidateDefinition(Errors));
	}

	// Duplicate RuleIds would share state.
	{
		UDocPuzzleRule_Composite* Root = CreateComposite(TEXT("DupRoot"), EDocPuzzleCompositionMode::All, {
			CreateOrderedRule(TEXT("Dup"), { TEXT("A") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected),
			CreateOrderedRule(TEXT("Dup"), { TEXT("B") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected) });
		TArray<FText> Errors;
		TestFalse(TEXT("Duplicate RuleIds invalid"), CreateTestDefinition(Root, TEXT("Def_Dup"))->ValidateDefinition(Errors));
	}

	// Missing input: a rule references an input the definition does not declare.
	{
		UDocPuzzleDefinition* Def = CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Missing"), { TEXT("Declared"), TEXT("Undeclared") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected), TEXT("Def_Missing"));
		FDocPuzzleInputDefinition Declared;
		Declared.InputId = TEXT("Declared");
		Def->Inputs.Add(Declared);
		TArray<FText> Errors;
		TestFalse(TEXT("Undeclared input reference invalid"), Def->ValidateDefinition(Errors));
	}

	// A well-formed graph passes.
	{
		UDocPuzzleRule_Composite* Root = CreateComposite(TEXT("OkRoot"), EDocPuzzleCompositionMode::Any, {
			CreateOrderedRule(TEXT("OkA"), { TEXT("A") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected),
			CreateOrderedRule(TEXT("OkB"), { TEXT("B") }, EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected) });
		TArray<FText> Errors;
		TestTrue(TEXT("Valid graph passes"), CreateTestDefinition(Root, TEXT("Def_Ok"))->ValidateDefinition(Errors));
	}
	return true;
}

// PUZ-10: Doc.Puzzle.IsolatedConsumers
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPuzzleIsolatedConsumersTest, FAutomationTestBase, "Doc.Puzzle.IsolatedConsumers", DocPuzzleTests::Flags)
bool FDocPuzzleIsolatedConsumersTest::RunTest(const FString& Parameters)
{
	using namespace DocPuzzleTests;
	FDocScopedTestWorld TW;

	UDocPuzzleDefinition* Def = CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Comp"), { TEXT("PedestalTrigger") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt));

	AActor* PuzzleActor = TW.Spawn<AActor>();
	TestNotNull(TEXT("PuzzleActor spawned"), PuzzleActor);
	UDocPuzzleComponent* PuzzleComp = NewObject<UDocPuzzleComponent>(PuzzleActor, TEXT("PuzzleComponent"));
	PuzzleComp->Definition = Def;
	PuzzleComp->RegisterComponent();

	AActor* InputActor = TW.Spawn<AActor>();
	TestNotNull(TEXT("InputActor spawned"), InputActor);
	UDocPuzzleInputComponent* InputComp = NewObject<UDocPuzzleInputComponent>(InputActor, TEXT("InputComponent"));
	InputComp->InputId = TEXT("PedestalTrigger");
	InputComp->TargetPuzzleComponent = PuzzleComp;
	InputComp->RegisterComponent();

	if (!PuzzleActor->HasActorBegunPlay())
	{
		PuzzleActor->DispatchBeginPlay();
	}
	if (!InputActor->HasActorBegunPlay())
	{
		InputActor->DispatchBeginPlay();
	}

	int32 ComponentSolvedCount = 0;
	PuzzleComp->OnPuzzleSolvedNative.AddLambda([&](const FGuid& InId, const FGuid& InAtt) {
		ComponentSolvedCount++;
	});

	const FDocSystemResult Result = InputComp->SendTrigger();
	TestTrue(TEXT("SendTrigger succeeds (component supplies the active attempt)"), Result.IsSuccess());
	TestTrue(TEXT("Component marks puzzle solved"), PuzzleComp->IsSolved());
	TestEqual(TEXT("Component OnPuzzleSolved fired"), ComponentSolvedCount, 1);

	// Unbinding keeps state; re-binding with the same definition restores it.
	UDocPuzzleSubsystem* Subsystem = TW.GetSubsystem<UDocPuzzleSubsystem>();
	const FGuid Id = PuzzleComp->InstanceId;
	TestTrue(TEXT("Unregister"), Subsystem->UnregisterPuzzle(Id).IsSuccess());
	TestTrue(TEXT("State retained"), Subsystem->HasRetainedState(Id));
	UDocPuzzleDefinition* Other = CreateTestDefinition(CreateOrderedRule(TEXT("Rule_Other"), { TEXT("X") }, EDocPuzzleOrderedMismatchPolicy::FailAttempt), TEXT("Def_Other"));
	TestEqual(TEXT("Re-binding to another definition refused"), Subsystem->RegisterPuzzle(Id, Other, FDocOwnerScope()).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Re-register with the same definition"), Subsystem->RegisterPuzzle(Id, Def, FDocOwnerScope()).IsSuccess());
	TestTrue(TEXT("Solved state survived unbinding"), Subsystem->IsPuzzleSolved(Id));
	Subsystem->UnregisterPuzzle(Id);
	TestTrue(TEXT("Explicit discard"), Subsystem->DiscardRetainedState(Id).IsSuccess());
	TestTrue(TEXT("Fresh registration after discard"), Subsystem->RegisterPuzzle(Id, Def, FDocOwnerScope()).IsSuccess());
	TestEqual(TEXT("Fresh state is Ready"), Subsystem->GetPuzzleState(Id), EDocPuzzleState::Ready);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
