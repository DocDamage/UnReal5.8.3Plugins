#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "DocRaceTimingTypes.h"
#include "DocRaceComponents.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCRACETIMINGRUNTIME_API UDocRaceGateComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDocRaceGateComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FDocRaceGateDefinition GateDefinition;

	virtual void OnRegister() override;
};

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCRACETIMINGRUNTIME_API UDocRaceParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocRaceParticipantComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid ParticipantId;

	/** The run this participant is currently timed in. Set it from the BeginRun result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid ActiveRunId;

	/** Breaks the swept segment of the active run (teleport, respawn, rebase). Returns false with no active run. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool NotifyDiscontinuity();

	/** Submits the owner's current location as a trusted sample for the active run. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool SubmitCurrentPosition(double Timestamp = 0.0);

	UFUNCTION(BlueprintPure, Category = "Race")
	FVector GetCurrentPosition() const;
};
