#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocTerminalTypes.h"
#include "DocTerminalDefinition.h"
#include "DocTerminalComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCWORLDTERMINALSRUNTIME_API UDocTerminalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocTerminalComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName DeviceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TObjectPtr<UDocTerminalDefinition> Definition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bHasPower = true;

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	void SetPowerState(bool bInHasPower);
};
