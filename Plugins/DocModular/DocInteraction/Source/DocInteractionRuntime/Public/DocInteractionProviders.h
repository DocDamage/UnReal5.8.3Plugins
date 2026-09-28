#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "DocInteractionTypes.h"
#include "DocInteractionProviders.generated.h"

class UDocInteractorComponent;

/** Input to providers: who is looking and what to ignore. */
struct DOCINTERACTIONRUNTIME_API FDocInteractionQuery
{
	const UDocInteractorComponent* Interactor = nullptr;
	UWorld* World = nullptr;
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	TArray<const AActor*> IgnoredActors;
};

/**
 * Base target provider (handoff 5.2). Configuration is immutable; GatherCandidates
 * must not store per-query state on the provider.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class DOCINTERACTIONRUNTIME_API UDocInteractionTargetProvider : public UObject
{
	GENERATED_BODY()

public:
	/** Name reported in candidates. Defaults to the class name. */
	UPROPERTY(EditAnywhere, Category = "Provider")
	FName ProviderName;

	UPROPERTY(EditAnywhere, Category = "Provider", meta = (ClampMin = "0.0", Units = "cm"))
	float Range = 250.f;

	/** Add found candidates to Out (duplicates across providers are merged by the interactor). */
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const {}

	FName GetEffectiveName() const { return ProviderName.IsNone() ? GetClass()->GetFName() : ProviderName; }

protected:
	/** Convert a hit/overlapped actor into a candidate if it has an enabled interactable. */
	bool MakeCandidate(const FDocInteractionQuery& Query, AActor* Actor, float Score, FDocInteractionCandidate& OutCandidate) const;
	bool HasLineOfSight(const FDocInteractionQuery& Query, const AActor* Target, const FVector& TargetPoint, ECollisionChannel Channel) const;
	void FillQueryParams(const FDocInteractionQuery& Query, struct FCollisionQueryParams& Params) const;
};

/** Single line trace from the viewpoint. */
UCLASS(DisplayName = "Line Trace")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_LineTrace : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, Category = "Provider") bool bTraceComplex = false;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/** Sphere sweep from the viewpoint (forgiving aim). */
UCLASS(DisplayName = "Sphere Trace")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_SphereTrace : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (ClampMin = "1.0", Units = "cm")) float Radius = 20.f;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/** Capsule sweep from the viewpoint. */
UCLASS(DisplayName = "Capsule Trace")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_CapsuleTrace : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (ClampMin = "1.0", Units = "cm")) float Radius = 20.f;
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (ClampMin = "1.0", Units = "cm")) float HalfHeight = 60.f;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/** Sphere overlap around the interactor. Scores by distance and facing. */
UCLASS(DisplayName = "Overlap")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_Overlap : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") TEnumAsByte<ECollisionChannel> OverlapChannel = ECC_WorldDynamic;
	UPROPERTY(EditAnywhere, Category = "Provider") bool bRequireLineOfSight = false;
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (EditCondition = "bRequireLineOfSight")) TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;
	/** Weight of facing (0..1) versus proximity in the score. */
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (ClampMin = "0.0", ClampMax = "1.0")) float FacingWeight = 0.5f;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/** Trace under the mouse cursor of the interactor's player controller (no-op without one). */
UCLASS(DisplayName = "Cursor Trace")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_CursorTrace : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/**
 * Registered interactables within Range, from the subsystem registry (no physics
 * query). Works for actors without collision.
 */
UCLASS(DisplayName = "Proximity")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_Proximity : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Provider") bool bRequireLineOfSight = false;
	UPROPERTY(EditAnywhere, Category = "Provider", meta = (EditCondition = "bRequireLineOfSight")) TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};

/** The interactor component's ExplicitTarget (AI, scripts). Range 0 = unlimited. */
UCLASS(DisplayName = "Explicit Target")
class DOCINTERACTIONRUNTIME_API UDocInteractionProvider_ExplicitTarget : public UDocInteractionTargetProvider
{
	GENERATED_BODY()
public:
	UDocInteractionProvider_ExplicitTarget() { Range = 0.f; }
	virtual void GatherCandidates(const FDocInteractionQuery& Query, TArray<FDocInteractionCandidate>& Out) const override;
};
