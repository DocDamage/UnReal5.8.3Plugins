#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocAcousticTypes.h"
#include "DocAcousticSpaceComponent.h"
#include "DocAcousticPortalComponent.h"
#include "DocAcousticEmitterComponent.h"
#include "IDocAcousticPlaybackAdapter.h"
#include "DocAcousticSpaceSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnAcousticPathResolved, FName, EmitterId, const FDocAcousticPathResult&, Result);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnAcousticPathResolvedNative, FName, const FDocAcousticPathResult&);

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticUpdateStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 ClaimedEmitters = 0;

	/** Emitters whose path was re-evaluated this update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 EvaluatedEmitters = 0;

	/** Claimed emitters left for a later update by MaxEmitterEvaluationsPerUpdate (staggered). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 DeferredEmitters = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 CacheHits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 AdapterApplies = 0;
};

/**
 * Authored space/portal graph, strongest-path transmission queries, and the single-listener parameter pipeline.
 * Only emitters holding an acoustic claim are ever written to the playback adapter.
 */
UCLASS()
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticSpaceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// Topology -----------------------------------------------------------------------------------

	/** Refuses a None id, invalid bounds, or an id held by another live space (Conflict). Replaces a retained descriptor. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult RegisterSpace(UDocAcousticSpaceComponent* Space);

	/** Removes the space, or keeps a component-free descriptor when bRetainDescriptorOnUnload is set. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void UnregisterSpace(UDocAcousticSpaceComponent* Space);

	/** Refuses invalid endpoints/values or a duplicate id. A retained descriptor with the same id is replaced and its openness kept. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult RegisterPortal(UDocAcousticPortalComponent* Portal);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void UnregisterPortal(UDocAcousticPortalComponent* Portal);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult SetPortalOpenness(FName PortalId, float NewOpenness);

	/** Called by portal components when openness or enabled state changes; invalidates cached paths. */
	void NotifyPortalChanged(FName PortalId);

	/** Unknown portal endpoints, ambiguous equal-priority overlaps, invalid portal values. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult ValidateTopology(TArray<FString>& OutIssues) const;

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	TArray<FName> GetRetainedDescriptorIds() const;

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FName ResolveSpaceForLocation(const FVector& Location, FName PreviousSpaceId = NAME_None, float HysteresisMargin = 50.0f) const;

	// Emitters and listeners ----------------------------------------------------------------------

	/** Refuses a None id or an id held by another live emitter. Idempotent for the same component. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult RegisterEmitter(UDocAcousticEmitterComponent* Emitter);

	/** Releases the emitter's claims (restoring its baseline through the adapter) and drops tracked state. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void UnregisterEmitter(UDocAcousticEmitterComponent* Emitter);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void UpdateListener(FName ListenerId, const FVector& Location);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void RemoveListener(FName ListenerId);

	/** Selects the one listener the audible mix follows. NotFound for an unknown listener. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult SetMixListener(FName ListenerId);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FName GetMixListener() const;

	/** Separate per-listener mixes are a distinct capability; the base always answers Unsupported. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult RequestIndependentListenerMixes();

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FName GetTrackedEmitterSpace(FName EmitterId) const;

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FName GetTrackedListenerSpace(FName ListenerId) const;

	// Queries ----------------------------------------------------------------------------------

	/**
	 * Strongest admissible path. Failed only when the visited budget is hit without approximation;
	 * every other outcome (including Blocked/Unresolved) succeeds with the documented floor policy.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult QueryTransmission(const FDocAcousticPathQuery& Query, FDocAcousticPathResult& OutResult);

	/** Last result computed for this emitter/listener pair, with all gain stages. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	bool GetDebugPath(FName EmitterId, FName ListenerId, FDocAcousticPathResult& OutResult) const;

	// Claims ------------------------------------------------------------------------------------

	/** One acoustic claim per registered emitter. A repeat request returns the existing claim as NoChange. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult AcquireAcousticClaim(FName EmitterId, FGuid& OutClaimId);

	/** Removes acoustic influence and returns the emitter to the host's current baseline. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	FDocSystemResult ReleaseAcousticClaim(const FGuid& ClaimId);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool HasAcousticClaim(FName EmitterId) const;

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	int32 GetActiveClaimCount() const { return ActiveClaims.Num(); }

	// Update ------------------------------------------------------------------------------------

	/**
	 * Re-evaluates up to MaxEmitterEvaluationsPerUpdate claimed emitters (staggered) against the mix listener,
	 * smooths every claimed emitter, and writes claimed emitters to the adapter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void AdvanceSmoothing(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FDocAcousticUpdateStats GetLastUpdateStats() const { return LastUpdateStats; }

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SetPlaybackAdapter(TScriptInterface<IDocAcousticPlaybackAdapter> InAdapter);

	// Programmatic topology (tests and tools) ------------------------------------------------------

	FDocSystemResult AddProgrammaticSpace(FName SpaceId, const FBox& Bounds, int32 Priority = 0);
	FDocSystemResult AddProgrammaticPortal(
		FName PortalId,
		FName SpaceA,
		FName SpaceB,
		float Openness = 1.0f,
		float MinGain = 0.05f,
		float MaxGain = 1.0f,
		float MinCutoff = 400.0f,
		float MaxCutoff = 20000.0f,
		const FVector& Location = FVector::ZeroVector
	);
	void ClearProgrammaticTopology();

	int64 GetTopologyRevision() const { return TopologyRevision; }

	// Policy -----------------------------------------------------------------------------------

	/** Gain used for Blocked, Unresolved and budget-failed results. Never 1.0 by accident. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float DisconnectedFloorGain = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	float DisconnectedCutoffHz = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	EDocAcousticUnresolvedPolicy UnresolvedPolicy = EDocAcousticUnresolvedPolicy::UseFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName ExteriorSpaceId = NAME_None;

	/** Nonnegative cost per unit of route length through portal positions. 0 = gain only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0"))
	float DistanceCostPerUnit = 0.0f;

	/** Margin used for tracked emitter/listener membership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0"))
	float MembershipHysteresis = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bAutoUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bAutoEvaluateClaimedEmitters = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "1"))
	int32 MaxEmitterEvaluationsPerUpdate = 16;

	/** A move larger than this between evaluations snaps smoothing instead of sweeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0"))
	float TeleportDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "1"))
	int32 MaxCacheEntries = 256;

	UPROPERTY(BlueprintAssignable, Category = "Acoustics")
	FDocOnAcousticPathResolved OnPathResolved;

	FDocOnAcousticPathResolvedNative OnPathResolvedNative;

private:
	struct FSpaceEntry
	{
		FName SpaceId;
		FBox Bounds = FBox(ForceInit);
		int32 Priority = 0;
		TWeakObjectPtr<UDocAcousticSpaceComponent> Component;
		bool bRetained = false;
	};

	struct FPortalEntry
	{
		FDocAcousticPortalLink Link;
		TWeakObjectPtr<UDocAcousticPortalComponent> Component;
		bool bRetained = false;
	};

	struct FListenerEntry
	{
		FVector Location = FVector::ZeroVector;
		double UpdateTime = 0.0;
		bool bJumped = false;
	};

	TMap<FName, FSpaceEntry> Spaces;
	TMap<FName, FPortalEntry> Portals;
	TMap<FName, TWeakObjectPtr<UDocAcousticEmitterComponent>> Emitters;
	TMap<FName, FListenerEntry> Listeners;
	TMap<FName, FName> TrackedMembership; // "E:<id>" / "L:<id>" -> space
	TMap<FGuid, FDocAcousticClaim> ActiveClaims;
	TMap<FString, FDocAcousticPathResult> PathCache;
	TMap<FString, FDocAcousticPathResult> DebugPaths;

	UPROPERTY()
	TScriptInterface<IDocAcousticPlaybackAdapter> PlaybackAdapter;

	FName MixListenerId = NAME_None;
	FDocAcousticUpdateStats LastUpdateStats;
	int64 TopologyRevision = 1;
	int64 CacheRevision = 0;
	int32 EmitterGenerationCounter = 0;
	int32 EvaluationCursor = 0;
	int32 PendingCacheHits = 0;

	FBox GetSpaceBounds(const FSpaceEntry& Entry) const;
	int32 GetSpacePriority(const FSpaceEntry& Entry) const;
	FDocAcousticPortalLink GetPortalLink(const FPortalEntry& Entry) const;
	void BumpTopology();
	FName ResolveTracked(const FString& Key, const FVector& Location, FName ExplicitSpace);
	void ApplyFloor(FDocAcousticPathResult& Result, EDocAcousticPathStatus Status, FName Reason, const FString& Message) const;
	float ComputeRouteDistance(const FVector& From, const TArray<FName>& PortalIds, const FVector& To) const;
	void ReleaseClaimInternal(const FDocAcousticClaim& Claim);
};
