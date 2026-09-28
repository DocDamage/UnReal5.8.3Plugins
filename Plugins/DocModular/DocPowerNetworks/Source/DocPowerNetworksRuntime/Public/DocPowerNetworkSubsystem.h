#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocPowerNetworkTypes.h"
#include "DocSystemResult.h"
#include "DocPowerNetworkSubsystem.generated.h"

class UDocPowerNodeComponent;
class UDocPowerSourceComponent;
class UDocPowerConsumerComponent;
class UDocPowerStorageComponent;

struct FDocPowerInternalNode
{
	FName NodeId = NAME_None;
	EDocPowerNodeKind Kind = EDocPowerNodeKind::Switch;
	TSet<FName> PortNames;
	bool bEnabled = true;

	// Source fields
	double MaxPowerWatts = 0.0;
	int32 SourcePriority = 0;
	bool bExternalSource = true;
	double Availability = 1.0;

	// Consumer fields
	double DesiredPowerWatts = 0.0;
	double MinimumPowerWatts = 0.0;
	int32 ConsumerPriority = 0;
	int32 TieBreakId = 0;
	EDocPowerAllocationMode AllocationMode = EDocPowerAllocationMode::Binary;
	EDocPowerConsumerState ConsumerState = EDocPowerConsumerState::Disconnected;
	double DeliveredWatts = 0.0;
	double RecoveryDropoutHysteresis = 0.05;

	// Storage fields
	double CapacityJoules = 0.0;
	double CurrentEnergyJoules = 0.0;
	double MaxChargeWatts = 0.0;
	double MaxDischargeWatts = 0.0;
	double ChargeEfficiency = 1.0;
	double DischargeEfficiency = 1.0;
	EDocPowerStorageMode StorageMode = EDocPowerStorageMode::Auto;

	// Breaker fields
	double OverloadThresholdWatts = 0.0;
	double TripDurationSeconds = 0.0;
	double TripCooldownSeconds = 0.0;
	TSet<FName> ProtectedBranchNodes;
	bool bTripped = false;
	double RemainingTripSeconds = 0.0;
	double RemainingCooldownSeconds = 0.0;
	bool bIsMeshedProtection = false;
	double OverloadAccumSeconds = 0.0;

	// Switch fields
	bool bSwitchClosed = true;

	// Stream unload: the record (energy, edges, latches) is kept but isolated from every island.
	bool bSuspended = false;

	// Component pointer (optional, may be null in pure record mode)
	TWeakObjectPtr<UDocPowerNodeComponent> Component;
};

UCLASS()
class DOCPOWERNETWORKSRUNTIME_API UDocPowerNetworkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Node Registration
	/**
	 * Validates the component's values (finite, non-negative, efficiencies in (0,1], energy within capacity).
	 * A duplicate NodeId is a Conflict, except a suspended record, which the reloaded component re-binds
	 * without overwriting the retained energy.
	 */
	FDocSystemResult RegisterNode(UDocPowerNodeComponent* Component);
	FDocSystemResult RegisterNodeRaw(FName NodeId, EDocPowerNodeKind Kind, const TArray<FName>& Ports);
	FDocSystemResult UnregisterNode(FName NodeId);
	/** Stream unload: keep the record and its edges, isolate it from islands, drop the component pointer. */
	FDocSystemResult SuspendNode(FName NodeId);
	bool HasNode(FName NodeId) const;
	bool IsNodeSuspended(FName NodeId) const;

	// Graph Wiring. ExpectedTopologyRevision >= 0 refuses the edit with Conflict when the topology has moved on.
	FDocSystemResult ConnectPorts(FName EdgeId, const FDocPowerPortId& PortA, const FDocPowerPortId& PortB, bool bClosed = true, int64 ExpectedTopologyRevision = -1);
	FDocSystemResult DisconnectPorts(FName EdgeId, int64 ExpectedTopologyRevision = -1);
	FDocSystemResult SetEdgeClosed(FName EdgeId, bool bClosed, int64 ExpectedTopologyRevision = -1);
	FDocSystemResult SetSwitchState(FName SwitchNodeId, bool bClosed, int64 ExpectedTopologyRevision = -1);
	bool HasEdge(FName EdgeId) const { return Edges.Contains(EdgeId); }
	bool GetSwitchState(FName SwitchNodeId) const;

	// Dynamic Adjustments
	FDocSystemResult SetSourceAvailability(FName SourceNodeId, double InAvailability);
	FDocSystemResult SetConsumerDemand(FName ConsumerNodeId, double InDesiredWatts, double InMinimumWatts);
	FDocSystemResult SetStorageMode(FName StorageNodeId, EDocPowerStorageMode InMode);
	FDocSystemResult SetStorageEnergy(FName StorageNodeId, double InEnergyJoules);

	// Breakers
	/**
	 * Trips when the pre-allocation demand of the protected branch (or of the whole island when the branch is empty)
	 * stays above the threshold for InTripDurationSeconds. A branch must be radial: reachable from the rest of the
	 * network only through this breaker. Meshed protection (declared or detected) returns Unsupported and changes nothing.
	 */
	FDocSystemResult ConfigureBreaker(FName BreakerNodeId, double InOverloadWatts, double InTripDurationSeconds, double InCooldownSeconds, const TSet<FName>& InProtectedBranch, bool bInMeshedProtection = false);
	FDocSystemResult RequestBreakerReset(FName BreakerNodeId);
	bool IsBreakerTripped(FName BreakerNodeId) const;

	// Queries
	FDocSystemResult QueryIsland(FName NodeId, FDocPowerIslandState& OutIsland) const;
	void GetAllIslands(TArray<FDocPowerIslandState>& OutIslands) const;
	bool GetNodeSupply(FName ConsumerNodeId, double& OutDeliveredWatts, EDocPowerConsumerState& OutState) const;

	// Simulation
	/**
	 * Advances by DeltaSeconds in sub-steps of at most MaxStepSeconds, up to MaxCatchUpSteps per call. Time beyond the
	 * budget is kept as simulation lag (processed by later calls), never silently dropped. Validation happens before
	 * any state changes: a failed call changes nothing. The ledger covers the whole call.
	 */
	FDocSystemResult StepSimulation(float DeltaSeconds);
	FDocSystemResult SetStepLimits(double InMaxStepSeconds, int32 InMaxCatchUpSteps);
	double GetSimulationLagSeconds() const { return PendingLagSeconds; }
	/** Explicit offline policy: drop the lag without simulating it (no energy is created or delivered). */
	void DiscardSimulationLag() { PendingLagSeconds = 0.0; }
	const FDocEnergyLedger& GetLastLedger() const { return LastLedger; }
	int64 GetTopologyRevision() const { return TopologyRevision; }
	int64 GetStepOrdinal() const { return StepOrdinal; }

	// Persistence / Snapshots
	FDocPowerNetworkSnapshot CaptureNetworkState() const;
	FDocSystemResult RestoreNetworkState(const FDocPowerNetworkSnapshot& Snapshot, bool bForceMatchRevision = false);

	// Events
	UPROPERTY(BlueprintAssignable, Category = "Power|Events")
	FDocOnBreakerTripped OnBreakerTripped;

	FDocOnBreakerTrippedNative OnBreakerTrippedNative;

	UPROPERTY(BlueprintAssignable, Category = "Power|Events")
	FDocOnPowerSupplyChanged OnPowerSupplyChanged;

	FDocOnPowerSupplyChangedNative OnPowerSupplyChangedNative;

private:
	void MarkTopologyDirty();
	void BuildIslands(TArray<TArray<FName>>& OutIslands) const;
	FDocSystemResult CheckExpectedRevision(int64 ExpectedTopologyRevision) const;
	bool IsRadialBranch(FName BreakerId, const TSet<FName>& Branch, const TMap<FName, FDocPowerEdge>& EdgeSet) const;
	FDocSystemResult ValidateBreakers(const TMap<FName, FDocPowerEdge>& EdgeSet) const;
	void SolveOneStep(double Dt, FDocEnergyLedger& Ledger, TArray<TPair<FName, FString>>& OutTrips);
	double SumStoredEnergy() const;

	double MaxStepSeconds = 1.0;
	int32 MaxCatchUpSteps = 10;
	double PendingLagSeconds = 0.0;

	int64 TopologyRevision = 1;
	int64 StepOrdinal = 0;

	TMap<FName, FDocPowerInternalNode> Nodes;
	TMap<FName, FDocPowerEdge> Edges;
	FDocEnergyLedger LastLedger;
};
