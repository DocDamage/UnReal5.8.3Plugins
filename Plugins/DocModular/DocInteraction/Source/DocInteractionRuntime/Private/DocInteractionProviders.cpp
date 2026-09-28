#include "DocInteractionProviders.h"
#include "DocInteractionComponents.h"
#include "DocInteractionSubsystem.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionProviders)

void UDocInteractionTargetProvider::FillQueryParams(const FDocInteractionQuery& Query, FCollisionQueryParams& Params) const
{
	Params.TraceTag = TEXT("DocInteraction");
	for (const AActor* Ignored : Query.IgnoredActors)
	{
		Params.AddIgnoredActor(Ignored);
	}
}

bool UDocInteractionTargetProvider::MakeCandidate(const FDocInteractionQuery& Query, AActor* Actor, float Score, FDocInteractionCandidate& OutCandidate) const
{
	if (!Actor || Query.IgnoredActors.Contains(Actor))
	{
		return false;
	}
	UDocInteractableComponent* Interactable = Actor->FindComponentByClass<UDocInteractableComponent>();
	if (!Interactable || !Interactable->IsInteractionEnabled())
	{
		return false;
	}
	OutCandidate = FDocInteractionCandidate();
	OutCandidate.Interactable = Interactable;
	OutCandidate.Actor = Actor;
	OutCandidate.Provider = GetEffectiveName();
	OutCandidate.Location = Interactable->GetInteractionLocation();
	OutCandidate.Distance = FVector::Dist(Query.ViewLocation, OutCandidate.Location);
	OutCandidate.Score = Score;
	OutCandidate.Priority = Interactable->CandidatePriority;
	OutCandidate.bEligible = true;
	return true;
}

bool UDocInteractionTargetProvider::HasLineOfSight(const FDocInteractionQuery& Query, const AActor* Target, const FVector& TargetPoint, ECollisionChannel Channel) const
{
	if (!Query.World)
	{
		return false;
	}
	FCollisionQueryParams Params;
	FillQueryParams(Query, Params);
	FHitResult Hit;
	const bool bHit = Query.World->LineTraceSingleByChannel(Hit, Query.ViewLocation, TargetPoint, Channel, Params);
	return !bHit || Hit.GetActor() == Target;
}

void UDocInteractionProvider_LineTrace::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.World) { return; }
	FCollisionQueryParams Params;
	FillQueryParams(Query, Params);
	Params.bTraceComplex = bTraceComplex;
	const FVector End = Query.ViewLocation + Query.ViewRotation.Vector() * Range;
	FHitResult Hit;
	if (Query.World->LineTraceSingleByChannel(Hit, Query.ViewLocation, End, TraceChannel, Params))
	{
		FDocInteractionCandidate Candidate;
		// Direct aim scores 1; closer hits score marginally higher.
		const float Score = 1.f + (Range > 0.f ? (1.f - Hit.Distance / Range) * 0.01f : 0.f);
		if (MakeCandidate(Query, Hit.GetActor(), Score, Candidate))
		{
			Out.Add(Candidate);
		}
	}
}

void UDocInteractionProvider_SphereTrace::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.World) { return; }
	FCollisionQueryParams Params;
	FillQueryParams(Query, Params);
	const FVector Dir = Query.ViewRotation.Vector();
	const FVector End = Query.ViewLocation + Dir * Range;
	TArray<FHitResult> Hits;
	Query.World->SweepMultiByChannel(Hits, Query.ViewLocation, End, FQuat::Identity, TraceChannel, FCollisionShape::MakeSphere(Radius), Params);
	for (const FHitResult& Hit : Hits)
	{
		FDocInteractionCandidate Candidate;
		const FVector ToHit = (Hit.ImpactPoint - Query.ViewLocation).GetSafeNormal();
		const float Aim = FMath::Max(0.f, FVector::DotProduct(Dir, ToHit));
		if (MakeCandidate(Query, Hit.GetActor(), Aim, Candidate))
		{
			Out.Add(Candidate);
		}
	}
}

void UDocInteractionProvider_CapsuleTrace::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.World) { return; }
	FCollisionQueryParams Params;
	FillQueryParams(Query, Params);
	const FVector Dir = Query.ViewRotation.Vector();
	const FVector End = Query.ViewLocation + Dir * Range;
	TArray<FHitResult> Hits;
	Query.World->SweepMultiByChannel(Hits, Query.ViewLocation, End, FQuat::Identity, TraceChannel, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
	for (const FHitResult& Hit : Hits)
	{
		FDocInteractionCandidate Candidate;
		const FVector ToHit = (Hit.ImpactPoint - Query.ViewLocation).GetSafeNormal();
		const float Aim = FMath::Max(0.f, FVector::DotProduct(Dir, ToHit));
		if (MakeCandidate(Query, Hit.GetActor(), Aim, Candidate))
		{
			Out.Add(Candidate);
		}
	}
}

void UDocInteractionProvider_Overlap::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.World || !Query.Interactor) { return; }
	const AActor* Self = Query.Interactor->GetOwner();
	const FVector Center = Self ? Self->GetActorLocation() : Query.ViewLocation;

	FCollisionQueryParams Params;
	FillQueryParams(Query, Params);
	TArray<FOverlapResult> Overlaps;
	Query.World->OverlapMultiByChannel(Overlaps, Center, FQuat::Identity, OverlapChannel, FCollisionShape::MakeSphere(Range), Params);

	const FVector Forward = Query.ViewRotation.Vector();
	TSet<const AActor*> Seen; // several primitives of one actor produce one candidate
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor || Seen.Contains(Actor))
		{
			continue;
		}
		Seen.Add(Actor);
		FDocInteractionCandidate Candidate;
		if (!MakeCandidate(Query, Actor, 0.f, Candidate))
		{
			continue;
		}
		if (bRequireLineOfSight && !HasLineOfSight(Query, Actor, Candidate.Location, LineOfSightChannel))
		{
			continue;
		}
		const float Proximity = Range > 0.f ? 1.f - FMath::Clamp(FVector::Dist(Center, Candidate.Location) / Range, 0.f, 1.f) : 0.f;
		const float Facing = FMath::Max(0.f, FVector::DotProduct(Forward, (Candidate.Location - Query.ViewLocation).GetSafeNormal()));
		Candidate.Score = FMath::Lerp(Proximity, Facing, FacingWeight);
		Out.Add(Candidate);
	}
}

void UDocInteractionProvider_CursorTrace::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.Interactor) { return; }
	const APawn* Pawn = Cast<APawn>(Query.Interactor->GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : Cast<APlayerController>(Query.Interactor->GetOwner());
	if (!PC)
	{
		return; // no cursor for AI / non-player interactors
	}
	FHitResult Hit;
	if (PC->GetHitResultUnderCursor(TraceChannel, false, Hit))
	{
		FDocInteractionCandidate Candidate;
		if (MakeCandidate(Query, Hit.GetActor(), 1.f, Candidate) && (Range <= 0.f || Candidate.Distance <= Range))
		{
			Out.Add(Candidate);
		}
	}
}

void UDocInteractionProvider_Proximity::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.World || !Query.Interactor) { return; }
	const UDocInteractionSubsystem* Subsystem = Query.World->GetSubsystem<UDocInteractionSubsystem>();
	if (!Subsystem) { return; }
	const AActor* Self = Query.Interactor->GetOwner();
	const FVector Center = Self ? Self->GetActorLocation() : Query.ViewLocation;

	TArray<UDocInteractableComponent*> Nearby;
	Subsystem->GetInteractablesInRadius(Center, Range, Nearby);
	for (UDocInteractableComponent* Interactable : Nearby)
	{
		AActor* Actor = Interactable ? Interactable->GetOwner() : nullptr;
		FDocInteractionCandidate Candidate;
		if (!MakeCandidate(Query, Actor, 0.f, Candidate))
		{
			continue;
		}
		if (bRequireLineOfSight && !HasLineOfSight(Query, Actor, Candidate.Location, LineOfSightChannel))
		{
			continue;
		}
		Candidate.Score = Range > 0.f ? 1.f - FMath::Clamp(FVector::Dist(Center, Candidate.Location) / Range, 0.f, 1.f) : 0.f;
		Out.Add(Candidate);
	}
}

void UDocInteractionProvider_ExplicitTarget::GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const
{
	if (!Query.Interactor) { return; }
	AActor* Target = Query.Interactor->ExplicitTarget.Get();
	FDocInteractionCandidate Candidate;
	if (MakeCandidate(Query, Target, 2.f, Candidate) && (Range <= 0.f || Candidate.Distance <= Range))
	{
		Out.Add(Candidate);
	}
}
