#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocAssemblyTypes.h"
#include "DocAssemblyLocalProvider.generated.h"

/**
 * Local capability provider for standalone machines and tests: a granted tool list, an explicitly configured
 * machine state, and a small external-part stock. A production inventory bridge replaces it.
 */
UCLASS(BlueprintType)
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocAssemblyLocalProvider : public UObject, public IDocAssemblyResourceProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGameplayTagContainer GrantedTools;

	/** When false, machine state is reported as unknown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bMachineStateKnown = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bPowered = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bLockedOut = true;

	/** Outcome the next CommitExternalPart returns (tests use Failed / InDoubt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyTransferOutcome NextCommitOutcome = EDocAssemblyTransferOutcome::Committed;

	/** What an in-doubt transfer turns out to have been when queried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EDocAssemblyTransferOutcome InDoubtResolution = EDocAssemblyTransferOutcome::InDoubt;

	/** External stock: part id -> part definition id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TMap<FGuid, FName> Stock;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	TSet<FGuid> Reserved;

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	TSet<FGuid> Transferred;

	virtual bool HasToolCapability(const FGuid& OperatorId, const FGameplayTag& ToolTag) override;
	virtual bool QueryMachineState(bool& bOutPowered, bool& bOutLockedOut) override;
	virtual bool ReserveExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId, FName& OutPartDefinitionId) override;
	virtual EDocAssemblyTransferOutcome CommitExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId) override;
	virtual void ReleaseExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId) override;
	virtual EDocAssemblyTransferOutcome QueryExternalTransfer(const FGuid& OperatorId, const FGuid& PartInstanceId) override;

	/** Parts still owned by the provider (stock minus transferred). */
	int32 CountOwned() const { return Stock.Num(); }
};
