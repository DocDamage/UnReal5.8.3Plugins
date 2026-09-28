#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocSystemResult.h"
#include "DocAcousticProfile.generated.h"

UCLASS(BlueprintType)
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Profile")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Profile", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReverbSendLevel = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Profile", meta = (ClampMin = "0.01", ClampMax = "20.0"))
	float DecayTimeSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Profile", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Damping = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Profile", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float HighFrequencyCutoffHz = 20000.0f;

	UFUNCTION(BlueprintCallable, Category = "Acoustic Profile")
	FDocSystemResult ValidateProfile() const;
};
