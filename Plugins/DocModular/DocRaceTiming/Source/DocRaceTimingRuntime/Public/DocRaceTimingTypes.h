#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocRaceTimingTypes.generated.h"

UENUM(BlueprintType)
enum class EDocRaceState : uint8
{
	Registered,
	Ready,
	Countdown,
	Running,
	Finished,
	Invalid,
	Aborted
};

UENUM(BlueprintType)
enum class EDocFalseStartPolicy : uint8
{
	RejectStart,
	Penalty,
	Invalidate
};

UENUM(BlueprintType)
enum class EDocRacePausePolicy : uint8
{
	PracticeAllowPause,
	CompetitiveContinuous
};

USTRUCT(BlueprintType)
struct DOCRACETIMINGRUNTIME_API FDocRaceGateDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName GateId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FVector ForwardDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float Width = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float Height = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bIsStartGate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bIsFinishGate = false;

	/** Forward crossing only: P0 strictly behind the plane, P1 strictly in front, intersection inside the bounds.
	 *  A segment that starts exactly on (or in front of) the plane is not a crossing. */
	bool TestSweptCrossing(const FVector& P0, const FVector& P1, float& OutFraction, FVector& OutCrossPoint) const;
};

USTRUCT(BlueprintType)
struct DOCRACETIMINGRUNTIME_API FDocRaceSplit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName GateId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 LapNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float SplitTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float SegmentDurationSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct DOCRACETIMINGRUNTIME_API FDocRacePenalty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName PenaltyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float PenaltySeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FString Reason;
};

USTRUCT(BlueprintType)
struct DOCRACETIMINGRUNTIME_API FDocRaceResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid RunId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid ParticipantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName CourseId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 CourseVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName AssistanceCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	EDocRaceState State = EDocRaceState::Invalid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float ElapsedTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float PenaltySeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float FinalTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TArray<FDocRaceSplit> Splits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TArray<FDocRacePenalty> Penalties;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bIsPersonalBest = false;

	/** True only for a legitimately Finished run, set once at commit. Restore never sets it again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bRewardGranted = false;

	/** The run was paused under a practice policy; it is a separate record category (RAC-05). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bPracticeResult = false;

	/** Why the run ended the way it did (MissedGate, FalseStart, Aborted, RestoredMidRun ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName StatusReason = NAME_None;
};

USTRUCT(BlueprintType)
struct DOCRACETIMINGRUNTIME_API FDocRaceRun
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid RunId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FGuid ParticipantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName CourseId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 CourseVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName AssistanceCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	EDocRaceState State = EDocRaceState::Registered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 CurrentLap = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 TargetLaps = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 ExpectedGateIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double RunStartTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	float ElapsedTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FVector LastSamplePosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double LastSampleTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bHasLastSample = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TArray<FDocRaceSplit> Splits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TArray<FDocRacePenalty> AppliedPenalties;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TSet<FName> AppliedPenaltyIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bResultCommitted = false;

	/** Standing start: the authoritative go boundary on the run clock (sample timestamp domain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double StartBoundaryTime = 0.0;

	/** Newest accepted sample timestamp. Samples must be strictly increasing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double LastAcceptedTimestamp = -1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bPaused = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double PauseStartTime = 0.0;

	/** Total paused duration, excluded from the run clock (practice policy only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	double PausedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	bool bPractice = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 LastPassedGateIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 FalseStartCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	FName StatusReason = NAME_None;
};
