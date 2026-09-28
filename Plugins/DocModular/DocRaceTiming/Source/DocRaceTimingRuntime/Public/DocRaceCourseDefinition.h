#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocRaceTimingTypes.h"
#include "DocRaceCourseDefinition.generated.h"

UCLASS(BlueprintType)
class DOCRACETIMINGRUNTIME_API UDocRaceCourseDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName CourseId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 CourseVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TArray<FDocRaceGateDefinition> OrderedGates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 DefaultLaps = 1;

	/** Running start: the clock starts at the first forward crossing of gate 0.
	 *  Standing start: the clock starts at the StartCountdown go boundary; crossing gate 0 before it is a false start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bRunningStart = true;

	/** Upper bound for one penalty. Larger, negative or non-finite penalties are refused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float MaxPenaltySeconds = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	EDocFalseStartPolicy FalseStartPolicy = EDocFalseStartPolicy::RejectStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float FalseStartPenaltySeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	EDocRacePausePolicy PausePolicy = EDocRacePausePolicy::CompetitiveContinuous;

	const FDocRaceGateDefinition* FindGate(FName GateId) const
	{
		return OrderedGates.FindByPredicate([GateId](const FDocRaceGateDefinition& G) { return G.GateId == GateId; });
	}

	int32 FindGateIndex(FName GateId) const
	{
		return OrderedGates.IndexOfByPredicate([GateId](const FDocRaceGateDefinition& G) { return G.GateId == GateId; });
	}
};
