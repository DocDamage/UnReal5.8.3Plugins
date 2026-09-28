#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocMechanicalTypes.h"
#include "DocMechanicalComponents.h"
#include "DocMechanicalNetworkSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnDriveChanged, FName, SourceId, float, RequestedSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnDriveStalled, FName, SourceId, FName, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnDriveRecovered, FName, SourceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnClutchChanged, FName, EdgeId, bool, bEngaged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnDriveStalledNative, FName, FName);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnDriveRecoveredNative, FName);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnClutchChangedNative, FName, bool);

/**
 * Rotational drive networks: rooted trees of signed-ratio edges, one driver per active tree, reflected loads and stalls.
 * Topology edits are validated before they commit; a failed solve leaves the last committed state untouched.
 */
UCLASS(BlueprintType)
class DOCMECHANICALNETWORKSRUNTIME_API UDocMechanicalNetworkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDocMechanicalNetworkSubsystem();

	//~ Subsystem lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Registration API (duplicates are refused; ExpectedRevision >= 0 refuses a stale edit)
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool RegisterNode(const FDocMechanicalNodeState& InNode);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool UnregisterNode(FName NodeId);

	/** Stream unload: keep the record (phase included); trees that reach it follow MissingLoadPolicy. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool SuspendNode(FName NodeId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool ResumeNode(FName NodeId);

	/** The source must attach to a tree root, and its tree may have no other enabled driver. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool RegisterDriveSource(const FDocDriveSourceState& InSource);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool UnregisterDriveSource(FName SourceId);

	/** Refused (with GetLastTopologyError and offending edge ids) if it would give a node two parents, close a cycle, or join two driven trees. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool ConnectDrive(const FDocDriveEdge& InEdge, int64 ExpectedRevision = -1);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool DisconnectDrive(FName EdgeId, int64 ExpectedRevision = -1);

	void RegisterNodeComponent(UDocMechanicalNodeComponent* Comp);
	void UnregisterNodeComponent(UDocMechanicalNodeComponent* Comp);

	// Controls
	/** Engaging validates the resulting topology and driver count; disengaging always commits. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool SetClutchState(FName EdgeId, bool bEngaged, int64 ExpectedRevision = -1);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool SetSourceSpeed(FName SourceId, float InSpeed);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool SetSourceCapacity(FName SourceId, float InCapacity);

	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool SetNodeLoad(FName NodeId, float LoadTorque);

	// Queries
	UFUNCTION(BlueprintPure, Category = "Doc|Mechanical")
	bool QueryNodeDrive(FName NodeId, FDocMechanicalNodeState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Mechanical")
	bool QueryDriveSource(FName SourceId, FDocDriveSourceState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Mechanical")
	TMap<FName, float> QueryReflectedLoads() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Mechanical")
	int64 GetTopologyRevision() const { return TopologyRevision; }

	UFUNCTION(BlueprintPure, Category = "Doc|Mechanical")
	FString GetLastTopologyError() const { return LastTopologyError; }

	/** Validates first; commits speeds, loads and drive states only when the topology is valid. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	FDocDriveSolveResult SolveNetwork();

	// Simulation execution
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	void StepSimulation(float DeltaTime);

	// Persistence
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	void CaptureState(FDocMechanicalSnapshot& OutSnapshot) const;

	/** Restores controls and phase without emitting events or revolution triggers; refuses (changing nothing) if invalid. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Mechanical")
	bool StageRestore(const FDocMechanicalSnapshot& Snapshot);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Mechanical")
	EDocMechanicalMissingLoadPolicy MissingLoadPolicy = EDocMechanicalMissingLoadPolicy::FailClosed;

	/** Seconds a FreeVisualCoast output takes to coast to rest (presentation only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Mechanical")
	float VisualCoastSeconds = 1.0f;

	// Events (after committed state)
	UPROPERTY(BlueprintAssignable, Category = "Doc|Mechanical|Events")
	FDocOnDriveChanged OnDriveChanged;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Mechanical|Events")
	FDocOnDriveStalled OnStalled;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Mechanical|Events")
	FDocOnDriveRecovered OnRecovered;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Mechanical|Events")
	FDocOnClutchChanged OnClutchChanged;

	FDocOnDriveStalledNative OnStalledNative;
	FDocOnDriveRecoveredNative OnRecoveredNative;
	FDocOnClutchChangedNative OnClutchChangedNative;

private:
	bool ValidateTopology(const TMap<FName, FDocDriveEdge>& EdgeSet, const TMap<FName, FDocDriveSourceState>& SourceSet, FString& OutError, TArray<FName>& OutOffendingEdges) const;
	bool CheckRevision(int64 ExpectedRevision);
	void CommitTopologyChange();

	UPROPERTY()
	TMap<FName, FDocMechanicalNodeState> Nodes;

	UPROPERTY()
	TMap<FName, FDocDriveEdge> Edges;

	UPROPERTY()
	TMap<FName, FDocDriveSourceState> Sources;

	TArray<TWeakObjectPtr<UDocMechanicalNodeComponent>> NodeComponents;
	int64 TopologyRevision = 1;
	FString LastTopologyError;
};
