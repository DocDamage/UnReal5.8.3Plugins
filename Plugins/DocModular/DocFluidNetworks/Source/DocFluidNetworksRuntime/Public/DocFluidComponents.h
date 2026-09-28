#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocFluidTypes.h"
#include "DocFluidComponents.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCFLUIDNETWORKSRUNTIME_API UDocFluidReservoirComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocFluidReservoirComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName ReservoirId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName FluidDefinitionId = FName("Water");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float Capacity = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float CurrentVolume = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float LeakRate = 0.0f; // Liters per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float ExternalSupplyRate = 0.0f; // Liters per second

	UFUNCTION(BlueprintPure, Category = "Fluid")
	FDocFluidReservoirState GetState() const;
};

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCFLUIDNETWORKSRUNTIME_API UDocFluidValveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocFluidValveComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float ValveOpening = 1.0f; // [0.0, 1.0]

	UFUNCTION(BlueprintCallable, Category = "Fluid")
	bool SetOpening(float InOpening);
};

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCFLUIDNETWORKSRUNTIME_API UDocFluidPumpComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocFluidPumpComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bPumpEnabled = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fluid")
	EDocFluidPumpStatus Status = EDocFluidPumpStatus::Enabled;

	UFUNCTION(BlueprintCallable, Category = "Fluid")
	void SetPumpEnabled(bool bEnabled);
};
