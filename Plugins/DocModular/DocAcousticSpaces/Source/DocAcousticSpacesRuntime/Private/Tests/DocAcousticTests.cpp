#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocAcousticSpaceSubsystem.h"
#include "DocAcousticSpaceComponent.h"
#include "DocAcousticPortalComponent.h"
#include "DocAcousticEmitterComponent.h"
#include "DocAcousticPlaybackAdapter.h"
#include "DocAcousticProfile.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocAcousticTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	AActor* SpawnAt(FDocScopedTestWorld& TW, const FVector& Location)
	{
		AActor* Actor = TW.Spawn<AActor>(Location);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	UDocAcousticEmitterComponent* SpawnEmitter(FDocScopedTestWorld& TW, FName Id, const FVector& Location, float SmoothingSpeed = 0.0f)
	{
		AActor* Actor = SpawnAt(TW, Location);
		UDocAcousticEmitterComponent* Emitter = NewObject<UDocAcousticEmitterComponent>(Actor);
		Emitter->EmitterId = Id;
		Emitter->SmoothingInterpSpeed = SmoothingSpeed;
		Emitter->RegisterComponent();
		return Emitter;
	}

	UDocAcousticReferencePlaybackAdapter* MakeAdapter(UDocAcousticSpaceSubsystem* Subsystem)
	{
		UDocAcousticReferencePlaybackAdapter* Adapter = NewObject<UDocAcousticReferencePlaybackAdapter>(Subsystem);
		Subsystem->SetPlaybackAdapter(Adapter);
		return Adapter;
	}
}

// ACO-01: Doc.Acoustics.SpaceMembership
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsSpaceMembershipTest, FAutomationTestBase, "Doc.Acoustics.SpaceMembership", DocAcousticTests::Flags)
bool FDocAcousticsSpaceMembershipTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();
	TestNotNull(TEXT("Subsystem is valid"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	Subsystem->AddProgrammaticSpace(TEXT("Space_Low"), FBox(FVector(0.0f), FVector(1000.0f)), 10);
	Subsystem->AddProgrammaticSpace(TEXT("Space_High"), FBox(FVector(500.0f), FVector(1500.0f)), 20);

	TestEqual(TEXT("Resolves to Space_Low"), Subsystem->ResolveSpaceForLocation(FVector(200.0f)), FName(TEXT("Space_Low")));
	TestEqual(TEXT("High priority wins in overlap region"), Subsystem->ResolveSpaceForLocation(FVector(750.0f)), FName(TEXT("Space_High")));
	TestEqual(TEXT("Boundary hysteresis retains previous space"),
		Subsystem->ResolveSpaceForLocation(FVector(1020.0f, 500.0f, 500.0f), TEXT("Space_Low"), 50.0f), FName(TEXT("Space_Low")));
	TestEqual(TEXT("Exited space hysteresis"),
		Subsystem->ResolveSpaceForLocation(FVector(1100.0f, 200.0f, 200.0f), TEXT("Space_Low"), 50.0f), FName(NAME_None));

	// Duplicate ids are refused.
	TestEqual(TEXT("Duplicate SpaceId conflicts"),
		Subsystem->AddProgrammaticSpace(TEXT("Space_Low"), FBox(FVector(5000.0f), FVector(6000.0f)), 0).Outcome, EDocResultOutcome::Conflict);

	// Equal-priority overlap: resolved by lexical id whatever the insertion order, and reported as ambiguous.
	Subsystem->AddProgrammaticSpace(TEXT("Tie_B"), FBox(FVector(3000.0f), FVector(4000.0f)), 5);
	Subsystem->AddProgrammaticSpace(TEXT("Tie_A"), FBox(FVector(3500.0f), FVector(4500.0f)), 5);
	TestEqual(TEXT("Tie resolved by id"), Subsystem->ResolveSpaceForLocation(FVector(3750.0f)), FName(TEXT("Tie_A")));
	TArray<FString> Issues;
	TestFalse(TEXT("Ambiguous membership reported"), Subsystem->ValidateTopology(Issues).IsSuccess());
	TestEqual(TEXT("Only the equal-priority overlap is ambiguous"), Issues.Num(), 1);

	// Tracked membership for a listener walking through a doorway does not toggle at the boundary.
	Subsystem->AddProgrammaticSpace(TEXT("Hall"), FBox(FVector(0, 10000, 0), FVector(1000, 11000, 1000)));
	Subsystem->AddProgrammaticSpace(TEXT("Room"), FBox(FVector(1000, 10000, 0), FVector(2000, 11000, 1000)));
	Subsystem->AddProgrammaticPortal(TEXT("HallDoor"), TEXT("Hall"), TEXT("Room"));
	Subsystem->MembershipHysteresis = 50.0f;

	auto Walk = [&](float X)
	{
		FDocAcousticPathQuery Q;
		Q.EmitterLocation = FVector(500, 10500, 500);
		Q.ListenerId = TEXT("Walker");
		Q.ListenerLocation = FVector(X, 10500, 500);
		FDocAcousticPathResult R;
		Subsystem->QueryTransmission(Q, R);
		return Subsystem->GetTrackedListenerSpace(TEXT("Walker"));
	};
	TestEqual(TEXT("Start in hall"), Walk(900.0f), FName(TEXT("Hall")));
	TestEqual(TEXT("Just past the boundary: still hall"), Walk(1030.0f), FName(TEXT("Hall")));
	TestEqual(TEXT("Clearly inside: room"), Walk(1100.0f), FName(TEXT("Room")));
	TestEqual(TEXT("Stepping back over the line: still room"), Walk(970.0f), FName(TEXT("Room")));
	TestEqual(TEXT("Clearly back: hall"), Walk(900.0f), FName(TEXT("Hall")));

	return true;
}

// ACO-02: Doc.Acoustics.StrongestPath
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsStrongestPathTest, FAutomationTestBase, "Doc.Acoustics.StrongestPath", DocAcousticTests::Flags)
bool FDocAcousticsStrongestPathTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();

	Subsystem->AddProgrammaticSpace(TEXT("RoomA"), FBox(FVector(0.0f), FVector(500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("RoomB"), FBox(FVector(1000.0f), FVector(1500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("RoomC"), FBox(FVector(2000.0f), FVector(2500.0f)));

	// A -> B -> C: 0.5 * 0.5 = 0.25. Direct A -> C: 0.8.
	Subsystem->AddProgrammaticPortal(TEXT("Portal_AB"), TEXT("RoomA"), TEXT("RoomB"), 1.0f, 0.1f, 0.5f, 400.0f, 10000.0f);
	Subsystem->AddProgrammaticPortal(TEXT("Portal_BC"), TEXT("RoomB"), TEXT("RoomC"), 1.0f, 0.1f, 0.5f, 400.0f, 10000.0f);
	Subsystem->AddProgrammaticPortal(TEXT("Portal_AC"), TEXT("RoomA"), TEXT("RoomC"), 1.0f, 0.1f, 0.8f, 400.0f, 15000.0f);

	FDocAcousticPathQuery Query;
	Query.EmitterLocation = FVector(250.0f);
	Query.ListenerLocation = FVector(2250.0f);

	FDocAcousticPathResult Result;
	TestTrue(TEXT("Query succeeded"), Subsystem->QueryTransmission(Query, Result).IsSuccess());
	TestEqual(TEXT("Path connected"), Result.Status, EDocAcousticPathStatus::Connected);
	TestTrue(TEXT("Strongest path chosen (AC with gain 0.8)"), FMath::IsNearlyEqual(Result.TransmissionGain, 0.8f, 1e-3f));
	TestTrue(TEXT("Path traversed only Portal_AC"), Result.PathPortals.Num() == 1 && Result.PathPortals[0] == TEXT("Portal_AC"));
	TestEqual(TEXT("Gain stages listed"), Result.PortalGains.Num(), 1);
	TestTrue(TEXT("Cost is -ln(gain)"), FMath::IsNearlyEqual(Result.PathCost, -FMath::Loge(0.8f), 1e-3f));

	// Weakening the direct portal below the two-hop route switches the path; gains multiply, cutoff is the minimum.
	Subsystem->SetPortalOpenness(TEXT("Portal_AC"), 0.0f); // 0.1
	Subsystem->QueryTransmission(Query, Result);
	TestTrue(TEXT("Two-hop route now strongest"), Result.PathPortals.Num() == 2 && Result.PathPortals[0] == TEXT("Portal_AB") && Result.PathPortals[1] == TEXT("Portal_BC"));
	TestTrue(TEXT("Gains multiply along the path"), FMath::IsNearlyEqual(Result.TransmissionGain, 0.25f, 1e-3f));
	TestTrue(TEXT("Most restrictive cutoff"), FMath::IsNearlyEqual(Result.CutoffFrequencyHz, 10000.0f, 1.0f));

	// Exact ties break by portal id, independent of insertion order.
	Subsystem->AddProgrammaticSpace(TEXT("TieFrom"), FBox(FVector(10000.0f), FVector(10500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("TieTo"), FBox(FVector(11000.0f), FVector(11500.0f)));
	Subsystem->AddProgrammaticPortal(TEXT("Door_B"), TEXT("TieFrom"), TEXT("TieTo"), 1.0f, 0.1f, 0.6f);
	Subsystem->AddProgrammaticPortal(TEXT("Door_A"), TEXT("TieFrom"), TEXT("TieTo"), 1.0f, 0.1f, 0.6f);
	FDocAcousticPathQuery TieQuery;
	TieQuery.EmitterLocation = FVector(10250.0f);
	TieQuery.ListenerLocation = FVector(11250.0f);
	Subsystem->QueryTransmission(TieQuery, Result);
	TestTrue(TEXT("Tie broken by lexical portal id"), Result.PathPortals.Num() == 1 && Result.PathPortals[0] == TEXT("Door_A"));
	TestTrue(TEXT("Parallel routes are not summed (no fake interference)"), FMath::IsNearlyEqual(Result.TransmissionGain, 0.6f, 1e-3f));

	// Invalid authoring is refused.
	TestFalse(TEXT("Self-loop portal refused"), Subsystem->AddProgrammaticPortal(TEXT("Loop"), TEXT("RoomA"), TEXT("RoomA")).IsSuccess());
	TestFalse(TEXT("Min > Max transmission refused"), Subsystem->AddProgrammaticPortal(TEXT("Bad"), TEXT("RoomA"), TEXT("RoomB"), 1.0f, 0.9f, 0.1f).IsSuccess());
	TestFalse(TEXT("Non-finite transmission refused"),
		Subsystem->AddProgrammaticPortal(TEXT("NaN"), TEXT("RoomA"), TEXT("RoomB"), 1.0f, 0.1f, std::numeric_limits<float>::quiet_NaN()).IsSuccess());

	return true;
}

// ACO-03: Doc.Acoustics.DoorTransition
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsDoorTransitionTest, FAutomationTestBase, "Doc.Acoustics.DoorTransition", DocAcousticTests::Flags)
bool FDocAcousticsDoorTransitionTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();

	Subsystem->AddProgrammaticSpace(TEXT("RoomA"), FBox(FVector(0.0f), FVector(500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("RoomB"), FBox(FVector(1000.0f), FVector(1500.0f)));
	Subsystem->AddProgrammaticPortal(TEXT("Door1"), TEXT("RoomA"), TEXT("RoomB"), 1.0f, 0.05f, 1.0f, 500.0f, 20000.0f);

	const int64 Rev1 = Subsystem->GetTopologyRevision();
	FDocAcousticPathQuery Query;
	Query.EmitterLocation = FVector(250.0f);
	Query.ListenerLocation = FVector(1250.0f);

	FDocAcousticPathResult ResOpen;
	Subsystem->QueryTransmission(Query, ResOpen);
	TestTrue(TEXT("Door open: full gain"), FMath::IsNearlyEqual(ResOpen.TransmissionGain, 1.0f, 1e-3f));
	TestTrue(TEXT("Door open: full cutoff"), FMath::IsNearlyEqual(ResOpen.CutoffFrequencyHz, 20000.0f, 1.0f));
	FDocAcousticPathResult ResOpenAgain;
	Subsystem->QueryTransmission(Query, ResOpenAgain);
	TestTrue(TEXT("Repeat query served from cache"), ResOpenAgain.bFromCache);

	Subsystem->SetPortalOpenness(TEXT("Door1"), 0.0f);
	TestTrue(TEXT("Topology revision incremented on door change"), Subsystem->GetTopologyRevision() > Rev1);

	FDocAcousticPathResult ResClosed;
	Subsystem->QueryTransmission(Query, ResClosed);
	TestFalse(TEXT("Door change invalidated the cache"), ResClosed.bFromCache);
	TestTrue(TEXT("Door closed: min gain 0.05"), FMath::IsNearlyEqual(ResClosed.TransmissionGain, 0.05f, 1e-3f));
	TestTrue(TEXT("Door closed: min cutoff 500Hz"), FMath::IsNearlyEqual(ResClosed.CutoffFrequencyHz, 500.0f, 1.0f));
	TestEqual(TEXT("Result carries the new revision"), ResClosed.TopologyRevision, Subsystem->GetTopologyRevision());

	// A portal component changed directly also invalidates cached paths.
	Subsystem->AddProgrammaticSpace(TEXT("RoomX"), FBox(FVector(5000.0f), FVector(5500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("RoomY"), FBox(FVector(6000.0f), FVector(6500.0f)));
	AActor* DoorActor = DocAcousticTests::SpawnAt(TW, FVector(5750.0f));
	UDocAcousticPortalComponent* Door = NewObject<UDocAcousticPortalComponent>(DoorActor);
	Door->PortalId = TEXT("CompDoor");
	Door->SpaceA = TEXT("RoomX");
	Door->SpaceB = TEXT("RoomY");
	Door->RegisterComponent();
	TestTrue(TEXT("Portal component self-registered"), Door->LastRegistrationError.IsEmpty());

	FDocAcousticPathQuery XY;
	XY.EmitterLocation = FVector(5250.0f);
	XY.ListenerLocation = FVector(6250.0f);
	FDocAcousticPathResult R;
	Subsystem->QueryTransmission(XY, R);
	TestTrue(TEXT("Component door open"), FMath::IsNearlyEqual(R.TransmissionGain, 1.0f, 1e-3f));
	Subsystem->QueryTransmission(XY, R);
	TestTrue(TEXT("Cached before change"), R.bFromCache);
	Door->SetOpenness(0.0f);
	Subsystem->QueryTransmission(XY, R);
	TestFalse(TEXT("Not served from a stale cache"), R.bFromCache);
	TestTrue(TEXT("Component door closed"), FMath::IsNearlyEqual(R.TransmissionGain, 0.05f, 1e-3f));

	// Smooth audible transition: the emitter approaches the new value over time, without a one-frame jump.
	UDocAcousticReferencePlaybackAdapter* Adapter = DocAcousticTests::MakeAdapter(Subsystem);
	Subsystem->UpdateListener(TEXT("Hero"), FVector(1250.0f));
	Subsystem->SetMixListener(TEXT("Hero"));
	Subsystem->SetPortalOpenness(TEXT("Door1"), 1.0f);
	UDocAcousticEmitterComponent* Emitter = DocAcousticTests::SpawnEmitter(TW, TEXT("Radio"), FVector(250.0f), 5.0f);
	FGuid Claim;
	Subsystem->AcquireAcousticClaim(TEXT("Radio"), Claim);
	Subsystem->AdvanceSmoothing(0.05f);
	FDocAcousticEmitterState S;
	Adapter->GetEmitterState(TEXT("Radio"), S);
	TestTrue(TEXT("First evaluation snaps to the open value"), FMath::IsNearlyEqual(S.AppliedGain, 1.0f, 1e-3f));

	Subsystem->SetPortalOpenness(TEXT("Door1"), 0.0f);
	float Previous = S.AppliedGain;
	bool bMonotonic = true;
	for (int32 i = 0; i < 10; ++i)
	{
		Subsystem->AdvanceSmoothing(0.05f);
		Adapter->GetEmitterState(TEXT("Radio"), S);
		bMonotonic &= S.AppliedGain <= Previous + 1e-5f;
		if (i == 0)
		{
			TestTrue(TEXT("No one-frame jump to the closed value"), S.AppliedGain > 0.5f);
		}
		Previous = S.AppliedGain;
	}
	TestTrue(TEXT("Gain decreases monotonically"), bMonotonic);
	for (int32 i = 0; i < 60; ++i)
	{
		Subsystem->AdvanceSmoothing(0.05f);
	}
	Adapter->GetEmitterState(TEXT("Radio"), S);
	TestTrue(TEXT("Settles at the closed transmission"), FMath::IsNearlyEqual(S.AppliedGain, 0.05f, 1e-2f));
	TestTrue(TEXT("Emitter kept its component identity"), Emitter->EmitterId == TEXT("Radio"));

	return true;
}

// ACO-04: Doc.Acoustics.DisconnectedPath
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsDisconnectedPathTest, FAutomationTestBase, "Doc.Acoustics.DisconnectedPath", DocAcousticTests::Flags)
bool FDocAcousticsDisconnectedPathTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();

	Subsystem->AddProgrammaticSpace(TEXT("RoomA"), FBox(FVector(0.0f), FVector(500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("RoomB"), FBox(FVector(2000.0f), FVector(2500.0f)));

	FDocAcousticPathQuery Query;
	Query.EmitterLocation = FVector(250.0f);
	Query.ListenerLocation = FVector(2250.0f);

	FDocAcousticPathResult Result;
	TestTrue(TEXT("Blocked is a valid answer, not an error"), Subsystem->QueryTransmission(Query, Result).IsSuccess());
	TestEqual(TEXT("Status is Blocked"), Result.Status, EDocAcousticPathStatus::Blocked);
	TestEqual(TEXT("Reason NoRoute"), Result.Reason, FName(TEXT("NoRoute")));
	TestTrue(TEXT("Gain is configured floor gain, NOT 1.0 fallback"), FMath::IsNearlyEqual(Result.TransmissionGain, Subsystem->DisconnectedFloorGain, 1e-4f));
	TestTrue(TEXT("Cutoff is configured floor cutoff"), FMath::IsNearlyEqual(Result.CutoffFrequencyHz, Subsystem->DisconnectedCutoffHz, 1.0f));

	// A fully closed door with zero closed transmission is a blocked edge, not an unsafe infinite cost.
	Subsystem->AddProgrammaticPortal(TEXT("Vault"), TEXT("RoomA"), TEXT("RoomB"), 0.0f, 0.0f, 1.0f);
	Subsystem->QueryTransmission(Query, Result);
	TestEqual(TEXT("Zero-gain door blocks"), Result.Status, EDocAcousticPathStatus::Blocked);
	TestTrue(TEXT("Zero-gain door uses the floor"), FMath::IsNearlyEqual(Result.TransmissionGain, Subsystem->DisconnectedFloorGain, 1e-4f));

	// Floor policy is configurable and honored.
	Subsystem->DisconnectedFloorGain = 0.02f;
	Subsystem->QueryTransmission(Query, Result);
	TestTrue(TEXT("Blocked route served from cache"), Result.bFromCache);
	TestTrue(TEXT("Changed floor policy applied even to a cached route"), FMath::IsNearlyEqual(Result.TransmissionGain, 0.02f, 1e-4f));

	// Unresolved: listener outside every space follows the unresolved policy.
	FDocAcousticPathQuery Outside = Query;
	Outside.ListenerLocation = FVector(99999.0f);
	Subsystem->QueryTransmission(Outside, Result);
	TestEqual(TEXT("Outside every space is Unresolved"), Result.Status, EDocAcousticPathStatus::Unresolved);
	TestTrue(TEXT("Unresolved uses the floor, never full volume"), Result.TransmissionGain < 0.5f);

	Subsystem->AddProgrammaticSpace(TEXT("Exterior"), FBox(FVector(-20000.0f), FVector(-19000.0f)));
	Subsystem->AddProgrammaticPortal(TEXT("Window"), TEXT("RoomA"), TEXT("Exterior"), 1.0f, 0.1f, 0.3f);
	Subsystem->UnresolvedPolicy = EDocAcousticUnresolvedPolicy::TreatAsExterior;
	Subsystem->ExteriorSpaceId = TEXT("Exterior");
	Subsystem->QueryTransmission(Outside, Result);
	TestEqual(TEXT("Exterior policy maps the listener"), Result.ListenerSpace, FName(TEXT("Exterior")));
	TestTrue(TEXT("Exterior reached through the window"), Result.Status == EDocAcousticPathStatus::Connected && FMath::IsNearlyEqual(Result.TransmissionGain, 0.3f, 1e-3f));

	return true;
}

// ACO-05: Doc.Acoustics.ParameterComposition
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsParameterCompositionTest, FAutomationTestBase, "Doc.Acoustics.ParameterComposition", DocAcousticTests::Flags)
bool FDocAcousticsParameterCompositionTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();
	UDocAcousticReferencePlaybackAdapter* Adapter = DocAcousticTests::MakeAdapter(Subsystem);

	UDocAcousticEmitterComponent* Emitter = DocAcousticTests::SpawnEmitter(TW, TEXT("Emitter_Music"), FVector::ZeroVector, 0.0f);
	Emitter->BaseGain = 0.8f;
	Emitter->BaseCutoffHz = 16000.0f;

	Emitter->SetTargetParameters(0.5f, 8000.0f, /*bSnap*/ true);
	TestTrue(TEXT("Target gain is 0.8 * 0.5 = 0.4"), FMath::IsNearlyEqual(Emitter->CurrentEffectiveGain, 0.4f, 1e-3f));
	TestTrue(TEXT("Target cutoff is min(16000, 8000) = 8000"), FMath::IsNearlyEqual(Emitter->CurrentEffectiveCutoffHz, 8000.0f, 1.0f));

	// Repeated acoustic updates never compound.
	for (int32 i = 0; i < 5; ++i)
	{
		Emitter->SetTargetParameters(0.5f, 8000.0f, true);
	}
	TestTrue(TEXT("No cumulative multiplication across updates"), FMath::IsNearlyEqual(Emitter->CurrentEffectiveGain, 0.4f, 1e-3f));

	// A host fade changes BaseGain; the next update composes with the new baseline.
	Emitter->BaseGain = 0.5f;
	Emitter->SetTargetParameters(0.5f, 8000.0f, true);
	TestTrue(TEXT("Non-cumulative parameter composition: 0.5 * 0.5 = 0.25"), FMath::IsNearlyEqual(Emitter->CurrentEffectiveGain, 0.25f, 1e-3f));

	// Through the claimed pipeline: a host fade mid-smoothing is not erased by the acoustic layer.
	FGuid Claim;
	TestTrue(TEXT("Claim acquired"), Subsystem->AcquireAcousticClaim(TEXT("Emitter_Music"), Claim).IsChanged());
	FGuid Again;
	const FDocSystemResult Repeat = Subsystem->AcquireAcousticClaim(TEXT("Emitter_Music"), Again);
	TestTrue(TEXT("Second claim request is NoChange with the same id"), Repeat.IsSuccess() && !Repeat.IsChanged() && Again == Claim);
	TestEqual(TEXT("Claim for unknown emitter is NotFound"), Subsystem->AcquireAcousticClaim(TEXT("Ghost"), Again).Outcome, EDocResultOutcome::NotFound);

	Emitter->BaseGain = 0.2f; // fade continues
	Subsystem->AdvanceSmoothing(0.1f);
	FDocAcousticEmitterState State;
	TestTrue(TEXT("Claimed emitter applied"), Adapter->GetEmitterState(TEXT("Emitter_Music"), State));
	TestTrue(TEXT("Applied = current host fade * transmission (0.2 * 0.5)"), FMath::IsNearlyEqual(State.AppliedGain, 0.1f, 1e-3f));

	// Releasing restores the current baseline, not a stale snapshot and not a hard-coded 1.0.
	Emitter->BaseGain = 0.3f;
	TestTrue(TEXT("Release"), Subsystem->ReleaseAcousticClaim(Claim).IsSuccess());
	Adapter->GetEmitterState(TEXT("Emitter_Music"), State);
	TestTrue(TEXT("Baseline restored to current host gain"), FMath::IsNearlyEqual(State.AppliedGain, 0.3f, 1e-3f));
	TestTrue(TEXT("Baseline cutoff restored"), FMath::IsNearlyEqual(State.AppliedCutoffHz, 16000.0f, 1.0f));
	TestFalse(TEXT("Adapter no longer claims the emitter"), State.bIsActive);
	TestTrue(TEXT("Acoustic influence cleared"), FMath::IsNearlyEqual(Emitter->TransmissionMultiplier, 1.0f, 1e-4f));

	// Unclaimed emitters are never written.
	const int32 AppliesBefore = State.ApplyCount;
	Subsystem->AdvanceSmoothing(0.1f);
	Adapter->GetEmitterState(TEXT("Emitter_Music"), State);
	TestEqual(TEXT("No writes after release"), State.ApplyCount, AppliesBefore);

	return true;
}

// ACO-06: Doc.Acoustics.ListenerIsolation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsListenerIsolationTest, FAutomationTestBase, "Doc.Acoustics.ListenerIsolation", DocAcousticTests::Flags)
bool FDocAcousticsListenerIsolationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();

	Subsystem->AddProgrammaticSpace(TEXT("SourceRoom"), FBox(FVector(0.0f), FVector(500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("Listener1Room"), FBox(FVector(1000.0f), FVector(1500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("Listener2Room"), FBox(FVector(2000.0f), FVector(2500.0f)));
	Subsystem->AddProgrammaticPortal(TEXT("P1"), TEXT("SourceRoom"), TEXT("Listener1Room"), 1.0f, 0.1f, 0.9f);
	Subsystem->AddProgrammaticPortal(TEXT("P2"), TEXT("SourceRoom"), TEXT("Listener2Room"), 1.0f, 0.1f, 0.3f);

	FDocAcousticPathQuery Q1;
	Q1.EmitterId = TEXT("Bell");
	Q1.ListenerId = TEXT("Listener1");
	Q1.ListenerLocation = FVector(1250.0f);
	Q1.EmitterLocation = FVector(250.0f);
	FDocAcousticPathResult R1;
	Subsystem->QueryTransmission(Q1, R1);

	FDocAcousticPathQuery Q2 = Q1;
	Q2.ListenerId = TEXT("Listener2");
	Q2.ListenerLocation = FVector(2250.0f);
	FDocAcousticPathResult R2;
	Subsystem->QueryTransmission(Q2, R2);

	TestTrue(TEXT("Listener 1 transmission is 0.9"), FMath::IsNearlyEqual(R1.TransmissionGain, 0.9f, 1e-3f));
	TestTrue(TEXT("Listener 2 transmission is 0.3"), FMath::IsNearlyEqual(R2.TransmissionGain, 0.3f, 1e-3f));
	TestEqual(TEXT("Listener 1 membership tracked separately"), Subsystem->GetTrackedListenerSpace(TEXT("Listener1")), FName(TEXT("Listener1Room")));
	TestEqual(TEXT("Listener 2 membership tracked separately"), Subsystem->GetTrackedListenerSpace(TEXT("Listener2")), FName(TEXT("Listener2Room")));

	FDocAcousticPathResult Debug1, Debug2;
	TestTrue(TEXT("Debug path per listener (1)"), Subsystem->GetDebugPath(TEXT("Bell"), TEXT("Listener1"), Debug1));
	TestTrue(TEXT("Debug path per listener (2)"), Subsystem->GetDebugPath(TEXT("Bell"), TEXT("Listener2"), Debug2));
	TestTrue(TEXT("Debug paths are distinct"), Debug1.PathPortals.Num() == 1 && Debug2.PathPortals.Num() == 1 && Debug1.PathPortals[0] != Debug2.PathPortals[0]);

	// Separate per-listener mixes are an explicit Unsupported capability.
	TestEqual(TEXT("Independent listener mixes unsupported"), Subsystem->RequestIndependentListenerMixes().Outcome, EDocResultOutcome::Unsupported);
	TestEqual(TEXT("Unknown mix listener is NotFound"), Subsystem->SetMixListener(TEXT("Nobody")).Outcome, EDocResultOutcome::NotFound);

	// The audible mix follows exactly one selected listener.
	UDocAcousticReferencePlaybackAdapter* Adapter = DocAcousticTests::MakeAdapter(Subsystem);
	UDocAcousticEmitterComponent* Bell = DocAcousticTests::SpawnEmitter(TW, TEXT("Bell"), FVector(250.0f), 0.0f);
	FGuid Claim;
	Subsystem->AcquireAcousticClaim(TEXT("Bell"), Claim);
	Subsystem->UpdateListener(TEXT("Listener1"), FVector(1250.0f));
	Subsystem->UpdateListener(TEXT("Listener2"), FVector(2250.0f));

	TestTrue(TEXT("Mix listener 1"), Subsystem->SetMixListener(TEXT("Listener1")).IsSuccess());
	TestEqual(TEXT("Adapter follows the mix listener"), Adapter->GetActiveListener(), FName(TEXT("Listener1")));
	Subsystem->AdvanceSmoothing(0.1f);
	FDocAcousticEmitterState State;
	Adapter->GetEmitterState(TEXT("Bell"), State);
	TestTrue(TEXT("Mix uses listener 1 path"), FMath::IsNearlyEqual(State.AppliedGain, 0.9f, 1e-3f));

	Subsystem->SetMixListener(TEXT("Listener2"));
	Subsystem->AdvanceSmoothing(0.1f);
	Adapter->GetEmitterState(TEXT("Bell"), State);
	TestTrue(TEXT("Mix switched to listener 2 path"), FMath::IsNearlyEqual(State.AppliedGain, 0.3f, 1e-3f));
	TestTrue(TEXT("Emitter alive"), Bell != nullptr);

	return true;
}

// ACO-07: Doc.Acoustics.PathBudget
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsPathBudgetTest, FAutomationTestBase, "Doc.Acoustics.PathBudget", DocAcousticTests::Flags)
bool FDocAcousticsPathBudgetTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();

	for (int32 i = 0; i <= 5; ++i)
	{
		Subsystem->AddProgrammaticSpace(*FString::Printf(TEXT("S%d"), i), FBox(FVector(i * 1000.0f), FVector(i * 1000.0f + 500.0f)));
		if (i > 0)
		{
			Subsystem->AddProgrammaticPortal(*FString::Printf(TEXT("P_%d_%d"), i - 1, i),
				*FString::Printf(TEXT("S%d"), i - 1), *FString::Printf(TEXT("S%d"), i), 1.0f, 0.1f, 0.8f);
		}
	}

	FDocAcousticPathQuery Query;
	Query.EmitterLocation = FVector(250.0f);
	Query.ListenerLocation = FVector(5250.0f);
	Query.MaxHops = 2;

	FDocAcousticPathResult Result;
	Subsystem->QueryTransmission(Query, Result);
	TestEqual(TEXT("Path exceeding MaxHops returns Blocked"), Result.Status, EDocAcousticPathStatus::Blocked);
	TestEqual(TEXT("Reason HopLimit"), Result.Reason, FName(TEXT("HopLimit")));

	Query.MaxHops = 10;
	Query.MaxVisitedNodes = 2;
	Query.bAllowApproximation = false;
	FDocSystemResult RetBudget = Subsystem->QueryTransmission(Query, Result);
	TestEqual(TEXT("Exceeded budget fails cleanly"), RetBudget.Outcome, EDocResultOutcome::Failed);
	TestEqual(TEXT("Status indicates BudgetExceeded"), Result.Status, EDocAcousticPathStatus::BudgetExceeded);
	TestTrue(TEXT("Budget failure uses the floor, not full volume"), FMath::IsNearlyEqual(Result.TransmissionGain, Subsystem->DisconnectedFloorGain, 1e-4f));

	// With approximation allowed, a tentative route is returned and flagged, never presented as exact.
	Subsystem->AddProgrammaticPortal(TEXT("P_0_5_Weak"), TEXT("S0"), TEXT("S5"), 1.0f, 0.001f, 0.01f);
	Query.MaxVisitedNodes = 1;
	Query.bAllowApproximation = true;
	TestTrue(TEXT("Approximate query succeeds"), Subsystem->QueryTransmission(Query, Result).IsSuccess());
	TestEqual(TEXT("Approximate route connected"), Result.Status, EDocAcousticPathStatus::Connected);
	TestTrue(TEXT("Flagged approximate"), Result.bIsApproximation);
	TestEqual(TEXT("Reason BudgetApproximate"), Result.Reason, FName(TEXT("BudgetApproximate")));

	Query.MaxVisitedNodes = 64;
	Query.bAllowApproximation = false;
	Subsystem->QueryTransmission(Query, Result);
	TestFalse(TEXT("Full search is exact"), Result.bIsApproximation);
	TestTrue(TEXT("Full search finds the stronger 5-hop chain (0.8^5)"), FMath::IsNearlyEqual(Result.TransmissionGain, FMath::Pow(0.8f, 5.0f), 1e-3f));

	// A large graph terminates within the visited limit.
	for (int32 i = 0; i < 300; ++i)
	{
		Subsystem->AddProgrammaticSpace(*FString::Printf(TEXT("Big%03d"), i), FBox(FVector(100000.0f + i * 100.0f, 0, 0), FVector(100000.0f + i * 100.0f + 50.0f, 50, 50)));
		if (i > 0)
		{
			Subsystem->AddProgrammaticPortal(*FString::Printf(TEXT("BigP%03d"), i), *FString::Printf(TEXT("Big%03d"), i - 1), *FString::Printf(TEXT("Big%03d"), i));
		}
	}
	FDocAcousticPathQuery Big;
	Big.EmitterLocation = FVector(100025.0f, 25, 25);
	Big.ListenerLocation = FVector(100000.0f + 299 * 100.0f + 25.0f, 25, 25);
	Big.MaxHops = 1000;
	Big.MaxVisitedNodes = 64;
	TestEqual(TEXT("Large graph stops at the visited budget"), Subsystem->QueryTransmission(Big, Result).Outcome, EDocResultOutcome::Failed);
	TestEqual(TEXT("Large graph reports BudgetExceeded"), Result.Status, EDocAcousticPathStatus::BudgetExceeded);

	return true;
}

// ACO-08: Doc.Acoustics.UnloadTeardown
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsUnloadTeardownTest, FAutomationTestBase, "Doc.Acoustics.UnloadTeardown", DocAcousticTests::Flags)
bool FDocAcousticsUnloadTeardownTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();
	UDocAcousticReferencePlaybackAdapter* Adapter = DocAcousticTests::MakeAdapter(Subsystem);

	UDocAcousticEmitterComponent* Emitter = DocAcousticTests::SpawnEmitter(TW, TEXT("Emitter_Teardown"), FVector::ZeroVector);
	TestTrue(TEXT("Explicit re-registration is idempotent"), Subsystem->RegisterEmitter(Emitter).IsSuccess());

	FGuid ClaimId;
	TestTrue(TEXT("Claim acquired"), Subsystem->AcquireAcousticClaim(TEXT("Emitter_Teardown"), ClaimId).IsSuccess());
	Subsystem->UnregisterEmitter(Emitter);
	TestEqual(TEXT("Claim was already released during emitter unregister"), Subsystem->ReleaseAcousticClaim(ClaimId).Outcome, EDocResultOutcome::NotFound);

	// Destroying the emitter's actor releases its claim and resets the adapter to the baseline.
	UDocAcousticEmitterComponent* Doomed = DocAcousticTests::SpawnEmitter(TW, TEXT("Doomed"), FVector::ZeroVector);
	Doomed->BaseGain = 0.6f;
	FGuid DoomedClaim;
	Subsystem->AcquireAcousticClaim(TEXT("Doomed"), DoomedClaim);
	Doomed->SetTargetParameters(0.1f, 500.0f, true);
	Subsystem->AdvanceSmoothing(0.1f);
	FDocAcousticEmitterState State;
	Adapter->GetEmitterState(TEXT("Doomed"), State);
	TestTrue(TEXT("Applied before destruction"), State.bIsActive);
	Doomed->GetOwner()->Destroy();
	TestEqual(TEXT("No claims left"), Subsystem->GetActiveClaimCount(), 0);
	Adapter->GetEmitterState(TEXT("Doomed"), State);
	TestFalse(TEXT("Adapter released the emitter"), State.bIsActive);
	TestTrue(TEXT("Adapter reset to the last baseline"), FMath::IsNearlyEqual(State.AppliedGain, 0.6f, 1e-3f));
	Subsystem->AdvanceSmoothing(0.1f); // no stale reference use
	TestEqual(TEXT("Re-registration under the same id works after destruction"),
		DocAcousticTests::SpawnEmitter(TW, TEXT("Doomed"), FVector::ZeroVector)->LastRegistrationError, FString());

	// Spaces and portals: unload without retention removes; with retention keeps a component-free descriptor.
	AActor* Level = DocAcousticTests::SpawnAt(TW, FVector::ZeroVector);
	UDocAcousticSpaceComponent* Cellar = NewObject<UDocAcousticSpaceComponent>(Level);
	Cellar->SpaceId = TEXT("Cellar");
	Cellar->BoundsBox = FBox(FVector(0.0f), FVector(500.0f));
	Cellar->RegisterComponent();
	UDocAcousticSpaceComponent* Attic = NewObject<UDocAcousticSpaceComponent>(Level);
	Attic->SpaceId = TEXT("Attic");
	Attic->BoundsBox = FBox(FVector(1000.0f), FVector(1500.0f));
	Attic->bRetainDescriptorOnUnload = true;
	Attic->RegisterComponent();
	UDocAcousticPortalComponent* Hatch = NewObject<UDocAcousticPortalComponent>(Level);
	Hatch->PortalId = TEXT("Hatch");
	Hatch->SpaceA = TEXT("Cellar");
	Hatch->SpaceB = TEXT("Attic");
	Hatch->bRetainDescriptorOnUnload = true;
	Hatch->RegisterComponent();
	Hatch->SetOpenness(0.5f);

	UDocAcousticSpaceComponent* Duplicate = NewObject<UDocAcousticSpaceComponent>(Level);
	Duplicate->SpaceId = TEXT("Cellar");
	Duplicate->BoundsBox = FBox(FVector(0.0f), FVector(10.0f));
	Duplicate->RegisterComponent();
	TestFalse(TEXT("Duplicate SpaceId component refused"), Duplicate->LastRegistrationError.IsEmpty());

	Hatch->DestroyComponent();
	Attic->DestroyComponent();
	TArray<FName> Retained = Subsystem->GetRetainedDescriptorIds();
	TestTrue(TEXT("Retained portal and space descriptors kept"), Retained.Contains(TEXT("Hatch")) && Retained.Contains(TEXT("Attic")));

	FDocAcousticPathQuery Q;
	Q.EmitterLocation = FVector(250.0f);
	Q.ListenerLocation = FVector(1250.0f);
	FDocAcousticPathResult R;
	Subsystem->QueryTransmission(Q, R);
	TestEqual(TEXT("Retained topology still routes"), R.Status, EDocAcousticPathStatus::Connected);

	UDocAcousticPortalComponent* Reloaded = NewObject<UDocAcousticPortalComponent>(Level);
	Reloaded->PortalId = TEXT("Hatch");
	Reloaded->SpaceA = TEXT("Cellar");
	Reloaded->SpaceB = TEXT("Attic");
	Reloaded->Openness = 1.0f;
	Reloaded->RegisterComponent();
	TestTrue(TEXT("Reloaded portal registered over its descriptor"), Reloaded->LastRegistrationError.IsEmpty());
	TestTrue(TEXT("Reloaded portal takes the retained openness"), FMath::IsNearlyEqual(Reloaded->Openness, 0.5f, 1e-4f));
	TestFalse(TEXT("Portal no longer listed as retained"), Subsystem->GetRetainedDescriptorIds().Contains(TEXT("Hatch")));

	Cellar->DestroyComponent();
	Subsystem->QueryTransmission(Q, R);
	TestEqual(TEXT("Removed (non-retained) space is unresolved"), R.Status, EDocAcousticPathStatus::Unresolved);

	return true;
}

// ACO-09: Doc.Acoustics.NativeEmitterPlayback
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsNativeEmitterPlaybackTest, FAutomationTestBase, "Doc.Acoustics.NativeEmitterPlayback", DocAcousticTests::Flags)
bool FDocAcousticsNativeEmitterPlaybackTest::RunTest(const FString& Parameters)
{
	// Logic-level evidence only: the reference adapter records applied parameters. Audible cooked evidence is a manual gate.
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();
	UDocAcousticReferencePlaybackAdapter* Adapter = DocAcousticTests::MakeAdapter(Subsystem);

	Subsystem->AddProgrammaticSpace(TEXT("Stage"), FBox(FVector(0.0f), FVector(500.0f)));
	Subsystem->AddProgrammaticSpace(TEXT("Lobby"), FBox(FVector(1000.0f), FVector(1500.0f)));
	Subsystem->AddProgrammaticPortal(TEXT("StageDoor"), TEXT("Stage"), TEXT("Lobby"), 1.0f, 0.05f, 0.9f, 1200.0f, 18000.0f, FVector(750.0f));

	Subsystem->UpdateListener(TEXT("HeroListener"), FVector(1250.0f));
	TestTrue(TEXT("Mix listener selected"), Subsystem->SetMixListener(TEXT("HeroListener")).IsSuccess());

	UDocAcousticEmitterComponent* Voice = DocAcousticTests::SpawnEmitter(TW, TEXT("Voice_NPC"), FVector(250.0f), 20.0f);
	UDocAcousticEmitterComponent* Unclaimed = DocAcousticTests::SpawnEmitter(TW, TEXT("Ambient"), FVector(300.0f), 20.0f);
	FGuid Claim;
	Subsystem->AcquireAcousticClaim(TEXT("Voice_NPC"), Claim);

	Subsystem->AdvanceSmoothing(0.1f);
	FDocAcousticEmitterState State;
	TestTrue(TEXT("Adapter has state for claimed emitter"), Adapter->GetEmitterState(TEXT("Voice_NPC"), State));
	TestTrue(TEXT("State is active"), State.bIsActive);
	TestTrue(TEXT("Applied gain = base * open door (0.9)"), FMath::IsNearlyEqual(State.AppliedGain, 0.9f, 1e-3f));
	TestTrue(TEXT("Applied cutoff = open door cutoff"), FMath::IsNearlyEqual(State.AppliedCutoffHz, 18000.0f, 1.0f));
	TestFalse(TEXT("Unclaimed emitter is never written"), Adapter->GetEmitterState(TEXT("Ambient"), State));
	TestEqual(TEXT("One evaluation"), Subsystem->GetLastUpdateStats().EvaluatedEmitters, 1);

	Subsystem->SetPortalOpenness(TEXT("StageDoor"), 0.0f);
	for (int32 i = 0; i < 40; ++i)
	{
		Subsystem->AdvanceSmoothing(0.05f);
	}
	Adapter->GetEmitterState(TEXT("Voice_NPC"), State);
	TestTrue(TEXT("Closed door applied to playback gain"), FMath::IsNearlyEqual(State.AppliedGain, 0.05f, 1e-2f));
	TestTrue(TEXT("Closed door applied to playback cutoff"), FMath::IsNearlyEqual(State.AppliedCutoffHz, 1200.0f, 20.0f));

	FDocAcousticPathResult Debug;
	TestTrue(TEXT("Debug path available for the pair"), Subsystem->GetDebugPath(TEXT("Voice_NPC"), TEXT("HeroListener"), Debug));
	TestTrue(TEXT("Debug path shows the gain stage"), Debug.PortalGains.Num() == 1 && FMath::IsNearlyEqual(Debug.PortalGains[0], 0.05f, 1e-3f));
	TestTrue(TEXT("Route distance goes through the door position"), FMath::IsNearlyEqual(Debug.TotalDistance, 2.0f * FVector::Dist(FVector(250.0f), FVector(750.0f)), 1.0f));

	// Teleport snaps instead of sweeping.
	Voice->GetOwner()->SetActorLocation(FVector(1250.0f)); // into the lobby, same space as the listener
	Subsystem->AdvanceSmoothing(0.01f);
	Adapter->GetEmitterState(TEXT("Voice_NPC"), State);
	TestTrue(TEXT("Teleport snapped to same-space gain"), FMath::IsNearlyEqual(State.AppliedGain, 1.0f, 1e-3f));
	TestTrue(TEXT("Unclaimed emitter untouched"), Unclaimed->CurrentEffectiveGain == 1.0f);

	return true;
}

// ACO-10: Doc.Acoustics.IsolatedBase
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAcousticsIsolatedBaseTest, FAutomationTestBase, "Doc.Acoustics.IsolatedBase", DocAcousticTests::Flags)
bool FDocAcousticsIsolatedBaseTest::RunTest(const FString& Parameters)
{
	// Standalone: no DocRegions, no DocAdaptiveAudio, component-authored topology only.
	FDocScopedTestWorld TW;
	UDocAcousticSpaceSubsystem* Subsystem = TW.World->GetSubsystem<UDocAcousticSpaceSubsystem>();
	TestNotNull(TEXT("Acoustic subsystem runs standalone"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	Subsystem->AddProgrammaticSpace(TEXT("IsolatedSpace"), FBox(FVector(-500.0f), FVector(500.0f)));
	TestEqual(TEXT("Resolved space in standalone test"), Subsystem->ResolveSpaceForLocation(FVector::ZeroVector), FName(TEXT("IsolatedSpace")));

	AActor* Level = DocAcousticTests::SpawnAt(TW, FVector::ZeroVector);
	UDocAcousticSpaceComponent* Kitchen = NewObject<UDocAcousticSpaceComponent>(Level);
	Kitchen->SpaceId = TEXT("Kitchen");
	Kitchen->BoundsBox = FBox(FVector(2000.0f), FVector(2500.0f));
	Kitchen->RegisterComponent();
	UDocAcousticSpaceComponent* Pantry = NewObject<UDocAcousticSpaceComponent>(Level);
	Pantry->SpaceId = TEXT("Pantry");
	Pantry->BoundsBox = FBox(FVector(3000.0f), FVector(3500.0f));
	Pantry->RegisterComponent();
	UDocAcousticPortalComponent* Door = NewObject<UDocAcousticPortalComponent>(Level);
	Door->PortalId = TEXT("PantryDoor");
	Door->SpaceA = TEXT("Kitchen");
	Door->SpaceB = TEXT("Pantry");
	Door->MaxTransmissionGain = 0.7f;
	Door->RegisterComponent();

	FDocAcousticPathQuery Q;
	Q.EmitterLocation = FVector(2250.0f);
	Q.ListenerLocation = FVector(3250.0f);
	FDocAcousticPathResult R;
	TestTrue(TEXT("Component-authored topology query"), Subsystem->QueryTransmission(Q, R).IsSuccess());
	TestTrue(TEXT("Self-registered components route"), R.Status == EDocAcousticPathStatus::Connected && FMath::IsNearlyEqual(R.TransmissionGain, 0.7f, 1e-3f));

	TArray<FString> Issues;
	TestTrue(TEXT("Clean topology validates"), Subsystem->ValidateTopology(Issues).IsSuccess());

	Door->SetPortalEnabled(false);
	Subsystem->QueryTransmission(Q, R);
	TestEqual(TEXT("Disabled portal blocks"), R.Status, EDocAcousticPathStatus::Blocked);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
