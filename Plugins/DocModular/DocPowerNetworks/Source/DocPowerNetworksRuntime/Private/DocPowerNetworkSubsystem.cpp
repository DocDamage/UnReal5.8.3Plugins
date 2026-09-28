#include "DocPowerNetworkSubsystem.h"
#include "DocPowerNetworksLog.h"
#include "DocPowerNodeComponent.h"
#include "DocPowerSourceComponent.h"
#include "DocPowerConsumerComponent.h"
#include "DocPowerStorageComponent.h"

namespace DocPowerPrivate
{
	static bool IsFiniteNonNegative(double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}

	static bool IsValidEfficiency(double Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0 && Value <= 1.0;
	}

	static bool CanDischarge(const FDocPowerInternalNode& Node)
	{
		return Node.StorageMode == EDocPowerStorageMode::Auto || Node.StorageMode == EDocPowerStorageMode::DischargeOnly;
	}

	static bool CanCharge(const FDocPowerInternalNode& Node)
	{
		return Node.StorageMode == EDocPowerStorageMode::Auto || Node.StorageMode == EDocPowerStorageMode::ChargeOnly;
	}
}

void UDocPowerNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TopologyRevision = 1;
	StepOrdinal = 0;
	PendingLagSeconds = 0.0;
	UE_LOG(LogDocPowerNetworks, Log, TEXT("DocPowerNetworkSubsystem initialized"));
}

void UDocPowerNetworkSubsystem::Deinitialize()
{
	Nodes.Empty();
	Edges.Empty();
	Super::Deinitialize();
}

void UDocPowerNetworkSubsystem::MarkTopologyDirty()
{
	++TopologyRevision;
}

FDocSystemResult UDocPowerNetworkSubsystem::CheckExpectedRevision(int64 ExpectedTopologyRevision) const
{
	if (ExpectedTopologyRevision >= 0 && ExpectedTopologyRevision != TopologyRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Stale topology revision (expected %lld, current %lld)"), ExpectedTopologyRevision, TopologyRevision));
	}
	return FDocSystemResult::MakeSuccess();
}

double UDocPowerNetworkSubsystem::SumStoredEnergy() const
{
	double Sum = 0.0;
	for (const TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		if (Kvp.Value.Kind == EDocPowerNodeKind::Storage)
		{
			Sum += Kvp.Value.CurrentEnergyJoules;
		}
	}
	return Sum;
}

FDocSystemResult UDocPowerNetworkSubsystem::RegisterNode(UDocPowerNodeComponent* Component)
{
	using namespace DocPowerPrivate;
	if (!Component || Component->NodeId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid power node component or NodeId"));
	}

	if (FDocPowerInternalNode* Existing = Nodes.Find(Component->NodeId))
	{
		if (Existing->Component.Get() == Component)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Component already registered"));
		}
		if (Existing->bSuspended && !Existing->Component.IsValid() && Existing->Kind == Component->NodeKind)
		{
			// Stream reload: re-bind the retained record; its stored energy and edges are authoritative.
			Existing->Component = Component;
			Existing->bSuspended = false;
			Existing->bEnabled = true;
			MarkTopologyDirty();
			return FDocSystemResult::MakeSuccess();
		}
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("NodeId %s is already registered"), *Component->NodeId.ToString()));
	}

	FDocPowerInternalNode InternalNode;
	InternalNode.NodeId = Component->NodeId;
	InternalNode.Kind = Component->NodeKind;
	InternalNode.Component = Component;
	for (const FName& PName : Component->PortNames)
	{
		InternalNode.PortNames.Add(PName);
	}
	if (InternalNode.PortNames.Num() == 0)
	{
		InternalNode.PortNames.Add(TEXT("Main"));
	}

	if (UDocPowerSourceComponent* Src = Cast<UDocPowerSourceComponent>(Component))
	{
		if (!IsFiniteNonNegative(Src->MaxPowerWatts) || !FMath::IsFinite(Src->Availability) || Src->Availability < 0.0 || Src->Availability > 1.0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Source power must be finite and >= 0, availability in [0,1]"));
		}
		InternalNode.MaxPowerWatts = Src->MaxPowerWatts;
		InternalNode.SourcePriority = Src->Priority;
		InternalNode.bExternalSource = Src->bExternal;
		InternalNode.Availability = Src->Availability;
	}
	else if (UDocPowerConsumerComponent* Consumer = Cast<UDocPowerConsumerComponent>(Component))
	{
		if (!IsFiniteNonNegative(Consumer->DesiredPowerWatts) || !IsFiniteNonNegative(Consumer->MinimumPowerWatts)
			|| !FMath::IsFinite(Consumer->RecoveryDropoutHysteresis) || Consumer->RecoveryDropoutHysteresis < 0.0 || Consumer->RecoveryDropoutHysteresis > 1.0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Consumer demand must be finite and >= 0, hysteresis in [0,1]"));
		}
		if (Consumer->AllocationMode == EDocPowerAllocationMode::Scalable && Consumer->MinimumPowerWatts > Consumer->DesiredPowerWatts)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Scalable consumer minimum exceeds desired power"));
		}
		InternalNode.DesiredPowerWatts = Consumer->DesiredPowerWatts;
		InternalNode.MinimumPowerWatts = Consumer->MinimumPowerWatts;
		InternalNode.ConsumerPriority = Consumer->Priority;
		InternalNode.TieBreakId = Consumer->TieBreakId;
		InternalNode.AllocationMode = Consumer->AllocationMode;
		InternalNode.RecoveryDropoutHysteresis = Consumer->RecoveryDropoutHysteresis;
	}
	else if (UDocPowerStorageComponent* Storage = Cast<UDocPowerStorageComponent>(Component))
	{
		if (!IsFiniteNonNegative(Storage->CapacityJoules) || !IsFiniteNonNegative(Storage->CurrentEnergyJoules) || Storage->CurrentEnergyJoules > Storage->CapacityJoules
			|| !IsFiniteNonNegative(Storage->MaxChargeWatts) || !IsFiniteNonNegative(Storage->MaxDischargeWatts)
			|| !IsValidEfficiency(Storage->ChargeEfficiency) || !IsValidEfficiency(Storage->DischargeEfficiency))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Storage values must be finite, energy within capacity, efficiencies in (0,1]"));
		}
		InternalNode.CapacityJoules = Storage->CapacityJoules;
		InternalNode.CurrentEnergyJoules = Storage->CurrentEnergyJoules;
		InternalNode.MaxChargeWatts = Storage->MaxChargeWatts;
		InternalNode.MaxDischargeWatts = Storage->MaxDischargeWatts;
		InternalNode.ChargeEfficiency = Storage->ChargeEfficiency;
		InternalNode.DischargeEfficiency = Storage->DischargeEfficiency;
		InternalNode.StorageMode = Storage->StorageMode;
	}

	Nodes.Add(Component->NodeId, InternalNode);
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::RegisterNodeRaw(FName NodeId, EDocPowerNodeKind Kind, const TArray<FName>& Ports)
{
	if (NodeId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("NodeId is None"));
	}
	if (Nodes.Contains(NodeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("NodeId %s is already registered"), *NodeId.ToString()));
	}

	FDocPowerInternalNode InternalNode;
	InternalNode.NodeId = NodeId;
	InternalNode.Kind = Kind;
	for (const FName& PName : Ports)
	{
		InternalNode.PortNames.Add(PName);
	}
	if (InternalNode.PortNames.Num() == 0)
	{
		InternalNode.PortNames.Add(TEXT("Main"));
	}

	Nodes.Add(NodeId, InternalNode);
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::UnregisterNode(FName NodeId)
{
	if (!Nodes.Contains(NodeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Node not found"));
	}

	Nodes.Remove(NodeId);
	for (auto It = Edges.CreateIterator(); It; ++It)
	{
		if (It->Value.PortA.NodeId == NodeId || It->Value.PortB.NodeId == NodeId)
		{
			It.RemoveCurrent();
		}
	}
	for (TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		Kvp.Value.ProtectedBranchNodes.Remove(NodeId);
	}

	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SuspendNode(FName NodeId)
{
	FDocPowerInternalNode* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Node not found"));
	}
	if (Node->bSuspended)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Node->bSuspended = true;
	Node->bEnabled = false;
	Node->Component.Reset();
	Node->DeliveredWatts = 0.0;
	if (Node->Kind == EDocPowerNodeKind::Consumer)
	{
		Node->ConsumerState = EDocPowerConsumerState::Disconnected;
	}
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

bool UDocPowerNetworkSubsystem::HasNode(FName NodeId) const
{
	return Nodes.Contains(NodeId);
}

bool UDocPowerNetworkSubsystem::IsNodeSuspended(FName NodeId) const
{
	const FDocPowerInternalNode* Node = Nodes.Find(NodeId);
	return Node && Node->bSuspended;
}

bool UDocPowerNetworkSubsystem::IsRadialBranch(FName BreakerId, const TSet<FName>& Branch, const TMap<FName, FDocPowerEdge>& EdgeSet) const
{
	if (Branch.Num() == 0)
	{
		return true; // Island-level protection.
	}

	// Structural graph (every edge and switch counts as potentially conductive), without the breaker itself.
	TMap<FName, TArray<FName>> Adjacency;
	bool bBreakerFeedsBranch = false;
	for (const TPair<FName, FDocPowerEdge>& Kvp : EdgeSet)
	{
		const FName A = Kvp.Value.PortA.NodeId;
		const FName B = Kvp.Value.PortB.NodeId;
		if (A == BreakerId || B == BreakerId)
		{
			const FName Other = A == BreakerId ? B : A;
			bBreakerFeedsBranch |= Branch.Contains(Other);
			continue;
		}
		Adjacency.FindOrAdd(A).Add(B);
		Adjacency.FindOrAdd(B).Add(A);
	}
	if (!bBreakerFeedsBranch)
	{
		return false; // Not downstream of this breaker.
	}

	// Any path from the branch to a node outside it, other than through the breaker, is a mesh.
	TArray<FName> Frontier = Branch.Array();
	TSet<FName> Visited(Branch);
	while (Frontier.Num() > 0)
	{
		const FName Current = Frontier.Pop();
		if (const TArray<FName>* Neighbors = Adjacency.Find(Current))
		{
			for (const FName& Neighbor : *Neighbors)
			{
				if (!Branch.Contains(Neighbor))
				{
					return false;
				}
				if (!Visited.Contains(Neighbor))
				{
					Visited.Add(Neighbor);
					Frontier.Add(Neighbor);
				}
			}
		}
	}
	return true;
}

FDocSystemResult UDocPowerNetworkSubsystem::ValidateBreakers(const TMap<FName, FDocPowerEdge>& EdgeSet) const
{
	for (const TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		const FDocPowerInternalNode& Node = Kvp.Value;
		if (Node.Kind != EDocPowerNodeKind::Breaker)
		{
			continue;
		}
		if (Node.bIsMeshedProtection || !IsRadialBranch(Node.NodeId, Node.ProtectedBranchNodes, EdgeSet))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Breaker %s: meshed branch protection is unsupported"), *Node.NodeId.ToString()));
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::ConnectPorts(FName EdgeId, const FDocPowerPortId& PortA, const FDocPowerPortId& PortB, bool bClosed, int64 ExpectedTopologyRevision)
{
	if (EdgeId.IsNone() || !PortA.IsValid() || !PortB.IsValid() || PortA == PortB)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid EdgeId or PortId"));
	}
	const FDocSystemResult Revision = CheckExpectedRevision(ExpectedTopologyRevision);
	if (!Revision.IsSuccess())
	{
		return Revision;
	}
	const FDocPowerInternalNode* NodeA = Nodes.Find(PortA.NodeId);
	const FDocPowerInternalNode* NodeB = Nodes.Find(PortB.NodeId);
	if (!NodeA || !NodeB)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Endpoint node does not exist"));
	}
	if (!NodeA->PortNames.Contains(PortA.PortName) || !NodeB->PortNames.Contains(PortB.PortName))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Endpoint port does not exist on the node"));
	}
	if (Edges.Contains(EdgeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("EdgeId %s already exists"), *EdgeId.ToString()));
	}

	// Structural edits are atomic: validate breaker branches against the edited graph before committing.
	TMap<FName, FDocPowerEdge> Proposed = Edges;
	Proposed.Add(EdgeId, FDocPowerEdge(EdgeId, PortA, PortB, bClosed));
	const FDocSystemResult Breakers = ValidateBreakers(Proposed);
	if (!Breakers.IsSuccess())
	{
		return Breakers;
	}

	Edges = MoveTemp(Proposed);
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::DisconnectPorts(FName EdgeId, int64 ExpectedTopologyRevision)
{
	const FDocSystemResult Revision = CheckExpectedRevision(ExpectedTopologyRevision);
	if (!Revision.IsSuccess())
	{
		return Revision;
	}
	if (!Edges.Contains(EdgeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Edge not found"));
	}
	Edges.Remove(EdgeId);
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SetEdgeClosed(FName EdgeId, bool bClosed, int64 ExpectedTopologyRevision)
{
	const FDocSystemResult Revision = CheckExpectedRevision(ExpectedTopologyRevision);
	if (!Revision.IsSuccess())
	{
		return Revision;
	}
	FDocPowerEdge* Edge = Edges.Find(EdgeId);
	if (!Edge)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Edge not found"));
	}
	if (Edge->bClosed != bClosed)
	{
		Edge->bClosed = bClosed;
		MarkTopologyDirty();
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SetSwitchState(FName SwitchNodeId, bool bClosed, int64 ExpectedTopologyRevision)
{
	const FDocSystemResult Revision = CheckExpectedRevision(ExpectedTopologyRevision);
	if (!Revision.IsSuccess())
	{
		return Revision;
	}
	FDocPowerInternalNode* Node = Nodes.Find(SwitchNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Switch)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Switch node not found"));
	}
	if (Node->bSwitchClosed != bClosed)
	{
		Node->bSwitchClosed = bClosed;
		MarkTopologyDirty();
	}
	return FDocSystemResult::MakeSuccess();
}

bool UDocPowerNetworkSubsystem::GetSwitchState(FName SwitchNodeId) const
{
	const FDocPowerInternalNode* Node = Nodes.Find(SwitchNodeId);
	return Node ? Node->bSwitchClosed : false;
}

FDocSystemResult UDocPowerNetworkSubsystem::SetSourceAvailability(FName SourceNodeId, double InAvailability)
{
	FDocPowerInternalNode* Node = Nodes.Find(SourceNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Source)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Source node not found"));
	}
	if (!FMath::IsFinite(InAvailability) || InAvailability < 0.0 || InAvailability > 1.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Availability must be in [0,1]"));
	}
	Node->Availability = InAvailability;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SetConsumerDemand(FName ConsumerNodeId, double InDesiredWatts, double InMinimumWatts)
{
	FDocPowerInternalNode* Node = Nodes.Find(ConsumerNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Consumer)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Consumer node not found"));
	}
	if (!DocPowerPrivate::IsFiniteNonNegative(InDesiredWatts) || !DocPowerPrivate::IsFiniteNonNegative(InMinimumWatts))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Negative or non-finite demand"));
	}
	if (Node->AllocationMode == EDocPowerAllocationMode::Scalable && InMinimumWatts > InDesiredWatts)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Scalable minimum exceeds desired power"));
	}
	Node->DesiredPowerWatts = InDesiredWatts;
	Node->MinimumPowerWatts = InMinimumWatts;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SetStorageMode(FName StorageNodeId, EDocPowerStorageMode InMode)
{
	FDocPowerInternalNode* Node = Nodes.Find(StorageNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Storage)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Storage node not found"));
	}
	Node->StorageMode = InMode;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::SetStorageEnergy(FName StorageNodeId, double InEnergyJoules)
{
	FDocPowerInternalNode* Node = Nodes.Find(StorageNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Storage)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Storage node not found"));
	}
	if (!DocPowerPrivate::IsFiniteNonNegative(InEnergyJoules) || InEnergyJoules > Node->CapacityJoules)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Energy must be finite and within [0, capacity]"));
	}
	Node->CurrentEnergyJoules = InEnergyJoules;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::ConfigureBreaker(FName BreakerNodeId, double InOverloadWatts, double InTripDurationSeconds, double InCooldownSeconds, const TSet<FName>& InProtectedBranch, bool bInMeshedProtection)
{
	FDocPowerInternalNode* Node = Nodes.Find(BreakerNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Breaker)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Breaker node not found"));
	}
	if (!DocPowerPrivate::IsFiniteNonNegative(InOverloadWatts) || !DocPowerPrivate::IsFiniteNonNegative(InTripDurationSeconds) || !DocPowerPrivate::IsFiniteNonNegative(InCooldownSeconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Breaker values must be finite and >= 0"));
	}
	for (const FName& Member : InProtectedBranch)
	{
		if (!Nodes.Contains(Member) || Member == BreakerNodeId)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Protected branch node %s is not valid"), *Member.ToString()));
		}
	}
	if (bInMeshedProtection)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Meshed branch protection is unsupported"));
	}
	if (!IsRadialBranch(BreakerNodeId, InProtectedBranch, Edges))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Protected branch is not a unique radial branch of this breaker: meshed protection is unsupported"));
	}

	Node->OverloadThresholdWatts = InOverloadWatts;
	Node->TripDurationSeconds = InTripDurationSeconds;
	Node->TripCooldownSeconds = InCooldownSeconds;
	Node->ProtectedBranchNodes = InProtectedBranch;
	Node->bIsMeshedProtection = false;
	Node->OverloadAccumSeconds = 0.0;
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::RequestBreakerReset(FName BreakerNodeId)
{
	FDocPowerInternalNode* Node = Nodes.Find(BreakerNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Breaker)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Breaker node not found"));
	}
	if (!Node->bTripped)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Breaker is not tripped"));
	}
	if (Node->RemainingCooldownSeconds > 0.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, FString::Printf(TEXT("Breaker %s cooldown active (%.2f s remaining)"), *BreakerNodeId.ToString(), Node->RemainingCooldownSeconds));
	}

	Node->bTripped = false;
	Node->RemainingTripSeconds = 0.0;
	Node->OverloadAccumSeconds = 0.0;
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}

bool UDocPowerNetworkSubsystem::IsBreakerTripped(FName BreakerNodeId) const
{
	const FDocPowerInternalNode* Node = Nodes.Find(BreakerNodeId);
	return Node ? Node->bTripped : false;
}

void UDocPowerNetworkSubsystem::BuildIslands(TArray<TArray<FName>>& OutIslands) const
{
	OutIslands.Empty();

	auto Conducts = [](const FDocPowerInternalNode* Node)
	{
		if (!Node || !Node->bEnabled)
		{
			return false;
		}
		if (Node->Kind == EDocPowerNodeKind::Switch && !Node->bSwitchClosed)
		{
			return false;
		}
		if (Node->Kind == EDocPowerNodeKind::Breaker && Node->bTripped)
		{
			return false;
		}
		return true;
	};

	TArray<FName> Order;
	TMap<FName, TArray<FName>> Adjacency;
	for (const TPair<FName, FDocPowerInternalNode>& NodeKvp : Nodes)
	{
		if (NodeKvp.Value.bEnabled)
		{
			Adjacency.FindOrAdd(NodeKvp.Key);
			Order.Add(NodeKvp.Key);
		}
	}
	Order.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	for (const TPair<FName, FDocPowerEdge>& EdgeKvp : Edges)
	{
		const FDocPowerEdge& Edge = EdgeKvp.Value;
		if (!Edge.bClosed)
		{
			continue;
		}
		if (!Conducts(Nodes.Find(Edge.PortA.NodeId)) || !Conducts(Nodes.Find(Edge.PortB.NodeId)))
		{
			continue;
		}
		Adjacency.FindOrAdd(Edge.PortA.NodeId).Add(Edge.PortB.NodeId);
		Adjacency.FindOrAdd(Edge.PortB.NodeId).Add(Edge.PortA.NodeId);
	}

	TSet<FName> Visited;
	for (const FName& StartNode : Order)
	{
		if (Visited.Contains(StartNode))
		{
			continue;
		}

		TArray<FName> IslandNodes;
		TArray<FName> Queue;
		Queue.Add(StartNode);
		Visited.Add(StartNode);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FName Current = Queue[Head];
			IslandNodes.Add(Current);
			if (const TArray<FName>* Neighbors = Adjacency.Find(Current))
			{
				for (const FName& Neighbor : *Neighbors)
				{
					if (!Visited.Contains(Neighbor))
					{
						Visited.Add(Neighbor);
						Queue.Add(Neighbor);
					}
				}
			}
		}

		IslandNodes.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		OutIslands.Add(IslandNodes);
	}
}

FDocSystemResult UDocPowerNetworkSubsystem::QueryIsland(FName NodeId, FDocPowerIslandState& OutIsland) const
{
	if (!Nodes.Contains(NodeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Node not found"));
	}

	TArray<TArray<FName>> AllIslands;
	BuildIslands(AllIslands);

	for (int32 Idx = 0; Idx < AllIslands.Num(); ++Idx)
	{
		const TArray<FName>& IslandNodes = AllIslands[Idx];
		if (IslandNodes.Contains(NodeId))
		{
			OutIsland = FDocPowerIslandState();
			OutIsland.IslandId = Idx;
			OutIsland.NodeIds = IslandNodes;
			for (const FName& NId : IslandNodes)
			{
				if (const FDocPowerInternalNode* Node = Nodes.Find(NId))
				{
					if (Node->Kind == EDocPowerNodeKind::Source)
					{
						OutIsland.TotalPotentialSupplyWatts += Node->MaxPowerWatts * Node->Availability;
					}
					else if (Node->Kind == EDocPowerNodeKind::Consumer)
					{
						OutIsland.TotalRequestedWatts += Node->DesiredPowerWatts;
						OutIsland.TotalDeliveredWatts += Node->DeliveredWatts;
					}
					else if (Node->Kind == EDocPowerNodeKind::Storage)
					{
						OutIsland.TotalStoredEnergyJoules += Node->CurrentEnergyJoules;
					}
					else if (Node->Kind == EDocPowerNodeKind::Breaker && Node->bTripped)
					{
						OutIsland.bHasTripOrFault = true;
					}
				}
			}
			return FDocSystemResult::MakeSuccess();
		}
	}

	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Node is not part of any island (suspended or disabled)"));
}

void UDocPowerNetworkSubsystem::GetAllIslands(TArray<FDocPowerIslandState>& OutIslands) const
{
	OutIslands.Empty();
	TArray<TArray<FName>> IslandGroups;
	BuildIslands(IslandGroups);

	for (int32 Idx = 0; Idx < IslandGroups.Num(); ++Idx)
	{
		FDocPowerIslandState Island;
		Island.IslandId = Idx;
		Island.NodeIds = IslandGroups[Idx];
		bool bHasPowerNode = false;

		for (const FName& NId : Island.NodeIds)
		{
			if (const FDocPowerInternalNode* Node = Nodes.Find(NId))
			{
				if (Node->Kind == EDocPowerNodeKind::Source)
				{
					Island.TotalPotentialSupplyWatts += Node->MaxPowerWatts * Node->Availability;
					bHasPowerNode = true;
				}
				else if (Node->Kind == EDocPowerNodeKind::Consumer)
				{
					Island.TotalRequestedWatts += Node->DesiredPowerWatts;
					Island.TotalDeliveredWatts += Node->DeliveredWatts;
					bHasPowerNode = true;
				}
				else if (Node->Kind == EDocPowerNodeKind::Storage)
				{
					Island.TotalStoredEnergyJoules += Node->CurrentEnergyJoules;
					bHasPowerNode = true;
				}
				else if (Node->Kind == EDocPowerNodeKind::Breaker && Node->bTripped)
				{
					Island.bHasTripOrFault = true;
				}
			}
		}

		if (bHasPowerNode)
		{
			OutIslands.Add(Island);
		}
	}
}

bool UDocPowerNetworkSubsystem::GetNodeSupply(FName ConsumerNodeId, double& OutDeliveredWatts, EDocPowerConsumerState& OutState) const
{
	const FDocPowerInternalNode* Node = Nodes.Find(ConsumerNodeId);
	if (!Node || Node->Kind != EDocPowerNodeKind::Consumer)
	{
		return false;
	}
	OutDeliveredWatts = Node->DeliveredWatts;
	OutState = Node->ConsumerState;
	return true;
}

FDocSystemResult UDocPowerNetworkSubsystem::SetStepLimits(double InMaxStepSeconds, int32 InMaxCatchUpSteps)
{
	if (!FMath::IsFinite(InMaxStepSeconds) || InMaxStepSeconds <= 0.0 || InMaxCatchUpSteps < 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Step limits must be positive"));
	}
	MaxStepSeconds = InMaxStepSeconds;
	MaxCatchUpSteps = InMaxCatchUpSteps;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPowerNetworkSubsystem::StepSimulation(float DeltaSeconds)
{
	// Validation first: a refused step changes nothing.
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("DeltaSeconds must be positive and finite"));
	}
	const FDocSystemResult Breakers = ValidateBreakers(Edges);
	if (!Breakers.IsSuccess())
	{
		return Breakers;
	}

	// Record consumer outputs to report only committed changes.
	TMap<FName, TPair<double, EDocPowerConsumerState>> Before;
	for (const TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		if (Kvp.Value.Kind == EDocPowerNodeKind::Consumer)
		{
			Before.Add(Kvp.Key, TPair<double, EDocPowerConsumerState>(Kvp.Value.DeliveredWatts, Kvp.Value.ConsumerState));
		}
	}

	FDocEnergyLedger Ledger;
	Ledger.StoredEnergyBeforeJoules = SumStoredEnergy();
	TArray<TPair<FName, FString>> Trips;

	double Remaining = PendingLagSeconds + (double)DeltaSeconds;
	int32 Steps = 0;
	while (Remaining > 1e-9 && Steps < MaxCatchUpSteps)
	{
		const double Dt = FMath::Min(Remaining, MaxStepSeconds);
		SolveOneStep(Dt, Ledger, Trips);
		Remaining -= Dt;
		++Steps;
		++StepOrdinal;
	}
	// Budget exhausted: the rest is visible lag, never silently dropped or compressed into a longer step.
	PendingLagSeconds = Remaining > 1e-9 ? Remaining : 0.0;
	Ledger.StoredEnergyAfterJoules = SumStoredEnergy();
	LastLedger = Ledger;

	// Notify after all state is committed.
	for (const TPair<FName, FString>& Trip : Trips)
	{
		OnBreakerTripped.Broadcast(Trip.Key, Trip.Value);
		OnBreakerTrippedNative.Broadcast(Trip.Key, Trip.Value);
	}
	TArray<TTuple<FName, double, EDocPowerConsumerState, TWeakObjectPtr<UDocPowerNodeComponent>>> Changes;
	for (const TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		const FDocPowerInternalNode& Node = Kvp.Value;
		if (Node.Kind != EDocPowerNodeKind::Consumer)
		{
			continue;
		}
		const TPair<double, EDocPowerConsumerState>* Prev = Before.Find(Kvp.Key);
		if (!Prev || Prev->Key != Node.DeliveredWatts || Prev->Value != Node.ConsumerState)
		{
			Changes.Add(MakeTuple(Node.NodeId, Node.DeliveredWatts, Node.ConsumerState, Node.Component));
		}
	}
	for (const auto& Change : Changes)
	{
		if (UDocPowerConsumerComponent* ConsumerComp = Cast<UDocPowerConsumerComponent>(Change.Get<3>().Get()))
		{
			ConsumerComp->UpdateSupply(Change.Get<1>(), Change.Get<2>());
		}
		OnPowerSupplyChanged.Broadcast(Change.Get<0>(), Change.Get<1>(), Change.Get<2>());
		OnPowerSupplyChangedNative.Broadcast(Change.Get<0>(), Change.Get<1>(), Change.Get<2>());
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocPowerNetworkSubsystem::SolveOneStep(double Dt, FDocEnergyLedger& Ledger, TArray<TPair<FName, FString>>& OutTrips)
{
	using namespace DocPowerPrivate;

	// 1. Breaker cooldowns run on simulation time.
	for (TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		FDocPowerInternalNode& Node = Kvp.Value;
		if (Node.Kind == EDocPowerNodeKind::Breaker && Node.bTripped)
		{
			Node.RemainingCooldownSeconds = FMath::Max(0.0, Node.RemainingCooldownSeconds - Dt);
		}
	}

	// 2. Trip decisions from the coherent pre-allocation demand snapshot. The overload must persist for the trip duration.
	TArray<TArray<FName>> Islands;
	BuildIslands(Islands);
	bool bAnyTrip = false;
	for (const TArray<FName>& Island : Islands)
	{
		for (const FName& NId : Island)
		{
			FDocPowerInternalNode* Breaker = Nodes.Find(NId);
			if (!Breaker || Breaker->Kind != EDocPowerNodeKind::Breaker || Breaker->bTripped || Breaker->OverloadThresholdWatts <= 0.0)
			{
				continue;
			}
			double Demand = 0.0;
			const bool bBranch = Breaker->ProtectedBranchNodes.Num() > 0;
			for (const FName& MemberId : Island)
			{
				if (bBranch && !Breaker->ProtectedBranchNodes.Contains(MemberId))
				{
					continue;
				}
				const FDocPowerInternalNode* Member = Nodes.Find(MemberId);
				if (Member && Member->Kind == EDocPowerNodeKind::Consumer)
				{
					Demand += Member->DesiredPowerWatts;
				}
			}

			if (Demand > Breaker->OverloadThresholdWatts)
			{
				Breaker->OverloadAccumSeconds += Dt;
				if (Breaker->OverloadAccumSeconds + 1e-9 >= Breaker->TripDurationSeconds)
				{
					Breaker->bTripped = true;
					Breaker->RemainingCooldownSeconds = Breaker->TripCooldownSeconds;
					Breaker->OverloadAccumSeconds = 0.0;
					bAnyTrip = true;
					OutTrips.Emplace(Breaker->NodeId, FString::Printf(TEXT("Overload: %.1f W > %.1f W for %.2f s (pre-allocation demand)"), Demand, Breaker->OverloadThresholdWatts, Breaker->TripDurationSeconds));
				}
			}
			else
			{
				Breaker->OverloadAccumSeconds = 0.0;
			}
		}
	}
	if (bAnyTrip)
	{
		MarkTopologyDirty();
		BuildIslands(Islands);
	}

	// 3. Remember last step's consumer state for recovery hysteresis, then reset.
	TMap<FName, EDocPowerConsumerState> PrevState;
	for (TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		if (Kvp.Value.Kind == EDocPowerNodeKind::Consumer)
		{
			PrevState.Add(Kvp.Key, Kvp.Value.ConsumerState);
			Kvp.Value.DeliveredWatts = 0.0;
			Kvp.Value.ConsumerState = EDocPowerConsumerState::Disconnected;
		}
	}

	// 4. Solve each island.
	for (const TArray<FName>& Island : Islands)
	{
		double ExternalAvail = 0.0;
		TArray<FName> Consumers;
		TArray<FName> Batteries;
		for (const FName& NId : Island)
		{
			const FDocPowerInternalNode* Node = Nodes.Find(NId);
			if (!Node || !Node->bEnabled)
			{
				continue;
			}
			if (Node->Kind == EDocPowerNodeKind::Source)
			{
				ExternalAvail += Node->MaxPowerWatts * Node->Availability;
			}
			else if (Node->Kind == EDocPowerNodeKind::Consumer)
			{
				Consumers.Add(NId);
			}
			else if (Node->Kind == EDocPowerNodeKind::Storage)
			{
				Batteries.Add(NId); // Island lists are already in lexical order.
			}
		}
		const bool bHasInfrastructure = ExternalAvail > 0.0 || Batteries.Num() > 0;

		// Battery output limits, validated before allocation: power limit and stored energy after efficiency.
		TMap<FName, double> BatteryCaps;
		double BatteryAvail = 0.0;
		for (const FName& BatId : Batteries)
		{
			const FDocPowerInternalNode& Bat = Nodes.FindChecked(BatId);
			double Cap = 0.0;
			if (CanDischarge(Bat) && Bat.CurrentEnergyJoules > 0.0)
			{
				Cap = FMath::Min(Bat.MaxDischargeWatts, Bat.CurrentEnergyJoules * Bat.DischargeEfficiency / Dt);
			}
			BatteryCaps.Add(BatId, Cap);
			BatteryAvail += Cap;
		}

		// Stable order: priority desc, tie-break id asc, NodeId lexical (never registration or hash order).
		Consumers.Sort([this](const FName& A, const FName& B) {
			const FDocPowerInternalNode& NodeA = Nodes.FindChecked(A);
			const FDocPowerInternalNode& NodeB = Nodes.FindChecked(B);
			if (NodeA.ConsumerPriority != NodeB.ConsumerPriority)
			{
				return NodeA.ConsumerPriority > NodeB.ConsumerPriority;
			}
			if (NodeA.TieBreakId != NodeB.TieBreakId)
			{
				return NodeA.TieBreakId < NodeB.TieBreakId;
			}
			return A.LexicalLess(B);
		});

		double Remaining = ExternalAvail + BatteryAvail;
		double Granted = 0.0;
		for (const FName& CId : Consumers)
		{
			FDocPowerInternalNode& Consumer = Nodes.FindChecked(CId);
			if (!bHasInfrastructure)
			{
				Consumer.ConsumerState = EDocPowerConsumerState::Disconnected;
				continue;
			}
			if (Consumer.DesiredPowerWatts <= 0.0)
			{
				Consumer.ConsumerState = EDocPowerConsumerState::Off;
				continue;
			}

			// Recovery hysteresis: a load that browned out needs a margin above its threshold to come back.
			const EDocPowerConsumerState* Prev = PrevState.Find(CId);
			const bool bRecovering = Prev && *Prev == EDocPowerConsumerState::Brownout;
			const double Margin = bRecovering ? (1.0 + Consumer.RecoveryDropoutHysteresis) : 1.0;

			double Grant = 0.0;
			if (Consumer.AllocationMode == EDocPowerAllocationMode::Binary)
			{
				if (Remaining >= Consumer.DesiredPowerWatts * Margin)
				{
					Grant = Consumer.DesiredPowerWatts;
				}
			}
			else
			{
				if (Remaining >= Consumer.DesiredPowerWatts)
				{
					Grant = Consumer.DesiredPowerWatts;
				}
				else if (Remaining > 0.0 && Remaining >= Consumer.MinimumPowerWatts * Margin)
				{
					Grant = Remaining; // Never below the authored minimum, from any mix of sources.
				}
			}

			Consumer.DeliveredWatts = Grant;
			Consumer.ConsumerState = Grant >= Consumer.DesiredPowerWatts ? EDocPowerConsumerState::Supplied : EDocPowerConsumerState::Brownout;
			Remaining -= Grant;
			Granted += Grant;
		}

		// Source the grants: external supply first, then batteries in stable order.
		const double ExternalUsed = FMath::Min(Granted, ExternalAvail);
		Ledger.ExternalEnergyAcceptedJoules += ExternalUsed * Dt;
		double FromBatteries = Granted - ExternalUsed;
		TSet<FName> Discharged;
		for (const FName& BatId : Batteries)
		{
			if (FromBatteries <= 0.0)
			{
				break;
			}
			const double Take = FMath::Min(FromBatteries, BatteryCaps.FindChecked(BatId));
			if (Take <= 0.0)
			{
				continue;
			}
			FDocPowerInternalNode& Bat = Nodes.FindChecked(BatId);
			const double OutJoules = Take * Dt;
			const double DrawnJoules = OutJoules / Bat.DischargeEfficiency;
			Bat.CurrentEnergyJoules = FMath::Max(0.0, Bat.CurrentEnergyJoules - DrawnJoules);
			Ledger.ConversionLossesJoules += DrawnJoules - OutJoules;
			FromBatteries -= Take;
			Discharged.Add(BatId);
		}
		Ledger.LoadEnergyDeliveredJoules += Granted * Dt;

		// Charge only from external supply left after committed loads; a discharging battery never charges in the same step.
		double Surplus = ExternalAvail - ExternalUsed;
		for (const FName& BatId : Batteries)
		{
			if (Surplus <= 0.0)
			{
				break;
			}
			if (Discharged.Contains(BatId))
			{
				continue;
			}
			FDocPowerInternalNode& Bat = Nodes.FindChecked(BatId);
			if (!CanCharge(Bat) || Bat.CurrentEnergyJoules >= Bat.CapacityJoules)
			{
				continue;
			}
			const double RoomJoules = Bat.CapacityJoules - Bat.CurrentEnergyJoules;
			const double AcceptWatts = FMath::Min3(Surplus, Bat.MaxChargeWatts, RoomJoules / (Bat.ChargeEfficiency * Dt));
			if (AcceptWatts <= 0.0)
			{
				continue;
			}
			const double InJoules = AcceptWatts * Dt;
			const double StoredJoules = InJoules * Bat.ChargeEfficiency;
			Bat.CurrentEnergyJoules = FMath::Min(Bat.CapacityJoules, Bat.CurrentEnergyJoules + StoredJoules);
			Ledger.ExternalEnergyAcceptedJoules += InJoules;
			Ledger.ConversionLossesJoules += InJoules - StoredJoules;
			Surplus -= AcceptWatts;
		}
	}
}

FDocPowerNetworkSnapshot UDocPowerNetworkSubsystem::CaptureNetworkState() const
{
	FDocPowerNetworkSnapshot Snapshot;
	Snapshot.TopologyRevision = TopologyRevision;
	Snapshot.StepOrdinal = StepOrdinal;
	Snapshot.PendingLagSeconds = PendingLagSeconds;

	for (const TPair<FName, FDocPowerInternalNode>& Kvp : Nodes)
	{
		const FDocPowerInternalNode& Node = Kvp.Value;
		if (Node.Kind == EDocPowerNodeKind::Storage)
		{
			Snapshot.StorageEnergies.Add(Node.NodeId, Node.CurrentEnergyJoules);
		}
		else if (Node.Kind == EDocPowerNodeKind::Switch)
		{
			Snapshot.SwitchStates.Add(Node.NodeId, Node.bSwitchClosed);
		}
		else if (Node.Kind == EDocPowerNodeKind::Breaker)
		{
			Snapshot.BreakerTripped.Add(Node.NodeId, Node.bTripped);
			Snapshot.BreakerRemainingCooldowns.Add(Node.NodeId, Node.RemainingCooldownSeconds);
			Snapshot.BreakerOverloadSeconds.Add(Node.NodeId, Node.OverloadAccumSeconds);
		}
	}
	return Snapshot;
}

FDocSystemResult UDocPowerNetworkSubsystem::RestoreNetworkState(const FDocPowerNetworkSnapshot& Snapshot, bool bForceMatchRevision)
{
	using namespace DocPowerPrivate;
	if (!bForceMatchRevision && Snapshot.TopologyRevision != TopologyRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Topology revision mismatch (snapshot %lld vs current %lld)"), Snapshot.TopologyRevision, TopologyRevision));
	}

	// Validate everything before applying anything.
	for (const TPair<FName, double>& Kvp : Snapshot.StorageEnergies)
	{
		const FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key);
		if (!IsFiniteNonNegative(Kvp.Value) || (Node && Kvp.Value > Node->CapacityJoules))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Invalid stored energy for %s"), *Kvp.Key.ToString()));
		}
	}
	for (const TPair<FName, double>& Kvp : Snapshot.BreakerRemainingCooldowns)
	{
		if (!IsFiniteNonNegative(Kvp.Value))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid breaker cooldown"));
		}
	}
	if (!IsFiniteNonNegative(Snapshot.PendingLagSeconds) || Snapshot.StepOrdinal < 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid step state"));
	}

	for (const TPair<FName, double>& Kvp : Snapshot.StorageEnergies)
	{
		if (FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key))
		{
			Node->CurrentEnergyJoules = Kvp.Value;
		}
	}
	for (const TPair<FName, bool>& Kvp : Snapshot.SwitchStates)
	{
		if (FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key))
		{
			Node->bSwitchClosed = Kvp.Value;
		}
	}
	for (const TPair<FName, bool>& Kvp : Snapshot.BreakerTripped)
	{
		if (FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key))
		{
			Node->bTripped = Kvp.Value;
		}
	}
	for (const TPair<FName, double>& Kvp : Snapshot.BreakerRemainingCooldowns)
	{
		if (FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key))
		{
			Node->RemainingCooldownSeconds = Kvp.Value;
		}
	}
	for (const TPair<FName, double>& Kvp : Snapshot.BreakerOverloadSeconds)
	{
		if (FDocPowerInternalNode* Node = Nodes.Find(Kvp.Key))
		{
			Node->OverloadAccumSeconds = FMath::Max(0.0, Kvp.Value);
		}
	}

	// Last committed step, not a fresh one: restore never runs extra ticks.
	StepOrdinal = Snapshot.StepOrdinal;
	PendingLagSeconds = Snapshot.PendingLagSeconds;
	MarkTopologyDirty();
	return FDocSystemResult::MakeSuccess();
}
