#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocSurfacePaintingTypes.h"
#include "IDocSurfaceCoordinateProvider.h"
#include "DocPaintableSurfaceComponent.h"
#include "DocSurfacePaintingSubsystem.generated.h"

/** Registry of paintable instances by instance SurfaceId. */
UCLASS()
class DOCSURFACEPAINTINGRUNTIME_API UDocSurfacePaintingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Conflict if another live instance already uses the SurfaceId. */
	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult RegisterSurfaceComponent(UDocPaintableSurfaceComponent* Component);

	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	void UnregisterSurfaceComponent(UDocPaintableSurfaceComponent* Component);

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	UDocPaintableSurfaceComponent* GetSurfaceComponent(FName SurfaceId) const;

	UFUNCTION(BlueprintCallable, Category = "Surface Painting")
	FDocSystemResult ApplyStrokeToSurface(FName SurfaceId, const FDocPaintStroke& Stroke);

	UFUNCTION(BlueprintPure, Category = "Surface Painting")
	FDocSystemResult QuerySurfaceCoverage(FName SurfaceId, FDocPaintCoverageResult& OutResult) const;

	static constexpr int32 MaxMaskDimension = UDocPaintSurfaceDefinition::MaxMaskDimension;
	static constexpr int32 MaxTotalTexels = UDocPaintSurfaceDefinition::MaxTotalTexels;

private:
	TMap<FName, TWeakObjectPtr<UDocPaintableSurfaceComponent>> RegisteredSurfaces;
};

/**
 * Reference provider using the engine's collision UV lookup. It requires the project's physics setting that keeps
 * UV data in hit results; when that setting is off it reports Unsupported and never changes the setting.
 */
UCLASS()
class DOCSURFACEPAINTINGRUNTIME_API UDocCollisionUVCoordinateProvider : public UObject, public IDocSurfaceCoordinateProvider
{
	GENERATED_BODY()

public:
	virtual EDocPaintMappingStatus ResolveSurfaceUV(const FHitResult& Hit, int32 UVChannel, FVector2D& OutUV) const override;
};
