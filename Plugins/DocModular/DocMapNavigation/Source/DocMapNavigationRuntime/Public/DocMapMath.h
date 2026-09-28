#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DocMapTypes.h"
#include "DocMapMath.generated.h"

/**
 * Pure map math (handoff 3.3 / 3.6). Every conversion states its inverse; 2D map
 * coordinates lose height, so MapToWorld needs a caller-supplied height or floor plane.
 */
UCLASS()
class DOCMAPNAVIGATIONRUNTIME_API UDocMapMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** World → normalized (unclamped) + height; Inside/Outside against [0,1]². */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static FDocMapPoint WorldToNormalized(const FDocMapCoordinateTransform& Transform, FVector World);

	/** Normalized + explicit height → world (the declared surface, not a ground hit). */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static bool NormalizedToWorld(const FDocMapCoordinateTransform& Transform, FVector2D Normalized, double Height, FVector& OutWorld, FString& OutError);

	/** Normalized ↔ map texture pixels. */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static FVector2D NormalizedToPixels(FVector2D Normalized, FVector2D TexturePixels) { return Normalized * TexturePixels; }

	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static FVector2D PixelsToNormalized(FVector2D Pixels, FVector2D TexturePixels);

	/** Normalized map → local widget coordinates (zoom, centre/pan, rotation, DPI). False on invalid geometry. */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static bool MapToWidget(const FDocMapViewGeometry& Geometry, FVector2D Normalized, FVector2D& OutWidget);

	/** Exact inverse of MapToWidget. */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static bool WidgetToMap(const FDocMapViewGeometry& Geometry, FVector2D Widget, FVector2D& OutNormalized);

	/**
	 * Floor from altitude: half-open bands [MinZ, MaxZ) (Z == MaxZ belongs above), overlap by
	 * priority then FloorId, hysteresis keeps CurrentFloor while Z stays within its band
	 * expanded by Hysteresis. No candidate → NAME_None (exterior/unknown by policy).
	 */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static FName ResolveFloor(const TArray<FDocMapFloorDefinition>& Floors, double Z, FName CurrentFloor, double Hysteresis);

	/** Signed bearing (-180,180] from Heading yaw to Target, and [0,360) compass bearing from north. */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static bool ComputeBearing(FVector From, float HeadingYaw, FVector Target, float NorthYaw, FDocCompassEntry& OutEntry);

	/** Off-screen indicator data for one world point. False for invalid geometry. */
	UFUNCTION(BlueprintPure, Category = "Doc|Map")
	static bool ProjectIndicator(const FDocMapViewProjection& View, FVector Target, FDocOffscreenIndicator& OutIndicator);

	/** Engine-frame location ↔ absolute (origin-rebasing independent) location. */
	static FVector ToAbsolute(const UWorld* World, const FVector& EngineLocation);
	static FVector ToEngine(const UWorld* World, const FVector& AbsoluteLocation);
};
