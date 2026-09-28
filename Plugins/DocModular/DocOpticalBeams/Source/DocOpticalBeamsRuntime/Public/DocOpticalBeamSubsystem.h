#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "DocOpticalBeamTypes.h"
#include "DocOpticalBeamSubsystem.generated.h"

class UDocBeamEmitterComponent;
class UDocBeamSurfaceComponent;
class UDocBeamReceiverComponent;
class UDocOpticalProfile;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocBeamPathUpdatedSignature, FName, EmitterId, const FDocBeamPath&, Path);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocBeamPathUpdatedNative, FName, const FDocBeamPath&);

/**
 * Beam registry, path solver and receiver clock.
 * Edits (emitter enable/pose, surface pose/profile, registration) queue invalidation; Tick re-solves dirty emitters
 * within MaxSolvesPerTick and advances receivers. Trace results carry emitter generation and topology revision and
 * are discarded if either changed before commit.
 */
UCLASS(BlueprintType)
class DOCOPTICALBEAMSRUNTIME_API UDocOpticalBeamSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UDocOpticalBeamSubsystem();

	static UDocOpticalBeamSubsystem* Get(const UWorld* World);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual TStatId GetStatId() const override;

	/** When false, Tick does nothing; tests and custom drivers call AdvanceSimulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	bool bAutoTick = true;

	/** Dirty emitters re-solved per update (round-robin by id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics", meta = (ClampMin = "0"))
	int32 MaxSolvesPerTick = 8;

	/** A contribution whose emitter has been waiting for a re-solve longer than this cannot grant eligibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics", meta = (ClampMin = "0.0"))
	float MaxPathStalenessSeconds = 0.25f;

	// Registration -------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RegisterEmitter(UDocBeamEmitterComponent* Emitter);

	/** Removes only this emitter's contributions. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	void UnregisterEmitter(UDocBeamEmitterComponent* Emitter);

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RegisterSurface(UDocBeamSurfaceComponent* Surface);

	UFUNCTION(BlueprintCallable, Category = "Optics")
	void UnregisterSurface(UDocBeamSurfaceComponent* Surface);

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RegisterReceiver(UDocBeamReceiverComponent* Receiver);

	UFUNCTION(BlueprintCallable, Category = "Optics")
	void UnregisterReceiver(UDocBeamReceiverComponent* Receiver);

	// Edits (all queue invalidation) ---------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult SetEmitterEnabled(FName EmitterId, bool bEnabled);

	/** New local origin/direction for an emitter. Non-finite or zero direction is refused. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult UpdateEmitterPose(FName EmitterId, const FVector& LocalOffset, const FVector& LocalDirection);

	/** Call after moving an emitter's owner actor. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult NotifyEmitterMoved(FName EmitterId);

	/** Call after moving a surface (or anything tagged as a beam surface). Invalidates every path. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult NotifySurfaceMoved(FName SurfaceId);

	/** Validates the profile first; null makes the surface a plain blocker. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult SetSurfaceProfile(FName SurfaceId, UDocOpticalProfile* Profile);

	// Solving ----------------------------------------------------------------------------------

	/** Immediate solve and commit for one emitter. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult SolveEmitterPath(UDocBeamEmitterComponent* Emitter, FDocBeamPath& OutPath);

	/** Immediate solve by id. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RequestPathRefresh(FName EmitterId);

	/** Immediate solve of every registered emitter, in id order. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RequestAllPathsRefresh();

	/** Split solve for batched/async tracing: create a ticket, trace (no side effects), then commit. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult CreatePathRequest(FName EmitterId, FDocBeamPathRequest& OutRequest);

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult TracePath(const FDocBeamPathRequest& Request, FDocBeamPath& OutPath);

	/** Conflict if the emitter generation or topology changed since the ticket; the result is discarded. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult CommitPath(const FDocBeamPathRequest& Request, const FDocBeamPath& Path);

	/** Advances the simulation clock: re-solves queued emitters, applies the staleness policy, advances receivers. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	void AdvanceSimulation(float DeltaSeconds);

	// Queries ----------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult QueryPath(FName EmitterId, FDocBeamPath& OutPath) const;

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult QueryReceiver(FName ReceiverId, FDocReceiverState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Optics")
	bool IsPathPending(FName EmitterId) const;

	/** Seconds since the emitter's path was committed (0 if none). */
	UFUNCTION(BlueprintPure, Category = "Optics")
	double GetPathAgeSeconds(FName EmitterId) const;

	UFUNCTION(BlueprintPure, Category = "Optics")
	int64 GetTopologyRevision() const { return TopologyRevision; }

	UFUNCTION(BlueprintPure, Category = "Optics")
	double GetSimulationTime() const { return SimTime; }

	UFUNCTION(BlueprintPure, Category = "Optics")
	int32 GetPendingPathCount() const { return DirtySince.Num(); }

	// Persistence ------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult CaptureReceiverState(FName ReceiverId, FDocReceiverSnapshot& OutSnapshot) const;

	/** Restores receiver facts (latched only) and queues every path for recomputation. */
	UFUNCTION(BlueprintCallable, Category = "Optics")
	FDocSystemResult RestoreReceiverState(const FDocReceiverSnapshot& Snapshot);

	// Programmatic geometry (tests and tools) ---------------------------------------------------------

	/** One-sided analytic plane: hit only when the beam travels against PlaneNormal. */
	void AddProgrammaticPlane(FName Id, const FVector& PlaneOrigin, const FVector& PlaneNormal, UDocBeamSurfaceComponent* SurfaceComp = nullptr, UDocBeamReceiverComponent* ReceiverComp = nullptr);
	void ClearProgrammaticPlanes();

	UPROPERTY(BlueprintAssignable, Category = "Optics|Events")
	FDocBeamPathUpdatedSignature OnBeamPathUpdated;

	FDocBeamPathUpdatedNative OnBeamPathUpdatedNative;

private:
	struct FProgrammaticPlane
	{
		FName Id;
		FVector Origin;
		FVector Normal;
		TWeakObjectPtr<UDocBeamSurfaceComponent> Surface;
		TWeakObjectPtr<UDocBeamReceiverComponent> Receiver;
	};

	struct FSceneHit
	{
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		double Distance = 0.0;
		UDocBeamSurfaceComponent* Surface = nullptr;
		UDocBeamReceiverComponent* Receiver = nullptr;
		UPrimitiveComponent* Primitive = nullptr;
		AActor* Actor = nullptr;
	};

	bool IntersectScene(const UDocBeamEmitterComponent& Emitter, const FVector& RayOrigin, const FVector& RayDir, double MaxDist, bool bIgnoreOwner, FSceneHit& OutHit) const;
	void TraceInternal(const UDocBeamEmitterComponent& Emitter, FDocBeamPath& OutPath) const;
	void ApplyCommittedPath(UDocBeamEmitterComponent& Emitter, const FDocBeamPath& Path);
	void MarkDirty(FName EmitterId);
	void MarkAllDirty();
	void BumpTopology();
	UDocBeamEmitterComponent* FindEmitter(FName EmitterId) const;
	UDocBeamSurfaceComponent* FindSurface(FName SurfaceId) const;
	UDocBeamReceiverComponent* FindReceiver(FName ReceiverId) const;
	TArray<FName> SortedEmitterIds() const;

	TMap<FName, TWeakObjectPtr<UDocBeamEmitterComponent>> Emitters;
	TMap<FName, TWeakObjectPtr<UDocBeamSurfaceComponent>> Surfaces;
	TMap<FName, TWeakObjectPtr<UDocBeamReceiverComponent>> Receivers;
	TArray<FProgrammaticPlane> ProgrammaticPlanes;

	TMap<FName, FDocBeamPath> CachedPaths;
	/** Emitters waiting for a re-solve -> simulation time they became dirty. */
	TMap<FName, double> DirtySince;

	int64 TopologyRevision = 1;
	int64 NextRequestId = 1;
	double SimTime = 0.0;
	int32 SolveCursor = 0;
};
