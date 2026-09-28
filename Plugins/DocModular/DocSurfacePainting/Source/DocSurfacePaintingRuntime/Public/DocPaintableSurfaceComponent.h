#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "DocSystemResult.h"
#include "DocSurfacePaintingTypes.h"
#include "DocPaintDefinitions.h"
#include "DocPaintableSurfaceComponent.generated.h"

class IDocSurfaceCoordinateProvider;
class UMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceStrokeApplied, FName, SurfaceId, const FDocPaintStroke&, Stroke);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceStrokeAppliedNative, FName, const FDocPaintStroke&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceCoverageUpdated, FName, SurfaceId, const FDocPaintCoverageResult&, Coverage);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceCoverageUpdatedNative, FName, const FDocPaintCoverageResult&);

/** Fires once per transition of bMeetsThreshold, from canonical state (never from render lag). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceCoverageGoalChanged, FName, SurfaceId, const FDocPaintCoverageResult&, Coverage);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnSurfaceCoverageGoalChangedNative, FName, const FDocPaintCoverageResult&);

/**
 * One paintable instance. Owns a canonical CPU mask (checkpoint + ordered stroke journal); the optional texture is a
 * presentation copy tagged with the canonical revision it shows.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCSURFACEPAINTINGRUNTIME_API UDocPaintableSurfaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocPaintableSurfaceComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Instance id (unique per world). Defaults to the owner's name. Two instances of one mesh never share it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Painting")
	FName SurfaceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Painting")
	TObjectPtr<UDocPaintSurfaceDefinition> SurfaceDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Painting")
	FDocSurfaceMaskState MaskState;

	/** Strokes applied since the last checkpoint, in authoritative order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Painting")
	TArray<FDocPaintStroke> StrokeJournal;

	/** Deprecated alias kept for tools: journal length before compaction when no definition limit applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Painting")
	int32 MaxHistoryStrokes = 256;

	// Presentation (optional; headless and dedicated servers skip it) ------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	bool bCreateRenderTexture = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	TObjectPtr<UMeshComponent> TargetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	int32 MaterialSlot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	FName MaskTextureParameter = TEXT("PaintMask");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Painting")
	FString LastRegistrationError;

	// Events ------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Surface Painting")
	FDocOnSurfaceStrokeApplied OnStrokeApplied;
	FDocOnSurfaceStrokeAppliedNative OnStrokeAppliedNative;

	UPROPERTY(BlueprintAssignable, Category = "Surface Painting")
	FDocOnSurfaceCoverageUpdated OnCoverageUpdated;
	FDocOnSurfaceCoverageUpdatedNative OnCoverageUpdatedNative;

	UPROPERTY(BlueprintAssignable, Category = "Surface Painting")
	FDocOnSurfaceCoverageGoalChanged OnCoverageGoalChanged;
	FDocOnSurfaceCoverageGoalChangedNative OnCoverageGoalChangedNative;

	// API ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult InitializeCanvas(const UDocPaintSurfaceDefinition* InDef);

	/**
	 * Validates everything before touching the mask: mapping, stroke id (required), duplicates (NoChange), point
	 * count/finite/range, radius/strength, sequence order, resampled work. Then rasterizes and commits one revision.
	 */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult ApplyStroke(const FDocPaintStroke& Stroke);

	/** Builds a single-dab stroke from a world hit through a coordinate provider. Missing UVs never paint (0,0). */
	FDocSystemResult PaintAtHit(const FHitResult& Hit, const IDocSurfaceCoordinateProvider& Provider, EDocPaintOperation Operation,
		float RadiusUV, float Strength, FName OwnerId, FDocPaintStroke& OutStroke);

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	EDocPaintMappingStatus GetMappingStatus() const;

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	FDocPaintCoverageResult QueryCoverage() const;

	/** Ticket for a presentation upload of the current canonical revision, plus the dirty rectangle since the last upload. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	int64 CreateRenderUploadTicket(FIntRect& OutDirtyRect) const;

	/** Accepts only uploads newer than the last one and not ahead of canonical state. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult CommitRenderUpload(int64 UploadedRevision);

	/** Raw mask copy (tools). */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult CreateCheckpoint(FDocSurfaceMaskState& OutCheckpoint) const;

	/** Raw mask restore; validated against this instance's definition (dimensions, fingerprint). Eligibility stays authored. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult RestoreCheckpoint(const FDocSurfaceMaskState& InCheckpoint);

	/** Persistent form: compressed checkpoint + residual journal + mask hash. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult CaptureSnapshot(FDocPaintSurfaceSnapshot& OutSnapshot) const;

	/** Validates sizes before allocation, replays the journal on a scratch copy, verifies the hash, then commits. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult RestoreSnapshot(const FDocPaintSurfaceSnapshot& Snapshot);

	/** Local-undo profile only: removes the newest journal stroke if it belongs to OwnerId. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult UndoLastStroke(FName OwnerId);

	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	void SetEligibleTexelsMask(const TArray<bool>& InEligible);

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	uint8 GetTexelValue(int32 X, int32 Y) const;

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	int32 GetCheckpointStrokeIdCount() const { return CheckpointStrokeIds.Num(); }

	static constexpr int32 MaxRememberedStrokeIds = 4096;

private:
	FDocSystemResult ValidateStroke(const FDocPaintStroke& Stroke, TArray<FVector2D>& OutDabs, int64& OutSequence) const;
	static void Rasterize(TArray<uint8>& Mask, int32 Width, int32 Height, const TArray<FVector2D>& Dabs, float Radius, float Strength, EDocPaintOperation Op, FIntRect& InOutDirty);
	bool ReplayInto(TArray<uint8>& Mask, const TArray<FDocPaintStroke>& Strokes, FIntRect& InOutDirty) const;
	void CompactJournalIfNeeded();
	void BroadcastCoverage();
	void UpdatePresentation();
	int32 JournalLimit() const;

	/** Mask at the last checkpoint. */
	TArray<uint8> CheckpointMask;
	int64 CheckpointSequence = 0;
	TArray<FGuid> CheckpointStrokeIds;
	TSet<FGuid> AppliedStrokeIds;
	FIntRect DirtySinceUpload;
	bool bHasDirty = false;
	bool bLastGoalMet = false;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RenderTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RenderMaterial;
};
