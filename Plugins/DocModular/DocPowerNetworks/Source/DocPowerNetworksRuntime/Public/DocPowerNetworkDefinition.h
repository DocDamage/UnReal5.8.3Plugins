#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocPowerNetworkDefinition.generated.h"

UCLASS(BlueprintType)
class DOCPOWERNETWORKSRUNTIME_API UDocPowerNetworkDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	FName NetworkId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	double DefaultStepDurationSeconds = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power", meta = (ClampMin = "0.000001"))
	double EnergyBalanceToleranceJoules = 0.001;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxCatchUpStepsPerTick = 10;
};
