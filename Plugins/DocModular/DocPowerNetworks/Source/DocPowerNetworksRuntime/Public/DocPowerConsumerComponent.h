#pragma once

#include "CoreMinimal.h"
#include "DocPowerNodeComponent.h"
#include "DocPowerConsumerComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPOWERNETWORKSRUNTIME_API UDocPowerConsumerComponent : public UDocPowerNodeComponent
{
	GENERATED_BODY()

public:
	UDocPowerConsumerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double DesiredPowerWatts = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	double MinimumPowerWatts = 50.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	int32 TieBreakId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	EDocPowerAllocationMode AllocationMode = EDocPowerAllocationMode::Binary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	EDocPowerConsumerState CurrentState = EDocPowerConsumerState::Disconnected;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	double DeliveredWatts = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double RecoveryDropoutHysteresis = 0.05;

	UPROPERTY(BlueprintAssignable, Category = "Power|Events")
	FDocOnPowerSupplyChanged OnPowerSupplyChanged;

	FDocOnPowerSupplyChangedNative OnPowerSupplyChangedNative;

	void UpdateSupply(double InDeliveredWatts, EDocPowerConsumerState InState);
};
