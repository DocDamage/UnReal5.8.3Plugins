#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocMaterialReactionTypes.h"
#include "DocMaterialReactionDefinition.generated.h"

/**
 * One authored reaction. Rates are per second of simulation time.
 * Moisture gain marks the reaction as wetting-class: wetting deltas are applied before any other
 * reaction is evaluated in the same step (see README "Step order").
 */
UCLASS(BlueprintType)
class DOCMATERIALREACTIONSRUNTIME_API UDocMaterialReactionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName ReactionId = NAME_None;

	/** Reactions in the same group never consume resources in the same step; highest Priority wins, ties by ReactionId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName ExclusiveGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	FGameplayTag ExposureChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	float ActivationThreshold = 1.0f;

	/** Must be <= ActivationThreshold. An active reaction ends only below this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	float DeactivationThreshold = 0.5f;

	/** Continuous seconds at or above the activation threshold before starting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	float DwellDuration = 0.0f;

	/** Reaction cannot start or continue while Moisture >= this value. Values above 1 disable the check. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	float InhibitAtMoisture = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumption", meta = (ClampMin = "0"))
	float FuelConsumptionRate = 0.0f;

	/** Consumes existing moisture only (drying). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumption", meta = (ClampMin = "0"))
	float MoistureConsumptionRate = 0.0f;

	/** Adds moisture (wetting). Capped at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumption", meta = (ClampMin = "0"))
	float MoistureGainRate = 0.0f;

	/** Signed; phase stays in [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumption")
	float PhaseChangeRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumption", meta = (ClampMin = "0"))
	float CharRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
	bool bRequiresFuel = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
	bool bRequiresMoisture = false;

	/** Once active, keeps going without exposure until depleted, extinguished, suppressed or inhibited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
	bool bSelfSustaining = false;

	/** Stateful exposure transfer to registered neighbors. Cosmetic spread belongs to presentation, not here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation")
	bool bCanPropagate = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation")
	float PropagationRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation")
	FGameplayTag PropagationExposureChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation")
	float PropagationIntensity = 1.0f;

	/** Transfers from an activation at this generation or deeper are refused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation", meta = (ClampMin = "0"))
	int32 MaxPropagationGeneration = 8;

	/** Minimum seconds between refreshes of the same source/target transfer. 0 = every step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Propagation", meta = (ClampMin = "0"))
	float PropagationCooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer ResultingTags;

	bool IsWettingClass() const { return MoistureGainRate > 0.0f; }
};
