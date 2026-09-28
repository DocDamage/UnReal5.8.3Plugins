#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DocPlaytestSettings.generated.h"

/**
 * Developer settings for diagnostic playtest recording bounds and privacy redaction.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Playtest Recorder"))
class DOCPLAYTESTRECORDERRUNTIME_API UDocPlaytestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDocPlaytestSettings()
	{
		CategoryName = TEXT("DocModular");
		SectionName = TEXT("DocPlaytestRecorder");

		RedactedKeywords = {
			TEXT("password"), TEXT("token"), TEXT("secret"),
			TEXT("auth"), TEXT("credential"), TEXT("private_key")
		};
	}

	UPROPERTY(EditAnywhere, Config, Category = "RingBuffer")
	int32 MaxEventCount = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "RingBuffer")
	float MaxRetentionWindowSeconds = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "RingBuffer")
	int64 MaxBufferSizeBytes = 5 * 1024 * 1024; // 5 MiB

	UPROPERTY(EditAnywhere, Config, Category = "Privacy")
	TArray<FString> RedactedKeywords;

	/** Largest single payload retained (bytes); larger payloads are replaced by a truncation marker. */
	UPROPERTY(EditAnywhere, Config, Category = "RingBuffer")
	int64 MaxEventPayloadBytes = 64 * 1024;

	UPROPERTY(EditAnywhere, Config, Category = "Export")
	FString DefaultExportDirectory;
};
