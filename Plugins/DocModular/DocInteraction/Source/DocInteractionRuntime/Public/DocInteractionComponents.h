#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"
#include "DocInteractionTypes.h"
#include "DocInteractionComponents.generated.h"

class UDocInteractionTargetProvider;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocFocusChanged, AActor*, NewFocus, AActor*, OldFocus);

/** Optional actor interface to react to interaction phases and notify/animation actions. */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocInteractionReceiver : public UInterface
{
	GENERATED_BODY()
};

class DOCINTERACTIONRUNTIME_API IDocInteractionReceiver
{
	GENERATED_BODY()

public:
	/** EventTag is a phase tag (Interaction.Phase.*) or a tag supplied by an action. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Interaction")
	void OnDocInteraction(const FDocInteractionContext& Context, FGameplayTag EventTag);
};

/** Reusable set of interaction definitions shared by many interactables. Immutable at runtime. */
UCLASS(BlueprintType)
class DOCINTERACTIONRUNTIME_API UDocInteractionProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TArray<FDocInteractionDefinition> Definitions;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/**
 * Makes its owning actor interactable (any actor class). Supplies immutable
 * definitions from an optional profile plus inline definitions (inline wins on
 * duplicate DefinitionId). Registers with UDocInteractionSubsystem while playing.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINTERACTIONRUNTIME_API UDocInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocInteractableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UDocInteractionProfile> Profile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TArray<FDocInteractionDefinition> Definitions;

	/** Candidate priority when several interactables are detected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	int32 CandidatePriority = 0;

	/** Offset from the actor origin used for distance/facing/proximity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FVector InteractionPointOffset = FVector::ZeroVector;

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void SetInteractionEnabled(bool bInEnabled) { bInteractionEnabled = bInEnabled; }

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	bool IsInteractionEnabled() const { return bInteractionEnabled; }

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	FVector GetInteractionLocation() const;

	/** Returns the definition or null. The pointer is valid until definitions change. */
	const FDocInteractionDefinition* FindDefinition(FName DefinitionId) const;

	/** Effective definitions (profile + inline), inline overriding profile by id. */
	void GetEffectiveDefinitions(TArray<const FDocInteractionDefinition*>& Out) const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	UPROPERTY(EditAnywhere, Category = "Interaction")
	bool bInteractionEnabled = true;
};

/**
 * Detects interactables for its owning actor (any actor; AI and scripted actors
 * may skip detection and call RequestInteractionWithTarget directly).
 *
 * Candidate selection is deterministic: eligible first, then CandidatePriority,
 * then provider score, then distance, then actor name as a stable tie-breaker.
 * Providers that find the same interactable produce one candidate.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINTERACTIONRUNTIME_API UDocInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocInteractorComponent();

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Detection")
	TArray<TObjectPtr<UDocInteractionTargetProvider>> Providers;

	/** Run detection automatically at DetectionInterval. Disable for AI/scripted interactors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	bool bAutoDetect = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "s"))
	float DetectionInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "1"))
	int32 MaxCandidates = 8;

	/**
	 * Focus hysteresis: a new best candidate replaces the current focus only when it
	 * scores at least this much higher (after priority), or the current one is lost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0"))
	float FocusSwitchScoreMargin = 0.05f;

	/** Actors ignored by all providers (the owner is always ignored). */
	UPROPERTY(EditAnywhere, Category = "Detection")
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void AddIgnoredActor(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void ClearIgnoredActors();

	/** Target for the ExplicitTarget provider (set by AI/scripts). */
	UPROPERTY(BlueprintReadWrite, Category = "Detection")
	TWeakObjectPtr<AActor> ExplicitTarget;

	// ---- Queries (no side effects) ----

	/** Run all providers now and return sorted, de-duplicated candidates. Does not change focus. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	TArray<FDocInteractionCandidate> QueryCandidates() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	const TArray<FDocInteractionCandidate>& GetCandidates() const { return Candidates; }

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	AActor* GetFocusedActor() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	UDocInteractableComponent* GetFocusedInteractable() const { return Focused.Get(); }

	/** Options on the focused target with availability and failure reasons (UI view model). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	TArray<FDocInteractionOption> GetFocusedOptions() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	int32 GetSelectedOptionIndex() const { return SelectedOption; }

	// ---- Commands ----

	/** Re-run detection and focus selection now. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void RefreshDetection();

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void SelectNextOption();

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void SelectPreviousOption();

	/** Start the selected option on the focused target (input "pressed"). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocInteractionSessionInfo BeginSelectedInteraction();

	/** Input "released": completes Continuous/Repeated sessions and cancels unfinished holds. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void EndActiveInteraction();

	/** Explicit request (AI, scripts, tests). Target must have a UDocInteractableComponent. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocInteractionSessionInfo RequestInteractionWithTarget(AActor* Target, FName DefinitionId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocSystemResult CancelActiveInteraction();

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	FDocRequestHandle GetActiveSession() const { return ActiveSession; }

	// ---- Events ----

	UPROPERTY(BlueprintAssignable, Category = "Doc|Interaction")
	FDocInteractionSessionEvent OnSessionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Interaction")
	FDocInteractionSessionEvent OnSessionProgress;

	/** Terminal: Completed, Cancelled, Failed or Rejected. Fires exactly once per session. */
	UPROPERTY(BlueprintAssignable, Category = "Doc|Interaction")
	FDocInteractionSessionEvent OnSessionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Interaction")
	FDocFocusChanged OnFocusChanged;

	// Called by the subsystem.
	void NotifySessionStarted(const FDocInteractionSessionInfo& Info);
	void NotifySessionProgress(const FDocInteractionSessionInfo& Info);
	void NotifySessionEnded(const FDocInteractionSessionInfo& Info);

	/** Viewpoint used by trace providers: owner's eyes viewpoint (works for any actor). */
	void GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void UpdateFocus(const TArray<FDocInteractionCandidate>& NewCandidates);

	UPROPERTY(Transient)
	TArray<FDocInteractionCandidate> Candidates;

	TWeakObjectPtr<UDocInteractableComponent> Focused;
	float FocusedScore = 0.f;
	int32 SelectedOption = 0;
	FDocRequestHandle ActiveSession;
	float TimeSinceDetection = 0.f;
};
