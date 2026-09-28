#pragma once

#include "CoreMinimal.h"
#include "DocSurfacePaintingTypes.generated.h"

/**
 * Mask polarity: each texel stores "amount of layer" in 0..255 (paint, dirt, cover).
 *   Paint:  m = max(m, a)
 *   Clean:  m = min(m, 255 - a)
 *   Reveal: same monotonic rule as Clean (removes a cover layer).
 * a = round(255 * Strength * (1 - d / R)) for texel-centre distance d <= R (round half away from zero).
 */
UENUM(BlueprintType)
enum class EDocPaintOperation : uint8
{
	Paint UMETA(DisplayName = "Paint"),
	Clean UMETA(DisplayName = "Clean"),
	Reveal UMETA(DisplayName = "Reveal")
};

UENUM(BlueprintType)
enum class EDocPaintMappingStatus : uint8
{
	Valid UMETA(DisplayName = "Valid"),
	/** The coordinate provider cannot supply UVs (for example collision UV support is off). */
	Unsupported UMETA(DisplayName = "Unsupported"),
	/** The authored UVs overlap/mirror and sharing paint was not accepted. */
	OverlappingUVs UMETA(DisplayName = "Overlapping UVs"),
	/** No UV for this hit, or a UV outside the declared address mode. Nothing is written. */
	InvalidMapping UMETA(DisplayName = "Invalid Mapping"),
	/** Saved state was made for a different mesh/UV fingerprint. */
	IncompatibleSurface UMETA(DisplayName = "Incompatible Surface")
};

/** What happens to UVs outside [0,1]. */
UENUM(BlueprintType)
enum class EDocPaintUVAddressMode : uint8
{
	/** Points outside [0,1] are refused (InvalidMapping). */
	Reject UMETA(DisplayName = "Reject"),
	/** Points wrap into [0,1) (tiling UVs). Brush footprints do not wrap across the edge. */
	Wrap UMETA(DisplayName = "Wrap")
};

/** How coverage counts a texel. */
UENUM(BlueprintType)
enum class EDocPaintCoverageMode : uint8
{
	/** Covered when mask >= CoverageThreshold (painting goals). */
	AtOrAboveThreshold UMETA(DisplayName = "At Or Above Threshold"),
	/** Covered when mask < CoverageThreshold (cleaning/reveal goals). */
	BelowThreshold UMETA(DisplayName = "Below Threshold")
};

USTRUCT(BlueprintType)
struct DOCSURFACEPAINTINGRUNTIME_API FDocPaintStroke
{
	GENERATED_BODY()

	/** Required. A stroke id is applied at most once per surface (dedup survives save/restore). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	FGuid StrokeId;

	/** Instance id of the target surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	FName SurfaceId = NAME_None;

	/** Who made the stroke; used by the local-undo profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	FName OwnerId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	EDocPaintOperation Operation = EDocPaintOperation::Paint;

	/** Surface-local UVs. Consecutive points closer than MaxBridgeGapUV are joined by bounded resampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	TArray<FVector2D> Points;

	/** Brush radius in UV units (not world meters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float RadiusUV = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	/** Authoritative order. Must be greater than the surface's last accepted sequence (0 = assign next). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int64 SequenceNumber = 0;

	/** Brush definition version the client used; informational. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 BrushVersion = 0;
};

/** Canonical CPU mask. The GPU texture is only a presentation copy. */
USTRUCT(BlueprintType)
struct DOCSURFACEPAINTINGRUNTIME_API FDocSurfaceMaskState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	FName SurfaceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int32 Width = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int32 Height = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int64 CanonicalRevision = 0;

	/** Highest canonical revision the presentation texture has received. Never ahead of CanonicalRevision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int64 RenderRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	FString MeshFingerprint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	TArray<uint8> RawMaskBytes;

	/** From the definition (authored eligibility). Not taken from saves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	TArray<bool> EligibleTexels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int64 LastSequence = 0;
};

USTRUCT(BlueprintType)
struct DOCSURFACEPAINTINGRUNTIME_API FDocPaintCoverageResult
{
	GENERATED_BODY()

	/** Denominator: authored eligible texels. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int32 EligibleTexelCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int32 CoveredTexelCount = 0;

	/** Covered / eligible; 0 when there are no eligible texels. Texel coverage, not physical area. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	float CoverageRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	uint8 Threshold = 128;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	EDocPaintCoverageMode Mode = EDocPaintCoverageMode::AtOrAboveThreshold;

	/** Target ratio from the definition. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	float RequiredRatio = 1.0f;

	/** Smallest representable ratio step (1 / eligible). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	float Precision = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	int64 CanonicalRevision = 0;

	/** Never true with zero eligible texels. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paint")
	bool bMeetsThreshold = false;
};

/** Saved surface: checkpoint mask (compressed) + ordered residual journal, verified by hash. */
USTRUCT(BlueprintType)
struct DOCSURFACEPAINTINGRUNTIME_API FDocPaintSurfaceSnapshot
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	FName SurfaceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	FString MeshFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 Width = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 Height = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 RasterizerVersion = 1;

	/** zlib-compressed checkpoint mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	TArray<uint8> CompressedCheckpoint;

	/** Declared decompressed size; validated against Width*Height and limits before allocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int32 UncompressedSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int64 CheckpointSequence = 0;

	/** Strokes after the checkpoint, in authoritative order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	TArray<FDocPaintStroke> Journal;

	/** Ids of strokes folded into the checkpoint (bounded), so they are not re-applied after load. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	TArray<FGuid> CheckpointStrokeIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int64 CanonicalRevision = 0;

	/** CRC32 of the full canonical mask at capture; restore verifies replay reproduces it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	int64 MaskHash = 0;
};
