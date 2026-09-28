#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Engine/StreamableManager.h"
#include "Engine/EngineTypes.h"
#include "Math/RandomStream.h"
#include "DocSurfaceFeedbackTypes.h"
#include "DocSurfaceFeedbackSubsystem.generated.h"

/**
 * Executes one response kind. Registered per kind; a kind without an executor is
 * Unsupported. Game thread only. Bridges register MetaSound/Niagara executors.
 */
class DOCSURFACEFEEDBACKRUNTIME_API IDocSurfaceResponseExecutor
{
public:
	virtual ~IDocSurfaceResponseExecutor() = default;
	/** Cosmetic executors are skipped on dedicated servers. */
	virtual bool IsCosmetic() const { return true; }
	/** Returns Executed (with an optional instance id for tracking/continuous control) or a failure admission. */
	virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
		UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) = 0;
	virtual bool IsInstanceActive(int32 Instance) const { return false; }
	virtual void UpdateInstance(int32 Instance, const FVector& Location, float Scale) {}
	virtual void StopInstance(int32 Instance) {}
};

/** Authorized gameplay responses (GameplayEvent/GameplayTag). The base never mutates tags itself. */
class DOCSURFACEFEEDBACKRUNTIME_API IDocSurfaceGameplayProvider
{
public:
	virtual ~IDocSurfaceGameplayProvider() = default;
	virtual FDocSystemResult ApplySurfaceGameplayResponse(const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response) = 0;
};

/** Host surface-tag provider (third mapping stage). */
class DOCSURFACEFEEDBACKRUNTIME_API IDocSurfaceTagProvider
{
public:
	virtual ~IDocSurfaceTagProvider() = default;
	virtual FGameplayTag GetSurfaceTag(const FDocSurfaceFeedbackRequest& Request) const = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDocSurfaceFeedbackNative, const FDocSurfaceFeedbackResult&);

/**
 * World-scoped resolution and dispatch (expansion handoff Section 5).
 * ResolveFeedback is pure (no loading, no playback). SubmitFeedback resolves, applies
 * budgets/cooldowns/dedupe, loads missing variations asynchronously (with TTL and
 * owner-loss cancellation) and dispatches to executors.
 */
UCLASS()
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocSurfaceFeedbackSubsystem* Get(const UObject* WorldContextObject);

	/** Side-effect-free: surface mapping + rule selection + chosen variation descriptors. */
	UFUNCTION(BlueprintPure, Category = "Doc|Surface")
	FDocSurfaceFeedbackResult ResolveFeedback(const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceResponseProfile* Profile) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSurfaceFeedbackResult SubmitFeedback(FDocSurfaceFeedbackRequest Request, UDocSurfaceResponseProfile* Profile);

	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSystemResult UpdateContinuous(FDocRequestHandle Handle, FVector Location, float Magnitude);

	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSystemResult StopContinuous(FDocRequestHandle Handle);

	/** Surface mapping stage only. */
	FGameplayTag ResolveSurface(const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceMappingAsset* Mapping, EDocSurfaceOrigin& OutOrigin) const;

	void RegisterExecutor(EDocSurfaceResponseKind Kind, TSharedPtr<IDocSurfaceResponseExecutor> Executor);
	void UnregisterExecutor(EDocSurfaceResponseKind Kind);
	void SetGameplayProvider(TSharedPtr<IDocSurfaceGameplayProvider> Provider) { GameplayProvider = Provider; }
	void SetTagProvider(TSharedPtr<IDocSurfaceTagProvider> Provider) { TagProvider = Provider; }

	/** Debug history (bounded; disabled by default and in Shipping). */
	const TArray<FDocSurfaceFeedbackResult>& GetHistory() const { return History; }
	FDocSurfaceFeedbackNative OnFeedbackDispatchedNative;

	// ---- Tests ----
	using FLoader = TFunction<void(const TArray<FSoftObjectPath>&, TFunction<void()>)>;
	void SetLoaderForTesting(FLoader InLoader) { LoaderOverride = MoveTemp(InLoader); }
	void SetViewLocationsOverride(const TArray<FVector>& Locations) { ViewOverride = Locations; bUseViewOverride = true; }
	void SetCosmeticsDisabledForTesting(bool bDisabled) { bForceNoCosmetics = bDisabled; }
	void AdvanceForTesting(float DeltaSeconds) { TickFeedback(DeltaSeconds); }
	int32 GetQueuedCount() const { return Queued.Num(); }
	double GetClock() const { return Clock; }

	//~ USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { TickFeedback(DeltaTime); }
	virtual TStatId GetStatId() const override;

private:
	struct FCandidate
	{
		const FDocSurfaceResponseRule* Rule = nullptr;
		EDocSurfaceMatchPath Path = EDocSurfaceMatchPath::None;
		int32 EventSpecificity = 0;
		int32 SurfaceSpecificity = 0;
		int32 ContextCount = 0;
	};

	struct FQueuedRequest
	{
		FDocSurfaceFeedbackRequest Request;
		TWeakObjectPtr<UDocSurfaceResponseProfile> Profile;
		FDocSurfaceFeedbackResult Result;
		int64 LoadId = 0;
		bool bHadInstigator = false;
		TSharedPtr<FStreamableHandle> Handle;
	};

	struct FContinuous
	{
		EDocSurfaceResponseKind Kind = EDocSurfaceResponseKind::Sound;
		int32 Instance = 0;
		TWeakObjectPtr<AActor> Source;
		bool bHadSource = false;
		float MinScale = 1.f, MaxScale = 1.f;
	};

	struct FActiveInstance
	{
		EDocSurfaceResponseKind Kind = EDocSurfaceResponseKind::Sound;
		int32 Instance = 0;
	};

	void Dispatch(FDocSurfaceFeedbackResult& Result, const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceResponseProfile* Profile);
	bool IsCosmeticAllowed(const FDocSurfaceFeedbackRequest& Request, FString& OutReason) const;
	int32 CountActive(EDocSurfaceResponseKind Kind) const;
	int32 BudgetFor(EDocSurfaceResponseKind Kind) const;
	int32 PickVariation(const FDocSurfaceResponse& Response, const FDocSurfaceFeedbackRequest& Request, const FName& RuleId) const;
	FString SourceKey(const FDocSurfaceFeedbackRequest& Request, const FName& RuleId) const;
	void OnLoaded(int64 LoadId);
	void Record(const FDocSurfaceFeedbackResult& Result);
	void TickFeedback(float DeltaSeconds);

	TMap<EDocSurfaceResponseKind, TSharedPtr<IDocSurfaceResponseExecutor>> Executors;
	TSharedPtr<IDocSurfaceGameplayProvider> GameplayProvider;
	TSharedPtr<IDocSurfaceTagProvider> TagProvider;
	TArray<FQueuedRequest> Queued;
	TDocHandleTable<FContinuous> Continuous;
	TArray<FActiveInstance> Active;
	TMap<FString, double> Cooldowns;
	TMap<int64, double> SeenCorrelations;
	/** Last variation per (source, rule, response) for no-immediate-repeat. */
	mutable TMap<FString, int32> LastVariation;
	TArray<double> RecentRequests;
	TArray<FDocSurfaceFeedbackResult> History;
	FStreamableManager Streamable;
	FLoader LoaderOverride;
	TArray<FVector> ViewOverride;
	bool bUseViewOverride = false;
	bool bForceNoCosmetics = false;
	int64 NextRequestId = 1;
	int64 NextLoadId = 1;
	double Clock = 0.0;
};

/**
 * Request producer on any actor (no movement/weapon framework required): manual
 * triggers, hit results, traced sockets (AnimNotify), distance-based steps and landings.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocSurfaceFeedbackComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TObjectPtr<UDocSurfaceResponseProfile> Profile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	FGameplayTagContainer ContextTags;

	// ---- Tracing (explicit settings; returns physical materials) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace", meta = (Units = "cm"))
	float TraceDistance = 60.f;

	// ---- Distance producer ----
	/** Emit footsteps from grounded travel. When on, footstep AnimNotifies for this component are ignored (no double-fire). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance")
	bool bDistanceFootsteps = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (Units = "cm", ClampMin = "10"))
	float StrideCm = 120.f;

	/** Moves longer than this in one update are teleports: phase resets, no steps emitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (Units = "cm"))
	float TeleportThresholdCm = 500.f;

	/** Source ids alternated by distance steps (data-driven; biped names are only an example). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance")
	TArray<FName> StepSources = { TEXT("FootL"), TEXT("FootR") };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (Units = "cm/s"))
	float MinLandingSpeed = 250.f;

	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSurfaceFeedbackResult TriggerManual(FGameplayTag EventTag, FVector Location, FVector Normal, float Magnitude, FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSurfaceFeedbackResult TriggerFromHit(FGameplayTag EventTag, const FHitResult& Hit, float Magnitude, FName SourceId);

	/** Trace down from a location (socket) and submit; used by the AnimNotify producer. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	FDocSurfaceFeedbackResult TriggerTraced(FGameplayTag EventTag, FVector From, float Magnitude, FName SourceId, bool bFromAnimNotify = false);

	/** Distance producer input: call with the owner's location and grounded state (e.g. from movement or tick). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Surface")
	int32 UpdateMovement(FVector Location, bool bGrounded, FVector Velocity);

	UFUNCTION(BlueprintPure, Category = "Doc|Surface")
	float GetStridePhase() const { return Accumulated; }

private:
	FDocSurfaceFeedbackRequest MakeRequest(FGameplayTag EventTag, float Magnitude, FName SourceId) const;

	bool bHasLast = false;
	FVector LastLocation = FVector::ZeroVector;
	bool bWasGrounded = true;
	float Accumulated = 0.f;
	int32 NextStepSource = 0;
	FVector LastAirVelocity = FVector::ZeroVector;
};

/** Footstep/impact AnimNotify: immutable; reads the executing mesh context. */
UCLASS(meta = (DisplayName = "Doc Surface Feedback"))
class DOCSURFACEFEEDBACKRUNTIME_API UDocSurfaceAnimNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (Categories = "SurfaceEvent"))
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	FName Socket;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	float Magnitude = 1.f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
