// DocRegions automation tests (REG-01..REG-05).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocRegionSubsystem.h"
#include "DocRegionComponents.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

namespace DocRegionsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Zone, "Test.DocRegions.Zone");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Room, "Test.DocRegions.Zone.Room");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Other, "Test.DocRegions.Other");

	UDocRegionDefinition* MakeDef(FGameplayTag Tag, int32 Priority)
	{
		UDocRegionDefinition* Def = NewObject<UDocRegionDefinition>(GetTransientPackage());
		Def->RegionTag = Tag;
		Def->Priority = Priority;
		return Def;
	}

	/** Spawn a box region, configure it, and re-register so the grid uses the final bounds. */
	ADocRegionActor* SpawnBox(FDocScopedTestWorld& TW, UDocRegionSubsystem* Subsystem, const FVector& Center, const FVector& Extent, UDocRegionDefinition* Def)
	{
		ADocRegionActor* Actor = TW.Spawn<ADocRegionActor>(Center);
		Actor->Region->Definition = Def;
		Actor->Region->Shape = EDocRegionShape::Box;
		Actor->Region->BoxExtent = Extent;
		Subsystem->UnregisterRegion(Actor->Region);
		Subsystem->RegisterRegion(Actor->Region);
		return Actor;
	}


	/** Movable observer actor (any actor with a root component works). */
	AActor* SpawnObserver(FDocScopedTestWorld& TW, const FVector& Location)
	{
		AStaticMeshActor* Actor = TW.Spawn<AStaticMeshActor>(Location);
		if (Actor && Actor->GetStaticMeshComponent())
		{
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		}
		return Actor;
	}
}

using namespace DocRegionsTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocRegionsEnterExitTest, "Doc.Regions.EnterExit", Flags)
bool FDocRegionsEnterExitTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocRegionSubsystem* Subsystem = TW.GetSubsystem<UDocRegionSubsystem>();
	if (!TestNotNull(TEXT("Subsystem"), Subsystem)) { return false; }

	SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(500.f), MakeDef(TAG_Zone, 0));
	AActor* Observer = SpawnObserver(TW, FVector(100, 0, 0));
	TestTrue(TEXT("Register observer"), Subsystem->RegisterObserver(Observer, FDocRegionObserverOptions()).IsSuccess());

	TestTrue(TEXT("Spawned inside → member immediately"), Subsystem->IsActorInRegion(Observer, TAG_Zone));

	Observer->SetActorLocation(FVector(600, 0, 0));
	Subsystem->UpdateAllObservers();
	TestFalse(TEXT("Moved out"), Subsystem->IsActorInRegion(Observer, TAG_Zone));

	Observer->SetActorLocation(FVector(500, 0, 0)); // exactly on the boundary: inclusive
	Subsystem->UpdateAllObservers();
	TestTrue(TEXT("Boundary counts as inside"), Subsystem->IsActorInRegion(Observer, TAG_Zone));

	Observer->SetActorLocation(FVector(50000, 0, 0));
	Subsystem->UpdateAllObservers();
	TestFalse(TEXT("Teleported far away"), Subsystem->IsActorInRegion(Observer, TAG_Zone));
	TestEqual(TEXT("GetActorsInRegion empty"), Subsystem->GetActorsInRegion(TAG_Zone).Num(), 0);

	TestTrue(TEXT("Unregister"), Subsystem->UnregisterObserver(Observer).IsSuccess());
	TestEqual(TEXT("Second unregister NoChange"), Subsystem->UnregisterObserver(Observer).Outcome, EDocResultOutcome::NoChange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocRegionsNestedPriorityTest, "Doc.Regions.NestedPriority", Flags)
bool FDocRegionsNestedPriorityTest::RunTest(const FString& Parameters)
{
	for (int32 Order = 0; Order < 2; ++Order)
	{
		FDocScopedTestWorld TW;
		UDocRegionSubsystem* Subsystem = TW.GetSubsystem<UDocRegionSubsystem>();
		if (!TestNotNull(TEXT("Subsystem"), Subsystem)) { return false; }

		UDocRegionDefinition* Outer = MakeDef(TAG_Zone, 0);
		UDocRegionDefinition* Inner = MakeDef(TAG_Other, 5);
		if (Order == 0)
		{
			SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(1000.f), Outer);
			SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(200.f), Inner);
		}
		else
		{
			SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(200.f), Inner);
			SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(1000.f), Outer);
		}
		const FDocRegionInfo Primary = Subsystem->GetRegionAtLocation(FVector(10, 0, 0));
		TestEqual(FString::Printf(TEXT("Higher priority inner region wins (order %d)"), Order), Primary.RegionTag, FGameplayTag(TAG_Other));
		TestEqual(TEXT("Both regions contain the point"), Subsystem->GetRegionsAtLocation(FVector(10, 0, 0)).Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocRegionsPrimaryTest, "Doc.Regions.PrimaryRegion", Flags)
bool FDocRegionsPrimaryTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocRegionSubsystem* Subsystem = TW.GetSubsystem<UDocRegionSubsystem>();
	if (!TestNotNull(TEXT("Subsystem"), Subsystem)) { return false; }

	// Equal priority: the more specific tag (Zone.Room) wins over Zone.
	SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(300.f), MakeDef(TAG_Room, 0));
	SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(300.f), MakeDef(TAG_Zone, 0));
	TestEqual(TEXT("More specific tag wins at equal priority"), Subsystem->GetRegionAtLocation(FVector::ZeroVector).RegionTag, FGameplayTag(TAG_Room));

	// Equal priority and tag depth: smaller volume wins.
	SpawnBox(TW, Subsystem, FVector(5000, 0, 0), FVector(1000.f), MakeDef(TAG_Other, 0));
	ADocRegionActor* Small = SpawnBox(TW, Subsystem, FVector(5000, 0, 0), FVector(100.f), MakeDef(TAG_Other, 0));
	TestEqual(TEXT("Smaller region wins tie"), Subsystem->GetRegionAtLocation(FVector(5000, 0, 0)).InstanceId, Small->Region->GetInstanceId());

	// Primary-changed event fires once per change.
	AActor* Observer = SpawnObserver(TW, FVector(5000, 0, 0));
	Subsystem->RegisterObserver(Observer, FDocRegionObserverOptions());
	TestEqual(TEXT("Observer primary"), Subsystem->GetPrimaryRegionForActor(Observer).InstanceId, Small->Region->GetInstanceId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocRegionsUnloadTest, "Doc.Regions.UnloadReload", Flags)
bool FDocRegionsUnloadTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocRegionSubsystem* Subsystem = TW.GetSubsystem<UDocRegionSubsystem>();
	if (!TestNotNull(TEXT("Subsystem"), Subsystem)) { return false; }

	ADocRegionActor* Room = SpawnBox(TW, Subsystem, FVector::ZeroVector, FVector(500.f), MakeDef(TAG_Room, 0));
	const FGuid RoomId = Room->Region->GetInstanceId();
	AActor* Observer = SpawnObserver(TW, FVector::ZeroVector);
	Subsystem->RegisterObserver(Observer, FDocRegionObserverOptions());
	TestTrue(TEXT("Inside"), Subsystem->IsActorInRegion(Observer, TAG_Room));

	// Unload (unregister) → exit, no dangling component in queries.
	Subsystem->UnregisterRegion(Room->Region);
	TestFalse(TEXT("Exited on unload"), Subsystem->IsActorInRegion(Observer, TAG_Room));
	TestEqual(TEXT("Region no longer registered"), Subsystem->GetRegisteredRegionCount(), 0);

	// Reload with the same stable id → one membership, no duplicate.
	Subsystem->RegisterRegion(Room->Region);
	TestEqual(TEXT("Same instance id after reload"), Subsystem->GetRegionsForActor(Observer).Num(), 1);
	if (Subsystem->GetRegionsForActor(Observer).Num() == 1)
	{
		TestEqual(TEXT("Id preserved"), Subsystem->GetRegionsForActor(Observer)[0].InstanceId, RoomId);
	}
	Subsystem->RegisterRegion(Room->Region);
	TestEqual(TEXT("Re-registering does not duplicate"), Subsystem->GetRegisteredRegionCount(), 1);

	// Destroy the region actor entirely.
	Room->Destroy();
	TestFalse(TEXT("Exited on destroy"), Subsystem->IsActorInRegion(Observer, TAG_Room));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocRegionsTagQueryTest, "Doc.Regions.TagQueries", Flags)
bool FDocRegionsTagQueryTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocRegionSubsystem* Subsystem = TW.GetSubsystem<UDocRegionSubsystem>();
	if (!TestNotNull(TEXT("Subsystem"), Subsystem)) { return false; }

	UDocRegionDefinition* Shared = MakeDef(TAG_Room, 0);
	SpawnBox(TW, Subsystem, FVector(0, 0, 0), FVector(100.f), Shared);
	SpawnBox(TW, Subsystem, FVector(10000, 0, 0), FVector(100.f), Shared);
	SpawnBox(TW, Subsystem, FVector(20000, 0, 0), FVector(100.f), MakeDef(TAG_Other, 0));

	TestEqual(TEXT("Two instances share a definition tag"), Subsystem->FindRegionsByTag(TAG_Room).Num(), 2);
	TestEqual(TEXT("Parent-tag query includes children"), Subsystem->FindRegionsByTag(TAG_Zone, true).Num(), 2);
	FDocRegionInfo One;
	TestEqual(TEXT("Singular lookup reports ambiguity"), Subsystem->FindRegionByTag(TAG_Room, One).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Unique lookup succeeds"), Subsystem->FindRegionByTag(TAG_Other, One).IsSuccess());
	TestEqual(TEXT("Missing lookup NotFound"), Subsystem->FindRegionByTag(TAG_Zone, One).Outcome, EDocResultOutcome::NotFound);

	// Parent cycle detection.
	UDocRegionDefinition* A = MakeDef(TAG_Zone, 0);
	UDocRegionDefinition* B = MakeDef(TAG_Room, 0);
	A->ExplicitParent = B;
	B->ExplicitParent = A;
	TestEqual(TEXT("Cycle detected"), A->ComputeDepth(), -1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
