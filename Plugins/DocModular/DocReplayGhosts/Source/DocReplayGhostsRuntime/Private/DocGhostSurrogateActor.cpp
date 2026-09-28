#include "DocGhostSurrogateActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"

ADocGhostSurrogateActor::ADocGhostSurrogateActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SceneRoot->PrimaryComponentTick.bCanEverTick = false;
}

void ADocGhostSurrogateActor::UpdateGhostTransform(const FTransform& InTransform)
{
	// Teleport, no sweep: a ghost never pushes or overlaps anything.
	SetActorTransform(InTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void ADocGhostSurrogateActor::EnforceIsolation()
{
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
	SetActorTickEnabled(false);
	TArray<UActorComponent*> Components;
	GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}
		Component->SetComponentTickEnabled(false);
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->SetCanEverAffectNavigation(false);
			Primitive->SetSimulatePhysics(false);
		}
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			// No animation evaluation means no notifies and no root motion reach gameplay.
			Skeletal->bPauseAnims = true;
		}
	}
}

bool ADocGhostSurrogateActor::HasAnyCollision() const
{
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive && Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			return true;
		}
	}
	return GetActorEnableCollision();
}

bool ADocGhostSurrogateActor::HasAnyOverlapEvents() const
{
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive && Primitive->GetGenerateOverlapEvents())
		{
			return true;
		}
	}
	return false;
}

bool ADocGhostSurrogateActor::HasAnyTickingComponent() const
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		if (Component && Component->IsComponentTickEnabled())
		{
			return true;
		}
	}
	return IsActorTickEnabled();
}

bool ADocGhostSurrogateActor::CanAffectLiveWorld() const
{
	return HasAnyCollision() || HasAnyOverlapEvents() || HasAnyTickingComponent() || CanBeDamaged() || GetIsReplicated();
}
