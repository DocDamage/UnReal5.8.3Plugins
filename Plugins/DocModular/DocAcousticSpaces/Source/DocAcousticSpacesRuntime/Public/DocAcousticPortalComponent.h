#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocAcousticTypes.h"
#include "DocAcousticPortalComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnPortalOpennessChanged, FName, PortalId, float, NewOpenness);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnPortalOpennessChangedNative, FName, float);

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticPortalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocAcousticPortalComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal")
	FName PortalId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal")
	FName SpaceA = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal")
	FName SpaceB = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Openness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinTransmissionGain = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxTransmissionGain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MinCutoffHz = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MaxCutoffHz = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal")
	bool bIsEnabled = true;

	/** Keep this portal's descriptor (endpoints, transmission, last openness) in the topology when its level unloads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Portal")
	bool bRetainDescriptorOnUnload = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Portal")
	FString LastRegistrationError;

	UPROPERTY(BlueprintAssignable, Category = "Acoustic Portal")
	FDocOnPortalOpennessChanged OnOpennessChanged;

	FDocOnPortalOpennessChangedNative OnOpennessChangedNative;

	/** Changes openness and invalidates cached paths (topology revision bump). */
	UFUNCTION(BlueprintCallable, Category = "Acoustic Portal")
	void SetOpenness(float InOpenness);

	UFUNCTION(BlueprintCallable, Category = "Acoustic Portal")
	void SetPortalEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Acoustic Portal")
	float GetEffectiveGain() const;

	UFUNCTION(BlueprintPure, Category = "Acoustic Portal")
	float GetEffectiveCutoffHz() const;

	UFUNCTION(BlueprintPure, Category = "Acoustic Portal")
	FVector GetPortalLocation() const;

	FDocAcousticPortalLink ToLink() const;
};
