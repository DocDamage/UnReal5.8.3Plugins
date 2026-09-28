#include "DocRegionComponents.h"
#include "DocRegionSubsystem.h"
#include "DocRegionsLog.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocRegionComponents)

// ---------------------------------------------------------------------------
// Definition
// ---------------------------------------------------------------------------

int32 UDocRegionDefinition::ComputeDepth() const
{
	TSet<const UDocRegionDefinition*> Visited;
	Visited.Add(this);
	int32 Depth = 0;
	const UDocRegionDefinition* Current = this;
	while (Current)
	{
		// Parents are soft references; only already-loaded parents contribute (no sync load).
		const UDocRegionDefinition* Parent = Current->ExplicitParent.Get();
		if (!Parent)
		{
			break;
		}
		if (Visited.Contains(Parent))
		{
			return -1; // cycle
		}
		Visited.Add(Parent);
		++Depth;
		Current = Parent;
	}
	return Depth;
}

#if WITH_EDITOR
EDataValidationResult UDocRegionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!RegionTag.IsValid())
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: RegionTag is not set"), *GetPathName())));
		Result = EDataValidationResult::Invalid;
	}
	// Editor validation may load parents to check the whole chain.
	TSet<const UDocRegionDefinition*> Visited{ this };
	const UDocRegionDefinition* Current = this;
	while (Current && !Current->ExplicitParent.IsNull())
	{
		const UDocRegionDefinition* Parent = Current->ExplicitParent.LoadSynchronous();
		if (!Parent)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: parent %s cannot be loaded"), *GetPathName(), *Current->ExplicitParent.ToString())));
			return EDataValidationResult::Invalid;
		}
		if (Visited.Contains(Parent))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: explicit parent cycle through %s"), *GetPathName(), *Parent->GetPathName())));
			return EDataValidationResult::Invalid;
		}
		Visited.Add(Parent);
		Current = Parent;
	}
	return Result;
}
#endif

// ---------------------------------------------------------------------------
// Region component
// ---------------------------------------------------------------------------

UDocRegionComponent::UDocRegionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocRegionComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// Runtime-spawned instances (not templates, not loaded) get an id once.
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_WasLoaded) && !InstanceId.IsValid())
	{
		InstanceId = FGuid::NewGuid();
	}
}

void UDocRegionComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	// Editor duplication creates a new authored identity; PIE duplication preserves it.
	if (!bDuplicateForPIE)
	{
		InstanceId = FGuid::NewGuid();
	}
}

#if WITH_EDITOR
void UDocRegionComponent::PostEditImport()
{
	Super::PostEditImport();
	InstanceId = FGuid::NewGuid(); // paste
}
#endif

void UDocRegionComponent::SetInstanceId(const FGuid& InId)
{
	if (!InId.IsValid())
	{
		return;
	}
	UDocRegionSubsystem* Subsystem = HasBegunPlay() ? UDocRegionSubsystem::Get(this) : nullptr;
	if (Subsystem) { Subsystem->UnregisterRegion(this); }
	InstanceId = InId;
	if (Subsystem) { Subsystem->RegisterRegion(this); }
}

FGameplayTag UDocRegionComponent::GetRegionTag() const
{
	return Definition ? Definition->RegionTag : FGameplayTag();
}

int32 UDocRegionComponent::GetEffectivePriority() const
{
	return bOverridePriority ? PriorityOverride : (Definition ? Definition->Priority : 0);
}

bool UDocRegionComponent::ContainsPoint(const FVector& WorldPoint, float Tolerance) const
{
	const FTransform& T = GetComponentTransform();
	switch (Shape)
	{
	case EDocRegionShape::Box:
	{
		const FVector Local = T.InverseTransformPosition(WorldPoint);
		const FVector Extent = BoxExtent + FVector(Tolerance) / T.GetScale3D().GetAbs().ComponentMax(FVector(KINDA_SMALL_NUMBER));
		return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
	}
	case EDocRegionShape::Sphere:
	{
		const float WorldRadius = Radius * T.GetScale3D().GetAbsMax();
		return FVector::DistSquared(T.GetLocation(), WorldPoint) <= FMath::Square(WorldRadius + Tolerance);
	}
	case EDocRegionShape::Capsule:
	{
		const FVector Local = T.InverseTransformPositionNoScale(WorldPoint);
		const float Scale = T.GetScale3D().GetAbsMax();
		const float R = Radius * Scale + Tolerance;
		const float SegmentHalf = FMath::Max(0.f, CapsuleHalfHeight * Scale - Radius * Scale);
		const float ClampedZ = FMath::Clamp(Local.Z, -SegmentHalf, SegmentHalf);
		return FVector::DistSquared(Local, FVector(0.f, 0.f, ClampedZ)) <= R * R;
	}
	case EDocRegionShape::Custom:
	default:
	{
		AActor* Owner = GetOwner();
		if (const AVolume* Volume = Cast<AVolume>(Owner))
		{
			return Volume->EncompassesPoint(WorldPoint, Tolerance);
		}
		if (Owner && Owner->Implements<UDocRegionShapeProvider>())
		{
			return IDocRegionShapeProvider::Execute_RegionContainsPoint(Owner, WorldPoint, Tolerance);
		}
		return false;
	}
	}
}

FBox UDocRegionComponent::GetWorldBounds() const
{
	const FTransform& T = GetComponentTransform();
	switch (Shape)
	{
	case EDocRegionShape::Box:
		return FBox(-BoxExtent, BoxExtent).TransformBy(T);
	case EDocRegionShape::Sphere:
	{
		const float R = Radius * T.GetScale3D().GetAbsMax();
		return FBox::BuildAABB(T.GetLocation(), FVector(R));
	}
	case EDocRegionShape::Capsule:
	{
		const float Scale = T.GetScale3D().GetAbsMax();
		const FVector Extent(Radius * Scale, Radius * Scale, FMath::Max(Radius, CapsuleHalfHeight) * Scale);
		return FBox(-Extent, Extent).TransformBy(FTransform(T.GetRotation(), T.GetLocation()));
	}
	default:
	{
		AActor* Owner = GetOwner();
		if (const AVolume* Volume = Cast<AVolume>(Owner))
		{
			return Volume->GetComponentsBoundingBox(true);
		}
		if (Owner && Owner->Implements<UDocRegionShapeProvider>())
		{
			return IDocRegionShapeProvider::Execute_GetRegionBounds(Owner);
		}
		return FBox(ForceInit);
	}
	}
}

FDocRegionInfo UDocRegionComponent::MakeInfo() const
{
	FDocRegionInfo Info;
	Info.InstanceId = InstanceId;
	Info.RegionTag = GetRegionTag();
	Info.DisplayName = Definition ? Definition->DisplayName : FText::GetEmpty();
	Info.Priority = GetEffectivePriority();
	Info.Depth = Definition ? FMath::Max(0, Definition->ComputeDepth()) : 0;
	Info.Component = const_cast<UDocRegionComponent*>(this);
	Info.Definition = Definition;
	return Info;
}

void UDocRegionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
	{
		Subsystem->RegisterRegion(this);
	}
}

void UDocRegionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
	{
		Subsystem->UnregisterRegion(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UDocRegionComponent::OnUnregister()
{
	if (!IsTemplate())
	{
		if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
		{
			Subsystem->UnregisterRegion(this);
		}
	}
	Super::OnUnregister();
}

// ---------------------------------------------------------------------------
// Actors
// ---------------------------------------------------------------------------

ADocRegionVolume::ADocRegionVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Region = CreateDefaultSubobject<UDocRegionComponent>(TEXT("Region"));
	Region->Shape = EDocRegionShape::Custom;
	Region->SetupAttachment(RootComponent);
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

ADocRegionActor::ADocRegionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Region = CreateDefaultSubobject<UDocRegionComponent>(TEXT("Region"));
	RootComponent = Region;
}

// ---------------------------------------------------------------------------
// Observer component
// ---------------------------------------------------------------------------

void UDocRegionObserverComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
	{
		FDocRegionObserverOptions Options;
		Options.MembershipTest = MembershipTest;
		Options.ReferenceOffset = ReferenceOffset;
		Subsystem->RegisterObserver(GetOwner(), Options);
	}
}

void UDocRegionObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
	{
		Subsystem->UnregisterObserver(GetOwner());
	}
	Super::EndPlay(EndPlayReason);
}

void UDocRegionObserverComponent::OnUnregister()
{
	if (!IsTemplate())
	{
		if (UDocRegionSubsystem* Subsystem = UDocRegionSubsystem::Get(this))
		{
			Subsystem->UnregisterObserver(GetOwner());
		}
	}
	Super::OnUnregister();
}
