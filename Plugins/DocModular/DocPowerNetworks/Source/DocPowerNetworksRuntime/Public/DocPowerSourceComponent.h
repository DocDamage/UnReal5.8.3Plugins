#pragma once

#include "CoreMinimal.h"
#include "DocPowerNodeComponent.h"
#include "DocPowerSourceComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPOWERNETWORKSRUNTIME_API UDocPowerSourceComponent : public UDocPowerNodeComponent
{
	GENERATED_BODY()

public:
	UDocPowerSourceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double MaxPowerWatts = 1000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bExternal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double Availability = 1.0;
};
