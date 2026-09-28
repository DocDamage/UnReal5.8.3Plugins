#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Chaos/ChaosEngineInterface.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocSurfaceFeedbackTypes.generated.h"

class UPhysicalMaterial;
class UPrimitiveComponent;

namespace DocSurfaceTags
{
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Surface);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Wood);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Metal);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dirt);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Grass);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Water);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mud);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Concrete);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Glass);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Sand);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Snow);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Custom);
	/** Configured default surface (distinct from Unmapped). */
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Default);

	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Footstep);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Landing);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Jump);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Slide);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Impact);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Projectile);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Melee);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Vehicle);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Drag);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Break);
	DOCSURFACEFEEDBACKRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Custom);
}

/** Where the resolved surface came from (diagnostics; handoff 5.2). */
UENUM(BlueprintType)
enum class EDocSurfaceOrigin : uint8
{
	PhysicalMaterial,
	PhysicalSurfaceType,
	TagProvider,
	/** No mapping matched; the configured Default surface was used. */
	ConfiguredDefault,
	/** Material missing/unmapped and no default configured. */
	Unmapped,
	/** Request carried no hit. */
	NoHit
};

UENUM(BlueprintType)
enum class EDocSurfaceMappingStage : uint8
{
	PhysicalMaterial,
	PhysicalSurfaceType,
	TagProvider
};

/** Fallback chain position of the selected rule (handoff 5.3). */
UENUM(BlueprintType)
enum class EDocSurfaceMatchPath : uint8
{
	None,
	EventSurfaceContext,
	EventSurface,
	EventOnly,
	Default
};

UENUM(BlueprintType)
enum class EDocSurfaceResponseKind : uint8
{
	Sound,
	MetaSound,
	Niagara,
	Decal,
	CameraShake,
	Haptic,
	GameplayEvent,
	GameplayTag,
	SpawnedActor,
	Custom
};

/** Admission/dispatch outcome of one response (never an undifferentiated success). */
UENUM(BlueprintType)
enum class EDocSurfaceAdmission : uint8
{
	NotDispatched,
	Executed,
	BudgetSuppressed,
	Unsupported,
	AssetUnavailable,
	Cancelled,
	PermissionDenied,
	Pending
};

USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceMaterialEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TSoftObjectPtr<UPhysicalMaterial> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping", meta = (Categories = "Surface"))
	FGameplayTag Surface;
};

USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceTypeEntry
{
	GENERATED_BODY()

	/** Project-configured physical surface (labels come from the host's Physics settings). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping", meta = (Categories = "Surface"))
	FGameplayTag Surface;
};

/** Explicit mapping from the host's physical materials/surfaces to Surface.* tags. Never replaces the host table. */
UCLASS(BlueprintType)
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceMappingAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TArray<FDocSurfaceMaterialEntry> Materials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TArray<FDocSurfaceTypeEntry> SurfaceTypes;

	/** Resolution order (default: material, surface type, tag provider). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TArray<EDocSurfaceMappingStage> Precedence = { EDocSurfaceMappingStage::PhysicalMaterial, EDocSurfaceMappingStage::PhysicalSurfaceType, EDocSurfaceMappingStage::TagProvider };

	/** Used when nothing maps. Empty = the result is Unmapped (distinguishable from a configured default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping", meta = (Categories = "Surface"))
	FGameplayTag DefaultSurface;
};

/** One response of a rule. */
USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	EDocSurfaceResponseKind Kind = EDocSurfaceResponseKind::Sound;

	/** Variations (sound, decal material, Niagara system, camera shake class, force feedback, actor class...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	TArray<TSoftObjectPtr<UObject>> Variations;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	bool bNoImmediateRepeat = true;

	/** Used when every variation is unavailable (instead of silently running another rule). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	TSoftObjectPtr<UObject> Fallback;

	/** Volume/scale = Lerp(Min, Max, clamped magnitude 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response") float MinScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response") float MaxScale = 1.f;

	/** Gameplay payload tag (GameplayEvent/GameplayTag kinds) or custom executor name tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	FGameplayTag PayloadTag;

	/** Decal size (cm) and lifetime (s); spawned actor lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response") FVector DecalSize = FVector(8.f, 16.f, 16.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response") float LifetimeSeconds = 10.f;

	/** Continuous responses (slide/drag/vehicle) return a handle with update/stop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	bool bContinuous = false;

	bool IsGameplay() const { return Kind == EDocSurfaceResponseKind::GameplayEvent || Kind == EDocSurfaceResponseKind::GameplayTag; }
};

/** Deterministic response rule (handoff 5.3). */
USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceResponseRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	FName RuleId;

	/** Empty = default rule for any event (last fallback stage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule", meta = (Categories = "SurfaceEvent"))
	FGameplayTag EventTag;

	/** true: request event must equal EventTag; false: request event may be a child of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	bool bExactEvent = false;

	/** Empty = any surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule", meta = (Categories = "Surface"))
	FGameplayTag Surface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	bool bExactSurface = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	FGameplayTagContainer RequiredContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	FGameplayTagContainer BlockedContext;

	/** Inclusive magnitude range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule") float MinMagnitude = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule") float MaxMagnitude = TNumericLimits<float>::Max();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule")
	TArray<FDocSurfaceResponse> Responses;

	/** Per source (instigator + source id) cooldown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rule", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.f;
};

UCLASS(BlueprintType)
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceResponseProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	TObjectPtr<UDocSurfaceMappingAsset> Mapping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	TArray<FDocSurfaceResponseRule> Rules;

	/** Bump when rules change (part of cache keys). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	int32 Version = 1;

	/** Authoring check: returns human-readable warnings (ambiguous ties, duplicate ids, empty responses). */
	TArray<FString> FindAuthoringProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Request (handoff 5.2). Weak references only: a queued request never keeps actors alive. */
USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceFeedbackRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request", meta = (Categories = "SurfaceEvent"))
	FGameplayTag EventTag;

	/** World units (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") FVector Normal = FVector::UpVector;
	/** cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") FVector Velocity = FVector::ZeroVector;
	/** Event-defined strength; the profile's rules document its meaning (normalized 0..1 for footsteps, impulse for impacts...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") float Magnitude = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") TWeakObjectPtr<AActor> Instigator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") TWeakObjectPtr<UPrimitiveComponent> HitComponent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") TWeakObjectPtr<UPhysicalMaterial> PhysicalMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") FGameplayTagContainer ContextTags;

	/** False = no surface was hit (air, missed trace). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") bool bHasHit = true;

	/** Source/limb/channel id (e.g. "FootL"); part of cooldown and dedupe keys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") FName SourceId;

	/** Correlates a predicted cosmetic and its confirmed copy (network bridge). 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") int64 CorrelationId = 0;

	/** Requests gameplay responses too (still requires authority and a provider). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request") bool bAuthoritativeIntent = false;

	/** Filled by the subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "Request") int64 RequestId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Request") double SubmittedAt = 0.0;
};

USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceResponseDispatch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") EDocSurfaceResponseKind Kind = EDocSurfaceResponseKind::Sound;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FSoftObjectPath Asset;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") int32 VariationIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") float Scale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") EDocSurfaceAdmission Admission = EDocSurfaceAdmission::NotDispatched;
	/** Continuous responses only. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FString Reason;
};

/** Inspectable result (handoff 5.3 / 5.6). Resolution success is separate from playback success. */
USTRUCT(BlueprintType)
struct DOCSURFACEFEEDBACKRUNTIME_API FDocSurfaceFeedbackResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FDocSystemResult Result;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") int64 RequestId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FGameplayTag EventTag;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FGameplayTag Surface;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") EDocSurfaceOrigin SurfaceOrigin = EDocSurfaceOrigin::NoHit;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") FName SelectedRule;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") EDocSurfaceMatchPath MatchPath = EDocSurfaceMatchPath::None;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") int32 CandidateCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") TArray<FString> Rejected;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") TArray<FDocSurfaceResponseDispatch> Responses;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") float Magnitude = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Surface") bool bNormalFallback = false;
};

/** Project Settings → Plugins → Doc Surface Feedback. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Surface Feedback"))
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceFeedbackSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) int32 MaxActiveSounds = 24;
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) int32 MaxActiveEffects = 32;
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) int32 MaxActiveDecals = 64;
	/** Requests per second across the world (0 = unlimited). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0")) int32 MaxRequestsPerSecond = 120;
	/** Cosmetic responses farther than this from every local view are culled (0 = no cull). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", Units = "cm")) float CullDistance = 6000.f;
	/** Requests waiting on asset loads. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1")) int32 MaxQueuedRequests = 32;
	/** A queued request older than this is Cancelled instead of playing late. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0.0")) float QueuedRequestTTLSeconds = 0.3f;
	/** Duplicate window for CorrelationId (predicted + confirmed copies). */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "0.0")) float DedupeWindowSeconds = 1.f;
	/** Classes a SpawnedActor response may spawn (never classes named by request data). */
	UPROPERTY(Config, EditAnywhere, Category = "Security") TArray<TSoftClassPtr<AActor>> AllowedSpawnClasses;
	/** Bounded debug history (0 = off). Always off in Shipping. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0")) int32 DebugHistorySize = 0;
};
