#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocModContentTypes.h"
#include "DocModDefinitionProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocModDefinitionProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface implemented by feature modules to register and validate mod definitions.
 */
class DOCMODCONTENTRUNTIME_API IDocModDefinitionProvider
{
	GENERATED_BODY()

public:
	virtual FString GetSupportedSchema() const = 0;
	virtual int32 GetSupportedSchemaVersion() const = 0;

	virtual bool ValidateDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError) = 0;
	virtual bool StageDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError) = 0;
	virtual void CommitStagedDefinitions() = 0;
	virtual void RollbackStagedDefinitions() = 0;
	virtual void DeactivateDefinition(const FString& DefinitionId) = 0;
};

/**
 * Built-in sample provider for doc.sample.definition, verifying end-to-end functionality
 * without requiring any sibling plugin dependencies (MOD-10).
 */
UCLASS()
class DOCMODCONTENTRUNTIME_API UDocModSampleDefinitionProvider : public UObject, public IDocModDefinitionProvider
{
	GENERATED_BODY()

public:
	virtual FString GetSupportedSchema() const override { return TEXT("doc.sample.definition"); }
	virtual int32 GetSupportedSchemaVersion() const override { return 1; }

	virtual bool ValidateDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError) override;
	virtual bool StageDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError) override;
	virtual void CommitStagedDefinitions() override;
	virtual void RollbackStagedDefinitions() override;
	virtual void DeactivateDefinition(const FString& DefinitionId) override;

	// Query
	bool HasCommittedDefinition(const FString& DefinitionId) const;
	FString GetCommittedDefinitionContent(const FString& DefinitionId) const;
	int32 GetCommittedCount() const { return CommittedDefinitions.Num(); }
	int32 GetStagedCount() const { return StagedDefinitions.Num(); }

	// Test hooks for atomic failure verification
	bool bSimulateStageFailure = false;
	bool bSimulateValidateFailure = false;
	FString SimulateFailureDefinitionId;

private:
	TMap<FString, FString> StagedDefinitions;
	TMap<FString, FString> CommittedDefinitions;
};
