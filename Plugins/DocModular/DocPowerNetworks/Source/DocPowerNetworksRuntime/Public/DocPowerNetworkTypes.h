#pragma once

#include "CoreMinimal.h"
#include "DocPowerNetworkTypes.generated.h"

UENUM(BlueprintType)
enum class EDocPowerNodeKind : uint8
{
	Source,
	Consumer,
	Storage,
	Switch,
	Breaker
};

UENUM(BlueprintType)
enum class EDocPowerConsumerState : uint8
{
	Disconnected,
	Off,
	Supplied,
	Brownout,
	Faulted
};

UENUM(BlueprintType)
enum class EDocPowerAllocationMode : uint8
{
	Binary,
	Scalable
};

UENUM(BlueprintType)
enum class EDocPowerStorageMode : uint8
{
	Auto,
	ChargeOnly,
	DischargeOnly,
	Standby
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocPowerPortId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FName PortName = NAME_None;

	FDocPowerPortId() = default;
	FDocPowerPortId(FName InNodeId, FName InPortName)
		: NodeId(InNodeId), PortName(InPortName)
	{
	}

	bool IsValid() const
	{
		return !NodeId.IsNone() && !PortName.IsNone();
	}

	bool operator==(const FDocPowerPortId& Other) const
	{
		return NodeId == Other.NodeId && PortName == Other.PortName;
	}

	bool operator!=(const FDocPowerPortId& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FDocPowerPortId& PortId)
	{
		return HashCombine(GetTypeHash(PortId.NodeId), GetTypeHash(PortId.PortName));
	}
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocPowerEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FDocPowerPortId PortA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FDocPowerPortId PortB;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bClosed = true;

	FDocPowerEdge() = default;
	FDocPowerEdge(FName InEdgeId, const FDocPowerPortId& InPortA, const FDocPowerPortId& InPortB, bool bInClosed = true)
		: EdgeId(InEdgeId), PortA(InPortA), PortB(InPortB), bClosed(bInClosed)
	{
	}
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocPowerAllocation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	FName NodeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double RequestedWatts = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double DeliveredWatts = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double DeliveredJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	EDocPowerConsumerState State = EDocPowerConsumerState::Disconnected;
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocPowerIslandState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	int32 IslandId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TArray<FName> NodeIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double TotalPotentialSupplyWatts = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double TotalRequestedWatts = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double TotalDeliveredWatts = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double TotalStoredEnergyJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	bool bHasTripOrFault = false;
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocEnergyLedger
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double ExternalEnergyAcceptedJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double StoredEnergyBeforeJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double LoadEnergyDeliveredJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double StoredEnergyAfterJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double ConversionLossesJoules = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double ExplicitDiscardJoules = 0.0;

	bool IsBalanced(double Tolerance = 1e-3) const
	{
		const double InEnergy = ExternalEnergyAcceptedJoules + StoredEnergyBeforeJoules;
		const double OutEnergy = LoadEnergyDeliveredJoules + StoredEnergyAfterJoules + ConversionLossesJoules + ExplicitDiscardJoules;
		return FMath::IsNearlyEqual(InEnergy, OutEnergy, Tolerance);
	}
};

USTRUCT(BlueprintType)
struct DOCPOWERNETWORKSRUNTIME_API FDocPowerNetworkSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	int64 TopologyRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	int64 StepOrdinal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, double> StorageEnergies;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, bool> SwitchStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, bool> BreakerTripped;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, double> BreakerRemainingCooldowns;

	/** Continuous overload time accumulated toward each breaker's trip duration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, double> BreakerOverloadSeconds;

	/** Simulation time not yet processed when the snapshot was taken. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double PendingLagSeconds = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocOnPowerSupplyChanged, FName, NodeId, double, DeliveredWatts, EDocPowerConsumerState, State);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocOnPowerSupplyChangedNative, FName /*NodeId*/, double /*DeliveredWatts*/, EDocPowerConsumerState /*State*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnBreakerTripped, FName, BreakerId, FString, Reason);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnBreakerTrippedNative, FName /*BreakerId*/, const FString& /*Reason*/);
