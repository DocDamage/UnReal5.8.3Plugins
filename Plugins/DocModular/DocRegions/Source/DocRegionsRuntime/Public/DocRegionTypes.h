#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Interface.h"
#include "DocRegionTypes.generated.h"

class UDocRegionComponent;

/**
 * Immutable region definition (handoff 7.2). One definition may be used by many
 * placed region instances; queries distinguish definition tag from instance id.
 * Metadata never hard-references optional consumer classes: consumers read generic
 * tags/values or validated extension structs.
 */
UCLASS(BlueprintType)
class DOCREGIONSRUNTIME_API UDocRegionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Semantic tag, e.g. Region.World.ZoneA.Building.RoomA. Hierarchy is organisation, not proof of containment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (Categories = "Region"))
	FGameplayTag RegionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	FText DisplayName;

	/** Higher wins primary selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	FGameplayTagContainer RegionTraits;

	/** Optional explicit logical parent (validated for cycles). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	TSoftObjectPtr<UDocRegionDefinition> ExplicitParent;

	/** Generic metadata for consumers (audio profile names, streaming chunk names...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	TMap<FName, FString> Metadata;

	/** Typed extension records owned by bridges (e.g. a Regions→Audio bridge struct). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	TArray<FInstancedStruct> Extensions;

	/** Number of explicit ancestors (0 = root). -1 when a cycle is detected. */
	int32 ComputeDepth() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UENUM(BlueprintType)
enum class EDocRegionShape : uint8
{
	Box,
	Sphere,
	Capsule,
	/** Delegates containment to the owner's brush (ADocRegionVolume) or an IDocRegionShapeProvider on the owner. */
	Custom
};

UENUM(BlueprintType)
enum class EDocRegionTransitionReason : uint8
{
	/** First evaluation of an observer (e.g. spawned inside). */
	Initial,
	Movement,
	/** Observer moved more than the teleport threshold since the last evaluation. */
	Teleport,
	/** A region was registered while the observer was inside it. */
	RegionAdded,
	/** The region unloaded or was unregistered. */
	RegionUnloaded,
	/** The observer was unregistered or destroyed. */
	ObserverRemoved,
	/** The region's shape moved or changed. */
	RegionMoved
};

UENUM(BlueprintType)
enum class EDocRegionMembershipTest : uint8
{
	/** Observer reference point (actor location + offset). Boundary points count as inside (within tolerance). */
	ReferencePoint,
	/** Observer's component bounds (AABB) intersect the region's bounds (conservative). */
	BoundsOverlap
};

/** Snapshot of a region instance for events and queries. */
USTRUCT(BlueprintType)
struct DOCREGIONSRUNTIME_API FDocRegionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") FGameplayTag RegionTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") int32 Priority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") int32 Depth = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") TWeakObjectPtr<UDocRegionComponent> Component;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Regions") TSoftObjectPtr<UDocRegionDefinition> Definition;

	bool IsValid() const { return InstanceId.IsValid(); }
	friend bool operator==(const FDocRegionInfo& A, const FDocRegionInfo& B) { return A.InstanceId == B.InstanceId; }
};

/** Optional custom shape (e.g. spline corridor, polygon) implemented on the owning actor. */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocRegionShapeProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCREGIONSRUNTIME_API IDocRegionShapeProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Regions")
	bool RegionContainsPoint(const FVector& WorldPoint, float Tolerance) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Regions")
	FBox GetRegionBounds() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocRegionMembershipEvent, AActor*, Observer, const FDocRegionInfo&, Region, EDocRegionTransitionReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDocPrimaryRegionChangedEvent, AActor*, Observer, const FDocRegionInfo&, NewPrimary, const FDocRegionInfo&, OldPrimary, EDocRegionTransitionReason, Reason);
