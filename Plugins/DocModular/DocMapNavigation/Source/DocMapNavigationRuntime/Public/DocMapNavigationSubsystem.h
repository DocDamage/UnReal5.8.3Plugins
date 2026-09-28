#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/DeveloperSettings.h"
#include "DocMapTypes.h"
#include "DocMapNavigationSubsystem.generated.h"

/**
 * Feature-internal world marker registry (handoff 3.1): shared detached marker
 * descriptors, one registration per source per world. Views (local players, a
 * server replication adapter) read permission-filtered snapshots.
 */
UCLASS()
class DOCMAPNAVIGATIONRUNTIME_API UDocMapMarkerRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocMapMarkerRegistry* Get(const UObject* WorldContextObject);

	/**
	 * Register (or same-source upsert) a marker. A different source registering an existing
	 * (MarkerId, InstanceScope) fails with Conflict (no last-writer-wins).
	 */
	FDocSystemResult RegisterMarker(UObject* Source, FName MarkerId, const FGuid& InstanceScope, FName MapId,
		UDocMapMarkerDefinition* Definition, const FVector& EngineLocation, FDocRequestHandle& OutHandle, const FDocOwnerScope& OwnerScope = FDocOwnerScope());

	/** Releases this source's registration; lifetime policy decides what remains. Idempotent. */
	FDocSystemResult UnregisterMarker(const FDocRequestHandle& Handle, UObject* Source);

	/** Same-source update. ExpectedRevision < 0 = upsert; otherwise a mismatch is Conflict. */
	FDocSystemResult UpdateMarker(const FDocRequestHandle& Handle, UObject* Source, const FVector& EngineLocation, float Yaw, int64 ExpectedRevision = -1);

	/** Shared visibility (all views). Per-player hiding lives in the local player subsystem. */
	FDocSystemResult SetMarkerVisibility(FName MarkerId, const FGuid& InstanceScope, bool bVisible);

	/** Authorized explicit removal of a retained/persistent marker. */
	FDocSystemResult RemovePersistentMarker(FName MarkerId, const FGuid& InstanceScope);

	/** Provider-managed markers: the provider updates or removes after its source left. */
	FDocSystemResult ProviderUpdate(FName MarkerId, const FGuid& InstanceScope, const FVector& EngineLocation, bool bRemove);

	/** Custom (host/player) marker without a source; persisted through capture/restore. */
	FDocSystemResult AddCustomMarker(FName MarkerId, FName MapId, UDocMapMarkerDefinition* Definition, const FVector& EngineLocation, const FDocOwnerScope& OwnerScope);

	bool GetMarker(FName MarkerId, const FGuid& InstanceScope, FDocMapMarkerState& Out) const;
	/** Immutable snapshot of every record (views filter it). */
	TArray<FDocMapMarkerState> Snapshot() const;
	int32 Num() const { return Markers.Num(); }
	int64 GetRegistryRevision() const { return RegistryRevision; }

	// ---- Persistence (versioned; feature-owned payload) ----
	/** Persistent/custom markers only (never handles, actor pointers or widget coordinates). */
	TArray<FDocMapMarkerState> CapturePersistent() const;
	/** Stage + apply; unknown definitions are quarantined (kept, not shown), nothing is replayed as new. */
	void RestorePersistent(const TArray<FDocMapMarkerState>& Records);

	FDocMarkerChangedNative OnMarkerChangedNative;
	/** ProviderManaged source left: provider must update or remove. */
	FDocMarkerChangedNative OnProviderAttentionNative;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	struct FKey
	{
		FName MarkerId;
		FGuid Scope;
		friend bool operator==(const FKey& A, const FKey& B) { return A.MarkerId == B.MarkerId && A.Scope == B.Scope; }
		friend uint32 GetTypeHash(const FKey& K) { return HashCombine(GetTypeHash(K.MarkerId), GetTypeHash(K.Scope)); }
	};
	struct FRegistration
	{
		FKey Key;
		FObjectKey Source;
	};

	void Changed(const FDocMapMarkerState& State, EDocMarkerChange Change);

	TMap<FKey, FDocMapMarkerState> Markers;
	UPROPERTY() TArray<TObjectPtr<UDocMapMarkerDefinition>> DefinitionRefs;
	TMap<FKey, FObjectKey> Owners;
	TDocHandleTable<FRegistration> Registrations;
	int64 RegistryRevision = 0;
};

/** Registers its owner as a marker; dynamic owners update by threshold and cadence (no per-frame projection). */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCMAPNAVIGATIONRUNTIME_API UDocMapMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDocMapMarkerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") TObjectPtr<UDocMapMarkerDefinition> Definition;
	/** Stable id; empty = owner name (only stable for level-placed actors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FName MarkerId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FName MapId;
	/** Set by level-instance spawners so repeated placements do not collide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") FGuid InstanceScope;
	/** Static markers never tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marker") bool bDynamic = false;

	UFUNCTION(BlueprintPure, Category = "Doc|Map") FDocRequestHandle GetRegistration() const { return Registration; }
	UFUNCTION(BlueprintPure, Category = "Doc|Map") FDocSystemResult GetRegistrationResult() const { return RegistrationResult; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FDocRequestHandle Registration;
	FDocSystemResult RegistrationResult;
	FVector LastSent = FVector::ZeroVector;
};

/**
 * Per-local-player map state (handoff 3.4–3.6): owner scope, filters, tracking, waypoints,
 * discovery (region ids + chunked radius coverage), floors, compass and off-screen data.
 */
UCLASS()
class DOCMAPNAVIGATIONRUNTIME_API UDocMapNavigationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UDocMapNavigationSubsystem* Get(const ULocalPlayer* Player);

	/** Trusted host context: the durable owner of this player's map state. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void SetOwnerScope(const FDocOwnerScope& Scope) { OwnerScope = Scope; }
	UFUNCTION(BlueprintPure, Category = "Doc|Map") FDocOwnerScope GetOwnerScope() const { return OwnerScope; }

	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void RegisterMap(UDocMapDefinition* Map);
	UFUNCTION(BlueprintPure, Category = "Doc|Map") UDocMapDefinition* FindMap(FName MapId) const;

	// ---- Queries (immutable snapshots, bounded, deterministic order) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") TArray<FDocMapMarkerState> GetVisibleMarkers(const FDocMarkerQuery& Query) const;
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") TArray<FDocMapMarkerState> GetMarkersByTag(FName MapId, FGameplayTag Tag, bool bExact) const;
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") bool GetNearestMarker(const FDocMarkerQuery& Query, FDocMapMarkerState& Out) const;

	// ---- Per-player marker state ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void SetMarkerHidden(FName MarkerId, bool bHidden);
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void SetMarkerTracked(FName MarkerId, bool bTracked);
	UFUNCTION(BlueprintPure, Category = "Doc|Map") bool IsMarkerTracked(FName MarkerId) const { return Tracked.Contains(MarkerId); }
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void SetTagFilter(FGameplayTag Tag, bool bHidden);

	/** Owner-local waypoint (never shared with other players). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") FDocSystemResult SetWaypoint(FName MapId, FVector Location, FName FloorId);
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void ClearWaypoint(FName MapId);
	UFUNCTION(BlueprintPure, Category = "Doc|Map") bool GetWaypoint(FName MapId, FVector& OutLocation, FName& OutFloor) const;

	// ---- Discovery (owner + map scoped; not line-of-sight) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") FDocSystemResult DiscoverLocation(FName MapId, FName LocationId);
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") FDocSystemResult UndiscoverLocation(FName MapId, FName LocationId);
	UFUNCTION(BlueprintPure, Category = "Doc|Map") bool IsLocationDiscovered(FName MapId, FName LocationId) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Map") TArray<FName> GetDiscoveredLocations(FName MapId) const;
	/** Reveal radius coverage (cells whose centres are within Radius). Returns newly revealed cell count. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") int32 RevealRadius(FName MapId, FVector WorldLocation, double Radius);
	UFUNCTION(BlueprintPure, Category = "Doc|Map") bool IsPointRevealed(FName MapId, FVector WorldLocation) const;
	/** Explicit area reset of radius coverage (the only way coverage is erased). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") int32 ResetRevealedArea(FName MapId, FVector WorldLocation, double Radius);
	int32 GetAllocatedChunkCount(FName MapId) const;

	// ---- Floors, compass, off-screen ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") FName UpdateFloor(FName MapId, double WorldZ);
	/** Explicit/provider override for stacked interiors (None = automatic). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") void SetFloorOverride(FName MapId, FName FloorId);
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") TArray<FDocCompassEntry> GetCompass(const FDocMarkerQuery& Query, FVector From, float HeadingYaw) const;
	UFUNCTION(BlueprintCallable, Category = "Doc|Map") TArray<FDocOffscreenIndicator> GetOffscreenIndicators(const FDocMarkerQuery& Query, const FDocMapViewProjection& View) const;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Map") FDocDiscoveryEvent OnLocationDiscovered;

	// ---- Persistence (versioned; restore never replays discovery events) ----
	static constexpr int32 SchemaVersion = 1;
	TArray<uint8> CaptureState() const;
	/** Fails (state untouched) when fog schema differs, unless bDiscardIncompatibleFog explicitly confirms a reset. */
	FDocSystemResult RestoreState(const TArray<uint8>& Bytes, bool bDiscardIncompatibleFog = false);

	void InitializeForTesting(UWorld* World) { WorldOverride = World; }
	virtual UWorld* GetWorld() const override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

private:
	struct FChunk
	{
		TBitArray<> Cells;
	};
	struct FMapState
	{
		TSet<FName> Locations;
		TMap<FIntPoint, FChunk> Chunks;
		double CellSize = 2000.0;
		TOptional<TPair<FVector, FName>> Waypoint;
		FName CurrentFloor;
		FName FloorOverride;
	};

	bool PassesFilters(const FDocMapMarkerState& M, const FDocMarkerQuery& Query, const UDocMapDefinition* Map) const;
	bool IsMarkerRevealed(const FDocMapMarkerState& M, const UDocMapDefinition* Map) const;
	FMapState& StateFor(FName MapId);
	bool CellOf(const UDocMapDefinition* Map, const FVector& World, FIntPoint& OutCell) const;

	FDocOwnerScope OwnerScope;
	TMap<FName, TObjectPtr<UDocMapDefinition>> MapsById;
	UPROPERTY() TArray<TObjectPtr<UDocMapDefinition>> MapRefs;
	TMap<FName, FMapState> States;
	TSet<FName> Hidden;
	TSet<FName> Tracked;
	FGameplayTagContainer HiddenTags;
	TWeakObjectPtr<UWorld> WorldOverride;
};

/** Reveals radius coverage around its owner for a local player's map (bounded cadence). */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCMAPNAVIGATIONRUNTIME_API UDocMapDiscoveryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocMapDiscoveryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery") FName MapId;
	/** 0 = the map's DefaultRevealRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery", meta = (Units = "cm")) float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery") float IntervalSeconds = 0.5f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

/** Project settings. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Map Navigation"))
class DOCMAPNAVIGATIONRUNTIME_API UDocMapNavigationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Default presentation budget for marker queries. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1")) int32 DefaultMaxResults = 128;
	/** Radius coverage chunk edge in cells (memory bounded by allocated chunks). */
	UPROPERTY(Config, EditAnywhere, Category = "Discovery", meta = (ClampMin = "4", ClampMax = "256")) int32 ChunkCells = 32;
};
