#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocMechanicalTypes.h"
#include "DocMechanicalComponents.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCMECHANICALNETWORKSRUNTIME_API UDocMechanicalNodeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocMechanicalNodeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float AppliedLoadTorque = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	EDocMechanicalDetachedPolicy DetachedPolicy = EDocMechanicalDetachedPolicy::HoldPhase;

	/** Absolute speed limit in rad/s (0 = unlimited). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float MaxOperatingSpeed = 0.0f;

	UFUNCTION(BlueprintPure, Category = "Mechanical")
	FDocMechanicalNodeState GetState() const;
};

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCMECHANICALNETWORKSRUNTIME_API UDocDriveSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocDriveSourceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName SourceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName AttachedNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float RequestedSpeed = 10.0f; // rad/s

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	float TorqueCapacity = 100.0f; // Nm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsEnabled = true;

	UFUNCTION(BlueprintCallable, Category = "Mechanical")
	void SetRequestedSpeed(float InSpeed);

	UFUNCTION(BlueprintCallable, Category = "Mechanical")
	void SetTorqueCapacity(float InCapacity);
};

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCMECHANICALNETWORKSRUNTIME_API UDocClutchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocClutchComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	FName EdgeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mechanical")
	bool bIsEngaged = true;

	UFUNCTION(BlueprintCallable, Category = "Mechanical")
	void SetEngaged(bool bInEngaged);
};
