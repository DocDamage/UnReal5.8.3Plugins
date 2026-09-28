#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Sound/SoundBase.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocAdaptiveAudioTypes.generated.h"

/** Channel tags (handoff 10.3). */
namespace DocAudioTags
{
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Music);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ambience);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Environment);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tension);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stinger);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DialogueSupport);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Custom);
	DOCADAPTIVEAUDIORUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer);
}

/** How requests on one channel combine (handoff 10.3). */
UENUM(BlueprintType)
enum class EDocAudioBlendRule : uint8
{
	/** One effective profile at a time (music). */
	Exclusive,
	/** All valid profiles play together, bounded by voice limits (ambience beds). */
	Layered,
	/** Each request plays once and releases itself (stingers, UI). */
	AdditiveOneShot
};

/** Transition timing (handoff 10.4). Musical boundaries require a scheduling capability (Quartz bridge). */
UENUM(BlueprintType)
enum class EDocAudioQuantization : uint8
{
	Immediate,
	Beat,
	Bar,
	Phrase,
	Custom
};

/** What happens when a requested profile's sounds fail to load or play. */
UENUM(BlueprintType)
enum class EDocAudioFailurePolicy : uint8
{
	/** Keep whatever valid profile was already playing on the channel. */
	KeepPrevious,
	/** Play the profile's FallbackProfile. */
	UseFallback,
	/** Fade the channel to silence and report the failure. */
	Silence
};

UENUM(BlueprintType)
enum class EDocAudioFadeCurve : uint8
{
	Linear,
	Logarithmic,
	SCurve,
	Sin
};

/** Lifecycle of a channel transition (handoff 10.4). */
UENUM(BlueprintType)
enum class EDocAudioTransitionState : uint8
{
	Idle,
	Requested,
	AssetPreloading,
	Scheduled,
	Playing,
	Fading,
	Released,
	Failed,
	Cancelled,
	TimedOut
};

/** Split-screen policy: one shared output mix (handoff 10.1). */
UENUM(BlueprintType)
enum class EDocAudioListenerPolicy : uint8
{
	/** Context comes from the primary local player's sources only. */
	SharedOutputPrimaryListener,
	/** Context tags of all local players are combined into one shared state. */
	SharedOutputCombinedState
};

/** One stem/layer with musical metadata (handoff 10.3–10.4). */
UCLASS(BlueprintType)
class DOCADAPTIVEAUDIORUNTIME_API UDocAudioLayerDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Layer role, e.g. Audio.Layer.Base / Percussion / Melody / Tension / Combat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer", meta = (Categories = "Audio.Layer"))
	FGameplayTag LayerTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 1.f;

	/** Restart when the source finishes, if the asset itself does not loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	bool bLoop = true;

	/** Musical metadata (0 = unknown). Needed to validate aligned stems and bar/phrase timing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0"))
	float TempoBPM = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0"))
	int32 BeatsPerBar = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0"))
	int32 LoopLengthBars = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0"))
	float StartOffsetSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct DOCADAPTIVEAUDIORUNTIME_API FDocAudioLayerRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	TObjectPtr<UDocAudioLayerDefinition> Layer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	bool bEnabledByDefault = true;
};

/** A state profile: what a channel should sound like while requested (handoff 10.3). */
UCLASS(BlueprintType)
class DOCADAPTIVEAUDIORUNTIME_API UDocAudioStateProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	FGameplayTag ProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (Categories = "Audio"))
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	TArray<FDocAudioLayerRef> Layers;

	/** Random one-shots while this profile is effective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OneShots")
	TArray<TSoftObjectPtr<USoundBase>> RandomOneShots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OneShots", meta = (ClampMin = "0.1"))
	float OneShotMinIntervalSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OneShots", meta = (ClampMin = "0.1"))
	float OneShotMaxIntervalSeconds = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OneShots", meta = (ClampMin = "0"))
	int32 MaxConcurrentOneShots = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OneShots", meta = (ClampMin = "0.0"))
	float OneShotVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	EDocAudioFadeCurve FadeCurve = EDocAudioFadeCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	EDocAudioQuantization Quantization = EDocAudioQuantization::Immediate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	EDocAudioFailurePolicy FailurePolicy = EDocAudioFailurePolicy::KeepPrevious;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", meta = (EditCondition = "FailurePolicy == EDocAudioFailurePolicy::UseFallback"))
	TObjectPtr<UDocAudioStateProfile> FallbackProfile;

	/** Profile applies only while the combined context tags match (empty = always). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagQuery Condition;

	/** Upper bound of simultaneous voices (layers + one-shots) this profile may own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (ClampMin = "1"))
	int32 MaxVoices = 8;

	/** Collect the soft paths this profile needs to play. */
	void GatherSoundPaths(TArray<FSoftObjectPath>& Out) const;
};

/** Result of RequestAudioState. The handle is an owner-scoped lease. */
USTRUCT(BlueprintType)
struct DOCADAPTIVEAUDIORUNTIME_API FDocAudioRequestInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") FDocSystemResult Result;
};

/** Debugger snapshot of one channel (handoff 10.5). */
USTRUCT(BlueprintType)
struct DOCADAPTIVEAUDIORUNTIME_API FDocAudioChannelDebug
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") FGameplayTag Channel;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") EDocAudioBlendRule Rule = EDocAudioBlendRule::Exclusive;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") EDocAudioTransitionState State = EDocAudioTransitionState::Idle;
	/** Profiles currently audible (one for Exclusive). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") TArray<FGameplayTag> PlayingProfiles;
	/** Profile being loaded/scheduled, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") FGameplayTag PendingProfile;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") TArray<FGameplayTag> EnabledLayers;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") int32 EffectiveRequests = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") int32 VoiceCount = 0;
	/** Requested versus actual start time of the last transition (subsystem clock, seconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") double LastRequestedTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") double LastActualTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Audio") FString LastDiagnostic;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocAudioChannelEvent, FGameplayTag, Channel, FGameplayTag, ProfileId, EDocAudioTransitionState, State);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocAudioChannelNative, FGameplayTag /*Channel*/, FGameplayTag /*ProfileId*/, EDocAudioTransitionState /*State*/);

USTRUCT()
struct DOCADAPTIVEAUDIORUNTIME_API FDocAudioChannelConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Channel", meta = (Categories = "Audio"))
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, Category = "Channel")
	EDocAudioBlendRule Rule = EDocAudioBlendRule::Exclusive;
};

/** Project Settings → Plugins → Doc Adaptive Audio. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Adaptive Audio"))
class DOCADAPTIVEAUDIORUNTIME_API UDocAdaptiveAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDocAdaptiveAudioSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Blend rule per channel. Channels not listed are Exclusive. */
	UPROPERTY(Config, EditAnywhere, Category = "Channels")
	TArray<FDocAudioChannelConfig> Channels;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1"))
	int32 MaxTotalVoices = 32;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "0"))
	int32 MaxOneShotsPerSecond = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "0"))
	int32 MaxPendingRequests = 64;

	/** Asset preload timeout (0 = none). */
	UPROPERTY(Config, EditAnywhere, Category = "Loading", meta = (ClampMin = "0.0"))
	float LoadTimeoutSeconds = 10.f;

	/** When a musical boundary is requested but no scheduling capability is installed: true = reject, false = apply immediately and report it. */
	UPROPERTY(Config, EditAnywhere, Category = "Timing")
	bool bRejectUnavailableQuantization = false;

	UPROPERTY(Config, EditAnywhere, Category = "Listeners")
	EDocAudioListenerPolicy ListenerPolicy = EDocAudioListenerPolicy::SharedOutputCombinedState;

	/** Ambient emitter budget (UDocAudioEmitterComponent). */
	UPROPERTY(Config, EditAnywhere, Category = "Emitters", meta = (ClampMin = "0"))
	int32 MaxActiveEmitters = 16;

	UPROPERTY(Config, EditAnywhere, Category = "Emitters", meta = (ClampMin = "0.05"))
	float EmitterEvaluationIntervalSeconds = 0.5f;

	EDocAudioBlendRule GetRule(const FGameplayTag& Channel) const;
};
