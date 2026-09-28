#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocSystemResult.h"
#include "DocGestureTypes.h"
#include "DocGestureTemplate.generated.h"

/**
 * An authored single-stroke template. Treated as immutable data: the subsystem compiles a private normalized
 * snapshot keyed by ComputeContentHash() + algorithm version, so editing the asset later never changes a
 * registered model or an in-flight request.
 */
UCLASS(BlueprintType)
class DOCGESTURERECOGNITIONRUNTIME_API UDocGestureTemplate : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FName GestureId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FText DisplayName;

	/** Canonical stroke in drawing-plane units, in the direction it should be drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	TArray<FVector2D> CanonicalPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture", meta = (ClampMin = "8", ClampMax = "256"))
	int32 ResampleCount = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	EDocGestureScalePolicy ScalePolicy = EDocGestureScalePolicy::UniformPreserveAspect;

	/** Align by indicative angle and search +/- RotationSearchDegrees. Leave off for directional symbols. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bRotationInvariant = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture", meta = (ClampMin = "0", ClampMax = "180"))
	float RotationSearchDegrees = 45.0f;

	/** Accept the stroke drawn in reverse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bDirectionInvariant = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture", meta = (ClampMin = "0", ClampMax = "1"))
	float MinSimilarityThreshold = 0.70f;

	/** Required similarity lead over the runner-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture", meta = (ClampMin = "0", ClampMax = "1"))
	float MinRunnerUpMargin = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float MinPathLength = 10.0f;

	/** The semantic intent may be submitted through the non-drawing alternative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bAllowAccessibleAlternative = true;

	FDocSystemResult ValidateTemplate() const;
	FString ComputeContentHash() const;

	// Normalization helpers (pure functions)
	static bool ResamplePoints(const TArray<FVector2D>& InPoints, int32 TargetCount, TArray<FVector2D>& OutPoints);
	static FVector2D ComputeCentroid(const TArray<FVector2D>& InPoints);
	static void TranslateToOrigin(TArray<FVector2D>& InOutPoints);
	static void ScalePoints(TArray<FVector2D>& InOutPoints, EDocGestureScalePolicy Policy, float TargetBoxSize = 1.0f);
	static void RotatePoints(TArray<FVector2D>& InOutPoints, float AngleRadians);
	static float ComputeIndicativeAngle(const TArray<FVector2D>& InPoints);
	static float ComputeAveragePointDistance(const TArray<FVector2D>& PointsA, const TArray<FVector2D>& PointsB);
};

UCLASS(BlueprintType)
class DOCGESTURERECOGNITIONRUNTIME_API UDocGestureTemplateSet : public UDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxTemplates = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FName SetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	TArray<TObjectPtr<UDocGestureTemplate>> Templates;

	void AddTemplate(UDocGestureTemplate* Template);
	UDocGestureTemplate* FindTemplate(FName InGestureId) const;
	void IncrementVersion() { ++Version; }

	FDocSystemResult ValidateSet() const;

	/** Authoring aid: template pairs that recognize each other under their own invariance settings. */
	TArray<FString> FindCollisions() const;
};

/** Normalized, immutable copy of a template used for recognition. */
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureCompiledTemplate
{
	FName GestureId;
	FText DisplayName;
	FString ContentHash;
	TArray<FVector2D> Points;
	int32 ResampleCount = 32;
	EDocGestureScalePolicy ScalePolicy = EDocGestureScalePolicy::UniformPreserveAspect;
	bool bRotationInvariant = false;
	float RotationSearchDegrees = 45.0f;
	bool bDirectionInvariant = false;
	float MinSimilarityThreshold = 0.7f;
	float MinRunnerUpMargin = 0.1f;
	bool bAllowAccessibleAlternative = true;
};

/** The deterministic recognition pipeline. */
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureRecognizer
{
	static constexpr int32 AlgorithmVersion = 2;
	static constexpr int32 RotationIterations = 12;

	static bool Compile(const UDocGestureTemplate& Template, FDocGestureCompiledTemplate& Out);

	/** Normalizes raw points for comparison against a template (optionally reversed). */
	static bool Normalize(const TArray<FVector2D>& RawPoints, const FDocGestureCompiledTemplate& Settings, bool bReverse, TArray<FVector2D>& Out);

	/** Best mean distance of a stroke to a template (reversal/rotation per template policy). Adds evaluations to InOutWork. */
	static bool Score(const TArray<FVector2D>& RawPoints, const FDocGestureCompiledTemplate& Template, FDocGestureCandidate& OutCandidate, int32& InOutWork);

	static float SimilarityFromDistance(float Distance);
};
