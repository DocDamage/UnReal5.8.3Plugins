#pragma once

#include "CoreMinimal.h"
#include "DocOwnerScope.h"
#include "DocGestureTypes.generated.h"

UENUM(BlueprintType)
enum class EDocGestureRecognitionStatus : uint8
{
	Recognized UMETA(DisplayName = "Recognized"),
	NotRecognized UMETA(DisplayName = "Not Recognized"),
	Ambiguous UMETA(DisplayName = "Ambiguous"),
	InvalidStroke UMETA(DisplayName = "Invalid Stroke"),
	Cancelled UMETA(DisplayName = "Cancelled")
};

/** Uniform keeps the aspect ratio (default). Non-uniform stretching is opt-in; near-1D strokes fall back to uniform. */
UENUM(BlueprintType)
enum class EDocGestureScalePolicy : uint8
{
	UniformPreserveAspect UMETA(DisplayName = "Uniform Preserve Aspect"),
	NonUniformFitBox UMETA(DisplayName = "Non-Uniform Fit Box")
};

UENUM(BlueprintType)
enum class EDocGestureInputSource : uint8
{
	Mouse UMETA(DisplayName = "Mouse"),
	Touch UMETA(DisplayName = "Touch"),
	AnalogStick UMETA(DisplayName = "Analog Stick"),
	AccessibleAlternative UMETA(DisplayName = "Accessible Alternative")
};

/** What happens when a captured point falls outside the drawing surface. */
UENUM(BlueprintType)
enum class EDocGesturePointerLeavePolicy : uint8
{
	Clamp,
	EndStroke,
	Cancel
};

/**
 * Local drawing plane. Adapters convert device coordinates into this plane (origin top-left, +X right, +Y down,
 * units = the surface's own layout units after DPI scaling) before appending points.
 */
USTRUCT(BlueprintType)
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureCaptureSettings
{
	GENERATED_BODY()

	/** Surface size in plane units; zero means unbounded (no leave handling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FVector2D SurfaceSize = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	EDocGesturePointerLeavePolicy LeavePolicy = EDocGesturePointerLeavePolicy::Clamp;

	/** Analog-stick integration: plane units per second at full deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float AnalogSpeedUnitsPerSecond = 300.0f;

	/** Stick deflection below this magnitude is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float AnalogDeadZone = 0.15f;
};

USTRUCT(BlueprintType)
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureCandidate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FName GestureId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FText DisplayName;

	/** Bounded similarity in [0, 1] derived from mean point distance. Not a probability of intent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float Similarity = 0.0f;

	/** Mean normalized point distance (lower is closer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float Distance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bMatchedReversed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float MatchedRotationDegrees = 0.0f;
};

USTRUCT(BlueprintType)
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FDocOwnerScope Owner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 Generation = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	EDocGestureInputSource InputSource = EDocGestureInputSource::Mouse;

	/** Points in the local drawing plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	TArray<FVector2D> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float Duration = 0.0f;

	/** True when samples are integrated/coarse (analog stick) rather than direct pointer positions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bCoarseSampling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bIsComplete = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	bool bIsCancelled = false;

	float CalculatePathLength() const;
	bool HasNonFinitePoints() const;
	bool IsDegenerate(float MinLength = 10.0f) const;
};

USTRUCT(BlueprintType)
struct DOCGESTURERECOGNITIONRUNTIME_API FDocGestureRecognitionResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FDocOwnerScope Owner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	EDocGestureRecognitionStatus Status = EDocGestureRecognitionStatus::NotRecognized;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FName MatchedGestureId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float Similarity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	float RunnerUpMargin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	TArray<FDocGestureCandidate> Candidates;

	/** Template set version of the model that produced this result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 TemplateVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FName TemplateSetId = NAME_None;

	/** Hash of the compiled model (templates + algorithm version) used for this result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FString ModelHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 AlgorithmVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 RequestGeneration = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	EDocGestureInputSource InputSource = EDocGestureInputSource::Mouse;

	/** Distance evaluations spent (bounded by template count, direction variants and rotation iterations). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	int32 WorkUnits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gesture")
	FString FailureReason;
};
