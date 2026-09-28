#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocOpticalBeamTypes.h"
#include "DocOpticalProfile.generated.h"

/**
 * Surface response. Coefficients are in [0,1]: a surface never creates intensity.
 * Filter channel rule: an empty FilterChannels passes every channel; otherwise only listed channels (tag match) pass.
 */
UCLASS(BlueprintType)
class DOCOPTICALBEAMSRUNTIME_API UDocOpticalProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	FName ProfileId = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	EDocBeamSurfaceType SurfaceType = EDocBeamSurfaceType::Mirror;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Reflectivity = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TransmissionEfficiency = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTagContainer FilterChannels;

	/** Filter only: relabels the transmitted beam's channel. Intensity still only decreases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTag ShiftedOutputChannel;

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult ValidateProfile() const;
};
