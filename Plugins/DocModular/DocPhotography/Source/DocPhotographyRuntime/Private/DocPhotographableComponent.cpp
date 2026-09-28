#include "DocPhotographableComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocPhotographableComponent::UDocPhotographableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default samples: centre plus four face points.
	RelativeSamplePoints.Add(FVector::ZeroVector);
	RelativeSamplePoints.Add(FVector(0.0f, 30.0f, 30.0f));
	RelativeSamplePoints.Add(FVector(0.0f, -30.0f, 30.0f));
	RelativeSamplePoints.Add(FVector(0.0f, 30.0f, -30.0f));
	RelativeSamplePoints.Add(FVector(0.0f, -30.0f, -30.0f));
}

void UDocPhotographableComponent::OnRegister()
{
	Super::OnRegister();
	if (SubjectId.IsNone() && GetOwner())
	{
		SubjectId = GetOwner()->GetFName();
	}
	if (UWorld* World = GetWorld())
	{
		if (UDocPhotoSubjectRegistry* Registry = World->GetSubsystem<UDocPhotoSubjectRegistry>())
		{
			Registry->RegisterSubject(this);
		}
	}
}

void UDocPhotographableComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocPhotoSubjectRegistry* Registry = World->GetSubsystem<UDocPhotoSubjectRegistry>())
		{
			Registry->UnregisterSubject(this);
		}
	}
	Super::OnUnregister();
}

TArray<FVector> UDocPhotographableComponent::GetWorldSamplePoints() const
{
	TArray<FVector> Result;
	const FTransform& Xf = GetComponentTransform();
	for (const FVector& Rel : RelativeSamplePoints)
	{
		Result.Add(Xf.TransformPosition(Rel));
	}
	return Result;
}

TArray<FVector> UDocPhotographableComponent::GetWorldBoundsCorners() const
{
	TArray<FVector> Corners;
	const FTransform& Xf = GetComponentTransform();
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Local((i & 1) ? BoundsExtent.X : -BoundsExtent.X, (i & 2) ? BoundsExtent.Y : -BoundsExtent.Y, (i & 4) ? BoundsExtent.Z : -BoundsExtent.Z);
		Corners.Add(Xf.TransformPosition(Local));
	}
	return Corners;
}

void UDocPhotoSubjectRegistry::RegisterSubject(UDocPhotographableComponent* Subject)
{
	if (!Subject)
	{
		return;
	}
	for (const TWeakObjectPtr<UDocPhotographableComponent>& Existing : Subjects)
	{
		if (Existing.Get() == Subject)
		{
			return;
		}
	}
	Subjects.Add(Subject);
	++Revision;
}

void UDocPhotoSubjectRegistry::UnregisterSubject(UDocPhotographableComponent* Subject)
{
	const int32 Removed = Subjects.RemoveAll([Subject](const TWeakObjectPtr<UDocPhotographableComponent>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == Subject;
	});
	if (Removed > 0)
	{
		++Revision;
	}
}

TArray<UDocPhotographableComponent*> UDocPhotoSubjectRegistry::GetSubjectsSorted() const
{
	TArray<UDocPhotographableComponent*> Result;
	for (const TWeakObjectPtr<UDocPhotographableComponent>& Ptr : Subjects)
	{
		if (UDocPhotographableComponent* Subject = Ptr.Get())
		{
			Result.Add(Subject);
		}
	}
	Result.Sort([](const UDocPhotographableComponent& A, const UDocPhotographableComponent& B)
	{
		return A.SubjectId == B.SubjectId ? A.GetName() < B.GetName() : A.SubjectId.LexicalLess(B.SubjectId);
	});
	return Result;
}

int32 UDocPhotoSubjectRegistry::GetSubjectCount() const
{
	return GetSubjectsSorted().Num();
}
