#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocStreamingTypes.generated.h"

class UWorld;

UENUM(BlueprintType)
enum class EDocStreamingBackend : uint8
{
	/** ULevelStreamingDynamic level instance (base backend). */
	LevelInstance,
	/** Reserved for bridges (World Partition sources / data layers). The base returns Unsupported. */
	BridgeDefined
};

UENUM(BlueprintType)
enum class EDocChunkReadiness : uint8
{
	/** Level package loaded (not necessarily visible). */
	Loaded,
	/** Loaded and made visible (added to the world). Default. */
	Visible
};

/**
 * Logical chunk definition (handoff 8.2). Distances are Unreal units (cm).
 */
UCLASS(BlueprintType)
class DOCSTREAMINGRUNTIME_API UDocStreamingChunkDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable definition id. Defaults to the asset name when None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	FName ChunkId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	FGameplayTag ChunkTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	EDocStreamingBackend Backend = EDocStreamingBackend::LevelInstance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk", meta = (EditCondition = "Backend == EDocStreamingBackend::LevelInstance"))
	TSoftObjectPtr<UWorld> Level;

	/** Placement used when a request supplies no transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	FTransform DefaultTransform;

	/** Chunks that must be resident first. Loaded in dependency order; cycles are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	TArray<TObjectPtr<UDocStreamingChunkDefinition>> Dependencies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	int32 Priority = 0;

	/** Stay resident after the last lease is released (until world teardown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	bool bKeepLoaded = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk")
	EDocChunkReadiness Readiness = EDocChunkReadiness::Visible;

	/** Seconds (real time) before a pending request times out. 0 = no timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk", meta = (ClampMin = "0.0", Units = "s"))
	float TimeoutSeconds = 60.f;

	/** For source components: request inside LoadDistance, release beyond UnloadDistance (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distances", meta = (ClampMin = "0.0", Units = "cm"))
	float LoadDistance = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distances", meta = (ClampMin = "0.0", Units = "cm"))
	float UnloadDistance = 12000.f;

	FName GetEffectiveChunkId() const { return ChunkId.IsNone() ? GetFName() : ChunkId; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Which placement of a chunk (handoff 8.2): definition id + stable instance scope. */
USTRUCT(BlueprintType)
struct DOCSTREAMINGRUNTIME_API FDocChunkInstanceKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Streaming")
	FName ChunkId;

	/** Zero = default placement. Repeated placements use distinct scopes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Streaming")
	FGuid InstanceScope;

	friend bool operator==(const FDocChunkInstanceKey& A, const FDocChunkInstanceKey& B) { return A.ChunkId == B.ChunkId && A.InstanceScope == B.InstanceScope; }
	friend uint32 GetTypeHash(const FDocChunkInstanceKey& K) { return HashCombine(GetTypeHash(K.ChunkId), GetTypeHash(K.InstanceScope)); }
	FString ToString() const { return FString::Printf(TEXT("%s#%s"), *ChunkId.ToString(), *InstanceScope.ToString(EGuidFormats::Short)); }
};

/** Observed backend state of an instance (handoff 8.4). */
UENUM(BlueprintType)
enum class EDocChunkObservedState : uint8
{
	Unrequested,
	Loading,
	Loaded,
	MakingVisible,
	Ready,
	Unloading,
	Failed
};

/** State of one request/lease. Exactly one terminal state. */
UENUM(BlueprintType)
enum class EDocStreamingRequestState : uint8
{
	Pending,
	Ready,
	Failed,
	TimedOut,
	Cancelled,
	Released
};

USTRUCT(BlueprintType)
struct DOCSTREAMINGRUNTIME_API FDocChunkStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") FDocChunkInstanceKey Key;
	/** Desired: at least one lease or keep-loaded wants it resident. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") bool bDesiredResident = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") EDocChunkObservedState Observed = EDocChunkObservedState::Unrequested;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") int32 LeaseCount = 0;
	/** Native streaming offers no measurable progress for a single level: always false. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") bool bProgressKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") FString LastFailure;
};

USTRUCT(BlueprintType)
struct DOCSTREAMINGRUNTIME_API FDocStreamingRequestInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") FDocChunkInstanceKey Key;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") EDocStreamingRequestState State = EDocStreamingRequestState::Failed;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") FDocSystemResult Result;
	/** True when released but the content stayed resident for other owners/policies. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Streaming") bool bReleasedButResident = false;
};

/** A mutually exclusive set of world-state variants (e.g. Bridge.Intact / Bridge.Destroyed). */
USTRUCT(BlueprintType)
struct DOCSTREAMINGRUNTIME_API FDocWorldStateVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World State")
	FGameplayTag StateTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World State")
	TArray<TObjectPtr<UDocStreamingChunkDefinition>> Chunks;
};

UCLASS(BlueprintType)
class DOCSTREAMINGRUNTIME_API UDocWorldStateGroup : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Conflict group id; one effective variant per group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World State")
	FName GroupId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World State")
	TArray<FDocWorldStateVariant> Variants;

	/** Effective when no request is active (may be empty = nothing loaded). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World State")
	FGameplayTag DefaultState;

	const FDocWorldStateVariant* FindVariant(FGameplayTag StateTag) const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDocStreamingRequestChanged, const FDocStreamingRequestInfo& /*Info*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocStreamingRequestChangedDynamic, const FDocStreamingRequestInfo&, Info);
