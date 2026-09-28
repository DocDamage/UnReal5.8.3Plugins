#pragma once

#include "CoreMinimal.h"
#include "DocPowerNodeComponent.h"
#include "DocPowerStorageComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPOWERNETWORKSRUNTIME_API UDocPowerStorageComponent : public UDocPowerNodeComponent
{
	GENERATED_BODY()

public:
	UDocPowerStorageComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double CapacityJoules = 10000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double CurrentEnergyJoules = 5000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double MaxChargeWatts = 1000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double MaxDischargeWatts = 1000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0001", ClampMax = "1.0"))
	double ChargeEfficiency = 0.9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0001", ClampMax = "1.0"))
	double DischargeEfficiency = 0.9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	EDocPowerStorageMode StorageMode = EDocPowerStorageMode::Auto;
};
