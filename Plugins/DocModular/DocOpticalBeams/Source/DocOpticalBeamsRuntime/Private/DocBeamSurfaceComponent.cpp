#include "DocBeamSurfaceComponent.h"
#include "DocOpticalBeamSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocBeamSurfaceComponent::UDocBeamSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocBeamSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	if (SurfaceId.IsNone() && GetOwner())
	{
		SurfaceId = GetOwner()->GetFName();
	}
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		const FDocSystemResult Result = Subsystem->RegisterSurface(this);
		LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
	}
}

void UDocBeamSurfaceComponent::OnUnregister()
{
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		Subsystem->UnregisterSurface(this);
	}
	Super::OnUnregister();
}

FVector UDocBeamSurfaceComponent::GetSurfaceNormal(const FVector& HitNormal) const
{
	if (!SurfaceNormalOverride.IsNearlyZero() && !SurfaceNormalOverride.ContainsNaN())
	{
		if (const AActor* Owner = GetOwner())
		{
			const FTransform& Xf = Owner->GetActorTransform();
			FVector N = Xf.TransformVectorNoScale(SurfaceNormalOverride).GetSafeNormal();
			// An odd number of negative scale axes mirrors the surface; keep the authored front consistent.
			const FVector S = Xf.GetScale3D();
			if (S.X * S.Y * S.Z < 0.0)
			{
				N = -N;
			}
			return N;
		}
		return SurfaceNormalOverride.GetSafeNormal();
	}
	return HitNormal.GetSafeNormal();
}

EDocBeamSurfaceType UDocBeamSurfaceComponent::GetSurfaceType() const
{
	if (!OpticalProfile || !OpticalProfile->ValidateProfile().IsSuccess())
	{
		return EDocBeamSurfaceType::Blocker;
	}
	return OpticalProfile->SurfaceType;
}
