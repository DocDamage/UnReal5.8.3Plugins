#include "DocFluidNetworkSubsystem.h"
#include "DocFluidNetworksLog.h"

namespace DocFluidPrivate
{
	static bool IsValidReservoir(const FDocFluidReservoirState& R)
	{
		return !R.ReservoirId.IsNone() && !R.FluidDefinitionId.IsNone()
			&& FMath::IsFinite(R.Capacity) && R.Capacity >= 0.0f
			&& FMath::IsFinite(R.CurrentVolume) && R.CurrentVolume >= 0.0f && R.CurrentVolume <= R.Capacity
			&& FMath::IsFinite(R.QuarantinedVolume) && R.QuarantinedVolume >= 0.0f;
	}

	static bool IsValidEdgeValues(const FDocFluidEdge& E)
	{
		return !E.EdgeId.IsNone() && !E.SourceReservoirId.IsNone() && !E.DestinationReservoirId.IsNone()
			&& E.SourceReservoirId != E.DestinationReservoirId
			&& FMath::IsFinite(E.MaxFlowRate) && E.MaxFlowRate >= 0.0f
			&& FMath::IsFinite(E.ValveOpening) && E.ValveOpening >= 0.0f && E.ValveOpening <= 1.0f;
	}

	static bool IsValidRate(float Rate)
	{
		return FMath::IsFinite(Rate) && Rate >= 0.0f;
	}
}

UDocFluidNetworkSubsystem::UDocFluidNetworkSubsystem()
	: MaxSimulationStepsPerCatchUp(100)
	, FixedStepDeltaTime(0.1f)
{
}

void UDocFluidNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetLedger();
}

void UDocFluidNetworkSubsystem::Deinitialize()
{
	Reservoirs.Reset();
	Edges.Reset();
	ExternalSupplyRates.Reset();
	LeakRates.Reset();
	ReservoirComponents.Reset();
	Super::Deinitialize();
}

void UDocFluidNetworkSubsystem::ResetLedger()
{
	const double Simulated = CumulativeLedger.SimulatedSeconds;
	CumulativeLedger = FDocFluidLedger();
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	CumulativeLedger.SimulatedSeconds = Simulated;
}

double UDocFluidNetworkSubsystem::GetTotalSystemVolume() const
{
	double Total = 0.0;
	for (const TPair<FName, FDocFluidReservoirState>& Pair : Reservoirs)
	{
		Total += Pair.Value.CurrentVolume;
	}
	return Total;
}

bool UDocFluidNetworkSubsystem::IsActive(FName ReservoirId) const
{
	const FDocFluidReservoirState* R = Reservoirs.Find(ReservoirId);
	return R && !R->bSuspended;
}

bool UDocFluidNetworkSubsystem::RegisterReservoir(const FDocFluidReservoirState& InReservoir)
{
	if (!DocFluidPrivate::IsValidReservoir(InReservoir) || Reservoirs.Contains(InReservoir.ReservoirId))
	{
		return false;
	}
	// An existing edge between different liquids would become invalid; refuse rather than mix silently.
	for (const TPair<FName, FDocFluidEdge>& Kvp : Edges)
	{
		const FDocFluidEdge& E = Kvp.Value;
		const FName Other = E.SourceReservoirId == InReservoir.ReservoirId ? E.DestinationReservoirId
			: (E.DestinationReservoirId == InReservoir.ReservoirId ? E.SourceReservoirId : NAME_None);
		if (const FDocFluidReservoirState* OtherRes = Other.IsNone() ? nullptr : Reservoirs.Find(Other))
		{
			if (OtherRes->FluidDefinitionId != InReservoir.FluidDefinitionId)
			{
				return false;
			}
		}
	}

	Reservoirs.Add(InReservoir.ReservoirId, InReservoir);
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	return true;
}

bool UDocFluidNetworkSubsystem::UnregisterReservoir(FName ReservoirId)
{
	FDocFluidReservoirState Removed;
	if (!Reservoirs.RemoveAndCopyValue(ReservoirId, Removed))
	{
		return false;
	}
	// Removing a storage node is a declared discard, never a silent loss.
	CumulativeLedger.TotalDiscarded += Removed.CurrentVolume;
	CumulativeLedger.NetQuarantined -= Removed.QuarantinedVolume;
	CumulativeLedger.TotalDiscarded += Removed.QuarantinedVolume;
	ExternalSupplyRates.Remove(ReservoirId);
	LeakRates.Remove(ReservoirId);
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	return true;
}

bool UDocFluidNetworkSubsystem::SuspendReservoir(FName ReservoirId)
{
	FDocFluidReservoirState* R = Reservoirs.Find(ReservoirId);
	if (!R)
	{
		return false;
	}
	R->bSuspended = true;
	return true;
}

bool UDocFluidNetworkSubsystem::ResumeReservoir(FName ReservoirId)
{
	FDocFluidReservoirState* R = Reservoirs.Find(ReservoirId);
	if (!R)
	{
		return false;
	}
	R->bSuspended = false;
	return true;
}

bool UDocFluidNetworkSubsystem::RegisterConnection(const FDocFluidEdge& InEdge)
{
	if (!DocFluidPrivate::IsValidEdgeValues(InEdge) || Edges.Contains(InEdge.EdgeId))
	{
		return false;
	}
	const FDocFluidReservoirState* Src = Reservoirs.Find(InEdge.SourceReservoirId);
	const FDocFluidReservoirState* Dst = Reservoirs.Find(InEdge.DestinationReservoirId);
	if (Src && Dst && Src->FluidDefinitionId != Dst->FluidDefinitionId)
	{
		return false; // Mixing is a future extension; refuse the connection explicitly.
	}
	Edges.Add(InEdge.EdgeId, InEdge);
	return true;
}

bool UDocFluidNetworkSubsystem::RemoveConnection(FName EdgeId)
{
	return Edges.Remove(EdgeId) > 0;
}

void UDocFluidNetworkSubsystem::RegisterReservoirComponent(UDocFluidReservoirComponent* Comp)
{
	if (!Comp || Comp->ReservoirId.IsNone())
	{
		return;
	}
	ReservoirComponents.AddUnique(Comp);

	if (FDocFluidReservoirState* Existing = Reservoirs.Find(Comp->ReservoirId))
	{
		// Stream reload: the retained record is authoritative; the component's authored start volume does not overwrite it.
		Existing->bSuspended = false;
		Comp->CurrentVolume = Existing->CurrentVolume;
		return;
	}

	if (RegisterReservoir(Comp->GetState()))
	{
		if (Comp->LeakRate > 0.0f)
		{
			SetLeakRate(Comp->ReservoirId, Comp->LeakRate);
		}
		if (Comp->ExternalSupplyRate > 0.0f)
		{
			SetExternalSupplyRate(Comp->ReservoirId, Comp->ExternalSupplyRate);
		}
	}
	else
	{
		UE_LOG(LogDocFluid, Warning, TEXT("Reservoir component %s has invalid values and was not registered"), *Comp->ReservoirId.ToString());
	}
}

void UDocFluidNetworkSubsystem::UnregisterReservoirComponent(UDocFluidReservoirComponent* Comp)
{
	if (Comp)
	{
		ReservoirComponents.Remove(Comp);
		// Stream unload is not storage-node deletion: keep contents and stop simulating the reservoir.
		SuspendReservoir(Comp->ReservoirId);
	}
}

bool UDocFluidNetworkSubsystem::SetValveOpening(FName EdgeId, float Opening)
{
	if (!FMath::IsFinite(Opening) || Opening < 0.0f || Opening > 1.0f)
	{
		return false;
	}
	if (FDocFluidEdge* Edge = Edges.Find(EdgeId))
	{
		Edge->ValveOpening = Opening;
		return true;
	}
	return false;
}

bool UDocFluidNetworkSubsystem::SetPumpEnabled(FName EdgeId, bool bEnabled)
{
	if (FDocFluidEdge* Edge = Edges.Find(EdgeId))
	{
		Edge->bPumpEnabled = bEnabled;
		return true;
	}
	return false;
}

bool UDocFluidNetworkSubsystem::SetEdgeBlocked(FName EdgeId, bool bBlocked)
{
	if (FDocFluidEdge* Edge = Edges.Find(EdgeId))
	{
		Edge->bIsBlocked = bBlocked;
		return true;
	}
	return false;
}

EDocFluidPumpStatus UDocFluidNetworkSubsystem::GetEdgeStatus(FName EdgeId, FName& OutReason) const
{
	const FDocFluidEdge* Edge = Edges.Find(EdgeId);
	if (!Edge)
	{
		OutReason = TEXT("UnknownEdge");
		return EDocFluidPumpStatus::Faulted;
	}
	const FDocFluidReservoirState* Src = Reservoirs.Find(Edge->SourceReservoirId);
	const FDocFluidReservoirState* Dst = Reservoirs.Find(Edge->DestinationReservoirId);
	if (Src && Dst && Src->FluidDefinitionId != Dst->FluidDefinitionId)
	{
		OutReason = TEXT("FluidMismatch");
		return EDocFluidPumpStatus::Faulted;
	}
	if (Edge->bIsBlocked)
	{
		OutReason = TEXT("Blocked");
		return EDocFluidPumpStatus::Blocked;
	}
	if (Edge->bPumpRequired && !Edge->bPumpEnabled)
	{
		OutReason = TEXT("PumpDisabled");
		return EDocFluidPumpStatus::Disabled;
	}
	if (!Src || !Dst)
	{
		OutReason = TEXT("UnresolvedEndpoint");
		return EDocFluidPumpStatus::UnavailableSupply;
	}
	if (Src->bSuspended || Dst->bSuspended)
	{
		OutReason = TEXT("SuspendedEndpoint");
		return EDocFluidPumpStatus::UnavailableSupply;
	}
	if (Src->CurrentVolume <= 0.0f)
	{
		OutReason = TEXT("SourceEmpty");
		return EDocFluidPumpStatus::UnavailableSupply;
	}
	OutReason = NAME_None;
	return EDocFluidPumpStatus::Enabled;
}

bool UDocFluidNetworkSubsystem::SetExternalSupplyRate(FName ReservoirId, float LitersPerSec)
{
	if (!DocFluidPrivate::IsValidRate(LitersPerSec) || !Reservoirs.Contains(ReservoirId))
	{
		return false;
	}
	ExternalSupplyRates.Add(ReservoirId, LitersPerSec);
	return true;
}

bool UDocFluidNetworkSubsystem::SetLeakRate(FName ReservoirId, float LitersPerSec)
{
	if (!DocFluidPrivate::IsValidRate(LitersPerSec) || !Reservoirs.Contains(ReservoirId))
	{
		return false;
	}
	LeakRates.Add(ReservoirId, LitersPerSec);
	return true;
}

FDocFluidDrainReceipt UDocFluidNetworkSubsystem::RequestDrain(FName ReservoirId, float RequestedVolume, bool bAllowPartial)
{
	FDocFluidDrainReceipt Receipt;
	Receipt.ReservoirId = ReservoirId;
	Receipt.RemainingRequested = RequestedVolume;

	if (!FMath::IsFinite(RequestedVolume) || RequestedVolume <= 0.0f)
	{
		Receipt.Result = EDocFluidDrainResult::InvalidAmount;
		return Receipt;
	}

	FDocFluidReservoirState* Res = Reservoirs.Find(ReservoirId);
	if (!Res || Res->bSuspended)
	{
		Receipt.Result = EDocFluidDrainResult::Unavailable;
		Receipt.CommittedRevision = Res ? Res->Revision : 0;
		return Receipt;
	}

	if (Res->CurrentVolume < RequestedVolume && !bAllowPartial)
	{
		Receipt.Result = EDocFluidDrainResult::Unavailable;
		Receipt.CommittedRevision = Res->Revision;
		return Receipt;
	}

	const float Drained = FMath::Min(Res->CurrentVolume, RequestedVolume);
	Res->CurrentVolume -= Drained;
	++Res->Revision;

	Receipt.AmountDrained = Drained;
	Receipt.RemainingRequested = RequestedVolume - Drained;
	Receipt.Result = (Receipt.RemainingRequested <= 1e-4f) ? EDocFluidDrainResult::Success : EDocFluidDrainResult::Partial;
	Receipt.CommittedRevision = Res->Revision;

	CumulativeLedger.TotalDrains += Drained;
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	return Receipt;
}

bool UDocFluidNetworkSubsystem::QueryReservoir(FName ReservoirId, FDocFluidReservoirState& OutState) const
{
	if (const FDocFluidReservoirState* Found = Reservoirs.Find(ReservoirId))
	{
		OutState = *Found;
		return true;
	}
	return false;
}

bool UDocFluidNetworkSubsystem::MigrateCapacity(FName ReservoirId, float NewCapacity, EDocFluidCapacityMigrationPolicy Policy, float& OutDiscrepancy)
{
	OutDiscrepancy = 0.0f;
	if (!FMath::IsFinite(NewCapacity) || NewCapacity < 0.0f)
	{
		return false;
	}
	FDocFluidReservoirState* Res = Reservoirs.Find(ReservoirId);
	if (!Res)
	{
		return false;
	}

	if (NewCapacity < Res->CurrentVolume)
	{
		OutDiscrepancy = Res->CurrentVolume - NewCapacity;
		if (Policy == EDocFluidCapacityMigrationPolicy::SpillToSink)
		{
			CumulativeLedger.TotalExternalOutflow += OutDiscrepancy;
		}
		else
		{
			Res->QuarantinedVolume += OutDiscrepancy;
			CumulativeLedger.NetQuarantined += OutDiscrepancy;
		}
		Res->CurrentVolume = NewCapacity;
	}

	Res->Capacity = NewCapacity;
	++Res->Revision;
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	return true;
}

bool UDocFluidNetworkSubsystem::ReleaseQuarantine(FName ReservoirId, float& OutReleased)
{
	OutReleased = 0.0f;
	FDocFluidReservoirState* Res = Reservoirs.Find(ReservoirId);
	if (!Res)
	{
		return false;
	}
	OutReleased = FMath::Min(Res->QuarantinedVolume, Res->GetFreeCapacity());
	if (OutReleased > 0.0f)
	{
		Res->QuarantinedVolume -= OutReleased;
		Res->CurrentVolume += OutReleased;
		++Res->Revision;
		CumulativeLedger.NetQuarantined -= OutReleased;
		CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	}
	return true;
}

void UDocFluidNetworkSubsystem::CaptureState(FDocFluidNetworkSnapshot& OutSnapshot) const
{
	OutSnapshot = FDocFluidNetworkSnapshot();
	Reservoirs.GenerateValueArray(OutSnapshot.Reservoirs);
	Edges.GenerateValueArray(OutSnapshot.Edges);
	OutSnapshot.ExternalSupplyRates = ExternalSupplyRates;
	OutSnapshot.LeakRates = LeakRates;
	OutSnapshot.SimulatedSeconds = CumulativeLedger.SimulatedSeconds;
	OutSnapshot.PendingLagSeconds = PendingLagSeconds;
}

bool UDocFluidNetworkSubsystem::StageRestore(const FDocFluidNetworkSnapshot& Snapshot)
{
	using namespace DocFluidPrivate;
	if (Snapshot.SchemaVersion != 1 || !FMath::IsFinite(Snapshot.SimulatedSeconds) || !FMath::IsFinite(Snapshot.PendingLagSeconds) || Snapshot.PendingLagSeconds < 0.0)
	{
		return false;
	}

	// Validate everything before applying anything.
	TMap<FName, FDocFluidReservoirState> NewReservoirs;
	for (const FDocFluidReservoirState& Saved : Snapshot.Reservoirs)
	{
		if (NewReservoirs.Contains(Saved.ReservoirId))
		{
			return false;
		}
		FDocFluidReservoirState R = Saved;
		if (const FDocFluidReservoirState* Current = Reservoirs.Find(Saved.ReservoirId))
		{
			// Authored content wins for capacity and liquid; a smaller tank quarantines the excess.
			R.Capacity = Current->Capacity;
			if (Current->FluidDefinitionId != Saved.FluidDefinitionId)
			{
				return false;
			}
		}
		if (!FMath::IsFinite(R.CurrentVolume) || R.CurrentVolume < 0.0f || !FMath::IsFinite(R.QuarantinedVolume) || R.QuarantinedVolume < 0.0f)
		{
			return false;
		}
		if (R.CurrentVolume > R.Capacity)
		{
			R.QuarantinedVolume += R.CurrentVolume - R.Capacity;
			R.CurrentVolume = R.Capacity;
		}
		if (!IsValidReservoir(R))
		{
			return false;
		}
		NewReservoirs.Add(R.ReservoirId, R);
	}
	TMap<FName, FDocFluidEdge> NewEdges;
	for (const FDocFluidEdge& E : Snapshot.Edges)
	{
		if (!IsValidEdgeValues(E) || NewEdges.Contains(E.EdgeId))
		{
			return false;
		}
		NewEdges.Add(E.EdgeId, E);
	}
	for (const TPair<FName, float>& Kvp : Snapshot.ExternalSupplyRates)
	{
		if (!IsValidRate(Kvp.Value))
		{
			return false;
		}
	}
	for (const TPair<FName, float>& Kvp : Snapshot.LeakRates)
	{
		if (!IsValidRate(Kvp.Value))
		{
			return false;
		}
	}

	// Reservoirs registered now but absent from the save keep their current state.
	for (const TPair<FName, FDocFluidReservoirState>& Kvp : Reservoirs)
	{
		if (!NewReservoirs.Contains(Kvp.Key))
		{
			NewReservoirs.Add(Kvp.Key, Kvp.Value);
		}
	}

	Reservoirs = MoveTemp(NewReservoirs);
	Edges = MoveTemp(NewEdges);
	ExternalSupplyRates = Snapshot.ExternalSupplyRates;
	LeakRates = Snapshot.LeakRates;
	PendingLagSeconds = Snapshot.PendingLagSeconds;

	// A restore starts a new accounting period: nothing is replayed.
	CumulativeLedger = FDocFluidLedger();
	CumulativeLedger.SimulatedSeconds = Snapshot.SimulatedSeconds;
	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
	SyncComponents();
	return true;
}

void UDocFluidNetworkSubsystem::SyncComponents()
{
	for (const TWeakObjectPtr<UDocFluidReservoirComponent>& WeakComp : ReservoirComponents)
	{
		if (UDocFluidReservoirComponent* Comp = WeakComp.Get())
		{
			if (const FDocFluidReservoirState* State = Reservoirs.Find(Comp->ReservoirId))
			{
				Comp->CurrentVolume = State->CurrentVolume;
			}
		}
	}
}

void UDocFluidNetworkSubsystem::StepSimulation(float DeltaTime)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	const double StepDt = FixedStepDeltaTime > 0.0f ? (double)FixedStepDeltaTime : 0.1;
	const int32 Budget = FMath::Max(1, MaxSimulationStepsPerCatchUp);
	double Available = PendingLagSeconds + (double)DeltaTime;

	// Whole fixed steps only; the tolerance absorbs float representation of the step length.
	const int32 WholeSteps = (int32)FMath::Min((double)Budget, FMath::FloorToDouble((Available + 1e-6) / StepDt));
	for (int32 Step = 0; Step < WholeSteps; ++Step)
	{
		ExecuteSingleStep((float)StepDt);
	}
	Available -= WholeSteps * StepDt;
	PendingLagSeconds = Available > 1e-6 ? Available : 0.0;
	CumulativeLedger.SimulatedSeconds += WholeSteps * StepDt;

	SyncComponents();
}

void UDocFluidNetworkSubsystem::ExecuteSingleStep(float StepDt)
{
	CumulativeLedger.StepTransfers.Reset();

	// Deterministic ordering (FLU-04): never registration or hash order.
	TArray<FName> SortedResKeys;
	Reservoirs.GetKeys(SortedResKeys);
	SortedResKeys.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
	TArray<FName> SortedEdgeKeys;
	Edges.GetKeys(SortedEdgeKeys);
	SortedEdgeKeys.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	// 1. Snapshot initial volumes and free capacities.
	TMap<FName, float> InitialVolumes;
	TMap<FName, float> InitialFreeCapacities;
	for (const FName& ResId : SortedResKeys)
	{
		const FDocFluidReservoirState& Res = Reservoirs[ResId];
		InitialVolumes.Add(ResId, Res.CurrentVolume);
		InitialFreeCapacities.Add(ResId, Res.GetFreeCapacity());
	}

	// 2. Propose edge transfers.
	TArray<FDocFluidTransfer> ProposedTransfers;
	for (const FName& EdgeId : SortedEdgeKeys)
	{
		const FDocFluidEdge& Edge = Edges[EdgeId];
		if (Edge.bIsBlocked || Edge.ValveOpening <= 0.0f || (Edge.bPumpRequired && !Edge.bPumpEnabled))
		{
			continue;
		}
		// Unresolved or suspended endpoints suspend the edge; mismatched liquids never mix (see GetEdgeStatus).
		if (!IsActive(Edge.SourceReservoirId) || !IsActive(Edge.DestinationReservoirId))
		{
			continue;
		}
		if (Reservoirs[Edge.SourceReservoirId].FluidDefinitionId != Reservoirs[Edge.DestinationReservoirId].FluidDefinitionId)
		{
			continue;
		}

		float Proposed = Edge.MaxFlowRate * Edge.ValveOpening * StepDt;
		Proposed = FMath::Min(Proposed, InitialVolumes[Edge.SourceReservoirId]);
		Proposed = FMath::Min(Proposed, InitialFreeCapacities[Edge.DestinationReservoirId]);
		if (Proposed > 0.0f)
		{
			FDocFluidTransfer Transfer;
			Transfer.EdgeId = Edge.EdgeId;
			Transfer.SourceId = Edge.SourceReservoirId;
			Transfer.DestinationId = Edge.DestinationReservoirId;
			Transfer.ProposedVolume = Proposed;
			ProposedTransfers.Add(Transfer);
		}
	}

	// 3. Constrain total outgoing volume per source, proportionally (FLU-02).
	TMap<FName, float> TotalProposedOutflow;
	for (const FDocFluidTransfer& T : ProposedTransfers)
	{
		TotalProposedOutflow.FindOrAdd(T.SourceId) += T.ProposedVolume;
	}
	for (FDocFluidTransfer& T : ProposedTransfers)
	{
		const float Outflow = TotalProposedOutflow[T.SourceId];
		const float InitVol = InitialVolumes[T.SourceId];
		if (Outflow > InitVol && Outflow > 1e-6f)
		{
			T.ProposedVolume *= (InitVol / Outflow);
		}
	}

	// 4. Constrain total incoming volume per destination to its initial free capacity (FLU-03).
	TMap<FName, float> TotalProposedInflow;
	for (const FDocFluidTransfer& T : ProposedTransfers)
	{
		TotalProposedInflow.FindOrAdd(T.DestinationId) += T.ProposedVolume;
	}
	for (FDocFluidTransfer& T : ProposedTransfers)
	{
		const float Inflow = TotalProposedInflow[T.DestinationId];
		const float InitCap = InitialFreeCapacities[T.DestinationId];
		if (Inflow > InitCap && Inflow > 1e-6f)
		{
			T.ProposedVolume *= (InitCap / Inflow);
		}
	}

	// 5. Commit all transfers together (declared one-step transport delay, FLU-01).
	for (FDocFluidTransfer& T : ProposedTransfers)
	{
		if (T.ProposedVolume > 0.0f)
		{
			FDocFluidReservoirState& Src = Reservoirs[T.SourceId];
			FDocFluidReservoirState& Dst = Reservoirs[T.DestinationId];
			const float Amount = FMath::Min(T.ProposedVolume, Src.CurrentVolume); // rounding guard: never below zero
			Src.CurrentVolume -= Amount;
			Dst.CurrentVolume += Amount; // Bounded by the initial free capacity in step 4; no clamp, so nothing is lost to rounding.
			T.CommittedVolume = Amount;
			T.RateLitersPerSecond = StepDt > 0.0f ? Amount / StepDt : 0.0f;
			CumulativeLedger.StepTransfers.Add(T);
			++Src.Revision;
			++Dst.Revision;
		}
	}

	// 6. External inflow and leaks (FLU-06); suspended reservoirs neither gain nor lose.
	for (const FName& ResId : SortedResKeys)
	{
		FDocFluidReservoirState& Res = Reservoirs[ResId];
		if (Res.bSuspended)
		{
			continue;
		}
		if (const float* SupplyRate = ExternalSupplyRates.Find(ResId))
		{
			const float SupplyAmount = FMath::Min((*SupplyRate) * StepDt, Res.GetFreeCapacity());
			if (SupplyAmount > 0.0f)
			{
				Res.CurrentVolume += SupplyAmount;
				CumulativeLedger.TotalExternalInflow += SupplyAmount;
				++Res.Revision;
			}
		}
		if (const float* LeakRate = LeakRates.Find(ResId))
		{
			const float LeakAmount = FMath::Min((*LeakRate) * StepDt, Res.CurrentVolume);
			if (LeakAmount > 0.0f)
			{
				Res.CurrentVolume -= LeakAmount;
				CumulativeLedger.TotalLeaks += LeakAmount;
				++Res.Revision;
			}
		}
	}

	CumulativeLedger.TotalSystemVolume = GetTotalSystemVolume();
}
