#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocTerminalTypes.h"
#include "DocTerminalDefinition.generated.h"

UCLASS(BlueprintType)
class DOCWORLDTERMINALSRUNTIME_API UDocTerminalDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	FName DeviceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	FText DeviceName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	EDocTerminalExclusivity DefaultExclusivity = EDocTerminalExclusivity::SingleWriter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	TArray<FDocVirtualFile> InitialFiles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	TMap<FString, FString> InitialSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	TArray<FName> SupportedApplicationIds;
};
