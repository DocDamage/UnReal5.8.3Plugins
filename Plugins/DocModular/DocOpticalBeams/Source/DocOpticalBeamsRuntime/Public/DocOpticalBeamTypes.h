#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocOpticalBeamTypes.generated.h"

UENUM(BlueprintType)
enum class EDocBeamSurfaceType : uint8
{
	Blocker,
	Mirror,
	/** Transmits accepted channels with loss; may relabel the channel (ShiftedOutputChannel). */
	Filter,
	/** Future capability (refraction/splitting). Profiles using it fail validation; the solver treats it as a blocker. */
	Prism
};

UENUM(BlueprintType)
enum class EDocBeamTerminationReason : uint8
{
	None,
	HitBlocker,
	HitReceiver,
	OutOfRange,
	IntensityDepleted,
	TerminatedByBudget,
	LoopDetected,
	/** A filter rejected the beam's channel. */
	FilteredOut,
	/** Emitter disabled or its direction is invalid. */
	Disabled
};

UENUM(BlueprintType)
enum class EDocBeamReceiverAggregationMode : uint8
{
	/** Any single accepted contribution at or above MinRequiredIntensity. */
	AnyEligible,
	/** Sum of accepted contributions at or above MinRequiredIntensity. */
	SumIntensity,
	/** Every channel in AcceptedChannels has a contribution at or above MinRequiredIntensity. */
	AllRequiredChannels
};

UENUM(BlueprintType)
enum class EDocBeamReceiverState : uint8
{
	Inactive,
	Dwelling,
	Activated,
	Hysteresis
};

USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocBeamSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FVector StartPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FVector EndPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	float Length = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	float InitialIntensity = 1.0f;

	/** Intensity leaving this segment's end (after the surface response). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	float FinalIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FGameplayTag ChannelTag;

	UPROPERTY(BlueprintReadOnly, Category = "Optics")
	TWeakObjectPtr<UPrimitiveComponent> HitComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Optics")
	TWeakObjectPtr<AActor> HitActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName HitSurfaceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName HitReceiverId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FVector HitNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int32 SegmentIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	EDocBeamTerminationReason TerminationReason = EDocBeamTerminationReason::None;
};

USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocBeamPath
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName EmitterId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	TArray<FDocBeamSegment> Segments;

	/** Range ledger, including the self-hit offsets. Never exceeds the emitter's MaxRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	float TotalLength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int32 ReflectionCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	EDocBeamTerminationReason TerminationReason = EDocBeamTerminationReason::None;

	/** Emitter generation this path was traced for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 PathGeneration = 0;

	/** Surface topology revision this path was traced against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 TopologyRevision = 0;

	/** Simulation time of the commit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	double SolvedAtSeconds = 0.0;

	/** Receiver hit at the end of the path (None when the beam ends elsewhere). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName TerminalReceiverId = NAME_None;
};

/** Ticket for one trace. A result is committed only if the emitter generation and topology revision still match. */
USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocBeamPathRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 RequestId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName EmitterId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 EmitterGeneration = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 TopologyRevision = 0;
};

USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocReceiverContribution
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	FName EmitterId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	float Intensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	FGameplayTag Channel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	int64 PathGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	double SolvedAtSeconds = 0.0;

	/** The emitter's path is older than the staleness limit; excluded from eligibility until re-solved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	bool bHeldStale = false;
};

USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocReceiverState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	FName ReceiverId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	EDocBeamReceiverState State = EDocBeamReceiverState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	bool bIsActivated = false;

	/** Set when a latching receiver activated; survives loss of the beam and save/restore. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	bool bLatched = false;

	/** Sum of all (non-held) contributions, in emitter-id order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	float CurrentIntensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	float DwellTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	float RemainingHysteresisSeconds = 0.0f;

	/** EmitterId -> intensity (compact view). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	TMap<FName, float> EmitterContributions;

	/** Full evidence, sorted by emitter id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	TArray<FDocReceiverContribution> Contributions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optics")
	int64 StateRevision = 0;
};

/** Saved receiver facts. Trace hits are never saved; paths are recomputed after restore. */
USTRUCT(BlueprintType)
struct DOCOPTICALBEAMSRUNTIME_API FDocReceiverSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	FName ReceiverId = NAME_None;

	/** Restored only for latching receivers. Non-latched receivers must earn activation again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	bool bLatchedActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics")
	int64 StateRevision = 0;
};

namespace DocOpticalMath
{
	/** r = d - 2 (d.n) n for normalized d and n. Returns zero for zero or non-finite input. */
	DOCOPTICALBEAMSRUNTIME_API FVector ComputeReflection(const FVector& InDirection, const FVector& InNormal);
}
