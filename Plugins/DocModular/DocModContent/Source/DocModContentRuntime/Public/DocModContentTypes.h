#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DocModContentTypes.generated.h"

/** Validation result status code */
UENUM(BlueprintType)
enum class EDocModValidationResult : uint8
{
	Valid = 0,
	MalformedJson,
	InvalidVersion,
	PathTraversal,
	ResourceLimitExceeded,
	ExecutableContentBlocked,
	NamespaceCollision,
	MissingDependency,
	DependencyCycle,
	ConflictDetected,
	ByteHashMismatch,
	ProviderError,
	UnsupportedSchema,
	InUse
};

/** Semantic Versioning representation */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModSemVer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Version")
	int32 Major = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Version")
	int32 Minor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Version")
	int32 Patch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Version")
	FString PreRelease;

	FDocModSemVer() = default;
	FDocModSemVer(int32 InMajor, int32 InMinor, int32 InPatch, const FString& InPreRelease = TEXT(""))
		: Major(InMajor), Minor(InMinor), Patch(InPatch), PreRelease(InPreRelease) {}

	static bool Parse(const FString& InVersionString, FDocModSemVer& OutSemVer);
	FString ToString() const;

	bool operator<(const FDocModSemVer& Other) const;
	bool operator<=(const FDocModSemVer& Other) const;
	bool operator>(const FDocModSemVer& Other) const;
	bool operator>=(const FDocModSemVer& Other) const;
	bool operator==(const FDocModSemVer& Other) const;
	bool operator!=(const FDocModSemVer& Other) const;

	bool MatchesConstraint(const FString& Constraint) const;
};

/** A dependency requirement from one mod to another */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModDependency
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Dependency")
	FString PackId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Dependency")
	FString VersionConstraint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Dependency")
	bool bOptional = false;
};

/** An authored definition entry inside a mod pack */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModDefinitionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Definition")
	FString DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Definition")
	FString Schema;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Definition")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Definition")
	FString RelativePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Definition")
	FString ContentHash;
};

/** Content manifest representing a mod pack */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModManifest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	int32 ManifestVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	FString PackId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	FDocModSemVer PackVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	TArray<FDocModDependency> Requires;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	TArray<FString> Conflicts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	TArray<FDocModDefinitionEntry> Definitions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod|Manifest")
	FString PackDirectoryPath;

	static bool ParseFromJson(const FString& JsonContent, FDocModManifest& OutManifest, FString& OutError);
	FString ToJson() const;
};

/** State snapshot / catalog revision */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModCatalogRevision
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Catalog")
	int64 RevisionNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Catalog")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Catalog")
	TArray<FString> ActivePackIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Catalog")
	TMap<FString, FDocModDefinitionEntry> RegisteredDefinitions;
};

/** Plan representing topological dependency order and diagnostic load order */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModActivationPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Plan")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Plan")
	TArray<FString> OrderedPackIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Plan")
	TArray<FString> LoadOrderExplanations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Plan")
	TMap<FString, FString> ValidatedDefinitionHashes; // DefinitionId -> Expected Hash

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Plan")
	TArray<FString> DiagnosticMessages;
};

/** Pin handle for retaining definitions in-use */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModPinHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Pin")
	FGuid PinId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Pin")
	FString DefinitionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Pin")
	FDateTime AcquiredTime;

	bool IsValid() const { return PinId.IsValid() && !DefinitionId.IsEmpty(); }
};

/** Quarantined record for missing content during save load */
USTRUCT(BlueprintType)
struct DOCMODCONTENTRUNTIME_API FDocModQuarantineRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Quarantine")
	FString PackId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Quarantine")
	FString DefinitionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Quarantine")
	FString PreservedPayloadJson;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mod|Quarantine")
	FString QuarantineReason;
};
