#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "DocPlaytestTypes.generated.h"

UENUM(BlueprintType)
enum class EDocPlaytestMarkerStatus : uint8
{
	Active,
	Finalized,
	Exported,
	Cancelled
};

USTRUCT(BlueprintType)
struct DOCPLAYTESTRECORDERRUNTIME_API FDocPlaytestEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	FGuid EventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	double MonotonicSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	FString Category;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	FString PayloadJson;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	int32 PayloadSizeBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Event")
	bool bIsRedacted = false;
};

USTRUCT(BlueprintType)
struct DOCPLAYTESTRECORDERRUNTIME_API FDocIssueMarker
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	FGuid MarkerId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	double MonotonicSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	FString TesterNote;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	FString Category;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	float PreWindowSeconds = 10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	float PostWindowSeconds = 5.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Marker")
	EDocPlaytestMarkerStatus Status = EDocPlaytestMarkerStatus::Active;
};

USTRUCT(BlueprintType)
struct DOCPLAYTESTRECORDERRUNTIME_API FDocDiagnosticSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Snapshot")
	FGuid SnapshotId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Snapshot")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Snapshot")
	FString ProviderName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Snapshot")
	FString DataJson;
};

USTRUCT(BlueprintType)
struct DOCPLAYTESTRECORDERRUNTIME_API FDocDiagnosticOmission
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Omission")
	FString ProviderName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Omission")
	FString Reason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Omission")
	FDateTime Timestamp;
};

USTRUCT(BlueprintType)
struct DOCPLAYTESTRECORDERRUNTIME_API FDocPlaytestExportManifest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	FString SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	FGuid IssueId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	FString TesterNote;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	FDateTime StartTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	FDateTime EndTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	TMap<FString, int64> ExportedFiles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	TMap<FString, FString> FileHashes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	TArray<FDocDiagnosticOmission> Omissions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	int32 TotalEvents = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Playtest|Manifest")
	int32 EvictedEvents = 0;

	FString ToJson() const;
	static bool ParseFromJson(const FString& JsonContent, FDocPlaytestExportManifest& OutManifest, FString& OutError);
};
