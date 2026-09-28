#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/WeakObjectPtr.h"
#include "DocPersistentObjectId.h"
#include "DocRequestHandle.h"
#include "DocSharedTypes.h"
#include "DocEventsTypes.generated.h"

/** Scope kinds (handoff 6.2). Region/Team/Custom scopes are value keys; no Region/team classes are imported. */
UENUM(BlueprintType)
enum class EDocEventScopeKind : uint8
{
	/** The whole owning world. "Global" means world-global, never cross-world. */
	World,
	Actor,
	Component,
	Region,
	Team,
	Custom
};

/**
 * Event scope. World scope ignores Object/Key. Actor/Component scopes compare the
 * object identity (weak). Region/Team/Custom scopes compare Key.
 */
USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocEventScope
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	EDocEventScopeKind Kind = EDocEventScopeKind::World;

	/** Actor or component for Actor/Component scopes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	TWeakObjectPtr<UObject> Object;

	/** Value identifier for Region/Team/Custom scopes (e.g. a region instance id string or team name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FName Key;

	static FDocEventScope WorldScope() { return FDocEventScope(); }
	static FDocEventScope ForObject(EDocEventScopeKind InKind, UObject* InObject) { FDocEventScope S; S.Kind = InKind; S.Object = InObject; return S; }
	static FDocEventScope ForKey(EDocEventScopeKind InKind, FName InKey) { FDocEventScope S; S.Kind = InKind; S.Key = InKey; return S; }

	FString ToString() const;

	friend bool operator==(const FDocEventScope& A, const FDocEventScope& B)
	{
		if (A.Kind != B.Kind) { return false; }
		switch (A.Kind)
		{
		case EDocEventScopeKind::World: return true;
		case EDocEventScopeKind::Actor:
		case EDocEventScopeKind::Component: return A.Object == B.Object;
		default: return A.Key == B.Key;
		}
	}
	friend bool operator!=(const FDocEventScope& A, const FDocEventScope& B) { return !(A == B); }
	friend uint32 GetTypeHash(const FDocEventScope& S)
	{
		uint32 H = ::GetTypeHash(static_cast<uint8>(S.Kind));
		switch (S.Kind)
		{
		case EDocEventScopeKind::World: return H;
		case EDocEventScopeKind::Actor:
		case EDocEventScopeKind::Component: return HashCombine(H, GetTypeHash(S.Object));
		default: return HashCombine(H, GetTypeHash(S.Key));
		}
	}
};

UENUM(BlueprintType)
enum class EDocEventTagMatch : uint8
{
	Exact,
	IncludeChildren
};

/**
 * Event envelope (handoff 6.2). Payload is a typed FInstancedStruct; the
 * Float/Int/Name convenience fields are for simple events whose schema declares
 * them. Never store the same semantic value in both a convenience field and the
 * payload.
 */
USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocGameplayEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FDocEventScope Scope;

	/** Assigned by the bus at broadcast: monotonic per world subsystem instance. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	int64 Sequence = 0;

	/** Assigned by the bus at broadcast, in TimeDomain. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	double Timestamp = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	EDocClockDomain TimeDomain = EDocClockDomain::WorldGameplay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	TWeakObjectPtr<UObject> Sender;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	TWeakObjectPtr<UObject> Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FDocPersistentObjectId SenderId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FDocPersistentObjectId TargetId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	bool bHasLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events", meta = (EditCondition = "bHasLocation"))
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FGameplayTagContainer ContextTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FInstancedStruct Payload;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	float FloatValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	FName NameValue;

	/** Optional correlation with an operation (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	int64 CorrelationId = 0;
};

/** Subscription options. */
USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocEventListenOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	EDocEventTagMatch Match = EDocEventTagMatch::IncludeChildren;

	/** When true, only events whose Scope equals ScopeFilter are delivered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	bool bFilterByScope = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events", meta = (EditCondition = "bFilterByScope"))
	FDocEventScope ScopeFilter;

	/** Higher priority listeners are called first; ties by registration order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	int32 Priority = 0;

	/** Remove after the first delivered event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	bool bOneShot = false;

	/** After subscribing, deliver matching retained values (queued, not re-entrant). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	bool bReplayRetained = false;

	/**
	 * Optional owner. When set and later destroyed, the subscription is removed.
	 * Dynamic (Blueprint) subscriptions use the delegate's bound object if unset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Events")
	TWeakObjectPtr<UObject> Owner;
};

/** Latest retained value for an exact tag + scope. Runtime only; never disk persistence. */
USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocRetainedEventValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	FDocGameplayEvent Event;

	/** Increments on every update of this tag + scope. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	int64 Revision = 0;

	/** World time of last update (for TTL). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events")
	double UpdatedAt = 0.0;
};

/** Bounded debug record (payload contents are never captured). */
USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocEventDebugRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") FGameplayTag EventTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") FString Scope;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") double Timestamp = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") FString SenderName;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") FString TargetName;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") FString PayloadType;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int32 ListenerCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 CorrelationId = 0;
};

USTRUCT(BlueprintType)
struct DOCEVENTSRUNTIME_API FDocEventBusStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int32 ActiveSubscriptions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int32 QueuedEvents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int32 ScheduledEvents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int32 RetainedValues = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 TotalBroadcast = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 DroppedEvents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 DeferredEvents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 EvictedRetained = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Events") int64 EvictedHistory = 0;
};

DECLARE_DELEGATE_OneParam(FDocGameplayEventNativeDelegate, const FDocGameplayEvent& /*Event*/);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDocGameplayEventDynamicDelegate, const FDocGameplayEvent&, Event);
