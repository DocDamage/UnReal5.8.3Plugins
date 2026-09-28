#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocFluidTypes.h"
#include "DocFluidComponents.h"
#include "DocFluidNetworkSubsystem.h"
#include "Algo/Reverse.h"
#include "GameFramework/Actor.h"

namespace DocFluidTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		FDocScopedTestWorld ScopedWorld;
		UDocFluidNetworkSubsystem* Subsystem = nullptr;

		FFixture()
		{
			UWorld* World = ScopedWorld.World;
			check(World);
			Subsystem = World->GetSubsystem<UDocFluidNetworkSubsystem>();
			check(Subsystem);
			Subsystem->ResetLedger();
		}

		FDocFluidReservoirState CreateReservoir(FName Id, float Capacity, float InitialVolume)
		{
			FDocFluidReservoirState Res;
			Res.ReservoirId = Id;
			Res.Capacity = Capacity;
			Res.CurrentVolume = InitialVolume;
			Subsystem->RegisterReservoir(Res);
			return Res;
		}

		FDocFluidEdge CreateEdge(FName EdgeId, FName Src, FName Dst, float MaxFlowRate = 10.0f, float ValveOpening = 1.0f, bool bPumpRequired = false, bool bPumpEnabled = false)
		{
			FDocFluidEdge Edge;
			Edge.EdgeId = EdgeId;
			Edge.SourceReservoirId = Src;
			Edge.DestinationReservoirId = Dst;
			Edge.MaxFlowRate = MaxFlowRate;
			Edge.ValveOpening = ValveOpening;
			Edge.bPumpRequired = bPumpRequired;
			Edge.bPumpEnabled = bPumpEnabled;
			Subsystem->RegisterConnection(Edge);
			return Edge;
		}
	};
}

// FLU-01: Source loss equals destination gain under the declared units
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidSingleTransferTest, FAutomationTestBase, "Doc.Fluid.SingleTransfer", DocFluidTests::Flags)
bool FDocFluidSingleTransferTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
	Fix.CreateReservoir(TEXT("TankB"), 100.0f, 0.0f);
	Fix.CreateEdge(TEXT("PipeAB"), TEXT("TankA"), TEXT("TankB"), 10.0f, 1.0f);

	const double InitialTotal = Fix.Subsystem->GetTotalSystemVolume();
	TestEqual(TEXT("Initial total volume is 50L"), InitialTotal, 50.0);

	// Step simulation by 1.0s (Max flow rate is 10L/s)
	Fix.Subsystem->StepSimulation(1.0f);

	FDocFluidReservoirState StateA, StateB;
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);

	TestEqual(TEXT("TankA lost 10L"), StateA.CurrentVolume, 40.0f);
	TestEqual(TEXT("TankB gained 10L"), StateB.CurrentVolume, 10.0f);

	const double FinalTotal = Fix.Subsystem->GetTotalSystemVolume();
	TestEqual(TEXT("Total volume strictly conserved"), FinalTotal, InitialTotal);
	TestTrue(TEXT("Ledger confirms exact conservation"), Fix.Subsystem->GetCumulativeLedger().VerifyConservation(InitialTotal, FinalTotal));

	return true;
}

// FLU-02: Multiple edges cannot overdraft one reservoir
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidCompetingOutflowsTest, FAutomationTestBase, "Doc.Fluid.CompetingOutflows", DocFluidTests::Flags)
bool FDocFluidCompetingOutflowsTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	// TankA has only 10L available
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 10.0f);
	Fix.CreateReservoir(TEXT("TankB"), 100.0f, 0.0f);
	Fix.CreateReservoir(TEXT("TankC"), 100.0f, 0.0f);

	// Two pipes each requesting 10L/s (total 20L requested over 1s)
	Fix.CreateEdge(TEXT("PipeAB"), TEXT("TankA"), TEXT("TankB"), 10.0f, 1.0f);
	Fix.CreateEdge(TEXT("PipeAC"), TEXT("TankA"), TEXT("TankC"), 10.0f, 1.0f);

	Fix.Subsystem->StepSimulation(1.0f);

	FDocFluidReservoirState StateA, StateB, StateC;
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
	Fix.Subsystem->QueryReservoir(TEXT("TankC"), StateC);

	TestEqual(TEXT("TankA is empty but not overdrafted"), StateA.CurrentVolume, 0.0f);
	TestEqual(TEXT("TankB received proportional 5L"), StateB.CurrentVolume, 5.0f);
	TestEqual(TEXT("TankC received proportional 5L"), StateC.CurrentVolume, 5.0f);
	TestEqual(TEXT("Total system volume remains 10L"), Fix.Subsystem->GetTotalSystemVolume(), 10.0);

	return true;
}

// FLU-03: Multiple sources cannot overfill a destination
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidCompetingInflowsTest, FAutomationTestBase, "Doc.Fluid.CompetingInflows", DocFluidTests::Flags)
bool FDocFluidCompetingInflowsTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
	Fix.CreateReservoir(TEXT("TankB"), 100.0f, 50.0f);
	// Destination has capacity 10L and starts at 0L
	Fix.CreateReservoir(TEXT("TankDest"), 10.0f, 0.0f);

	// Both tanks attempt to push 10L/s (total 20L incoming for 10L capacity)
	Fix.CreateEdge(TEXT("PipeADest"), TEXT("TankA"), TEXT("TankDest"), 10.0f, 1.0f);
	Fix.CreateEdge(TEXT("PipeBDest"), TEXT("TankB"), TEXT("TankDest"), 10.0f, 1.0f);

	Fix.Subsystem->StepSimulation(1.0f);

	FDocFluidReservoirState StateDest, StateA, StateB;
	Fix.Subsystem->QueryReservoir(TEXT("TankDest"), StateDest);
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);

	TestEqual(TEXT("TankDest filled exactly to capacity (10L)"), StateDest.CurrentVolume, 10.0f);
	TestEqual(TEXT("TankA supplied proportional 5L"), StateA.CurrentVolume, 45.0f);
	TestEqual(TEXT("TankB supplied proportional 5L"), StateB.CurrentVolume, 45.0f);
	TestEqual(TEXT("Total system volume preserved at 100L"), Fix.Subsystem->GetTotalSystemVolume(), 100.0);

	return true;
}

// FLU-04: Registration permutations yield equal/tolerance-bounded results
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidOrderIndependenceTest, FAutomationTestBase, "Doc.Fluid.OrderIndependence", DocFluidTests::Flags)
bool FDocFluidOrderIndependenceTest::RunTest(const FString& Parameters)
{
	// Fan-out, fan-in and a chain, with competing constraints on every tank.
	auto Run = [](bool bReversed) -> TMap<FName, float>
	{
		DocFluidTests::FFixture Fix;
		TArray<TTuple<FName, float, float>> Tanks = {
			MakeTuple(FName(TEXT("A")), 100.0f, 12.0f), MakeTuple(FName(TEXT("B")), 100.0f, 7.0f),
			MakeTuple(FName(TEXT("C")), 6.0f, 1.0f), MakeTuple(FName(TEXT("D")), 9.0f, 0.0f) };
		TArray<TTuple<FName, FName, FName, float>> Pipes = {
			MakeTuple(FName(TEXT("P1")), FName(TEXT("A")), FName(TEXT("C")), 8.0f), MakeTuple(FName(TEXT("P2")), FName(TEXT("B")), FName(TEXT("C")), 5.0f),
			MakeTuple(FName(TEXT("P3")), FName(TEXT("A")), FName(TEXT("D")), 7.0f), MakeTuple(FName(TEXT("P4")), FName(TEXT("C")), FName(TEXT("D")), 3.0f) };
		if (bReversed)
		{
			Algo::Reverse(Tanks);
			Algo::Reverse(Pipes);
		}
		for (const auto& T : Tanks)
		{
			Fix.CreateReservoir(T.Get<0>(), T.Get<1>(), T.Get<2>());
		}
		for (const auto& P : Pipes)
		{
			Fix.CreateEdge(P.Get<0>(), P.Get<1>(), P.Get<2>(), P.Get<3>(), 1.0f);
		}
		Fix.Subsystem->StepSimulation(1.0f);
		TMap<FName, float> Out;
		for (const TCHAR* Id : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") })
		{
			FDocFluidReservoirState State;
			Fix.Subsystem->QueryReservoir(Id, State);
			Out.Add(Id, State.CurrentVolume);
		}
		return Out;
	};

	const TMap<FName, float> R1 = Run(false);
	const TMap<FName, float> R2 = Run(true);
	float Total = 0.0f;
	for (const TPair<FName, float>& Kvp : R1)
	{
		TestEqual(*FString::Printf(TEXT("%s identical under reversed registration"), *Kvp.Key.ToString()), R2[Kvp.Key], Kvp.Value, 1e-6f);
		Total += Kvp.Value;
	}
	TestEqual(TEXT("Network total conserved (20 L)"), Total, 20.0f, 1e-4f);
	return true;
}

// FLU-05: Each control affects only eligible transfers and retains stored volume
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidValvePumpBlockageTest, FAutomationTestBase, "Doc.Fluid.ValvePumpBlockage", DocFluidTests::Flags)
bool FDocFluidValvePumpBlockageTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
	Fix.CreateReservoir(TEXT("TankB"), 100.0f, 0.0f);
	Fix.CreateReservoir(TEXT("TankC"), 100.0f, 0.0f);
	Fix.CreateReservoir(TEXT("TankD"), 100.0f, 0.0f);
	Fix.CreateEdge(TEXT("PipeValve"), TEXT("TankA"), TEXT("TankB"), 10.0f, 0.0f);
	Fix.CreateEdge(TEXT("PipePump"), TEXT("TankA"), TEXT("TankC"), 10.0f, 1.0f, true, false);
	Fix.CreateEdge(TEXT("PipeBlock"), TEXT("TankA"), TEXT("TankD"), 10.0f, 1.0f);
	TestTrue(TEXT("Blockage can be set"), Fix.Subsystem->SetEdgeBlocked(TEXT("PipeBlock"), true));

	FName Reason;
	TestEqual(TEXT("Pump status: Disabled"), Fix.Subsystem->GetEdgeStatus(TEXT("PipePump"), Reason), EDocFluidPumpStatus::Disabled);
	TestEqual(TEXT("Blocked status"), Fix.Subsystem->GetEdgeStatus(TEXT("PipeBlock"), Reason), EDocFluidPumpStatus::Blocked);
	TestFalse(TEXT("Invalid valve opening refused"), Fix.Subsystem->SetValveOpening(TEXT("PipeValve"), 1.5f));

	Fix.Subsystem->StepSimulation(1.0f);
	FDocFluidReservoirState StateA, StateB, StateC, StateD;
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
	Fix.Subsystem->QueryReservoir(TEXT("TankC"), StateC);
	Fix.Subsystem->QueryReservoir(TEXT("TankD"), StateD);
	TestEqual(TEXT("TankA retained its contents"), StateA.CurrentVolume, 50.0f);
	TestEqual(TEXT("Closed valve: TankB zero"), StateB.CurrentVolume, 0.0f);
	TestEqual(TEXT("Stopped pump: TankC zero"), StateC.CurrentVolume, 0.0f);
	TestEqual(TEXT("Blocked pipe: TankD zero"), StateD.CurrentVolume, 0.0f);

	// Each control only affects its own path.
	Fix.Subsystem->SetValveOpening(TEXT("PipeValve"), 0.5f);
	Fix.Subsystem->SetPumpEnabled(TEXT("PipePump"), true);
	TestEqual(TEXT("Pump status: Enabled"), Fix.Subsystem->GetEdgeStatus(TEXT("PipePump"), Reason), EDocFluidPumpStatus::Enabled);
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
	Fix.Subsystem->QueryReservoir(TEXT("TankC"), StateC);
	Fix.Subsystem->QueryReservoir(TEXT("TankD"), StateD);
	TestEqual(TEXT("TankB received 5L (half-open valve)"), StateB.CurrentVolume, 5.0f);
	TestEqual(TEXT("TankC received 10L (pump on)"), StateC.CurrentVolume, 10.0f);
	TestEqual(TEXT("TankD still blocked"), StateD.CurrentVolume, 0.0f);
	TestEqual(TEXT("TankA drained by exactly 15L"), StateA.CurrentVolume, 35.0f);

	Fix.Subsystem->SetEdgeBlocked(TEXT("PipeBlock"), false);
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryReservoir(TEXT("TankD"), StateD);
	TestEqual(TEXT("Unblocked pipe flows"), StateD.CurrentVolume, 10.0f);

	// A drained source reports unavailable supply.
	Fix.CreateReservoir(TEXT("Empty"), 10.0f, 0.0f);
	Fix.CreateEdge(TEXT("PipeEmpty"), TEXT("Empty"), TEXT("TankB"), 10.0f, 1.0f);
	TestEqual(TEXT("Empty source: UnavailableSupply"), Fix.Subsystem->GetEdgeStatus(TEXT("PipeEmpty"), Reason), EDocFluidPumpStatus::UnavailableSupply);
	TestEqual(TEXT("Reason names it"), Reason, FName(TEXT("SourceEmpty")));
	return true;
}

// FLU-06: Inflow, leaks, and drains reconcile in the volume ledger
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidExplicitSourcesSinksTest, FAutomationTestBase, "Doc.Fluid.ExplicitSourcesSinks", DocFluidTests::Flags)
bool FDocFluidExplicitSourcesSinksTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
	Fix.Subsystem->SetExternalSupplyRate(TEXT("TankA"), 10.0f);
	Fix.Subsystem->SetLeakRate(TEXT("TankA"), 2.0f);
	const double InitialTotal = Fix.Subsystem->GetTotalSystemVolume();

	Fix.Subsystem->StepSimulation(1.0f); // +10 in, -2 leak
	FDocFluidDrainReceipt Receipt = Fix.Subsystem->RequestDrain(TEXT("TankA"), 15.0f);
	TestEqual(TEXT("Drain succeeded"), Receipt.Result, EDocFluidDrainResult::Success);
	TestEqual(TEXT("Drained 15L"), Receipt.AmountDrained, 15.0f);

	FDocFluidDrainReceipt TooMuch = Fix.Subsystem->RequestDrain(TEXT("TankA"), 1000.0f);
	TestEqual(TEXT("Impossible drain is not silently partial"), TooMuch.Result, EDocFluidDrainResult::Unavailable);
	TestEqual(TEXT("Nothing removed"), TooMuch.AmountDrained, 0.0f);

	FDocFluidReservoirState StateA;
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	TestEqual(TEXT("TankA is 43L"), StateA.CurrentVolume, 43.0f);

	const double FinalTotal = Fix.Subsystem->GetTotalSystemVolume();
	const FDocFluidLedger& Ledger = Fix.Subsystem->GetCumulativeLedger();
	TestEqual(TEXT("Ledger inflow"), Ledger.TotalExternalInflow, 10.0, 1e-4);
	TestEqual(TEXT("Ledger leak"), Ledger.TotalLeaks, 2.0, 1e-4);
	TestEqual(TEXT("Ledger drain"), Ledger.TotalDrains, 15.0, 1e-4);
	TestTrue(TEXT("TotalAfter == TotalBefore + In - Leak - Drain"), Ledger.VerifyConservation(InitialTotal, FinalTotal));

	// A full tank does not overfill from supply; an empty tank does not leak below zero.
	Fix.CreateReservoir(TEXT("Full"), 10.0f, 10.0f);
	Fix.CreateReservoir(TEXT("Dry"), 10.0f, 0.5f);
	Fix.Subsystem->SetExternalSupplyRate(TEXT("Full"), 5.0f);
	Fix.Subsystem->SetLeakRate(TEXT("Dry"), 5.0f);
	Fix.Subsystem->StepSimulation(1.0f);
	FDocFluidReservoirState Full, Dry;
	Fix.Subsystem->QueryReservoir(TEXT("Full"), Full);
	Fix.Subsystem->QueryReservoir(TEXT("Dry"), Dry);
	TestEqual(TEXT("No overfill"), Full.CurrentVolume, 10.0f);
	TestEqual(TEXT("No negative volume"), Dry.CurrentVolume, 0.0f);
	return true;
}

// FLU-07: Cycles and bounded catch-up do not create liquid or exceed limits
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidLoopAndHitchTest, FAutomationTestBase, "Doc.Fluid.LoopAndHitch", DocFluidTests::Flags)
bool FDocFluidLoopAndHitchTest::RunTest(const FString& Parameters)
{
	// Asymmetric loop: flows in both directions at once without creating or losing liquid.
	{
		DocFluidTests::FFixture Fix;
		Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
		Fix.CreateReservoir(TEXT("TankB"), 60.0f, 50.0f);
		Fix.CreateEdge(TEXT("PipeAB"), TEXT("TankA"), TEXT("TankB"), 10.0f, 1.0f);
		Fix.CreateEdge(TEXT("PipeBA"), TEXT("TankB"), TEXT("TankA"), 3.0f, 1.0f);
		const double InitialTotal = Fix.Subsystem->GetTotalSystemVolume();
		Fix.Subsystem->StepSimulation(10.0f);
		TestEqual(TEXT("Loop conserves volume"), Fix.Subsystem->GetTotalSystemVolume(), InitialTotal, 1e-3);
		FDocFluidReservoirState StateA, StateB;
		Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
		Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
		TestTrue(TEXT("TankA within [0, capacity]"), StateA.CurrentVolume >= 0.0f && StateA.CurrentVolume <= StateA.Capacity + 1e-4f);
		TestTrue(TEXT("TankB within [0, capacity]"), StateB.CurrentVolume >= 0.0f && StateB.CurrentVolume <= StateB.Capacity + 1e-4f);
		TestEqual(TEXT("TankB settles one step of return flow below capacity (59.7 L): the declared one-step delay"), StateB.CurrentVolume, 59.7f, 1e-3f);
	}

	// A hitch runs at most the step budget; the rest is visible lag, never one giant step.
	{
		DocFluidTests::FFixture Fix;
		Fix.Subsystem->MaxSimulationStepsPerCatchUp = 20;
		Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
		Fix.CreateReservoir(TEXT("TankB"), 100.0f, 0.0f);
		Fix.CreateEdge(TEXT("PipeAB"), TEXT("TankA"), TEXT("TankB"), 10.0f, 1.0f);
		Fix.Subsystem->StepSimulation(10.0f);
		FDocFluidReservoirState StateB;
		Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
		TestEqual(TEXT("Only 20 x 0.1 s simulated (20 L moved)"), StateB.CurrentVolume, 20.0f, 1e-3f);
		TestEqual(TEXT("8 s of lag kept"), Fix.Subsystem->GetSimulationLagSeconds(), 8.0, 1e-3);
		TestEqual(TEXT("Simulated time recorded"), Fix.Subsystem->GetCumulativeLedger().SimulatedSeconds, 2.0, 1e-3);
		Fix.Subsystem->DiscardSimulationLag();
		Fix.Subsystem->StepSimulation(0.05f);
		Fix.Subsystem->QueryReservoir(TEXT("TankB"), StateB);
		TestEqual(TEXT("Half a step does not run"), StateB.CurrentVolume, 20.0f, 1e-3f);
		TestEqual(TEXT("It waits as lag"), Fix.Subsystem->GetSimulationLagSeconds(), 0.05, 1e-4);
		const TArray<FDocFluidTransfer> Rates = Fix.Subsystem->QueryTransferRates();
		TestTrue(TEXT("Transfer rate reported (10 L/s)"), Rates.Num() == 1 && FMath::IsNearlyEqual(Rates[0].RateLitersPerSecond, 10.0f, 1e-3f));
	}
	return true;
}

// FLU-08: Stream loss suspends transfers without deleting contents
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidUnresolvedEndpointTest, FAutomationTestBase, "Doc.Fluid.UnresolvedEndpoint", DocFluidTests::Flags)
bool FDocFluidUnresolvedEndpointTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;
	Fix.CreateReservoir(TEXT("TankA"), 100.0f, 50.0f);
	Fix.CreateEdge(TEXT("BrokenPipe"), TEXT("TankA"), TEXT("TankMissing"), 10.0f, 1.0f);
	Fix.Subsystem->StepSimulation(1.0f);
	FDocFluidReservoirState StateA;
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	TestEqual(TEXT("TankA contents retained"), StateA.CurrentVolume, 50.0f);
	FName Reason;
	TestEqual(TEXT("Edge reports its unresolved endpoint"), Fix.Subsystem->GetEdgeStatus(TEXT("BrokenPipe"), Reason), EDocFluidPumpStatus::UnavailableSupply);
	TestEqual(TEXT("Reason"), Reason, FName(TEXT("UnresolvedEndpoint")));
	Fix.CreateReservoir(TEXT("TankMissing"), 100.0f, 0.0f);
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
	TestEqual(TEXT("Flows once the endpoint resolves"), StateA.CurrentVolume, 40.0f);

	// A streamed-out reservoir component: contents kept, nothing flows, supply and leaks pause.
	AActor* Actor = Fix.ScopedWorld.Spawn<AActor>();
	UDocFluidReservoirComponent* Comp = NewObject<UDocFluidReservoirComponent>(Actor);
	Comp->ReservoirId = TEXT("Streamed");
	Comp->Capacity = 100.0f;
	Comp->CurrentVolume = 30.0f;
	Comp->LeakRate = 1.0f;
	Comp->RegisterComponent();
	if (!Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay();
	}
	Fix.CreateEdge(TEXT("PipeStream"), TEXT("Streamed"), TEXT("TankMissing"), 10.0f, 1.0f);
	TestTrue(TEXT("Component registered its reservoir"), Fix.Subsystem->QueryReservoir(TEXT("Streamed"), StateA));
	TestTrue(TEXT("Component has begun play"), Comp->HasBegunPlay());
	if (!Comp->HasBegunPlay())
	{
		return false;
	}
	Comp->EndPlay(EEndPlayReason::RemovedFromWorld);
	const double Before = Fix.Subsystem->GetTotalSystemVolume();
	Fix.Subsystem->StepSimulation(1.0f);
	FDocFluidReservoirState Streamed;
	Fix.Subsystem->QueryReservoir(TEXT("Streamed"), Streamed);
	TestTrue(TEXT("Unloaded reservoir is suspended, not deleted"), Streamed.bSuspended);
	TestEqual(TEXT("Its contents are untouched (no flow, no leak)"), Streamed.CurrentVolume, 30.0f);
	TestEqual(TEXT("Edge reports the suspended endpoint"), Fix.Subsystem->GetEdgeStatus(TEXT("PipeStream"), Reason), EDocFluidPumpStatus::UnavailableSupply);
	TestTrue(TEXT("Nothing else changed: only TankA->TankMissing flowed"), FMath::IsNearlyEqual(Fix.Subsystem->GetTotalSystemVolume(), Before, 1e-3));

	// Reload: a new component with an authored start volume does not overwrite the retained contents.
	UDocFluidReservoirComponent* Reloaded = NewObject<UDocFluidReservoirComponent>(Actor);
	Reloaded->ReservoirId = TEXT("Streamed");
	Reloaded->Capacity = 100.0f;
	Reloaded->CurrentVolume = 99.0f;
	Reloaded->RegisterComponent();
	Fix.Subsystem->QueryReservoir(TEXT("Streamed"), Streamed);
	TestFalse(TEXT("Resumed"), Streamed.bSuspended);
	TestEqual(TEXT("Retained volume kept"), Streamed.CurrentVolume, 30.0f);
	TestEqual(TEXT("Component shows the retained volume"), Reloaded->CurrentVolume, 30.0f);
	return true;
}

// FLU-09: Smaller restored capacity follows an explicit non-silent policy
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidCapacityMigrationTest, FAutomationTestBase, "Doc.Fluid.CapacityMigration", DocFluidTests::Flags)
bool FDocFluidCapacityMigrationTest::RunTest(const FString& Parameters)
{
	// Spill to an explicit sink.
	{
		DocFluidTests::FFixture Fix;
		Fix.CreateReservoir(TEXT("TankA"), 100.0f, 80.0f);
		float Discrepancy = 0.0f;
		TestTrue(TEXT("Spill migration"), Fix.Subsystem->MigrateCapacity(TEXT("TankA"), 50.0f, EDocFluidCapacityMigrationPolicy::SpillToSink, Discrepancy));
		TestEqual(TEXT("Discrepancy 30L"), Discrepancy, 30.0f);
		FDocFluidReservoirState StateA;
		Fix.Subsystem->QueryReservoir(TEXT("TankA"), StateA);
		TestEqual(TEXT("TankA at new capacity"), StateA.CurrentVolume, 50.0f);
		TestEqual(TEXT("Spill recorded as external outflow"), Fix.Subsystem->GetCumulativeLedger().TotalExternalOutflow, 30.0, 1e-4);
		TestTrue(TEXT("Ledger balances"), Fix.Subsystem->GetCumulativeLedger().VerifyConservation(80.0, Fix.Subsystem->GetTotalSystemVolume()));
	}

	// Quarantine keeps the excess, recorded, and can release it later.
	{
		DocFluidTests::FFixture Fix;
		Fix.CreateReservoir(TEXT("TankQ"), 100.0f, 80.0f);
		float Discrepancy = 0.0f;
		TestTrue(TEXT("Quarantine migration"), Fix.Subsystem->MigrateCapacity(TEXT("TankQ"), 50.0f, EDocFluidCapacityMigrationPolicy::Quarantine, Discrepancy));
		FDocFluidReservoirState State;
		Fix.Subsystem->QueryReservoir(TEXT("TankQ"), State);
		TestEqual(TEXT("Active volume at capacity"), State.CurrentVolume, 50.0f);
		TestEqual(TEXT("30L quarantined"), State.QuarantinedVolume, 30.0f);
		TestEqual(TEXT("Quarantine recorded in the ledger"), Fix.Subsystem->GetCumulativeLedger().NetQuarantined, 30.0, 1e-4);
		TestTrue(TEXT("Ledger balances"), Fix.Subsystem->GetCumulativeLedger().VerifyConservation(80.0, Fix.Subsystem->GetTotalSystemVolume()));
		Fix.Subsystem->MigrateCapacity(TEXT("TankQ"), 100.0f, EDocFluidCapacityMigrationPolicy::Quarantine, Discrepancy);
		float Released = 0.0f;
		TestTrue(TEXT("Release"), Fix.Subsystem->ReleaseQuarantine(TEXT("TankQ"), Released));
		TestEqual(TEXT("All 30L released"), Released, 30.0f);
		Fix.Subsystem->QueryReservoir(TEXT("TankQ"), State);
		TestEqual(TEXT("Back to 80L"), State.CurrentVolume, 80.0f);
		TestTrue(TEXT("Ledger balances after release"), Fix.Subsystem->GetCumulativeLedger().VerifyConservation(80.0, Fix.Subsystem->GetTotalSystemVolume()));

		// Removing a storage node is a recorded discard.
		TestTrue(TEXT("Remove tank"), Fix.Subsystem->UnregisterReservoir(TEXT("TankQ")));
		TestEqual(TEXT("Discard recorded"), Fix.Subsystem->GetCumulativeLedger().TotalDiscarded, 80.0, 1e-4);
		TestTrue(TEXT("Ledger balances after removal"), Fix.Subsystem->GetCumulativeLedger().VerifyConservation(80.0, Fix.Subsystem->GetTotalSystemVolume()));
	}

	// Restore into content whose tank shrank: the saved excess is quarantined, not clamped away.
	{
		FDocFluidNetworkSnapshot Snapshot;
		{
			DocFluidTests::FFixture Fix;
			Fix.CreateReservoir(TEXT("TankR"), 100.0f, 90.0f);
			Fix.CreateReservoir(TEXT("TankS"), 100.0f, 0.0f);
			Fix.CreateEdge(TEXT("PipeRS"), TEXT("TankR"), TEXT("TankS"), 10.0f, 0.25f);
			Fix.Subsystem->StepSimulation(0.35f); // 3 whole steps + lag
			Fix.Subsystem->CaptureState(Snapshot);
		}
		DocFluidTests::FFixture Fix;
		Fix.CreateReservoir(TEXT("TankR"), 60.0f, 0.0f); // updated content: smaller tank
		TestTrue(TEXT("Restore"), Fix.Subsystem->StageRestore(Snapshot));
		FDocFluidReservoirState R, S;
		Fix.Subsystem->QueryReservoir(TEXT("TankR"), R);
		Fix.Subsystem->QueryReservoir(TEXT("TankS"), S);
		TestEqual(TEXT("Authored capacity wins"), R.Capacity, 60.0f);
		TestEqual(TEXT("Active volume at the new capacity"), R.CurrentVolume, 60.0f);
		TestEqual(TEXT("Saved excess quarantined (90 - 0.75 - 60)"), R.QuarantinedVolume, 29.25f, 1e-3f);
		TestEqual(TEXT("Other tank restored"), S.CurrentVolume, 0.75f, 1e-3f);
		TestEqual(TEXT("Lag restored"), Fix.Subsystem->GetSimulationLagSeconds(), 0.05, 1e-3);
		FName Reason;
		TestEqual(TEXT("Controls restored with the edge"), Fix.Subsystem->GetEdgeStatus(TEXT("PipeRS"), Reason), EDocFluidPumpStatus::Enabled);

		FDocFluidNetworkSnapshot Bad = Snapshot;
		Bad.Reservoirs[0].CurrentVolume = -1.0f;
		TestFalse(TEXT("Invalid snapshot refused"), Fix.Subsystem->StageRestore(Bad));
		Fix.Subsystem->QueryReservoir(TEXT("TankR"), R);
		TestEqual(TEXT("A refused restore changes nothing"), R.CurrentVolume, 60.0f);
	}
	return true;
}

// FLU-10: Negative/non-finite values, invalid types, and unsupported junctions fail safely
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocFluidInvalidNumbersTest, FAutomationTestBase, "Doc.Fluid.InvalidNumbers", DocFluidTests::Flags)
bool FDocFluidInvalidNumbersTest::RunTest(const FString& Parameters)
{
	DocFluidTests::FFixture Fix;

	FDocFluidReservoirState NegCap;
	NegCap.ReservoirId = TEXT("Bad1");
	NegCap.Capacity = -10.0f;
	TestFalse(TEXT("Negative capacity rejected"), Fix.Subsystem->RegisterReservoir(NegCap));

	FDocFluidReservoirState NanVol;
	NanVol.ReservoirId = TEXT("Bad2");
	NanVol.Capacity = 100.0f;
	NanVol.CurrentVolume = NAN;
	TestFalse(TEXT("NaN volume rejected"), Fix.Subsystem->RegisterReservoir(NanVol));

	FDocFluidReservoirState Overfill;
	Overfill.ReservoirId = TEXT("Bad3");
	Overfill.Capacity = 50.0f;
	Overfill.CurrentVolume = 60.0f;
	TestFalse(TEXT("Volume above capacity rejected"), Fix.Subsystem->RegisterReservoir(Overfill));

	FDocFluidReservoirState NoLiquid;
	NoLiquid.ReservoirId = TEXT("Bad4");
	NoLiquid.FluidDefinitionId = NAME_None;
	TestFalse(TEXT("Missing liquid type rejected"), Fix.Subsystem->RegisterReservoir(NoLiquid));

	TestEqual(TEXT("Negative drain returns InvalidAmount"), Fix.Subsystem->RequestDrain(TEXT("NonExistent"), -5.0f).Result, EDocFluidDrainResult::InvalidAmount);

	Fix.CreateReservoir(TEXT("Water1"), 100.0f, 50.0f);
	Fix.CreateReservoir(TEXT("Water2"), 100.0f, 0.0f);
	FDocFluidReservoirState Oil;
	Oil.ReservoirId = TEXT("Oil1");
	Oil.FluidDefinitionId = TEXT("Oil");
	Oil.Capacity = 100.0f;
	TestTrue(TEXT("Oil tank registers"), Fix.Subsystem->RegisterReservoir(Oil));
	TestFalse(TEXT("Duplicate reservoir id rejected (no silent overwrite)"), Fix.Subsystem->RegisterReservoir(Oil));

	FDocFluidEdge Mixed;
	Mixed.EdgeId = TEXT("Mix");
	Mixed.SourceReservoirId = TEXT("Water1");
	Mixed.DestinationReservoirId = TEXT("Oil1");
	TestFalse(TEXT("Edge between different liquids rejected"), Fix.Subsystem->RegisterConnection(Mixed));

	FDocFluidEdge SelfLoop;
	SelfLoop.EdgeId = TEXT("Self");
	SelfLoop.SourceReservoirId = TEXT("Water1");
	SelfLoop.DestinationReservoirId = TEXT("Water1");
	TestFalse(TEXT("Zero-length self junction rejected"), Fix.Subsystem->RegisterConnection(SelfLoop));

	FDocFluidEdge Good;
	Good.EdgeId = TEXT("W12");
	Good.SourceReservoirId = TEXT("Water1");
	Good.DestinationReservoirId = TEXT("Water2");
	TestTrue(TEXT("Valid edge"), Fix.Subsystem->RegisterConnection(Good));
	TestFalse(TEXT("Duplicate edge id rejected"), Fix.Subsystem->RegisterConnection(Good));

	FDocFluidEdge BadValve = Good;
	BadValve.EdgeId = TEXT("BadValve");
	BadValve.ValveOpening = 2.0f;
	TestFalse(TEXT("Valve outside [0,1] rejected"), Fix.Subsystem->RegisterConnection(BadValve));

	TestFalse(TEXT("Negative supply rejected"), Fix.Subsystem->SetExternalSupplyRate(TEXT("Water1"), -1.0f));
	TestFalse(TEXT("NaN leak rejected"), Fix.Subsystem->SetLeakRate(TEXT("Water1"), NAN));

	const double Before = Fix.Subsystem->GetTotalSystemVolume();
	Fix.Subsystem->StepSimulation(NAN);
	Fix.Subsystem->StepSimulation(-1.0f);
	FDocFluidReservoirState W2;
	Fix.Subsystem->QueryReservoir(TEXT("Water2"), W2);
	TestEqual(TEXT("Invalid steps do nothing"), W2.CurrentVolume, 0.0f);
	TestEqual(TEXT("Volume unchanged"), Fix.Subsystem->GetTotalSystemVolume(), Before);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
