#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocSystemResult.h"
#include "DocAssemblyTypes.h"
#include "DocAssemblyDefinitions.h"
#include "DocAssemblyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnAssemblyOperationCommitted, const FDocAssemblyOperation&, Operation, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnDiagnosticCompleted, const FDocDiagnosticReport&, Report);

/**
 * A multipart machine. Operations go through: validate session/owner/revision -> acquire claims ->
 * validate resources and prerequisites (stage) -> commit logical state -> presentation -> receipt -> release claims.
 * Failed validation never mutates anything; staged operations can be cancelled until commit.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocAssemblyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxPresentationAttempts = 3;
	static constexpr int32 MaxHistory = 32;
	static constexpr int32 MaxReceipts = 64;

	UDocAssemblyComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TObjectPtr<UDocAssemblyDefinition> AssemblyDefinition;

	UPROPERTY(BlueprintAssignable, Category = "Assembly")
	FDocOnAssemblyOperationCommitted OnOperationCommitted;

	UPROPERTY(BlueprintAssignable, Category = "Assembly")
	FDocOnDiagnosticCompleted OnDiagnosticCompleted;

	/** Trusted tool/machine-state/external-part provider (must implement IDocAssemblyResourceProvider). */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult SetResourceProvider(UObject* Provider);

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult InitializeAssembly(UDocAssemblyDefinition* InDefinition);

	// Sessions and claims

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult BeginMaintenanceSession(FGuid OperatorId, FGuid& OutSessionId);

	/** Ends a session, cancelling its staged operations and releasing its leases. In-doubt transfers persist. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult EndMaintenanceSession(FGuid SessionId);

	/** Explicit manipulation lease; other operators cannot operate on the target while it is held. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult AcquireClaim(FGuid SessionId, FName TargetSlotId, FName TargetFastenerId);

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult ReleaseClaim(FGuid SessionId, FName TargetSlotId, FName TargetFastenerId);

	// Operations

	/** Validates and stages an operation, taking exclusive claims on its targets. Returns the operation handle. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult RequestOperation(const FDocAssemblyOperation& InOperation, FGuid& OutOperationId);

	/** Commits the staged operation's logical change. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult CommitOperation(const FGuid& OperationId);

	/** Before commit: rolls back and releases claims/reservations. After commit: NoChange (the committed outcome stands). */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult CancelOperation(const FGuid& OperationId);

	/** RequestOperation + CommitOperation. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult ExecuteOperationImmediate(const FDocAssemblyOperation& InOperation);

	/** Resolves in-doubt external transfers with the provider. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult ReconcileTransfers();

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult RunDiagnostic(FName TestId, FDocDiagnosticReport& OutReport);

	// Queries

	/** Player-facing view: hidden faults are removed. */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	void QueryAssembly(FDocAssemblyState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult QueryAvailableOperations(FGuid SessionId, TArray<FDocAssemblyOperation>& OutOperations) const;

	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocAssemblyProgress QueryProcedureProgress() const;

	UFUNCTION(BlueprintPure, Category = "Assembly")
	FName GetAssemblyId() const { return AssemblyState.AssemblyId; }

	UFUNCTION(BlueprintPure, Category = "Assembly")
	int64 GetRevision() const { return AssemblyState.Revision; }

	UFUNCTION(BlueprintPure, Category = "Assembly")
	bool IsSlotAccessible(FName SlotId) const;

	UFUNCTION(BlueprintPure, Category = "Assembly")
	bool AreFastenersReleased(FName SlotId) const;

	UFUNCTION(BlueprintPure, Category = "Assembly")
	bool IsPartInstalled(const FGuid& PartInstanceId) const;

	/** Number of logical locations the part occupies (must be 0 or 1). */
	UFUNCTION(BlueprintPure, Category = "Assembly")
	int32 CountPartLocations(const FGuid& PartInstanceId) const;

	const FDocPartInstance* FindPart(const FGuid& PartInstanceId) const;
	const FDocPartInstance* GetInstalledPartInSlot(FName SlotId) const;
	const FDocFastenerRecord* FindFastenerRecord(FName FastenerId) const { return AssemblyState.Fasteners.Find(FastenerId); }
	const FDocAssemblyReceipt* FindReceipt(const FGuid& OperationId) const;

	// Persistence

	/** Full authoritative state for saving (includes hidden faults). */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocAssemblyState CaptureAssemblyState() const { return AssemblyState; }

	/**
	 * Validates one-location-per-part and migrates definition changes: parts in removed/incompatible slots are
	 * detached, parts with unknown definitions or duplicate records are quarantined (never deleted), fasteners are
	 * reconciled with the definition, and in-doubt receipts are re-armed for reconciliation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	FDocSystemResult StageRestore(const FDocAssemblyState& InState);

	// Presentation

	/** The next N visual spawns fail (tests / fault injection). */
	void SetSimulatedPresentationFailures(int32 Count) { PresentationFailuresRemaining = FMath::Max(0, Count); }
	bool IsPresentationPending(const FGuid& PartInstanceId) const { return PendingPresentationParts.Contains(PartInstanceId); }
	int32 GetVisualMeshSpawnCount(const FGuid& PartInstanceId) const;

	/** Bounded retry of a failed visual spawn; never touches logical ownership. */
	FDocSystemResult RetryPresentation(const FGuid& PartInstanceId);

private:
	struct FTargets
	{
		FName Slot = NAME_None;
		FName Fastener = NAME_None;
		TArray<FGuid> Parts;
	};

	IDocAssemblyResourceProvider* GetProvider() const;
	FTargets GetTargets(const FDocAssemblyOperation& Op) const;
	bool IsClaimedAgainst(const FTargets& Targets, const FGuid& OperatorId, const FGuid& IgnoreOperationId) const;
	FDocSystemResult CheckLockout(const FDocPartSlotDefinition& Slot) const;
	FDocSystemResult ValidateOperation(const FDocAssemblyOperation& Op, bool bRevalidate, FName& OutExternalDefinition) const;
	void ApplyOperation(const FDocAssemblyOperation& Op, FName ExternalDefinition);
	void AddReceipt(const FDocAssemblyOperation& Op, EDocAssemblyTransactionState State, const FString& Note, FName ExternalDefinition = NAME_None);
	void ReleaseOperationClaims(const FGuid& OperationId);
	void AbortStaged(const FGuid& OperationId, EDocAssemblyTransactionState FinalState, const FString& Note);
	void TrySpawnPresentation(const FGuid& PartInstanceId);
	bool IsSlotReachable(const FDocPartSlotDefinition& Slot) const;

	UPROPERTY()
	FDocAssemblyState AssemblyState;

	UPROPERTY()
	TObjectPtr<UObject> ResourceProviderObject;

	UPROPERTY()
	TArray<FDocAssemblyClaim> ActiveClaims;

	TMap<FGuid, FGuid> Sessions; // SessionId -> OperatorId
	TMap<FGuid, FDocAssemblyOperation> StagedOperations;
	TMap<FGuid, FDocAssemblyOperation> InDoubtOperations;
	TMap<FGuid, FName> ExternalDefinitions; // OperationId -> external part definition
	TSet<FGuid> PendingPresentationParts;
	TMap<FGuid, int32> VisualSpawnCounts;
	TMap<FGuid, int32> PresentationAttempts;
	int32 PresentationFailuresRemaining = 0;
};
