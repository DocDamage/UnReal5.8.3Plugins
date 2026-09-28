#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocMechanicalTypes.h"
#include "DocMechanicalComponents.h"
#include "DocMechanicalNetworkSubsystem.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocMechanicalTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		FDocScopedTestWorld ScopedWorld;
		UDocMechanicalNetworkSubsystem* Subsystem = nullptr;

		FFixture()
		{
			UWorld* World = ScopedWorld.World;
			check(World);
			Subsystem = World->GetSubsystem<UDocMechanicalNetworkSubsystem>();
			check(Subsystem);
		}

		void AddNode(FName Id, float LoadTorque = 0.0f)
		{
			FDocMechanicalNodeState Node;
			Node.NodeId = Id;
			Node.AppliedLoadTorque = LoadTorque;
			Subsystem->RegisterNode(Node);
		}

		void AddSource(FName SourceId, FName NodeId, float Speed = 10.0f, float Capacity = 100.0f, float Hysteresis = 0.10f)
		{
			FDocDriveSourceState Src;
			Src.SourceId = SourceId;
			Src.AttachedNodeId = NodeId;
			Src.RequestedSpeed = Speed;
			Src.TorqueCapacity = Capacity;
			Src.StallHysteresisMargin = Hysteresis;
			Subsystem->RegisterDriveSource(Src);
		}

		void AddEdge(FName EdgeId, FName Parent, FName Child, float Ratio = 1.0f, float Efficiency = 1.0f, bool bClutched = false, bool bEngaged = true)
		{
			FDocDriveEdge Edge;
			Edge.EdgeId = EdgeId;
			Edge.ParentNodeId = Parent;
			Edge.ChildNodeId = Child;
			Edge.Ratio = Ratio;
			Edge.Efficiency = Efficiency;
			Edge.bIsClutched = bClutched;
			Edge.bIsEngaged = bEngaged;
			Subsystem->ConnectDrive(Edge);
		}
	};
}

// MEC-01: Chain ratios and gear/belt signs produce expected speeds
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalRatioAndDirectionTest, FAutomationTestBase, "Doc.Mechanical.RatioAndDirection", DocMechanicalTests::Flags)
bool FDocMechanicalRatioAndDirectionTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("ShaftMotor"));
	Fix.AddNode(TEXT("ShaftGear"));
	Fix.AddNode(TEXT("ShaftOutput"));

	Fix.AddSource(TEXT("Motor"), TEXT("ShaftMotor"), 10.0f); // 10 rad/s
	// Gear pair reverses direction: ratio -2.0 -> -20 rad/s
	Fix.AddEdge(TEXT("GearPair"), TEXT("ShaftMotor"), TEXT("ShaftGear"), -2.0f, 1.0f);
	// Belt preserves direction: ratio 0.5 -> -10 rad/s
	Fix.AddEdge(TEXT("BeltDrive"), TEXT("ShaftGear"), TEXT("ShaftOutput"), 0.5f, 1.0f);

	FDocDriveSolveResult Solved = Fix.Subsystem->SolveNetwork();
	TestTrue(TEXT("Solve succeeded"), Solved.bSuccess);

	FDocMechanicalNodeState StateMotor, StateGear, StateOut;
	Fix.Subsystem->QueryNodeDrive(TEXT("ShaftMotor"), StateMotor);
	Fix.Subsystem->QueryNodeDrive(TEXT("ShaftGear"), StateGear);
	Fix.Subsystem->QueryNodeDrive(TEXT("ShaftOutput"), StateOut);

	TestEqual(TEXT("Motor shaft runs at +10 rad/s"), StateMotor.ActualSpeed, 10.0f);
	TestEqual(TEXT("Gear shaft reversed to -20 rad/s"), StateGear.ActualSpeed, -20.0f);
	TestEqual(TEXT("Output shaft runs at -10 rad/s"), StateOut.ActualSpeed, -10.0f);

	return true;
}

// MEC-02: Branch loads and efficiency produce the declared parent demand
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalReflectedLoadsTest, FAutomationTestBase, "Doc.Mechanical.ReflectedLoads", DocMechanicalTests::Flags)
bool FDocMechanicalReflectedLoadsTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("MotorNode"), 0.0f);
	Fix.AddNode(TEXT("ChildA"), 20.0f);
	Fix.AddNode(TEXT("ChildB"), 10.0f);
	Fix.AddNode(TEXT("GrandC"), 4.0f);
	Fix.AddSource(TEXT("Motor"), TEXT("MotorNode"), 10.0f, 200.0f);
	Fix.AddEdge(TEXT("E2"), TEXT("MotorNode"), TEXT("ChildB"), -0.5f, 1.0f);
	Fix.AddEdge(TEXT("E1"), TEXT("MotorNode"), TEXT("ChildA"), 2.0f, 0.8f);
	Fix.AddEdge(TEXT("E3"), TEXT("ChildB"), TEXT("GrandC"), 3.0f, 0.5f);
	TestTrue(TEXT("Solve"), Fix.Subsystem->SolveNetwork().bSuccess);

	// ChildB: 10 + 3*4/0.5 = 34. Motor: 2*20/0.8 + 0.5*34/1 = 50 + 17 = 67.
	const TMap<FName, float> Loads = Fix.Subsystem->QueryReflectedLoads();
	TestEqual(TEXT("Grandchild keeps its own load"), Loads.FindRef(TEXT("GrandC")), 4.0f);
	TestEqual(TEXT("Branch B reflects its child"), Loads.FindRef(TEXT("ChildB")), 34.0f);
	TestEqual(TEXT("Motor demand sums both branches (|ratio| x load / efficiency)"), Loads.FindRef(TEXT("MotorNode")), 67.0f);
	FDocDriveSourceState Source;
	Fix.Subsystem->QueryDriveSource(TEXT("Motor"), Source);
	TestEqual(TEXT("Demand recorded on the source"), Source.LastDemandTorque, 67.0f);
	return true;
}

// MEC-03: Overload stalls and recovery hysteresis prevents oscillation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalStallRecoveryTest, FAutomationTestBase, "Doc.Mechanical.StallRecovery", DocMechanicalTests::Flags)
bool FDocMechanicalStallRecoveryTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("MotorNode"));
	Fix.AddNode(TEXT("DrivenNode"), 50.0f);

	// Capacity 100 Nm, Hysteresis 10% (recovery requires < 90 Nm)
	Fix.AddSource(TEXT("Motor"), TEXT("MotorNode"), 10.0f, 100.0f, 0.10f);
	Fix.AddEdge(TEXT("DirectDrive"), TEXT("MotorNode"), TEXT("DrivenNode"), 1.0f, 1.0f);

	int32 Stalls = 0;
	int32 Recoveries = 0;
	Fix.Subsystem->OnStalledNative.AddLambda([&Stalls](FName, FName Reason) { if (Reason == TEXT("UnmetLoad")) { ++Stalls; } });
	Fix.Subsystem->OnRecoveredNative.AddLambda([&Recoveries](FName) { ++Recoveries; });

	// 1. Initial load 50 Nm <= 100 Nm -> Running
	Fix.Subsystem->SolveNetwork();
	FDocMechanicalNodeState NodeState;
	Fix.Subsystem->QueryNodeDrive(TEXT("DrivenNode"), NodeState);
	TestEqual(TEXT("Initial state is running at 10 rad/s"), NodeState.ActualSpeed, 10.0f);
	TestFalse(TEXT("Not stalled"), NodeState.bIsStalled);

	// 2. Increase load to 110 Nm -> Exceeds 100 Nm -> Stalled
	Fix.Subsystem->SetNodeLoad(TEXT("DrivenNode"), 110.0f);
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryNodeDrive(TEXT("DrivenNode"), NodeState);
	TestEqual(TEXT("Overloaded speed drops to 0"), NodeState.ActualSpeed, 0.0f);
	TestTrue(TEXT("Node is stalled"), NodeState.bIsStalled);

	// 3. Lower load to 95 Nm: Below 100 Nm, but above recovery threshold (90 Nm) -> remains stalled
	Fix.Subsystem->SetNodeLoad(TEXT("DrivenNode"), 95.0f);
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryNodeDrive(TEXT("DrivenNode"), NodeState);
	TestTrue(TEXT("Still stalled in hysteresis band"), NodeState.bIsStalled);
	TestEqual(TEXT("Speed remains 0 in hysteresis"), NodeState.ActualSpeed, 0.0f);

	// 4. Lower load to 85 Nm: Below 90 Nm -> Recovers!
	Fix.Subsystem->SetNodeLoad(TEXT("DrivenNode"), 85.0f);
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryNodeDrive(TEXT("DrivenNode"), NodeState);
	TestFalse(TEXT("Recovered from stall"), NodeState.bIsStalled);
	TestEqual(TEXT("Speed restored to 10 rad/s"), NodeState.ActualSpeed, 10.0f);
	TestEqual(TEXT("Exactly one stall event"), Stalls, 1);
	TestEqual(TEXT("Exactly one recovery event"), Recoveries, 1);

	return true;
}

// MEC-04: Zero speed does not divide by zero or generate invalid torque
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalZeroSpeedTest, FAutomationTestBase, "Doc.Mechanical.ZeroSpeed", DocMechanicalTests::Flags)
bool FDocMechanicalZeroSpeedTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("MotorNode"));
	Fix.AddNode(TEXT("LoadNode"), 30.0f);

	// Motor requested speed is 0.0 rad/s
	Fix.AddSource(TEXT("Motor"), TEXT("MotorNode"), 0.0f, 100.0f);
	Fix.AddEdge(TEXT("Drive"), TEXT("MotorNode"), TEXT("LoadNode"), 2.0f, 1.0f);

	FDocDriveSolveResult Solved = Fix.Subsystem->SolveNetwork();
	TestTrue(TEXT("Solve succeeds at zero speed without divide by zero"), Solved.bSuccess);

	FDocMechanicalNodeState MotorState;
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	TestEqual(TEXT("Actual speed is 0"), MotorState.ActualSpeed, 0.0f);
	TestEqual(TEXT("Static reflected load calculated cleanly (60 Nm)"), MotorState.ReflectedLoadTorque, 60.0f);

	return true;
}

// MEC-05: Disengaging a branch isolates its load without phase corruption
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalClutchIsolationTest, FAutomationTestBase, "Doc.Mechanical.ClutchIsolation", DocMechanicalTests::Flags)
bool FDocMechanicalClutchIsolationTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("MotorNode"));
	Fix.AddNode(TEXT("WorkNode"), 150.0f); // would stall a 100 Nm motor
	Fix.AddSource(TEXT("Motor"), TEXT("MotorNode"), 10.0f, 100.0f);
	Fix.AddEdge(TEXT("ClutchedDrive"), TEXT("MotorNode"), TEXT("WorkNode"), 1.0f, 1.0f, true, true);
	int32 ClutchEvents = 0;
	Fix.Subsystem->OnClutchChangedNative.AddLambda([&ClutchEvents](FName, bool) { ++ClutchEvents; });

	Fix.Subsystem->SolveNetwork();
	FDocMechanicalNodeState MotorState, WorkState;
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	TestTrue(TEXT("Stalled while the heavy branch is engaged"), MotorState.bIsStalled);

	TestTrue(TEXT("Disengage"), Fix.Subsystem->SetClutchState(TEXT("ClutchedDrive"), false));
	TestEqual(TEXT("Clutch event"), ClutchEvents, 1);
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	Fix.Subsystem->QueryNodeDrive(TEXT("WorkNode"), WorkState);
	TestFalse(TEXT("Motor runs once the branch is isolated"), MotorState.bIsStalled);
	TestEqual(TEXT("Motor reflected load drops to 0"), MotorState.ReflectedLoadTorque, 0.0f);
	TestTrue(TEXT("Motor advanced 10 rad (wrapped)"), FMath::IsNearlyEqual(MotorState.AccumulatedRevolutions, 10.0 / (2.0 * UE_DOUBLE_PI), 1e-5));
	TestFalse(TEXT("Detached branch is not driven"), WorkState.bIsDriven);
	TestEqual(TEXT("Detached branch holds its phase"), WorkState.PhaseAngle, 0.0);

	// Re-engaging continues the branch from its own phase; it does not snap to the motor's.
	Fix.Subsystem->SetNodeLoad(TEXT("WorkNode"), 0.0f);
	TestTrue(TEXT("Re-engage"), Fix.Subsystem->SetClutchState(TEXT("ClutchedDrive"), true));
	Fix.Subsystem->StepSimulation(0.5f);
	Fix.Subsystem->QueryNodeDrive(TEXT("WorkNode"), WorkState);
	TestTrue(TEXT("Branch phase advanced 5 rad from 0, not from the motor's phase"), FMath::IsNearlyEqual(WorkState.PhaseAngle, 5.0, 1e-5));

	// FreeVisualCoast: presentation coasts, logical phase holds.
	Fix.AddNode(TEXT("Flywheel"));
	FDocMechanicalNodeState Fly;
	Fix.Subsystem->QueryNodeDrive(TEXT("Flywheel"), Fly);
	Fly.NodeId = TEXT("CoastWheel");
	Fly.DetachedPolicy = EDocMechanicalDetachedPolicy::FreeVisualCoast;
	TestTrue(TEXT("Coasting node"), Fix.Subsystem->RegisterNode(Fly));
	Fix.AddEdge(TEXT("CoastClutch"), TEXT("MotorNode"), TEXT("CoastWheel"), 1.0f, 1.0f, true, true);
	Fix.Subsystem->StepSimulation(0.1f);
	Fix.Subsystem->QueryNodeDrive(TEXT("CoastWheel"), Fly);
	const double LogicalAtDetach = Fly.PhaseAngle;
	Fix.Subsystem->SetClutchState(TEXT("CoastClutch"), false);
	Fix.Subsystem->StepSimulation(0.5f); // coast 1 s: speed 10 -> 5, visual += 2.5
	Fix.Subsystem->QueryNodeDrive(TEXT("CoastWheel"), Fly);
	TestTrue(TEXT("Logical phase holds"), FMath::IsNearlyEqual(Fly.PhaseAngle, LogicalAtDetach, 1e-9));
	TestTrue(TEXT("Visual phase coasts"), FMath::IsNearlyEqual(Fly.VisualPhaseAngle, LogicalAtDetach + 2.5, 1e-4));
	return true;
}

// MEC-06: Cycles, reconvergent paths, and multiple drivers fail before mutation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalInvalidTopologyTest, FAutomationTestBase, "Doc.Mechanical.InvalidTopology", DocMechanicalTests::Flags)
bool FDocMechanicalInvalidTopologyTest::RunTest(const FString& Parameters)
{
	auto MakeEdge = [](FName Id, FName Parent, FName Child, bool bClutched = false, bool bEngaged = true)
	{
		FDocDriveEdge Edge;
		Edge.EdgeId = Id;
		Edge.ParentNodeId = Parent;
		Edge.ChildNodeId = Child;
		Edge.bIsClutched = bClutched;
		Edge.bIsEngaged = bEngaged;
		return Edge;
	};

	// Cycle: refused at the edit, before commit.
	{
		DocMechanicalTests::FFixture Fix;
		Fix.AddNode(TEXT("X"));
		Fix.AddNode(TEXT("Y"));
		Fix.AddNode(TEXT("Z"));
		TestTrue(TEXT("X->Y"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("XY"), TEXT("X"), TEXT("Y"))));
		TestTrue(TEXT("Y->Z"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("YZ"), TEXT("Y"), TEXT("Z"))));
		const int64 Rev = Fix.Subsystem->GetTopologyRevision();
		TestFalse(TEXT("Closing the cycle is refused"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("ZX"), TEXT("Z"), TEXT("X"))));
		TestTrue(TEXT("Error names the cycle"), Fix.Subsystem->GetLastTopologyError().Contains(TEXT("cycle")));
		TestEqual(TEXT("Nothing committed"), Fix.Subsystem->GetTopologyRevision(), Rev);
		TestTrue(TEXT("Network still solves"), Fix.Subsystem->SolveNetwork().bSuccess);
	}

	// Reconvergent path.
	{
		DocMechanicalTests::FFixture Fix;
		Fix.AddNode(TEXT("A"));
		Fix.AddNode(TEXT("B"));
		Fix.AddNode(TEXT("C"));
		Fix.AddSource(TEXT("Motor"), TEXT("A"), 10.0f);
		Fix.AddEdge(TEXT("AC"), TEXT("A"), TEXT("C"));
		TestFalse(TEXT("Second parent refused"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("BC"), TEXT("B"), TEXT("C"))));
		TestTrue(TEXT("Error names both edges"), Fix.Subsystem->GetLastTopologyError().Contains(TEXT("AC")) && Fix.Subsystem->GetLastTopologyError().Contains(TEXT("BC")));
		Fix.Subsystem->SolveNetwork();
		FDocMechanicalNodeState C;
		Fix.Subsystem->QueryNodeDrive(TEXT("C"), C);
		TestEqual(TEXT("Existing drive unaffected"), C.ActualSpeed, 10.0f);

		// A disengaged clutched path may exist, but engaging it is validated.
		TestTrue(TEXT("Disengaged second path allowed"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("BC2"), TEXT("B"), TEXT("C"), true, false)));
		TestFalse(TEXT("Engaging it is refused"), Fix.Subsystem->SetClutchState(TEXT("BC2"), true));
	}

	// Multiple drivers and non-root sources.
	{
		DocMechanicalTests::FFixture Fix;
		Fix.AddNode(TEXT("A"));
		Fix.AddNode(TEXT("B"));
		Fix.AddNode(TEXT("D"));
		Fix.AddSource(TEXT("M1"), TEXT("A"), 10.0f);
		Fix.AddEdge(TEXT("AB"), TEXT("A"), TEXT("B"));
		FDocDriveSourceState Second;
		Second.SourceId = TEXT("M2");
		Second.AttachedNodeId = TEXT("A");
		TestFalse(TEXT("Two drivers on one tree refused"), Fix.Subsystem->RegisterDriveSource(Second));
		Second.AttachedNodeId = TEXT("B");
		TestFalse(TEXT("A source inside a driven tree refused"), Fix.Subsystem->RegisterDriveSource(Second));
		Second.AttachedNodeId = TEXT("D");
		TestTrue(TEXT("A second tree may have its own driver"), Fix.Subsystem->RegisterDriveSource(Second));
		TestFalse(TEXT("Joining two driven trees refused"), Fix.Subsystem->ConnectDrive(MakeEdge(TEXT("AD"), TEXT("A"), TEXT("D"))));

		// Stale edits are refused.
		TestFalse(TEXT("Stale revision refused"), Fix.Subsystem->DisconnectDrive(TEXT("AB"), Fix.Subsystem->GetTopologyRevision() - 1));
		FDocMechanicalNodeState B;
		Fix.Subsystem->SolveNetwork();
		Fix.Subsystem->QueryNodeDrive(TEXT("B"), B);
		TestTrue(TEXT("Edge still there"), B.bIsDriven);
	}
	return true;
}

// MEC-07: Step partitioning/restore preserve declared angular tolerance
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalPhaseContinuityTest, FAutomationTestBase, "Doc.Mechanical.PhaseContinuity", DocMechanicalTests::Flags)
bool FDocMechanicalPhaseContinuityTest::RunTest(const FString& Parameters)
{
	FDocMechanicalNodeState Single, Partitioned;
	FDocMechanicalSnapshot Snapshot;
	{
		DocMechanicalTests::FFixture Fix;
		Fix.AddNode(TEXT("Shaft"));
		Fix.AddSource(TEXT("Motor"), TEXT("Shaft"), 10.0f);
		Fix.Subsystem->StepSimulation(1.0f);
		Fix.Subsystem->QueryNodeDrive(TEXT("Shaft"), Single);
	}
	{
		DocMechanicalTests::FFixture Fix;
		Fix.AddNode(TEXT("Shaft"));
		Fix.AddSource(TEXT("Motor"), TEXT("Shaft"), 10.0f);
		for (int32 i = 0; i < 10; ++i)
		{
			Fix.Subsystem->StepSimulation(0.1f);
		}
		Fix.Subsystem->QueryNodeDrive(TEXT("Shaft"), Partitioned);
		Fix.Subsystem->CaptureState(Snapshot);
	}
	const double TwoPi = 2.0 * UE_DOUBLE_PI;
	TestTrue(TEXT("Revolutions match across step partitioning"), FMath::IsNearlyEqual(Single.AccumulatedRevolutions, Partitioned.AccumulatedRevolutions, 1e-5));
	TestTrue(TEXT("10 rad = 1.5915 revolutions"), FMath::IsNearlyEqual(Single.AccumulatedRevolutions, 10.0 / TwoPi, 1e-5));
	TestTrue(TEXT("Phase is wrapped to [0, 2pi)"), Single.PhaseAngle >= 0.0 && Single.PhaseAngle < TwoPi);
	TestTrue(TEXT("Wrapped phase is 10 - 2pi"), FMath::IsNearlyEqual(Single.PhaseAngle, 10.0 - TwoPi, 1e-5));
	TestTrue(TEXT("Partitioned phase matches"), FMath::IsNearlyEqual(Single.PhaseAngle, Partitioned.PhaseAngle, 1e-5));

	// Restore into a fresh world: phase and revolutions come back, no events fire, and motion continues from there.
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("Shaft"));
	Fix.AddSource(TEXT("Motor"), TEXT("Shaft"), 0.0f);
	int32 Events = 0;
	Fix.Subsystem->OnStalledNative.AddLambda([&Events](FName, FName) { ++Events; });
	Fix.Subsystem->OnRecoveredNative.AddLambda([&Events](FName) { ++Events; });
	TestTrue(TEXT("Restore"), Fix.Subsystem->StageRestore(Snapshot));
	FDocMechanicalNodeState Restored;
	Fix.Subsystem->QueryNodeDrive(TEXT("Shaft"), Restored);
	TestTrue(TEXT("Phase restored"), FMath::IsNearlyEqual(Restored.PhaseAngle, Partitioned.PhaseAngle, 1e-9));
	TestTrue(TEXT("Revolutions restored"), FMath::IsNearlyEqual(Restored.AccumulatedRevolutions, Partitioned.AccumulatedRevolutions, 1e-9));
	Fix.Subsystem->StepSimulation(0.5f); // restored source speed is 10
	Fix.Subsystem->QueryNodeDrive(TEXT("Shaft"), Restored);
	TestTrue(TEXT("Motion continues from the restored phase"), FMath::IsNearlyEqual(Restored.AccumulatedRevolutions, 15.0 / TwoPi, 1e-5));
	TestEqual(TEXT("Restore emitted no historical events"), Events, 0);

	FDocMechanicalSnapshot Bad = Snapshot;
	Bad.Phases.Add(TEXT("Shaft"), std::numeric_limits<double>::quiet_NaN());
	TestFalse(TEXT("Non-finite phase refused"), Fix.Subsystem->StageRestore(Bad));
	return true;
}

// MEC-08: Presentation does not clobber unrelated transform/animation owners
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalAdapterOwnershipTest, FAutomationTestBase, "Doc.Mechanical.AdapterOwnership", DocMechanicalTests::Flags)
bool FDocMechanicalAdapterOwnershipTest::RunTest(const FString& Parameters)
{
	// Base scope: components register logical records only and never write actor transforms.
	DocMechanicalTests::FFixture Fix;
	AActor* TestActor = Fix.ScopedWorld.Spawn<AActor>();
	TestNotNull(TEXT("Actor spawned"), TestActor);
	const FTransform Before = TestActor->GetActorTransform();

	UDocMechanicalNodeComponent* Comp = NewObject<UDocMechanicalNodeComponent>(TestActor);
	Comp->NodeId = TEXT("ComponentNode");
	Comp->AppliedLoadTorque = 15.0f;
	Comp->MaxOperatingSpeed = 50.0f;
	Comp->DetachedPolicy = EDocMechanicalDetachedPolicy::FreeVisualCoast;
	Comp->RegisterComponent();
	Fix.Subsystem->RegisterNodeComponent(Comp);

	FDocMechanicalNodeState State;
	TestTrue(TEXT("Component node registered"), Fix.Subsystem->QueryNodeDrive(TEXT("ComponentNode"), State));
	TestEqual(TEXT("Load forwarded"), State.AppliedLoadTorque, 15.0f);
	TestEqual(TEXT("Speed limit forwarded"), State.MaxOperatingSpeed, 50.0f);
	TestEqual(TEXT("Detached policy forwarded"), State.DetachedPolicy, EDocMechanicalDetachedPolicy::FreeVisualCoast);

	Fix.AddSource(TEXT("Motor"), TEXT("ComponentNode"), 10.0f, 100.0f);
	Fix.Subsystem->StepSimulation(1.0f);
	TestTrue(TEXT("Simulation never moves the owning actor"), TestActor->GetActorTransform().Equals(Before));

	// Teardown releases the claim without deleting the logical record.
	Fix.Subsystem->UnregisterNodeComponent(Comp);
	Fix.Subsystem->QueryNodeDrive(TEXT("ComponentNode"), State);
	TestTrue(TEXT("Record retained as suspended"), State.bSuspended);
	Comp->DestroyComponent();
	return true;
}

// MEC-09: Missing nodes and restored controls follow the declared suspension policy
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalUnloadRestoreTest, FAutomationTestBase, "Doc.Mechanical.UnloadRestore", DocMechanicalTests::Flags)
bool FDocMechanicalUnloadRestoreTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("MotorNode"));
	Fix.AddSource(TEXT("Motor"), TEXT("MotorNode"), 10.0f);
	Fix.AddEdge(TEXT("DanglingEdge"), TEXT("MotorNode"), TEXT("MissingNode"), 2.0f);

	// FailClosed (default): an unknown load is not zero load.
	TestTrue(TEXT("Solve succeeds"), Fix.Subsystem->SolveNetwork().bSuccess);
	FDocDriveSourceState Source;
	Fix.Subsystem->QueryDriveSource(TEXT("Motor"), Source);
	TestEqual(TEXT("Fail closed: stalled"), Source.DriveState, EDocDriveStallState::Stalled);
	TestEqual(TEXT("Reason names the missing endpoint"), Source.StatusReason, FName(TEXT("MissingEndpoint")));
	FDocMechanicalNodeState MotorState;
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	TestEqual(TEXT("No motion"), MotorState.ActualSpeed, 0.0f);
	TestEqual(TEXT("Request kept visible"), MotorState.RequestedSpeed, 10.0f);

	// Suspend policy: the tree is suspended, not reported as a stall.
	Fix.Subsystem->MissingLoadPolicy = EDocMechanicalMissingLoadPolicy::Suspend;
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryDriveSource(TEXT("Motor"), Source);
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	TestEqual(TEXT("Suspended reason"), Source.StatusReason, FName(TEXT("Suspended")));
	TestFalse(TEXT("Not driven while suspended"), MotorState.bIsDriven);
	TestEqual(TEXT("No motion while suspended"), MotorState.ActualSpeed, 0.0f);

	// Resolving the endpoint restores motion; phase is retained across an unload.
	Fix.AddNode(TEXT("MissingNode"));
	Fix.Subsystem->StepSimulation(1.0f);
	FDocMechanicalNodeState Child;
	Fix.Subsystem->QueryNodeDrive(TEXT("MissingNode"), Child);
	TestEqual(TEXT("Endpoint resolved: running at 20 rad/s"), Child.ActualSpeed, 20.0f);
	const double ChildPhase = Child.PhaseAngle;
	TestTrue(TEXT("Unload the child"), Fix.Subsystem->SuspendNode(TEXT("MissingNode")));
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryNodeDrive(TEXT("MissingNode"), Child);
	TestTrue(TEXT("Unloaded record keeps its phase"), FMath::IsNearlyEqual(Child.PhaseAngle, ChildPhase, 1e-9));
	Fix.Subsystem->ResumeNode(TEXT("MissingNode"));
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryNodeDrive(TEXT("MissingNode"), Child);
	TestEqual(TEXT("Reloaded: running again"), Child.ActualSpeed, 20.0f);

	// Controls round-trip through capture/restore.
	Fix.AddNode(TEXT("Aux"));
	Fix.AddEdge(TEXT("AuxClutch"), TEXT("MotorNode"), TEXT("Aux"), 1.0f, 1.0f, true, true);
	Fix.Subsystem->SetClutchState(TEXT("AuxClutch"), false);
	Fix.Subsystem->SetSourceSpeed(TEXT("Motor"), 7.0f);
	FDocMechanicalSnapshot Snapshot;
	Fix.Subsystem->CaptureState(Snapshot);
	Fix.Subsystem->SetClutchState(TEXT("AuxClutch"), true);
	Fix.Subsystem->SetSourceSpeed(TEXT("Motor"), 1.0f);
	TestTrue(TEXT("Restore controls"), Fix.Subsystem->StageRestore(Snapshot));
	Fix.Subsystem->SolveNetwork();
	FDocMechanicalNodeState Aux;
	Fix.Subsystem->QueryNodeDrive(TEXT("Aux"), Aux);
	Fix.Subsystem->QueryNodeDrive(TEXT("MotorNode"), MotorState);
	TestFalse(TEXT("Clutch restored disengaged"), Aux.bIsDriven);
	TestEqual(TEXT("Source speed restored"), MotorState.ActualSpeed, 7.0f);
	return true;
}

// MEC-10: Extreme ratios/speeds fail safely; no Power or physics-plugin dependency
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMechanicalBoundsAndIsolationTest, FAutomationTestBase, "Doc.Mechanical.BoundsAndIsolation", DocMechanicalTests::Flags)
bool FDocMechanicalBoundsAndIsolationTest::RunTest(const FString& Parameters)
{
	DocMechanicalTests::FFixture Fix;
	Fix.AddNode(TEXT("RootNode"));
	FDocMechanicalNodeState Limited;
	Limited.NodeId = TEXT("ExtremeNode");
	Limited.AppliedLoadTorque = 10.0f;
	Limited.MaxOperatingSpeed = 1000.0f;
	TestTrue(TEXT("Speed-limited node"), Fix.Subsystem->RegisterNode(Limited));
	Fix.AddSource(TEXT("Motor"), TEXT("RootNode"), 1000.0f, 1e7f);
	Fix.AddEdge(TEXT("ExtremeDrive"), TEXT("RootNode"), TEXT("ExtremeNode"), 10000.0f, 0.95f);

	int32 Faults = 0;
	Fix.Subsystem->OnStalledNative.AddLambda([&Faults](FName, FName Reason) { if (Reason == TEXT("Overspeed")) { ++Faults; } });
	TestTrue(TEXT("Solve completes"), Fix.Subsystem->SolveNetwork().bSuccess);
	FDocDriveSourceState Source;
	Fix.Subsystem->QueryDriveSource(TEXT("Motor"), Source);
	TestEqual(TEXT("Overspeed faults the drive"), Source.DriveState, EDocDriveStallState::Overloaded);
	TestEqual(TEXT("Overspeed event"), Faults, 1);
	FDocMechanicalNodeState Ext;
	Fix.Subsystem->QueryNodeDrive(TEXT("ExtremeNode"), Ext);
	TestEqual(TEXT("No motion is published for an over-limit request"), Ext.ActualSpeed, 0.0f);
	TestEqual(TEXT("The offending request stays visible"), Ext.RequestedSpeed, 1.0e7f);
	TestTrue(TEXT("Reflected torque finite"), FMath::IsFinite(Ext.ReflectedLoadTorque));
	const double PhaseBefore = Ext.PhaseAngle;
	Fix.Subsystem->StepSimulation(1.0f);
	Fix.Subsystem->QueryNodeDrive(TEXT("ExtremeNode"), Ext);
	TestEqual(TEXT("Faulted output does not advance"), Ext.PhaseAngle, PhaseBefore);

	Fix.Subsystem->SetSourceSpeed(TEXT("Motor"), 0.05f); // 500 rad/s at the output: within the limit
	Fix.Subsystem->SolveNetwork();
	Fix.Subsystem->QueryNodeDrive(TEXT("ExtremeNode"), Ext);
	TestEqual(TEXT("Within the limit it runs"), Ext.ActualSpeed, 500.0f, 1e-2f);

	FDocDriveEdge Bad;
	Bad.EdgeId = TEXT("BadEdge");
	Bad.ParentNodeId = TEXT("RootNode");
	Bad.ChildNodeId = TEXT("Other");
	Bad.Ratio = 0.0f;
	TestFalse(TEXT("Zero ratio refused"), Fix.Subsystem->ConnectDrive(Bad));
	Bad.Ratio = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN ratio refused"), Fix.Subsystem->ConnectDrive(Bad));
	Bad.Ratio = 1.0f;
	Bad.Efficiency = 1.5f;
	TestFalse(TEXT("Efficiency above 1 refused"), Fix.Subsystem->ConnectDrive(Bad));
	TestFalse(TEXT("Negative load refused"), Fix.Subsystem->SetNodeLoad(TEXT("RootNode"), -1.0f));
	TestFalse(TEXT("Non-finite speed refused"), Fix.Subsystem->SetSourceSpeed(TEXT("Motor"), std::numeric_limits<float>::infinity()));
	FDocMechanicalNodeState Dup;
	Dup.NodeId = TEXT("RootNode");
	TestFalse(TEXT("Duplicate node refused"), Fix.Subsystem->RegisterNode(Dup));
	Fix.Subsystem->StepSimulation(std::numeric_limits<float>::quiet_NaN());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
