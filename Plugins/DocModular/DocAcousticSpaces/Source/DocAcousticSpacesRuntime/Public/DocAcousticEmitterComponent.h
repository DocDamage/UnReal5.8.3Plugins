#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocAcousticEmitterComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocOnAcousticParametersUpdated, FName, EmitterId, float, EffectiveGain, float, EffectiveCutoffHz);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocOnAcousticParametersUpdatedNative, FName, float, float);

/**
 * Per-emitter acoustic state. The acoustic layer owns only TransmissionMultiplier and RestrictiveCutoffHz;
 * the host owns BaseGain/BaseCutoffHz (for example an adaptive-audio fade). The effective target is always
 * recomputed as BaseGain * TransmissionMultiplier, never multiplied onto the previous output.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocAcousticEmitterComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Emitter")
	FName EmitterId = NAME_None;

	/** Optional explicit membership. When set, location-based resolution is skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Emitter")
	FName ExplicitSpaceId = NAME_None;

	/** Host-owned baseline gain (fades etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Emitter", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BaseGain = 1.0f;

	/** Host-owned baseline cutoff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Emitter", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float BaseCutoffHz = 20000.0f;

	/** Acoustic-owned multiplier from the last path result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float TransmissionMultiplier = 1.0f;

	/** Acoustic-owned cutoff from the last path result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float RestrictiveCutoffHz = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float TargetEffectiveGain = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float TargetEffectiveCutoffHz = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float CurrentEffectiveGain = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	float CurrentEffectiveCutoffHz = 20000.0f;

	/** Exponential approach rate (1/s). Time-based, frame-rate independent. 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustic Emitter", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SmoothingInterpSpeed = 15.0f;

	/** Incremented by the subsystem each time this emitter registers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	int32 RegistrationGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustic Emitter")
	FString LastRegistrationError;

	UPROPERTY(BlueprintAssignable, Category = "Acoustic Emitter")
	FDocOnAcousticParametersUpdated OnParametersUpdated;

	FDocOnAcousticParametersUpdatedNative OnParametersUpdatedNative;

	/** Sets the acoustic-owned values; the target is recomputed from the current host baseline. */
	UFUNCTION(BlueprintCallable, Category = "Acoustic Emitter")
	void SetTargetParameters(float InTransmissionMultiplier, float InRestrictiveCutoffHz, bool bSnap = false);

	/** Re-reads the host baseline and moves current values toward the target. */
	UFUNCTION(BlueprintCallable, Category = "Acoustic Emitter")
	void AdvanceSmoothing(float DeltaSeconds);

	/** Jumps to the target (for example after a teleport). */
	UFUNCTION(BlueprintCallable, Category = "Acoustic Emitter")
	void ResetSmoothing();

	/** Removes acoustic influence: multiplier 1, cutoff open, snapped to the current baseline. */
	UFUNCTION(BlueprintCallable, Category = "Acoustic Emitter")
	void ClearAcousticInfluence();

	UFUNCTION(BlueprintPure, Category = "Acoustic Emitter")
	FVector GetEmitterLocation() const;

	/** Location at the last acoustic evaluation, used for teleport detection. */
	FVector LastEvaluatedLocation = FVector::ZeroVector;
	bool bHasEvaluatedLocation = false;

private:
	void RecomputeTarget();
	void BroadcastCurrent();
};
