#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Engine/HitResult.h"
#include "DocSurfacePaintingTypes.h"
#include "IDocSurfaceCoordinateProvider.generated.h"

UINTERFACE(MinimalAPI)
class UDocSurfaceCoordinateProvider : public UInterface
{
	GENERATED_BODY()
};

/** Converts a world hit into a surface-local UV. Must never invent (0,0) for a missing UV. */
class DOCSURFACEPAINTINGRUNTIME_API IDocSurfaceCoordinateProvider
{
	GENERATED_BODY()

public:
	virtual EDocPaintMappingStatus ResolveSurfaceUV(const FHitResult& Hit, int32 UVChannel, FVector2D& OutUV) const = 0;
};
