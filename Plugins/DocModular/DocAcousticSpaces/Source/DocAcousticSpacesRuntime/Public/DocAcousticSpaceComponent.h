#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocAcousticProfile.h"
#include "DocAcousticSpaceComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticSpaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocAcousticSpaceComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	FName SpaceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	FBox BoundsBox = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	TObjectPtr<UDocAcousticProfile> AcousticProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	bool bIsExterior = false;

	/** Keep this space's id, bounds and priority in the topology when its level unloads (no component reference is kept). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Space")
	bool bRetainDescriptorOnUnload = false;

	/** Result of the last automatic registration (for example a duplicate SpaceId). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Space")
	FString LastRegistrationError;

	UFUNCTION(BlueprintPure, Category = "Acoustic Space")
	bool ContainsLocation(const FVector& Location) const;

	UFUNCTION(BlueprintPure, Category = "Acoustic Space")
	bool ContainsLocationWithHysteresis(const FVector& Location, float HysteresisMargin) const;
};
