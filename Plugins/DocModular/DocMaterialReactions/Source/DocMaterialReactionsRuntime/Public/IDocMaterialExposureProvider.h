#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocMaterialReactionTypes.h"
#include "IDocMaterialExposureProvider.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UDocMaterialExposureProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCMATERIALREACTIONSRUNTIME_API IDocMaterialExposureProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Exposure")
	TArray<FDocExposureSample> GetExposureSamples(const FVector& Location, float QueryRadius) const;
};
