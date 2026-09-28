#include "DocMechanicalNetworkSubsystem.h"
#include "DocMechanicalNetworksLog.h"

namespace DocMechanicalPrivate
{
	const FName ReasonUnmetLoad(TEXT("UnmetLoad"));
	const FName ReasonOverspeed(TEXT("Overspeed"));
	const FName ReasonMissingEndpoint(TEXT("MissingEndpoint"));
	const FName ReasonSuspended(TEXT("Suspended"));

	static bool IsActiveEdge(const FDocDriveEdge& Edge)
	{
		return !(Edge.bIsClutched && !Edge.bIsEngaged);
	}

	static bool IsValidEdgeValues(const FDocDriveEdge& E)
	{
		return !E.EdgeId.IsNone() && !E.ParentNodeId.IsNone() && !E.ChildNodeId.IsNone() && E.ParentNodeId != E.ChildNodeId
			&& FMath::IsFinite(E.Ratio) && !FMath::IsNearlyZero(E.Ratio)
			&& FMath::IsFinite(E.Efficiency) && E.Efficiency > 0.0f && E.Efficiency <= 1.0f;
	}

	static bool IsValidSourceValues(const FDocDriveSourceState& S)
	{
		return !S.SourceId.IsNone() && !S.AttachedNodeId.IsNone() && FMath::IsFinite(S.RequestedSpeed)
			&& FMath::IsFinite(S.TorqueCapacity) && S.TorqueCapacity >= 0.0f
			&& FMath::IsFinite(S.StallHysteresisMargin) && S.StallHysteresisMargin >= 0.0f && S.StallHysteresisMargin <= 1.0f;
	}

	static double WrapPhase(double Phase)
	{
		const double TwoPi = 2.0 * UE_DOUBLE_PI;
		double Wrapped = FMath::Fmod(Phase, TwoPi);
		if (Wrapped < 0.0)
		{
			Wrapped += TwoPi;
		}
		return Wrapped;
	}

	template <typename ValueType>
	static TArray<FName> SortedKeys(const TMap<FName, ValueType>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		return Keys;
	}
}

UDocMechanicalNetworkSubsystem::UDocMechanicalNetworkSubsystem()
{
}

void UDocMechanicalNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Nodes.Reset();
	Edges.Reset();
	Sources.Reset();
	NodeComponents.Reset();
	TopologyRevision = 1;
}

void UDocMechanicalNetworkSubsystem::Deinitialize()
{
	Nodes.Reset();
	Edges.Reset();
	Sources.Reset();
	NodeComponents.Reset();
	Super::Deinitialize();
}

bool UDocMechanicalNetworkSubsystem::CheckRevision(int64 ExpectedRevision)
{
	if (ExpectedRevision >= 0 && ExpectedRevision != TopologyRevision)
	{
		LastTopologyError = FString::Printf(TEXT("Stale topology revision (expected %lld, current %lld)"), ExpectedRevision, TopologyRevision);
		return false;
	}
	return true;
}

void UDocMechanicalNetworkSubsystem::CommitTopologyChange()
{
	++TopologyRevision;
	LastTopologyError.Reset();
}

bool UDocMechanicalNetworkSubsystem::ValidateTopology(const TMap<FName, FDocDriveEdge>& EdgeSet, const TMap<FName, FDocDriveSourceState>& SourceSet, FString& OutError, TArray<FName>& OutOffendingEdges) const
{
	using namespace DocMechanicalPrivate;
	OutOffendingEdges.Reset();

	TMap<FName, FName> ChildToParent;
	TMap<FName, FName> ChildToEdge;
	for (const FName& EdgeId : SortedKeys(EdgeSet))
	{
		const FDocDriveEdge& Edge = EdgeSet[EdgeId];
		if (!IsActiveEdge(Edge))
		{
			continue;
		}
		if (const FName* ExistingEdge = ChildToEdge.Find(Edge.ChildNodeId))
		{
			OutOffendingEdges = { *ExistingEdge, EdgeId };
			OutError = FString::Printf(TEXT("Node %s would have two drive parents (edges %s and %s): multiple paths are unsupported"),
				*Edge.ChildNodeId.ToString(), *ExistingEdge->ToString(), *EdgeId.ToString());
			return false;
		}
		ChildToParent.Add(Edge.ChildNodeId, Edge.ParentNodeId);
		ChildToEdge.Add(Edge.ChildNodeId, EdgeId);
	}

	for (const TPair<FName, FName>& Pair : ChildToParent)
	{
		TSet<FName> Visited;
		TArray<FName> Path;
		FName Current = Pair.Key;
		while (!Current.IsNone())
		{
			if (Visited.Contains(Current))
			{
				OutOffendingEdges = Path;
				OutError = FString::Printf(TEXT("Drive cycle through node %s is unsupported"), *Current.ToString());
				return false;
			}
			Visited.Add(Current);
			if (const FName* EdgeToParent = ChildToEdge.Find(Current))
			{
				Path.Add(*EdgeToParent);
			}
			Current = ChildToParent.FindRef(Current);
		}
	}

	TMap<FName, FName> DriverByRoot;
	for (const FName& SourceId : SortedKeys(SourceSet))
	{
		const FDocDriveSourceState& Source = SourceSet[SourceId];
		if (!Source.bIsEnabled)
		{
			continue;
		}
		if (const FName* EdgeToParent = ChildToEdge.Find(Source.AttachedNodeId))
		{
			OutOffendingEdges = { *EdgeToParent };
			OutError = FString::Printf(TEXT("Source %s must attach to a tree root; node %s is driven through edge %s"),
				*SourceId.ToString(), *Source.AttachedNodeId.ToString(), *EdgeToParent->ToString());
			return false;
		}
		if (const FName* Other = DriverByRoot.Find(Source.AttachedNodeId))
		{
			OutError = FString::Printf(TEXT("Tree rooted at %s would have two drivers (%s and %s)"), *Source.AttachedNodeId.ToString(), *Other->ToString(), *SourceId.ToString());
			return false;
		}
		DriverByRoot.Add(Source.AttachedNodeId, SourceId);
	}

	// Two driven roots joined by an edge would give a root a parent, which is caught above.
	return true;
}

bool UDocMechanicalNetworkSubsystem::RegisterNode(const FDocMechanicalNodeState& InNode)
{
	if (InNode.NodeId.IsNone() || Nodes.Contains(InNode.NodeId)
		|| !FMath::IsFinite(InNode.AppliedLoadTorque) || InNode.AppliedLoadTorque < 0.0f
		|| !FMath::IsFinite(InNode.MaxOperatingSpeed) || InNode.MaxOperatingSpeed < 0.0f
		|| !FMath::IsFinite(InNode.PhaseAngle))
	{
		return false;
	}
	FDocMechanicalNodeState Node = InNode;
	Node.PhaseAngle = DocMechanicalPrivate::WrapPhase(Node.PhaseAngle);
	Node.VisualPhaseAngle = Node.PhaseAngle;
	Nodes.Add(Node.NodeId, Node);
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::UnregisterNode(FName NodeId)
{
	if (!Nodes.Contains(NodeId))
	{
		return false;
	}
	Nodes.Remove(NodeId);
	for (auto It = Edges.CreateIterator(); It; ++It)
	{
		if (It->Value.ParentNodeId == NodeId || It->Value.ChildNodeId == NodeId)
		{
			It.RemoveCurrent();
		}
	}
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::SuspendNode(FName NodeId)
{
	FDocMechanicalNodeState* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return false;
	}
	Node->bSuspended = true;
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::ResumeNode(FName NodeId)
{
	FDocMechanicalNodeState* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return false;
	}
	Node->bSuspended = false;
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::RegisterDriveSource(const FDocDriveSourceState& InSource)
{
	if (!DocMechanicalPrivate::IsValidSourceValues(InSource) || Sources.Contains(InSource.SourceId))
	{
		LastTopologyError = TEXT("Invalid or duplicate drive source");
		return false;
	}
	TMap<FName, FDocDriveSourceState> Proposed = Sources;
	FDocDriveSourceState Source = InSource;
	Source.bIsStalled = false;
	Source.DriveState = EDocDriveStallState::Running;
	Source.StatusReason = NAME_None;
	Proposed.Add(Source.SourceId, Source);
	TArray<FName> Offending;
	if (!ValidateTopology(Edges, Proposed, LastTopologyError, Offending))
	{
		return false;
	}
	Sources = MoveTemp(Proposed);
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::UnregisterDriveSource(FName SourceId)
{
	if (Sources.Remove(SourceId) > 0)
	{
		CommitTopologyChange();
		return true;
	}
	return false;
}

bool UDocMechanicalNetworkSubsystem::ConnectDrive(const FDocDriveEdge& InEdge, int64 ExpectedRevision)
{
	if (!DocMechanicalPrivate::IsValidEdgeValues(InEdge))
	{
		LastTopologyError = TEXT("Edge values are invalid (ids, finite nonzero ratio, efficiency in (0,1])");
		return false;
	}
	if (Edges.Contains(InEdge.EdgeId))
	{
		LastTopologyError = FString::Printf(TEXT("Edge %s already exists"), *InEdge.EdgeId.ToString());
		return false;
	}
	if (!CheckRevision(ExpectedRevision))
	{
		return false;
	}
	TMap<FName, FDocDriveEdge> Proposed = Edges;
	Proposed.Add(InEdge.EdgeId, InEdge);
	TArray<FName> Offending;
	if (!ValidateTopology(Proposed, Sources, LastTopologyError, Offending))
	{
		return false; // Rejected before commit.
	}
	Edges = MoveTemp(Proposed);
	CommitTopologyChange();
	return true;
}

bool UDocMechanicalNetworkSubsystem::DisconnectDrive(FName EdgeId, int64 ExpectedRevision)
{
	if (!CheckRevision(ExpectedRevision))
	{
		return false;
	}
	if (Edges.Remove(EdgeId) > 0)
	{
		CommitTopologyChange();
		return true;
	}
	return false;
}

void UDocMechanicalNetworkSubsystem::RegisterNodeComponent(UDocMechanicalNodeComponent* Comp)
{
	if (!Comp || Comp->NodeId.IsNone())
	{
		return;
	}
	NodeComponents.AddUnique(Comp);
	if (FDocMechanicalNodeState* Existing = Nodes.Find(Comp->NodeId))
	{
		if (Existing->bSuspended)
		{
			ResumeNode(Comp->NodeId); // Reload keeps the retained phase; it does not snap to anything.
		}
		return;
	}
	RegisterNode(Comp->GetState());
}

void UDocMechanicalNetworkSubsystem::UnregisterNodeComponent(UDocMechanicalNodeComponent* Comp)
{
	if (Comp)
	{
		NodeComponents.Remove(Comp);
		SuspendNode(Comp->NodeId); // Unload is not deletion.
	}
}

bool UDocMechanicalNetworkSubsystem::SetClutchState(FName EdgeId, bool bEngaged, int64 ExpectedRevision)
{
	FDocDriveEdge* Edge = Edges.Find(EdgeId);
	if (!Edge || !Edge->bIsClutched)
	{
		LastTopologyError = TEXT("Edge not found or not clutched");
		return false;
	}
	if (!CheckRevision(ExpectedRevision))
	{
		return false;
	}
	if (Edge->bIsEngaged == bEngaged)
	{
		return true;
	}
	if (bEngaged)
	{
		TMap<FName, FDocDriveEdge> Proposed = Edges;
		Proposed[EdgeId].bIsEngaged = true;
		TArray<FName> Offending;
		if (!ValidateTopology(Proposed, Sources, LastTopologyError, Offending))
		{
			return false;
		}
	}
	Edges[EdgeId].bIsEngaged = bEngaged;
	CommitTopologyChange();
	OnClutchChanged.Broadcast(EdgeId, bEngaged);
	OnClutchChangedNative.Broadcast(EdgeId, bEngaged);
	return true;
}

bool UDocMechanicalNetworkSubsystem::SetSourceSpeed(FName SourceId, float InSpeed)
{
	if (!FMath::IsFinite(InSpeed))
	{
		return false;
	}
	FDocDriveSourceState* Source = Sources.Find(SourceId);
	if (!Source)
	{
		return false;
	}
	if (Source->RequestedSpeed != InSpeed)
	{
		Source->RequestedSpeed = InSpeed;
		OnDriveChanged.Broadcast(SourceId, InSpeed);
	}
	return true;
}

bool UDocMechanicalNetworkSubsystem::SetSourceCapacity(FName SourceId, float InCapacity)
{
	if (!FMath::IsFinite(InCapacity) || InCapacity < 0.0f)
	{
		return false;
	}
	if (FDocDriveSourceState* Source = Sources.Find(SourceId))
	{
		Source->TorqueCapacity = InCapacity;
		return true;
	}
	return false;
}

bool UDocMechanicalNetworkSubsystem::SetNodeLoad(FName NodeId, float LoadTorque)
{
	if (!FMath::IsFinite(LoadTorque) || LoadTorque < 0.0f)
	{
		return false;
	}
	if (FDocMechanicalNodeState* Node = Nodes.Find(NodeId))
	{
		Node->AppliedLoadTorque = LoadTorque;
		return true;
	}
	return false;
}

bool UDocMechanicalNetworkSubsystem::QueryNodeDrive(FName NodeId, FDocMechanicalNodeState& OutState) const
{
	if (const FDocMechanicalNodeState* Found = Nodes.Find(NodeId))
	{
		OutState = *Found;
		return true;
	}
	return false;
}

bool UDocMechanicalNetworkSubsystem::QueryDriveSource(FName SourceId, FDocDriveSourceState& OutState) const
{
	if (const FDocDriveSourceState* Found = Sources.Find(SourceId))
	{
		OutState = *Found;
		return true;
	}
	return false;
}

TMap<FName, float> UDocMechanicalNetworkSubsystem::QueryReflectedLoads() const
{
	TMap<FName, float> Out;
	for (const TPair<FName, FDocMechanicalNodeState>& Pair : Nodes)
	{
		Out.Add(Pair.Key, Pair.Value.ReflectedLoadTorque);
	}
	return Out;
}

FDocDriveSolveResult UDocMechanicalNetworkSubsystem::SolveNetwork()
{
	using namespace DocMechanicalPrivate;
	FDocDriveSolveResult Result;

	// Validate before touching any state: a failed solve keeps the last committed result.
	if (!ValidateTopology(Edges, Sources, Result.FailureReason, Result.OffendingEdgeIds))
	{
		Result.bSuccess = false;
		return Result;
	}

	// Children in stable edge-id order (never map iteration order).
	TMap<FName, TArray<FDocDriveEdge>> Children;
	for (const FName& EdgeId : SortedKeys(Edges))
	{
		const FDocDriveEdge& Edge = Edges[EdgeId];
		if (IsActiveEdge(Edge))
		{
			Children.FindOrAdd(Edge.ParentNodeId).Add(Edge);
		}
	}

	TMap<FName, FDocMechanicalNodeState> NewNodes = Nodes;
	for (TPair<FName, FDocMechanicalNodeState>& Pair : NewNodes)
	{
		Pair.Value.bIsDriven = false;
		Pair.Value.RequestedSpeed = 0.0f;
		Pair.Value.ActualSpeed = 0.0f;
		Pair.Value.ReflectedLoadTorque = Pair.Value.AppliedLoadTorque;
		Pair.Value.bIsStalled = false;
	}

	TMap<FName, FDocDriveSourceState> NewSources = Sources;
	for (const FName& SourceId : SortedKeys(NewSources))
	{
		FDocDriveSourceState& Source = NewSources[SourceId];
		if (!Source.bIsEnabled)
		{
			Source.DriveState = EDocDriveStallState::Running;
			Source.StatusReason = NAME_None;
			continue;
		}

		// Walk the tree once: order, requested speeds, and any missing or unloaded node.
		struct FVisit { FName NodeId; float Speed; };
		TArray<FVisit> Order;
		bool bMissing = !NewNodes.Contains(Source.AttachedNodeId) || NewNodes[Source.AttachedNodeId].bSuspended;
		if (!bMissing)
		{
			TArray<FVisit> Stack;
			Stack.Push(FVisit{ Source.AttachedNodeId, Source.RequestedSpeed });
			while (Stack.Num() > 0)
			{
				const FVisit Current = Stack.Pop();
				Order.Add(Current);
				if (const TArray<FDocDriveEdge>* Kids = Children.Find(Current.NodeId))
				{
					for (int32 i = Kids->Num() - 1; i >= 0; --i)
					{
						const FDocDriveEdge& Edge = (*Kids)[i];
						const FDocMechanicalNodeState* Child = NewNodes.Find(Edge.ChildNodeId);
						if (!Child || Child->bSuspended)
						{
							bMissing = true; // A missing load provider is not zero load.
							continue;
						}
						Stack.Push(FVisit{ Edge.ChildNodeId, Current.Speed * Edge.Ratio });
					}
				}
			}
		}

		// Reflected load, bottom-up (reverse pre-order visits children before parents).
		for (int32 i = Order.Num() - 1; i >= 0; --i)
		{
			FDocMechanicalNodeState& Node = NewNodes[Order[i].NodeId];
			float Total = Node.AppliedLoadTorque;
			if (const TArray<FDocDriveEdge>* Kids = Children.Find(Node.NodeId))
			{
				for (const FDocDriveEdge& Edge : *Kids)
				{
					if (const FDocMechanicalNodeState* Child = NewNodes.Find(Edge.ChildNodeId))
					{
						if (!Child->bSuspended)
						{
							Total += FMath::Abs(Edge.Ratio) * Child->ReflectedLoadTorque / Edge.Efficiency;
						}
					}
				}
			}
			Node.ReflectedLoadTorque = Total;
		}
		const float Demand = Order.Num() > 0 ? NewNodes[Order[0].NodeId].ReflectedLoadTorque : 0.0f;
		Source.LastDemandTorque = Demand;

		// Speed limits are checked before anything is published.
		bool bOverspeed = false;
		for (const FVisit& Visit : Order)
		{
			const FDocMechanicalNodeState& Node = NewNodes[Visit.NodeId];
			if (Node.MaxOperatingSpeed > 0.0f && FMath::Abs(Visit.Speed) > Node.MaxOperatingSpeed)
			{
				bOverspeed = true;
			}
		}

		// Unmet-load stall with hysteresis.
		if (Source.bIsStalled)
		{
			if (Demand < Source.TorqueCapacity * (1.0f - Source.StallHysteresisMargin))
			{
				Source.bIsStalled = false;
			}
		}
		else if (Demand > Source.TorqueCapacity)
		{
			Source.bIsStalled = true;
		}

		bool bSuspendTree = false;
		if (bMissing)
		{
			if (MissingLoadPolicy == EDocMechanicalMissingLoadPolicy::FailClosed)
			{
				Source.DriveState = EDocDriveStallState::Stalled;
				Source.StatusReason = ReasonMissingEndpoint;
			}
			else
			{
				Source.DriveState = EDocDriveStallState::Running;
				Source.StatusReason = ReasonSuspended;
				bSuspendTree = true;
			}
		}
		else if (bOverspeed)
		{
			Source.DriveState = EDocDriveStallState::Overloaded;
			Source.StatusReason = ReasonOverspeed;
		}
		else if (Source.bIsStalled)
		{
			Source.DriveState = EDocDriveStallState::Stalled;
			Source.StatusReason = ReasonUnmetLoad;
		}
		else
		{
			Source.DriveState = EDocDriveStallState::Running;
			Source.StatusReason = NAME_None;
		}

		const bool bMoving = !bSuspendTree && Source.DriveState == EDocDriveStallState::Running;
		for (const FVisit& Visit : Order)
		{
			FDocMechanicalNodeState& Node = NewNodes[Visit.NodeId];
			Node.bIsDriven = !bSuspendTree;
			Node.RequestedSpeed = Visit.Speed; // Kept even when stalled: the unmet request stays visible.
			Node.ActualSpeed = bMoving ? Visit.Speed : 0.0f;
			Node.bIsStalled = !bSuspendTree && !bMoving;
			++Node.Revision;
		}
	}

	// Commit, then notify from committed state.
	TArray<TPair<FName, FName>> Stalls;
	TArray<FName> Recoveries;
	for (const TPair<FName, FDocDriveSourceState>& Pair : NewSources)
	{
		const FDocDriveSourceState* Prev = Sources.Find(Pair.Key);
		const EDocDriveStallState Before = Prev ? Prev->DriveState : EDocDriveStallState::Running;
		if (Pair.Value.DriveState != EDocDriveStallState::Running && Before == EDocDriveStallState::Running)
		{
			Stalls.Emplace(Pair.Key, Pair.Value.StatusReason);
		}
		else if (Pair.Value.DriveState == EDocDriveStallState::Running && Before != EDocDriveStallState::Running)
		{
			Recoveries.Add(Pair.Key);
		}
	}
	Nodes = MoveTemp(NewNodes);
	Sources = MoveTemp(NewSources);

	Result.bSuccess = true;
	for (const TPair<FName, FDocMechanicalNodeState>& Pair : Nodes)
	{
		Result.ActualSpeeds.Add(Pair.Key, Pair.Value.ActualSpeed);
		Result.ReflectedLoads.Add(Pair.Key, Pair.Value.ReflectedLoadTorque);
		Result.bIsStalled |= Pair.Value.bIsStalled;
	}

	for (const TPair<FName, FName>& Stall : Stalls)
	{
		OnStalled.Broadcast(Stall.Key, Stall.Value);
		OnStalledNative.Broadcast(Stall.Key, Stall.Value);
	}
	for (const FName& SourceId : Recoveries)
	{
		OnRecovered.Broadcast(SourceId);
		OnRecoveredNative.Broadcast(SourceId);
	}
	return Result;
}

void UDocMechanicalNetworkSubsystem::StepSimulation(float DeltaTime)
{
	using namespace DocMechanicalPrivate;
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}
	if (!SolveNetwork().bSuccess)
	{
		return; // Invalid topology: nothing advances.
	}

	const double Dt = (double)DeltaTime;
	const double CoastSeconds = FMath::Max((double)VisualCoastSeconds, 1e-3);
	for (TPair<FName, FDocMechanicalNodeState>& Pair : Nodes)
	{
		FDocMechanicalNodeState& Node = Pair.Value;
		if (Node.bIsDriven && Node.ActualSpeed != 0.0f)
		{
			const double Delta = (double)Node.ActualSpeed * Dt;
			Node.PhaseAngle = WrapPhase(Node.PhaseAngle + Delta);
			Node.AccumulatedRevolutions += Delta / (2.0 * UE_DOUBLE_PI);
			Node.VisualPhaseAngle = Node.PhaseAngle;
			Node.VisualCoastSpeed = Node.ActualSpeed;
		}
		else if (!Node.bIsDriven && Node.DetachedPolicy == EDocMechanicalDetachedPolicy::FreeVisualCoast && Node.VisualCoastSpeed != 0.0f)
		{
			// Presentation only: the logical phase holds; no momentum is claimed.
			Node.VisualCoastSpeed = (float)((double)Node.VisualCoastSpeed * FMath::Max(0.0, 1.0 - Dt / CoastSeconds));
			Node.VisualPhaseAngle = WrapPhase(Node.VisualPhaseAngle + (double)Node.VisualCoastSpeed * Dt);
		}
		else
		{
			Node.VisualPhaseAngle = Node.PhaseAngle;
			Node.VisualCoastSpeed = 0.0f;
		}
	}
}

void UDocMechanicalNetworkSubsystem::CaptureState(FDocMechanicalSnapshot& OutSnapshot) const
{
	OutSnapshot = FDocMechanicalSnapshot();
	for (const TPair<FName, FDocMechanicalNodeState>& Pair : Nodes)
	{
		OutSnapshot.Phases.Add(Pair.Key, Pair.Value.PhaseAngle);
		OutSnapshot.Revolutions.Add(Pair.Key, Pair.Value.AccumulatedRevolutions);
		OutSnapshot.Loads.Add(Pair.Key, Pair.Value.AppliedLoadTorque);
	}
	for (const TPair<FName, FDocDriveEdge>& Pair : Edges)
	{
		if (Pair.Value.bIsClutched)
		{
			OutSnapshot.ClutchEngaged.Add(Pair.Key, Pair.Value.bIsEngaged);
		}
	}
	for (const TPair<FName, FDocDriveSourceState>& Pair : Sources)
	{
		OutSnapshot.SourceSpeeds.Add(Pair.Key, Pair.Value.RequestedSpeed);
		OutSnapshot.SourceCapacities.Add(Pair.Key, Pair.Value.TorqueCapacity);
		OutSnapshot.SourceEnabled.Add(Pair.Key, Pair.Value.bIsEnabled);
	}
}

bool UDocMechanicalNetworkSubsystem::StageRestore(const FDocMechanicalSnapshot& Snapshot)
{
	using namespace DocMechanicalPrivate;
	if (Snapshot.SchemaVersion != 1)
	{
		return false;
	}
	for (const TPair<FName, double>& Kvp : Snapshot.Phases)
	{
		if (!FMath::IsFinite(Kvp.Value)) { return false; }
	}
	for (const TPair<FName, double>& Kvp : Snapshot.Revolutions)
	{
		if (!FMath::IsFinite(Kvp.Value)) { return false; }
	}
	for (const TPair<FName, float>& Kvp : Snapshot.Loads)
	{
		if (!FMath::IsFinite(Kvp.Value) || Kvp.Value < 0.0f) { return false; }
	}
	for (const TPair<FName, float>& Kvp : Snapshot.SourceSpeeds)
	{
		if (!FMath::IsFinite(Kvp.Value)) { return false; }
	}
	for (const TPair<FName, float>& Kvp : Snapshot.SourceCapacities)
	{
		if (!FMath::IsFinite(Kvp.Value) || Kvp.Value < 0.0f) { return false; }
	}

	// Controls must yield a valid topology before anything is applied.
	TMap<FName, FDocDriveEdge> NewEdges = Edges;
	for (const TPair<FName, bool>& Kvp : Snapshot.ClutchEngaged)
	{
		if (FDocDriveEdge* Edge = NewEdges.Find(Kvp.Key))
		{
			Edge->bIsEngaged = Kvp.Value;
		}
	}
	TMap<FName, FDocDriveSourceState> NewSources = Sources;
	for (TPair<FName, FDocDriveSourceState>& Pair : NewSources)
	{
		FDocDriveSourceState& Source = Pair.Value;
		Source.RequestedSpeed = Snapshot.SourceSpeeds.Contains(Pair.Key) ? Snapshot.SourceSpeeds[Pair.Key] : Source.RequestedSpeed;
		Source.TorqueCapacity = Snapshot.SourceCapacities.Contains(Pair.Key) ? Snapshot.SourceCapacities[Pair.Key] : Source.TorqueCapacity;
		Source.bIsEnabled = Snapshot.SourceEnabled.Contains(Pair.Key) ? Snapshot.SourceEnabled[Pair.Key] : Source.bIsEnabled;
		Source.bIsStalled = false; // Derived; recomputed by the next solve.
		Source.DriveState = EDocDriveStallState::Running;
		Source.StatusReason = NAME_None;
	}
	FString Error;
	TArray<FName> Offending;
	if (!ValidateTopology(NewEdges, NewSources, Error, Offending))
	{
		LastTopologyError = Error;
		return false;
	}

	for (TPair<FName, FDocMechanicalNodeState>& Pair : Nodes)
	{
		FDocMechanicalNodeState& Node = Pair.Value;
		if (const double* Phase = Snapshot.Phases.Find(Pair.Key))
		{
			Node.PhaseAngle = WrapPhase(*Phase);
			Node.VisualPhaseAngle = Node.PhaseAngle;
			Node.VisualCoastSpeed = 0.0f;
		}
		if (const double* Revs = Snapshot.Revolutions.Find(Pair.Key))
		{
			Node.AccumulatedRevolutions = *Revs;
		}
		if (const float* Load = Snapshot.Loads.Find(Pair.Key))
		{
			Node.AppliedLoadTorque = *Load;
		}
		Node.ActualSpeed = 0.0f;
		Node.RequestedSpeed = 0.0f;
	}
	Edges = MoveTemp(NewEdges);
	Sources = MoveTemp(NewSources);
	CommitTopologyChange(); // No events: restore does not replay history.
	return true;
}
