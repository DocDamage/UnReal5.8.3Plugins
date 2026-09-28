#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocPowerNetworkTypes.h"
#include "DocPowerNodeComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPOWERNETWORKSRUNTIME_API UDocPowerNodeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocPowerNodeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	EDocPowerNodeKind NodeKind = EDocPowerNodeKind::Switch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	TArray<FName> PortNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bAutoRegister = true;

	/** On stream unload (RemovedFromWorld / LevelTransition) keep the logical record suspended instead of deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bRetainOnUnload = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	TArray<FDocPowerPortId> GetPortIds() const;
};
