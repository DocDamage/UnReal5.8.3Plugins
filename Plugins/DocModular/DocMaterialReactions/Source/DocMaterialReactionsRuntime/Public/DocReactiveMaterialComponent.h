#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocMaterialReactionTypes.h"
#include "DocMaterialProfile.h"
#include "DocMaterialReactionDefinition.h"
#include "DocReactiveMaterialComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocMaterialStateChangedDynamicDelegate, const FDocMaterialState&, NewState, FName, Cause);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocMaterialStateChangedNativeDelegate, const FDocMaterialState&, FName);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocReactionStartedDynamicDelegate, FName, ReactionId, const FDocReactionTransition&, Transition);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocReactionStartedNativeDelegate, FName, const FDocReactionTransition&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocReactionEndedDynamicDelegate, FName, ReactionId, const FDocReactionTransition&, Transition);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocReactionEndedNativeDelegate, FName, const FDocReactionTransition&);

/**
 * Owns one object's material facts and reaction instances.
 * All mutations commit first; events are broadcast afterwards with the committed revision and transition id.
 * Gameplay mutation requires authority on the owning actor.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCMATERIALREACTIONSRUNTIME_API UDocReactiveMaterialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocReactiveMaterialComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Stable id used by the subsystem, contacts and propagation. Assigned from owner/component names when None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FName ReactiveObjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	TObjectPtr<UDocMaterialProfile> MaterialProfile;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	FDocMaterialState CurrentState;

	UFUNCTION(BlueprintCallable, Category = "Material")
	bool InitializeFromProfile(UDocMaterialProfile* NewProfile);

	/** Adds or replaces the sample owned by Sample.SourceId. Refuses invalid or stale (lower Sequence) samples. */
	UFUNCTION(BlueprintCallable, Category = "Exposure")
	bool AddExposureSource(const FDocExposureSample& Sample);

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	bool UpdateExposureSource(FName SourceId, float NewIntensity);

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	bool RemoveExposureSource(FName SourceId);

	/** Combined, capped value for a channel under the profile's rule. */
	UFUNCTION(BlueprintCallable, Category = "Exposure")
	float GetAggregatedChannelIntensity(FGameplayTag Channel) const;

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	int32 GetExposureSourceCount() const { return ExposureSources.Num(); }

	/** Source-owned suppression. DurationSeconds <= 0 = permanent until this source revokes it. Ends the reaction if active. */
	UFUNCTION(BlueprintCallable, Category = "Suppression")
	void SuppressReaction(FName ReactionId, FName SourceId, float DurationSeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Suppression")
	void UnsuppressReaction(FName ReactionId, FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Suppression")
	bool IsReactionSuppressed(FName ReactionId) const;

	/** Starts a reaction now, bypassing exposure and dwell but not eligibility, authority or limits. */
	UFUNCTION(BlueprintCallable, Category = "Reaction")
	bool RequestManualReaction(FName ReactionId, FName Cause = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Reaction")
	void ExtinguishReactions(FName Cause = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Reaction")
	bool IsReactionActive(FName ReactionId) const;

	/** Reaction instances that are not Inactive, sorted by ReactionId. */
	UFUNCTION(BlueprintCallable, Category = "Reaction")
	TArray<FDocReactionInstance> GetActiveReactions() const;

	UFUNCTION(BlueprintCallable, Category = "Reaction")
	bool GetReactionInstance(FName ReactionId, FDocReactionInstance& OutInstance) const;

	/** Reason code for the last refused request on this component. */
	UFUNCTION(BlueprintCallable, Category = "Diagnostics")
	FName GetLastRejectReason() const { return LastRejectReason; }

	/** Advances one step. CurrentTime is the simulation clock used for exposure and suppression expiry. */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StepSimulation(float DeltaTime, double CurrentTime);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	double GetSimulationTime() const { return SimTime; }

	void SetSimulationTime(double InTime) { SimTime = InTime; }
	void SetMaxActiveReactions(int32 InMax) { MaxActiveReactions = FMath::Max(1, InMax); }

	/** Drops every live exposure sample. Saved facts are kept. */
	void ClearLiveExposure();

	/** Removes samples whose SourceId starts with Prefix; returns the count. */
	int32 RemoveExposureSourcesWithPrefix(const FString& Prefix);

	FDocMaterialSnapshot CaptureSnapshot() const;

	/** Validates the whole snapshot first. On success restores facts and reactions without start events or consumption. */
	bool RestoreSnapshot(const FDocMaterialSnapshot& Snapshot, FString& OutError);

	// Events
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FDocMaterialStateChangedDynamicDelegate OnMaterialStateChanged;
	FDocMaterialStateChangedNativeDelegate OnMaterialStateChangedNative;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FDocReactionStartedDynamicDelegate OnReactionStarted;
	FDocReactionStartedNativeDelegate OnReactionStartedNative;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FDocReactionEndedDynamicDelegate OnReactionEnded;
	FDocReactionEndedNativeDelegate OnReactionEndedNative;

	/** Nested mutations from event handlers deeper than this are refused (RecursionLimit). */
	static constexpr int32 MaxNestedTransitionDepth = 4;

private:
	struct FPendingEvents
	{
		TArray<FDocReactionTransition> Started;
		TArray<FDocReactionTransition> Ended;
		bool bStateChanged = false;
		FName Cause = NAME_None;
	};

	UPROPERTY()
	TMap<FName, FDocExposureSample> ExposureSources;

	/** ReactionId -> (SourceId -> absolute expiry on the simulation clock; negative = permanent). */
	TMap<FName, TMap<FName, double>> SuppressionSources;

	UPROPERTY()
	TMap<FName, FDocReactionInstance> ActiveReactions;

	double SimTime = 0.0;
	int32 MaxActiveReactions = 8;
	int32 BroadcastDepth = 0;
	FName LastRejectReason = NAME_None;

	bool Reject(FName Reason);
	bool CanMutate();
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();

	float ComputeChannel(const FGameplayTag& Channel, int32* OutMinGeneration = nullptr) const;
	int32 CountActive() const;

	FDocReactionTransition MakeStart(const UDocMaterialReactionDefinition& Def, FDocReactionInstance& Inst, FName Cause, int32 Generation, FDocMaterialState& Work);
	FDocReactionTransition MakeEnd(const UDocMaterialReactionDefinition* Def, FDocReactionInstance& Inst, EDocReactionState NewState, FName Cause, FDocMaterialState& Work);

	/** Commits Work as the new state (bumping revision once) and broadcasts pending events. */
	void Commit(FDocMaterialState& Work, FPendingEvents& Events);
};
