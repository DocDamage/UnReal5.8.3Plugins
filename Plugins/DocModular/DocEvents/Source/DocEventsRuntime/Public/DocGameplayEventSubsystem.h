#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocEventsTypes.h"
#include "DocSystemResult.h"
#include "Interfaces/DocWorldStateProvider.h"
#include "DocGameplayEventSubsystem.generated.h"

/** Runtime limits (copied from UDocEventsSettings at Initialize; tests may override). */
struct FDocEventBusLimits
{
	int32 MaxEventsPerDrain = 1000;
	int32 MaxQueuedEvents = 4096;
	int32 MaxRetainedValues = 512;
	float RetainedTimeToLiveSeconds = 0.f;
	int32 HistoryCapacity = 1024;
	FGameplayTagContainer AutoRetainTags;
	FGameplayTagContainer HistoryTrackedTags;
	bool bCaptureDebugHistory = false;
	int32 DebugHistoryCapacity = 256;
};

/** A scheduled (next-frame or delayed) event. */
USTRUCT()
struct FDocScheduledEvent
{
	GENERATED_BODY()

	UPROPERTY() FDocGameplayEvent Event;
	UPROPERTY() int64 OperationId = 0;
	UPROPERTY() EDocClockDomain Clock = EDocClockDomain::WorldGameplay;
	UPROPERTY() double DueTime = 0.0;
	/** Fire on the next tick regardless of time. */
	UPROPERTY() bool bNextFrame = false;
};

/**
 * World-scoped typed event bus (handoff Section 6).
 *
 * Threading: game thread only. Delivery order: priority (desc), then registration
 * order. A dispatch snapshot is taken per event; listeners removed during delivery
 * are skipped, listeners added during delivery see only later events. Broadcasts
 * made from inside a listener are queued and drained after the current event.
 *
 * Lifetime: one instance per game/PIE world. Nothing crosses worlds. On
 * Deinitialize, every subscription, scheduled event and retained value is dropped
 * and OnBusShutdown fires so async waiters can finish as Cancelled.
 *
 * Implements IDocWorldStateProvider so retained State.* values can answer
 * world-state queries (Yes when a retained value exists for the tag in World scope).
 */
UCLASS()
class DOCEVENTSRUNTIME_API UDocGameplayEventSubsystem : public UTickableWorldSubsystem, public IDocWorldStateProvider
{
	GENERATED_BODY()

public:
	static UDocGameplayEventSubsystem* Get(const UObject* WorldContextObject);

	// ---- Broadcast ----

	/** Validate and deliver (or queue, when called during dispatch). Returns InvalidInput for a missing tag or schema mismatch. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocSystemResult BroadcastEvent(const FDocGameplayEvent& Event);

	/** Deliver on the next tick. Returns a handle usable with CancelScheduledEvent. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocRequestHandle BroadcastNextFrame(const FDocGameplayEvent& Event);

	/** Deliver after Delay seconds measured in Clock (WorldGameplay pauses with the world; RealTime does not). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocRequestHandle BroadcastAfterDelay(const FDocGameplayEvent& Event, float DelaySeconds, EDocClockDomain Clock = EDocClockDomain::WorldGameplay);

	/** Idempotent. Returns NoChange if the event already fired or was cancelled. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocSystemResult CancelScheduledEvent(const FDocRequestHandle& Handle);

	// ---- Subscribe ----

	FDocRequestHandle SubscribeNative(FGameplayTag EventTag, const FDocEventListenOptions& Options, FDocGameplayEventNativeDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category = "Doc|Events", meta = (DisplayName = "Subscribe To Gameplay Event"))
	FDocRequestHandle Subscribe(FGameplayTag EventTag, FDocEventListenOptions Options, FDocGameplayEventDynamicDelegate Delegate);

	/** Idempotent; safe during dispatch (the listener will not be called again). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocSystemResult Unsubscribe(const FDocRequestHandle& Handle);

	/** Remove every subscription owned by Owner. Returns count removed. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	int32 UnsubscribeAllForOwner(const UObject* Owner);

	UFUNCTION(BlueprintPure, Category = "Doc|Events")
	bool IsSubscribed(const FDocRequestHandle& Handle) const;

	// ---- Retained values (runtime only) ----

	/** Store Event as the latest value for its exact tag + scope and broadcast it. Rejects payloads holding strong object references. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocSystemResult UpdateRetainedValue(const FDocGameplayEvent& Event, bool bBroadcast = true);

	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	bool GetLatestPayload(FGameplayTag EventTag, const FDocEventScope& Scope, FDocRetainedEventValue& OutValue) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	FDocSystemResult ClearRetainedValue(FGameplayTag EventTag, const FDocEventScope& Scope);

	// ---- History ----

	/**
	 * Did a matching event occur within the bounded history? Unknown when the tag is
	 * not tracked, history is disabled, or matching entries may have been evicted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	EDocTriState HasEventOccurred(FGameplayTag EventTag, EDocEventTagMatch Match, bool bFilterByScope, const FDocEventScope& Scope) const;

	// ---- Schemas ----

	/** Declare the expected payload struct for an exact tag (overrides settings). Null clears. */
	void RegisterPayloadSchema(FGameplayTag EventTag, const UScriptStruct* PayloadStruct);

	// ---- Diagnostics ----

	UFUNCTION(BlueprintPure, Category = "Doc|Events")
	FDocEventBusStats GetStats() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Events")
	TArray<FDocEventDebugRecord> GetDebugHistory() const;

	/** Fires once when the bus shuts down (world teardown). */
	FSimpleMulticastDelegate OnBusShutdown;

	/** Test hook: replace limits. */
	void SetLimitsForTesting(const FDocEventBusLimits& InLimits) { Limits = InLimits; }

	/** Process due scheduled events and deferred queue now (also called from Tick). */
	void ProcessPendingWork();

	//~ USubsystem / UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	//~ IDocWorldStateProvider
	virtual EDocTriState QueryDocWorldState_Implementation(FGameplayTag StateTag, EDocTagMatchMode MatchMode) const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FSubscription
	{
		FGameplayTag Tag;
		FDocEventListenOptions Options;
		FDocGameplayEventNativeDelegate Native;
		FDocGameplayEventDynamicDelegate Dynamic;
		uint64 Order = 0;
		bool bOwnerWasSet = false;
		bool bRemoved = false;
	};

	struct FHistoryEntry
	{
		FGameplayTag Tag;
		FDocEventScope Scope;
		int64 Sequence = 0;
	};

	FDocRequestHandle AddSubscription(FSubscription&& Subscription);
	FDocSystemResult ValidateEvent(const FDocGameplayEvent& Event) const;
	void StampEvent(FDocGameplayEvent& Event);
	void Enqueue(FDocGameplayEvent&& Event);
	void Drain();
	void DeliverOne(const FDocGameplayEvent& Event);
	void GatherListeners(const FDocGameplayEvent& Event, TArray<FDocRequestHandle>& OutHandles);
	void RecordHistory(const FDocGameplayEvent& Event);
	void RecordDebug(const FDocGameplayEvent& Event, int32 ListenerCount);
	void MaybeAutoRetain(const FDocGameplayEvent& Event);
	void StoreRetained(const FDocGameplayEvent& Event);
	void ExpireRetained();
	double NowFor(EDocClockDomain Clock) const;
	static bool PayloadHoldsStrongObjectRefs(const UScriptStruct* Struct);

	FDocEventBusLimits Limits;

	TDocHandleTable<FSubscription> Subscriptions;
	TMap<FGameplayTag, TArray<FDocRequestHandle>> ExactIndex;
	TMap<FGameplayTag, TArray<FDocRequestHandle>> HierarchicalIndex;
	uint64 NextOrder = 1;

	/** Queued events (UPROPERTY so instanced payload object refs are GC-visible while queued). */
	UPROPERTY(Transient)
	TArray<FDocGameplayEvent> Queue;

	UPROPERTY(Transient)
	TArray<FDocScheduledEvent> Scheduled;

	UPROPERTY(Transient)
	TArray<FDocRetainedEventValue> Retained;

	TMap<const UScriptStruct*, bool> StrongRefCache;
	TMap<FGameplayTag, TWeakObjectPtr<const UScriptStruct>> Schemas;

	TArray<FHistoryEntry> History;
	int32 HistoryHead = 0;
	int64 HistoryRecorded = 0;

	TArray<FDocEventDebugRecord> DebugRing;
	int32 DebugHead = 0;

	int64 NextSequence = 1;
	int32 DispatchDepth = 0;
	bool bDraining = false;
	bool bShutdown = false;

	FDocEventBusStats Stats;
};
