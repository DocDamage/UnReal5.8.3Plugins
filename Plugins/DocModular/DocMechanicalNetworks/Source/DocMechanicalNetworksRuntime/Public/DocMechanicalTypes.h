#pragma once

#include "CoreMinimal.h"
#include "DocMechanicalTypes.generated.h"

UENUM(BlueprintType)
enum class EDocDriveStallState : uint8
{
	Running UMETA(DisplayName = "Running"),
	Stalled UMETA(DisplayName = "Stalled"),
	Overloaded UMETA(DisplayName = "Overloaded")
};

UENUM(BlueprintType)
enum class EDocClutchState : uint8
{
	Engaged UMETA(DisplayName = "Engaged"),
	Disengaged UMETA(DisplayName = "Disengaged")
};

/** What a driver does when its tree reaches a missing or unloaded node (its load cannot be known). */
UENUM(BlueprintType)
enum class EDocMechanicalMissingLoadPolicy : uint8
{
	/** Treat the unknown load as unmet: the tree stalls with a MissingEndpoint diagnostic. */
	FailClosed UMETA(DisplayName = "Fail Closed"),
	/** Suspend the tree without calling it a stall. */
	Suspend UMETA(DisplayName = "Suspend")
};

UENUM(BlueprintType)
enum class EDocMechanicalDetachedPolicy : uint8
{
	HoldPhase UMETA(DisplayName = "Hold Phase"),
	FreeVisualCoast UMETA(DisplayName = "Free Visual Coast")
};

USTRUCT(BlueprintType)
struct DOCMECHANICALNETWORKSRUNTIME_API FDocDriveEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName ParentNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName ChildNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float Ratio = 1.0f; // Signed speed ratio (negative for gears, positive for belts/shafts)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float Efficiency = 1.0f; // (0.0, 1.0]

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsClutched = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsEngaged = true;
};

USTRUCT(BlueprintType)
struct DOCMECHANICALNETWORKSRUNTIME_API FDocDriveSourceState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName SourceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName AttachedNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float RequestedSpeed = 0.0f; // rad/s

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float TorqueCapacity = 100.0f; // Nm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float StallHysteresisMargin = 0.10f; // 10%

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsStalled = false;

	/** Committed outcome of the last solve: Running, Stalled (unmet load) or Overloaded (a node would exceed its maximum speed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mechanical")
	EDocDriveStallState DriveState = EDocDriveStallState::Running;

	/** Diagnostic for the state: UnmetLoad, Overspeed, MissingEndpoint, Suspended or None. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mechanical")
	FName StatusReason = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mechanical")
	float LastDemandTorque = 0.0f;
};

USTRUCT(BlueprintType)
struct DOCMECHANICALNETWORKSRUNTIME_API FDocMechanicalNodeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float RequestedSpeed = 0.0f; // rad/s

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float ActualSpeed = 0.0f; // rad/s

	/** Logical phase, wrapped to [0, 2*pi). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	double PhaseAngle = 0.0; // radians

	/** Signed accumulated revolutions, kept separately from the wrapped phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	double AccumulatedRevolutions = 0.0;

	/** Presentation-only phase: equals PhaseAngle while driven; coasts under FreeVisualCoast when detached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	double VisualPhaseAngle = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float VisualCoastSpeed = 0.0f;

	/** Absolute speed limit in rad/s (0 = unlimited). A solve that would exceed it faults the drive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float MaxOperatingSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	EDocMechanicalDetachedPolicy DetachedPolicy = EDocMechanicalDetachedPolicy::HoldPhase;

	/** Stream-unloaded: the record is kept; a tree that reaches it follows the missing-load policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bSuspended = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float AppliedLoadTorque = 0.0f; // Nm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float ReflectedLoadTorque = 0.0f; // Nm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsDriven = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsStalled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	int32 Revision = 0;
};

USTRUCT(BlueprintType)
struct DOCMECHANICALNETWORKSRUNTIME_API FDocDriveSolveResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsStalled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FString FailureReason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, float> ActualSpeeds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, float> ReflectedLoads;

	/** Edges named by a topology failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TArray<FName> OffendingEdgeIds;
};

/** Persisted controls and phase; speeds and reflected loads are recomputed. */
USTRUCT(BlueprintType)
struct DOCMECHANICALNETWORKSRUNTIME_API FDocMechanicalSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, double> Phases;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, double> Revolutions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, float> Loads;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, bool> ClutchEngaged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, float> SourceSpeeds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, float> SourceCapacities;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	TMap<FName, bool> SourceEnabled;
};
