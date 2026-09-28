#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "DocStreamingTypes.h"
#include "DocStreamingSubsystem.generated.h"

class ULevelStreamingDynamic;

/**
 * Request/lease layer over native level-instance streaming (handoff Section 8).
 *
 * - RequestChunk returns a lease; every call is an independent lease. Releasing a
 *   lease never unloads content another lease, dependency, or keep-loaded policy
 *   still needs (reported as bReleasedButResident).
 * - The dependency graph is validated (missing definitions, cycles) before any
 *   native request. Dependencies are acquired as independent internal leases in
 *   dependency order and unwound if this request later fails.
 * - Desired state (leases), observed backend state (polled from ULevelStreaming)
 *   and request outcome are tracked separately. Loaded is not Ready unless the
 *   definition's readiness (Loaded or Visible) is met.
 * - A cancelled request ignores its late native completion.
 * - Owner-bound leases are released when the owner is destroyed.
 */
UCLASS()
class DOCSTREAMINGRUNTIME_API UDocWorldStreamingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocWorldStreamingSubsystem* Get(const UObject* WorldContextObject);

	/** Request a chunk placement. bUseTransform=false uses the definition's DefaultTransform. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Streaming")
	FDocStreamingRequestInfo RequestChunk(UDocStreamingChunkDefinition* Definition, UObject* Owner, FGuid InstanceScope, bool bUseTransform, FTransform Transform);

	/** Idempotent. Cancels a pending request, or releases a ready lease. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Streaming")
	FDocStreamingRequestInfo ReleaseChunk(const FDocRequestHandle& Request);

	UFUNCTION(BlueprintPure, Category = "Doc|Streaming")
	FDocChunkStatus GetChunkStatus(const FDocChunkInstanceKey& Key) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Streaming")
	bool GetRequestInfo(const FDocRequestHandle& Request, FDocStreamingRequestInfo& OutInfo) const;

	/** Request an effective variant for a world-state group (highest priority, then most recent, wins). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Streaming")
	FDocRequestHandle RequestWorldState(UDocWorldStateGroup* Group, FGameplayTag StateTag, int32 Priority, UObject* Owner);

	/** Idempotent. Recomputes the effective variant; does not blindly deactivate a variant another owner needs. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Streaming")
	FDocSystemResult ReleaseWorldState(const FDocRequestHandle& Request);

	UFUNCTION(BlueprintPure, Category = "Doc|Streaming")
	FGameplayTag GetEffectiveWorldState(UDocWorldStateGroup* Group) const;

	/** True while the effective variant's chunks are not all Ready. */
	UFUNCTION(BlueprintPure, Category = "Doc|Streaming")
	bool IsWorldStateTransitioning(UDocWorldStateGroup* Group) const;

	/** Fires on every request state change (Ready, Failed, TimedOut, Cancelled, Released). */
	FDocStreamingRequestChanged OnRequestChanged;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Streaming")
	FDocStreamingRequestChangedDynamic OnRequestChangedDynamic;

	/** Poll native state and advance requests (Tick does this). */
	void PollNow();

	/** Test hook: simulate a native state without real level packages. */
	void DebugForceObservedState(const FDocChunkInstanceKey& Key, EDocChunkObservedState State);

	/** Test hook: skip native level loading (instances stay in Loading until forced). */
	void SetNativeBackendEnabledForTesting(bool bEnabled) { bNativeBackendEnabled = bEnabled; }

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FLease
	{
		FDocChunkInstanceKey Key;
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerWasSet = false;
		/** Internal dependency leases acquired on behalf of this lease. */
		TArray<FDocRequestHandle> DependencyLeases;
		/** Not a user request: part of another lease's dependency set or a world-state group. */
		bool bInternal = false;
		EDocStreamingRequestState State = EDocStreamingRequestState::Pending;
		double StartTime = 0.0;
		float Timeout = 0.f;
		FDocSystemResult Result;
	};

	struct FInstance
	{
		FDocChunkInstanceKey Key;
		TWeakObjectPtr<UDocStreamingChunkDefinition> Definition;
		FTransform Transform;
		TSet<FDocRequestHandle> Leases;
		EDocChunkObservedState Observed = EDocChunkObservedState::Unrequested;
		bool bUnloadRequested = false;
		FString LastFailure;
	};

	struct FWorldStateRequest
	{
		TWeakObjectPtr<UDocWorldStateGroup> Group;
		FGameplayTag StateTag;
		int32 Priority = 0;
		uint64 Sequence = 0;
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerWasSet = false;
	};

	struct FGroupState
	{
		FGameplayTag Effective;
		TArray<FDocRequestHandle> ChunkLeases;
	};

	bool ValidateGraph(const UDocStreamingChunkDefinition* Root, FString& OutError) const;
	FDocRequestHandle AcquireLease(UDocStreamingChunkDefinition* Definition, UObject* Owner, const FGuid& Scope, const FTransform& Transform, bool bInternal, FDocSystemResult& OutResult);
	void ReleaseLeaseInternal(const FDocRequestHandle& Handle, EDocStreamingRequestState FinalState, const FDocSystemResult& Result, bool bNotify);
	void BeginNativeLoad(FInstance& Instance);
	void BeginNativeUnload(FInstance& Instance);
	void RefreshObserved(FInstance& Instance);
	bool IsInstanceReady(const FInstance& Instance) const;
	bool AreDependenciesReady(const FLease& Lease) const;
	void UpdatePendingLeases();
	void RecomputeWorldState(UDocWorldStateGroup* Group);
	FDocStreamingRequestInfo MakeInfo(const FDocRequestHandle& Handle, const FLease& Lease) const;
	void Notify(const FDocStreamingRequestInfo& Info);
	double Now() const;

	TDocHandleTable<FLease> Leases;
	TMap<FDocChunkInstanceKey, FInstance> Instances;
	TMap<FDocChunkInstanceKey, TWeakObjectPtr<ULevelStreamingDynamic>> NativeLevels;

	/** GC root for native streaming objects we created. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> OwnedStreamingLevels;

	TDocHandleTable<FWorldStateRequest> WorldStateRequests;
	TMap<TWeakObjectPtr<UDocWorldStateGroup>, FGroupState> Groups;
	uint64 NextWorldStateSequence = 1;
	bool bNativeBackendEnabled = true;
	bool bShuttingDown = false;
};

/** Requests chunks by distance from its owner, with load/unload hysteresis. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCSTREAMINGRUNTIME_API UDocStreamingSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocStreamingSourceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	TArray<TObjectPtr<UDocStreamingChunkDefinition>> Chunks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = "0.0", Units = "s"))
	float EvaluationInterval = 0.5f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TMap<TWeakObjectPtr<UDocStreamingChunkDefinition>, FDocRequestHandle> Active;
	float TimeSinceEvaluation = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocChunkWaitResult, const FDocStreamingRequestInfo&, Info);

/** Blueprint async: request a chunk and wait until Ready (or a terminal failure). The lease stays held on success. */
UCLASS()
class DOCSTREAMINGRUNTIME_API UDocWaitChunkReadyAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Doc|Streaming", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UDocWaitChunkReadyAction* RequestChunkAndWait(UObject* WorldContextObject, UDocStreamingChunkDefinition* Definition, UObject* Owner, FGuid InstanceScope);

	UPROPERTY(BlueprintAssignable) FDocChunkWaitResult OnReady;
	UPROPERTY(BlueprintAssignable) FDocChunkWaitResult OnFailed;

	virtual void Activate() override;

private:
	void HandleChanged(const FDocStreamingRequestInfo& Info);
	void Finish(const FDocStreamingRequestInfo& Info, bool bReady);

	TWeakObjectPtr<UObject> WorldContext;
	TWeakObjectPtr<UDocStreamingChunkDefinition> Definition;
	TWeakObjectPtr<UObject> Owner;
	FGuid Scope;
	FDocRequestHandle Handle;
	FDelegateHandle ChangedHandle;
	TWeakObjectPtr<UDocWorldStreamingSubsystem> Subsystem;
	bool bFinished = false;
};
