#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocOwnerScope.h"
#include "DocSharedTypes.h"
#include "DocPhotoTypes.generated.h"

UENUM(BlueprintType)
enum class EDocPhotoStorageStatus : uint8
{
	None UMETA(DisplayName = "None"),
	/** Encoded image written and referenced by BlobKey. */
	Available UMETA(DisplayName = "Available"),
	/** Metadata kept, image blob missing (for example deleted outside the game). */
	ImageUnavailable UMETA(DisplayName = "Image Unavailable"),
	/** Legacy value; never set on a committed record. */
	Staged UMETA(DisplayName = "Staged"),
	/** Legacy value; a failed capture never commits a record. */
	Failed UMETA(DisplayName = "Failed")
};

UENUM(BlueprintType)
enum class EDocPhotoCaptureState : uint8
{
	PendingRender UMETA(DisplayName = "Pending Render"),
	PendingReadback UMETA(DisplayName = "Pending Readback"),
	Encoding UMETA(DisplayName = "Encoding"),
	Committing UMETA(DisplayName = "Committing"),
	Committed UMETA(DisplayName = "Committed"),
	Cancelled UMETA(DisplayName = "Cancelled"),
	Failed UMETA(DisplayName = "Failed")
};

/** Capturing an image and satisfying a challenge are separate results. */
UENUM(BlueprintType)
enum class EDocPhotoEvaluationStatus : uint8
{
	NotEvaluated UMETA(DisplayName = "Not Evaluated"),
	Satisfied UMETA(DisplayName = "Satisfied"),
	NotSatisfied UMETA(DisplayName = "Not Satisfied"),
	/** Snapshot and pixels are not coherent (time or pose tolerance exceeded); not scored. */
	Unavailable UMETA(DisplayName = "Unavailable")
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoSubjectObservation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FName SubjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FGameplayTag DefinitionTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 SubjectVersion = 0;

	/** Subject location at the capture snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float DistanceToCamera = 0.0f;

	/** Angle between the camera forward vector and the direction to the subject. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float OffAxisAngleDegrees = 0.0f;

	/** Angle between the subject's forward vector and the direction to the camera (0 = facing the camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float FacingAngleDegrees = 0.0f;

	/** Projected authored bounds clipped to the image, in UV (0..1). Bounds coverage, not pixel coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FBox2D ScreenBounds = FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);

	/** Fraction of the projected (unclipped) bounds area inside the image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float BoundsInFrameFraction = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 SampleCount = 0;

	/** Samples in front of the near plane and inside the image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 InFrameSampleCount = 0;

	/** In-frame samples with a clear line of sight (sampled estimate, not pixel accuracy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 VisibleSampleCount = 0;

	/** VisibleSampleCount / SampleCount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float EstimatedVisibleFraction = 0.0f;

	/** Subject centre inside the image and in front of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bInFrustum = false;

	/** Every bounds corner is behind the near plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bIsBehindCamera = false;

	/** Some corners are behind the near plane: projected bounds are partial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bCrossesNearPlane = false;

	/** Subject moved beyond the pose tolerance between snapshot and pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bPoseStale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	double Timestamp = 0.0;
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoRecord
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FGuid PhotoId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FDocOwnerScope OwnerScope;

	/** World the photo was taken in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FName WorldName = NAME_None;

	/** Snapshot (shutter) time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	double Timestamp = 0.0;

	/** Time the pixels were produced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	double CapturedTimestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int64 SubjectSnapshotRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FVector CameraLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FRotator CameraRotation = FRotator::ZeroRotator;

	/** Width / height of the capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float AspectRatio = 1.777778f;

	/** Horizontal field of view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float FOVDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 ImageWidth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 ImageHeight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FName ImageFormat = NAME_None;

	/** SHA-1 of the stored encoded bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FString ImageHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int64 EncodedBytes = 0;

	/** Key inside the blob store (never an arbitrary path). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FString BlobKey;

	/** Deprecated: retained for older saves; the blob store resolves BlobKey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FString StoragePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	EDocPhotoStorageStatus StorageStatus = EDocPhotoStorageStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	TArray<FDocPhotoSubjectObservation> Observations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 ProfileVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	EDocPhotoEvaluationStatus EvaluationStatus = EDocPhotoEvaluationStatus::NotEvaluated;

	/** 0..1 weighted score of the best qualifying subject. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float EvaluationScore = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bCriteriaMet = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FString EvaluationReason;
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoCaptureRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FDocOwnerScope OwnerScope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FVector CameraLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FRotator CameraRotation = FRotator::ZeroRotator;

	/** Horizontal field of view in degrees, 5..170. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float FOVDegrees = 90.0f;

	/** Width / height. Derived from Width/Height when both are set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float AspectRatio = 1.777778f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 Width = 320;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 Height = 180;

	/** Pixels produced later than this after the snapshot are not scored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float MaxTimeToleranceSeconds = 0.5f;

	/** A subject that moved further than this (cm) between snapshot and pixels is stale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float PoseToleranceCm = 25.0f;
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoGalleryQuota
{
	GENERATED_BODY()

	/** Per owner. A full gallery refuses new captures; nothing is evicted automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 MaxPhotos = 50;

	/** Per owner, encoded bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int64 MaxBytes = 50 * 1024 * 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 MaxConcurrentCaptures = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 MaxImageDimension = 4096;

	/** Subjects considered per capture (nearest first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 MaxSubjectsPerCapture = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 MaxSamplesPerSubject = 16;
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoCaptureStatus
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photography")
	FGuid RequestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photography")
	EDocPhotoCaptureState State = EDocPhotoCaptureState::PendingRender;

	/** Set once committed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photography")
	FGuid PhotoId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photography")
	FString FailureReason;

	/** Image-source ticket (callbacks carry it; late ones are ignored). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photography")
	int64 Ticket = 0;
};

USTRUCT(BlueprintType)
struct DOCPHOTOGRAPHYRUNTIME_API FDocPhotoGallerySnapshot
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FDocOwnerScope OwnerScope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	TArray<FDocPhotoRecord> Records;
};
