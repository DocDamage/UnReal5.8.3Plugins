#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/StreamableManager.h"
#include "Math/RandomStream.h"
#include "DocAdaptiveAudioTypes.h"
#include "DocAudioPlaybackBackend.h"
#include "DocAdaptiveAudioSubsystem.generated.h"

class UDocAudioEmitterComponent;
class ULocalPlayer;

/**
 * Logical audio director for one world (handoff Section 10).
 *
 * - Requests are owner-scoped leases on a profile. Per channel, the effective set is
 *   recomputed from currently valid requests only (owner alive, condition matches the
 *   combined context): Exclusive → highest priority, then higher tie-breaker, then most
 *   recent; Layered → all valid; AdditiveOneShot → each request plays once.
 * - Transitions: Requested → AssetPreloading → Scheduled → Playing/Fading → Released,
 *   or Failed / Cancelled / TimedOut. Superseded loads never start (each pending
 *   transition has an id; late completions for unknown ids are ignored). The previous
 *   profile keeps playing until the new one is ready.
 * - Musical boundaries (Beat/Bar/Phrase/Custom) need a scheduler (Quartz bridge). Without
 *   one the transition is applied immediately and reported, or rejected (settings).
 * - Dedicated servers and hosts without an audio device use a logical-only backend.
 */
UCLASS()
class DOCADAPTIVEAUDIORUNTIME_API UDocAdaptiveAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocAdaptiveAudioSubsystem* Get(const UObject* WorldContextObject);

	// ---- Requests ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio", meta = (DefaultToSelf = "Owner"))
	FDocAudioRequestInfo RequestAudioState(UDocAudioStateProfile* Profile, UObject* Owner, int32 TieBreaker = 0);

	/** Idempotent: releasing twice returns NoChange. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio", meta = (DefaultToSelf = "Owner"))
	FDocSystemResult ReleaseAudioState(FDocRequestHandle Handle, UObject* Owner);

	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	int32 ReleaseAllForOwner(UObject* Owner);

	// ---- Context (supplied by adapters: regions, time, weather, combat...) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	void SetContextTags(FName Source, const FGameplayTagContainer& Tags);

	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	void ClearContextSource(FName Source);

	/**
	 * Per-listener context (split screen). Combined with the shared sources according to
	 * the ListenerPolicy setting: all local players, or the primary local player only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	void SetListenerContextTags(ULocalPlayer* Player, const FGameplayTagContainer& Tags);

	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	FGameplayTagContainer GetCombinedContext() const;

	// ---- Layers ----
	/** Enable/disable a layer role on a channel (applies to current and future profiles). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	FDocSystemResult SetLayerEnabled(FGameplayTag Channel, FGameplayTag LayerTag, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	void ClearLayerOverrides(FGameplayTag Channel);

	// ---- Debug ----
	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	FDocAudioChannelDebug GetChannelDebug(FGameplayTag Channel) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	TArray<FGameplayTag> GetKnownChannels() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	int32 GetTotalVoiceCount() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	FString DescribeBackend() const;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Audio")
	FDocAudioChannelEvent OnChannelStateChanged;
	FDocAudioChannelNative OnChannelStateChangedNative;

	// ---- Extension points ----
	/** Seeded selection for reproducible one-shot choice (not identical audio rendering). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Audio")
	void SetRandomSeed(int32 Seed);

	/**
	 * Scheduler for musical boundaries (Quartz bridge). It must call Fire exactly once on
	 * the game thread at the boundary, or return false if it cannot schedule.
	 */
	using FScheduler = TFunction<bool(EDocAudioQuantization /*Boundary*/, const UDocAudioStateProfile* /*Profile*/, TFunction<void()> /*Fire*/)>;
	void SetScheduler(FScheduler InScheduler) { Scheduler = MoveTemp(InScheduler); }
	bool HasSchedulingCapability() const { return (bool)Scheduler; }

	/** Replace playback (tests, bridges). Stops everything owned by the previous backend. */
	void SetPlaybackBackend(TSharedPtr<IDocAudioPlaybackBackend> InBackend);
	IDocAudioPlaybackBackend* GetPlaybackBackend() const { return Backend.Get(); }

	using FLoader = TFunction<void(const TArray<FSoftObjectPath>& /*Paths*/, TFunction<void()> /*OnComplete*/)>;
	/** Tests: take over asset loading to control completion order. */
	void SetLoaderForTesting(FLoader InLoader) { LoaderOverride = MoveTemp(InLoader); }
	/** Tests: drive the director without world ticking. */
	void AdvanceForTesting(float DeltaSeconds) { TickDirector(DeltaSeconds); }
	double GetClock() const { return Clock; }

	// ---- Emitters ----
	void RegisterEmitter(UDocAudioEmitterComponent* Emitter);
	void UnregisterEmitter(UDocAudioEmitterComponent* Emitter);
	/** Tests/hosts without player controllers: explicit listener positions. Empty = use local players. */
	void SetListenerLocationsOverride(const TArray<FVector>& Locations) { ListenerOverride = Locations; }
	int32 GetActiveEmitterCount() const;
	bool IsEmitterVoiceActive(const UDocAudioEmitterComponent* Emitter) const;

	//~ USubsystem / tickable
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { TickDirector(DeltaTime); }
	virtual TStatId GetStatId() const override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	struct FRequest
	{
		TWeakObjectPtr<UObject> Owner;
		TObjectPtr<UDocAudioStateProfile> Profile;
		int32 TieBreaker = 0;
		int64 Sequence = 0;
	};

	struct FVoice
	{
		int32 Id = 0;
		FGameplayTag Layer;
		TObjectPtr<USoundBase> Sound;
		float Volume = 1.f;
		bool bLoop = false;
		bool bOneShot = false;
		/** >0: fading out; stop at this clock time. */
		double FadeDeadline = 0.0;
	};

	struct FPlaying
	{
		TObjectPtr<UDocAudioStateProfile> Profile;
		TArray<FVoice> Voices;
		TArray<TObjectPtr<USoundBase>> OneShotSounds;
		double NextOneShotTime = 0.0;
		bool bFadingOut = false;
		bool bOneShotProfile = false;
	};

	struct FPending
	{
		int64 Id = 0;
		TObjectPtr<UDocAudioStateProfile> Profile;
		EDocAudioTransitionState State = EDocAudioTransitionState::Requested;
		double RequestedAt = 0.0;
		double Deadline = 0.0;
		bool bOneShot = false;
		bool bIsFallback = false;
		TSharedPtr<FStreamableHandle> LoadHandle;
	};

	struct FChannel
	{
		FGameplayTag Tag;
		EDocAudioBlendRule Rule = EDocAudioBlendRule::Exclusive;
		EDocAudioTransitionState State = EDocAudioTransitionState::Idle;
		TArray<FPlaying> Playing;
		TArray<FPending> Pending;
		TMap<FGameplayTag, bool> LayerOverrides;
		/** Profiles whose last transition failed; not retried until the channel's requests change. */
		TSet<FObjectKey> FailedProfiles;
		int32 EffectiveRequests = 0;
		double LastRequestedTime = 0.0;
		double LastActualTime = 0.0;
		FString LastDiagnostic;
	};

	FChannel& GetOrAddChannel(const FGameplayTag& Tag);
	void Recompute(const FGameplayTag& ChannelTag);
	void RecomputeAll();
	bool IsRequestValid(const FRequest& Request) const;
	void BeginTransition(FChannel& Channel, UDocAudioStateProfile* Profile, bool bOneShot, bool bIsFallback);
	void CancelPending(FChannel& Channel, FPending& Pending);
	void OnPendingLoaded(FGameplayTag ChannelTag, int64 PendingId);
	void CommitPending(FGameplayTag ChannelTag, int64 PendingId);
	void FailPending(FChannel& Channel, int32 PendingIndex, EDocAudioTransitionState FailState, const FString& Why);
	void StartProfile(FChannel& Channel, UDocAudioStateProfile* Profile, bool bOneShot);
	void FadeOutPlaying(FChannel& Channel, FPlaying& Playing);
	bool IsLayerEnabled(const FChannel& Channel, const FDocAudioLayerRef& Ref) const;
	int32 StartLayerVoice(FChannel& Channel, FPlaying& Playing, const FDocAudioLayerRef& Ref, bool bOneShot, float FadeIn);
	void SetState(FChannel& Channel, EDocAudioTransitionState NewState, const FGameplayTag& ProfileId);
	void TickDirector(float DeltaSeconds);
	void TickVoices(FChannel& Channel);
	void TickOneShots(FChannel& Channel);
	void TickEmitters();
	void PruneDeadOwners();
	IDocAudioPlaybackBackend& EnsureBackend();
	bool ConsumeOneShotBudget();
	/** State events are queued and broadcast after internal bookkeeping, so handlers may call back in safely. */
	void FlushEvents();
	struct FQueuedEvent { FGameplayTag Channel; FGameplayTag Profile; EDocAudioTransitionState State; };
	TArray<FQueuedEvent> QueuedEvents;
	bool bFlushingEvents = false;

	TDocHandleTable<FRequest> Requests;
	TMap<FGameplayTag, FChannel> Channels;
	TMap<FName, FGameplayTagContainer> ContextSources;
	TMap<TWeakObjectPtr<ULocalPlayer>, FGameplayTagContainer> ListenerContexts;
	TSharedPtr<IDocAudioPlaybackBackend> Backend;
	FStreamableManager Streamable;
	FLoader LoaderOverride;
	FScheduler Scheduler;
	FRandomStream Random;
	TArray<double> RecentOneShots;
	int64 NextSequence = 1;
	int64 NextPendingId = 1;
	double Clock = 0.0;
	bool bDeinitializing = false;

	// Emitters
	struct FEmitterState
	{
		TWeakObjectPtr<UDocAudioEmitterComponent> Emitter;
		int32 Voice = 0;
	};
	TArray<FEmitterState> Emitters;
	TArray<TPair<int32, double>> FadingEmitterVoices;
	TArray<TSharedPtr<FStreamableHandle>> EmitterLoads;
	TArray<FVector> ListenerOverride;
	double NextEmitterEvaluation = 0.0;
};

/**
 * Positional ambient emitter under the director's emitter budget (handoff 10.5): the
 * nearest/highest-priority emitters within range play; the rest stay silent.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCADAPTIVEAUDIORUNTIME_API UDocAudioEmitterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitter")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitter", meta = (ClampMin = "0.0"))
	float Volume = 1.f;

	/** Audible range in centimeters (budget selection only; attenuation stays in the sound asset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitter", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitter")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitter", meta = (ClampMin = "0.0"))
	float FadeSeconds = 0.5f;

	UFUNCTION(BlueprintPure, Category = "Doc|Audio")
	bool IsEmitterActive() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
