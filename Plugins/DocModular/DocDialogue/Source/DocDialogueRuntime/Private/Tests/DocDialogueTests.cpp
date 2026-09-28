// DocDialogue automation tests (DIA-01..11 base logic, plus validation). Headless:
// no NPC framework, UI, Sequences or Events. Time is driven by the subsystem's
// manual test clock; providers are native test doubles.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocDialogueSubsystem.h"
#include "NativeGameplayTags.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

namespace DocDialogueTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Secret, "Test.Dialogue.Secret");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Hostile, "Test.Dialogue.Hostile");

	FDocOwnerScope OwnerOf(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0xD1A1, 0x0001, 0x0002, static_cast<uint32>(N))); }

	UDocDialogueGraph* NewGraph(FName Id, FName Start)
	{
		UDocDialogueGraph* G = NewObject<UDocDialogueGraph>(GetTransientPackage());
		G->GraphId = Id;
		G->StartNode = Start;
		return G;
	}

	FDocDialogueNode MakeNode(FName Id, EDocDialogueNodeType Type, FName Next = NAME_None)
	{
		FDocDialogueNode N;
		N.NodeId = Id;
		N.Type = Type;
		N.Next = Next;
		return N;
	}

	FDocDialogueNode MakeLine(FName Id, FName Next, const TCHAR* Text = TEXT("Line"))
	{
		FDocDialogueNode N = MakeNode(Id, EDocDialogueNodeType::Line, Next);
		N.Text = FText::FromString(Text);
		return N;
	}

	FDocDialogueChoice MakeChoice(FName Id, FName Destination)
	{
		FDocDialogueChoice C;
		C.ChoiceId = Id;
		C.ChoiceText = FText::FromName(Id);
		C.Destination = Destination;
		return C;
	}

	FDocDialogueNode MakeChoiceNode(FName Id, const TArray<FDocDialogueChoice>& Choices)
	{
		FDocDialogueNode N = MakeNode(Id, EDocDialogueNodeType::Choice);
		N.Choices = Choices;
		return N;
	}

	FDocDialogueCondition MakeTagCondition(EDocDialogueConditionType Type, const FGameplayTag& Tag, FName Role = NAME_None)
	{
		FDocDialogueCondition C;
		C.Type = Type;
		C.Tag = Tag;
		C.Role = Role;
		return C;
	}

	FDocDialogueCondition MakeNumeric(FName Variable, EDocDialogueCompare Compare, double Value)
	{
		FDocDialogueCondition C;
		C.Type = EDocDialogueConditionType::NumericComparison;
		C.VariableName = Variable;
		C.Compare = Compare;
		C.Value = Value;
		return C;
	}

	FDocDialogueAction MakeCustom(FName Id, FName Provider, const TCHAR* Payload = TEXT(""))
	{
		FDocDialogueAction A;
		A.ActionId = Id;
		A.Type = EDocDialogueActionType::Custom;
		A.ProviderId = Provider;
		A.Payload = Payload;
		return A;
	}

	FDocDialogueAction MakeVarAction(FName Id, EDocDialogueActionType Type, FName Variable, const FDocDialogueValue& Value)
	{
		FDocDialogueAction A;
		A.ActionId = Id;
		A.Type = Type;
		A.VariableName = Variable;
		A.Value = Value;
		return A;
	}

	FDocDialogueVariableDecl MakeVar(FName Name, EDocDialogueValueType Type, const FDocDialogueValue& Default, bool bPersistent = false)
	{
		FDocDialogueVariableDecl V;
		V.Name = Name;
		V.Type = Type;
		V.Default = Default;
		V.bPersistent = bPersistent;
		return V;
	}

	FDocDialogueRole MakeRole(FName Id, bool bExclusive = true, EDocDialogueLossPolicy Loss = EDocDialogueLossPolicy::Cancel, float Timeout = 30.f)
	{
		FDocDialogueRole R;
		R.RoleId = Id;
		R.DisplayName = FText::FromName(Id);
		R.bExclusive = bExclusive;
		R.LossPolicy = Loss;
		R.PauseTimeoutSeconds = Timeout;
		return R;
	}

	FDocDialogueParticipantBinding Bind(FName Role, UObject* Participant)
	{
		FDocDialogueParticipantBinding B;
		B.RoleId = Role;
		B.Participant = Participant;
		return B;
	}

	bool AnyContains(const TArray<FString>& Lines, const TCHAR* Needle)
	{
		return Lines.ContainsByPredicate([Needle](const FString& L) { return L.Contains(Needle); });
	}

	class FRecordingActions final : public IDocDialogueActionProvider
	{
	public:
		TMap<FName, int32> Executions;
		TSet<FName> FailOnce;
		bool bPending = false;
		bool bIdempotent = false;
		bool bCompensation = false;
		int32 Compensations = 0;

		virtual FDocDialogueActionOutcome ExecuteAction(const FDocDialogueActionRequest& Request) override
		{
			const FName Id = Request.Action->ActionId;
			++Executions.FindOrAdd(Id);
			if (FailOnce.Remove(Id) > 0)
			{
				return FDocDialogueActionOutcome::Failed(FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Declined")));
			}
			return bPending ? FDocDialogueActionOutcome::Pending() : FDocDialogueActionOutcome::Committed();
		}
		virtual bool IsIdempotent() const override { return bIdempotent; }
		virtual bool SupportsCompensation() const override { return bCompensation; }
		virtual FDocSystemResult CompensateAction(const FDocDialogueActionRequest&) override
		{
			++Compensations;
			return FDocSystemResult::MakeSuccess();
		}
		int32 Count(FName Id) const { return Executions.FindRef(Id); }
	};

	class FStateConditions final : public IDocDialogueConditionProvider
	{
	public:
		EDocConditionState State = EDocConditionState::Satisfied;
		mutable int32 Calls = 0;
		virtual FDocConditionResult EvaluateCondition(const FDocDialogueConditionQuery&) const override
		{
			++Calls;
			switch (State)
			{
			case EDocConditionState::Satisfied: return FDocConditionResult::Satisfied();
			case EDocConditionState::Unsatisfied: return FDocConditionResult::Unsatisfied(FGameplayTag(), TEXT("No"));
			default: return FDocConditionResult::Unavailable(FGameplayTag(), TEXT("Not loaded"));
			}
		}
	};

	struct FFixture
	{
		UDocDialogueSubsystem* S = nullptr;
		TArray<FDocDialogueLineInfo> Shown;
		TArray<FDocDialogueLineInfo> Completed;
		TArray<FDocDialogueChoiceList> Presented;
		TArray<EDocDialogueSessionState> Ended;
		TArray<FDocSystemResult> EndResults;
		// Declared last: destroyed first, while the recorders above are still alive.
		FDocScopedTestWorld TW;

		FFixture()
		{
			S = TW.GetSubsystem<UDocDialogueSubsystem>();
			if (S)
			{
				S->SetTimeForTesting(0.0);
				S->OnLineShownNative.AddLambda([this](FDocRequestHandle, const FDocDialogueLineInfo& L) { Shown.Add(L); });
				S->OnLineCompletedNative.AddLambda([this](FDocRequestHandle, const FDocDialogueLineInfo& L) { Completed.Add(L); });
				S->OnChoicesPresentedNative.AddLambda([this](FDocRequestHandle, const FDocDialogueChoiceList& L) { Presented.Add(L); });
				S->OnSessionEndedNative.AddLambda([this](FDocRequestHandle, EDocDialogueSessionState State, const FDocSystemResult& R) { Ended.Add(State); EndResults.Add(R); });
			}
		}

		~FFixture()
		{
			if (S)
			{
				S->OnLineShownNative.Clear();
				S->OnLineCompletedNative.Clear();
				S->OnChoicesPresentedNative.Clear();
				S->OnSessionEndedNative.Clear();
			}
		}

		FDocRequestHandle Start(UDocDialogueGraph* Graph, FDocSystemResult& OutResult, const TArray<FDocDialogueParticipantBinding>& Bindings = {},
			const FDocOwnerScope& Owner = OwnerOf(1), EDocDialogueAuthorityMode Mode = EDocDialogueAuthorityMode::OwnerAuthoritative,
			UObject* Initiator = nullptr, const TArray<UObject*>& Audience = {}, const FGameplayTagContainer& Context = FGameplayTagContainer())
		{
			FDocDialogueStartRequest Request;
			Request.Graph = Graph;
			Request.Owner = Owner;
			Request.Bindings = Bindings;
			Request.AuthorityMode = Mode;
			Request.Initiator = Initiator;
			for (UObject* A : Audience) { Request.Audience.Add(A); }
			Request.ContextTags = Context;
			return S->StartDialogue(Request, OutResult);
		}

		FDocDialogueStartRequest Request(UDocDialogueGraph* Graph, const FDocOwnerScope& Owner) const
		{
			FDocDialogueStartRequest R;
			R.Graph = Graph;
			R.Owner = Owner;
			return R;
		}

		FDocDialogueSessionSnapshot Snap(FDocRequestHandle H) const
		{
			FDocDialogueSessionSnapshot X;
			S->GetSessionSnapshot(H, X);
			return X;
		}

		int64 Rev(FDocRequestHandle H) const { return Snap(H).Revision; }

		UDocDialogueParticipantComponent* Participant(FName Id, bool bShared = false, const FGameplayTag& Tag = FGameplayTag())
		{
			AActor* Actor = TW.Spawn<AActor>();
			UDocDialogueParticipantComponent* C = NewObject<UDocDialogueParticipantComponent>(Actor);
			C->ParticipantId = Id;
			C->DisplayName = FText::FromName(Id);
			C->bAllowConcurrentConversations = bShared;
			if (Tag.IsValid()) { C->Tags.AddTag(Tag); }
			C->RegisterComponent();
			if (!Actor->HasActorBegunPlay())
			{
				Actor->DispatchBeginPlay();
			}
			return C;
		}

		int64 VarInt(FDocRequestHandle H, FName Name) const
		{
			FDocDialogueValue V;
			return S->GetVariable(H, Name, V) ? V.Int : -999;
		}
	};
}

using namespace DocDialogueTests;

// ---------------------------------------------------------------------------
// DIA-01
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueLineAdvanceTest, "Doc.Dialogue.LineAdvance", DocDialogueTests::Flags)
bool FDocDialogueLineAdvanceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("LineTest"), TEXT("L1"));
	FDocDialogueNode L2 = MakeLine(TEXT("L2"), TEXT("L3"));
	L2.bAutoAdvance = true;
	L2.Duration = 2.f;
	L2.bSkippable = false;
	FDocDialogueNode L3 = MakeLine(TEXT("L3"), NAME_None);
	L3.bAutoAdvance = true;
	L3.TimingSource = EDocDialogueLineTiming::PresentationAck;
	L3.Duration = 0.5f;
	G->Nodes = { MakeLine(TEXT("L1"), TEXT("L2")), L2, L3 };

	FDocSystemResult R;
	const FDocRequestHandle H = F.Start(G, R);
	TestTrue(TEXT("Started"), R.IsSuccess());
	TestTrue(TEXT("Waiting for advance"), F.Snap(H).State == EDocDialogueSessionState::WaitingForAdvance);
	TestEqual(TEXT("First line ordinal"), F.Shown.Num() > 0 ? F.Shown[0].LineOrdinal : -1, 1LL);

	const int64 R1 = F.Rev(H);
	R = F.S->Advance(H, R1 - 1, nullptr);
	TestTrue(TEXT("Stale revision rejected"), R.Outcome == EDocResultOutcome::Conflict && R.ErrorTag == DocDialogueTags::Error_Dialogue_StaleRevision);
	TestTrue(TEXT("Manual advance"), F.S->Advance(H, R1, nullptr).IsSuccess());
	TestEqual(TEXT("L2 shown"), F.Shown.Num(), 2);

	const int64 R2 = F.Rev(H);
	R = F.S->Advance(H, R2, nullptr);
	TestTrue(TEXT("Non-skippable auto line rejects early advance"), R.ErrorTag == DocDialogueTags::Error_Dialogue_NotSkippable);
	F.S->AdvanceTimeForTesting(1.0);
	TestEqual(TEXT("Still L2 at t=1"), F.Snap(H).Node.NodeId, FName(TEXT("L2")));
	F.S->AdvanceTimeForTesting(1.0);
	TestEqual(TEXT("Timer completed L2"), F.Snap(H).Node.NodeId, FName(TEXT("L3")));
	R = F.S->Advance(H, R2, nullptr);
	TestTrue(TEXT("Late manual input for L2 is stale"), R.Outcome == EDocResultOutcome::Conflict);
	TestEqual(TEXT("Exactly one completion per line"), F.Completed.Num(), 2);

	TestTrue(TEXT("Ack of wrong ordinal rejected"), F.S->AcknowledgeLinePresented(H, 99).Outcome == EDocResultOutcome::Conflict);
	F.S->AdvanceTimeForTesting(0.4);
	TestEqual(TEXT("Ack line waits for presentation"), F.Snap(H).Node.NodeId, FName(TEXT("L3")));
	TestTrue(TEXT("Ack accepted"), F.S->AcknowledgeLinePresented(H, 3).IsSuccess());
	F.S->AdvanceTimeForTesting(0.5);
	TestEqual(TEXT("Ended once"), F.Ended.Num(), 1);
	TestTrue(TEXT("Completed"), F.Ended.Num() == 1 && F.Ended[0] == EDocDialogueSessionState::Completed);
	TestEqual(TEXT("Three completions"), F.Completed.Num(), 3);
	if (F.Completed.Num() == 3)
	{
		TestEqual(TEXT("Ordinals stable"), F.Completed[2].LineOrdinal, 3LL);
	}
	TestFalse(TEXT("Handle stale after end"), F.S->Advance(H, 0, nullptr).IsSuccess());
	return true;
}

// ---------------------------------------------------------------------------
// DIA-02
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueChoiceBranchTest, "Doc.Dialogue.ChoiceBranch", DocDialogueTests::Flags)
bool FDocDialogueChoiceBranchTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("Branch"), TEXT("C1"));
	G->Nodes = {
		MakeChoiceNode(TEXT("C1"), { MakeChoice(TEXT("Yes"), TEXT("LY")), MakeChoice(TEXT("No"), TEXT("LN")) }),
		MakeLine(TEXT("LY"), NAME_None, TEXT("Yes")),
		MakeLine(TEXT("LN"), NAME_None, TEXT("No")) };

	FDocSystemResult R;
	const FDocRequestHandle H = F.Start(G, R);
	TestTrue(TEXT("Started"), R.IsSuccess());
	TestTrue(TEXT("Waiting for choice"), F.Snap(H).State == EDocDialogueSessionState::WaitingForChoice);
	TestEqual(TEXT("Choices presented"), F.Presented.Num(), 1);

	FDocDialogueChoiceList List;
	TestTrue(TEXT("List"), F.S->GetAvailableChoices(H, nullptr, List).IsSuccess());
	TestEqual(TEXT("Two choices"), List.Choices.Num(), 2);
	TestEqual(TEXT("List stamped with revision"), List.SessionRevision, F.Rev(H));

	R = F.S->SelectChoice(H, TEXT("Yes"), List.SessionRevision - 1, nullptr);
	TestTrue(TEXT("Stale list rejected"), R.Outcome == EDocResultOutcome::Conflict);
	R = F.S->SelectChoice(H, TEXT("Maybe"), List.SessionRevision, nullptr);
	TestTrue(TEXT("Foreign choice id rejected"), R.Outcome == EDocResultOutcome::NotFound && R.ErrorTag == DocDialogueTags::Error_Dialogue_ChoiceNotFound);
	TestTrue(TEXT("Select Yes"), F.S->SelectChoice(H, TEXT("Yes"), List.SessionRevision, nullptr).IsSuccess());
	TestEqual(TEXT("Branch destination"), F.Snap(H).Node.NodeId, FName(TEXT("LY")));
	TestFalse(TEXT("Second submission of the old list rejected"), F.S->SelectChoice(H, TEXT("No"), List.SessionRevision, nullptr).IsSuccess());
	TestEqual(TEXT("Still on LY"), F.Snap(H).Node.NodeId, FName(TEXT("LY")));

	FDocDialogueMemory Memory;
	TestTrue(TEXT("Memory"), F.S->GetMemory(TEXT("Branch"), OwnerOf(1), Memory));
	TestEqual(TEXT("One choice recorded"), Memory.ChoiceHistory.Num(), 1);
	if (Memory.ChoiceHistory.Num() == 1)
	{
		TestEqual(TEXT("Recorded by id"), Memory.ChoiceHistory[0].ChoiceId, FName(TEXT("Yes")));
		TestEqual(TEXT("Transition ordinal"), Memory.ChoiceHistory[0].TransitionOrdinal, 1LL);
	}
	return true;
}

// ---------------------------------------------------------------------------
// DIA-03
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueHiddenChoiceTest, "Doc.Dialogue.HiddenChoice", DocDialogueTests::Flags)
bool FDocDialogueHiddenChoiceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	FDocDialogueChoice Secret = MakeChoice(TEXT("Secret"), TEXT("LS"));
	Secret.HiddenConditions = { MakeTagCondition(EDocDialogueConditionType::TagPresent, TAG_Secret) };
	FDocDialogueChoice Once = MakeChoice(TEXT("Once"), TEXT("C1"));
	Once.bOnceOnly = true;
	UDocDialogueGraph* G = NewGraph(TEXT("Hidden"), TEXT("C1"));
	G->Nodes = { MakeChoiceNode(TEXT("C1"), { Secret, Once, MakeChoice(TEXT("Leave"), NAME_None) }), MakeLine(TEXT("LS"), NAME_None) };

	FDocSystemResult R;
	FDocRequestHandle H = F.Start(G, R);
	FDocDialogueChoiceList List;
	F.S->GetAvailableChoices(H, nullptr, List);
	TestNull(TEXT("Secret hidden"), List.Find(TEXT("Secret")));
	TestEqual(TEXT("Two visible"), List.Choices.Num(), 2);
	R = F.S->SelectChoice(H, TEXT("Secret"), List.SessionRevision, nullptr);
	TestTrue(TEXT("Direct submission of hidden choice rejected"), R.ErrorTag == DocDialogueTags::Error_Dialogue_ChoiceHidden);

	TestTrue(TEXT("Once taken"), F.S->SelectChoice(H, TEXT("Once"), List.SessionRevision, nullptr).IsSuccess());
	F.S->GetAvailableChoices(H, nullptr, List);
	TestNull(TEXT("Once-only now hidden"), List.Find(TEXT("Once")));
	R = F.S->SelectChoice(H, TEXT("Once"), List.SessionRevision, nullptr);
	TestTrue(TEXT("Once-only cannot be resubmitted"), R.ErrorTag == DocDialogueTags::Error_Dialogue_ChoiceHidden);
	F.S->Cancel(H, nullptr);

	FGameplayTagContainer Context;
	Context.AddTag(TAG_Secret);
	H = F.Start(G, R, {}, OwnerOf(1), EDocDialogueAuthorityMode::OwnerAuthoritative, nullptr, {}, Context);
	F.S->GetAvailableChoices(H, nullptr, List);
	TestNotNull(TEXT("Secret visible with context tag"), List.Find(TEXT("Secret")));
	TestNull(TEXT("Once-only remembered across sessions"), List.Find(TEXT("Once")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueDisabledChoiceTest, "Doc.Dialogue.DisabledChoice", DocDialogueTests::Flags)
bool FDocDialogueDisabledChoiceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	FDocDialogueChoice Buy = MakeChoice(TEXT("Buy"), TEXT("LB"));
	Buy.DisabledConditions = { MakeNumeric(TEXT("Gold"), EDocDialogueCompare::GreaterOrEqual, 10.0) };
	Buy.FailureReason = FText::FromString(TEXT("Not enough gold"));
	UDocDialogueGraph* G = NewGraph(TEXT("Disabled"), TEXT("C1"));
	G->Variables = { MakeVar(TEXT("Gold"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(5)) };
	G->Nodes = { MakeChoiceNode(TEXT("C1"), { Buy, MakeChoice(TEXT("Leave"), NAME_None) }), MakeLine(TEXT("LB"), NAME_None) };

	FDocSystemResult R;
	const FDocRequestHandle H = F.Start(G, R);
	FDocDialogueChoiceList List;
	F.S->GetAvailableChoices(H, nullptr, List);
	const FDocDialogueChoiceView* View = List.Find(TEXT("Buy"));
	TestNotNull(TEXT("Buy shown"), View);
	if (View)
	{
		TestTrue(TEXT("Buy disabled"), View->State == EDocDialogueChoiceState::VisibleDisabled);
		TestTrue(TEXT("Failure reason"), View->FailureReason.EqualTo(Buy.FailureReason));
	}
	R = F.S->SelectChoice(H, TEXT("Buy"), List.SessionRevision, nullptr);
	TestTrue(TEXT("Disabled choice rejected"), R.ErrorTag == DocDialogueTags::Error_Dialogue_ChoiceDisabled && !R.UserMessage.IsEmpty());

	TestTrue(TEXT("Authorized write (float coerced to int)"), F.S->SetVariable(H, TEXT("Gold"), FDocDialogueValue::MakeFloat(10.4), nullptr).IsSuccess());
	TestEqual(TEXT("Coerced value"), F.VarInt(H, TEXT("Gold")), 10LL);
	R = F.S->SelectChoice(H, TEXT("Buy"), List.SessionRevision, nullptr);
	TestTrue(TEXT("Old list is stale after the write"), R.Outcome == EDocResultOutcome::Conflict);
	F.S->GetAvailableChoices(H, nullptr, List);
	View = List.Find(TEXT("Buy"));
	TestTrue(TEXT("Buy enabled"), View && View->State == EDocDialogueChoiceState::VisibleEnabled);
	TestTrue(TEXT("Buy selected"), F.S->SelectChoice(H, TEXT("Buy"), List.SessionRevision, nullptr).IsSuccess());
	TestEqual(TEXT("At LB"), F.Snap(H).Node.NodeId, FName(TEXT("LB")));
	TestTrue(TEXT("Undeclared write rejected"), F.S->SetVariable(H, TEXT("Nope"), FDocDialogueValue::MakeInt(1), nullptr).Outcome == EDocResultOutcome::NotFound);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-04
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueConditionTest, "Doc.Dialogue.Condition", DocDialogueTests::Flags)
bool FDocDialogueConditionTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("Cond"), TEXT("C0"));
	G->Roles = { MakeRole(TEXT("Guard")) };
	G->Variables = {
		MakeVar(TEXT("Count"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(2)),
		MakeVar(TEXT("Ratio"), EDocDialogueValueType::Float, FDocDialogueValue::MakeFloat(0.5)) };

	FDocDialogueNode C0 = MakeNode(TEXT("C0"), EDocDialogueNodeType::Condition, TEXT("B1"));
	C0.Conditions = { MakeNumeric(TEXT("Count"), EDocDialogueCompare::Greater, 1.5), MakeNumeric(TEXT("Ratio"), EDocDialogueCompare::Equal, 0.5) };
	C0.FailDestination = TEXT("LFail");
	FDocDialogueNode B1 = MakeNode(TEXT("B1"), EDocDialogueNodeType::Branch, TEXT("LNotMet"));
	FDocDialogueBranchCase Met;
	FDocDialogueCondition MetKing;
	MetKing.Type = EDocDialogueConditionType::EventState;
	MetKing.QueryId = TEXT("MetKing");
	Met.Conditions = { MetKing };
	Met.Destination = TEXT("LMet");
	B1.Cases = { Met };
	B1.UnavailableDestination = TEXT("LUnknown");
	FDocDialogueChoice Bribe = MakeChoice(TEXT("Bribe"), NAME_None);
	Bribe.HiddenConditions = { MakeTagCondition(EDocDialogueConditionType::ParticipantTag, TAG_Hostile, TEXT("Guard")) };
	G->Nodes = { C0, B1,
		MakeLine(TEXT("LMet"), TEXT("CH")), MakeLine(TEXT("LNotMet"), TEXT("CH")), MakeLine(TEXT("LUnknown"), TEXT("CH")), MakeLine(TEXT("LFail"), NAME_None),
		MakeChoiceNode(TEXT("CH"), { Bribe, MakeChoice(TEXT("Greet"), NAME_None) }) };

	UDocDialogueParticipantComponent* Hostile = F.Participant(TEXT("Guard_A"), false, TAG_Hostile);
	UDocDialogueParticipantComponent* Calm = F.Participant(TEXT("Guard_B"));

	// Unavailable provider is never "false": the branch takes its unavailable route.
	FDocSystemResult R;
	FDocRequestHandle H = F.Start(G, R, { Bind(TEXT("Guard"), Hostile) });
	TestTrue(TEXT("Typed numeric coercion passed the gate"), R.IsSuccess());
	TestEqual(TEXT("Unavailable route"), F.Snap(H).Node.NodeId, FName(TEXT("LUnknown")));
	F.S->Advance(H, F.Rev(H), nullptr);
	FDocDialogueChoiceList List;
	F.S->GetAvailableChoices(H, nullptr, List);
	TestNotNull(TEXT("Hostile guard enables Bribe"), List.Find(TEXT("Bribe")));
	F.S->Cancel(H, nullptr);

	TSharedPtr<FStateConditions> Provider = MakeShared<FStateConditions>();
	F.S->RegisterConditionProvider(TEXT("Event"), Provider);
	H = F.Start(G, R, { Bind(TEXT("Guard"), Calm) });
	TestEqual(TEXT("Satisfied route"), F.Snap(H).Node.NodeId, FName(TEXT("LMet")));
	F.S->Advance(H, F.Rev(H), nullptr);
	const int64 Before = F.Rev(H);
	F.S->GetAvailableChoices(H, nullptr, List);
	F.S->GetAvailableChoices(H, nullptr, List);
	TestNull(TEXT("Calm guard hides Bribe"), List.Find(TEXT("Bribe")));
	TestEqual(TEXT("Evaluation is pure (no revision change)"), F.Rev(H), Before);
	F.S->Cancel(H, nullptr);

	Provider->State = EDocConditionState::Unsatisfied;
	H = F.Start(G, R, { Bind(TEXT("Guard"), Calm) });
	TestEqual(TEXT("Unsatisfied route"), F.Snap(H).Node.NodeId, FName(TEXT("LNotMet")));
	F.S->Cancel(H, nullptr);

	G->Variables[0].Default = FDocDialogueValue::MakeInt(1);
	H = F.Start(G, R, { Bind(TEXT("Guard"), Calm) });
	TestEqual(TEXT("Numeric gate fails"), F.Snap(H).Node.NodeId, FName(TEXT("LFail")));
	F.S->Cancel(H, nullptr);

	// Non-numeric comparison is rejected by validation instead of guessed at runtime.
	UDocDialogueGraph* Bad = NewGraph(TEXT("BadCond"), TEXT("C0"));
	Bad->Variables = { MakeVar(TEXT("Mood"), EDocDialogueValueType::Name, FDocDialogueValue::MakeName(TEXT("Happy"))) };
	FDocDialogueNode BadNode = MakeNode(TEXT("C0"), EDocDialogueNodeType::Condition);
	BadNode.Conditions = { MakeNumeric(TEXT("Mood"), EDocDialogueCompare::Greater, 1.0) };
	Bad->Nodes = { BadNode };
	TArray<FString> Errors, Warnings;
	Bad->FindProblems(Errors, Warnings);
	TestTrue(TEXT("Name variable is not numeric"), AnyContains(Errors, TEXT("is not numeric")));
	return true;
}

// ---------------------------------------------------------------------------
// DIA-05
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueJumpTest, "Doc.Dialogue.Jump", DocDialogueTests::Flags)
bool FDocDialogueJumpTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("Loop"), TEXT("L1"));
	G->Variables = { MakeVar(TEXT("Counter"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(0)) };
	FDocDialogueNode B1 = MakeNode(TEXT("B1"), EDocDialogueNodeType::Branch, TEXT("END"));
	FDocDialogueBranchCase Again;
	Again.Conditions = { MakeNumeric(TEXT("Counter"), EDocDialogueCompare::Less, 3.0) };
	Again.Destination = TEXT("E1");
	B1.Cases = { Again };
	FDocDialogueNode E1 = MakeNode(TEXT("E1"), EDocDialogueNodeType::Event, TEXT("J1"));
	E1.Actions = { MakeVarAction(TEXT("Inc"), EDocDialogueActionType::AddVariable, TEXT("Counter"), FDocDialogueValue::MakeInt(1)) };
	G->Nodes = { MakeLine(TEXT("L1"), TEXT("B1")), B1, E1, MakeNode(TEXT("J1"), EDocDialogueNodeType::Jump, TEXT("L1")), MakeNode(TEXT("END"), EDocDialogueNodeType::End) };

	FDocSystemResult R;
	const FDocRequestHandle H = F.Start(G, R);
	TestTrue(TEXT("Started"), R.IsSuccess());
	for (int32 i = 1; i <= 3; ++i)
	{
		F.S->Advance(H, F.Rev(H), nullptr);
		TestEqual(FString::Printf(TEXT("Counter after loop %d"), i), F.VarInt(H, TEXT("Counter")), static_cast<int64>(i));
	}
	F.S->Advance(H, F.Rev(H), nullptr);
	TestTrue(TEXT("Loop exits and completes"), F.Ended.Num() == 1 && F.Ended[0] == EDocDialogueSessionState::Completed);

	// Conditional immediate loop: bounded at runtime with an actionable diagnostic.
	UDocDialogueGraph* Runaway = NewGraph(TEXT("Runaway"), TEXT("B1"));
	Runaway->MaxStepsPerDispatch = 8;
	Runaway->MaxNonYieldingSteps = 50;
	FDocDialogueNode RB = MakeNode(TEXT("B1"), EDocDialogueNodeType::Branch);
	FDocDialogueBranchCase Always;
	Always.Conditions = { MakeTagCondition(EDocDialogueConditionType::TagMissing, TAG_Secret) };
	Always.Destination = TEXT("E1");
	RB.Cases = { Always };
	Runaway->Nodes = { RB, MakeNode(TEXT("E1"), EDocDialogueNodeType::Event, TEXT("J1")), MakeNode(TEXT("J1"), EDocDialogueNodeType::Jump, TEXT("B1")) };
	F.Start(Runaway, R, {}, OwnerOf(2));
	TestTrue(TEXT("First dispatch yields, not fails"), R.IsSuccess());
	for (int32 i = 0; i < 10; ++i)
	{
		F.S->AdvanceTimeForTesting(0.0);
	}
	TestEqual(TEXT("Runaway ended"), F.Ended.Num(), 2);
	if (F.EndResults.Num() == 2)
	{
		TestTrue(TEXT("Runaway failed"), F.Ended[1] == EDocDialogueSessionState::Failed);
		TestTrue(TEXT("Runaway tag"), F.EndResults[1].ErrorTag == DocDialogueTags::Error_Dialogue_RunawayLoop);
		TestTrue(TEXT("Diagnostic names graph"), F.EndResults[1].Diagnostic.Contains(TEXT("Runaway")));
	}

	// Unconditional zero-wait cycle: rejected by validation.
	UDocDialogueGraph* Static = NewGraph(TEXT("Static"), TEXT("J1"));
	Static->Nodes = { MakeNode(TEXT("J1"), EDocDialogueNodeType::Jump, TEXT("J2")), MakeNode(TEXT("J2"), EDocDialogueNodeType::Jump, TEXT("J1")) };
	TArray<FString> Errors, Warnings;
	Static->FindProblems(Errors, Warnings);
	TestTrue(TEXT("Zero-wait cycle flagged"), AnyContains(Errors, TEXT("zero-wait cycle")));
	F.Start(Static, R, {}, OwnerOf(3));
	TestTrue(TEXT("Invalid graph refused"), R.Outcome == EDocResultOutcome::InvalidConfiguration);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-06
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueEndTest, "Doc.Dialogue.End", DocDialogueTests::Flags)
bool FDocDialogueEndTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("EndTest"), TEXT("L1"));
	G->Roles = { MakeRole(TEXT("NPC"), true, EDocDialogueLossPolicy::PauseWithTimeout, 5.f) };
	G->Nodes = { MakeLine(TEXT("L1"), TEXT("EndN")), MakeNode(TEXT("EndN"), EDocDialogueNodeType::End) };
	UDocDialogueParticipantComponent* Npc = F.Participant(TEXT("Npc"));

	FDocSystemResult R;
	FDocRequestHandle H = F.Start(G, R, { Bind(TEXT("NPC"), Npc) });
	TestEqual(TEXT("Reserved"), F.S->GetReservationCount(TEXT("Npc")), 1);
	F.S->Advance(H, F.Rev(H), nullptr);
	TestTrue(TEXT("Normal end"), F.Ended.Last() == EDocDialogueSessionState::Completed);
	TestEqual(TEXT("Released on end"), F.S->GetReservationCount(TEXT("Npc")), 0);

	H = F.Start(G, R, { Bind(TEXT("NPC"), Npc) });
	TestTrue(TEXT("Cancel"), F.S->Cancel(H, nullptr).IsSuccess());
	TestTrue(TEXT("Cancelled"), F.Ended.Last() == EDocDialogueSessionState::Cancelled);
	TestEqual(TEXT("Released on cancel"), F.S->GetReservationCount(TEXT("Npc")), 0);

	H = F.Start(G, R, { Bind(TEXT("NPC"), Npc) });
	TestTrue(TEXT("Explicit End"), F.S->End(H, nullptr).IsSuccess());
	TestTrue(TEXT("Completed by End"), F.Ended.Last() == EDocDialogueSessionState::Completed);

	UDocDialogueGraph* Broken = NewGraph(TEXT("EndFail"), TEXT("X"));
	Broken->Roles = { MakeRole(TEXT("NPC")) };
	FDocDialogueNode X = MakeNode(TEXT("X"), EDocDialogueNodeType::Custom);
	X.CustomType = TEXT("Nope");
	Broken->Nodes = { X };
	F.Start(Broken, R, { Bind(TEXT("NPC"), Npc) });
	TestTrue(TEXT("Missing handler fails the session"), R.Outcome == EDocResultOutcome::Unsupported);
	TestTrue(TEXT("Failed"), F.Ended.Last() == EDocDialogueSessionState::Failed);
	TestEqual(TEXT("Released on failure"), F.S->GetReservationCount(TEXT("Npc")), 0);

	H = F.Start(G, R, { Bind(TEXT("NPC"), Npc) });
	Npc->GetOwner()->Destroy();
	TestTrue(TEXT("Loss pauses"), F.Snap(H).State == EDocDialogueSessionState::Paused);
	F.S->AdvanceTimeForTesting(4.0);
	TestTrue(TEXT("Still paused"), F.Snap(H).State == EDocDialogueSessionState::Paused);
	F.S->AdvanceTimeForTesting(2.0);
	TestTrue(TEXT("Pause timeout"), F.Ended.Last() == EDocDialogueSessionState::TimedOut);
	TestEqual(TEXT("Released on timeout"), F.S->GetReservationCount(TEXT("Npc")), 0);
	TestEqual(TEXT("No sessions left"), F.S->GetActiveSessionCount(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-07
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueConcurrentParticipantsTest, "Doc.Dialogue.ConcurrentParticipants", DocDialogueTests::Flags)
bool FDocDialogueConcurrentParticipantsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("Conv"), TEXT("L1"));
	G->Roles = { MakeRole(TEXT("NPC")), MakeRole(TEXT("Narrator")) };
	G->Nodes = { MakeLine(TEXT("L1"), NAME_None) };
	// "Narrator" sorts before "Zed_*", so a conflict on the NPC must roll the narrator back.
	UDocDialogueParticipantComponent* Guard = F.Participant(TEXT("Zed_Guard"));
	UDocDialogueParticipantComponent* Merchant = F.Participant(TEXT("Zed_Merchant"));
	UDocDialogueParticipantComponent* Narrator = F.Participant(TEXT("Narrator"), true);

	FDocSystemResult R;
	const FDocRequestHandle A = F.Start(G, R, { Bind(TEXT("NPC"), Guard), Bind(TEXT("Narrator"), Narrator) }, OwnerOf(1));
	TestTrue(TEXT("A started"), R.IsSuccess());

	F.Start(G, R, { Bind(TEXT("NPC"), Guard), Bind(TEXT("Narrator"), Narrator) }, OwnerOf(2));
	TestTrue(TEXT("Exclusive conflict"), R.Outcome == EDocResultOutcome::Conflict && R.ErrorTag == DocDialogueTags::Error_Dialogue_ReservationConflict);
	TestEqual(TEXT("All-or-none: narrator rolled back"), F.S->GetReservationCount(TEXT("Narrator")), 1);

	const FDocRequestHandle C = F.Start(G, R, { Bind(TEXT("NPC"), Merchant), Bind(TEXT("Narrator"), Narrator) }, OwnerOf(3));
	TestTrue(TEXT("Different NPC, shared narrator"), R.IsSuccess());
	TestEqual(TEXT("Narrator shared by two"), F.S->GetReservationCount(TEXT("Narrator")), 2);

	F.Start(G, R, { Bind(TEXT("NPC"), Narrator), Bind(TEXT("Narrator"), Narrator) }, OwnerOf(4));
	TestTrue(TEXT("One participant in two roles rejected"), R.Outcome == EDocResultOutcome::InvalidInput);

	F.S->Cancel(A, nullptr);
	F.Start(G, R, { Bind(TEXT("NPC"), Guard), Bind(TEXT("Narrator"), Narrator) }, OwnerOf(2));
	TestTrue(TEXT("Guard free after A ended"), R.IsSuccess());
	TestEqual(TEXT("Guard reserved once"), F.S->GetReservationCount(TEXT("Zed_Guard")), 1);
	F.S->Cancel(C, nullptr);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-08
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueActionReceiptTest, "Doc.Dialogue.ActionReceipt", DocDialogueTests::Flags)
bool FDocDialogueActionReceiptTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	TSharedPtr<FRecordingActions> Shop = MakeShared<FRecordingActions>();
	TSharedPtr<FRecordingActions> Refund = MakeShared<FRecordingActions>();
	Refund->bCompensation = true;
	F.S->RegisterActionProvider(TEXT("Shop"), Shop);
	F.S->RegisterActionProvider(TEXT("Refund"), Refund);

	FDocDialogueAction Grant = MakeCustom(TEXT("Grant"), TEXT("Shop"), TEXT("item=Sword"));
	Grant.bCompensateOnCancel = true; // requested, but Shop advertises no compensation
	FDocDialogueAction Token = MakeCustom(TEXT("Token"), TEXT("Refund"));
	Token.bCompensateOnCancel = true;
	FDocDialogueChoice Buy = MakeChoice(TEXT("Buy"), TEXT("L2"));
	Buy.Actions = { Grant, MakeCustom(TEXT("Pay"), TEXT("Shop"), TEXT("gold=10")), Token,
		MakeVarAction(TEXT("Mark"), EDocDialogueActionType::SetVariable, TEXT("Bought"), FDocDialogueValue::MakeInt(1)) };
	UDocDialogueGraph* G = NewGraph(TEXT("Shop"), TEXT("C1"));
	G->Variables = { MakeVar(TEXT("Bought"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(0)) };
	G->Nodes = { MakeChoiceNode(TEXT("C1"), { Buy, MakeChoice(TEXT("Leave"), NAME_None) }), MakeLine(TEXT("L2"), NAME_None) };

	Shop->FailOnce.Add(TEXT("Pay"));
	FDocSystemResult R;
	const FDocRequestHandle H = F.Start(G, R);
	const int64 R1 = F.Rev(H);
	R = F.S->SelectChoice(H, TEXT("Buy"), R1, nullptr);
	TestFalse(TEXT("Failed required action rejects the transition"), R.IsSuccess());
	TestTrue(TEXT("Back to the choice"), F.Snap(H).State == EDocDialogueSessionState::WaitingForChoice);
	TestEqual(TEXT("Staged variable not committed"), F.VarInt(H, TEXT("Bought")), 0LL);
	TestEqual(TEXT("Grant ran once"), Shop->Count(TEXT("Grant")), 1);
	TestEqual(TEXT("Token never ran"), Refund->Count(TEXT("Token")), 0);
	TestTrue(TEXT("Stale choice rejected"), F.S->SelectChoice(H, TEXT("Buy"), R1, nullptr).Outcome == EDocResultOutcome::Conflict);

	const int64 R2 = F.Rev(H);
	TestTrue(TEXT("Retry succeeds"), F.S->SelectChoice(H, TEXT("Buy"), R2, nullptr).IsSuccess());
	TestEqual(TEXT("Committed Grant not replayed"), Shop->Count(TEXT("Grant")), 1);
	TestEqual(TEXT("Pay retried"), Shop->Count(TEXT("Pay")), 2);
	TestEqual(TEXT("Token ran"), Refund->Count(TEXT("Token")), 1);
	TestEqual(TEXT("Variable committed"), F.VarInt(H, TEXT("Bought")), 1LL);
	const TArray<FDocDialogueActionRecord> Log = F.S->GetActionLog(H);
	const FDocDialogueActionRecord* GrantRecord = Log.FindByPredicate([](const FDocDialogueActionRecord& X) { return X.Key.ActionId == FName(TEXT("Grant")); });
	TestTrue(TEXT("Grant recorded as duplicate"), GrantRecord && GrantRecord->State == EDocDialogueActionState::Duplicate);

	TestFalse(TEXT("Duplicate delivery rejected"), F.S->SelectChoice(H, TEXT("Buy"), R2, nullptr).IsSuccess());
	TestEqual(TEXT("No extra execution"), Shop->Count(TEXT("Pay")), 2);

	F.S->Cancel(H, nullptr);
	TestEqual(TEXT("Validated compensation ran"), Refund->Compensations, 1);
	TestEqual(TEXT("No silent take-back without a contract"), Shop->Compensations, 0);

	// Pending action -> WaitingForAction -> completion.
	TSharedPtr<FRecordingActions> Async = MakeShared<FRecordingActions>();
	Async->bPending = true;
	F.S->RegisterActionProvider(TEXT("Async"), Async);
	UDocDialogueGraph* P = NewGraph(TEXT("Async"), TEXT("E0"));
	FDocDialogueNode E0 = MakeNode(TEXT("E0"), EDocDialogueNodeType::Event, TEXT("L1"));
	E0.Actions = { MakeCustom(TEXT("Work"), TEXT("Async")) };
	P->Nodes = { E0, MakeLine(TEXT("L1"), NAME_None) };
	const FDocRequestHandle HP = F.Start(P, R);
	TestTrue(TEXT("Waiting for action"), F.Snap(HP).State == EDocDialogueSessionState::WaitingForAction);
	TestTrue(TEXT("Wrong action id"), F.S->CompletePendingAction(HP, TEXT("Other"), FDocSystemResult::MakeSuccess()).Outcome == EDocResultOutcome::Conflict);
	TestTrue(TEXT("Completion"), F.S->CompletePendingAction(HP, TEXT("Work"), FDocSystemResult::MakeSuccess()).IsSuccess());
	TestEqual(TEXT("Transition committed"), F.Snap(HP).Node.NodeId, FName(TEXT("L1")));
	FDocDialogueMemory Memory;
	F.S->GetMemory(TEXT("Async"), OwnerOf(1), Memory);
	TestEqual(TEXT("Receipt stored"), Memory.Receipts.Num(), 1);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-09
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueSaveRestoreTest, "Doc.Dialogue.SaveRestore", DocDialogueTests::Flags)
bool FDocDialogueSaveRestoreTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	TSharedPtr<FRecordingActions> Shop = MakeShared<FRecordingActions>();
	F.S->RegisterActionProvider(TEXT("Shop"), Shop);

	UDocDialogueGraph* G = NewGraph(TEXT("Save"), TEXT("E0"));
	G->Variables = {
		MakeVar(TEXT("Trust"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(0), true),
		MakeVar(TEXT("Temp"), EDocDialogueValueType::Int, FDocDialogueValue::MakeInt(0)) };
	FDocDialogueNode E0 = MakeNode(TEXT("E0"), EDocDialogueNodeType::Event, TEXT("C1"));
	E0.Actions = { MakeVarAction(TEXT("SetTrust"), EDocDialogueActionType::SetVariable, TEXT("Trust"), FDocDialogueValue::MakeInt(5)), MakeCustom(TEXT("Grant"), TEXT("Shop")) };
	FDocDialogueChoice B = MakeChoice(TEXT("B"), TEXT("LB"));
	B.bOnceOnly = true;
	G->Nodes = { E0, MakeChoiceNode(TEXT("C1"), { MakeChoice(TEXT("A"), TEXT("LA")), B }), MakeLine(TEXT("LA"), NAME_None), MakeLine(TEXT("LB"), NAME_None) };

	FDocSystemResult R;
	FDocRequestHandle H = F.Start(G, R);
	TestTrue(TEXT("At the choice"), F.Snap(H).State == EDocDialogueSessionState::WaitingForChoice);
	TestEqual(TEXT("Grant once"), Shop->Count(TEXT("Grant")), 1);

	TArray<uint8> Bytes;
	DocCoreSerialization::Encode(F.S->CaptureState(), Bytes);
	FDocDialogueSaveData Saved;
	TestTrue(TEXT("Round trip"), DocCoreSerialization::Decode(Saved, Bytes));
	TestEqual(TEXT("One memory"), Saved.Memories.Num(), 1);
	if (Saved.Memories.Num() == 1)
	{
		TestTrue(TEXT("Checkpoint at a safe boundary"), Saved.Memories[0].Checkpoint.bValid && Saved.Memories[0].Checkpoint.NodeId == FName(TEXT("C1")));
		TestEqual(TEXT("Receipt saved"), Saved.Memories[0].Receipts.Num(), 1);
	}

	TestTrue(TEXT("Restore refused while active"), F.S->RestoreState(Saved).Outcome == EDocResultOutcome::Conflict);
	F.S->Cancel(H, nullptr);
	TestTrue(TEXT("Restore"), F.S->RestoreState(Saved).IsSuccess());

	H = F.S->ResumeFromCheckpoint(F.Request(G, OwnerOf(1)), EDocDialogueRestorePolicy::Error, R);
	TestTrue(TEXT("Resumed"), R.IsSuccess());
	TestEqual(TEXT("Resumed at checkpoint"), F.Snap(H).Node.NodeId, FName(TEXT("C1")));
	TestEqual(TEXT("Variables restored"), F.VarInt(H, TEXT("Trust")), 5LL);
	TestEqual(TEXT("Committed action not replayed"), Shop->Count(TEXT("Grant")), 1);
	F.S->SelectChoice(H, TEXT("B"), F.Rev(H), nullptr);
	F.S->Advance(H, F.Rev(H), nullptr);
	FDocDialogueMemory Memory;
	F.S->GetMemory(TEXT("Save"), OwnerOf(1), Memory);
	TestTrue(TEXT("History kept"), Memory.VisitedNodes.Contains(FName(TEXT("E0"))) && Memory.OnceChoicesTaken.Contains(FName(TEXT("B"))));
	TestFalse(TEXT("Checkpoint cleared at end"), Memory.Checkpoint.bValid);

	// Content migration: removed node -> error, redirect, or explicit restart. Never a silent jump.
	TestTrue(TEXT("Restore again"), F.S->RestoreState(Saved).IsSuccess());
	G->Nodes[1].NodeId = TEXT("C1v2");
	G->Nodes[0].Next = TEXT("C1v2");
	F.S->ResumeFromCheckpoint(F.Request(G, OwnerOf(1)), EDocDialogueRestorePolicy::Error, R);
	TestTrue(TEXT("Missing node is an error"), R.Outcome == EDocResultOutcome::NotFound && R.ErrorTag == DocDialogueTags::Error_Dialogue_MissingNode);
	G->NodeRedirects.Add(TEXT("C1"), TEXT("C1v2"));
	H = F.S->ResumeFromCheckpoint(F.Request(G, OwnerOf(1)), EDocDialogueRestorePolicy::Error, R);
	TestEqual(TEXT("Redirected"), F.Snap(H).Node.NodeId, FName(TEXT("C1v2")));
	F.S->Cancel(H, nullptr);
	TestTrue(TEXT("Restore for restart"), F.S->RestoreState(Saved).IsSuccess());
	G->NodeRedirects.Reset();
	H = F.S->ResumeFromCheckpoint(F.Request(G, OwnerOf(1)), EDocDialogueRestorePolicy::RestartFromStart, R);
	TestTrue(TEXT("Explicit restart"), R.IsSuccess());
	TestEqual(TEXT("Restart replays only by explicit choice"), Shop->Count(TEXT("Grant")), 2);
	F.S->Cancel(H, nullptr);

	// In-flight non-idempotent action: not retried automatically; host resolves.
	TSharedPtr<FRecordingActions> Async = MakeShared<FRecordingActions>();
	Async->bPending = true;
	F.S->RegisterActionProvider(TEXT("Async"), Async);
	UDocDialogueGraph* P = NewGraph(TEXT("Pend"), TEXT("E0"));
	FDocDialogueNode PE = MakeNode(TEXT("E0"), EDocDialogueNodeType::Event, TEXT("L1"));
	PE.Actions = { MakeCustom(TEXT("Work"), TEXT("Async")) };
	P->Nodes = { PE, MakeLine(TEXT("L1"), NAME_None) };
	H = F.Start(P, R, {}, OwnerOf(2));
	TestTrue(TEXT("In flight"), F.Snap(H).State == EDocDialogueSessionState::WaitingForAction);
	const FDocDialogueSaveData WithIntent = F.S->CaptureState();
	F.S->Cancel(H, nullptr);
	TestTrue(TEXT("Restore intent"), F.S->RestoreState(WithIntent).IsSuccess());
	H = F.S->ResumeFromCheckpoint(F.Request(P, OwnerOf(2)), EDocDialogueRestorePolicy::Error, R);
	TestTrue(TEXT("Unresolved action blocks resume"), !H.IsSet() && R.ErrorTag == DocDialogueTags::Error_Dialogue_UnresolvedAction);
	TestTrue(TEXT("Host resolves as committed"), F.S->ResolveCheckpointAction(P, OwnerOf(2), true).IsSuccess());
	H = F.S->ResumeFromCheckpoint(F.Request(P, OwnerOf(2)), EDocDialogueRestorePolicy::Error, R);
	TestTrue(TEXT("Resumed after resolution"), R.IsSuccess());
	TestEqual(TEXT("Transition completed without re-execution"), F.Snap(H).Node.NodeId, FName(TEXT("L1")));
	TestEqual(TEXT("Work executed once"), Async->Count(TEXT("Work")), 1);
	return true;
}

// ---------------------------------------------------------------------------
// DIA-10
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueVoiceAndLocalizationTest, "Doc.Dialogue.VoiceAndLocalization", DocDialogueTests::Flags)
bool FDocDialogueVoiceAndLocalizationTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	const UDocDialogueSettings* Cfg = GetDefault<UDocDialogueSettings>();
	auto Estimate = [Cfg](const TCHAR* Text)
	{
		return FMath::Clamp(FCString::Strlen(Text) * Cfg->TextSecondsPerCharacter, Cfg->MinLineSeconds, FMath::Max(Cfg->MinLineSeconds, Cfg->MaxLineSeconds));
	};
	F.S->SetVoiceDurationResolver([](const TSoftObjectPtr<USoundBase>& Voice)
	{
		return Voice.ToSoftObjectPath().ToString().Contains(TEXT("VO_L3")) ? 3.f : -1.f;
	});

	auto VoiceLine = [](FName Id, FName Next, const TCHAR* Text, const TCHAR* Voice)
	{
		FDocDialogueNode N = MakeLine(Id, Next, Text);
		N.bAutoAdvance = true;
		N.TimingSource = EDocDialogueLineTiming::VoiceDuration;
		N.VoiceAsset = TSoftObjectPtr<USoundBase>(FSoftObjectPath(Voice));
		return N;
	};
	FDocDialogueNode L1 = VoiceLine(TEXT("L1"), TEXT("L2"), TEXT("Hi"), TEXT("/Game/VO/VO_L1.VO_L1"));
	L1.MissingVoicePolicy = EDocDialogueMissingVoicePolicy::UseTextEstimate;
	FDocDialogueNode L2 = VoiceLine(TEXT("L2"), TEXT("L3"), TEXT("Second"), TEXT("/Game/VO/VO_L2.VO_L2"));
	L2.MissingVoicePolicy = EDocDialogueMissingVoicePolicy::UseDuration;
	L2.Duration = 2.5f;
	FDocDialogueNode L3 = VoiceLine(TEXT("L3"), TEXT("L4"), TEXT("Voiced"), TEXT("/Game/VO/VO_L3.VO_L3"));
	L3.SubtitleTiming.MinDisplaySeconds = 3.5f;
	FDocDialogueNode L4 = MakeLine(TEXT("L4"), NAME_None, TEXT("Bye"));
	L4.bAutoAdvance = true;
	L4.TimingSource = EDocDialogueLineTiming::PresentationAck;
	UDocDialogueGraph* G = NewGraph(TEXT("Voice"), TEXT("L1"));
	G->Nodes = { L1, L2, L3, L4 };

	FDocSystemResult R;
	F.Start(G, R);
	constexpr double Eps = 0.001;
	TestEqual(TEXT("L1 shown"), F.Shown.Num(), 1);
	if (F.Shown.Num() >= 1)
	{
		TestTrue(TEXT("Missing voice reported"), F.Shown[0].bVoiceMissing);
		TestTrue(TEXT("Subtitle text is the authored FText"), F.Shown[0].Text.EqualTo(L1.Text));
		TestEqual(TEXT("Text-estimate fallback"), F.Shown[0].ResolvedDuration, Estimate(TEXT("Hi")), 0.0001f);
	}
	F.S->AdvanceTimeForTesting(Estimate(TEXT("Hi")) + Eps);
	TestEqual(TEXT("L2 shown"), F.Shown.Num(), 2);
	if (F.Shown.Num() >= 2)
	{
		TestTrue(TEXT("L2 voice missing"), F.Shown[1].bVoiceMissing);
		TestEqual(TEXT("Authored duration fallback"), F.Shown[1].ResolvedDuration, 2.5f, 0.0001f);
	}
	F.S->AdvanceTimeForTesting(2.5 + Eps);
	TestEqual(TEXT("L3 shown"), F.Shown.Num(), 3);
	if (F.Shown.Num() >= 3)
	{
		TestFalse(TEXT("L3 voice resolved"), F.Shown[2].bVoiceMissing);
		TestEqual(TEXT("Subtitle minimum wins over 3s voice"), F.Shown[2].ResolvedDuration, 3.5f, 0.0001f);
	}
	F.S->AdvanceTimeForTesting(3.5 + Eps);
	TestEqual(TEXT("L4 shown"), F.Shown.Num(), 4);
	// No presentation adapter acknowledges L4: the safety timeout completes it (no headless deadlock).
	F.S->AdvanceTimeForTesting(FMath::Max(static_cast<double>(Cfg->PresentationAckTimeoutSeconds), static_cast<double>(Estimate(TEXT("Bye")))) + Eps);
	TestTrue(TEXT("Headless session completed"), F.Ended.Num() == 1 && F.Ended[0] == EDocDialogueSessionState::Completed);
	// Cooked second-culture verification requires a packaged build (see README).
	return true;
}

// ---------------------------------------------------------------------------
// DIA-11 (local/standalone part)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueAuthorityAndAudienceTest, "Doc.Dialogue.AuthorityAndAudience", DocDialogueTests::Flags)
bool FDocDialogueAuthorityAndAudienceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocDialogueGraph* G = NewGraph(TEXT("Auth"), TEXT("L1"));
	G->Nodes = { MakeLine(TEXT("L1"), TEXT("L2")), MakeLine(TEXT("L2"), NAME_None) };
	AActor* Initiator = F.TW.Spawn<AActor>();
	AActor* Friend = F.TW.Spawn<AActor>();
	AActor* Stranger = F.TW.Spawn<AActor>();

	FDocSystemResult R;
	FDocRequestHandle H = F.Start(G, R, {}, OwnerOf(1), EDocDialogueAuthorityMode::OwnerAuthoritative, Initiator, { Friend });
	R = F.S->Advance(H, F.Rev(H), Stranger);
	TestTrue(TEXT("Stranger denied"), R.Outcome == EDocResultOutcome::PermissionDenied && R.ErrorTag == DocDialogueTags::Error_Dialogue_Unauthorized);
	TestTrue(TEXT("Audience cannot drive an owner conversation"), F.S->Advance(H, F.Rev(H), Friend).Outcome == EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Initiator drives"), F.S->Advance(H, F.Rev(H), Initiator).IsSuccess());
	TestTrue(TEXT("Stranger cannot cancel"), F.S->Cancel(H, Stranger).Outcome == EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Host code may end"), F.S->End(H, nullptr).IsSuccess());

	H = F.Start(G, R, {}, OwnerOf(1), EDocDialogueAuthorityMode::SharedAuthoritative, Initiator, { Friend });
	TestTrue(TEXT("Audience drives a shared conversation"), F.S->Advance(H, F.Rev(H), Friend).IsSuccess());
	TestTrue(TEXT("Stranger still denied"), F.S->Advance(H, F.Rev(H), Stranger).Outcome == EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Pause"), F.S->Pause(H, Friend).IsSuccess());
	TestTrue(TEXT("Paused rejects advance"), F.S->Advance(H, F.Rev(H), Friend).Outcome == EDocResultOutcome::NotReady);
	TestTrue(TEXT("Resume"), F.S->Resume(H, Friend).IsSuccess());
	TestTrue(TEXT("Initiator cancels"), F.S->Cancel(H, Initiator).IsSuccess());

	H = F.Start(G, R, {}, OwnerOf(1), EDocDialogueAuthorityMode::LocalOnly, Initiator, { Friend });
	TestTrue(TEXT("Local conversation runs independently"), F.S->Advance(H, F.Rev(H), Friend).IsSuccess());
	TestTrue(TEXT("Local stranger denied"), F.S->Advance(H, F.Rev(H), Stranger).Outcome == EDocResultOutcome::PermissionDenied);
	return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocDialogueValidationTest, "Doc.Dialogue.Validation", DocDialogueTests::Flags)
bool FDocDialogueValidationTest::RunTest(const FString& Parameters)
{
	UDocDialogueGraph* Bad = NewGraph(TEXT("Bad"), TEXT("Nope"));
	FDocDialogueNode L1 = MakeLine(TEXT("L1"), TEXT("Missing"));
	L1.SpeakerRole = TEXT("Ghost");
	FDocDialogueNode E1 = MakeNode(TEXT("E1"), EDocDialogueNodeType::Event);
	E1.Actions = { MakeVarAction(TEXT("Set"), EDocDialogueActionType::SetVariable, TEXT("Undeclared"), FDocDialogueValue::MakeInt(1)) };
	Bad->Nodes = { L1, MakeLine(TEXT("L1"), NAME_None), E1 };
	TArray<FString> Errors, Warnings;
	Bad->FindProblems(Errors, Warnings);
	TestTrue(TEXT("Missing start"), AnyContains(Errors, TEXT("Missing start node")));
	TestTrue(TEXT("Duplicate id"), AnyContains(Errors, TEXT("Duplicate NodeId L1")));
	TestTrue(TEXT("Broken link"), AnyContains(Errors, TEXT("broken link to Missing")));
	TestTrue(TEXT("Missing role"), AnyContains(Errors, TEXT("missing participant role Ghost")));
	TestTrue(TEXT("Undeclared variable"), AnyContains(Errors, TEXT("undeclared variable")));

	UDocDialogueGraph* Warn = NewGraph(TEXT("Warn"), TEXT("L1"));
	FDocDialogueNode Intentional = MakeLine(TEXT("L3"), NAME_None);
	Intentional.bIntentionallyUnreachable = true;
	Warn->Nodes = { MakeLine(TEXT("L1"), NAME_None), MakeLine(TEXT("L2"), NAME_None), Intentional };
	Errors.Reset();
	Warnings.Reset();
	Warn->FindProblems(Errors, Warnings);
	TestEqual(TEXT("No errors"), Errors.Num(), 0);
	TestTrue(TEXT("Unreachable warned"), AnyContains(Warnings, TEXT("Node L2 is unreachable")));
	TestFalse(TEXT("Intentional not warned"), AnyContains(Warnings, TEXT("Node L3")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
