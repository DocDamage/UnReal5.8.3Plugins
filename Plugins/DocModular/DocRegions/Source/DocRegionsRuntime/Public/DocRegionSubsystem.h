#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocRegionTypes.h"
#include "DocSystemResult.h"
#include "DocRegionSubsystem.generated.h"

class UDocRegionComponent;

/** Observer registration options. */
USTRUCT(BlueprintType)
struct DOCREGIONSRUNTIME_API FDocRegionObserverOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	EDocRegionMembershipTest MembershipTest = EDocRegionMembershipTest::ReferencePoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	FVector ReferenceOffset = FVector::ZeroVector;
};

/**
 * Logical region registry and tracked membership (handoff Section 7).
 *
 * - Tracks only explicitly registered observers, never every actor.
 * - Membership is geometric (reference point or bounds), so several primitives of
 *   one observer can never double-enter or exit early.
 * - After recomputing an observer's full set, emits exits, then entries, then one
 *   primary-changed event, each with a reason.
 * - Primary selection: higher priority, then deeper explicit hierarchy, then more
 *   specific tag (more segments), then smaller bounds volume, then InstanceId.
 * - Static regions live in a uniform grid; movable regions are checked each update.
 * - Geometry for unloaded regions is not evaluated; queries only report loaded,
 *   registered regions.
 */
UCLASS()
class DOCREGIONSRUNTIME_API UDocRegionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocRegionSubsystem* Get(const UObject* WorldContextObject);

	// ---- Registration ----
	void RegisterRegion(UDocRegionComponent* Region);
	void UnregisterRegion(UDocRegionComponent* Region);

	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	FDocSystemResult RegisterObserver(AActor* Observer, FDocRegionObserverOptions Options);

	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	FDocSystemResult UnregisterObserver(AActor* Observer);

	/** Recompute every observer now (Tick does this on UpdateInterval). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	void UpdateAllObservers();

	// ---- Queries ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	FDocRegionInfo GetRegionAtLocation(const FVector& Location) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	TArray<FDocRegionInfo> GetRegionsAtLocation(const FVector& Location) const;

	/** Current tracked membership (empty if the actor is not an observer). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	TArray<FDocRegionInfo> GetRegionsForActor(const AActor* Actor) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	FDocRegionInfo GetPrimaryRegionForActor(const AActor* Actor) const;

	/** True when the tracked actor is in a region whose tag matches (children included when bIncludeChildTags). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	bool IsActorInRegion(const AActor* Actor, FGameplayTag RegionTag, bool bIncludeChildTags = true) const;

	/** Tracked, loaded observers in any region matching the tag. Not an omniscient list. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	TArray<AActor*> GetActorsInRegion(FGameplayTag RegionTag, bool bIncludeChildTags = true) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	TArray<FDocRegionInfo> FindRegionsByTag(FGameplayTag RegionTag, bool bIncludeChildTags = false) const;

	/** Unique lookup: NotFound when none, Conflict when ambiguous (several instances). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Regions")
	FDocSystemResult FindRegionByTag(FGameplayTag RegionTag, FDocRegionInfo& OutRegion) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Regions")
	int32 GetRegisteredRegionCount() const { return Regions.Num(); }

	// ---- Events ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Regions")
	FDocRegionMembershipEvent OnRegionEntered;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Regions")
	FDocRegionMembershipEvent OnRegionExited;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Regions")
	FDocPrimaryRegionChangedEvent OnPrimaryRegionChanged;

	// ---- Tuning ----
	/** Seconds between observer updates (0 = every tick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	float UpdateInterval = 0.1f;

	/** Movement larger than this between updates is reported as Teleport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	float TeleportDistance = 1000.f;

	/** Boundary tolerance in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	float BoundaryTolerance = 0.5f;

	/** Uniform grid cell size (cm) for static regions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Regions")
	float GridCellSize = 5000.f;

	/** Compare primary candidates (true = A beats B). Exposed for tests and tools. */
	static bool IsBetterPrimary(const FDocRegionInfo& A, const UDocRegionComponent* AComp, const FDocRegionInfo& B, const UDocRegionComponent* BComp);

	//~ Subsystem
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FObserver
	{
		TWeakObjectPtr<AActor> Actor;
		FDocRegionObserverOptions Options;
		TArray<FGuid> Current;       // sorted by InstanceId
		FGuid Primary;
		FVector LastLocation = FVector::ZeroVector;
		bool bInitialized = false;
	};

	struct FCellKey
	{
		int32 X = 0, Y = 0, Z = 0;
		friend bool operator==(const FCellKey& A, const FCellKey& B) { return A.X == B.X && A.Y == B.Y && A.Z == B.Z; }
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y)), ::GetTypeHash(K.Z)); }
	};

	void GatherCandidateRegions(const FBox& QueryBox, TArray<UDocRegionComponent*>& Out) const;
	void ComputeMembership(const FObserver& Observer, TArray<UDocRegionComponent*>& OutRegions) const;
	void UpdateObserver(FObserver& Observer, EDocRegionTransitionReason ForcedReason, bool bForceReason);
	void AddToGrid(UDocRegionComponent* Region);
	void RemoveFromGrid(UDocRegionComponent* Region);
	FDocRegionInfo InfoFor(const FGuid& Id) const;
	UDocRegionComponent* ChoosePrimary(const TArray<UDocRegionComponent*>& Members) const;
	FCellKey CellFor(const FVector& P) const;

	TMap<FGuid, TWeakObjectPtr<UDocRegionComponent>> Regions;
	/** Last known info of each registered region (used for exit events after unload). */
	TMap<FGuid, FDocRegionInfo> RegionInfoCache;
	TMap<FCellKey, TArray<FGuid>> Grid;
	TMap<FGuid, TArray<FCellKey>> RegionCells;
	TArray<FGuid> MovableRegions;
	TArray<FObserver> Observers;
	float TimeSinceUpdate = 0.f;
};
