#include "DocSurfacePaintingSubsystem.h"
#include "DocSurfacePaintingLog.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsSettings.h"

void UDocSurfacePaintingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocSurfacePaintingSubsystem::Deinitialize()
{
	RegisteredSurfaces.Empty();
	Super::Deinitialize();
}

FDocSystemResult UDocSurfacePaintingSubsystem::RegisterSurfaceComponent(UDocPaintableSurfaceComponent* Component)
{
	if (!Component || Component->SurfaceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Surface needs an instance SurfaceId"));
	}
	if (const TWeakObjectPtr<UDocPaintableSurfaceComponent>* Existing = RegisteredSurfaces.Find(Component->SurfaceId))
	{
		if (Existing->Get() == Component)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("SurfaceId %s is already used by another instance"), *Component->SurfaceId.ToString()));
		}
	}
	RegisteredSurfaces.Add(Component->SurfaceId, Component);
	return FDocSystemResult::MakeSuccess();
}

void UDocSurfacePaintingSubsystem::UnregisterSurfaceComponent(UDocPaintableSurfaceComponent* Component)
{
	if (Component && GetSurfaceComponent(Component->SurfaceId) == Component)
	{
		RegisteredSurfaces.Remove(Component->SurfaceId);
	}
}

UDocPaintableSurfaceComponent* UDocSurfacePaintingSubsystem::GetSurfaceComponent(FName SurfaceId) const
{
	if (const TWeakObjectPtr<UDocPaintableSurfaceComponent>* Found = RegisteredSurfaces.Find(SurfaceId))
	{
		return Found->Get();
	}
	return nullptr;
}

FDocSystemResult UDocSurfacePaintingSubsystem::ApplyStrokeToSurface(FName SurfaceId, const FDocPaintStroke& Stroke)
{
	UDocPaintableSurfaceComponent* Comp = GetSurfaceComponent(SurfaceId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Surface %s not found"), *SurfaceId.ToString()));
	}
	if (!Stroke.SurfaceId.IsNone() && Stroke.SurfaceId != SurfaceId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Stroke targets a different surface"));
	}
	return Comp->ApplyStroke(Stroke);
}

FDocSystemResult UDocSurfacePaintingSubsystem::QuerySurfaceCoverage(FName SurfaceId, FDocPaintCoverageResult& OutResult) const
{
	UDocPaintableSurfaceComponent* Comp = GetSurfaceComponent(SurfaceId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Surface %s not found"), *SurfaceId.ToString()));
	}
	OutResult = Comp->QueryCoverage();
	return FDocSystemResult::MakeSuccess();
}

EDocPaintMappingStatus UDocCollisionUVCoordinateProvider::ResolveSurfaceUV(const FHitResult& Hit, int32 UVChannel, FVector2D& OutUV) const
{
	const UPhysicsSettings* Settings = UPhysicsSettings::Get();
	if (!Settings || !Settings->bSupportUVFromHitResults)
	{
		return EDocPaintMappingStatus::Unsupported; // host setting is left untouched
	}
	if (!Hit.bBlockingHit || !Hit.GetComponent())
	{
		return EDocPaintMappingStatus::InvalidMapping;
	}
	FVector2D UV;
	if (!UGameplayStatics::FindCollisionUV(Hit, UVChannel, UV) || !FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y))
	{
		return EDocPaintMappingStatus::InvalidMapping;
	}
	OutUV = UV;
	return EDocPaintMappingStatus::Valid;
}
