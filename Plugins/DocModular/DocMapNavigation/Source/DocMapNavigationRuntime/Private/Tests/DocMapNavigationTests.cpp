// DocMapNavigation automation tests (MAP-01..11 base logic). Local-player subsystems
// are created on bare ULocalPlayers; no UI, Events, Regions, Save or World Partition.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocMapMath.h"
#include "DocMapNavigationSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DocMapTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Quest, "Map.Marker.Objective.Quest");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Layer, "Map.Layer.Test");

	struct FPlayer
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocMapNavigationSubsystem> S;
		explicit FPlayer(UWorld* World, const FGuid& Subject = FGuid::NewGuid())
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			S.Reset(NewObject<UDocMapNavigationSubsystem>(Player.Get()));
			S->InitializeForTesting(World);
			S->SetOwnerScope(FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, Subject));
		}
	};

	UDocMapDefinition* MakeMap(FName Id = TEXT("World"))
	{
		UDocMapDefinition* Map = NewObject<UDocMapDefinition>(GetTransientPackage());
		Map->MapId = Id;
		Map->Transform.Origin = FVector(-50000, -50000, 0);
		Map->Transform.LengthU = 100000;
		Map->Transform.LengthV = 100000;
		Map->DiscoveryCellSize = 1000;
		return Map;
	}

	UDocMapMarkerDefinition* MakeMarkerDef(EDocMarkerLifetime Lifetime = EDocMarkerLifetime::ActorLifetime, int32 Priority = 0, const FGameplayTag& Tag = DocMapTags::Marker_Location)
	{
		UDocMapMarkerDefinition* Def = NewObject<UDocMapMarkerDefinition>(GetTransientPackage());
		Def->MarkerTag = Tag;
		Def->Lifetime = Lifetime;
		Def->Priority = Priority;
		return Def;
	}

	FDocMapFloorDefinition MakeFloor(FName Id, double Min, double Max, int32 Priority = 0)
	{
		FDocMapFloorDefinition F;
		F.FloorId = Id;
		F.MinZ = Min;
		F.MaxZ = Max;
		F.Priority = Priority;
		return F;
	}
}

using namespace DocMapTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapCoordinateTest, "Doc.Map.CoordinateConversion", Flags)
bool FDocMapCoordinateTest::RunTest(const FString& Parameters)
{
	FDocMapCoordinateTransform T;
	const double A = FMath::DegreesToRadians(30.0);
	T.U = FVector(FMath::Cos(A), FMath::Sin(A), 0);
	T.V = FVector(-FMath::Sin(A), FMath::Cos(A), 0);
	T.N = FVector(0, 0, 1);
	T.Origin = FVector(1234.5, -987.25, 50);
	T.LengthU = 20000;
	T.LengthV = 10000;
	const FVector World(5000.25, 3000.75, 777.5);
	const FDocMapPoint P = UDocMapMath::WorldToNormalized(T, World);
	TestTrue(TEXT("Valid"), P.IsValid());
	FVector Back;
	FString Error;
	TestTrue(TEXT("Inverse"), UDocMapMath::NormalizedToWorld(T, P.Normalized, P.Height, Back, Error));
	TestTrue(TEXT("Round trip with preserved height"), Back.Equals(World, 0.01));

	const FDocMapPoint Outside = UDocMapMath::WorldToNormalized(T, T.Origin - T.U * 5000.0);
	TestEqual(TEXT("Outside reported"), Outside.Status, EDocMapPointStatus::Outside);
	TestTrue(TEXT("Unclamped math kept"), Outside.Normalized.X < 0.0);
	TestEqual(TEXT("Display clamp separate"), Outside.DisplayClamped.X, 0.0);

	FDocMapCoordinateTransform Degenerate = T;
	Degenerate.V = Degenerate.U;
	TestFalse(TEXT("Degenerate basis rejected"), UDocMapMath::WorldToNormalized(Degenerate, World).IsValid());
	FDocMapCoordinateTransform Zero = T;
	Zero.LengthU = 0;
	TestFalse(TEXT("Zero extent rejected"), UDocMapMath::WorldToNormalized(Zero, World).IsValid());
	TestFalse(TEXT("NaN rejected"), UDocMapMath::WorldToNormalized(T, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)).IsValid());
	TestFalse(TEXT("NaN height rejected"), UDocMapMath::NormalizedToWorld(T, FVector2D(0.5, 0.5), std::numeric_limits<double>::quiet_NaN(), Back, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapRegistrationTest, "Doc.Map.MarkerRegistration", Flags)
bool FDocMapRegistrationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocMapMarkerRegistry* R = TW.GetSubsystem<UDocMapMarkerRegistry>();
	if (!TestNotNull(TEXT("Registry"), R)) { return false; }
	AActor* S1 = TW.Spawn<AActor>();
	AActor* S2 = TW.Spawn<AActor>();
	FDocRequestHandle H1, H2;
	TestTrue(TEXT("Register"), R->RegisterMarker(S1, TEXT("Shop"), FGuid(), TEXT("World"), MakeMarkerDef(), FVector(1, 2, 3), H1).IsSuccess());
	TestEqual(TEXT("Duplicate id from another source"), R->RegisterMarker(S2, TEXT("Shop"), FGuid(), TEXT("World"), MakeMarkerDef(), FVector::ZeroVector, H2).Outcome, EDocResultOutcome::Conflict);
	FDocMapMarkerState State;
	R->GetMarker(TEXT("Shop"), FGuid(), State);
	TestEqual(TEXT("Stale revision rejected"), R->UpdateMarker(H1, S1, FVector(9, 9, 9), 0.f, State.Revision + 5).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Expected revision accepted"), R->UpdateMarker(H1, S1, FVector(9, 9, 9), 0.f, State.Revision).IsSuccess());
	TestEqual(TEXT("Wrong source cannot update"), R->UpdateMarker(H1, S2, FVector::ZeroVector, 0.f).Outcome, EDocResultOutcome::NotFound);

	TestTrue(TEXT("Unregister"), R->UnregisterMarker(H1, S1).IsSuccess());
	TestEqual(TEXT("Repeated removal is NoChange"), R->UnregisterMarker(H1, S1).Outcome, EDocResultOutcome::NoChange);
	TestFalse(TEXT("ActorLifetime removed"), R->GetMarker(TEXT("Shop"), FGuid(), State));

	FDocRequestHandle HP, HL, HM;
	R->RegisterMarker(S1, TEXT("Camp"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::PersistentLocation), FVector(10, 0, 0), HP);
	R->RegisterMarker(S1, TEXT("Npc"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::LastKnownDynamic), FVector(20, 0, 0), HL);
	R->RegisterMarker(S1, TEXT("Convoy"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::ProviderManaged), FVector(30, 0, 0), HM);
	int32 Attention = 0;
	R->OnProviderAttentionNative.AddLambda([&Attention](const FDocMapMarkerState&, EDocMarkerChange) { ++Attention; });
	R->UnregisterMarker(HP, S1);
	R->UnregisterMarker(HL, S1);
	R->UnregisterMarker(HM, S1);
	TestTrue(TEXT("Persistent retained"), R->GetMarker(TEXT("Camp"), FGuid(), State) && !State.bStale && !State.bHasLiveSource);
	TestTrue(TEXT("LastKnown stale"), R->GetMarker(TEXT("Npc"), FGuid(), State) && State.bStale);
	TestTrue(TEXT("Provider-managed stale, not invented"), R->GetMarker(TEXT("Convoy"), FGuid(), State) && State.bStale && State.Location.Equals(FVector(30, 0, 0)));
	TestEqual(TEXT("Provider asked"), Attention, 1);
	TestTrue(TEXT("Provider updates"), R->ProviderUpdate(TEXT("Convoy"), FGuid(), FVector(40, 0, 0), false).IsSuccess());
	TestTrue(TEXT("Provider removes"), R->ProviderUpdate(TEXT("Convoy"), FGuid(), FVector::ZeroVector, true).IsSuccess());
	TestTrue(TEXT("Explicit persistent removal"), R->RemovePersistentMarker(TEXT("Camp"), FGuid()).IsSuccess());
	TestEqual(TEXT("Removal idempotent"), R->RemovePersistentMarker(TEXT("Camp"), FGuid()).Outcome, EDocResultOutcome::NoChange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapFilteringTest, "Doc.Map.MarkerFiltering", Flags)
bool FDocMapFilteringTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocMapMarkerRegistry* R = TW.GetSubsystem<UDocMapMarkerRegistry>();
	FPlayer P(TW.World);
	FPlayer Other(TW.World);
	P.S->RegisterMap(MakeMap());
	AActor* Src = TW.Spawn<AActor>();
	FDocRequestHandle H;
	R->RegisterMarker(Src, TEXT("B"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::ActorLifetime, 5), FVector(100, 0, 0), H);
	R->RegisterMarker(Src, TEXT("A"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::ActorLifetime, 5), FVector(200, 0, 0), H);
	R->RegisterMarker(Src, TEXT("Low"), FGuid(), TEXT("World"), MakeMarkerDef(EDocMarkerLifetime::ActorLifetime, 1, TAG_Quest), FVector(5000, 0, 0), H);
	UDocMapMarkerDefinition* Private = MakeMarkerDef();
	Private->Audience = EDocMarkerAudience::OwnerOnly;
	R->RegisterMarker(Src, TEXT("Mine"), FGuid(), TEXT("World"), Private, FVector::ZeroVector, H, P.S->GetOwnerScope());
	UDocMapMarkerDefinition* Layered = MakeMarkerDef();
	Layered->Layer = TAG_Layer;
	R->RegisterMarker(Src, TEXT("Layered"), FGuid(), TEXT("World"), Layered, FVector::ZeroVector, H);

	FDocMarkerQuery Q;
	Q.MapId = TEXT("World");
	TArray<FDocMapMarkerState> All = P.S->GetVisibleMarkers(Q);
	TestEqual(TEXT("Priority then stable id: A before B"), All.Num() >= 2 ? All[0].MarkerId : NAME_None, FName(TEXT("A")));
	TestEqual(TEXT("Second"), All.Num() >= 2 ? All[1].MarkerId : NAME_None, FName(TEXT("B")));
	TestTrue(TEXT("Owner sees private marker"), All.ContainsByPredicate([](const FDocMapMarkerState& M) { return M.MarkerId == TEXT("Mine"); }));
	TestFalse(TEXT("Audience enforced before the view"), Other.S->GetVisibleMarkers(Q).ContainsByPredicate([](const FDocMapMarkerState& M) { return M.MarkerId == TEXT("Mine"); }));

	TestEqual(TEXT("Hierarchical tag"), P.S->GetMarkersByTag(TEXT("World"), DocMapTags::Marker_Objective, false).Num(), 1);
	TestEqual(TEXT("Exact tag"), P.S->GetMarkersByTag(TEXT("World"), DocMapTags::Marker_Objective, true).Num(), 0);
	FDocMarkerQuery LayerQ = Q;
	LayerQ.Layers.AddTag(TAG_Layer);
	TestEqual(TEXT("Layer filter"), P.S->GetVisibleMarkers(LayerQ).Num(), 1);
	FDocMarkerQuery Near = Q;
	Near.bUseDistance = true;
	Near.MaxDistance = 1000;
	TestFalse(TEXT("Distance filter"), P.S->GetVisibleMarkers(Near).ContainsByPredicate([](const FDocMapMarkerState& M) { return M.MarkerId == TEXT("Low"); }));
	FDocMarkerQuery Bounded = Q;
	Bounded.MaxResults = 2;
	TestEqual(TEXT("Bounded result"), P.S->GetVisibleMarkers(Bounded).Num(), 2);
	P.S->SetMarkerHidden(TEXT("A"), true);
	TestEqual(TEXT("Per-player hidden"), P.S->GetVisibleMarkers(Bounded)[0].MarkerId, FName(TEXT("B")));
	FDocMapMarkerState Nearest;
	FDocMarkerQuery NearestQ = Q;
	NearestQ.Origin = FVector(4900, 0, 0);
	TestTrue(TEXT("Nearest"), P.S->GetNearestMarker(NearestQ, Nearest) && Nearest.MarkerId == TEXT("Low"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapFloorTest, "Doc.Map.MultiFloorResolution", Flags)
bool FDocMapFloorTest::RunTest(const FString& Parameters)
{
	const TArray<FDocMapFloorDefinition> Floors = { MakeFloor(TEXT("Ground"), 0, 400), MakeFloor(TEXT("Upper"), 400, 800), MakeFloor(TEXT("Mezz"), 600, 700, 5) };
	TestEqual(TEXT("Inside band"), UDocMapMath::ResolveFloor(Floors, 399.9, NAME_None, 0), FName(TEXT("Ground")));
	TestEqual(TEXT("Top boundary belongs above"), UDocMapMath::ResolveFloor(Floors, 400, NAME_None, 0), FName(TEXT("Upper")));
	TestEqual(TEXT("Overlap by priority"), UDocMapMath::ResolveFloor(Floors, 650, NAME_None, 0), FName(TEXT("Mezz")));
	TestEqual(TEXT("Hysteresis keeps floor"), UDocMapMath::ResolveFloor(Floors, 430, TEXT("Ground"), 50), FName(TEXT("Ground")));
	TestEqual(TEXT("Leaves after hysteresis"), UDocMapMath::ResolveFloor(Floors, 460, TEXT("Ground"), 50), FName(TEXT("Upper")));
	TestEqual(TEXT("No candidate: unknown"), UDocMapMath::ResolveFloor(Floors, -100, NAME_None, 0), NAME_None);

	FDocScopedTestWorld TW;
	FPlayer P(TW.World);
	UDocMapDefinition* Map = MakeMap();
	Map->Floors = Floors;
	P.S->RegisterMap(Map);
	TestEqual(TEXT("Automatic"), P.S->UpdateFloor(TEXT("World"), 100), FName(TEXT("Ground")));
	P.S->SetFloorOverride(TEXT("World"), TEXT("Upper"));
	TestEqual(TEXT("Explicit override"), P.S->UpdateFloor(TEXT("World"), 100), FName(TEXT("Upper")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapDiscoveryTest, "Doc.Map.Discovery", Flags)
bool FDocMapDiscoveryTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P1(TW.World);
	FPlayer P2(TW.World);
	UDocMapDefinition* Map = MakeMap();
	P1.S->RegisterMap(Map);
	P2.S->RegisterMap(Map);
	TestTrue(TEXT("Discover region"), P1.S->DiscoverLocation(TEXT("World"), TEXT("OldTown")).IsChanged());
	TestEqual(TEXT("Idempotent"), P1.S->DiscoverLocation(TEXT("World"), TEXT("OldTown")).Outcome, EDocResultOutcome::NoChange);
	TestFalse(TEXT("Owners independent"), P2.S->IsLocationDiscovered(TEXT("World"), TEXT("OldTown")));

	const int32 First = P1.S->RevealRadius(TEXT("World"), FVector(0, 0, 0), 3000);
	TestTrue(TEXT("Radius reveals cells"), First > 0);
	TestTrue(TEXT("Point revealed"), P1.S->IsPointRevealed(TEXT("World"), FVector(1500, 0, 0)));
	TestFalse(TEXT("Far point not revealed"), P1.S->IsPointRevealed(TEXT("World"), FVector(20000, 0, 0)));
	const int32 Overlap = P1.S->RevealRadius(TEXT("World"), FVector(1000, 0, 0), 3000);
	TestTrue(TEXT("Overlapping reveal adds only new cells"), Overlap < First);
	TestTrue(TEXT("Bounded memory: few chunks"), P1.S->GetAllocatedChunkCount(TEXT("World")) <= 4);
	TestFalse(TEXT("Other owner's fog untouched"), P2.S->IsPointRevealed(TEXT("World"), FVector(0, 0, 0)));

	TestTrue(TEXT("Undiscover region"), P1.S->UndiscoverLocation(TEXT("World"), TEXT("OldTown")).IsChanged());
	TestTrue(TEXT("Region undiscovery leaves radius coverage"), P1.S->IsPointRevealed(TEXT("World"), FVector(0, 0, 0)));
	TestTrue(TEXT("Explicit area reset"), P1.S->ResetRevealedArea(TEXT("World"), FVector(0, 0, 0), 1000) > 0);
	TestFalse(TEXT("Reset area hidden"), P1.S->IsPointRevealed(TEXT("World"), FVector(0, 0, 0)));
	TestTrue(TEXT("Outside the reset remains"), P1.S->IsPointRevealed(TEXT("World"), FVector(2500, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapPersistentMarkerTest, "Doc.Map.PersistentMarkerWithoutActor", Flags)
bool FDocMapPersistentMarkerTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocMapMarkerRegistry* R = TW.GetSubsystem<UDocMapMarkerRegistry>();
	FPlayer P(TW.World);
	P.S->RegisterMap(MakeMap());
	AActor* Chest = TW.Spawn<AActor>(FVector(700, 800, 0));
	UDocMapMarkerComponent* Marker = NewObject<UDocMapMarkerComponent>(Chest);
	Marker->Definition = MakeMarkerDef(EDocMarkerLifetime::PersistentLocation);
	Marker->MarkerId = TEXT("Chest");
	Marker->MapId = TEXT("World");
	Chest->SetRootComponent(Marker);
	Marker->RegisterComponent();
	Chest->SetActorLocation(FVector(700, 800, 0));
	if (!Chest->HasActorBegunPlay())
	{
		Chest->DispatchBeginPlay();
	}
	TestTrue(TEXT("Registered"), Marker->GetRegistration().IsSet() || Marker->GetRegistrationResult().IsSuccess());
	Chest->Destroy(); // unload
	FDocMarkerQuery Q;
	Q.MapId = TEXT("World");
	const TArray<FDocMapMarkerState> Visible = P.S->GetVisibleMarkers(Q);
	const FDocMapMarkerState* Found = Visible.FindByPredicate([](const FDocMapMarkerState& M) { return M.MarkerId == TEXT("Chest"); });
	TestNotNull(TEXT("Descriptor survives unload"), Found);
	TestTrue(TEXT("No live source (no stale actor access)"), Found && !Found->bHasLiveSource);
	TestEqual(TEXT("Registry keeps one record"), R->Num(), 1);

	// Repeated level placements do not collide.
	FDocRequestHandle H1, H2;
	AActor* Src = TW.Spawn<AActor>();
	TestTrue(TEXT("Instance 1"), R->RegisterMarker(Src, TEXT("Door"), FGuid::NewGuid(), TEXT("World"), MakeMarkerDef(), FVector::ZeroVector, H1).IsSuccess());
	TestTrue(TEXT("Instance 2"), R->RegisterMarker(Src, TEXT("Door"), FGuid::NewGuid(), TEXT("World"), MakeMarkerDef(), FVector::ZeroVector, H2).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapCompassTest, "Doc.Map.CompassWrap", Flags)
bool FDocMapCompassTest::RunTest(const FString& Parameters)
{
	FDocCompassEntry E;
	// Target at world yaw -170 (just past the ±180 seam), heading 170: +20 to the right.
	const FVector Target = FVector(FMath::Cos(FMath::DegreesToRadians(-170.0)), FMath::Sin(FMath::DegreesToRadians(-170.0)), 0) * 1000.0;
	TestTrue(TEXT("Compute"), UDocMapMath::ComputeBearing(FVector::ZeroVector, 170.f, Target, 90.f, E));
	TestTrue(TEXT("Wrap across ±180"), FMath::IsNearlyEqual(E.RelativeBearing, 20.f, 0.01f));
	TestTrue(TEXT("Arbitrary north"), FMath::IsNearlyEqual(E.CompassBearing, 100.f, 0.01f));
	TestTrue(TEXT("Behind is +180 not -180"), UDocMapMath::ComputeBearing(FVector::ZeroVector, 0.f, FVector(-100, 0, 0), 0.f, E) && FMath::IsNearlyEqual(E.RelativeBearing, 180.f));
	TestTrue(TEXT("Same position"), UDocMapMath::ComputeBearing(FVector(5, 5, 0), 0.f, FVector(5, 5, 100), 0.f, E) && E.bSamePosition);
	TestFalse(TEXT("Invalid heading"), UDocMapMath::ComputeBearing(FVector::ZeroVector, std::numeric_limits<float>::quiet_NaN(), Target, 0.f, E));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapWidgetTest, "Doc.Map.WidgetTransform", Flags)
bool FDocMapWidgetTest::RunTest(const FString& Parameters)
{
	FDocMapViewGeometry G;
	G.WidgetSize = FVector2D(800, 600);
	G.DPIScale = 1.5f;
	G.Zoom = 2.5f;
	G.Center = FVector2D(0.3, 0.6);
	G.RotationDegrees = 37.f;
	for (const FVector2D& N : { FVector2D(0.3, 0.6), FVector2D(0.1, 0.9), FVector2D(-0.2, 1.3) })
	{
		FVector2D W, Back;
		TestTrue(TEXT("Forward"), UDocMapMath::MapToWidget(G, N, W));
		TestTrue(TEXT("Inverse"), UDocMapMath::WidgetToMap(G, W, Back));
		TestTrue(TEXT("Round trip"), Back.Equals(N, 1.e-6));
	}
	FVector2D CenterW;
	UDocMapMath::MapToWidget(G, G.Center, CenterW);
	TestTrue(TEXT("Centre maps to widget centre (DPI applied)"), CenterW.Equals(G.WidgetSize * 0.5 * G.DPIScale, 1.e-6));
	FDocMapViewGeometry Bad = G;
	Bad.WidgetSize = FVector2D::ZeroVector;
	FVector2D Out;
	TestFalse(TEXT("Invalid geometry rejected"), UDocMapMath::MapToWidget(Bad, FVector2D(0.5, 0.5), Out));
	Bad = G;
	Bad.Zoom = 0.f;
	TestFalse(TEXT("Invalid zoom rejected"), UDocMapMath::WidgetToMap(Bad, FVector2D(1, 1), Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapSaveTest, "Doc.Map.SaveRestore", Flags)
bool FDocMapSaveTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	const FGuid Subject = FGuid::NewGuid();
	FPlayer P(TW.World, Subject);
	UDocMapDefinition* Map = MakeMap();
	P.S->RegisterMap(Map);
	P.S->DiscoverLocation(TEXT("World"), TEXT("Harbor"));
	P.S->RevealRadius(TEXT("World"), FVector(0, 0, 0), 2500);
	P.S->SetWaypoint(TEXT("World"), FVector(10, 20, 30), TEXT("Upper"));
	P.S->SetMarkerTracked(TEXT("Quest1"), true);
	const TArray<uint8> Saved = P.S->CaptureState();

	FPlayer Fresh(TW.World, Subject);
	Fresh.S->RegisterMap(Map);
	TestTrue(TEXT("Restore"), Fresh.S->RestoreState(Saved).IsSuccess());
	TestTrue(TEXT("Region restored"), Fresh.S->IsLocationDiscovered(TEXT("World"), TEXT("Harbor")));
	TestTrue(TEXT("Coverage restored"), Fresh.S->IsPointRevealed(TEXT("World"), FVector(0, 0, 0)));
	FVector W;
	FName Floor;
	TestTrue(TEXT("Waypoint restored"), Fresh.S->GetWaypoint(TEXT("World"), W, Floor) && W.Equals(FVector(10, 20, 30)) && Floor == TEXT("Upper"));
	TestTrue(TEXT("Tracking restored"), Fresh.S->IsMarkerTracked(TEXT("Quest1")));
	TestEqual(TEXT("Restore is not a new discovery"), Fresh.S->DiscoverLocation(TEXT("World"), TEXT("Harbor")).Outcome, EDocResultOutcome::NoChange);

	FPlayer Stranger(TW.World);
	Stranger.S->RegisterMap(Map);
	TestEqual(TEXT("Other owner's state refused"), Stranger.S->RestoreState(Saved).Outcome, EDocResultOutcome::PermissionDenied);

	UDocMapDefinition* Changed = MakeMap();
	Changed->DiscoveryCellSize = 500;
	FPlayer Migrating(TW.World, Subject);
	Migrating.S->RegisterMap(Changed);
	TestEqual(TEXT("Fog schema change needs confirmation"), Migrating.S->RestoreState(Saved).Outcome, EDocResultOutcome::InvalidConfiguration);
	TestFalse(TEXT("Nothing applied on refusal"), Migrating.S->IsLocationDiscovered(TEXT("World"), TEXT("Harbor")));
	TestTrue(TEXT("Explicit reset accepted"), Migrating.S->RestoreState(Saved, /*bDiscardIncompatibleFog*/ true).IsSuccess());
	TestTrue(TEXT("Regions kept on fog reset"), Migrating.S->IsLocationDiscovered(TEXT("World"), TEXT("Harbor")));
	TArray<uint8> Garbage = { 1, 2, 3 };
	TestFalse(TEXT("Corrupt state rejected"), Fresh.S->RestoreState(Garbage).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocMapOffscreenTest, "Doc.Map.OffscreenAndWaypoint", Flags)
bool FDocMapOffscreenTest::RunTest(const FString& Parameters)
{
	FDocMapViewProjection View;
	View.ViewportSize = FVector2D(1000, 500);
	View.SafeMin = FVector2D(50, 25);
	View.SafeMax = FVector2D(950, 475);
	FDocOffscreenIndicator I;
	TestTrue(TEXT("Ahead"), UDocMapMath::ProjectIndicator(View, FVector(1000, 0, 0), I));
	TestTrue(TEXT("On screen at centre"), I.bOnScreen && I.ScreenPosition.Equals(FVector2D(500, 250), 0.5));
	UDocMapMath::ProjectIndicator(View, FVector(100, 1000, 0), I);
	TestTrue(TEXT("Right edge, clamped to safe area"), !I.bOnScreen && FMath::IsNearlyEqual(I.ScreenPosition.X, 950.0, 0.5));
	UDocMapMath::ProjectIndicator(View, FVector(-1000, 10, 0), I);
	TestTrue(TEXT("Behind camera flagged"), I.bBehindCamera && !I.bOnScreen);
	FDocMapViewProjection Zero = View;
	Zero.ViewportSize = FVector2D::ZeroVector;
	TestFalse(TEXT("Zero viewport rejected"), UDocMapMath::ProjectIndicator(Zero, FVector(1000, 0, 0), I));

	FDocScopedTestWorld TW;
	FPlayer P1(TW.World);
	FPlayer P2(TW.World);
	P1.S->SetWaypoint(TEXT("World"), FVector(1, 2, 3), NAME_None);
	FVector W;
	FName F;
	TestTrue(TEXT("Owner waypoint"), P1.S->GetWaypoint(TEXT("World"), W, F));
	TestFalse(TEXT("Waypoints are owner-local"), P2.S->GetWaypoint(TEXT("World"), W, F));
	P1.S->ClearWaypoint(TEXT("World"));
	TestFalse(TEXT("Cleared"), P1.S->GetWaypoint(TEXT("World"), W, F));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
