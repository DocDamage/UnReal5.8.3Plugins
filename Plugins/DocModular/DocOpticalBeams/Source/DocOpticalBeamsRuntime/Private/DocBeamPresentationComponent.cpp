#include "DocBeamPresentationComponent.h"
#include "DocBeamEmitterComponent.h"
#include "DocOpticalBeamSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocBeamPresentationComponent::UDocBeamPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocBeamPresentationComponent::OnRegister()
{
	Super::OnRegister();
	if (EmitterId.IsNone() && GetOwner())
	{
		if (const UDocBeamEmitterComponent* Emitter = GetOwner()->FindComponentByClass<UDocBeamEmitterComponent>())
		{
			EmitterId = Emitter->EmitterId;
		}
	}
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		PathHandle = Subsystem->OnBeamPathUpdatedNative.AddUObject(this, &UDocBeamPresentationComponent::HandlePathUpdated);
		FDocBeamPath Existing;
		if (Subsystem->QueryPath(EmitterId, Existing).IsSuccess())
		{
			ApplyPath(Existing);
		}
	}
}

void UDocBeamPresentationComponent::OnUnregister()
{
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		Subsystem->OnBeamPathUpdatedNative.Remove(PathHandle);
	}
	PathHandle.Reset();
	if (InstanceComponent)
	{
		InstanceComponent->DestroyComponent();
		InstanceComponent = nullptr;
	}
	Super::OnUnregister();
}

void UDocBeamPresentationComponent::HandlePathUpdated(FName InEmitterId, const FDocBeamPath& Path)
{
	if (InEmitterId == EmitterId)
	{
		ApplyPath(Path);
	}
}

void UDocBeamPresentationComponent::ApplyPath(const FDocBeamPath& Path)
{
	SegmentTransforms.Reset(Path.Segments.Num());
	SegmentChannels.Reset(Path.Segments.Num());
	for (const FDocBeamSegment& Segment : Path.Segments)
	{
		const FVector Dir = Segment.Direction.IsNearlyZero() ? FVector::ForwardVector : Segment.Direction;
		SegmentTransforms.Add(FTransform(Dir.Rotation(), Segment.StartPoint, FVector(FMath::Max(Segment.Length, 0.0f), BeamThickness, BeamThickness)));
		SegmentChannels.Add(Segment.ChannelTag);
	}
	++RebuildCount;

	if (BeamMesh && GetOwner())
	{
		if (!InstanceComponent)
		{
			InstanceComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
			InstanceComponent->SetStaticMesh(BeamMesh);
			InstanceComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (USceneComponent* Root = GetOwner()->GetRootComponent())
			{
				InstanceComponent->SetupAttachment(Root);
			}
			InstanceComponent->RegisterComponent();
		}
		InstanceComponent->ClearInstances();
		for (const FTransform& Xf : SegmentTransforms)
		{
			InstanceComponent->AddInstance(Xf, /*bWorldSpace*/ true);
		}
	}
}
