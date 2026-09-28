#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocWeatherTypes.generated.h"

class UCurveFloat;

namespace DocWeatherTags
{
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Clear);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cloudy);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fog);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rain);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rain_Light);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rain_Heavy);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Storm);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Thunderstorm);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Snow);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Snow_Light);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Snow_Heavy);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Wind);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Wind_High);
	DOCWEATHERRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Custom);
}

UENUM(BlueprintType)
enum class EDocPrecipitationType : uint8
{
	None,
	Rain,
	Snow,
	Sleet,
	Hail
};

/**
 * Weather state with explicit units and ranges (handoff 4.2). Validate() rejects
 * out-of-range values; nothing is partially applied.
 */
USTRUCT(BlueprintType)
struct DOCWEATHERRUNTIME_API FDocWeatherState
{
	GENERATED_BODY()

	static constexpr double UnlimitedVisibility = 1.0e7; // metres; sentinel for "unlimited"

	/** Fraction [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1")) float CloudCoverage = 0.f;
	/** Dimensionless authored control [0,10] (not physical density). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "10")) float CloudDensity = 1.f;
	/** Intensity [0,1]; renderers map to their own units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation", meta = (ClampMin = "0", ClampMax = "1")) float Precipitation = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation") EDocPrecipitationType PrecipitationType = EDocPrecipitationType::None;
	/** Dimensionless authored control [0,10]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0", ClampMax = "10")) float FogDensity = 0.f;
	/** Metres above the map's declared reference height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (Units = "m")) float FogHeight = 0.f;
	/** Metres/second [0,100]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", ClampMax = "100", Units = "m/s")) float WindSpeed = 0.f;
	/** Normalized XY direction; zero falls back to +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind") FVector2D WindDirection = FVector2D(1, 0);
	/** Degrees Celsius [-80,60]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air", meta = (ClampMin = "-80", ClampMax = "60")) float Temperature = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air", meta = (ClampMin = "0", ClampMax = "1")) float Humidity = 0.5f;
	/** Ground state fractions [0,1] (derived; relaxed toward profile targets over time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0", ClampMax = "1")) float Wetness = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0", ClampMax = "1")) float SnowAmount = 0.f;
	/** Lightning strikes per minute [0,120] (a rate, never chance-per-frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "120")) float LightningPerMinute = 0.f;
	/** Probability [0,1] that an emitted lightning event is accompanied by thunder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1")) float ThunderChancePerLightning = 1.f;
	/** Metres (> 0); UnlimitedVisibility = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (Units = "m")) float Visibility = 1.0e7f;
	/** Non-negative multiplier [0,4]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "4")) float AmbientLightMultiplier = 1.f;
	/** Declared dominant category (mixed weather carries context tags, never an interpolated category). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags", meta = (Categories = "Weather")) FGameplayTag DominantTag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags") FGameplayTagContainer ContextTags;

	bool Validate(TArray<FString>& OutErrors) const;
	FVector2D SafeWindDirection() const;
};

/** Fields a regional override replaces (explicit, per field). */
USTRUCT(BlueprintType)
struct DOCWEATHERRUNTIME_API FDocWeatherFieldMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bClouds = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bPrecipitation = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bFog = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bWind = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bTemperature = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bVisibility = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bLight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask") bool bDominantTag = false;
};

/** Immutable weather profile (handoff 4.3). */
UCLASS(BlueprintType)
class DOCWEATHERRUNTIME_API UDocWeatherProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FName ProfileId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (Categories = "Weather")) FGameplayTag WeatherTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FText DisplayName;
	/** Hold duration range (seconds of weather clock) for scheduled modes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "0")) float MinDuration = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "0")) float MaxDuration = 900.f;
	/** Target state (Wetness/SnowAmount are the ground targets the relaxation moves toward). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FDocWeatherState State;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (Categories = "Audio")) FGameplayTag AudioProfileTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FGameplayTagContainer GameplayTags;

	bool ValidateProfile(TArray<FString>& OutErrors) const;
};

UENUM(BlueprintType)
enum class EDocWeatherCurve : uint8
{
	Linear,
	CurveAsset,
	Step,
	/** Custom provider (bridge); Unsupported without one. */
	Custom
};

UENUM(BlueprintType)
enum class EDocCurveOvershoot : uint8
{
	Clamp,
	Reject
};

UCLASS(BlueprintType)
class DOCWEATHERRUNTIME_API UDocWeatherTransitionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition") FName TransitionId;
	/** Seconds of weather clock; 0 = apply immediately once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (ClampMin = "0")) float Duration = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition") EDocWeatherCurve Curve = EDocWeatherCurve::Linear;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (EditCondition = "Curve == EDocWeatherCurve::CurveAsset")) TObjectPtr<UCurveFloat> CurveAsset;
	/** Step curve: fraction of the duration at which values switch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (ClampMin = "0", ClampMax = "1")) float StepAt = 0.5f;
	/** Categorical fields (precipitation type, dominant tag) switch at this fraction (declared policy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (ClampMin = "0", ClampMax = "1")) float CategoricalSwitchAt = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition") EDocCurveOvershoot Overshoot = EDocCurveOvershoot::Clamp;
};

UENUM(BlueprintType)
enum class EDocWeatherScheduleMode : uint8
{
	/** Only explicit commands; no hidden scheduler. */
	Manual,
	WeightedRandom,
	/** Ordered entries, each held for its duration, looping. */
	TimeDuration,
	/** TimeDriven / RegionDriven / Scripted via a schedule provider (bridge). */
	Provider
};

USTRUCT(BlueprintType)
struct DOCWEATHERRUNTIME_API FDocWeatherScheduleEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") TObjectPtr<UDocWeatherProfile> Profile;
	/** WeightedRandom: non-negative; at least one positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") float Weight = 1.f;
	/** TimeDuration: hold seconds (0 = use the profile's range). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") TObjectPtr<UDocWeatherTransitionDefinition> Transition;
};

UCLASS(BlueprintType)
class DOCWEATHERRUNTIME_API UDocWeatherSchedule : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") FName ScheduleId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") EDocWeatherScheduleMode Mode = EDocWeatherScheduleMode::WeightedRandom;
	/** Stable candidate order = array order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") TArray<FDocWeatherScheduleEntry> Entries;

	FDocSystemResult ValidateSchedule() const;
};

UENUM(BlueprintType)
enum class EDocWeatherCommandPolicy : uint8
{
	/** Replace the current transition (evaluated snapshot becomes the source) and pause the scheduler. */
	ReplaceAndPauseScheduler,
	/** Replace; the scheduler keeps running and may pick the next profile later. */
	ReplaceKeepScheduler,
	/** Start after the current transition completes. */
	QueueBehindCurrent,
	/** Refuse while a transition is running. */
	RejectIfTransitioning
};

UENUM(BlueprintType)
enum class EDocWeatherTransitionOutcome : uint8
{
	Completed,
	/** Superseded by a newer transition (its evaluated state carried over). */
	Replaced,
	Cancelled,
	Failed
};

USTRUCT(BlueprintType)
struct DOCWEATHERRUNTIME_API FDocLightningEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Weather") int64 EventId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Weather") double ClockTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Weather") bool bThunder = false;
};

/** Everything needed to restore/reconstruct weather (handoff 4.6). Versioned. */
USTRUCT(BlueprintType)
struct DOCWEATHERRUNTIME_API FDocWeatherSaveData
{
	GENERATED_BODY()

	UPROPERTY() int32 Version = 1;
	UPROPERTY() int64 Revision = 0;
	UPROPERTY() double Clock = 0.0;
	UPROPERTY() FDocWeatherState Current;
	UPROPERTY() FName CurrentProfileId;
	UPROPERTY() bool bTransitionActive = false;
	UPROPERTY() int64 TransitionId = 0;
	UPROPERTY() FDocWeatherState TransitionSource;
	UPROPERTY() FName TransitionTargetId;
	UPROPERTY() FName TransitionDefinitionId;
	UPROPERTY() double TransitionElapsed = 0.0;
	UPROPERTY() double TransitionDuration = 0.0;
	UPROPERTY() EDocWeatherScheduleMode Mode = EDocWeatherScheduleMode::Manual;
	UPROPERTY() FName ScheduleId;
	UPROPERTY() bool bSchedulerPaused = false;
	UPROPERTY() double HoldRemaining = 0.0;
	UPROPERTY() int32 SequenceIndex = 0;
	UPROPERTY() int32 SelectionSeed = 0;
	UPROPERTY() int32 SelectionDraws = 0;
	UPROPERTY() int32 EventSeed = 0;
	UPROPERTY() double LightningHazard = 0.0;
	UPROPERTY() double LightningThreshold = 1.0;
	UPROPERTY() int64 NextEventId = 1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocWeatherProfileEvent, FName, ProfileId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocWeatherTransitionEvent, int64, TransitionId, EDocWeatherTransitionOutcome, Outcome);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocWeatherLightningEvent, const FDocLightningEvent&, Event);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocWeatherTransitionNative, int64, EDocWeatherTransitionOutcome);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocWeatherLightningNative, const FDocLightningEvent&);

/** Project Settings → Plugins → Doc Weather. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Weather"))
class DOCWEATHERRUNTIME_API UDocWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Relaxation rates k (1/s) for x(t+dt) = target + (x - target) * exp(-k dt). Game-state model, not hydrology. */
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "0")) float WetnessRiseRate = 0.02f;
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "0")) float WetnessDryRate = 0.004f;
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "0")) float SnowAccumulateRate = 0.005f;
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "0")) float SnowMeltRate = 0.002f;

	/** Clock jumps: at most this many scheduled selections are simulated; the rest is fast-forwarded without history. */
	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "1")) int32 MaxCatchUpSelections = 8;
	/** Lightning events emitted per update at most (excess dropped and counted). */
	UPROPERTY(Config, EditAnywhere, Category = "Events", meta = (ClampMin = "1")) int32 MaxEventsPerUpdate = 4;
	/** Evaluated state changes smaller than this do not bump the revision/broadcast OnWeatherChanged. */
	UPROPERTY(Config, EditAnywhere, Category = "Events", meta = (ClampMin = "0")) float SignificantDelta = 0.01f;
};
