#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Volume.h"
#include "DocRegionTypes.h"
#include "DocRegionComponents.generated.h"

/**
 * A placed region instance: definition + shape, attachable to any actor. The
 * component's own world transform positions the shape. InstanceId is authored,
 * serialized, and regenerated only on editor duplication/paste — never on load,
 * BeginPlay or PIE duplication.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCREGIONSRUNTIME_API UDocRegionComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDocRegionComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	TObjectPtr<UDocRegionDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	EDocRegionShape Shape = EDocRegionShape::Box;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (EditCondition = "Shape == EDocRegionShape::Box", Units = "cm"))
	FVector BoxExtent = FVector(500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (EditCondition = "Shape == EDocRegionShape::Sphere || Shape == EDocRegionShape::Capsule", Units = "cm", ClampMin = "0.0"))
	float Radius = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (EditCondition = "Shape == EDocRegionShape::Capsule", Units = "cm", ClampMin = "0.0"))
	float CapsuleHalfHeight = 500.f;

	/** Overrides the definition priority when bOverridePriority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	bool bOverridePriority = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (EditCondition = "bOverridePriority"))
	int32 PriorityOverride = 0;

	/** Set for regions that move at runtime; their bounds are re-evaluated every update. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	bool bMovable = false;

	UFUNCTION(BlueprintPure, Category = "Doc|Regions")
	FGuid GetInstanceId() const { return InstanceId; }

	/** For runtime-spawned regions whose owner persists an id (e.g. a level generator). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	void SetInstanceId(const FGuid& InId);

	UFUNCTION(BlueprintPure, Category = "Doc|Regions")
	FGameplayTag GetRegionTag() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Regions")
	int32 GetEffectivePriority() const;

	/** Boundary inclusive within Tolerance (cm). */
	bool ContainsPoint(const FVector& WorldPoint, float Tolerance) const;
	FBox GetWorldBounds() const;
	FDocRegionInfo MakeInfo() const;

	virtual void PostInitProperties() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
#endif
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Region", meta = (DisplayName = "Instance Id"))
	FGuid InstanceId;
};

/** Brush-shaped region. Containment uses the volume brush. */
UCLASS(ClassGroup = (Doc))
class DOCREGIONSRUNTIME_API ADocRegionVolume : public AVolume
{
	GENERATED_BODY()

public:
	ADocRegionVolume(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Region")
	TObjectPtr<UDocRegionComponent> Region;
};

/** Generic box region actor usable without brush authoring. */
UCLASS(ClassGroup = (Doc))
class DOCREGIONSRUNTIME_API ADocRegionActor : public AActor
{
	GENERATED_BODY()

public:
	ADocRegionActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Region")
	TObjectPtr<UDocRegionComponent> Region;
};

/** Registers its owner as a tracked region observer while playing. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCREGIONSRUNTIME_API UDocRegionObserverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Observer")
	EDocRegionMembershipTest MembershipTest = EDocRegionMembershipTest::ReferencePoint;

	/** Offset from the actor location for ReferencePoint tests. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Observer")
	FVector ReferenceOffset = FVector::ZeroVector;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;
};
