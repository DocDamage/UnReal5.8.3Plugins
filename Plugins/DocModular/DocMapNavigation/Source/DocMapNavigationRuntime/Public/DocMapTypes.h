#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DocOwnerScope.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocMapTypes.generated.h"

class UTexture2D;

namespace DocMapTags
{
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Objective);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Location);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Actor);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Waypoint);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_FastTravel);
	DOCMAPNAVIGATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer);
}

/** Marker lifetime when its source unregisters/unloads (handoff 3.2). */
UENUM(BlueprintType)
enum class EDocMarkerLifetime : uint8
{
	/** Remove the marker and notify views. */
	ActorLifetime,
	/** Keep the authored/saved location and metadata. */
	PersistentLocation,
	/** Keep the last authorized location, mark stale, expose observation time. */
	LastKnownDynamic,
	/** Ask the provider (delegate) to update/remove; never invent a current position. */
	ProviderManaged
};

/** Who may receive a marker descriptor (enforced before a view sees it; hiding a widget is not access control). */
UENUM(BlueprintType)
enum class EDocMarkerAudience : uint8
{
	Everyone,
	/** Only views whose owner scope equals the marker's OwnerScope. */
	OwnerOnly
};

UENUM(BlueprintType)
enum class EDocMapOrientation : uint8
{
	NorthUp,
	PlayerUp,
	CustomRotation
};

UENUM(BlueprintType)
enum class EDocMapDiscoveryMode : uint8
{
	None,
	RegionBased,
	RadiusDiscovery,
	/** Extension concepts (not implemented in the base; need their own capability tests). */
	GridBased,
	TextureMask,
	Custom
};

UENUM(BlueprintType)
enum class EDocMapPointStatus : uint8
{
	Invalid,
	Inside,
	Outside
};

/**
 * Authored planar orthographic mapping (handoff 3.3). Orthonormal basis U (map +u),
 * V (map +v), N (height), origin O at normalized (0,0), positive lengths Lu/Lv (cm)
 * so that normalized (1,1) is O + Lu*U + Lv*V. Double precision throughout.
 */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapCoordinateTransform
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FVector Origin = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FVector U = FVector(1, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FVector V = FVector(0, 1, 0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FVector N = FVector(0, 0, 1);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (Units = "cm")) double LengthU = 100000.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (Units = "cm")) double LengthV = 100000.0;

	/** Rejects zero/negative extents, non-orthonormal basis and non-finite values. */
	bool Validate(FString& OutError) const;
};

/** Typed conversion result: unclamped math and clamped display coordinates are separate. */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") EDocMapPointStatus Status = EDocMapPointStatus::Invalid;
	/** Unclamped normalized map coordinates. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FVector2D Normalized = FVector2D::ZeroVector;
	/** Clamped to [0,1] for display only (never used for inversion). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FVector2D DisplayClamped = FVector2D::ZeroVector;
	/** Signed height along N (lost by 2D maps; required to invert). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") double Height = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FString Error;

	bool IsValid() const { return Status != EDocMapPointStatus::Invalid; }
};

/** Widget/view geometry supplied by the UI adapter (base never needs UMG). */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapViewGeometry
{
	GENERATED_BODY()

	/** Local widget size in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector2D WidgetSize = FVector2D(512, 512);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") float DPIScale = 1.f;
	/** 1 = the whole map fits the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") float Zoom = 1.f;
	/** Normalized map coordinate shown at the widget centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector2D Center = FVector2D(0.5, 0.5);
	/** Display rotation (degrees, clockwise) applied around the widget centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") float RotationDegrees = 0.f;
};

/** Camera/viewport data for off-screen indicators (from the correct local player's view). */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapViewProjection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector ViewLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FRotator ViewRotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") float HorizontalFOV = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector2D ViewportSize = FVector2D(1920, 1080);
	/** Safe rectangle in viewport pixels (min/max). Zero size = whole viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector2D SafeMin = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector2D SafeMax = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapLayerDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer", meta = (Categories = "Map.Layer")) FGameplayTag LayerTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer") FText DisplayName;
};

/** Floor with a half-open altitude band [MinZ, MaxZ) (handoff 3.6). */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapFloorDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor") FName FloorId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor", meta = (Units = "cm")) double MinZ = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor", meta = (Units = "cm")) double MaxZ = 400.0;
	/** Overlapping bands: higher priority wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor") TSoftObjectPtr<UTexture2D> Texture;
	/** Optional region/provider key for stacked interiors where Z is ambiguous. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor") FName ProviderKey;
};

/** Map definition (handoff 3.2). */
UCLASS(BlueprintType)
class DOCMAPNAVIGATIONRUNTIME_API UDocMapDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id distinguishing definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FName MapId;
	/** Semantic category. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FGameplayTag MapTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FText DisplayName;
	/** Persistent world namespace this map covers (zero = any world). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FGuid WorldNamespace;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FDocMapCoordinateTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") TSoftObjectPtr<UTexture2D> Texture;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") FVector2D TexturePixels = FVector2D(2048, 2048);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") TArray<FDocMapLayerDefinition> Layers;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") TArray<FDocMapFloorDefinition> Floors;
	/** Floor band hysteresis (cm) to prevent flicker on stairs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (Units = "cm")) double FloorHysteresis = 50.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") float MinZoom = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") float MaxZoom = 8.f;
	/** World yaw (degrees) of authored north. Never assumed to be +X or +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map") float NorthYawDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery") EDocMapDiscoveryMode DiscoveryMode = EDocMapDiscoveryMode::RadiusDiscovery;
	/** Radius coverage cell size along U/V (cm). Stored with saves (schema). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery", meta = (Units = "cm", ClampMin = "100")) double DiscoveryCellSize = 2000.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery", meta = (Units = "cm")) double DefaultRevealRadius = 5000.0;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class DOCMAPNAVIGATIONRUNTIME_API UDocMapMarkerDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker", meta = (Categories = "Map.Marker")) FGameplayTag MarkerTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FText LabelTemplate;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FGameplayTagContainer FilterTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") bool bRequiresDiscovery = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") EDocMarkerAudience Audience = EDocMarkerAudience::Everyone;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") EDocMarkerLifetime Lifetime = EDocMarkerLifetime::ActorLifetime;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") bool bTrackedByDefault = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FGameplayTag Layer;
	/** Dynamic sources: minimum move before an update (cm) and cadence (s). Static markers never tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker", meta = (Units = "cm")) float MoveThreshold = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") float UpdateInterval = 0.25f;
};

/** Shared marker record snapshot (handoff 3.2). Per-player state is separate. */
USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMapMarkerState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName MarkerId;
	/** Repeated level placements get distinct scopes; (MarkerId, InstanceScope) is the key. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FGuid InstanceScope;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName MapId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FString SourceKey;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") TObjectPtr<UDocMapMarkerDefinition> Definition;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FGameplayTag MarkerTag;
	/** Detached absolute location (engine frame + origin offset; survives origin rebasing). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") float Yaw = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName FloorId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName RegionId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") int32 Priority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FGameplayTagContainer Tags;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FText Label;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") EDocMarkerLifetime Lifetime = EDocMarkerLifetime::ActorLifetime;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") EDocMarkerAudience Audience = EDocMarkerAudience::Everyone;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FDocOwnerScope OwnerScope;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bHasLiveSource = false;
	/** LastKnownDynamic / ProviderManaged after the source left: not a live feed. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bStale = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") double ObservedAt = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bSharedVisible = true;
	/** Custom (player/host-created) marker persisted with map state. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bCustom = false;
};

USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocMarkerQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FName MapId;
	/** Empty = all layers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FGameplayTagContainer Layers;
	/** None = any floor; otherwise markers on this floor (plus markers without a floor if bIncludeFloorless). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FName FloorId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") bool bIncludeFloorless = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FGameplayTagContainer RequiredTags;
	/** false = hierarchical (child tags satisfy parents). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") bool bExactTags = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") bool bUseDistance = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") FVector Origin = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map", meta = (Units = "cm")) double MaxDistance = 0.0;
	/** false = 2D distance in the map plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") bool b3DDistance = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") bool bIncludeUndiscovered = false;
	/** Presentation budget; 0 = settings default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Map") int32 MaxResults = 0;
};

USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocCompassEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName MarkerId;
	/** Signed bearing relative to the heading, (-180, 180]. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") float RelativeBearing = 0.f;
	/** Bearing relative to authored north, [0, 360). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") float CompassBearing = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") double Distance = 0.0;
	/** Direction in heading space: X forward, Y right. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FVector2D Direction = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") int32 Priority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bSamePosition = false;
};

USTRUCT(BlueprintType)
struct DOCMAPNAVIGATIONRUNTIME_API FDocOffscreenIndicator
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FName MarkerId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bOnScreen = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") bool bBehindCamera = false;
	/** Viewport pixels, clamped to the safe rectangle when off-screen. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") FVector2D ScreenPosition = FVector2D::ZeroVector;
	/** Screen-space angle (degrees, 0 = up, clockwise) from the centre toward the target. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Map") float Angle = 0.f;
};

UENUM(BlueprintType)
enum class EDocMarkerChange : uint8
{
	Added,
	Updated,
	Removed,
	Stale
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDocMarkerChangedNative, const FDocMapMarkerState& /*State*/, EDocMarkerChange /*Change*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocMarkerChangedEvent, const FDocMapMarkerState&, State, EDocMarkerChange, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocDiscoveryEvent, FName, MapId, FName, LocationId);
