#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "DocSystemResult.h"
#include "DocPhotoTypes.h"
#include "DocPhotoEvaluationProfile.h"
#include "DocPhotographableComponent.h"
#include "DocPhotoProviders.h"
#include "DocPhotographySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocPhotoCapturedDynamicDelegate, const FDocPhotoRecord&, Record);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocPhotoCapturedNativeDelegate, const FDocPhotoRecord&);

/**
 * Per-local-player photography sessions and gallery.
 *
 * Pipeline: validate → snapshot camera + subjects → image source renders (PendingRender/PendingReadback)
 * → SubmitPixels (Encoding) → PNG encode + hash → coherence check + evaluation → write blob (Committing)
 * → commit record (Committed) → notify. A record is committed only after its image is stored.
 */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotographySubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UDocPhotographySubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual TStatId GetStatId() const override;

	// Providers ---------------------------------------------------------------------------------

	/** Default: scene capture (needs a real RHI). */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	void SetImageSource(UDocPhotoImageSource* Source);

	/** Default: file store under Saved/DocPhotography. */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	void SetBlobStore(UDocPhotoBlobStore* Store);

	UDocPhotoImageSource* GetImageSource();
	UDocPhotoBlobStore* GetBlobStore();

	/** Line-of-sight test used for sampled visibility (default: Visibility-channel trace ignoring the subject's actor). */
	void SetVisibilityTester(TFunction<bool(UWorld*, const FVector& From, const FVector& To, const AActor* Subject)> Tester);

	// Capture -----------------------------------------------------------------------------------

	/** Starts a capture. The world is explicit (the subsystem retains only weak references). */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocSystemResult RequestPhoto(UWorld* World, const FDocPhotoCaptureRequest& Request, UDocPhotoEvaluationProfile* Profile, FGuid& OutRequestId);

	/** Called by image sources. Returns false (ignored) for unknown, cancelled or finished tickets. */
	bool SubmitPixels(int64 Ticket, int32 Width, int32 Height, const TArray<FColor>& Pixels, double CapturedTimeSeconds);

	bool FailCapture(int64 Ticket, const FString& Reason);

	/** Advances the pipeline (also called by Tick). */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	void Pump();

	/** Conflict once committed: the image exists and is not "un-taken". */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocSystemResult CancelCapture(const FGuid& RequestId);

	UFUNCTION(BlueprintCallable, Category = "Photography")
	bool QueryCapture(const FGuid& RequestId, FDocPhotoCaptureStatus& OutStatus) const;

	UFUNCTION(BlueprintPure, Category = "Photography")
	int32 GetActiveCaptureCount() const;

	// Gallery (owner-checked) ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Photography")
	bool QueryPhoto(const FDocOwnerScope& Owner, const FGuid& PhotoId, FDocPhotoRecord& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "Photography")
	TArray<FDocPhotoRecord> ListPhotos(const FDocOwnerScope& ForOwner) const;

	/** Reads and verifies the stored image. A missing blob marks the record ImageUnavailable (metadata kept). */
	FDocSystemResult LoadPhotoImage(const FDocOwnerScope& Owner, const FGuid& PhotoId, TArray64<uint8>& OutEncoded);

	/** Re-evaluates stored metadata against a profile (no pixels involved; stale subjects never qualify). */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocSystemResult EvaluatePhotoRecord(const FDocOwnerScope& Owner, const FGuid& PhotoId, UDocPhotoEvaluationProfile* Profile, FDocPhotoRecord& OutRecord) const;

	/** Explicit user deletion: removes the record and its manifest-owned blob. */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocSystemResult DeletePhoto(const FDocOwnerScope& Owner, const FGuid& PhotoId);

	UFUNCTION(BlueprintCallable, Category = "Photography")
	void SetGalleryQuota(const FDocPhotoGalleryQuota& NewQuota);

	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocPhotoGalleryQuota GetGalleryQuota() const { return GalleryQuota; }

	// Persistence ------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocPhotoGallerySnapshot CaptureGalleryMetadata(const FDocOwnerScope& Owner) const;

	/** Validates, then replaces that owner's records. Missing blobs become ImageUnavailable; other records stay valid. */
	UFUNCTION(BlueprintCallable, Category = "Photography")
	FDocSystemResult StageRestore(const FDocPhotoGallerySnapshot& Snapshot);

	// Math ---------------------------------------------------------------------------------------

	/**
	 * Projects with the capture camera. FOV is horizontal; Aspect = width / height. UV (0,0) is top-left.
	 * Points at or behind the near plane (1 cm) report bOutBehindCamera.
	 */
	static void ProjectWorldPointToScreenUV(const FVector& WorldPoint, const FVector& CameraLocation,
		const FRotator& CameraRotation, float FOVDegrees, float AspectRatio, FVector2D& OutScreenUV,
		bool& bOutInFrustum, bool& bOutBehindCamera);

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FDocPhotoCapturedDynamicDelegate OnPhotoCaptured;
	FDocPhotoCapturedNativeDelegate OnPhotoCapturedNative;

private:
	struct FActiveCapture
	{
		FGuid RequestId;
		int64 Ticket = 0;
		FDocPhotoCaptureRequest Request;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDocPhotoEvaluationProfile> Profile;
		EDocPhotoCaptureState State = EDocPhotoCaptureState::PendingRender;
		FString FailureReason;
		FName WorldName;
		double RequestedTime = 0.0;
		int64 SnapshotRevision = 0;
		TArray<FDocPhotoSubjectObservation> Observations;
		TArray<TWeakObjectPtr<UDocPhotographableComponent>> SubjectRefs;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
		double CapturedTime = 0.0;
		FGuid PhotoId;
		bool bWorldLost = false;
	};

	static bool IsTerminal(EDocPhotoCaptureState State);
	static bool IsWorldAlive(const FActiveCapture& Capture);
	FActiveCapture* FindByTicket(int64 Ticket);
	void SnapshotSubjects(UWorld& World, FActiveCapture& Capture) const;
	void Finish(FActiveCapture& Capture, EDocPhotoCaptureState State, const FString& Reason);
	void EncodeAndCommit(FActiveCapture& Capture);
	int32 CountOwnerPhotos(const FDocOwnerScope& Owner) const;
	int64 CountOwnerBytes(const FDocOwnerScope& Owner) const;
	void PruneFinished();
	void EnsureWorldDelegates();
	void HandleWorldGone(UWorld* World);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	FDelegateHandle WorldTearDownHandle;
	FDelegateHandle WorldCleanupHandle;
	static FString OwnerKey(const FDocOwnerScope& Owner);

	UPROPERTY(Transient)
	TObjectPtr<UDocPhotoImageSource> ImageSource;

	UPROPERTY(Transient)
	TObjectPtr<UDocPhotoBlobStore> BlobStore;

	UPROPERTY()
	TMap<FGuid, FDocPhotoRecord> GalleryRecords;

	UPROPERTY()
	FDocPhotoGalleryQuota GalleryQuota;

	TMap<FGuid, FActiveCapture> Captures;
	TArray<FGuid> CaptureOrder;
	int64 NextTicket = 1;
	TFunction<bool(UWorld*, const FVector&, const FVector&, const AActor*)> VisibilityTester;
};
