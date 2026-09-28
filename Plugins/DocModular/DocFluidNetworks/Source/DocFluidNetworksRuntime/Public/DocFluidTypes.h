#pragma once

#include "CoreMinimal.h"
#include "DocFluidTypes.generated.h"

UENUM(BlueprintType)
enum class EDocFluidPumpStatus : uint8
{
	Enabled UMETA(DisplayName = "Enabled"),
	Disabled UMETA(DisplayName = "Disabled"),
	UnavailableSupply UMETA(DisplayName = "Unavailable Supply"),
	Blocked UMETA(DisplayName = "Blocked"),
	Faulted UMETA(DisplayName = "Faulted")
};

UENUM(BlueprintType)
enum class EDocFluidCapacityMigrationPolicy : uint8
{
	Quarantine UMETA(DisplayName = "Quarantine"),
	SpillToSink UMETA(DisplayName = "Spill To Sink")
};

UENUM(BlueprintType)
enum class EDocFluidDrainResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	Partial UMETA(DisplayName = "Partial"),
	Unavailable UMETA(DisplayName = "Unavailable"),
	InvalidAmount UMETA(DisplayName = "Invalid Amount")
};

USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName SourceReservoirId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName DestinationReservoirId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float MaxFlowRate = 10.0f; // Liters per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float ValveOpening = 1.0f; // [0.0, 1.0]

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bPumpRequired = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bPumpEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bIsBlocked = false;
};

USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidReservoirState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName ReservoirId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName FluidDefinitionId = FName("Water");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float Capacity = 100.0f; // Liters

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float CurrentVolume = 0.0f; // Liters

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	int32 Revision = 0;

	/** Volume set aside by a capacity migration (Quarantine policy). Not part of the active system volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float QuarantinedVolume = 0.0f;

	/** Stream-unloaded: contents retained, no transfers, supply or leaks until the reservoir is resumed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bSuspended = false;

	float GetFillFraction() const { return Capacity > 0.0f ? FMath::Clamp(CurrentVolume / Capacity, 0.0f, 1.0f) : 0.0f; }
	float GetFreeCapacity() const { return FMath::Max(0.0f, Capacity - CurrentVolume); }
};

USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidTransfer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName SourceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName DestinationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float ProposedVolume = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float CommittedVolume = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float RateLitersPerSecond = 0.0f;
};

USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidLedger
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalSystemVolume = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalExternalInflow = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalExternalOutflow = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalLeaks = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalDrains = 0.0;

	/** Explicit discards, e.g. the contents of a reservoir removed with UnregisterReservoir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double TotalDiscarded = 0.0;

	/** Volume moved into quarantine minus volume released from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double NetQuarantined = 0.0;

	/** Simulated time committed so far (fixed steps only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double SimulatedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	TArray<FDocFluidTransfer> StepTransfers;

	bool VerifyConservation(double InitialTotal, double FinalTotal, double Tolerance = 1e-4) const;
};

USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidDrainReceipt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName ReservoirId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float AmountDrained = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float RemainingRequested = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	EDocFluidDrainResult Result = EDocFluidDrainResult::Unavailable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	int32 CommittedRevision = 0;
};

/** Detached state for persistence: runtime volumes, controls and committed simulation time. */
USTRUCT(BlueprintType)
struct DOCFLUIDNETWORKSRUNTIME_API FDocFluidNetworkSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	TArray<FDocFluidReservoirState> Reservoirs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	TArray<FDocFluidEdge> Edges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	TMap<FName, float> ExternalSupplyRates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	TMap<FName, float> LeakRates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double SimulatedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	double PendingLagSeconds = 0.0;
};
