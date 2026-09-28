#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/Interface.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocActivationTypes.generated.h"

class UDocWorldActivationComponent;

namespace DocActivationTags
{
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activation);
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dormant);
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Representation);
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lightweight);
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Active);
	DOCWORLDACTIVATIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Full);
}

/**
 * Tiers with an explicit rank (enum order is the rank; tag lexical order is never used).
 * Tags are the semantic names exposed to data and debuggers (handoff 14.2).
 */
UENUM(BlueprintType)
enum class EDocActivationTier : uint8
{
	/** Minimal loaded-actor activity (no-actor form needs a virtualization adapter). */
	Dormant = 0,
	/** Cheap non-gameplay representation: requires a representation adapter (ISM/HISM...). */
	Representation = 1,
	/** Reduced update rate, limited optional simulation. */
	Lightweight = 2,
	/** Normal gameplay participation. */
	Active = 3,
	/** Highest configured capability (only what the participant declares safe). */
	Full = 4
};

UENUM(BlueprintType)
enum class EDocActivationPolicyKind : uint8
{
	AlwaysActive,
	DistanceBased,
	Manual,
	/** IDocActivationParticipant::GetCustomDesiredTier on the owner or a component. */
	Custom,
	/** Requires a relevance provider (bridge); without one the policy reports and uses DefaultTier. */
	VisibilityBased,
	/** Requires interaction pins/providers; without one the policy reports and uses DefaultTier. */
	InteractionBased
};

/** One distance band. Distances are centimetres; promote and demote thresholds are separate (hysteresis). */
USTRUCT(BlueprintType)
struct DOCWORLDACTIVATIONRUNTIME_API FDocActivationBand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Band")
	EDocActivationTier Tier = EDocActivationTier::Active;

	/** Enter this tier (from below) when the nearest source is within this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Band", meta = (Units = "cm", ClampMin = "0"))
	float PromoteWithinCm = 2000.f;

	/** Leave this tier (downwards) only when the nearest source is beyond this distance (>= PromoteWithinCm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Band", meta = (Units = "cm", ClampMin = "0"))
	float DemoteBeyondCm = 2500.f;
};

/** How desired tiers are chosen (handoff 14.3). */
UCLASS(BlueprintType)
class DOCWORLDACTIVATIONRUNTIME_API UDocActivationPolicy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationPolicyKind Kind = EDocActivationPolicyKind::DistanceBased;

	/** Bands for DistanceBased. Tiers not covered by any band fall back to FarTier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	TArray<FDocActivationBand> Bands;

	/** Beyond every band. Defaults are illustrative, not production values: measure on your content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationTier FarTier = EDocActivationTier::Dormant;

	/** Initial tier and fallback when a policy input is unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationTier DefaultTier = EDocActivationTier::Active;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationTier MinTier = EDocActivationTier::Dormant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationTier MaxTier = EDocActivationTier::Full;

	/** Minimum time in a tier before a policy-driven change (pins raising the tier bypass it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy", meta = (ClampMin = "0.0"))
	float MinDwellSeconds = 1.f;

	/** Used when a desired tier needs an adapter that is not installed (never "delete the actor"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Policy")
	EDocActivationTier UnsupportedFallbackTier = EDocActivationTier::Lightweight;

	/** Resolve the policy tier from a distance with hysteresis relative to Current. */
	EDocActivationTier EvaluateDistance(float DistanceCm, EDocActivationTier Current) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Resources the base adapter may control. Components opt in explicitly (no universal disabling). */
USTRUCT(BlueprintType)
struct DOCWORLDACTIVATIONRUNTIME_API FDocActivationCapabilities
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities") bool bActorTick = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities") bool bComponentTick = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities") bool bVisibility = false;
	/** Off by default: a distant barrier may still need collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities") bool bCollision = false;
};

/** What a tier does to the controlled resources. */
USTRUCT(BlueprintType)
struct DOCWORLDACTIVATIONRUNTIME_API FDocActivationTierSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier")
	EDocActivationTier Tier = EDocActivationTier::Lightweight;

	/** Seconds between ticks (0 = every frame). Ignored when bDisableTick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier", meta = (ClampMin = "0.0"))
	float TickInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier")
	bool bDisableTick = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier")
	bool bHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier")
	bool bDisableCollision = false;
};

/** Per-tier behaviour for the base loaded-actor adapter. Tiers without an entry leave host state untouched. */
UCLASS(BlueprintType)
class DOCWORLDACTIVATIONRUNTIME_API UDocActivationProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	TArray<FDocActivationTierSettings> Tiers;

	const FDocActivationTierSettings* Find(EDocActivationTier Tier) const
	{
		return Tiers.FindByPredicate([Tier](const FDocActivationTierSettings& S) { return S.Tier == Tier; });
	}
};

/** Optional per-object participant: custom state, custom policy, tier notifications. */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocActivationParticipant : public UInterface
{
	GENERATED_BODY()
};

class DOCWORLDACTIVATIONRUNTIME_API IDocActivationParticipant
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Activation")
	void OnActivationTierChanged(EDocActivationTier OldTier, EDocActivationTier NewTier);

	/** Custom policy: return the desired tier. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Activation")
	EDocActivationTier GetCustomDesiredTier(EDocActivationTier Current) const;
};

/**
 * Representation adapter (ISM/HISM, virtualization, pooling bridges). Implements the
 * guarded transition: Prepare the destination, Validate, Commit the swap, Release the source;
 * on any failure the prior representation stays usable.
 */
class DOCWORLDACTIVATIONRUNTIME_API IDocActivationRepresentationAdapter
{
public:
	virtual ~IDocActivationRepresentationAdapter() = default;
	virtual FName GetAdapterName() const = 0;
	/** Can this adapter move the object into Tier (and back)? */
	virtual bool SupportsTier(const UDocWorldActivationComponent* Object, EDocActivationTier Tier) const = 0;
	virtual FDocSystemResult PrepareTransition(UDocWorldActivationComponent* Object, EDocActivationTier From, EDocActivationTier To) = 0;
	virtual FDocSystemResult CommitTransition(UDocWorldActivationComponent* Object, EDocActivationTier From, EDocActivationTier To) = 0;
	virtual void RollbackTransition(UDocWorldActivationComponent* Object, EDocActivationTier From, EDocActivationTier To) = 0;
};

USTRUCT(BlueprintType)
struct DOCWORLDACTIVATIONRUNTIME_API FDocActivationDebugInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") FName LogicalId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") EDocActivationTier Current = EDocActivationTier::Active;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") EDocActivationTier Desired = EDocActivationTier::Active;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") EDocActivationPolicyKind Policy = EDocActivationPolicyKind::AlwaysActive;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") float NearestSourceCm = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 Pins = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") EDocActivationTier PinnedMinTier = EDocActivationTier::Dormant;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") FName Representation;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") bool bTransitionQueued = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 Transitions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") FString LastDiagnostic;
};

USTRUCT(BlueprintType)
struct DOCWORLDACTIVATIONRUNTIME_API FDocActivationStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 Registered = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") TMap<EDocActivationTier, int32> CountByTier;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 EvaluationsLastTick = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 TransitionsLastTick = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 PendingTransitions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") float TransitionsPerSecond = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") double LastTickMilliseconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Activation") int32 UnregisteredByUnload = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocActivationTierEvent, UDocWorldActivationComponent*, Object, EDocActivationTier, OldTier, EDocActivationTier, NewTier);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocActivationTierNative, UDocWorldActivationComponent*, EDocActivationTier, EDocActivationTier);

/** Project Settings → Plugins → Doc World Activation. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc World Activation"))
class DOCWORLDACTIVATIONRUNTIME_API UDocWorldActivationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Objects evaluated per tick (round robin; prioritized re-evaluations first). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxEvaluationsPerTick = 64;

	/** Tier transitions committed per tick; the rest stay queued. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxTransitionsPerTick = 16;

	/** Minimum seconds between evaluations of one object. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0.0"))
	float EvaluationIntervalSeconds = 0.25f;

	/** Spatial index cell size (cm) used for prioritized re-evaluation around teleports and new sources. */
	UPROPERTY(Config, EditAnywhere, Category = "Index", meta = (ClampMin = "100.0", Units = "cm"))
	float CellSizeCm = 5000.f;

	/** Radius (cm) re-evaluated with priority when a source teleports or is added. */
	UPROPERTY(Config, EditAnywhere, Category = "Index", meta = (ClampMin = "0.0", Units = "cm"))
	float PriorityRadiusCm = 20000.f;

	/** Include every player's pawn/view as a source (server: all players, so one client can never demote shared simulation). */
	UPROPERTY(Config, EditAnywhere, Category = "Sources")
	bool bUseAllPlayersAsSources = true;
};
