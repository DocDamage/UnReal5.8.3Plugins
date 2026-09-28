#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/StreamableManager.h"
#include "Tickable.h"
#include "DocInspectionTypes.h"
#include "DocInspectionSubsystem.generated.h"

class UAudioComponent;
class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UEnhancedInputComponent;
class APlayerController;
struct FInputActionValue;

/** Makes an actor inspectable. The live actor is never moved, attached, disabled or rotated by inspection. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINSPECTIONRUNTIME_API UDocInspectableComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	TObjectPtr<UDocInspectionDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	bool bOverrideMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection", meta = (EditCondition = "bOverrideMode"))
	EDocInspectionMode Mode = EDocInspectionMode::World;

	EDocInspectionMode GetEffectiveMode() const;
};

/**
 * Isolated runtime preview stage: presentation mesh + light + scene capture into a
 * session-owned render target. Only this actor's primitives are rendered
 * (ShowOnly list). It never instantiates the live actor's class.
 */
UCLASS(NotPlaceable, NotBlueprintable, Transient)
class DOCINSPECTIONRUNTIME_API ADocInspectionPreviewStage : public AActor
{
	GENERATED_BODY()

public:
	ADocInspectionPreviewStage();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;

	void Setup(UStaticMesh* InMesh, UTextureRenderTarget2D* Target, float LightIntensity);
	/** Place the capture on an orbit around the mesh bounds centre. */
	void ApplyView(const FRotator& Orbit, const FVector2D& Pan, float Distance);
	void CaptureNow();
};

/**
 * Per-local-player inspection (handoff Section 12). One session at a time,
 * replace/reject configurable. A missing local player is an explicit error.
 *
 * Resources acquired per session and released on every end path (close, failure,
 * cancel during load, travel, player removal): control claims, the temporary input
 * mapping context (only if this session added it), the pushed input component, the
 * preview stage and render target, asset load handles, audio (unless deliberately
 * transferred to the continued-audio owner), media bridge sessions.
 */
UCLASS()
class DOCINSPECTIONRUNTIME_API UDocInspectionSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Null for a null player: never falls back to player index 0. */
	static UDocInspectionSubsystem* Get(const ULocalPlayer* Player);

	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection")
	FDocSystemResult OpenInspection(UDocInspectableComponent* Inspectable, FDocRequestHandle& OutSession);

	/** Inspect a definition with no live object (documents, codex images...). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection")
	FDocSystemResult OpenDefinition(UDocInspectionDefinition* Definition, FDocRequestHandle& OutSession);

	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection")
	FDocSystemResult CloseInspection(FDocRequestHandle Session);

	UFUNCTION(BlueprintPure, Category = "Doc|Inspection")
	bool IsInspecting() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Inspection")
	FDocInspectionViewModel GetViewModel() const;

	// ---- Navigation (bound to input actions, or called by UI) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") void Rotate(FVector2D Delta);
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") void Pan(FVector2D Delta);
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") void Zoom(float Delta);
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") bool NextPage();
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") bool PreviousPage();
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") void Accept();
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") void Back();

	/** Discover a focus point (idempotent; honours dependencies). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection")
	FDocSystemResult ActivateFocusPoint(FName FocusId);

	// ---- Audio logs ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") FDocSystemResult PlayAudioLog(float StartSeconds = 0.f);
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") FDocSystemResult PauseAudioLog();
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") FDocSystemResult ResumeAudioLog();
	UFUNCTION(BlueprintCallable, Category = "Doc|Inspection") FDocSystemResult StopContinuedAudio();

	// ---- Discoveries (per player; persist with a project profile key through a save bridge) ----
	UFUNCTION(BlueprintPure, Category = "Doc|Inspection")
	bool IsFocusDiscovered(FName InspectionId, FName FocusId) const;

	TMap<FName, TArray<FName>> GetDiscoveries() const;
	void RestoreDiscoveries(const TMap<FName, TArray<FName>>& In);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Inspection") FDocInspectionViewEvent OnViewModelChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Inspection") FDocInspectionDiscoveryEvent OnFocusDiscovered;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Inspection") FDocInspectionClosedEvent OnInspectionEnded;
	FDocInspectionViewNative OnViewModelChangedNative;
	FDocInspectionDiscoveryNative OnFocusDiscoveredNative;

	void SetRewardProvider(UObject* Provider);
	void SetMediaProvider(UObject* Provider);

	// ---- Tests ----
	/** Tests create the subsystem with NewObject on a bare ULocalPlayer; this supplies the world. */
	void InitializeForTesting(UWorld* World);
	using FLoader = TFunction<void(const TArray<FSoftObjectPath>&, TFunction<void()>)>;
	void SetLoaderForTesting(FLoader InLoader) { LoaderOverride = MoveTemp(InLoader); }
	void AdvanceForTesting(float DeltaSeconds) { TickInspection(DeltaSeconds); }
	ADocInspectionPreviewStage* GetPreviewStageForTesting() const { return Stage.Get(); }
	int32 GetCaptureCountForTesting() const { return CaptureCount; }

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;
	virtual UWorld* GetWorld() const override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override { TickInspection(DeltaTime); }
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Session.IsSet() || ContinuedAudio.Num() > 0; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

private:
	struct FSession
	{
		int64 Id = 0;
		TObjectPtr<UDocInspectionDefinition> Definition;
		TWeakObjectPtr<UDocInspectableComponent> Inspectable;
		EDocInspectionMode Mode = EDocInspectionMode::World;
		EDocInspectionState State = EDocInspectionState::None;
		double LoadDeadline = 0.0;
		TSharedPtr<FStreamableHandle> Load;
		int32 PageIndex = 0;
		FRotator Orbit = FRotator(-15.f, 0.f, 0.f);
		FVector2D PanOffset = FVector2D::ZeroVector;
		float Distance = 120.f;
		bool bViewDirty = true;
		double NextCaptureTime = 0.0;
		TArray<TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>> ControlClaims;
		bool bAddedMappingContext = false;
		bool bMediaOpen = false;
		FString Diagnostic;
	};

	FDocSystemResult Open(UDocInspectionDefinition* Definition, UDocInspectableComponent* Inspectable, FDocRequestHandle& OutSession);
	void OnAssetsLoaded(int64 SessionId);
	void Activate();
	void End(EDocInspectionState EndState, const FString& Reason);
	void AcquireControl();
	void InstallInput();
	void RemoveInput();
	void UpdateView(bool bForceCapture);
	void Broadcast();
	void TickInspection(float DeltaSeconds);
	bool IsFocusAvailable(const UDocInspectionDefinition* Definition, const FDocInspectionFocusPoint& Point) const;
	FTransform ComputeWorldView() const;

	void HandleRotate(const FInputActionValue& Value);
	void HandlePan(const FInputActionValue& Value);
	void HandleZoom(const FInputActionValue& Value);
	void HandleNext();
	void HandlePrevious();

	TOptional<FSession> Session;
	TWeakObjectPtr<ADocInspectionPreviewStage> Stage;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	UPROPERTY() TObjectPtr<UEnhancedInputComponent> InputComponent;
	TWeakObjectPtr<APlayerController> InputOwner;
	/** Audio deliberately transferred past its session (stopped on player removal / travel). */
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> ContinuedAudio;
	UPROPERTY() TObjectPtr<UDocInspectionDefinition> SessionDefinitionRef;
	UPROPERTY() TObjectPtr<UAudioComponent> SessionAudio;
	UPROPERTY() TArray<TObjectPtr<UObject>> LoadedAssets;
	TMap<FName, TSet<FName>> Discovered;
	TWeakObjectPtr<UObject> RewardProvider;
	TWeakObjectPtr<UObject> MediaProvider;
	TWeakObjectPtr<UWorld> WorldOverride;
	FStreamableManager Streamable;
	FLoader LoaderOverride;
	double Clock = 0.0;
	int32 CaptureCount = 0;
	bool bDeinitializing = false;
};
