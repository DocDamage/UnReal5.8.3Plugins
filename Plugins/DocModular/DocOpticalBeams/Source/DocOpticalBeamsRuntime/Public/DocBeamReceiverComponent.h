#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DocOpticalBeamTypes.h"
#include "DocBeamReceiverComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocBeamReceiverActivatedSignature, const FDocReceiverState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocBeamReceiverDeactivatedSignature, const FDocReceiverState&, State);

DECLARE_MULTICAST_DELEGATE_OneParam(FDocBeamReceiverActivatedNative, const FDocReceiverState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocBeamReceiverDeactivatedNative, const FDocReceiverState&);

/**
 * Tracks one contribution per emitter (replaced atomically when that emitter's path is committed),
 * aggregates them in emitter-id order, and runs dwell/hysteresis on the subsystem's simulation clock.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCOPTICALBEAMSRUNTIME_API UDocBeamReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocBeamReceiverComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver")
	FName ReceiverId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver")
	bool bIsEnabled = true;

	/** Empty = every channel is accepted. AllRequiredChannels needs each listed channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver")
	FGameplayTagContainer AcceptedChannels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver", meta = (ClampMin = "0.0"))
	float MinRequiredIntensity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver", meta = (ClampMin = "0.0"))
	float RequiredDwellTimeSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver", meta = (ClampMin = "0.0"))
	float ReleaseHysteresisSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver")
	EDocBeamReceiverAggregationMode AggregationMode = EDocBeamReceiverAggregationMode::AnyEligible;

	/** Once activated, stays activated (and is restored as such) until ResetLatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver")
	bool bLatchOnActivate = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Receiver")
	FDocReceiverState State;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Receiver")
	FString LastRegistrationError;

	/** Installs or replaces the contribution owned by Contribution.EmitterId. */
	void ReplaceContribution(const FDocReceiverContribution& Contribution);

	/** Convenience for tools/tests: replace with generation 0. */
	UFUNCTION(BlueprintCallable, Category = "Receiver")
	void UpdateContribution(FName InEmitterId, float InIntensity, const FGameplayTag& InChannel);

	UFUNCTION(BlueprintCallable, Category = "Receiver")
	void RemoveContribution(FName InEmitterId);

	UFUNCTION(BlueprintCallable, Category = "Receiver")
	void ClearAllContributions();

	/** Stale-path policy: a held contribution stays visible as evidence but cannot grant eligibility. */
	void SetContributionHeld(FName InEmitterId, bool bHeld);

	bool HasContribution(FName InEmitterId) const { return ContributionMap.Contains(InEmitterId); }

	UFUNCTION(BlueprintCallable, Category = "Receiver")
	void AdvanceDwell(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Receiver")
	bool EvaluateEligibility() const;

	UFUNCTION(BlueprintPure, Category = "Receiver")
	bool IsActivated() const { return State.bIsActivated; }

	UFUNCTION(BlueprintPure, Category = "Receiver")
	const FDocReceiverState& GetReceiverState() const { return State; }

	UFUNCTION(BlueprintCallable, Category = "Receiver")
	void ResetLatch();

	FDocReceiverSnapshot CaptureSnapshot() const;

	/** Restores latched activation only; clears contributions and dwell so non-latched activation must be re-earned. No events. */
	void RestoreSnapshot(const FDocReceiverSnapshot& Snapshot);

	UPROPERTY(BlueprintAssignable, Category = "Receiver|Events")
	FDocBeamReceiverActivatedSignature OnReceiverActivated;

	UPROPERTY(BlueprintAssignable, Category = "Receiver|Events")
	FDocBeamReceiverDeactivatedSignature OnReceiverDeactivated;

	FDocBeamReceiverActivatedNative OnReceiverActivatedNative;
	FDocBeamReceiverDeactivatedNative OnReceiverDeactivatedNative;

private:
	void SetActivated(bool bNewActivated);
	void RebuildView();
	bool Accepts(const FGameplayTag& Channel) const;

	TMap<FName, FDocReceiverContribution> ContributionMap;
};
