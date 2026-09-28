// DocInteraction automation tests (INT-01..INT-07, INT-09 base parts).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocInteractionSubsystem.h"
#include "DocInteractionComponents.h"
#include "DocInteractionProviders.h"
#include "DocInteractionRules.h"
#include "DocInteractionTestTypes.h"
#include "NativeGameplayTags.h"

namespace DocInteractionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Key, "Test.DocInteraction.HasKey");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Opened, "Test.DocInteraction.Opened");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Ping, "Test.DocInteraction.Ping");

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocInteractionSubsystem* Subsystem = nullptr;

		FFixture() { Subsystem = TW.GetSubsystem<UDocInteractionSubsystem>(); }

		/** Non-Character actor with an interactor component (INT-01). */
		UDocInteractorComponent* MakeInteractor(const FVector& Location, ADocInteractionTestActor** OutActor = nullptr)
		{
			ADocInteractionTestActor* Actor = TW.Spawn<ADocInteractionTestActor>(Location);
			UDocInteractorComponent* Interactor = NewObject<UDocInteractorComponent>(Actor);
			Interactor->bAutoDetect = false;
			Interactor->RegisterComponent();
			if (OutActor) { *OutActor = Actor; }
			return Interactor;
		}

		UDocInteractableComponent* MakeTarget(const FVector& Location, const FDocInteractionDefinition& Definition, ADocInteractionTestActor** OutActor = nullptr)
		{
			ADocInteractionTestActor* Actor = TW.Spawn<ADocInteractionTestActor>(Location);
			UDocInteractableComponent* Interactable = NewObject<UDocInteractableComponent>(Actor);
			Interactable->Definitions.Add(Definition);
			Interactable->RegisterComponent();
			if (OutActor) { *OutActor = Actor; }
			return Interactable;
		}
	};

	FDocInteractionDefinition MakeDefinition(FName Id, EDocInteractionMode Mode, UObject* Outer)
	{
		FDocInteractionDefinition Def;
		Def.DefinitionId = Id;
		Def.Mode = Mode;
		Def.MaxDistance = 500.f;
		Def.RevalidateInterval = 0.f; // revalidate every advance
		UDocInteractionAction_NotifyReceiver* Notify = NewObject<UDocInteractionAction_NotifyReceiver>(Outer);
		Notify->EventTag = TAG_Ping;
		Def.Actions.Add(Notify);
		return Def;
	}

	int32 CountTag(const ADocInteractionTestActor* Actor, FGameplayTag Tag)
	{
		int32 N = 0;
		for (const FGameplayTag& T : Actor->Received) { if (T == Tag) { ++N; } }
		return N;
	}
}

using namespace DocInteractionTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionBasicTest, "Doc.Interaction.BasicInteraction", Flags)
bool FDocInteractionBasicTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	ADocInteractionTestActor* TargetActor = nullptr;
	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector);
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0),
		MakeDefinition(TEXT("Use"), EDocInteractionMode::Instant, GetTransientPackage()), &TargetActor);

	int32 Ended = 0;
	F.Subsystem->OnSessionEndedNative.AddLambda([&Ended](const FDocInteractionSessionInfo&) { ++Ended; });

	const FDocInteractionSessionInfo Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Use"));
	TestEqual(TEXT("Instant completes"), Info.State, EDocInteractionSessionState::Completed);
	TestEqual(TEXT("Action ran once"), CountTag(TargetActor, TAG_Ping), 1);
	TestEqual(TEXT("Started + Completed phases"), CountTag(TargetActor, DocInteractionTags::Phase_Completed), 1);
	TestEqual(TEXT("Exactly one terminal notification"), Ended, 1);
	TestEqual(TEXT("No lingering sessions"), F.Subsystem->GetActiveSessionCount(), 0);

	const FDocInteractionSessionInfo Missing = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Nope"));
	TestEqual(TEXT("Unknown definition rejected"), Missing.State, EDocInteractionSessionState::Rejected);
	TestEqual(TEXT("Unknown definition is NotFound"), Missing.Result.Outcome, EDocResultOutcome::NotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionConditionTest, "Doc.Interaction.ConditionFailure", Flags)
bool FDocInteractionConditionTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	ADocInteractionTestActor* InteractorActor = nullptr;
	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector, &InteractorActor);

	FDocInteractionDefinition Def = MakeDefinition(TEXT("Unlock"), EDocInteractionMode::Instant, GetTransientPackage());
	UDocInteractionCondition_RequiredTags* NeedKey = NewObject<UDocInteractionCondition_RequiredTags>(GetTransientPackage());
	NeedKey->Subject = EDocInteractionTagSubject::Interactor;
	NeedKey->Tags.AddTag(TAG_Key);
	Def.Conditions.Add(NeedKey);
	UDocInteractionCondition_OneTime* Once = NewObject<UDocInteractionCondition_OneTime>(GetTransientPackage());
	Def.Conditions.Add(Once);
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0), Def);

	// Missing key: actionable reason, nothing executed.
	const TArray<FDocInteractionOption> Options = F.Subsystem->GetAvailableInteractions(Interactor, Target);
	TestEqual(TEXT("One option"), Options.Num(), 1);
	if (Options.Num() == 1)
	{
		TestFalse(TEXT("Unavailable without key"), Options[0].bAvailable);
		TestEqual(TEXT("Reason tag"), Options[0].FailureTag, FGameplayTag(DocInteractionTags::Error_MissingTags));
	}
	TestEqual(TEXT("Request rejected"), F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Unlock")).State, EDocInteractionSessionState::Rejected);

	// With key: succeeds once, then one-time blocks it.
	InteractorActor->Tags.AddTag(TAG_Key);
	TestEqual(TEXT("Completes with key"), F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Unlock")).State, EDocInteractionSessionState::Completed);
	const FDocInteractionSessionInfo Again = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Unlock"));
	TestEqual(TEXT("One-time blocks second use"), Again.Result.ErrorTag, FGameplayTag(DocInteractionTags::Error_AlreadyUsed));

	// Out of range.
	UDocInteractorComponent* Far = F.MakeInteractor(FVector(10000, 0, 0));
	TestEqual(TEXT("Out of range reason"), F.Subsystem->RequestInteraction(Far, Target, TEXT("Unlock")).Result.ErrorTag, FGameplayTag(DocInteractionTags::Error_OutOfRange));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionHoldTest, "Doc.Interaction.HoldInteraction", Flags)
bool FDocInteractionHoldTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	ADocInteractionTestActor* InteractorActor = nullptr;
	ADocInteractionTestActor* TargetActor = nullptr;
	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector, &InteractorActor);
	FDocInteractionDefinition Def = MakeDefinition(TEXT("Hold"), EDocInteractionMode::HoldToComplete, GetTransientPackage());
	Def.HoldDuration = 1.f;
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0), Def, &TargetActor);

	// Success path.
	FDocInteractionSessionInfo Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Hold"));
	TestEqual(TEXT("Hold running"), Info.State, EDocInteractionSessionState::Running);
	F.Subsystem->AdvanceSessions(0.5f);
	FDocInteractionSessionInfo Mid;
	TestTrue(TEXT("Session queryable"), F.Subsystem->GetSessionInfo(Info.Handle, Mid));
	TestEqual(TEXT("Half progress"), Mid.Progress, 0.5f, 0.001f);
	TestEqual(TEXT("No action before completion"), CountTag(TargetActor, TAG_Ping), 0);
	F.Subsystem->AdvanceSessions(0.6f);
	TestFalse(TEXT("Session finished"), F.Subsystem->GetSessionInfo(Info.Handle, Mid));
	TestEqual(TEXT("Action ran at commit"), CountTag(TargetActor, TAG_Ping), 1);

	// Cancellation path.
	Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Hold"));
	F.Subsystem->AdvanceSessions(0.2f);
	TestTrue(TEXT("Cancel ok"), F.Subsystem->CancelInteraction(Info.Handle).IsSuccess());
	TestEqual(TEXT("Second cancel NoChange"), F.Subsystem->CancelInteraction(Info.Handle).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Cancelled hold did not execute"), CountTag(TargetActor, TAG_Ping), 1);

	// Condition changes during hold (moved out of range) → blocked before commit.
	Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Hold"));
	InteractorActor->SetActorLocation(FVector(5000, 0, 0));
	F.Subsystem->AdvanceSessions(2.f);
	TestEqual(TEXT("Moved away: commit blocked"), CountTag(TargetActor, TAG_Ping), 1);
	InteractorActor->SetActorLocation(FVector::ZeroVector);

	// Target destroyed during hold → Failed, no action.
	EDocInteractionSessionState EndState = EDocInteractionSessionState::Running;
	Interactor->OnSessionEnded.Clear();
	FDelegateHandle H = F.Subsystem->OnSessionEndedNative.AddLambda([&EndState](const FDocInteractionSessionInfo& I) { EndState = I.State; });
	Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Hold"));
	TargetActor->Destroy();
	F.Subsystem->AdvanceSessions(2.f);
	TestEqual(TEXT("Target destroyed → Failed"), EndState, EDocInteractionSessionState::Failed);
	F.Subsystem->OnSessionEndedNative.Remove(H);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionModesTest, "Doc.Interaction.ContinuousAndRepeated", Flags)
bool FDocInteractionModesTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	ADocInteractionTestActor* TargetActor = nullptr;
	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector);

	FDocInteractionDefinition Repeat = MakeDefinition(TEXT("Crank"), EDocInteractionMode::Repeated, GetTransientPackage());
	Repeat.RepeatInterval = 0.5f;
	Repeat.MaxRepeats = 3;
	FDocInteractionDefinition Push = MakeDefinition(TEXT("Push"), EDocInteractionMode::Continuous, GetTransientPackage());
	Push.Concurrency = EDocInteractionConcurrency::Shared;
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0), Repeat, &TargetActor);
	Target->Definitions.Add(Push);

	FDocInteractionSessionInfo Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Crank"));
	TestEqual(TEXT("First repetition at start"), CountTag(TargetActor, TAG_Ping), 1);
	F.Subsystem->AdvanceSessions(1.0f);
	TestEqual(TEXT("Three repetitions total"), CountTag(TargetActor, TAG_Ping), 3);
	FDocInteractionSessionInfo Out;
	TestFalse(TEXT("Completed at MaxRepeats"), F.Subsystem->GetSessionInfo(Info.Handle, Out));

	Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Push"));
	TestEqual(TEXT("Continuous runs"), Info.State, EDocInteractionSessionState::Running);
	F.Subsystem->AdvanceSessions(5.f);
	TestTrue(TEXT("Still running"), F.Subsystem->GetSessionInfo(Info.Handle, Out));
	TestTrue(TEXT("Complete on release"), F.Subsystem->CompleteInteraction(Info.Handle).IsSuccess());
	TestEqual(TEXT("Continuous executed actions once"), CountTag(TargetActor, TAG_Ping), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionConcurrencyTest, "Doc.Interaction.Concurrency", Flags)
bool FDocInteractionConcurrencyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	UDocInteractorComponent* A = F.MakeInteractor(FVector(0, 0, 0));
	UDocInteractorComponent* B = F.MakeInteractor(FVector(0, 50, 0));
	FDocInteractionDefinition Def = MakeDefinition(TEXT("Lever"), EDocInteractionMode::HoldToComplete, GetTransientPackage());
	Def.HoldDuration = 1.f;
	Def.Concurrency = EDocInteractionConcurrency::ExclusiveTarget;
	Def.Conditions.Add(NewObject<UDocInteractionCondition_OneTime>(GetTransientPackage()));
	ADocInteractionTestActor* TargetActor = nullptr;
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0), Def, &TargetActor);

	const FDocInteractionSessionInfo First = F.Subsystem->RequestInteraction(A, Target, TEXT("Lever"));
	const FDocInteractionSessionInfo Second = F.Subsystem->RequestInteraction(B, Target, TEXT("Lever"));
	TestEqual(TEXT("First reserves"), First.State, EDocInteractionSessionState::Running);
	TestEqual(TEXT("Second conflicts"), Second.Result.Outcome, EDocResultOutcome::Conflict);

	const FDocInteractionSessionInfo SameInteractor = F.Subsystem->RequestInteraction(A, Target, TEXT("Lever"));
	TestEqual(TEXT("One session per interactor"), SameInteractor.Result.Outcome, EDocResultOutcome::Conflict);

	F.Subsystem->AdvanceSessions(1.1f);
	TestEqual(TEXT("Exclusive one-time committed once"), CountTag(TargetActor, TAG_Ping), 1);
	TestEqual(TEXT("Later request blocked by one-time"),
		F.Subsystem->RequestInteraction(B, Target, TEXT("Lever")).Result.ErrorTag, FGameplayTag(DocInteractionTags::Error_AlreadyUsed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionPartialFailureTest, "Doc.Interaction.PartialFailure", Flags)
bool FDocInteractionPartialFailureTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	ADocInteractionTestActor* TargetActor = nullptr;
	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector);

	FDocInteractionDefinition Def;
	Def.DefinitionId = TEXT("Open");
	Def.MaxDistance = 500.f;
	UDocInteractionAction_TagDelta* Tag = NewObject<UDocInteractionAction_TagDelta>(GetTransientPackage());
	Tag->ActionId = TEXT("MarkOpened");
	Tag->TagsToAdd.AddTag(TAG_Opened);
	Def.Actions.Add(Tag);
	UDocInteractionTestFailAction* Fail = NewObject<UDocInteractionTestFailAction>(GetTransientPackage());
	Fail->ActionId = TEXT("Explode");
	Def.Actions.Add(Fail);
	UDocInteractableComponent* Target = F.MakeTarget(FVector(100, 0, 0), Def, &TargetActor);

	const FDocInteractionSessionInfo Info = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Open"));
	TestEqual(TEXT("Failed"), Info.State, EDocInteractionSessionState::Failed);
	TestEqual(TEXT("Executed list reports the first action"), Info.ExecutedActions.Num(), 1);
	TestEqual(TEXT("Compensated the reversible action"), Info.CompensatedActions.Num(), 1);
	TestFalse(TEXT("Tag change rolled back by declared compensation"), TargetActor->Tags.HasTagExact(TAG_Opened));

	// Shared action instances do not leak state between sessions: run twice, same outcome.
	const FDocInteractionSessionInfo Again = F.Subsystem->RequestInteraction(Interactor, Target, TEXT("Open"));
	TestEqual(TEXT("Same outcome on repeat"), Again.ExecutedActions.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInteractionCandidatesTest, "Doc.Interaction.CandidateSelection", Flags)
bool FDocInteractionCandidatesTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Subsystem)) { return false; }

	UDocInteractorComponent* Interactor = F.MakeInteractor(FVector::ZeroVector);
	UDocInteractionProvider_Proximity* Prox = NewObject<UDocInteractionProvider_Proximity>(Interactor);
	Prox->Range = 400.f;
	UDocInteractionProvider_ExplicitTarget* Explicit = NewObject<UDocInteractionProvider_ExplicitTarget>(Interactor);
	Interactor->Providers = { Prox, Explicit };

	ADocInteractionTestActor* Near = nullptr;
	ADocInteractionTestActor* FarActor = nullptr;
	F.MakeTarget(FVector(100, 0, 0), MakeDefinition(TEXT("Use"), EDocInteractionMode::Instant, GetTransientPackage()), &Near);
	F.MakeTarget(FVector(300, 0, 0), MakeDefinition(TEXT("Use"), EDocInteractionMode::Instant, GetTransientPackage()), &FarActor);
	Interactor->ExplicitTarget = Near;

	const TArray<FDocInteractionCandidate> Candidates = Interactor->QueryCandidates();
	TestEqual(TEXT("Two unique candidates (overlapping providers merged)"), Candidates.Num(), 2);
	if (Candidates.Num() == 2)
	{
		TestEqual(TEXT("Explicit/near candidate first"), Candidates[0].Actor.Get(), static_cast<AActor*>(Near));
	}

	// Determinism: same query twice gives same order.
	const TArray<FDocInteractionCandidate> Again = Interactor->QueryCandidates();
	TestTrue(TEXT("Stable order"), Again.Num() == Candidates.Num() && (Again.Num() == 0 || Again[0].Actor == Candidates[0].Actor));

	Interactor->RefreshDetection();
	TestEqual(TEXT("Focus is best candidate"), Interactor->GetFocusedActor(), static_cast<AActor*>(Near));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
