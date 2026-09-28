#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocSystemResult.h"
#include "DocAssemblyTypes.h"
#include "DocAssemblyDefinitions.generated.h"

UCLASS(BlueprintType)
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocAssemblyPartDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName PartDefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGameplayTagContainer PartTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float Mass = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float DefaultCondition = 1.0f;
};

UCLASS(BlueprintType)
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocAssemblyDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName AssemblyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocPartSlotDefinition> Slots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocFastenerDefinition> Fasteners;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocDiagnosticTestDefinition> Diagnostics;

	/** Every part definition this assembly understands. Parts with unknown definitions are quarantined on restore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<TObjectPtr<UDocAssemblyPartDefinition>> PartDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocPartInstance> InitialParts;

	/** Ids, references, capacity, acyclic parents, and a consistent initial layout. */
	FDocSystemResult ValidateDefinition() const;

	/** True when the part definition is known and allowed in the slot. */
	bool IsCompatible(FName SlotId, FName PartDefinitionId) const;

	const UDocAssemblyPartDefinition* FindPartDefinition(FName PartDefinitionId) const;

	const FDocPartSlotDefinition* FindSlot(FName SlotId) const
	{
		return Slots.FindByPredicate([SlotId](const FDocPartSlotDefinition& S) { return S.SlotId == SlotId; });
	}

	const FDocFastenerDefinition* FindFastener(FName FastenerId) const
	{
		return Fasteners.FindByPredicate([FastenerId](const FDocFastenerDefinition& F) { return F.FastenerId == FastenerId; });
	}

	const FDocDiagnosticTestDefinition* FindDiagnostic(FName TestId) const
	{
		return Diagnostics.FindByPredicate([TestId](const FDocDiagnosticTestDefinition& D) { return D.TestId == TestId; });
	}
};

UCLASS(BlueprintType)
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocMaintenanceProcedure : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName ProcedureId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TargetAssemblyId = NAME_None;

	/** Guidance only: operations are always validated against current state, never against this list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FDocAssemblyOperation> RequiredSteps;
};
