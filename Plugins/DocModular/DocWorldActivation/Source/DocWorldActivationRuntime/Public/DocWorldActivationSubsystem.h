#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "DocActivationTypes.h"
#include "DocWorldActivationSubsystem.generated.h"

/**
 * Registers a loaded actor as a logical object for activation (handoff 14.1).
 * Unregistering on EndPlay (stream unload, travel) is never gameplay destruction.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCWORLDACTIVATIONRUNTIME_API UDocWorldActivationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocWorldActivationComponent();

	/** Stable logical id for debugging/adapters (defaults to the owner's name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	FName LogicalId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	TObjectPtr<UDocActivationPolicy> Policy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	TObjectPtr<UDocActivationProfile> Profile;

	/** Exactly which resources the base adapter may change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	FDocActivationCapabilities Controlled;

	UFUNCTION(BlueprintPure, Category = "Doc|Activation")
	EDocActivationTier GetCurrentTier() const { return CurrentTier; }

	UPROPERTY(BlueprintAssignable, Category = "Doc|Activation")
	FDocActivationTierEvent OnTierChanged;

	/** Set by the subsystem when a transition commits. */
	void SetCurrentTierInternal(EDocActivationTier Tier) { CurrentTier = Tier; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	EDocActivationTier CurrentTier = EDocActivationTier::Active;
};

/**
 * Gameplay LOD for loaded logical objects (handoff Section 14). Streaming decides
 * whether content is loaded; Activation decides how expensive its loaded representation is.
 *
 * Evaluate desired tier (policy with hysteresis/dwell, max with pins, clamped, unsupported
 * tiers mapped to a configured safe tier) → queue → budgeted transitions:
 * guard → capture → prepare → validate → commit → release. The base adapter changes only
 * declared resources and records owned overrides; it restores a value only if the host has
 * not changed it since (reconcile), so stale snapshots never overwrite host changes.
 */
UCLASS()
class DOCWORLDACTIVATIONRUNTIME_API UDocWorldActivationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocWorldActivationSubsystem* Get(const UObject* WorldContextObject);

	void Register(UDocWorldActivationComponent* Object);
	/** Unload/travel. Drops pins and restores owned overrides; never reports destruction. */
	void Unregister(UDocWorldActivationComponent* Object);

	// ---- Pins ----
	/** Hold at least MinTier while Owner is alive (and until Duration expires if > 0). Bypasses dwell. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	FDocRequestHandle PinTier(UDocWorldActivationComponent* Object, EDocActivationTier MinTier, UObject* Owner, FName Reason, float DurationSeconds = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	FDocSystemResult ReleasePin(FDocRequestHandle Pin, UObject* Owner);

	// ---- Manual policy ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	FDocSystemResult SetManualTier(UDocWorldActivationComponent* Object, EDocActivationTier Tier);

	// ---- Relevance sources ----
	/** Extra sources (vehicles, cinematic targets, cameras). Adding one prioritizes nearby re-evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	void AddRelevanceSource(USceneComponent* Source);

	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	void RemoveRelevanceSource(USceneComponent* Source);

	/** Teleports / large moves: re-evaluate objects near Location first. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Activation")
	void RequestPriorityReevaluation(FVector Location);

	/** Tests/hosts without players: fixed source locations (replace the player sources). */
	void SetSourceLocationsOverride(const TArray<FVector>& Locations) { SourceOverride = Locations; bUseSourceOverride = true; }

	// ---- Adapters ----
	void RegisterRepresentationAdapter(TSharedPtr<IDocActivationRepresentationAdapter> Adapter);
	void UnregisterRepresentationAdapter(TSharedPtr<IDocActivationRepresentationAdapter> Adapter);

	// ---- Debug ----
	UFUNCTION(BlueprintPure, Category = "Doc|Activation")
	FDocActivationDebugInfo GetDebugInfo(const UDocWorldActivationComponent* Object) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Activation")
	FDocActivationStats GetStats() const { return Stats; }

	FDocActivationTierNative OnTierChangedNative;

	void AdvanceForTesting(float DeltaSeconds) { TickActivation(DeltaSeconds); }

	//~ USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { TickActivation(DeltaTime); }
	virtual TStatId GetStatId() const override;

private:
	struct FOwnedOverride
	{
		bool bActive = false;
		// Original host values and the values we set (reconcile before restoring).
		bool bOrigTickEnabled = true;
		float OrigTickInterval = 0.f;
		bool bSetTickEnabled = true;
		float SetTickInterval = 0.f;
		bool bOrigHidden = false;
		bool bSetHidden = false;
		bool bOrigCollision = true;
		bool bSetCollision = true;
		TMap<TWeakObjectPtr<UActorComponent>, TPair<float, float>> ComponentIntervals; // orig, set
		TMap<TWeakObjectPtr<UActorComponent>, TPair<bool, bool>> ComponentTickEnabled; // orig, set
		bool bHasTick = false, bHasHidden = false, bHasCollision = false;
	};

	struct FRecord
	{
		TWeakObjectPtr<UDocWorldActivationComponent> Object;
		EDocActivationTier Current = EDocActivationTier::Active;
		EDocActivationTier Desired = EDocActivationTier::Active;
		TOptional<EDocActivationTier> Manual;
		double TierEnteredAt = 0.0;
		double NextEvaluation = 0.0;
		float NearestCm = -1.f;
		bool bQueued = false;
		FIntVector Cell = FIntVector::ZeroValue;
		FName Representation = TEXT("LoadedActor");
		int32 Transitions = 0;
		FString LastDiagnostic;
		FOwnedOverride Override;
	};

	struct FPin
	{
		TWeakObjectPtr<UDocWorldActivationComponent> Object;
		TWeakObjectPtr<UObject> Owner;
		EDocActivationTier MinTier = EDocActivationTier::Active;
		FName Reason;
		double ExpiresAt = 0.0;
	};

	FRecord* FindRecord(const UDocWorldActivationComponent* Object);
	const FRecord* FindRecord(const UDocWorldActivationComponent* Object) const;
	void GatherSources(TArray<FVector>& Out) const;
	void Evaluate(FRecord& Record, const TArray<FVector>& Sources);
	EDocActivationTier ComputeDesired(FRecord& Record, const TArray<FVector>& Sources, bool& bOutBypassDwell);
	EDocActivationTier PinnedMinimum(const UDocWorldActivationComponent* Object, int32* OutCount = nullptr) const;
	bool IsTierSupported(const FRecord& Record, EDocActivationTier Tier, IDocActivationRepresentationAdapter** OutAdapter) const;
	bool Transition(FRecord& Record, EDocActivationTier To);
	void ApplyBaseTier(FRecord& Record, EDocActivationTier Tier);
	void RestoreOwnedOverrides(FRecord& Record);
	FIntVector CellOf(const FVector& Location) const;
	void TickActivation(float DeltaSeconds);
	void UpdateCell(const FObjectKey& Key, FRecord& Record);
	void RemoveRecord(const FObjectKey& Key);
	void NotifyTierChanged(FRecord& Record, EDocActivationTier OldTier, EDocActivationTier NewTier);

	TMap<FObjectKey, FRecord> Records;
	/** Round-robin evaluation order, prioritized re-evaluations and queued transitions (keys, never indices). */
	TArray<FObjectKey> Order;
	TArray<FObjectKey> PriorityQueue;
	TArray<FObjectKey> TransitionQueue;
	TMap<FIntVector, TArray<FObjectKey>> Cells;
	int32 RoundRobin = 0;
	TDocHandleTable<FPin> Pins;
	TArray<TWeakObjectPtr<USceneComponent>> ExtraSources;
	TArray<FVector> SourceOverride;
	bool bUseSourceOverride = false;
	TArray<TSharedPtr<IDocActivationRepresentationAdapter>> Adapters;
	FDocActivationStats Stats;
	TArray<double> RecentTransitions;
	double Clock = 0.0;
};
