#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocPowerNetworkSubsystem.h"
#include "DocPowerNodeComponent.h"
#include "DocPowerSourceComponent.h"
#include "DocPowerConsumerComponent.h"
#include "DocPowerStorageComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Algo/Reverse.h"
#include <limits>

namespace DocPowerTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	AActor* SpawnTestActor(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	}

	// Components are created with auto-registration off so each test controls registration order explicitly.
	UDocPowerSourceComponent* AddSource(AActor* Actor, FName NodeId, double MaxWatts, bool bAuto = false)
	{
		UDocPowerSourceComponent* Comp = NewObject<UDocPowerSourceComponent>(Actor);
		Comp->NodeId = NodeId;
		Comp->MaxPowerWatts = MaxWatts;
		Comp->bExternal = true;
		Comp->Availability = 1.0;
		Comp->bAutoRegister = bAuto;
		Comp->RegisterComponent();
		return Comp;
	}

	UDocPowerConsumerComponent* AddConsumer(AActor* Actor, FName NodeId, double DesiredWatts, double MinWatts, int32 Priority = 0, int32 TieBreakId = 0, EDocPowerAllocationMode Mode = EDocPowerAllocationMode::Binary, bool bAuto = false)
	{
		UDocPowerConsumerComponent* Comp = NewObject<UDocPowerConsumerComponent>(Actor);
		Comp->NodeId = NodeId;
		Comp->DesiredPowerWatts = DesiredWatts;
		Comp->MinimumPowerWatts = MinWatts;
		Comp->Priority = Priority;
		Comp->TieBreakId = TieBreakId;
		Comp->AllocationMode = Mode;
		Comp->bAutoRegister = bAuto;
		Comp->RegisterComponent();
		return Comp;
	}

	UDocPowerStorageComponent* AddStorage(AActor* Actor, FName NodeId, double Capacity, double Energy, double MaxCharge, double MaxDischarge, double EtaC = 0.9, double EtaD = 0.9, bool bAuto = false)
	{
		UDocPowerStorageComponent* Comp = NewObject<UDocPowerStorageComponent>(Actor);
		Comp->NodeId = NodeId;
		Comp->CapacityJoules = Capacity;
		Comp->CurrentEnergyJoules = Energy;
		Comp->MaxChargeWatts = MaxCharge;
		Comp->MaxDischargeWatts = MaxDischarge;
		Comp->ChargeEfficiency = EtaC;
		Comp->DischargeEfficiency = EtaD;
		Comp->bAutoRegister = bAuto;
		Comp->RegisterComponent();
		return Comp;
	}

	FDocPowerPortId Main(FName NodeId) { return FDocPowerPortId(NodeId, TEXT("Main")); }

	double Supply(UDocPowerNetworkSubsystem* S, FName NodeId, EDocPowerConsumerState* OutState = nullptr)
	{
		double Watts = -1.0;
		EDocPowerConsumerState State = EDocPowerConsumerState::Disconnected;
		S->GetNodeSupply(NodeId, Watts, State);
		if (OutState)
		{
			*OutState = State;
		}
		return Watts;
	}

	double Stored(UDocPowerNetworkSubsystem* S, FName NodeId)
	{
		const FDocPowerNetworkSnapshot Snap = S->CaptureNetworkState();
		const double* E = Snap.StorageEnergies.Find(NodeId);
		return E ? *E : -1.0;
	}
}

// PWR-01: Doc.Power.ConnectivityIslands
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerConnectivityIslandsTest, FAutomationTestBase, "Doc.Power.ConnectivityIslands", DocPowerTests::Flags)
bool FDocPowerConnectivityIslandsTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	S->RegisterNode(AddSource(Actor, TEXT("Src1"), 500.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 200.0, 100.0));
	S->RegisterNode(AddSource(Actor, TEXT("Src2"), 500.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C2"), 200.0, 100.0));
	S->RegisterNodeRaw(TEXT("Sw1"), EDocPowerNodeKind::Switch, { TEXT("P1"), TEXT("P2") });
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Src1")), Main(TEXT("C1")));
	S->ConnectPorts(TEXT("E2"), Main(TEXT("C1")), FDocPowerPortId(TEXT("Sw1"), TEXT("P1")));
	S->ConnectPorts(TEXT("E3"), Main(TEXT("Src2")), Main(TEXT("C2")));
	S->ConnectPorts(TEXT("E4"), Main(TEXT("C2")), FDocPowerPortId(TEXT("Sw1"), TEXT("P2")));

	TArray<FDocPowerIslandState> Islands;
	S->GetAllIslands(Islands);
	TestEqual(TEXT("Single island when switch closed"), Islands.Num(), 1);
	TestEqual(TEXT("All 5 nodes in the island"), Islands.Num() == 1 ? Islands[0].NodeIds.Num() : 0, 5);

	// A stale edit is refused and changes nothing.
	const int64 Rev = S->GetTopologyRevision();
	TestEqual(TEXT("Stale switch edit refused"), S->SetSwitchState(TEXT("Sw1"), false, Rev - 1).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Switch unchanged"), S->GetSwitchState(TEXT("Sw1")));
	TestTrue(TEXT("Current-revision edit accepted"), S->SetSwitchState(TEXT("Sw1"), false, Rev).IsSuccess());

	S->GetAllIslands(Islands);
	TestEqual(TEXT("Two islands when switch opened"), Islands.Num(), 2);
	FDocPowerIslandState Island1, Island2;
	S->QueryIsland(TEXT("Src1"), Island1);
	S->QueryIsland(TEXT("Src2"), Island2);
	TestTrue(TEXT("Island1 holds Src1 and C1"), Island1.NodeIds.Contains(TEXT("Src1")) && Island1.NodeIds.Contains(TEXT("C1")));
	TestFalse(TEXT("Island1 does not hold Src2"), Island1.NodeIds.Contains(TEXT("Src2")));
	TestTrue(TEXT("Island2 holds Src2 and C2"), Island2.NodeIds.Contains(TEXT("Src2")) && Island2.NodeIds.Contains(TEXT("C2")));

	// Disconnecting one edge splits only its own island.
	S->DisconnectPorts(TEXT("E3"));
	S->GetAllIslands(Islands);
	TestEqual(TEXT("Three powered islands after disconnect"), Islands.Num(), 3);
	FDocPowerIslandState After1;
	S->QueryIsland(TEXT("Src1"), After1);
	TestTrue(TEXT("Unaffected island keeps its members"), After1.NodeIds == Island1.NodeIds);
	S->StepSimulation(1.0f);
	TestEqual(TEXT("C1 still powered"), Supply(S, TEXT("C1")), 200.0);
	EDocPowerConsumerState C2State;
	TestEqual(TEXT("C2 lost its source"), Supply(S, TEXT("C2"), &C2State), 0.0);
	TestEqual(TEXT("C2 is disconnected, not browned out"), C2State, EDocPowerConsumerState::Disconnected);
	return true;
}

// PWR-02: Doc.Power.PriorityAllocation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerPriorityAllocationTest, FAutomationTestBase, "Doc.Power.PriorityAllocation", DocPowerTests::Flags)
bool FDocPowerPriorityAllocationTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	auto RunScenario = [](bool bPermuted) -> TMap<FName, double>
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);

		UDocPowerSourceComponent* Src = AddSource(Actor, TEXT("Src"), 150.0);
		UDocPowerConsumerComponent* CHigh = AddConsumer(Actor, TEXT("C_High"), 100.0, 50.0, 10, 0);
		UDocPowerConsumerComponent* CMid1 = AddConsumer(Actor, TEXT("C_Mid1"), 50.0, 20.0, 5, 1);
		UDocPowerConsumerComponent* CMid2 = AddConsumer(Actor, TEXT("C_Mid2"), 50.0, 20.0, 5, 2);
		UDocPowerConsumerComponent* CLow = AddConsumer(Actor, TEXT("C_Low"), 50.0, 20.0, 1, 0);

		TArray<UDocPowerNodeComponent*> Order = { Src, CHigh, CMid1, CMid2, CLow };
		if (bPermuted)
		{
			Algo::Reverse(Order);
		}
		for (UDocPowerNodeComponent* Comp : Order)
		{
			S->RegisterNode(Comp);
		}
		TArray<FName> Ids = { TEXT("C_High"), TEXT("C_Mid1"), TEXT("C_Mid2"), TEXT("C_Low") };
		if (bPermuted)
		{
			Algo::Reverse(Ids);
		}
		for (const FName& Id : Ids)
		{
			S->ConnectPorts(*FString::Printf(TEXT("E_%s"), *Id.ToString()), Main(TEXT("Src")), Main(Id));
		}
		S->StepSimulation(1.0f);

		TMap<FName, double> Results;
		for (const FName& Id : Ids)
		{
			Results.Add(Id, Supply(S, Id));
		}
		return Results;
	};

	TMap<FName, double> A = RunScenario(false);
	TMap<FName, double> B = RunScenario(true);
	TestEqual(TEXT("C_High gets 100W"), A[TEXT("C_High")], 100.0);
	TestEqual(TEXT("C_Mid1 (lower tie-break) gets 50W"), A[TEXT("C_Mid1")], 50.0);
	TestEqual(TEXT("C_Mid2 starved"), A[TEXT("C_Mid2")], 0.0);
	TestEqual(TEXT("C_Low starved"), A[TEXT("C_Low")], 0.0);
	for (const TPair<FName, double>& Kvp : A)
	{
		TestEqual(*FString::Printf(TEXT("%s identical under reversed registration"), *Kvp.Key.ToString()), B[Kvp.Key], Kvp.Value);
	}
	return true;
}

// PWR-03: Doc.Power.BatteryConservation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerBatteryConservationTest, FAutomationTestBase, "Doc.Power.BatteryConservation", DocPowerTests::Flags)
bool FDocPowerBatteryConservationTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	S->RegisterNode(AddSource(Actor, TEXT("Src"), 500.0));
	S->RegisterNode(AddStorage(Actor, TEXT("Bat"), 2000.0, 500.0, 1000.0, 1000.0, 0.8, 0.8));
	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 100.0, 50.0));
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("Bat")));
	S->ConnectPorts(TEXT("E2"), Main(TEXT("Bat")), Main(TEXT("C1")));

	// Step 1: 100 W to the load, 400 W surplus charges at 0.8 -> +320 J.
	S->StepSimulation(1.0f);
	TestTrue(TEXT("Step 1 ledger balances"), S->GetLastLedger().IsBalanced(1e-6));
	TestTrue(TEXT("Load 100 J"), FMath::IsNearlyEqual(S->GetLastLedger().LoadEnergyDeliveredJoules, 100.0, 1e-6));
	TestTrue(TEXT("Accepted external energy is what was used (500 J), not potential"), FMath::IsNearlyEqual(S->GetLastLedger().ExternalEnergyAcceptedJoules, 500.0, 1e-6));
	TestTrue(TEXT("Battery 820 J"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), 820.0, 1e-6));

	// Step 2: no source; 100 J out at 0.8 draws 125 J.
	S->SetSourceAvailability(TEXT("Src"), 0.0);
	S->StepSimulation(1.0f);
	TestTrue(TEXT("Step 2 ledger balances"), S->GetLastLedger().IsBalanced(1e-6));
	TestTrue(TEXT("Load 100 J from battery"), FMath::IsNearlyEqual(S->GetLastLedger().LoadEnergyDeliveredJoules, 100.0, 1e-6));
	TestTrue(TEXT("Battery 695 J"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), 695.0, 1e-6));

	// Discharge is limited by stored energy before allocation: never a negative battery.
	FDocScopedTestWorld TW2;
	UDocPowerNetworkSubsystem* S2 = TW2.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor2 = SpawnTestActor(TW2.World);
	S2->RegisterNode(AddStorage(Actor2, TEXT("Low"), 1000.0, 50.0, 100.0, 1000.0, 0.5, 0.5));
	S2->RegisterNode(AddConsumer(Actor2, TEXT("Big"), 100.0, 0.0));
	S2->ConnectPorts(TEXT("E"), Main(TEXT("Low")), Main(TEXT("Big")));
	S2->StepSimulation(1.0f);
	TestEqual(TEXT("A binary load the battery cannot fully cover gets nothing"), Supply(S2, TEXT("Big")), 0.0);
	TestTrue(TEXT("Battery untouched"), FMath::IsNearlyEqual(Stored(S2, TEXT("Low")), 50.0, 1e-9));
	TestTrue(TEXT("Ledger balances"), S2->GetLastLedger().IsBalanced(1e-6));
	return true;
}

// PWR-04: Doc.Power.NoChargeDischargeLoop
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerNoChargeDischargeLoopTest, FAutomationTestBase, "Doc.Power.NoChargeDischargeLoop", DocPowerTests::Flags)
bool FDocPowerNoChargeDischargeLoopTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;

	// Batteries alone cannot charge each other.
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);
		S->RegisterNode(AddStorage(Actor, TEXT("Bat1"), 1000.0, 500.0, 500.0, 500.0));
		S->RegisterNode(AddStorage(Actor, TEXT("Bat2"), 1000.0, 200.0, 500.0, 500.0));
		S->ConnectPorts(TEXT("E1"), Main(TEXT("Bat1")), Main(TEXT("Bat2")));
		S->StepSimulation(1.0f);
		TestTrue(TEXT("Bat1 unchanged"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat1")), 500.0, 1e-9));
		TestTrue(TEXT("Bat2 unchanged"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat2")), 200.0, 1e-9));
		TestEqual(TEXT("No external energy accepted"), S->GetLastLedger().ExternalEnergyAcceptedJoules, 0.0);
	}

	// With a load and a shortfall: a discharging battery never charges, and the other gets nothing.
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);
		S->RegisterNode(AddSource(Actor, TEXT("Src"), 100.0));
		S->RegisterNode(AddStorage(Actor, TEXT("Bat1"), 1000.0, 500.0, 500.0, 500.0));
		S->RegisterNode(AddStorage(Actor, TEXT("Bat2"), 1000.0, 200.0, 500.0, 500.0));
		S->RegisterNode(AddConsumer(Actor, TEXT("C"), 100.0, 0.0));
		S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("Bat1")));
		S->ConnectPorts(TEXT("E2"), Main(TEXT("Bat1")), Main(TEXT("Bat2")));
		S->ConnectPorts(TEXT("E3"), Main(TEXT("Bat2")), Main(TEXT("C")));

		S->SetSourceAvailability(TEXT("Src"), 0.5); // 50 W external
		S->StepSimulation(1.0f);
		TestEqual(TEXT("Load fully supplied"), Supply(S, TEXT("C")), 100.0);
		TestTrue(TEXT("Bat1 (first in stable order) covers 50 W: -55.6 J"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat1")), 500.0 - 50.0 / 0.9, 1e-6));
		TestTrue(TEXT("Bat2 untouched"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat2")), 200.0, 1e-9));
		TestTrue(TEXT("Only the used 50 J of external energy accepted"), FMath::IsNearlyEqual(S->GetLastLedger().ExternalEnergyAcceptedJoules, 50.0, 1e-6));
		TestTrue(TEXT("Ledger balances"), S->GetLastLedger().IsBalanced(1e-6));

		// Bat1 charge-only: Bat2 discharges instead, Bat1 cannot be charged from it.
		const double Bat1Before = Stored(S, TEXT("Bat1"));
		S->SetStorageMode(TEXT("Bat1"), EDocPowerStorageMode::ChargeOnly);
		S->StepSimulation(1.0f);
		TestTrue(TEXT("Bat1 unchanged (no surplus to charge from)"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat1")), Bat1Before, 1e-9));
		TestTrue(TEXT("Bat2 discharged"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat2")), 200.0 - 50.0 / 0.9, 1e-6));
		TestTrue(TEXT("Ledger balances"), S->GetLastLedger().IsBalanced(1e-6));

		// Surplus only from the external source charges.
		S->SetConsumerDemand(TEXT("C"), 0.0, 0.0);
		S->StepSimulation(1.0f);
		TestTrue(TEXT("Bat1 charged from 50 W surplus at 0.9"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat1")), Bat1Before + 45.0, 1e-6));
		TestTrue(TEXT("Ledger balances"), S->GetLastLedger().IsBalanced(1e-6));
	}
	return true;
}

// PWR-05: Doc.Power.BinaryAndScalableLoads
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerBinaryAndScalableLoadsTest, FAutomationTestBase, "Doc.Power.BinaryAndScalableLoads", DocPowerTests::Flags)
bool FDocPowerBinaryAndScalableLoadsTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);
		S->RegisterNode(AddSource(Actor, TEXT("Src"), 120.0));
		S->RegisterNode(AddConsumer(Actor, TEXT("LBin1"), 100.0, 50.0, 10, 0, EDocPowerAllocationMode::Binary));
		S->RegisterNode(AddConsumer(Actor, TEXT("LBin2"), 50.0, 20.0, 5, 0, EDocPowerAllocationMode::Binary));
		S->RegisterNode(AddConsumer(Actor, TEXT("LScal"), 50.0, 10.0, 4, 0, EDocPowerAllocationMode::Scalable));
		S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("LBin1")));
		S->ConnectPorts(TEXT("E2"), Main(TEXT("Src")), Main(TEXT("LBin2")));
		S->ConnectPorts(TEXT("E3"), Main(TEXT("Src")), Main(TEXT("LScal")));
		S->StepSimulation(1.0f);

		EDocPowerConsumerState State;
		TestEqual(TEXT("LBin1 gets full 100W"), Supply(S, TEXT("LBin1"), &State), 100.0);
		TestEqual(TEXT("LBin1 Supplied"), State, EDocPowerConsumerState::Supplied);
		TestEqual(TEXT("LBin2 gets 0W (binary takes all or nothing)"), Supply(S, TEXT("LBin2"), &State), 0.0);
		TestEqual(TEXT("LBin2 Brownout"), State, EDocPowerConsumerState::Brownout);
		TestEqual(TEXT("LScal gets the remaining 20W"), Supply(S, TEXT("LScal"), &State), 20.0);
		TestEqual(TEXT("LScal Brownout"), State, EDocPowerConsumerState::Brownout);
	}

	// The minimum holds for any mix of sources: 5 W external + 10 W battery < 30 W minimum -> nothing.
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);
		S->RegisterNode(AddSource(Actor, TEXT("Src"), 5.0));
		S->RegisterNode(AddStorage(Actor, TEXT("Bat"), 1000.0, 1000.0, 100.0, 10.0));
		S->RegisterNode(AddConsumer(Actor, TEXT("Scal"), 50.0, 30.0, 0, 0, EDocPowerAllocationMode::Scalable));
		S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("Bat")));
		S->ConnectPorts(TEXT("E2"), Main(TEXT("Bat")), Main(TEXT("Scal")));
		S->StepSimulation(1.0f);
		EDocPowerConsumerState State;
		TestEqual(TEXT("Below-minimum supply is not delivered"), Supply(S, TEXT("Scal"), &State), 0.0);
		TestEqual(TEXT("Scalable load browned out"), State, EDocPowerConsumerState::Brownout);
		TestTrue(TEXT("Battery not drained for an unusable top-up"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), 1000.0, 1e-9));
	}

	// Recovery hysteresis: a browned-out load needs a margin to come back, then holds at its threshold.
	{
		FDocScopedTestWorld TW;
		UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
		AActor* Actor = SpawnTestActor(TW.World);
		S->RegisterNode(AddSource(Actor, TEXT("Src"), 200.0));
		S->RegisterNode(AddConsumer(Actor, TEXT("Motor"), 100.0, 0.0)); // hysteresis 0.05
		S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("Motor")));
		EDocPowerConsumerState State;

		S->SetSourceAvailability(TEXT("Src"), 0.45); // 90 W
		S->StepSimulation(0.1f);
		Supply(S, TEXT("Motor"), &State);
		TestEqual(TEXT("90 W: brownout"), State, EDocPowerConsumerState::Brownout);
		S->SetSourceAvailability(TEXT("Src"), 0.5); // 100 W: exactly the demand, below the 105 W recovery threshold
		S->StepSimulation(0.1f);
		Supply(S, TEXT("Motor"), &State);
		TestEqual(TEXT("100 W: stays off (no flicker at the threshold)"), State, EDocPowerConsumerState::Brownout);
		S->SetSourceAvailability(TEXT("Src"), 0.53); // 106 W
		S->StepSimulation(0.1f);
		Supply(S, TEXT("Motor"), &State);
		TestEqual(TEXT("106 W: recovers"), State, EDocPowerConsumerState::Supplied);
		S->SetSourceAvailability(TEXT("Src"), 0.5); // 100 W
		S->StepSimulation(0.1f);
		Supply(S, TEXT("Motor"), &State);
		TestEqual(TEXT("100 W: a running load stays on"), State, EDocPowerConsumerState::Supplied);
	}
	return true;
}

// PWR-06: Doc.Power.BreakerTripReset
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerBreakerTripResetTest, FAutomationTestBase, "Doc.Power.BreakerTripReset", DocPowerTests::Flags)
bool FDocPowerBreakerTripResetTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	// Src feeds C1 directly and C2 through a branch breaker.
	S->RegisterNode(AddSource(Actor, TEXT("Src"), 2000.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 1000.0, 0.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C2"), 100.0, 0.0));
	S->RegisterNodeRaw(TEXT("Brk"), EDocPowerNodeKind::Breaker, { TEXT("In"), TEXT("Out") });
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("C1")));
	S->ConnectPorts(TEXT("E2"), Main(TEXT("Src")), FDocPowerPortId(TEXT("Brk"), TEXT("In")));
	S->ConnectPorts(TEXT("E3"), FDocPowerPortId(TEXT("Brk"), TEXT("Out")), Main(TEXT("C2")));
	TestTrue(TEXT("Radial branch breaker accepted"), S->ConfigureBreaker(TEXT("Brk"), 200.0, 0.3, 1.0, { TEXT("C2") }).IsSuccess());

	int32 TripEvents = 0;
	S->OnBreakerTrippedNative.AddLambda([&TripEvents](FName BreakerId, const FString&) { if (BreakerId == TEXT("Brk")) { ++TripEvents; } });

	S->StepSimulation(0.1f);
	TestFalse(TEXT("Branch metric: C1's 1000 W outside the branch does not trip it"), S->IsBreakerTripped(TEXT("Brk")));

	// Overload must persist for the trip duration; a gap resets it.
	S->SetConsumerDemand(TEXT("C2"), 300.0, 0.0);
	S->StepSimulation(0.1f);
	S->StepSimulation(0.1f);
	TestFalse(TEXT("0.2 s of overload is below the 0.3 s trip duration"), S->IsBreakerTripped(TEXT("Brk")));
	TestEqual(TEXT("C2 still powered while the overload is timing"), Supply(S, TEXT("C2")), 300.0);
	S->SetConsumerDemand(TEXT("C2"), 100.0, 0.0);
	S->StepSimulation(0.1f);
	S->SetConsumerDemand(TEXT("C2"), 300.0, 0.0);
	S->StepSimulation(0.1f);
	S->StepSimulation(0.1f);
	TestFalse(TEXT("A break in the overload restarts the timer"), S->IsBreakerTripped(TEXT("Brk")));
	S->StepSimulation(0.1f);
	TestTrue(TEXT("Trips after 0.3 s of continuous overload"), S->IsBreakerTripped(TEXT("Brk")));
	TestEqual(TEXT("One trip event"), TripEvents, 1);
	TestEqual(TEXT("Protected branch loses power"), Supply(S, TEXT("C2")), 0.0);
	TestEqual(TEXT("The rest of the island keeps power"), Supply(S, TEXT("C1")), 1000.0);

	TestEqual(TEXT("Reset refused during cooldown"), S->RequestBreakerReset(TEXT("Brk")).Outcome, EDocResultOutcome::NotReady);
	S->StepSimulation(1.1f);
	TestTrue(TEXT("Reset after cooldown"), S->RequestBreakerReset(TEXT("Brk")).IsChanged());
	TestFalse(TEXT("No longer tripped"), S->IsBreakerTripped(TEXT("Brk")));

	// Island-level breaker with zero duration trips on the first overloaded step.
	FDocScopedTestWorld TW2;
	UDocPowerNetworkSubsystem* S2 = TW2.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor2 = SpawnTestActor(TW2.World);
	S2->RegisterNode(AddSource(Actor2, TEXT("Src"), 1000.0));
	S2->RegisterNode(AddConsumer(Actor2, TEXT("C"), 300.0, 0.0));
	S2->RegisterNodeRaw(TEXT("Main"), EDocPowerNodeKind::Breaker, { TEXT("In"), TEXT("Out") });
	S2->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), FDocPowerPortId(TEXT("Main"), TEXT("In")));
	S2->ConnectPorts(TEXT("E2"), FDocPowerPortId(TEXT("Main"), TEXT("Out")), Main(TEXT("C")));
	S2->ConfigureBreaker(TEXT("Main"), 200.0, 0.0, 1.0, {});
	S2->StepSimulation(0.1f);
	TestTrue(TEXT("Instant breaker trips"), S2->IsBreakerTripped(TEXT("Main")));
	return true;
}

// PWR-07: Doc.Power.UnsupportedProtection
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerUnsupportedProtectionTest, FAutomationTestBase, "Doc.Power.UnsupportedProtection", DocPowerTests::Flags)
bool FDocPowerUnsupportedProtectionTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	S->RegisterNode(AddSource(Actor, TEXT("Src"), 1000.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 300.0, 100.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C2"), 100.0, 0.0));
	S->RegisterNodeRaw(TEXT("BrkM"), EDocPowerNodeKind::Breaker, { TEXT("In"), TEXT("Out") });
	S->RegisterNodeRaw(TEXT("BrkR"), EDocPowerNodeKind::Breaker, { TEXT("In"), TEXT("Out") });

	// Declared meshed protection.
	const FDocSystemResult Declared = S->ConfigureBreaker(TEXT("BrkM"), 200.0, 0.0, 1.0, {}, /*bInMeshedProtection=*/true);
	TestEqual(TEXT("Declared meshed protection is Unsupported"), Declared.Outcome, EDocResultOutcome::Unsupported);
	TestTrue(TEXT("Diagnostic says unsupported"), Declared.Diagnostic.Contains(TEXT("unsupported")));

	// Detected mesh: C1 is fed both through BrkM and directly.
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), FDocPowerPortId(TEXT("BrkM"), TEXT("In")));
	S->ConnectPorts(TEXT("E2"), FDocPowerPortId(TEXT("BrkM"), TEXT("Out")), Main(TEXT("C1")));
	S->ConnectPorts(TEXT("E3"), Main(TEXT("Src")), Main(TEXT("C1")));
	TestEqual(TEXT("Branch reachable around the breaker is Unsupported"), S->ConfigureBreaker(TEXT("BrkM"), 200.0, 0.0, 1.0, { TEXT("C1") }).Outcome, EDocResultOutcome::Unsupported);

	// A radial branch is accepted; an edit that would mesh it is refused atomically.
	S->ConnectPorts(TEXT("E4"), Main(TEXT("Src")), FDocPowerPortId(TEXT("BrkR"), TEXT("In")));
	S->ConnectPorts(TEXT("E5"), FDocPowerPortId(TEXT("BrkR"), TEXT("Out")), Main(TEXT("C2")));
	TestTrue(TEXT("Radial branch accepted"), S->ConfigureBreaker(TEXT("BrkR"), 200.0, 0.0, 1.0, { TEXT("C2") }).IsSuccess());
	TestEqual(TEXT("Parallel feed around BrkR refused"), S->ConnectPorts(TEXT("E6"), Main(TEXT("Src")), Main(TEXT("C2"))).Outcome, EDocResultOutcome::Unsupported);
	TestFalse(TEXT("Refused edge not added"), S->HasEdge(TEXT("E6")));

	// The unsupported configuration did not poison the network: it still solves.
	TestTrue(TEXT("Network still steps"), S->StepSimulation(0.1f).IsSuccess());
	TestEqual(TEXT("C1 supplied"), Supply(S, TEXT("C1")), 300.0);
	TestEqual(TEXT("C2 supplied through BrkR"), Supply(S, TEXT("C2")), 100.0);
	return true;
}

// PWR-08: Doc.Power.TopologyDuringSolve
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerTopologyDuringSolveTest, FAutomationTestBase, "Doc.Power.TopologyDuringSolve", DocPowerTests::Flags)
bool FDocPowerTopologyDuringSolveTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	S->RegisterNode(AddSource(Actor, TEXT("Src"), 500.0));
	S->RegisterNode(AddStorage(Actor, TEXT("Bat"), 1000.0, 400.0, 100.0, 100.0));
	const FDocPowerNetworkSnapshot Snapshot = S->CaptureNetworkState();

	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 200.0, 100.0));
	TestTrue(TEXT("Topology revision advanced"), S->GetTopologyRevision() > Snapshot.TopologyRevision);
	const FDocSystemResult Restore = S->RestoreNetworkState(Snapshot, /*bForceMatchRevision=*/false);
	TestEqual(TEXT("Stale snapshot restore is a Conflict"), Restore.Outcome, EDocResultOutcome::Conflict);

	// A structural edit planned against an older revision is refused and not applied.
	TestEqual(TEXT("Stale edit refused"), S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("C1")), true, Snapshot.TopologyRevision).Outcome, EDocResultOutcome::Conflict);
	TestFalse(TEXT("Stale edge absent"), S->HasEdge(TEXT("E1")));

	// A refused step changes nothing.
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Bat")), Main(TEXT("C1")));
	const int64 Steps = S->GetStepOrdinal();
	TestFalse(TEXT("Non-finite step refused"), S->StepSimulation(std::numeric_limits<float>::quiet_NaN()).IsSuccess());
	TestEqual(TEXT("No step taken"), S->GetStepOrdinal(), Steps);
	TestTrue(TEXT("Battery untouched"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), 400.0, 1e-9));
	return true;
}

// PWR-09: Doc.Power.UnloadRestore
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerUnloadRestoreTest, FAutomationTestBase, "Doc.Power.UnloadRestore", DocPowerTests::Flags)
bool FDocPowerUnloadRestoreTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	// Auto-registered components, as placed in a streamed level.
	UDocPowerStorageComponent* Bat = AddStorage(Actor, TEXT("Bat"), 1000.0, 500.0, 200.0, 200.0, 0.9, 0.9, /*bAuto=*/true);
	AddConsumer(Actor, TEXT("Lamp"), 100.0, 0.0, 0, 0, EDocPowerAllocationMode::Binary, /*bAuto=*/true);
	if (!Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay(); // Begin play so the components auto-register as placed components do.
	}
	TestTrue(TEXT("Components registered themselves"), S->HasNode(TEXT("Bat")) && S->HasNode(TEXT("Lamp")));
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Bat")), Main(TEXT("Lamp")));
	S->StepSimulation(1.0f);
	const double AfterUse = 500.0 - 100.0 / 0.9;
	TestTrue(TEXT("Battery powered the lamp"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), AfterUse, 1e-6));

	// Stream unload: the record survives, the actor reference does not.
	TestTrue(TEXT("Battery component has begun play"), Bat->HasBegunPlay());
	if (!Bat->HasBegunPlay())
	{
		return false; // EndPlay below would assert.
	}
	Bat->EndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("Battery suspended, not deleted"), S->IsNodeSuspended(TEXT("Bat")));
	TestTrue(TEXT("Its edge survives"), S->HasEdge(TEXT("E1")));
	S->StepSimulation(1.0f);
	EDocPowerConsumerState LampState;
	TestEqual(TEXT("Lamp unpowered while the battery is unloaded"), Supply(S, TEXT("Lamp"), &LampState), 0.0);
	TestEqual(TEXT("Lamp disconnected"), LampState, EDocPowerConsumerState::Disconnected);
	TestTrue(TEXT("Suspended energy unchanged"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), AfterUse, 1e-6));

	// Stream reload: a fresh component re-binds and does not overwrite the retained energy.
	AddStorage(Actor, TEXT("Bat"), 1000.0, 999.0, 200.0, 200.0, 0.9, 0.9, /*bAuto=*/true);
	TestFalse(TEXT("Re-bound"), S->IsNodeSuspended(TEXT("Bat")));
	TestTrue(TEXT("Retained energy kept"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), AfterUse, 1e-6));
	S->StepSimulation(1.0f);
	TestEqual(TEXT("Lamp powered again through the retained edge"), Supply(S, TEXT("Lamp")), 100.0);

	// Breaker latch and cooldown survive capture/restore without extra ticks.
	S->RegisterNode(AddSource(Actor, TEXT("Gen"), 1000.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("Heater"), 300.0, 0.0));
	S->RegisterNodeRaw(TEXT("Brk"), EDocPowerNodeKind::Breaker, { TEXT("In"), TEXT("Out") });
	S->ConnectPorts(TEXT("E2"), Main(TEXT("Gen")), FDocPowerPortId(TEXT("Brk"), TEXT("In")));
	S->ConnectPorts(TEXT("E3"), FDocPowerPortId(TEXT("Brk"), TEXT("Out")), Main(TEXT("Heater")));
	S->ConfigureBreaker(TEXT("Brk"), 200.0, 0.0, 5.0, {});
	S->StepSimulation(0.1f);
	TestTrue(TEXT("Tripped"), S->IsBreakerTripped(TEXT("Brk")));
	const FDocPowerNetworkSnapshot Snapshot = S->CaptureNetworkState();

	S->StepSimulation(5.0f);
	TestTrue(TEXT("Reset after cooldown"), S->RequestBreakerReset(TEXT("Brk")).IsChanged());
	TestTrue(TEXT("Restore"), S->RestoreNetworkState(Snapshot, /*bForceMatchRevision=*/true).IsSuccess());
	TestTrue(TEXT("Latch restored"), S->IsBreakerTripped(TEXT("Brk")));
	TestEqual(TEXT("Cooldown restored"), S->RequestBreakerReset(TEXT("Brk")).Outcome, EDocResultOutcome::NotReady);
	TestEqual(TEXT("Step ordinal is the snapshot's: no extra ticks"), S->GetStepOrdinal(), Snapshot.StepOrdinal);
	TestTrue(TEXT("Battery energy restored"), FMath::IsNearlyEqual(Stored(S, TEXT("Bat")), Snapshot.StorageEnergies.FindRef(TEXT("Bat")), 1e-9));

	FDocPowerNetworkSnapshot Bad = Snapshot;
	Bad.StorageEnergies.Add(TEXT("Bat"), 5000.0);
	TestEqual(TEXT("Over-capacity restore refused"), S->RestoreNetworkState(Bad, true).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

// PWR-10: Doc.Power.HitchAndBounds
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPowerHitchAndBoundsTest, FAutomationTestBase, "Doc.Power.HitchAndBounds", DocPowerTests::Flags)
bool FDocPowerHitchAndBoundsTest::RunTest(const FString& Parameters)
{
	using namespace DocPowerTests;
	FDocScopedTestWorld TW;
	UDocPowerNetworkSubsystem* S = TW.GetSubsystem<UDocPowerNetworkSubsystem>();
	AActor* Actor = SpawnTestActor(TW.World);

	S->RegisterNode(AddSource(Actor, TEXT("Src"), 500.0));
	S->RegisterNode(AddConsumer(Actor, TEXT("C1"), 100.0, 50.0));
	S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("C1")));

	// Invalid numbers fail validation instead of being clamped.
	TestEqual(TEXT("Negative step"), S->StepSimulation(-1.0f).Outcome, EDocResultOutcome::InvalidInput);
	TestFalse(TEXT("Zero step"), S->StepSimulation(0.0f).IsSuccess());
	TestFalse(TEXT("NaN step"), S->StepSimulation(std::numeric_limits<float>::quiet_NaN()).IsSuccess());
	TestFalse(TEXT("Negative demand"), S->SetConsumerDemand(TEXT("C1"), -100.0, 50.0).IsSuccess());
	TestEqual(TEXT("Availability above 1"), S->SetSourceAvailability(TEXT("Src"), 1.5).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Zero efficiency"), S->RegisterNode(AddStorage(Actor, TEXT("BadBat"), 100.0, 10.0, 10.0, 10.0, 0.0, 0.9)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Energy above capacity"), S->RegisterNode(AddStorage(Actor, TEXT("BadBat2"), 100.0, 200.0, 10.0, 10.0)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Duplicate node id"), S->RegisterNodeRaw(TEXT("Src"), EDocPowerNodeKind::Switch, {}).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Duplicate edge id"), S->ConnectPorts(TEXT("E1"), Main(TEXT("Src")), Main(TEXT("C1"))).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Unknown port"), S->ConnectPorts(TEXT("E9"), FDocPowerPortId(TEXT("Src"), TEXT("Nope")), Main(TEXT("C1"))).Outcome, EDocResultOutcome::InvalidInput);

	// A 50 s hitch: at most 10 x 1 s steps now, the rest is visible lag, and the ledger covers exactly what ran.
	TestTrue(TEXT("Step limits"), S->SetStepLimits(1.0, 10).IsSuccess());
	const int64 Before = S->GetStepOrdinal();
	TestTrue(TEXT("Hitch handled"), S->StepSimulation(50.0f).IsSuccess());
	TestEqual(TEXT("Ten bounded steps"), S->GetStepOrdinal() - Before, (int64)10);
	TestTrue(TEXT("40 s of lag kept, not dropped"), FMath::IsNearlyEqual(S->GetSimulationLagSeconds(), 40.0, 1e-6));
	TestTrue(TEXT("Delivered energy matches simulated time (10 s x 100 W)"), FMath::IsNearlyEqual(S->GetLastLedger().LoadEnergyDeliveredJoules, 1000.0, 1e-6));
	TestTrue(TEXT("Ledger balanced"), S->GetLastLedger().IsBalanced(1e-6));
	S->StepSimulation(0.1f);
	TestTrue(TEXT("Lag drains within the budget"), FMath::IsNearlyEqual(S->GetSimulationLagSeconds(), 30.1, 1e-4));
	S->DiscardSimulationLag();
	TestEqual(TEXT("Explicit discard"), S->GetSimulationLagSeconds(), 0.0);

	// Extreme load: bounded and honest.
	S->SetConsumerDemand(TEXT("C1"), 1.0e12, 0.0);
	S->StepSimulation(0.1f);
	EDocPowerConsumerState State;
	TestEqual(TEXT("Impossible binary load gets nothing"), Supply(S, TEXT("C1"), &State), 0.0);
	TestEqual(TEXT("And is reported as brownout"), State, EDocPowerConsumerState::Brownout);
	TestTrue(TEXT("Ledger balanced"), S->GetLastLedger().IsBalanced(1e-6));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
