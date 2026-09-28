#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocOpticalBeamSubsystem.h"
#include "DocBeamEmitterComponent.h"
#include "DocBeamSurfaceComponent.h"
#include "DocBeamReceiverComponent.h"
#include "DocBeamPresentationComponent.h"
#include "DocOpticalProfile.h"
#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocOpticalTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Channel_Red, "Optics.Channel.Red");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Channel_Blue, "Optics.Channel.Blue");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Channel_Green, "Optics.Channel.Green");

	UDocOpticalProfile* CreateMirrorProfile(FName Id = TEXT("Profile_Mirror"), float Reflectivity = 0.95f)
	{
		UDocOpticalProfile* Prof = NewObject<UDocOpticalProfile>(GetTransientPackage());
		Prof->ProfileId = Id;
		Prof->SurfaceType = EDocBeamSurfaceType::Mirror;
		Prof->Reflectivity = Reflectivity;
		return Prof;
	}

	UDocOpticalProfile* CreateFilterProfile(FName Id, const FGameplayTag& PassChannel, float Transmission = 0.9f)
	{
		UDocOpticalProfile* Prof = NewObject<UDocOpticalProfile>(GetTransientPackage());
		Prof->ProfileId = Id;
		Prof->SurfaceType = EDocBeamSurfaceType::Filter;
		Prof->TransmissionEfficiency = Transmission;
		if (PassChannel.IsValid())
		{
			Prof->FilterChannels.AddTag(PassChannel);
		}
		return Prof;
	}

	UDocBeamEmitterComponent* MakeEmitter(FDocScopedTestWorld& TW, FName Id, float Intensity = 1.0f, FGameplayTag Channel = FGameplayTag(), float Range = 10000.0f)
	{
		AActor* Actor = TW.Spawn<AActor>();
		UDocBeamEmitterComponent* E = NewObject<UDocBeamEmitterComponent>(Actor);
		E->EmitterId = Id;
		E->LocalDirection = FVector(1.0f, 0.0f, 0.0f);
		E->InitialIntensity = Intensity;
		E->BeamChannel = Channel;
		E->MaxRange = Range;
		E->RegisterComponent();
		return E;
	}

	UDocBeamSurfaceComponent* MakeSurface(FDocScopedTestWorld& TW, FName Id, UDocOpticalProfile* Profile)
	{
		AActor* Actor = TW.Spawn<AActor>();
		UDocBeamSurfaceComponent* S = NewObject<UDocBeamSurfaceComponent>(Actor);
		S->SurfaceId = Id;
		S->OpticalProfile = Profile;
		S->RegisterComponent();
		return S;
	}

	UDocBeamReceiverComponent* MakeReceiver(FDocScopedTestWorld& TW, FName Id, float MinIntensity, float Dwell, float Release)
	{
		AActor* Actor = TW.Spawn<AActor>();
		UDocBeamReceiverComponent* R = NewObject<UDocBeamReceiverComponent>(Actor);
		R->ReceiverId = Id;
		R->MinRequiredIntensity = MinIntensity;
		R->RequiredDwellTimeSeconds = Dwell;
		R->ReleaseHysteresisSeconds = Release;
		R->RegisterComponent();
		return R;
	}
}

// OPT-01: Doc.Optics.ReflectionMath
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsReflectionMathTest, FAutomationTestBase, "Doc.Optics.ReflectionMath", DocOpticalTests::Flags)
bool FDocOpticsReflectionMathTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Direct normal bounce reflects 180 deg"),
		DocOpticalMath::ComputeReflection(FVector(1, 0, 0), FVector(-1, 0, 0)).Equals(FVector(-1, 0, 0), 1e-4f));
	TestTrue(TEXT("45 degree reflection"),
		DocOpticalMath::ComputeReflection(FVector(1, 1, 0).GetSafeNormal(), FVector(0, -1, 0)).Equals(FVector(1, -1, 0).GetSafeNormal(), 1e-4f));
	TestTrue(TEXT("Parallel surface keeps direction"),
		DocOpticalMath::ComputeReflection(FVector(1, 0, 0), FVector(0, 1, 0)).Equals(FVector(1, 0, 0), 1e-4f));

	// Analytic 3D fixture: d=(1,1,1)/sqrt3, n=(0,0,1): r=(1,1,-1)/sqrt3.
	TestTrue(TEXT("3D analytic fixture"),
		DocOpticalMath::ComputeReflection(FVector(1, 1, 1), FVector(0, 0, 1)).Equals(FVector(1, 1, -1).GetSafeNormal(), 1e-4f));
	// Inputs are normalized first; the normal's sign does not matter.
	TestTrue(TEXT("Unnormalized inputs"),
		DocOpticalMath::ComputeReflection(FVector(10, 0, 0), FVector(-3, 0, 0)).Equals(FVector(-1, 0, 0), 1e-4f));
	TestTrue(TEXT("Normal sign invariance"),
		DocOpticalMath::ComputeReflection(FVector(1, 1, 0), FVector(0, 1, 0)).Equals(DocOpticalMath::ComputeReflection(FVector(1, 1, 0), FVector(0, -1, 0)), 1e-5f));
	TestTrue(TEXT("Result is unit length"),
		FMath::IsNearlyEqual(DocOpticalMath::ComputeReflection(FVector(0.3, -2, 5), FVector(1, 2, 3)).Size(), 1.0, 1e-4));

	TestTrue(TEXT("Zero direction returns zero"), DocOpticalMath::ComputeReflection(FVector::ZeroVector, FVector::UpVector).IsNearlyZero());
	TestTrue(TEXT("Zero normal returns zero"), DocOpticalMath::ComputeReflection(FVector::ForwardVector, FVector::ZeroVector).IsNearlyZero());
	TestTrue(TEXT("NaN returns zero"), DocOpticalMath::ComputeReflection(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector::UpVector).IsNearlyZero());

	// One-sided mirror: a beam from behind is blocked instead of reflected.
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();
	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("E"));
	UDocBeamSurfaceComponent* Mirror = DocOpticalTests::MakeSurface(TW, TEXT("OneSided"), DocOpticalTests::CreateMirrorProfile());
	Mirror->bTwoSided = false;
	Mirror->SurfaceNormalOverride = FVector(1, 0, 0); // front faces +X; the beam travels +X, i.e. arrives from behind
	Subsystem->AddProgrammaticPlane(TEXT("Plane"), FVector(500, 0, 0), FVector(-1, 0, 0), Mirror);
	FDocBeamPath Path;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Back face of one-sided mirror blocks"), Path.TerminationReason, EDocBeamTerminationReason::HitBlocker);
	TestEqual(TEXT("No reflection counted"), Path.ReflectionCount, 0);

	return true;
}

// OPT-02: Doc.Optics.FilterLoss
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsFilterLossTest, FAutomationTestBase, "Doc.Optics.FilterLoss", DocOpticalTests::Flags)
bool FDocOpticsFilterLossTest::RunTest(const FString& Parameters)
{
	UDocOpticalProfile* BadProfile = DocOpticalTests::CreateMirrorProfile(TEXT("Bad"), 1.5f);
	TestEqual(TEXT("Profile exceeding 1.0 reflectivity fails"), BadProfile->ValidateProfile().Outcome, EDocResultOutcome::InvalidConfiguration);
	UDocOpticalProfile* Prism = DocOpticalTests::CreateMirrorProfile(TEXT("Prism"));
	Prism->SurfaceType = EDocBeamSurfaceType::Prism;
	TestEqual(TEXT("Prism is an explicit future capability"), Prism->ValidateProfile().Outcome, EDocResultOutcome::Unsupported);

	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Red"), 1.0f, DocOpticalTests::TAG_Channel_Red, 2000.0f);
	UDocBeamSurfaceComponent* Filter1 = DocOpticalTests::MakeSurface(TW, TEXT("Filter_Red"),
		DocOpticalTests::CreateFilterProfile(TEXT("Prof_Red"), DocOpticalTests::TAG_Channel_Red, 0.8f));
	UDocBeamSurfaceComponent* Filter2 = DocOpticalTests::MakeSurface(TW, TEXT("Filter_Blue"),
		DocOpticalTests::CreateFilterProfile(TEXT("Prof_Blue"), DocOpticalTests::TAG_Channel_Blue, 0.8f));
	Subsystem->AddProgrammaticPlane(TEXT("Plane_Red"), FVector(500, 0, 0), FVector(-1, 0, 0), Filter1);
	Subsystem->AddProgrammaticPlane(TEXT("Plane_Blue"), FVector(1000, 0, 0), FVector(-1, 0, 0), Filter2);

	FDocBeamPath Path;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Path has 2 segments"), Path.Segments.Num(), 2);
	TestTrue(TEXT("Accepted channel loses intensity (1.0 -> 0.8)"), Path.Segments.Num() == 2 && FMath::IsNearlyEqual(Path.Segments[0].FinalIntensity, 0.8f, 1e-3f));
	TestEqual(TEXT("Rejected channel stops at the blue filter"), Path.TerminationReason, EDocBeamTerminationReason::FilteredOut);
	bool bNeverIncreases = true;
	for (const FDocBeamSegment& S : Path.Segments)
	{
		bNeverIncreases &= S.FinalIntensity <= S.InitialIntensity + 1e-6f;
	}
	TestTrue(TEXT("No segment gains intensity"), bNeverIncreases);

	// Channel relabel: a pass-all filter that shifts to Green; intensity still only decreases.
	UDocOpticalProfile* Shift = DocOpticalTests::CreateFilterProfile(TEXT("Shift"), FGameplayTag(), 0.5f);
	Shift->ShiftedOutputChannel = DocOpticalTests::TAG_Channel_Green;
	TestTrue(TEXT("Swap blue filter for a shifting filter"), Subsystem->SetSurfaceProfile(TEXT("Filter_Blue"), Shift).IsSuccess());
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Beam passes both filters"), Path.Segments.Num(), 3);
	TestTrue(TEXT("Channel relabeled to green after the shifting filter"), Path.Segments.Num() == 3 && Path.Segments[2].ChannelTag == DocOpticalTests::TAG_Channel_Green);
	TestTrue(TEXT("0.8 * 0.5 = 0.4"), Path.Segments.Num() == 3 && FMath::IsNearlyEqual(Path.Segments[2].InitialIntensity, 0.4f, 1e-3f));

	// An invalid profile can never amplify: it is refused by the API and treated as a blocker if forced in.
	TestFalse(TEXT("Amplifying profile refused"), Subsystem->SetSurfaceProfile(TEXT("Filter_Red"), DocOpticalTests::CreateFilterProfile(TEXT("Amp"), FGameplayTag(), 1.5f)).IsSuccess());
	Filter1->OpticalProfile->TransmissionEfficiency = 1.5f; // bypassing the API
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Forced invalid filter acts as a blocker"), Path.TerminationReason, EDocBeamTerminationReason::HitBlocker);
	TestEqual(TEXT("Beam stops at the first filter"), Path.Segments.Num(), 1);

	return true;
}

// OPT-03: Doc.Optics.SelfHitAndRange
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsSelfHitAndRangeTest, FAutomationTestBase, "Doc.Optics.SelfHitAndRange", DocOpticalTests::Flags)
bool FDocOpticsSelfHitAndRangeTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Range"), 1.0f, FGameplayTag(), 1000.0f);
	Emitter->TraceEpsilon = 5.0f;
	UDocBeamSurfaceComponent* Mirror = DocOpticalTests::MakeSurface(TW, TEXT("Mirror_45"), DocOpticalTests::CreateMirrorProfile());
	Mirror->SurfaceNormalOverride = FVector(-1, 1, 0).GetSafeNormal();
	Subsystem->AddProgrammaticPlane(TEXT("Plane_Mirror"), FVector(400, 0, 0), FVector(-1, 1, 0).GetSafeNormal(), Mirror);

	FDocBeamPath Path;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Path reflected once"), Path.ReflectionCount, 1);
	TestEqual(TEXT("Path has 2 segments"), Path.Segments.Num(), 2);
	if (Path.Segments.Num() == 2)
	{
		TestTrue(TEXT("Second segment starts epsilon along the reflected direction"), Path.Segments[1].StartPoint.Equals(FVector(400, 5, 0), 1e-2f));
		TestTrue(TEXT("Reflected along +Y"), Path.Segments[1].Direction.Equals(FVector(0, 1, 0), 1e-4f));
		const float Traced = Path.Segments[0].Length + Path.Segments[1].Length;
		TestTrue(TEXT("Epsilon is charged to the range ledger (400 + 5 + 595)"), FMath::IsNearlyEqual(Traced + 5.0f, 1000.0f, 1e-2f));
	}
	TestTrue(TEXT("Total length never exceeds MaxRange"), Path.TotalLength <= Emitter->MaxRange + 1e-3f);
	TestEqual(TEXT("Ends out of range"), Path.TerminationReason, EDocBeamTerminationReason::OutOfRange);

	// Epsilon larger than the remaining range is clipped; nothing is traced beyond the range.
	Emitter->MaxRange = 402.0f;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestTrue(TEXT("Clipped at range"), FMath::IsNearlyEqual(Path.TotalLength, 402.0f, 1e-2f));
	TestEqual(TEXT("No segment after the range is exhausted"), Path.Segments.Num(), 1);
	TestEqual(TEXT("Out of range after the mirror"), Path.TerminationReason, EDocBeamTerminationReason::OutOfRange);

	// The emitter's owner is ignored only for the first segment (flag documented), so invalid vectors disable instead of tracing.
	TestFalse(TEXT("Zero direction refused"), Subsystem->UpdateEmitterPose(TEXT("Emitter_Range"), FVector::ZeroVector, FVector::ZeroVector).IsSuccess());
	Emitter->LocalDirection = FVector::ZeroVector; // forced
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Invalid direction yields a Disabled path"), Path.TerminationReason, EDocBeamTerminationReason::Disabled);
	TestEqual(TEXT("No segments for an invalid emitter"), Path.Segments.Num(), 0);

	return true;
}

// OPT-04: Doc.Optics.LoopTermination
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsLoopTerminationTest, FAutomationTestBase, "Doc.Optics.LoopTermination", DocOpticalTests::Flags)
bool FDocOpticsLoopTerminationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Loop"), 1.0f, FGameplayTag(), 20000.0f);
	Emitter->MaxReflections = 32;
	UDocBeamSurfaceComponent* Mirror1 = DocOpticalTests::MakeSurface(TW, TEXT("M1"), DocOpticalTests::CreateMirrorProfile(TEXT("P1"), 1.0f));
	UDocBeamSurfaceComponent* Mirror2 = DocOpticalTests::MakeSurface(TW, TEXT("M2"), DocOpticalTests::CreateMirrorProfile(TEXT("P2"), 1.0f));
	Subsystem->AddProgrammaticPlane(TEXT("P_M2"), FVector(500, 0, 0), FVector(-1, 0, 0), Mirror2);
	Subsystem->AddProgrammaticPlane(TEXT("P_M1"), FVector(100, 0, 0), FVector(1, 0, 0), Mirror1);

	FDocBeamPath Path;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Lossless mirror loop is detected by the visited guard"), Path.TerminationReason, EDocBeamTerminationReason::LoopDetected);
	TestTrue(TEXT("Loop stopped after one round trip"), Path.ReflectionCount <= 2);

	// Guard disabled in effect (different channel keys are impossible here), so the hard budgets must still stop it.
	Emitter->MaxSegments = 1;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Segment budget terminates"), Path.TerminationReason, EDocBeamTerminationReason::TerminatedByBudget);
	TestTrue(TEXT("Budget respected"), Path.Segments.Num() <= 1);

	Emitter->MaxSegments = 64;
	Emitter->MaxReflections = 1;
	Subsystem->SolveEmitterPath(Emitter, Path);
	TestEqual(TEXT("Reflection budget terminates, reported separately"), Path.TerminationReason, EDocBeamTerminationReason::TerminatedByBudget);
	TestEqual(TEXT("Exactly the budgeted reflections"), Path.ReflectionCount, 1);

	// A receiver inside a loop is satisfied once per path, never re-triggered by repeated visits.
	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Trap"), 0.1f, 0.0f, 0.0f);
	int32 Activations = 0;
	Receiver->OnReceiverActivatedNative.AddLambda([&Activations](const FDocReceiverState&) { ++Activations; });
	Subsystem->AddProgrammaticPlane(TEXT("P_Trap"), FVector(300, 0, 0), FVector(1, 0, 0), nullptr, Receiver); // hit on the way back
	Emitter->MaxReflections = 32;
	Subsystem->SolveEmitterPath(Emitter, Path);
	for (int32 i = 0; i < 5; ++i)
	{
		Subsystem->SolveEmitterPath(Emitter, Path);
		Subsystem->AdvanceSimulation(0.1f);
	}
	TestEqual(TEXT("Path ends at the receiver"), Path.TerminationReason, EDocBeamTerminationReason::HitReceiver);
	TestEqual(TEXT("One contribution, not one per visit"), Receiver->GetReceiverState().Contributions.Num(), 1);
	TestEqual(TEXT("Activated exactly once"), Activations, 1);

	return true;
}

// OPT-05: Doc.Optics.ReceiverContributions
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsReceiverContributionsTest, FAutomationTestBase, "Doc.Optics.ReceiverContributions", DocOpticalTests::Flags)
bool FDocOpticsReceiverContributionsTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Rec_Target"), 0.8f, 0.0f, 0.0f);
	Receiver->AggregationMode = EDocBeamReceiverAggregationMode::SumIntensity;
	Subsystem->AddProgrammaticPlane(TEXT("P_Rec"), FVector(1000, 0, 0), FVector(-1, 0, 0), nullptr, Receiver);

	UDocBeamEmitterComponent* E1 = DocOpticalTests::MakeEmitter(TW, TEXT("E1"), 0.5f, DocOpticalTests::TAG_Channel_Red);
	UDocBeamEmitterComponent* E2 = DocOpticalTests::MakeEmitter(TW, TEXT("E2"), 0.5f, DocOpticalTests::TAG_Channel_Blue);

	FDocBeamPath P1, P2;
	Subsystem->SolveEmitterPath(E1, P1);
	Subsystem->SolveEmitterPath(E2, P2);

	FDocReceiverState State;
	Subsystem->QueryReceiver(TEXT("Rec_Target"), State);
	TestEqual(TEXT("Receiver has 2 emitter contributions"), State.EmitterContributions.Num(), 2);
	TestTrue(TEXT("Sum intensity is 1.0"), FMath::IsNearlyEqual(State.CurrentIntensity, 1.0f, 1e-3f));
	TestTrue(TEXT("Receiver is eligible"), Receiver->EvaluateEligibility());

	// Re-solving replaces, never adds.
	for (int32 i = 0; i < 3; ++i)
	{
		Subsystem->SolveEmitterPath(E2, P2);
	}
	Subsystem->QueryReceiver(TEXT("Rec_Target"), State);
	TestEqual(TEXT("Still 2 contributions after repeated solves"), State.Contributions.Num(), 2);
	TestTrue(TEXT("Still 1.0 (no double counting)"), FMath::IsNearlyEqual(State.CurrentIntensity, 1.0f, 1e-3f));

	// Evidence carries generation and time.
	TestTrue(TEXT("Contribution evidence is stamped"), State.Contributions.Num() == 2 && State.Contributions[1].EmitterId == TEXT("E2") && State.Contributions[1].PathGeneration == E2->PathGeneration);

	// All-required-channels semantics.
	Receiver->AggregationMode = EDocBeamReceiverAggregationMode::AllRequiredChannels;
	Receiver->MinRequiredIntensity = 0.4f;
	Receiver->AcceptedChannels.AddTag(DocOpticalTests::TAG_Channel_Red);
	Receiver->AcceptedChannels.AddTag(DocOpticalTests::TAG_Channel_Blue);
	TestTrue(TEXT("Red and blue both present"), Receiver->EvaluateEligibility());

	// Disabling one emitter removes only its contribution once its path is re-solved.
	Subsystem->SetEmitterEnabled(TEXT("E2"), false);
	Subsystem->RequestPathRefresh(TEXT("E2"));
	Subsystem->QueryReceiver(TEXT("Rec_Target"), State);
	TestEqual(TEXT("Only E1 remains"), State.Contributions.Num(), 1);
	TestFalse(TEXT("Blue missing: all-required fails"), Receiver->EvaluateEligibility());

	// Unregistering removes only that emitter.
	Subsystem->SetEmitterEnabled(TEXT("E2"), true);
	Subsystem->RequestPathRefresh(TEXT("E2"));
	Subsystem->UnregisterEmitter(E1);
	Subsystem->QueryReceiver(TEXT("Rec_Target"), State);
	TestEqual(TEXT("E1 removed, E2 back"), State.Contributions.Num(), 1);
	TestTrue(TEXT("Remaining contribution is E2"), State.Contributions.Num() == 1 && State.Contributions[0].EmitterId == TEXT("E2"));

	// Duplicate ids are refused.
	UDocBeamEmitterComponent* Clash = DocOpticalTests::MakeEmitter(TW, TEXT("E2"));
	TestFalse(TEXT("Duplicate emitter id refused"), Clash->LastRegistrationError.IsEmpty());

	return true;
}

// OPT-06: Doc.Optics.SustainedActivation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsSustainedActivationTest, FAutomationTestBase, "Doc.Optics.SustainedActivation", DocOpticalTests::Flags)
bool FDocOpticsSustainedActivationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Rec_Dwell"), 0.5f, 0.4f, 0.2f);

	Receiver->UpdateContribution(TEXT("TestEmitter"), 1.0f, FGameplayTag());
	Receiver->AdvanceDwell(0.2f);
	TestFalse(TEXT("Not activated after 0.2s (< 0.4s)"), Receiver->IsActivated());
	TestEqual(TEXT("State is Dwelling"), Receiver->GetReceiverState().State, EDocBeamReceiverState::Dwelling);

	// A dip resets continuous dwell.
	Receiver->RemoveContribution(TEXT("TestEmitter"));
	Receiver->AdvanceDwell(0.05f);
	TestEqual(TEXT("Dwell reset by the dip"), Receiver->GetReceiverState().DwellTimeSeconds, 0.0f);
	Receiver->UpdateContribution(TEXT("TestEmitter"), 1.0f, FGameplayTag());
	Receiver->AdvanceDwell(0.3f);
	TestFalse(TEXT("0.2 + 0.3 split by a dip does not activate"), Receiver->IsActivated());

	int32 Activations = 0, Deactivations = 0;
	Receiver->OnReceiverActivatedNative.AddLambda([&Activations](const FDocReceiverState&) { ++Activations; });
	Receiver->OnReceiverDeactivatedNative.AddLambda([&Deactivations](const FDocReceiverState&) { ++Deactivations; });

	Receiver->AdvanceDwell(0.1f); // 0.4 continuous: exactly at the boundary
	TestTrue(TEXT("Activates at exactly the dwell boundary"), Receiver->IsActivated());

	Receiver->ClearAllContributions();
	Receiver->AdvanceDwell(0.1f);
	TestTrue(TEXT("Still activated in hysteresis"), Receiver->IsActivated());
	TestEqual(TEXT("State is Hysteresis"), Receiver->GetReceiverState().State, EDocBeamReceiverState::Hysteresis);

	// Regaining the beam inside hysteresis keeps activation without a new event.
	Receiver->UpdateContribution(TEXT("TestEmitter"), 1.0f, FGameplayTag());
	Receiver->AdvanceDwell(0.05f);
	TestEqual(TEXT("Back to Activated"), Receiver->GetReceiverState().State, EDocBeamReceiverState::Activated);

	Receiver->ClearAllContributions();
	Receiver->AdvanceDwell(0.1f);
	Receiver->AdvanceDwell(0.1f); // hysteresis 0.2 fully used
	TestFalse(TEXT("Deactivated after hysteresis expired"), Receiver->IsActivated());
	TestEqual(TEXT("One activation"), Activations, 1);
	TestEqual(TEXT("One deactivation"), Deactivations, 1);

	// Through the subsystem clock with a real beam.
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();
	Subsystem->AddProgrammaticPlane(TEXT("P"), FVector(300, 0, 0), FVector(-1, 0, 0), nullptr, Receiver);
	DocOpticalTests::MakeEmitter(TW, TEXT("Beam"));
	Subsystem->AdvanceSimulation(0.1f); // solves the queued emitter, then dwell 0.1
	TestFalse(TEXT("Clock: dwell not yet complete"), Receiver->IsActivated());
	Subsystem->AdvanceSimulation(0.3f);
	TestTrue(TEXT("Clock: activates after 0.4s of simulation time"), Receiver->IsActivated());

	return true;
}

// OPT-07: Doc.Optics.StaleTrace
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsStaleTraceTest, FAutomationTestBase, "Doc.Optics.StaleTrace", DocOpticalTests::Flags)
bool FDocOpticsStaleTraceTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Rec"), 0.5f, 0.0f, 0.0f);
	UDocBeamSurfaceComponent* Mirror = DocOpticalTests::MakeSurface(TW, TEXT("Mirror"), DocOpticalTests::CreateMirrorProfile(TEXT("M"), 1.0f));
	Subsystem->AddProgrammaticPlane(TEXT("P_Rec"), FVector(800, 0, 0), FVector(-1, 0, 0), nullptr, Receiver);
	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Gen"));

	FDocBeamPath Current;
	Subsystem->SolveEmitterPath(Emitter, Current);
	TestEqual(TEXT("Beam reaches the receiver"), Current.TerminationReason, EDocBeamTerminationReason::HitReceiver);

	// A trace started before a mirror edit is discarded at commit.
	FDocBeamPathRequest Request;
	Subsystem->CreatePathRequest(TEXT("Emitter_Gen"), Request);
	FDocBeamPath Late;
	Subsystem->TracePath(Request, Late);
	Subsystem->NotifySurfaceMoved(TEXT("Mirror"));
	TestEqual(TEXT("Late result after a surface move is a Conflict"), Subsystem->CommitPath(Request, Late).Outcome, EDocResultOutcome::Conflict);

	// ... and one started before an emitter edit.
	Subsystem->CreatePathRequest(TEXT("Emitter_Gen"), Request);
	Subsystem->TracePath(Request, Late);
	Subsystem->UpdateEmitterPose(TEXT("Emitter_Gen"), FVector::ZeroVector, FVector(0, 1, 0));
	TestEqual(TEXT("Late result after an emitter edit is a Conflict"), Subsystem->CommitPath(Request, Late).Outcome, EDocResultOutcome::Conflict);
	FDocBeamPath Cached;
	Subsystem->QueryPath(TEXT("Emitter_Gen"), Cached);
	TestEqual(TEXT("Cached path unchanged by discarded results"), Cached.PathGeneration, Current.PathGeneration);

	// A fresh ticket commits.
	Subsystem->UpdateEmitterPose(TEXT("Emitter_Gen"), FVector::ZeroVector, FVector(1, 0, 0));
	Subsystem->CreatePathRequest(TEXT("Emitter_Gen"), Request);
	Subsystem->TracePath(Request, Late);
	TestTrue(TEXT("Current ticket commits"), Subsystem->CommitPath(Request, Late).IsSuccess());
	Subsystem->QueryPath(TEXT("Emitter_Gen"), Cached);
	TestEqual(TEXT("Cached path is the newest generation"), Cached.PathGeneration, Emitter->PathGeneration);

	// Queued invalidation with a maximum-staleness policy.
	Subsystem->MaxSolvesPerTick = 0;
	Subsystem->MaxPathStalenessSeconds = 0.25f;
	Subsystem->AdvanceSimulation(0.01f);
	TestTrue(TEXT("Receiver active from the committed path"), Receiver->IsActivated());
	Subsystem->NotifySurfaceMoved(TEXT("Mirror"));
	TestTrue(TEXT("Surface move queues the path"), Subsystem->IsPathPending(TEXT("Emitter_Gen")));
	Subsystem->AdvanceSimulation(0.1f);
	TestTrue(TEXT("Within the staleness limit the old path still counts"), Receiver->IsActivated());
	Subsystem->AdvanceSimulation(0.2f);
	TestFalse(TEXT("Beyond the limit a stale path cannot keep gameplay activation"), Receiver->IsActivated());
	TestTrue(TEXT("Contribution is held, not deleted"), Receiver->GetReceiverState().Contributions.Num() == 1 && Receiver->GetReceiverState().Contributions[0].bHeldStale);
	TestTrue(TEXT("Path age is exposed"), Subsystem->GetPathAgeSeconds(TEXT("Emitter_Gen")) > 0.25);

	Subsystem->MaxSolvesPerTick = 8;
	Subsystem->AdvanceSimulation(0.01f);
	TestFalse(TEXT("Re-solved"), Subsystem->IsPathPending(TEXT("Emitter_Gen")));
	TestTrue(TEXT("Fresh path grants activation again"), Receiver->IsActivated());

	// Destroying the emitter: a ticket from before is discarded.
	Subsystem->CreatePathRequest(TEXT("Emitter_Gen"), Request);
	Subsystem->TracePath(Request, Late);
	Emitter->GetOwner()->Destroy();
	TestEqual(TEXT("Late result for a destroyed emitter is NotFound"), Subsystem->CommitPath(Request, Late).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Destroyed emitter's contribution removed"), Receiver->GetReceiverState().Contributions.Num(), 0);
	TestTrue(TEXT("Mirror still registered"), Mirror != nullptr);

	return true;
}

// OPT-08: Doc.Optics.RestoreRecompute
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsRestoreRecomputeTest, FAutomationTestBase, "Doc.Optics.RestoreRecompute", DocOpticalTests::Flags)
bool FDocOpticsRestoreRecomputeTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Rec_Restore"), 0.5f, 0.3f, 0.0f);
	UDocBeamReceiverComponent* Latch = DocOpticalTests::MakeReceiver(TW, TEXT("Rec_Latch"), 0.5f, 0.0f, 0.0f);
	Latch->bLatchOnActivate = true;
	Subsystem->AddProgrammaticPlane(TEXT("P_Rec"), FVector(500, 0, 0), FVector(-1, 0, 0), nullptr, Receiver);
	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Restore"));

	Subsystem->AdvanceSimulation(0.2f);
	TestFalse(TEXT("Dwell in progress"), Receiver->IsActivated());
	Subsystem->AdvanceSimulation(0.2f);
	TestTrue(TEXT("Receiver activated by beam"), Receiver->IsActivated());

	// Latching receiver activated by a manual contribution, then the beam source goes away.
	Latch->UpdateContribution(TEXT("Manual"), 1.0f, FGameplayTag());
	Latch->AdvanceDwell(0.01f);
	TestTrue(TEXT("Latch activated"), Latch->IsActivated());

	FDocReceiverSnapshot SavedReceiver, SavedLatch;
	TestTrue(TEXT("Capture receiver"), Subsystem->CaptureReceiverState(TEXT("Rec_Restore"), SavedReceiver).IsSuccess());
	TestTrue(TEXT("Capture latch"), Subsystem->CaptureReceiverState(TEXT("Rec_Latch"), SavedLatch).IsSuccess());
	TestFalse(TEXT("Non-latched receiver saves no activation"), SavedReceiver.bLatchedActive);
	TestTrue(TEXT("Latched receiver saves activation"), SavedLatch.bLatchedActive);

	// "Load": the world now has the emitter disabled.
	Subsystem->SetEmitterEnabled(TEXT("Emitter_Restore"), false);
	int32 Events = 0;
	Receiver->OnReceiverActivatedNative.AddLambda([&Events](const FDocReceiverState&) { ++Events; });
	Latch->OnReceiverActivatedNative.AddLambda([&Events](const FDocReceiverState&) { ++Events; });
	Latch->ClearAllContributions();
	TestTrue(TEXT("Restore receiver"), Subsystem->RestoreReceiverState(SavedReceiver).IsSuccess());
	TestTrue(TEXT("Restore latch"), Subsystem->RestoreReceiverState(SavedLatch).IsSuccess());

	TestFalse(TEXT("Non-latched receiver starts inactive after restore"), Receiver->IsActivated());
	TestEqual(TEXT("Stale trace contributions dropped"), Receiver->GetReceiverState().Contributions.Num(), 0);
	TestTrue(TEXT("Every path queued for recompute"), Subsystem->IsPathPending(TEXT("Emitter_Restore")));
	TestTrue(TEXT("Latched receiver restored active"), Latch->IsActivated());
	TestEqual(TEXT("Restore emits no activation events"), Events, 0);

	Subsystem->AdvanceSimulation(0.5f);
	TestFalse(TEXT("Recomputed path (emitter off) does not activate"), Receiver->IsActivated());
	TestTrue(TEXT("Latch holds without a beam"), Latch->IsActivated());

	// Re-enabled: activation must be earned with a fresh dwell.
	Subsystem->SetEmitterEnabled(TEXT("Emitter_Restore"), true);
	Subsystem->AdvanceSimulation(0.2f);
	TestFalse(TEXT("Fresh dwell required (0.2 < 0.3)"), Receiver->IsActivated());
	Subsystem->AdvanceSimulation(0.2f);
	TestTrue(TEXT("Activated after a fresh full dwell"), Receiver->IsActivated());
	TestTrue(TEXT("Emitter alive"), Emitter != nullptr);

	return true;
}

// OPT-09: Doc.Optics.CookedPresentation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsCookedPresentationTest, FAutomationTestBase, "Doc.Optics.CookedPresentation", DocOpticalTests::Flags)
bool FDocOpticsCookedPresentationTest::RunTest(const FString& Parameters)
{
	// Logic-level evidence: the generic presentation consumes committed segments without per-frame work or per-segment
	// components. Visual confirmation in a cooked build remains a manual gate.
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();

	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("Emitter_Pres"), 1.0f, DocOpticalTests::TAG_Channel_Red, 3000.0f);
	AActor* Owner = Emitter->GetOwner();
	UDocBeamPresentationComponent* Presentation = NewObject<UDocBeamPresentationComponent>(Owner);
	Presentation->RegisterComponent();
	TestEqual(TEXT("Presentation follows the owner's emitter"), Presentation->EmitterId, FName(TEXT("Emitter_Pres")));

	UDocBeamSurfaceComponent* Mirror = DocOpticalTests::MakeSurface(TW, TEXT("Mirror_P"), DocOpticalTests::CreateMirrorProfile());
	Mirror->SurfaceNormalOverride = FVector(-1, 1, 0).GetSafeNormal();
	Subsystem->AddProgrammaticPlane(TEXT("P_Mirr"), FVector(1000, 0, 0), FVector(-1, 1, 0).GetSafeNormal(), Mirror);

	Subsystem->AdvanceSimulation(0.016f);
	FDocBeamPath Path;
	Subsystem->QueryPath(TEXT("Emitter_Pres"), Path);
	TestEqual(TEXT("One transform per segment"), Presentation->SegmentTransforms.Num(), Path.Segments.Num());
	TestEqual(TEXT("Two segments (reflected)"), Path.Segments.Num(), 2);
	if (Presentation->SegmentTransforms.Num() == 2)
	{
		TestTrue(TEXT("Transform starts at the segment start"), Presentation->SegmentTransforms[0].GetLocation().Equals(Path.Segments[0].StartPoint, 1e-2f));
		TestTrue(TEXT("Transform length matches"), FMath::IsNearlyEqual(Presentation->SegmentTransforms[0].GetScale3D().X, Path.Segments[0].Length, 1e-2f));
		TestTrue(TEXT("Channel label available for non-color readability"), Presentation->SegmentChannels[0] == DocOpticalTests::TAG_Channel_Red);
	}

	// Nothing moved: no re-solve, no rebuild, no new components.
	const int32 Rebuilds = Presentation->RebuildCount;
	const int32 ComponentCount = Owner->GetComponents().Num();
	for (int32 i = 0; i < 10; ++i)
	{
		Subsystem->AdvanceSimulation(0.016f);
	}
	TestEqual(TEXT("No rebuild while nothing changes"), Presentation->RebuildCount, Rebuilds);
	TestEqual(TEXT("No components spawned per frame or per segment"), Owner->GetComponents().Num(), ComponentCount);

	// Moving the mirror rebuilds once.
	Subsystem->NotifySurfaceMoved(TEXT("Mirror_P"));
	Subsystem->AdvanceSimulation(0.016f);
	TestEqual(TEXT("One rebuild per committed path change"), Presentation->RebuildCount, Rebuilds + 1);
	for (const FDocBeamSegment& Seg : Path.Segments)
	{
		TestTrue(TEXT("Segment has positive length"), Seg.Length > 0.0f);
		TestTrue(TEXT("Segment intensity in range"), Seg.InitialIntensity >= 0.0f && Seg.FinalIntensity <= 1.0f);
		TestFalse(TEXT("Segment direction is not zero"), Seg.Direction.IsNearlyZero());
	}

	return true;
}

// OPT-10: Doc.Optics.NoSiblingDependency
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocOpticsNoSiblingDependencyTest, FAutomationTestBase, "Doc.Optics.NoSiblingDependency", DocOpticalTests::Flags)
bool FDocOpticsNoSiblingDependencyTest::RunTest(const FString& Parameters)
{
	// Standalone: no Puzzle, Power, Niagara, or security plugin. The module depends on DocModularCore and engine modules only.
	FDocScopedTestWorld TW;
	UDocOpticalBeamSubsystem* Subsystem = TW.World->GetSubsystem<UDocOpticalBeamSubsystem>();
	TestNotNull(TEXT("Subsystem operates completely standalone"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	UDocBeamReceiverComponent* Receiver = DocOpticalTests::MakeReceiver(TW, TEXT("Door"), 0.5f, 0.0f, 0.0f);
	UDocBeamSurfaceComponent* Mirror = DocOpticalTests::MakeSurface(TW, TEXT("Mirror"), DocOpticalTests::CreateMirrorProfile());
	Mirror->SurfaceNormalOverride = FVector(-1, 1, 0).GetSafeNormal();
	Subsystem->AddProgrammaticPlane(TEXT("PM"), FVector(400, 0, 0), FVector(-1, 1, 0).GetSafeNormal(), Mirror);
	Subsystem->AddProgrammaticPlane(TEXT("PR"), FVector(400, 600, 0), FVector(0, -1, 0), nullptr, Receiver);
	UDocBeamEmitterComponent* Emitter = DocOpticalTests::MakeEmitter(TW, TEXT("StandaloneEmitter"));

	Subsystem->AdvanceSimulation(0.05f);
	FDocBeamPath Path;
	TestTrue(TEXT("Path available"), Subsystem->QueryPath(TEXT("StandaloneEmitter"), Path).IsSuccess());
	TestEqual(TEXT("Slice: one reflection into the receiver"), Path.TerminationReason, EDocBeamTerminationReason::HitReceiver);
	TestEqual(TEXT("One reflection"), Path.ReflectionCount, 1);
	TestTrue(TEXT("Receiver activated"), Receiver->IsActivated());
	TestTrue(TEXT("Receiver intensity is the mirror's reflectivity"), FMath::IsNearlyEqual(Receiver->GetReceiverState().CurrentIntensity, 0.95f, 1e-3f));
	TestTrue(TEXT("Emitter alive"), Emitter != nullptr);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
