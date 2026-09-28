#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocPhotoTypes.h"
#include "DocPhotoEvaluationProfile.generated.h"

/** Authored photo challenge. Metrics are geometric estimates (bounds, sampled visibility), not image recognition. */
UCLASS(BlueprintType)
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoEvaluationProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation")
	int32 ProfileVersion = 1;

	/** Invalid tag = any subject. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation")
	FGameplayTag RequiredTag;

	/** Sampled visible fraction (visible samples / all samples). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinVisibleFraction = 0.2f;

	/** Fraction of the projected bounds inside the image. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinBoundsInFrameFraction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0"))
	float MinDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0"))
	float MaxDistance = 5000.0f;

	/** Subject facing: 0 = looking at the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxFacingAngleDegrees = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxOffAxisAngleDegrees = 180.0f;

	/** Clipped screen-bounds area (UV^2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinFramingCoverage = 0.0f;

	/** Score weights; normalized at evaluation. At least one must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0.0"))
	float WeightVisibility = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0.0"))
	float WeightDistance = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0.0"))
	float WeightFacing = 0.3f;

	/** Rejects contradictory ranges, non-finite values and all-zero weights. */
	UFUNCTION(BlueprintCallable, Category = "Evaluation")
	FDocSystemResult ValidateProfile() const;

	/** True when a subject qualifies. OutScore is 0..1 for the best qualifying subject. Stale observations never qualify. */
	UFUNCTION(BlueprintCallable, Category = "Evaluation")
	bool EvaluateObservations(const TArray<FDocPhotoSubjectObservation>& Observations, float& OutScore, FString& OutReason) const;
};
