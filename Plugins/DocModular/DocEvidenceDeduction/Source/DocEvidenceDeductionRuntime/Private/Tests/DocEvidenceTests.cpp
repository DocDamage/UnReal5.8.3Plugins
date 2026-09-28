#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocEvidenceSubsystem.h"
#include "DocEvidenceDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocEvidenceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocEvidenceSubsystem> Keep;
		UDocEvidenceSubsystem* Subsystem = nullptr;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			Subsystem = NewObject<UDocEvidenceSubsystem>(GameInstance.Get());
			Keep.Reset(Subsystem);
		}

		~FFixture()
		{
			if (Subsystem)
			{
				Subsystem->OnObservationAddedNative.Clear();
				Subsystem->OnObservationRevisedNative.Clear();
				Subsystem->OnObservationRetractedNative.Clear();
				Subsystem->OnConclusionCommittedNative.Clear();
				Subsystem->OnConclusionChallengedNative.Clear();
			}
		}

		UDocEvidenceDefinition* MakeEvidence(FName Id, bool bSecret = false)
		{
			UDocEvidenceDefinition* Def = NewObject<UDocEvidenceDefinition>(GetTransientPackage());
			Def->EvidenceId = Id;
			Def->bIsSecret = bSecret;
			Subsystem->RegisterEvidenceDefinition(Def);
			return Def;
		}

		UDocHypothesisDefinition* NewHypothesis(FName Id, const TArray<FName>& ReqEvidence, const TArray<FName>& DisqEvidence = {})
		{
			UDocHypothesisDefinition* Def = NewObject<UDocHypothesisDefinition>(GetTransientPackage());
			Def->HypothesisId = Id;
			if (ReqEvidence.Num() > 0)
			{
				FDocEvidenceSet Set;
				Set.RequiredEvidenceIds = ReqEvidence;
				Def->AlternativeRequiredEvidenceSets.Add(Set);
			}
			Def->DisqualifyingEvidenceIds = DisqEvidence;
			return Def;
		}

		UDocHypothesisDefinition* MakeHypothesis(FName Id, const TArray<FName>& ReqEvidence, const TArray<FName>& DisqEvidence = {})
		{
			UDocHypothesisDefinition* Def = NewHypothesis(Id, ReqEvidence, DisqEvidence);
			Subsystem->RegisterHypothesisDefinition(Def);
			return Def;
		}

		FDocSystemResult AddObs(FName ObsId, FName EvId, EDocEvidenceStatus Status = EDocEvidenceStatus::EstablishedFact, FName Owner = NAME_None, bool bHidden = false)
		{
			FDocEvidenceObservation Obs;
			Obs.ObservationId = ObsId;
			Obs.EvidenceId = EvId;
			Obs.Status = Status;
			Obs.SourceEntityId = TEXT("Detective");
			Obs.ClaimContent = TEXT("Evidence text");
			Obs.bIsHiddenFromPlayer = bHidden;
			return Subsystem->AddObservation(Obs, Owner);
		}

		EDocHypothesisEvaluation Eval(FName Hyp, FName Owner = NAME_None)
		{
			FDocDeductionResult R;
			Subsystem->EvaluateHypothesis(Hyp, R, Owner);
			return R.Evaluation;
		}
	};
}

// EVD-01: Doc.Evidence.FactClaimSeparation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceFactClaimSeparationTest, FAutomationTestBase, "Doc.Evidence.FactClaimSeparation", DocEvidenceTests::Flags)
bool FDocEvidenceFactClaimSeparationTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Footprints"));
	Fix.MakeEvidence(TEXT("Ev_Mud"));
	Fix.MakeHypothesis(TEXT("H_Intruder"), { TEXT("Ev_Footprints") });

	TestTrue(TEXT("Testimony recorded as a claim"), Fix.AddObs(TEXT("Obs_Witness"), TEXT("Ev_Footprints"), EDocEvidenceStatus::Claim).IsSuccess());
	TestEqual(TEXT("A claim does not support"), Fix.Eval(TEXT("H_Intruder")), EDocHypothesisEvaluation::Inconclusive);

	TestEqual(TEXT("A revision cannot silently promote a claim"),
		Fix.Subsystem->ReviseObservation(TEXT("Obs_Witness"), TEXT("I am sure now"), EDocEvidenceStatus::EstablishedFact).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Claim content can be revised as a claim"),
		Fix.Subsystem->ReviseObservation(TEXT("Obs_Witness"), TEXT("Big boots"), EDocEvidenceStatus::Claim).IsSuccess());
	TestEqual(TEXT("Still not supported"), Fix.Eval(TEXT("H_Intruder")), EDocHypothesisEvaluation::Inconclusive);

	FDocEvidenceLink Link;
	Link.LinkId = TEXT("Board_1");
	Link.SourceEvidenceId = TEXT("Ev_Mud");
	Link.TargetEvidenceId = TEXT("Ev_Footprints");
	Link.bPlayerAuthored = true;
	const int64 RevBeforeLink = Fix.Subsystem->GetEvidenceRevision();
	TestTrue(TEXT("Player draws a board link"), Fix.Subsystem->AddInterpretationLink(Link).IsSuccess());
	TestEqual(TEXT("A player link does not make the claim true"), Fix.Eval(TEXT("H_Intruder")), EDocHypothesisEvaluation::Inconclusive);
	TestEqual(TEXT("Interpretation does not change the evidence revision"), Fix.Subsystem->GetEvidenceRevision(), RevBeforeLink);

	TestEqual(TEXT("Establishing needs a corroborating source"), Fix.Subsystem->EstablishClaim(TEXT("Obs_Witness"), NAME_None).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Explicit promotion with provenance"), Fix.Subsystem->EstablishClaim(TEXT("Obs_Witness"), TEXT("Forensics")).IsSuccess());
	TestEqual(TEXT("Now supported"), Fix.Eval(TEXT("H_Intruder")), EDocHypothesisEvaluation::Supported);

	FDocEvidenceObservation Obs;
	Fix.Subsystem->GetObservation(TEXT("Obs_Witness"), Obs);
	TestEqual(TEXT("History keeps both prior revisions"), Obs.History.Num(), 2);
	TestTrue(TEXT("Promotion recorded with its source"), Obs.History.Num() == 2 && Obs.History[1].Reason == FName(TEXT("EstablishedBy:Forensics")));
	TestTrue(TEXT("Original claim status preserved in history"), Obs.History.Num() == 2 && Obs.History[1].Status == EDocEvidenceStatus::Claim);
	TestEqual(TEXT("Observation of an unregistered evidence id refused"), Fix.AddObs(TEXT("Obs_X"), TEXT("Ev_Typo")).Outcome, EDocResultOutcome::NotFound);

	return true;
}

// EVD-02: Doc.Evidence.SupportAndContradiction
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceSupportAndContradictionTest, FAutomationTestBase, "Doc.Evidence.SupportAndContradiction", DocEvidenceTests::Flags)
bool FDocEvidenceSupportAndContradictionTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Knife"));
	Fix.MakeEvidence(TEXT("Ev_Alibi"));
	Fix.MakeEvidence(TEXT("Ev_Sighting"));
	Fix.MakeHypothesis(TEXT("H_Cook"), { TEXT("Ev_Knife") }, { TEXT("Ev_Alibi") });

	TestEqual(TEXT("Nothing known: inconclusive"), Fix.Eval(TEXT("H_Cook")), EDocHypothesisEvaluation::Inconclusive);
	Fix.AddObs(TEXT("Obs_Knife"), TEXT("Ev_Knife"));
	TestEqual(TEXT("Required fact: supported"), Fix.Eval(TEXT("H_Cook")), EDocHypothesisEvaluation::Supported);
	Fix.AddObs(TEXT("Obs_Alibi"), TEXT("Ev_Alibi"));
	FDocDeductionResult R;
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Cook"), R);
	TestEqual(TEXT("Disqualifying fact: contradicted"), R.Evaluation, EDocHypothesisEvaluation::Contradicted);
	TestTrue(TEXT("Contradicting evidence listed"), R.ContradictingEvidenceIds.Contains(TEXT("Ev_Alibi")));

	// Typed contradiction: mutually exclusive statements about one subject.
	Fix.MakeHypothesis(TEXT("H_Butler"), { TEXT("Ev_Sighting") });
	FDocEvidenceObservation A;
	A.ObservationId = TEXT("Obs_Maid");
	A.EvidenceId = TEXT("Ev_Sighting");
	A.Status = EDocEvidenceStatus::EstablishedFact;
	A.SubjectId = TEXT("Butler");
	A.ClaimValue = TEXT("InKitchen");
	Fix.Subsystem->AddObservation(A);
	TestEqual(TEXT("Single sighting supports"), Fix.Eval(TEXT("H_Butler")), EDocHypothesisEvaluation::Supported);
	FDocEvidenceObservation B = A;
	B.ObservationId = TEXT("Obs_Gardener");
	B.ClaimValue = TEXT("InGarden");
	Fix.Subsystem->AddObservation(B);
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Butler"), R);
	TestEqual(TEXT("Mutually exclusive statements contradict"), R.Evaluation, EDocHypothesisEvaluation::Contradicted);
	TestTrue(TEXT("Typed as a mutually exclusive statement"), R.Contradictions.Num() == 1 && R.Contradictions[0].Kind == EDocContradictionKind::MutuallyExclusiveStatement);

	// Typed contradiction: incompatible timestamps in one event frame (precision-aware; unknown precision never contradicts).
	DocEvidenceTests::FFixture Time;
	Time.MakeEvidence(TEXT("Ev_Clock"));
	Time.MakeHypothesis(TEXT("H_Timeline"), { TEXT("Ev_Clock") });
	FDocEvidenceObservation T1;
	T1.ObservationId = TEXT("Obs_T1");
	T1.EvidenceId = TEXT("Ev_Clock");
	T1.Status = EDocEvidenceStatus::EstablishedFact;
	T1.SubjectId = TEXT("Shot");
	T1.EventFrameId = TEXT("Night");
	T1.EventTimestampSeconds = 100.0;
	T1.EventTimestampPrecisionSeconds = -1.0;
	FDocEvidenceObservation T2 = T1;
	T2.ObservationId = TEXT("Obs_T2");
	T2.EventTimestampSeconds = 400.0;
	Time.Subsystem->AddObservation(T1);
	Time.Subsystem->AddObservation(T2);
	TestEqual(TEXT("Unknown precision never contradicts"), Time.Eval(TEXT("H_Timeline")), EDocHypothesisEvaluation::Supported);

	DocEvidenceTests::FFixture Time2;
	Time2.MakeEvidence(TEXT("Ev_Clock"));
	Time2.MakeHypothesis(TEXT("H_Timeline"), { TEXT("Ev_Clock") });
	T1.EventTimestampPrecisionSeconds = 60.0;
	T2.EventTimestampPrecisionSeconds = 60.0;
	Time2.Subsystem->AddObservation(T1);
	Time2.Subsystem->AddObservation(T2);
	Time2.Subsystem->EvaluateHypothesis(TEXT("H_Timeline"), R);
	TestEqual(TEXT("300s apart with 60s precision contradicts"), R.Evaluation, EDocHypothesisEvaluation::Contradicted);
	TestTrue(TEXT("Typed as incompatible timestamp"), R.Contradictions.Num() == 1 && R.Contradictions[0].Kind == EDocContradictionKind::IncompatibleTimestamp);

	return true;
}

// EVD-03: Doc.Evidence.UnknownInputs
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceUnknownInputsTest, FAutomationTestBase, "Doc.Evidence.UnknownInputs", DocEvidenceTests::Flags)
bool FDocEvidenceUnknownInputsTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_A"));
	Fix.MakeEvidence(TEXT("Ev_B"));
	Fix.MakeEvidence(TEXT("Ev_Disq"));
	Fix.MakeHypothesis(TEXT("H"), { TEXT("Ev_A"), TEXT("Ev_B") }, { TEXT("Ev_Disq") });

	Fix.AddObs(TEXT("Obs_A"), TEXT("Ev_A"));
	FDocDeductionResult R;
	Fix.Subsystem->EvaluateHypothesis(TEXT("H"), R);
	TestEqual(TEXT("Missing B: inconclusive, not supported"), R.Evaluation, EDocHypothesisEvaluation::Inconclusive);
	TestEqual(TEXT("Missing count reported"), R.MissingEvidenceCount, 1);
	TestEqual(TEXT("Undisclosed identities are not listed"), R.MissingEvidenceIds.Num(), 0);
	TestFalse(TEXT("An undiscovered disqualifier is not proof of innocence or guilt"), R.Evaluation == EDocHypothesisEvaluation::Contradicted);

	Fix.AddObs(TEXT("Obs_Disq_Claim"), TEXT("Ev_Disq"), EDocEvidenceStatus::Claim);
	TestFalse(TEXT("A disqualifying claim does not contradict"), Fix.Eval(TEXT("H")) == EDocHypothesisEvaluation::Contradicted);

	Fix.AddObs(TEXT("Obs_B_Hidden"), TEXT("Ev_B"), EDocEvidenceStatus::EstablishedFact, NAME_None, /*bHidden*/ true);
	TestEqual(TEXT("Evidence hidden from the player cannot satisfy the player's rule"), Fix.Eval(TEXT("H")), EDocHypothesisEvaluation::Inconclusive);

	Fix.AddObs(TEXT("Obs_B"), TEXT("Ev_B"));
	TestEqual(TEXT("Both facts: supported"), Fix.Eval(TEXT("H")), EDocHypothesisEvaluation::Supported);
	Fix.Subsystem->RetractObservation(TEXT("Obs_B"));
	TestEqual(TEXT("A retracted fact no longer counts"), Fix.Eval(TEXT("H")), EDocHypothesisEvaluation::Inconclusive);

	TestEqual(TEXT("Unknown hypothesis is NotFound"), Fix.Subsystem->EvaluateHypothesis(TEXT("H_Nope"), R).Outcome, EDocResultOutcome::NotFound);

	return true;
}

// EVD-04: Doc.Evidence.AlternativeSets
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceAlternativeSetsTest, FAutomationTestBase, "Doc.Evidence.AlternativeSets", DocEvidenceTests::Flags)
bool FDocEvidenceAlternativeSetsTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	for (const TCHAR* Id : { TEXT("Ev_A"), TEXT("Ev_B"), TEXT("Ev_C"), TEXT("Ev_D") })
	{
		Fix.MakeEvidence(Id);
	}
	UDocHypothesisDefinition* H = Fix.NewHypothesis(TEXT("H_Alt"), { TEXT("Ev_A"), TEXT("Ev_B") });
	FDocEvidenceSet SetC;
	SetC.RequiredEvidenceIds = { TEXT("Ev_C") };
	H->AlternativeRequiredEvidenceSets.Add(SetC);
	Fix.Subsystem->RegisterHypothesisDefinition(H);

	Fix.AddObs(TEXT("Obs_C"), TEXT("Ev_C"));
	FDocDeductionResult R;
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Alt"), R);
	TestEqual(TEXT("Alternative set supports"), R.Evaluation, EDocHypothesisEvaluation::Supported);
	TestEqual(TEXT("Second set reported"), R.SatisfiedSetIndex, 1);

	// Exclusive conclusions.
	UDocHypothesisDefinition* Maid = Fix.NewHypothesis(TEXT("H_Maid"), { TEXT("Ev_A") });
	Maid->MutuallyExclusiveGroupId = TEXT("Culprit");
	UDocHypothesisDefinition* Chef = Fix.NewHypothesis(TEXT("H_Chef"), { TEXT("Ev_D") });
	Chef->MutuallyExclusiveGroupId = TEXT("Culprit");
	Fix.Subsystem->RegisterHypothesisDefinition(Maid);
	Fix.Subsystem->RegisterHypothesisDefinition(Chef);
	Fix.AddObs(TEXT("Obs_A"), TEXT("Ev_A"));
	Fix.AddObs(TEXT("Obs_D"), TEXT("Ev_D"));

	TestTrue(TEXT("Commit maid"), Fix.Subsystem->CommitConclusion(TEXT("C_Maid"), TEXT("H_Maid"), Fix.Subsystem->GetEvidenceRevision(), TEXT("R_Maid")).IsChanged());
	TestEqual(TEXT("Second culprit in the group refused"),
		Fix.Subsystem->CommitConclusion(TEXT("C_Chef"), TEXT("H_Chef"), Fix.Subsystem->GetEvidenceRevision(), TEXT("R_Chef")).Outcome, EDocResultOutcome::Conflict);
	const FDocSystemResult Again = Fix.Subsystem->CommitConclusion(TEXT("C_Maid"), TEXT("H_Maid"), Fix.Subsystem->GetEvidenceRevision(), TEXT("R_Maid"));
	TestTrue(TEXT("Recommitting the same conclusion is NoChange"), Again.IsSuccess() && !Again.IsChanged());
	TestEqual(TEXT("Choosing one does not delete the other's evidence"), Fix.Eval(TEXT("H_Chef")), EDocHypothesisEvaluation::Supported);
	TestTrue(TEXT("Chef's evidence still present"), Fix.Subsystem->HasObservation(TEXT("Obs_D")));
	TestTrue(TEXT("Ungrouped conclusions coexist"), Fix.Subsystem->CommitConclusion(TEXT("C_Alt"), TEXT("H_Alt"), Fix.Subsystem->GetEvidenceRevision(), NAME_None).IsChanged());

	return true;
}

// EVD-05: Doc.Evidence.RevisionInvalidation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceRevisionInvalidationTest, FAutomationTestBase, "Doc.Evidence.RevisionInvalidation", DocEvidenceTests::Flags)
bool FDocEvidenceRevisionInvalidationTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Glove"));
	Fix.MakeEvidence(TEXT("Ev_Unrelated"));
	UDocHypothesisDefinition* Base = Fix.NewHypothesis(TEXT("H_Glove"), { TEXT("Ev_Glove") });
	Base->bContradictionsDisqualify = false;
	Fix.Subsystem->RegisterHypothesisDefinition(Base);
	UDocHypothesisDefinition* Chain = Fix.NewHypothesis(TEXT("H_Chain"), {});
	Chain->DependentHypothesisIds = { TEXT("H_Glove") };
	Chain->bContradictionsDisqualify = false;
	TestTrue(TEXT("Dependent hypothesis registers"), Fix.Subsystem->RegisterHypothesisDefinition(Chain).IsSuccess());

	Fix.AddObs(TEXT("Obs_Glove"), TEXT("Ev_Glove"));
	TestEqual(TEXT("Chain supported through its dependency"), Fix.Eval(TEXT("H_Chain")), EDocHypothesisEvaluation::Supported);
	Fix.Subsystem->CommitConclusion(TEXT("C_Glove"), TEXT("H_Glove"), Fix.Subsystem->GetEvidenceRevision(), NAME_None);
	Fix.Subsystem->CommitConclusion(TEXT("C_Chain"), TEXT("H_Chain"), Fix.Subsystem->GetEvidenceRevision(), NAME_None);

	TArray<FName> Challenged;
	Fix.Subsystem->OnConclusionChallengedNative.AddLambda([&Challenged](FName Id, const FString&) { Challenged.Add(Id); });

	TestTrue(TEXT("Content revision"), Fix.Subsystem->ReviseObservation(TEXT("Obs_Glove"), TEXT("Left glove"), EDocEvidenceStatus::EstablishedFact).IsSuccess());
	TestEqual(TEXT("Still supported: nothing challenged"), Challenged.Num(), 0);
	Fix.AddObs(TEXT("Obs_Unrelated"), TEXT("Ev_Unrelated"));
	TestEqual(TEXT("Unrelated evidence does not touch these conclusions"), Challenged.Num(), 0);

	TestTrue(TEXT("Retract"), Fix.Subsystem->RetractObservation(TEXT("Obs_Glove")).IsSuccess());
	TestEqual(TEXT("Both dependent conclusions challenged"), Challenged.Num(), 2);
	TestTrue(TEXT("Conclusion history kept"), Fix.Subsystem->IsConclusionCommitted(TEXT("C_Glove")));
	TestTrue(TEXT("Marked challenged"), Fix.Subsystem->IsConclusionChallenged(TEXT("C_Glove")));
	FDocConclusionRecord Record;
	Fix.Subsystem->GetConclusion(TEXT("C_Glove"), Record);
	TestTrue(TEXT("Challenge reason recorded"), Record.ChallengeReason.Contains(TEXT("retracted")));
	TestTrue(TEXT("Supporting revision snapshot kept"), Record.SupportingObservationRevisions.Num() == 1 && Record.SupportingObservationRevisions[0] == TEXT("Obs_Glove@1"));

	TestEqual(TEXT("Retracting twice is NoChange"), Fix.Subsystem->RetractObservation(TEXT("Obs_Glove")).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("No duplicate challenge events"), Challenged.Num(), 2);

	return true;
}

// EVD-06: Doc.Evidence.StaleCommit
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceStaleCommitTest, FAutomationTestBase, "Doc.Evidence.StaleCommit", DocEvidenceTests::Flags)
bool FDocEvidenceStaleCommitTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Key"));
	Fix.MakeEvidence(TEXT("Ev_Note"));
	Fix.MakeHypothesis(TEXT("H_Key"), { TEXT("Ev_Key") });
	Fix.AddObs(TEXT("Obs_Key"), TEXT("Ev_Key"));

	FDocDeductionResult Seen;
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Key"), Seen);
	TestEqual(TEXT("Player sees support"), Seen.Evaluation, EDocHypothesisEvaluation::Supported);

	Fix.AddObs(TEXT("Obs_Note"), TEXT("Ev_Note")); // evidence changed after the player looked
	TestEqual(TEXT("Commit against the old snapshot is a Conflict"),
		Fix.Subsystem->CommitConclusion(TEXT("C_Key"), TEXT("H_Key"), Seen.SnapshotRevision, TEXT("R_Key")).Outcome, EDocResultOutcome::Conflict);
	TestFalse(TEXT("Nothing committed"), Fix.Subsystem->IsConclusionCommitted(TEXT("C_Key")));

	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Key"), Seen);
	int32 Committed = 0;
	Fix.Subsystem->OnConclusionCommittedNative.AddLambda([&Committed, &Fix](const FDocConclusionRecord& R)
	{
		++Committed;
		// The record is already committed when listeners run.
		check(Fix.Subsystem->IsConclusionCommitted(R.ConclusionId));
	});
	TestTrue(TEXT("Fresh snapshot commits"), Fix.Subsystem->CommitConclusion(TEXT("C_Key"), TEXT("H_Key"), Seen.SnapshotRevision, TEXT("R_Key")).IsChanged());
	TestEqual(TEXT("One commit event"), Committed, 1);
	TestTrue(TEXT("Receipt recorded"), Fix.Subsystem->HasReceipt(TEXT("R_Key")));
	TestEqual(TEXT("A receipt cannot be reused by another conclusion"),
		Fix.Subsystem->CommitConclusion(TEXT("C_Other"), TEXT("H_Key"), Fix.Subsystem->GetEvidenceRevision(), TEXT("R_Key")).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Unsupported hypothesis cannot be committed"),
		Fix.Subsystem->CommitConclusion(TEXT("C_Bad"), TEXT("H_Key"), Fix.Subsystem->GetEvidenceRevision(TEXT("OtherPlayer")), NAME_None, TEXT("OtherPlayer")).Outcome, EDocResultOutcome::Failed);

	return true;
}

// EVD-07: Doc.Evidence.PrivateExplanation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidencePrivateExplanationTest, FAutomationTestBase, "Doc.Evidence.PrivateExplanation", DocEvidenceTests::Flags)
bool FDocEvidencePrivateExplanationTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Public"));
	Fix.MakeEvidence(TEXT("Ev_SecretLedger"), /*bSecret*/ true);
	Fix.MakeEvidence(TEXT("Ev_Poison"));
	Fix.MakeEvidence(TEXT("Ev_GMOnly"));

	UDocHypothesisDefinition* Solution = Fix.NewHypothesis(TEXT("H_TrueCulprit"), { TEXT("Ev_Poison") });
	Solution->bIsHiddenSolution = true;
	Fix.Subsystem->RegisterHypothesisDefinition(Solution);
	Fix.MakeHypothesis(TEXT("H_Embezzle"), { TEXT("Ev_Public"), TEXT("Ev_SecretLedger") }, { TEXT("Ev_GMOnly") });

	// Hidden solution: nothing in the payload.
	Fix.AddObs(TEXT("Obs_Poison"), TEXT("Ev_Poison"));
	FDocDeductionResult R;
	TestEqual(TEXT("Hidden solution is PermissionDenied"), Fix.Subsystem->EvaluateHypothesis(TEXT("H_TrueCulprit"), R).Outcome, EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Hidden payload is empty"), R.SupportingEvidenceIds.Num() == 0 && R.Summary.IsEmpty() && R.Evaluation == EDocHypothesisEvaluation::Unavailable);
	TestEqual(TEXT("Ordinary explanation of a hidden solution is refused"), Fix.Subsystem->QueryExplanation(TEXT("H_TrueCulprit"), false, R).Outcome, EDocResultOutcome::PermissionDenied);
	TestEqual(TEXT("Asking for secrets without authorization is refused"), Fix.Subsystem->QueryExplanation(TEXT("H_TrueCulprit"), true, R).Outcome, EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Refused payload stays empty"), R.SupportingEvidenceIds.Num() == 0);
	const TArray<FDocDeductionResult> Available = Fix.Subsystem->QueryAvailableHypotheses();
	TestFalse(TEXT("Hidden solution not listed"), Available.ContainsByPredicate([](const FDocDeductionResult& X) { return X.HypothesisId == TEXT("H_TrueCulprit"); }));
	TestEqual(TEXT("Only the visible hypothesis is listed"), Available.Num(), 1);

	// Visible hypothesis requiring undiscovered secret evidence: no identities leak.
	Fix.AddObs(TEXT("Obs_Public"), TEXT("Ev_Public"));
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Embezzle"), R);
	TestEqual(TEXT("Needs more"), R.Evaluation, EDocHypothesisEvaluation::Inconclusive);
	TestEqual(TEXT("No missing identities"), R.MissingEvidenceIds.Num(), 0);
	TestTrue(TEXT("Marked redacted"), R.bRedacted);
	TestFalse(TEXT("Secret id never appears in the summary"), R.Summary.Contains(TEXT("SecretLedger")));

	// Hidden-from-player disqualifier does not affect (or reveal itself through) the player's evaluation.
	Fix.AddObs(TEXT("Obs_Ledger"), TEXT("Ev_SecretLedger"));
	Fix.AddObs(TEXT("Obs_GM"), TEXT("Ev_GMOnly"), EDocEvidenceStatus::EstablishedFact, NAME_None, /*bHidden*/ true);
	Fix.Subsystem->EvaluateHypothesis(TEXT("H_Embezzle"), R);
	TestEqual(TEXT("Player view: supported"), R.Evaluation, EDocHypothesisEvaluation::Supported);
	TestFalse(TEXT("Secret evidence id redacted from support"), R.SupportingEvidenceIds.Contains(TEXT("Ev_SecretLedger")));
	TestTrue(TEXT("Public support still shown"), R.SupportingEvidenceIds.Contains(TEXT("Ev_Public")));
	TestFalse(TEXT("GM-only disqualifier not revealed"), R.ContradictingEvidenceIds.Contains(TEXT("Ev_GMOnly")));

#if !UE_BUILD_SHIPPING
	Fix.Subsystem->bAuthorizeDebugExplanations = true;
	TestTrue(TEXT("Authorized debug explanation"), Fix.Subsystem->QueryDebugExplanation(TEXT("H_Embezzle"), R).IsSuccess());
	TestEqual(TEXT("Debug view sees the hidden disqualifier"), R.Evaluation, EDocHypothesisEvaluation::Contradicted);
	TestTrue(TEXT("Debug view lists it"), R.ContradictingEvidenceIds.Contains(TEXT("Ev_GMOnly")));
	Fix.Subsystem->bAuthorizeDebugExplanations = false;
#endif

	return true;
}

// EVD-08: Doc.Evidence.RuleCycle
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceRuleCycleTest, FAutomationTestBase, "Doc.Evidence.RuleCycle", DocEvidenceTests::Flags)
bool FDocEvidenceRuleCycleTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Root"));

	UDocHypothesisDefinition* A = Fix.NewHypothesis(TEXT("H_A"), { TEXT("Ev_Root") });
	A->DependentHypothesisIds = { TEXT("H_B") };
	TestTrue(TEXT("A registers (B not yet known)"), Fix.Subsystem->RegisterHypothesisDefinition(A).IsSuccess());
	UDocHypothesisDefinition* B = Fix.NewHypothesis(TEXT("H_B"), { TEXT("Ev_Root") });
	B->DependentHypothesisIds = { TEXT("H_A") };
	const FDocSystemResult Cycle = Fix.Subsystem->RegisterHypothesisDefinition(B);
	TestEqual(TEXT("Cycle rejected at registration"), Cycle.Outcome, EDocResultOutcome::InvalidConfiguration);
	TestTrue(TEXT("Diagnostic shows the path"), Cycle.ToString().Contains(TEXT("H_B -> H_A -> H_B")));

	UDocHypothesisDefinition* Self = Fix.NewHypothesis(TEXT("H_Self"), { TEXT("Ev_Root") });
	Self->DependentHypothesisIds = { TEXT("H_Self") };
	TestEqual(TEXT("Self dependency rejected"), Fix.Subsystem->RegisterHypothesisDefinition(Self).Outcome, EDocResultOutcome::InvalidConfiguration);

	// Depth limit.
	FName Previous = NAME_None;
	int32 Registered = 0;
	FDocSystemResult Last;
	for (int32 i = 0; i <= 16; ++i)
	{
		UDocHypothesisDefinition* Link = Fix.NewHypothesis(*FString::Printf(TEXT("H_Deep%02d"), i), i == 0 ? TArray<FName>{ TEXT("Ev_Root") } : TArray<FName>{});
		if (!Previous.IsNone())
		{
			Link->DependentHypothesisIds = { Previous };
		}
		Last = Fix.Subsystem->RegisterHypothesisDefinition(Link);
		Registered += Last.IsSuccess() ? 1 : 0;
		Previous = Link->HypothesisId;
	}
	TestEqual(TEXT("Chain of 16 accepted"), Registered, 16);
	TestEqual(TEXT("17th level rejected as too deep"), Last.Outcome, EDocResultOutcome::InvalidConfiguration);

	UDocHypothesisDefinition* Wide = Fix.NewHypothesis(TEXT("H_Wide"), { TEXT("Ev_Root") });
	for (int32 i = 0; i < UDocEvidenceSubsystem::MaxDependenciesPerHypothesis + 1; ++i)
	{
		Wide->DependentHypothesisIds.Add(*FString::Printf(TEXT("H_W%d"), i));
	}
	TestEqual(TEXT("Oversized dependency list rejected"), Fix.Subsystem->RegisterHypothesisDefinition(Wide).Outcome, EDocResultOutcome::InvalidConfiguration);

	UDocHypothesisDefinition* EmptySet = Fix.NewHypothesis(TEXT("H_Empty"), {});
	EmptySet->AlternativeRequiredEvidenceSets.AddDefaulted();
	TestEqual(TEXT("Empty evidence set rejected"), Fix.Subsystem->RegisterHypothesisDefinition(EmptySet).Outcome, EDocResultOutcome::InvalidConfiguration);

	// The accepted deep chain evaluates and terminates.
	Fix.AddObs(TEXT("Obs_Root"), TEXT("Ev_Root"));
	TestEqual(TEXT("Deepest accepted level evaluates"), Fix.Eval(TEXT("H_Deep15")), EDocHypothesisEvaluation::Supported);
	FDocDeductionResult Unknown;
	TestEqual(TEXT("Rejected level is unknown"), Fix.Subsystem->EvaluateHypothesis(TEXT("H_Deep16"), Unknown).Outcome, EDocResultOutcome::NotFound);

	return true;
}

// EVD-09: Doc.Evidence.RestoreReceipts
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceRestoreReceiptsTest, FAutomationTestBase, "Doc.Evidence.RestoreReceipts", DocEvidenceTests::Flags)
bool FDocEvidenceRestoreReceiptsTest::RunTest(const FString& Parameters)
{
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Map"));
	Fix.MakeHypothesis(TEXT("H_Map"), { TEXT("Ev_Map") });
	Fix.AddObs(TEXT("Obs_Map"), TEXT("Ev_Map"), EDocEvidenceStatus::EstablishedFact, TEXT("P1"));
	TestTrue(TEXT("Commit for P1"), Fix.Subsystem->CommitConclusion(TEXT("C_Map"), TEXT("H_Map"), Fix.Subsystem->GetEvidenceRevision(TEXT("P1")), TEXT("R_Reward"), TEXT("P1")).IsChanged());
	TestFalse(TEXT("Owner scopes are separate"), Fix.Subsystem->IsConclusionCommitted(TEXT("C_Map"), TEXT("P2")));
	TestEqual(TEXT("P2 has no evidence"), Fix.Eval(TEXT("H_Map"), TEXT("P2")), EDocHypothesisEvaluation::Inconclusive);

	const FDocEvidenceSnapshot Saved = Fix.Subsystem->CaptureState(TEXT("P1"));

	DocEvidenceTests::FFixture Loaded;
	Loaded.MakeEvidence(TEXT("Ev_Map"));
	Loaded.MakeHypothesis(TEXT("H_Map"), { TEXT("Ev_Map") });
	int32 Events = 0;
	Loaded.Subsystem->OnConclusionCommittedNative.AddLambda([&Events](const FDocConclusionRecord&) { ++Events; });
	TestTrue(TEXT("Restore"), Loaded.Subsystem->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("Restore does not award the conclusion again"), Events, 0);
	TestTrue(TEXT("History restored"), Loaded.Subsystem->IsConclusionCommitted(TEXT("C_Map"), TEXT("P1")));
	TestTrue(TEXT("Receipt restored"), Loaded.Subsystem->HasReceipt(TEXT("R_Reward"), TEXT("P1")));
	const FDocSystemResult Retry = Loaded.Subsystem->CommitConclusion(TEXT("C_Map"), TEXT("H_Map"), Loaded.Subsystem->GetEvidenceRevision(TEXT("P1")), TEXT("R_Reward"), TEXT("P1"));
	TestTrue(TEXT("Retried commit is NoChange"), Retry.IsSuccess() && !Retry.IsChanged());
	TestEqual(TEXT("Still no duplicate effect"), Events, 0);
	TestEqual(TEXT("Receipt cannot be reused"),
		Loaded.Subsystem->CommitConclusion(TEXT("C_Other"), TEXT("H_Map"), Loaded.Subsystem->GetEvidenceRevision(TEXT("P1")), TEXT("R_Reward"), TEXT("P1")).Outcome, EDocResultOutcome::Conflict);

	// Removed definition: quarantined, not active, not awarded.
	DocEvidenceTests::FFixture Migrated;
	Migrated.MakeEvidence(TEXT("Ev_Map"));
	TestTrue(TEXT("Restore without the hypothesis definition"), Migrated.Subsystem->RestoreState(Saved).IsSuccess());
	TestFalse(TEXT("Orphan conclusion is not active"), Migrated.Subsystem->IsConclusionCommitted(TEXT("C_Map"), TEXT("P1")));
	TestEqual(TEXT("Orphan conclusion kept in quarantine"), Migrated.Subsystem->CaptureState(TEXT("P1")).QuarantinedConclusions.Num(), 1);

	// Invalid snapshot refused without touching state.
	FDocEvidenceSnapshot Bad = Saved;
	FDocConclusionRecord Dup = Bad.Conclusions[TEXT("C_Map")];
	Dup.ConclusionId = TEXT("C_Dup");
	Bad.Conclusions.Add(TEXT("C_Dup"), Dup);
	TestFalse(TEXT("Two conclusions sharing a receipt refused"), Loaded.Subsystem->RestoreState(Bad).IsSuccess());
	TestTrue(TEXT("Existing state intact"), Loaded.Subsystem->IsConclusionCommitted(TEXT("C_Map"), TEXT("P1")));

	return true;
}

// EVD-10: Doc.Evidence.HeadlessIsolation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocEvidenceHeadlessIsolationTest, FAutomationTestBase, "Doc.Evidence.HeadlessIsolation", DocEvidenceTests::Flags)
bool FDocEvidenceHeadlessIsolationTest::RunTest(const FString& Parameters)
{
	// Slice without Codex, Dialogue, a board UI, or any AI model: three records, two hypotheses, one validated conclusion.
	DocEvidenceTests::FFixture Fix;
	Fix.MakeEvidence(TEXT("Ev_Receipt"));
	Fix.MakeEvidence(TEXT("Ev_Camera"));
	Fix.MakeEvidence(TEXT("Ev_Ticket"));
	Fix.MakeHypothesis(TEXT("H_Stayed"), { TEXT("Ev_Receipt"), TEXT("Ev_Camera") }, { TEXT("Ev_Ticket") });
	Fix.MakeHypothesis(TEXT("H_Left"), { TEXT("Ev_Ticket") });

	Fix.AddObs(TEXT("Obs_Receipt"), TEXT("Ev_Receipt"));
	Fix.AddObs(TEXT("Obs_Camera"), TEXT("Ev_Camera"));
	Fix.AddObs(TEXT("Obs_Ticket_Claim"), TEXT("Ev_Ticket"), EDocEvidenceStatus::Claim);

	TestEqual(TEXT("Stayed supported (ticket is only a claim)"), Fix.Eval(TEXT("H_Stayed")), EDocHypothesisEvaluation::Supported);
	TestEqual(TEXT("Left inconclusive"), Fix.Eval(TEXT("H_Left")), EDocHypothesisEvaluation::Inconclusive);
	TestTrue(TEXT("Conclusion committed"),
		Fix.Subsystem->CommitConclusion(TEXT("C_Stayed"), TEXT("H_Stayed"), Fix.Subsystem->GetEvidenceRevision(), TEXT("R_1")).IsChanged());

	Fix.Subsystem->EstablishClaim(TEXT("Obs_Ticket_Claim"), TEXT("TransitRecords"));
	TestEqual(TEXT("Established ticket contradicts Stayed"), Fix.Eval(TEXT("H_Stayed")), EDocHypothesisEvaluation::Contradicted);
	TestEqual(TEXT("... and supports Left"), Fix.Eval(TEXT("H_Left")), EDocHypothesisEvaluation::Supported);
	TestTrue(TEXT("Earlier conclusion is challenged, not erased"), Fix.Subsystem->IsConclusionChallenged(TEXT("C_Stayed")) && Fix.Subsystem->IsConclusionCommitted(TEXT("C_Stayed")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
