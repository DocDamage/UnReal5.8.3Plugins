#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocSystemResult.h"
#include "DocSurfacePaintingTypes.h"
#include "DocPaintDefinitions.generated.h"

/** Shared authored contract for a prepared mesh. Each component instance owns its own mask state. */
UCLASS(BlueprintType)
class DOCSURFACEPAINTINGRUNTIME_API UDocPaintSurfaceDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxMaskDimension = 1024;
	static constexpr int32 MaxTotalTexels = 1024 * 1024;
	static constexpr int32 RasterizerVersion = 1;

	/** Definition id (the asset's identity). Instances are identified by the component's SurfaceId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface")
	FName SurfaceId = NAME_None;

	/** Mesh + UV layout fingerprint. Saves made for another fingerprint are refused (IncompatibleSurface). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface")
	FString MeshFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface", meta = (ClampMin = "8", ClampMax = "1024"))
	int32 Width = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface", meta = (ClampMin = "8", ClampMax = "1024"))
	int32 Height = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface")
	uint8 InitialFillValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Surface", meta = (ClampMin = "0", ClampMax = "7"))
	int32 UVChannel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	EDocPaintUVAddressMode AddressMode = EDocPaintUVAddressMode::Reject;

	/** Authored: this mesh's UVs overlap or mirror. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	bool bHasOverlappingUVs = false;

	/** Explicit acceptance that overlapping regions share paint. Without it, painting is refused (OverlappingUVs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	bool bAcceptSharedOverlappingUVs = false;

	/** Consecutive stroke points further apart than this are not bridged (seams, jumps between UV islands). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxBridgeGapUV = 0.1f;

	/** Optional authored eligible-texel mask (non-zero = eligible), Width*Height bytes. Empty = all texels eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
	TArray<uint8> EligibleTexelMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
	uint8 CoverageThreshold = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
	EDocPaintCoverageMode CoverageMode = EDocPaintCoverageMode::AtOrAboveThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RequiredCoverageRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxStrokePoints = 1000;

	/** Upper bound on dabs after resampling one stroke; larger strokes are refused before any work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxResampledDabs = 4096;

	/** Journal length before it is folded into a new checkpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxJournalStrokes = 256;

	/** Local-session undo profile (single owner). Multiplayer collaborative undo is not supported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits")
	bool bAllowLocalUndo = false;

	UFUNCTION(BlueprintCallable, Category = "Paint Surface")
	FDocSystemResult ValidateDefinition() const;

	EDocPaintMappingStatus GetMappingStatus() const;
};

UCLASS(BlueprintType)
class DOCSURFACEPAINTINGRUNTIME_API UDocPaintBrushDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Brush")
	FName BrushId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Brush")
	int32 BrushVersion = 1;

	/** Radius in UV units. World-meter radii are not supported by the base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Brush", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float DefaultRadiusUV = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint Brush")
	EDocPaintOperation DefaultOperation = EDocPaintOperation::Paint;

	UFUNCTION(BlueprintCallable, Category = "Paint Brush")
	FDocSystemResult ValidateDefinition() const;
};
