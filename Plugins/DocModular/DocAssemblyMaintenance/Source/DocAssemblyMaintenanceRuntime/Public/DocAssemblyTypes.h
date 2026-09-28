#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "DocAssemblyTypes.generated.h"

/** Every part instance has exactly one logical location. */
UENUM(BlueprintType)
enum class EDocPartLocationKind : uint8
{
	Installed,
	Detached,
	/** Kept but unusable (missing content or corrupt record); never deleted or duplicated. */
	Quarantined,
	/** Reserved from an external provider while a transfer is staged or in doubt (not yet ours). */
	InTransit
};

UENUM(BlueprintType)
enum class EDocFastenerState : uint8
{
	Secured,
	Released,
	Damaged
};

UENUM(BlueprintType)
enum class EDocAssemblyOperationType : uint8
{
	Inspect,
	Test,
	ReleaseFastener,
	SecureFastener,
	RemovePart,
	InstallPart,
	ReplacePart,
	CompleteProcedure
};

UENUM(BlueprintType)
enum class EDocDiagnosticOutcome : uint8
{
	Pass,
	Fail,
	Inconclusive,
	BlockedByPower,
	BlockedByConfig
};

UENUM(BlueprintType)
enum class EDocAssemblyTransactionState : uint8
{
	Staged,
	Committed,
	RolledBack,
	Failed,
	/** The external provider could not confirm the transfer; nothing was applied locally until reconciliation. */
	InDoubt
};

/** Outcome reported by an external part provider. */
UENUM(BlueprintType)
enum class EDocAssemblyTransferOutcome : uint8
{
	Committed,
	Failed,
	InDoubt
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocPartSlotDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName SlotId = NAME_None;

	/** Parts whose definition carries any of these tags fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGameplayTagContainer AllowedPartTags;

	/** Parts with these definition ids fit. If both lists are empty, any known part definition fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> AllowedPartDefinitionIds;

	/** Base supports capacity 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int32 Capacity = 1;

	/** Structural parent (acyclic). A child slot is only reachable while its parent's part is installed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName ParentSlotId = NAME_None;

	/** Fasteners that must be released before the part in this slot can be removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> RequiredFastenerIds;

	/** Installed parts in these slots block access to this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> ObstructingSlotIds;

	/** Removing/installing here requires the machine-state provider to report a lockout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bRequiresLockout = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FTransform RelativeTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocFastenerDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName FastenerId = NAME_None;

	/** Tool capability required (verified by the resource provider). Empty = no tool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGameplayTag RequiredToolTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float ActionDurationSeconds = 0.5f;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocFastenerRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName FastenerId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocFastenerState State = EDocFastenerState::Secured;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float Tightness = 1.0f;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocPartInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid PartInstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName PartDefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int32 DefinitionVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocPartLocationKind LocationKind = EDocPartLocationKind::Detached;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName InstalledSlotId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FTransform DetachedTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float Condition = 1.0f;

	/** Authoritative fault state. Never returned by QueryAssembly; players learn faults only through diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> HiddenFaults;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 Revision = 0;

	/** Why a quarantined part was quarantined. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FString QuarantineReason;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocDiagnosticTestDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TestId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bRequiresPower = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> EvaluatedSlotIds;

	/** Only these hidden faults can be observed by this test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> DetectableFaultTags;

	/** A passing run of this test at the current revision is required to complete a procedure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bIsFunctionalTest = false;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocDiagnosticReport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TestId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocDiagnosticOutcome Outcome = EDocDiagnosticOutcome::Inconclusive;

	/** Only faults this test can detect (plus missing parts). Hidden faults outside its scope never appear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FName> ObservedFaults;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	double Timestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bIsConclusive = false;

	/** Assembly revision the observation describes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 AssemblyRevision = 0;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocAssemblyOperation
{
	GENERATED_BODY()

	/** Assigned when the request is accepted (the operation handle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperatorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyOperationType OperationType = EDocAssemblyOperationType::Inspect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetSlotId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetFastenerId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid PartInstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid ReplacementPartInstanceId;

	/** Assembly revision the caller saw; 0 = do not check. A mismatch is a Conflict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 ExpectedRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyTransactionState TransactionState = EDocAssemblyTransactionState::Staged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bCommitted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bCancelled = false;

	/** Set when the part came from an external provider (inventory bridge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bExternalPart = false;
};

/** Exclusive manipulation claim on a slot, fastener or part. */
USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocAssemblyClaim
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetSlotId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetFastenerId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid PartInstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperatorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid SessionId;

	/** Operation that owns this claim (invalid for an explicit manipulation lease). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	double ExpiryTime = 0.0;
};

/** Durable record of an operation's terminal outcome. */
USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocAssemblyReceipt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyOperationType OperationType = EDocAssemblyOperationType::Inspect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyTransactionState State = EDocAssemblyTransactionState::Staged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetSlotId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid PartInstanceId;

	/** Definition of an external part (needed to finish an in-doubt install after a reload). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName PartDefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bExternalPart = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid OperatorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 AssemblyRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FString Note;
};

USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocAssemblyState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName AssemblyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 Revision = 0;

	/** Keyed by slot id; each part's InstalledSlotId must equal its key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TMap<FName, FDocPartInstance> InstalledParts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TMap<FGuid, FDocPartInstance> DetachedParts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TMap<FName, FDocFastenerRecord> Fasteners;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocDiagnosticReport> DiagnosticHistory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocPartInstance> QuarantinedParts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocAssemblyReceipt> Receipts;

	/** Revision at which the functional test last passed (0 = never). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int64 LastFunctionalPassRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bProcedureComplete = false;
};

/** Progress summary for UI, derived from committed state only. */
USTRUCT(BlueprintType)
struct DOCASSEMBLYMAINTENANCERUNTIME_API FDocAssemblyProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 ReleasedFasteners = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 EmptySlots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 PendingOperations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 InDoubtTransfers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bFunctionalTestCurrent = false;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bProcedureComplete = false;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UDocAssemblyResourceProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Trusted capability/resource provider (tools, machine state, external parts). Defaults are the safe answers:
 * no tools, unknown machine state, no external parts. The assembly never assumes a capability it was not given.
 */
class DOCASSEMBLYMAINTENANCERUNTIME_API IDocAssemblyResourceProvider
{
	GENERATED_BODY()

public:
	virtual bool HasToolCapability(const FGuid& OperatorId, const FGameplayTag& ToolTag) { return false; }

	/** Returns false when machine state is unknown. */
	virtual bool QueryMachineState(bool& bOutPowered, bool& bOutLockedOut) { return false; }

	/** Reserves an external part for installation and reports its definition. */
	virtual bool ReserveExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId, FName& OutPartDefinitionId) { return false; }

	/** Transfers a reserved part into the assembly. InDoubt means the provider cannot confirm either way yet. */
	virtual EDocAssemblyTransferOutcome CommitExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId) { return EDocAssemblyTransferOutcome::Failed; }

	/** Releases a reservation that will not be committed. */
	virtual void ReleaseExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId) {}

	/** Resolves an in-doubt transfer. */
	virtual EDocAssemblyTransferOutcome QueryExternalTransfer(const FGuid& OperatorId, const FGuid& PartInstanceId) { return EDocAssemblyTransferOutcome::InDoubt; }
};
