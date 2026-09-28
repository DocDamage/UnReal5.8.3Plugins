#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "DocOpticalBeamTypes.h"
#include "DocBeamEmitterComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCOPTICALBEAMSRUNTIME_API UDocBeamEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocBeamEmitterComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	FName EmitterId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	bool bIsEnabled = true;

	/** Origin relative to the owning actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	FVector LocalOffset = FVector::ZeroVector;

	/** Direction relative to the owning actor. Zero or non-finite disables the beam (Disabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	FVector LocalDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "10.0"))
	float MaxRange = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	FGameplayTag BeamChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxReflections = 16;

	/** Hard cap on segments of any kind; guarantees termination even when the visited guard misses a cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxSegments = 64;

	/** Offset after each surface response to avoid re-hitting it. Charged to the range ledger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "0.1", ClampMax = "50.0"))
	float TraceEpsilon = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (ClampMin = "0.001"))
	float MinIntensity = 0.01f;

	/** Only this collision channel is traced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_WorldStatic;

	/** The owning actor is ignored only for the first segment (the beam leaving its housing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter")
	bool bIgnoreOwnerOnFirstSegment = true;

	/** Emitter generation; bumped by the subsystem on enable/pose changes. Older trace results are discarded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emitter")
	int64 PathGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emitter")
	FDocBeamPath CurrentPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emitter")
	FString LastRegistrationError;

	UFUNCTION(BlueprintCallable, Category = "Emitter")
	FVector GetWorldBeamOrigin() const;

	UFUNCTION(BlueprintCallable, Category = "Emitter")
	FVector GetWorldBeamDirection() const;

	/** Changes enabled state through the subsystem (new generation, path queued). */
	UFUNCTION(BlueprintCallable, Category = "Emitter")
	void SetEmitterEnabled(bool bInEnabled);
};
