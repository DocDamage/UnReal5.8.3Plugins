#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/SceneComponent.h"
#include "Math/RandomStream.h"
#include "DocWeatherTypes.h"
#include "DocWeatherSubsystem.generated.h"

class UDocWeatherRegionOverrideComponent;

/** Renderer/audio adapter (SkyAtmosphere, VolumetricClouds, Niagara, materials...). Bridges implement it. */
class DOCWEATHERRUNTIME_API IDocWeatherRenderAdapter
{
public:
	virtual ~IDocWeatherRenderAdapter() = default;
	virtual FName GetAdapterName() const = 0;
	/** Global state only, spatial samples, or per-view (documented by each adapter). */
	virtual void ApplyGlobalState(const FDocWeatherState& State) = 0;
	/** Release only this adapter's own controls (never overwrite a newer renderer owner). */
	virtual void ReleaseControls() = 0;
};

/** TimeDriven / RegionDriven / Scripted scheduling input (bridge). */
class DOCWEATHERRUNTIME_API IDocWeatherScheduleProvider
{
public:
	virtual ~IDocWeatherScheduleProvider() = default;
	/** Return the profile/transition to move to now, or null to keep the current one. */
	virtual UDocWeatherProfile* PickProfile(double Clock, const UDocWeatherProfile* Current, UDocWeatherTransitionDefinition*& OutTransition) = 0;
};

/**
 * World weather state (expansion handoff Section 4). Computes state only; it never
 * searches for sky Blueprints or owns lights. Rendering, audio and networking are adapters.
 *
 * Clock: the weather clock advances with world ticks (pauses and dilation included)
 * or explicit AdvanceClock jumps (bounded catch-up, no event replay). Backward jumps are
 * rejected. Random selection and lightning use separate streams with saved state.
 */
UCLASS()
class DOCWEATHERRUNTIME_API UDocWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocWeatherSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") void RegisterProfile(UDocWeatherProfile* Profile);
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") void RegisterTransition(UDocWeatherTransitionDefinition* Transition);

	/** Apply a profile via a transition (null = immediate). Invalid profiles are rejected without partial application. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather")
	FDocSystemResult SetWeather(UDocWeatherProfile* Profile, UDocWeatherTransitionDefinition* Transition, EDocWeatherCommandPolicy Policy, int64& OutTransitionId);

	/** Cancel the running transition, holding its current evaluated state. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") FDocSystemResult CancelTransition();

	UFUNCTION(BlueprintCallable, Category = "Doc|Weather")
	FDocSystemResult SetSchedule(UDocWeatherSchedule* Schedule, int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") void SetSchedulerPaused(bool bPaused);

	/** Forward clock jump (time skip). Bounded work; skipped lightning is not replayed. Backward = Unsupported. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") FDocSystemResult AdvanceClock(double Seconds);

	UFUNCTION(BlueprintPure, Category = "Doc|Weather") FDocWeatherState GetGlobalWeatherState() const { return Current; }
	/** Global state blended with regional overrides at Location (base remainder + normalized weights). */
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") FDocWeatherState SampleWeatherAtLocation(FVector Location) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") FName GetCurrentProfileId() const { return CurrentProfile ? CurrentProfile->ProfileId : NAME_None; }
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") bool IsTransitioning() const { return Transition.bActive; }
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") int64 GetRevision() const { return Revision; }
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") double GetClock() const { return Clock; }
	UFUNCTION(BlueprintPure, Category = "Doc|Weather") EDocWeatherScheduleMode GetMode() const { return Mode; }

	// ---- Events (profile-level, not per frame) ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherProfileEvent OnWeatherStarted;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherProfileEvent OnWeatherEnding;
	/** Evaluated state changed significantly (revision bumped). */
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherProfileEvent OnWeatherChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherTransitionEvent OnTransitionStarted;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherTransitionEvent OnTransitionCompleted;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Weather") FDocWeatherLightningEvent OnLightning;
	FDocWeatherTransitionNative OnTransitionCompletedNative;
	FDocWeatherLightningNative OnLightningNative;

	// ---- Regional overrides, adapters, providers ----
	void RegisterOverride(UDocWeatherRegionOverrideComponent* Override);
	void UnregisterOverride(UDocWeatherRegionOverrideComponent* Override);
	void RegisterAdapter(TSharedPtr<IDocWeatherRenderAdapter> Adapter);
	/** Adapter releases its own controls; others are untouched. */
	void UnregisterAdapter(TSharedPtr<IDocWeatherRenderAdapter> Adapter);
	void SetScheduleProvider(TSharedPtr<IDocWeatherScheduleProvider> Provider) { ScheduleProvider = Provider; }

	// ---- Persistence ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") FDocWeatherSaveData CaptureState() const;
	/** Validates before applying; restoring emits no started/lightning events. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Weather") FDocSystemResult RestoreState(const FDocWeatherSaveData& Data);

	int32 GetDroppedEventCount() const { return DroppedEvents; }
	void AdvanceForTesting(float DeltaSeconds) { Step(DeltaSeconds, /*bEmitEvents*/ true); }

	//~ USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { Step(DeltaTime, true); }
	virtual TStatId GetStatId() const override;

	/** Pure interpolation used by transitions (exposed for tests/tools). */
	static FDocWeatherState Blend(const FDocWeatherState& From, const FDocWeatherState& To, float Alpha, float CategoricalSwitchAt);

private:
	struct FTransitionState
	{
		bool bActive = false;
		int64 Id = 0;
		FDocWeatherState Source;
		TObjectPtr<UDocWeatherProfile> Target;
		TObjectPtr<UDocWeatherTransitionDefinition> Definition;
		double Elapsed = 0.0;
		double Duration = 0.0;
	};

	struct FQueued
	{
		TObjectPtr<UDocWeatherProfile> Profile;
		TObjectPtr<UDocWeatherTransitionDefinition> Definition;
		int64 Id = 0;
	};

	FDocSystemResult StartTransition(UDocWeatherProfile* Profile, UDocWeatherTransitionDefinition* Definition, int64 Id);
	void FinishTransition(EDocWeatherTransitionOutcome Outcome);
	bool EvaluateTransition(FDocWeatherState& Out, FString& OutError) const;
	void Step(double DeltaSeconds, bool bEmitEvents);
	void StepScheduler(double DeltaSeconds, int32& SelectionBudget);
	UDocWeatherProfile* PickWeighted();
	void RelaxGround(double DeltaSeconds, const FDocWeatherState& Target);
	void StepLightning(double DeltaSeconds, bool bEmitEvents);
	void Publish(bool bForce);
	double DrawHold(const UDocWeatherProfile* Profile, float EntryDuration);

	FDocWeatherState Current;
	FDocWeatherState LastPublished;
	TObjectPtr<UDocWeatherProfile> CurrentProfile;
	FTransitionState Transition;
	TArray<FQueued> Queue;
	UPROPERTY() TArray<TObjectPtr<UObject>> Referenced;
	TMap<FName, TObjectPtr<UDocWeatherProfile>> ProfilesById;
	TMap<FName, TObjectPtr<UDocWeatherTransitionDefinition>> TransitionsById;
	TObjectPtr<UDocWeatherSchedule> Schedule;
	EDocWeatherScheduleMode Mode = EDocWeatherScheduleMode::Manual;
	bool bSchedulerPaused = false;
	double HoldRemaining = 0.0;
	int32 SequenceIndex = 0;
	FRandomStream SelectionRandom;
	int32 SelectionSeed = 0;
	int32 SelectionDraws = 0;
	FRandomStream EventRandom;
	int32 EventSeed = 0;
	double LightningHazard = 0.0;
	double LightningThreshold = 1.0;
	int64 NextEventId = 1;
	int64 NextTransitionId = 1;
	int64 Revision = 0;
	double Clock = 0.0;
	int32 DroppedEvents = 0;
	TArray<TWeakObjectPtr<UDocWeatherRegionOverrideComponent>> Overrides;
	TArray<TSharedPtr<IDocWeatherRenderAdapter>> Adapters;
	TSharedPtr<IDocWeatherScheduleProvider> ScheduleProvider;
};

/** Spatial override (handoff 4.5): priority, weight, blend distance and explicit fields. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCWEATHERRUNTIME_API UDocWeatherRegionOverrideComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Stable identity (categorical tie-break). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") FName RegionId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override", meta = (ClampMin = "0", ClampMax = "1")) float Weight = 1.f;
	/** Box half-extent (cm) around the component; full influence inside, fading over BlendDistance outside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override", meta = (Units = "cm")) FVector Extent = FVector(1000.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override", meta = (Units = "cm", ClampMin = "0")) float BlendDistance = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") FDocWeatherFieldMask Fields;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") FDocWeatherState Values;
	/** Interior: suppress local precipitation exposure while outdoor weather continues. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") bool bInterior = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override") bool bActive = true;

	/** Influence [0,1] at a world location. */
	float InfluenceAt(const FVector& Location) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
