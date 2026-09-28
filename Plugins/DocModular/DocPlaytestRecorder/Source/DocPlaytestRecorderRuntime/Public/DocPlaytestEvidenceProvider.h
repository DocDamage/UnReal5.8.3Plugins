#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocPlaytestTypes.h"
#include "DocPlaytestEvidenceProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocPlaytestEvidenceProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for feature modules or adapters to capture contextual diagnostic snapshots.
 */
class DOCPLAYTESTRECORDERRUNTIME_API IDocPlaytestEvidenceProvider
{
	GENERATED_BODY()

public:
	virtual FString GetProviderName() const = 0;
	virtual bool CaptureSnapshot(FDocDiagnosticSnapshot& OutSnapshot, FString& OutError) = 0;
	virtual void NotifyWorldEpochBoundary(const FString& WorldName) = 0;
};

/**
 * Sample evidence provider used for self-contained testing (DBG-04, DBG-10).
 */
UCLASS()
class DOCPLAYTESTRECORDERRUNTIME_API UDocSamplePlaytestProvider : public UObject, public IDocPlaytestEvidenceProvider
{
	GENERATED_BODY()

public:
	virtual FString GetProviderName() const override { return TEXT("doc.sample.provider"); }
	virtual bool CaptureSnapshot(FDocDiagnosticSnapshot& OutSnapshot, FString& OutError) override;
	virtual void NotifyWorldEpochBoundary(const FString& WorldName) override;

	// Test control hooks
	bool bSimulateFailure = false;
	FString CustomPayloadData = TEXT("{\"sample_key\": \"sample_val\"}");
	int32 EpochNotificationCount = 0;
	FString LastNotifiedWorld;
};
