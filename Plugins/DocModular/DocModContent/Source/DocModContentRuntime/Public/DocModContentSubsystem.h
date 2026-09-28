#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DocModContentTypes.h"
#include "DocModDefinitionProvider.h"
#include "DocModContentSubsystem.generated.h"

/**
 * GameInstance subsystem providing mod content discovery, topological dependency ordering,
 * staged catalog activation transactions, pinning, and missing-content quarantine.
 */
UCLASS()
class DOCMODCONTENTRUNTIME_API UDocModContentSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UDocModContentSubsystem();

	static UDocModContentSubsystem* Get(const UObject* WorldContextObject);
	static void SetSubsystemOverrideForTesting(UDocModContentSubsystem* Override);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Provider registration
	void RegisterProvider(TSharedPtr<IDocModDefinitionProvider> Provider);
	void RegisterProviderObject(UObject* ProviderObject);
	void UnregisterProvider(const FString& Schema);
	IDocModDefinitionProvider* FindProvider(const FString& Schema) const;

	// Discovery & Validation
	bool DiscoverPackInDirectory(const FString& DirectoryPath, FDocModManifest& OutManifest, FString& OutError);
	EDocModValidationResult ValidateManifest(const FDocModManifest& Manifest, FString& OutError) const;
	EDocModValidationResult ValidatePathSafety(const FString& PackRoot, const FString& RelativePath, FString& OutCanonicalPath, FString& OutError) const;
	EDocModValidationResult ValidatePackContent(const FDocModManifest& Manifest, const FString& PackRoot, FString& OutError, TMap<FString, FString>& OutDefinitionHashes);

	// Dependency Resolution & Plan
	bool BuildActivationPlan(const TArray<FDocModManifest>& Packs, FDocModActivationPlan& OutPlan);

	// Transactional Activation
	EDocModValidationResult ActivatePlan(const FDocModActivationPlan& Plan, const TArray<FDocModManifest>& Packs);
	EDocModValidationResult RequestDeactivatePack(const FString& PackId);

	// Pinning
	FDocModPinHandle AcquireDefinitionPin(const FString& DefinitionId);
	bool ReleaseDefinitionPin(const FDocModPinHandle& PinHandle);
	bool IsDefinitionPinned(const FString& DefinitionId) const;
	int32 GetActivePinCount(const FString& DefinitionId) const;
	bool HasActivePinsForPack(const FString& PackId) const;

	// Quarantine
	void QuarantineMissingContent(const FString& PackId, const FString& DefinitionId, const FString& PreservedPayloadJson, const FString& Reason);
	TArray<FDocModQuarantineRecord> GetQuarantinedRecords() const;
	void ClearQuarantine();

	// Catalog Queries
	FDocModCatalogRevision GetCurrentCatalogRevision() const { return CurrentCatalog; }
	bool IsPackActive(const FString& PackId) const;
	bool FindDefinition(const FString& DefinitionId, FDocModDefinitionEntry& OutEntry) const;

	// Static hash utility
	static FString ComputeContentHash(const FString& Content);

private:
	TMap<FString, TSharedPtr<IDocModDefinitionProvider>> SharedProviders;
	TMap<FString, TWeakObjectPtr<UObject>> ObjectProviders;

	FDocModCatalogRevision CurrentCatalog;
	TMap<FString, FDocModManifest> ActiveManifests;

	// Active pins: DefinitionId -> Set of PinIds
	TMap<FString, TSet<FGuid>> ActivePins;

	// Quarantine
	TArray<FDocModQuarantineRecord> QuarantineStore;
};
