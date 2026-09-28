#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DocPlaytestTypes.h"
#include "DocPlaytestEvidenceProvider.h"
#include "DocPlaytestRecorderSubsystem.generated.h"

/**
 * GameInstance subsystem providing bounded diagnostic capture, ring buffering,
 * issue marker timeline correlation, and local evidence bundle exports.
 */
UCLASS()
class DOCPLAYTESTRECORDERRUNTIME_API UDocPlaytestRecorderSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UDocPlaytestRecorderSubsystem();

	static UDocPlaytestRecorderSubsystem* Get(const UObject* WorldContextObject);
	static void SetSubsystemOverrideForTesting(UDocPlaytestRecorderSubsystem* Override);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Enable / Disable capture. Capture is off until a host explicitly opts in (tester consent).
	void EnableCapture();
	void DisableCapture();
	bool IsCaptureEnabled() const { return bCaptureEnabled; }

	// Event Recording
	void RecordEvent(const FString& Category, const FString& PayloadJson);

	// Ring Buffer Queries
	int32 GetRetainedEventCount() const { return EventRingBuffer.Num(); }
	int64 GetRetainedBufferBytes() const { return CurrentBufferBytes; }
	int32 GetEvictedEventCount() const { return EvictedEventCount; }
	TArray<FDocPlaytestEvent> GetEventsInTimeRange(double StartMonotonicSeconds, double EndMonotonicSeconds) const;

	// Issue Markers
	FDocIssueMarker AddIssueMarker(const FString& TesterNote, const FString& Category, float PreWindowSeconds = 10.0f, float PostWindowSeconds = 5.0f);
	bool FinalizeMarker(const FGuid& MarkerId);
	bool GetMarker(const FGuid& MarkerId, FDocIssueMarker& OutMarker) const;

	// Providers
	void RegisterEvidenceProvider(UObject* ProviderObj);
	void UnregisterEvidenceProvider(const FString& ProviderName);

	// World Lifecycle
	void NotifyWorldTeardown(const FString& WorldName);

	// Bundle Export
	bool ExportIssueBundle(const FGuid& MarkerId, const FString& TargetDirectory, FString& OutExportPath, FString& OutError);
	void CancelExport(const FGuid& MarkerId);

	// Helpers
	static FString SanitizeAndRedact(const FString& InputJson, const TArray<FString>& RedactedKeys);
	static bool ValidateImportedPath(const FString& BaseDir, const FString& RelativePath, FString& OutFullPath);
	static FString ComputeFileHash(const FString& FileContent);

	// Test control hooks
	bool bSimulateExportDiskFault = false;

private:
	void EnforceBufferLimits();
	TArray<FString> GetRedactedKeywords() const;

	bool bCaptureEnabled = false;
	int64 CurrentBufferBytes = 0;
	int32 EvictedEventCount = 0;

	TArray<FDocPlaytestEvent> EventRingBuffer;
	TMap<FGuid, FDocIssueMarker> Markers;
	TMap<FString, TWeakObjectPtr<UObject>> RegisteredProviders;
	FString SessionId;
};
