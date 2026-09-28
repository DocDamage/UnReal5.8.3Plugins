#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocFluidTypes.h"
#include "DocFluidComponents.h"
#include "DocFluidNetworkSubsystem.generated.h"

UCLASS(BlueprintType)
class DOCFLUIDNETWORKSRUNTIME_API UDocFluidNetworkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDocFluidNetworkSubsystem();

	//~ Subsystem lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Registration API
	/** Refuses duplicates, a None liquid id, and non-finite, negative or over-capacity values. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool RegisterReservoir(const FDocFluidReservoirState& InReservoir);

	/** Removes a storage node. Its contents (and quarantine) are recorded as an explicit discard in the ledger. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool UnregisterReservoir(FName ReservoirId);

	/** Stream unload: keep contents, stop every transfer, supply and leak touching it. Not a deletion. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SuspendReservoir(FName ReservoirId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool ResumeReservoir(FName ReservoirId);

	/**
	 * Refuses duplicate ids, self-loops, invalid rates/openings, and edges between two registered reservoirs holding
	 * different liquids. An edge to a not-yet-registered reservoir is allowed and stays suspended until it resolves.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool RegisterConnection(const FDocFluidEdge& InEdge);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool RemoveConnection(FName EdgeId);

	void RegisterReservoirComponent(UDocFluidReservoirComponent* Comp);
	void UnregisterReservoirComponent(UDocFluidReservoirComponent* Comp);

	// Controls
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SetValveOpening(FName EdgeId, float Opening);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SetPumpEnabled(FName EdgeId, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SetEdgeBlocked(FName EdgeId, bool bBlocked);

	/** Why an edge does or does not flow: Enabled, Disabled (pump off), Blocked, UnavailableSupply (empty/unresolved/suspended) or Faulted (liquid mismatch, unknown edge). */
	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	EDocFluidPumpStatus GetEdgeStatus(FName EdgeId, FName& OutReason) const;

	/** Transfers committed by the last fixed step, with their rates. */
	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	TArray<FDocFluidTransfer> QueryTransferRates() const { return CumulativeLedger.StepTransfers; }

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SetExternalSupplyRate(FName ReservoirId, float LitersPerSec);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool SetLeakRate(FName ReservoirId, float LitersPerSec);

	// Direct Queries & Drains
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	FDocFluidDrainReceipt RequestDrain(FName ReservoirId, float RequestedVolume, bool bAllowPartial = false);

	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	bool QueryReservoir(FName ReservoirId, FDocFluidReservoirState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	double GetTotalSystemVolume() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	const FDocFluidLedger& GetCumulativeLedger() const { return CumulativeLedger; }

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	void ResetLedger();

	// Migration API
	/** Shrinking below the current volume moves the excess to quarantine or to the external-outflow sink; both are recorded. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool MigrateCapacity(FName ReservoirId, float NewCapacity, EDocFluidCapacityMigrationPolicy Policy, float& OutDiscrepancy);

	/** Moves quarantined volume back as free capacity allows. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool ReleaseQuarantine(FName ReservoirId, float& OutReleased);

	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	void CaptureState(FDocFluidNetworkSnapshot& OutSnapshot) const;

	/**
	 * Validates everything first, then replaces reservoirs, edges and rates. For a reservoir that is currently registered,
	 * the current (authored) capacity wins; saved volume above it is quarantined and recorded, never clamped away.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	bool StageRestore(const FDocFluidNetworkSnapshot& Snapshot);

	// Simulation execution
	/**
	 * Runs whole fixed steps of FixedStepDeltaTime, at most MaxSimulationStepsPerCatchUp per call. Time that does not fill
	 * a step, or exceeds the budget, is kept as lag for later calls; a long gap never becomes one long step.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	void StepSimulation(float DeltaTime);

	UFUNCTION(BlueprintPure, Category = "Doc|Fluid")
	double GetSimulationLagSeconds() const { return PendingLagSeconds; }

	/** Explicit offline policy: drop the lag without simulating it. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Fluid")
	void DiscardSimulationLag() { PendingLagSeconds = 0.0; }

	// Configuration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Fluid")
	int32 MaxSimulationStepsPerCatchUp = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Fluid")
	float FixedStepDeltaTime = 0.1f;

private:
	void ExecuteSingleStep(float StepDt);
	bool IsActive(FName ReservoirId) const;
	void SyncComponents();

	double PendingLagSeconds = 0.0;

	UPROPERTY()
	TMap<FName, FDocFluidReservoirState> Reservoirs;

	UPROPERTY()
	TMap<FName, FDocFluidEdge> Edges;

	UPROPERTY()
	TMap<FName, float> ExternalSupplyRates;

	UPROPERTY()
	TMap<FName, float> LeakRates;

	UPROPERTY()
	FDocFluidLedger CumulativeLedger;

	TArray<TWeakObjectPtr<UDocFluidReservoirComponent>> ReservoirComponents;
};
