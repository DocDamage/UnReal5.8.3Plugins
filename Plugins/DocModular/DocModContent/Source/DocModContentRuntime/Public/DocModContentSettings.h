#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DocModContentSettings.generated.h"

/**
 * Global developer settings and resource bounds for DocModContent.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Mod Content"))
class DOCMODCONTENTRUNTIME_API UDocModContentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDocModContentSettings()
	{
		CategoryName = TEXT("DocModular");
		SectionName = TEXT("DocModContent");

		BlockedExtensions = {
			TEXT(".dll"), TEXT(".exe"), TEXT(".so"), TEXT(".dylib"),
			TEXT(".uasset"), TEXT(".umap"), TEXT(".bat"), TEXT(".cmd"),
			TEXT(".sh"), TEXT(".ps1"), TEXT(".vbs"), TEXT(".js")
		};
		AllowedExtensions = { TEXT(".json"), TEXT(".txt"), TEXT(".csv") };
	}

	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int32 MaxFilesPerPack = 500;

	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int64 MaxFileSize = 10 * 1024 * 1024; // 10 MiB

	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int64 MaxAggregatePackBytes = 50 * 1024 * 1024; // 50 MiB

	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int32 MaxDefinitionsPerPack = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int32 MaxStringLength = 2048;

	UPROPERTY(EditAnywhere, Config, Category = "Security")
	TArray<FString> BlockedExtensions;

	/** Data-only allowlist. A non-empty list rejects every other extension (checked after the blocklist). */
	UPROPERTY(EditAnywhere, Config, Category = "Security")
	TArray<FString> AllowedExtensions;

	/** Largest manifest.json accepted before parsing. */
	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int64 MaxManifestBytes = 256 * 1024;

	/** Highest manifest_version this loader understands. */
	UPROPERTY(EditAnywhere, Config, Category = "Quotas")
	int32 MaxSupportedManifestVersion = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	TArray<FString> ConfiguredPackRoots;
};
