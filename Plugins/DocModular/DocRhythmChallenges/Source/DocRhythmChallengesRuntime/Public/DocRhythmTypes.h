#pragma once

#include "CoreMinimal.h"
#include "DocSystemResult.h"
#include "DocRhythmTypes.generated.h"

UENUM(BlueprintType)
enum class EDocRhythmHitJudgment : uint8
{
	None,
	Perfect,
	Great,
	Good,
	Miss
};

UENUM(BlueprintType)
enum class EDocRhythmNoteKind : uint8
{
	Tap,
	Hold
};

UENUM(BlueprintType)
enum class EDocRhythmInputType : uint8
{
	Press,
	Release
};

/** Loading -> Ready -> CountIn -> Playing <-> Paused -> Completed; Playing -> Aborted | TimingFault. */
UENUM(BlueprintType)
enum class EDocRhythmChallengeState : uint8
{
	Inactive,
	Loading,
	Ready,
	CountIn,
	Playing,
	Paused,
	Completed,
	Aborted,
	TimingFault
};

/** Where an input timestamp came from. Only report Hardware when the API really supplies it. */
UENUM(BlueprintType)
enum class EDocRhythmTimestampSource : uint8
{
	Hardware,
	InputSubsystem,
	DispatchEstimate,
	SyntheticClock,
	Unknown
};

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EDocRhythmTimingQualityFlags : uint8
{
	Clean = 0,
	ClockHitchDetected = 1 << 0,
	LowPrecisionInput = 1 << 1,
	CalibrationUncertain = 1 << 2,
	Assisted = 1 << 3,
	AudioReanchored = 1 << 4,
	NonComparable = 1 << 5
};
ENUM_CLASS_FLAGS(EDocRhythmTimingQualityFlags);

/** Results with assistance or degraded timing never share a record category with strict runs. */
UENUM(BlueprintType)
enum class EDocRhythmResultCategory : uint8
{
	Strict,
	Assisted,
	Degraded
};

/** What losing input focus does to an active hold. */
UENUM(BlueprintType)
enum class EDocRhythmFocusLossPolicy : uint8
{
	/** Active holds end as Miss (one terminal judgment each). */
	BreakHold,
	/** The challenge pauses (if the backend supports precise pause); holds stay pending. */
	PauseChallenge
};

/** Timing windows in microseconds. Edges are inclusive: |error| <= window. */
USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmWindowSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 PerfectUs = 30000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 GreatUs = 60000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 GoodUs = 100000;

	/** Beyond Good but within this window the note is consumed as a Miss; beyond it the input selects nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 MissUs = 150000;
};

/** Canonical note timing is integer microseconds from chart time 0. */
USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmNote
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	FName NoteId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int32 LaneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	EDocRhythmNoteKind Kind = EDocRhythmNoteKind::Tap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 TimeOffsetUs = 0;

	/** Hold length; the hold ends at TimeOffsetUs + DurationUs. Must be > 0 for holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 DurationUs = 0;
};

USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmInputSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int32 LaneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	EDocRhythmInputType InputType = EDocRhythmInputType::Press;

	/** Earliest trustworthy timestamp on the subsystem's monotonic clock. <= 0 means unknown (processing time is used and flagged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 MonotonicTimestampUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	EDocRhythmTimestampSource Source = EDocRhythmTimestampSource::InputSubsystem;

	/** Optional unique id of the physical input event; a repeated id is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	FGuid InputEventId;

	/** Attempt this input belongs to; 0 = current. Inputs for another attempt are refused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 AttemptId = 0;
};

/**
 * Calibration. Sign convention: positive InputOffsetUs means observed inputs are late and it is subtracted:
 *   JudgmentTimeUs = InputTimestampMappedToChartUs - InputOffsetUs;  ErrorUs = JudgmentTimeUs - NoteTimeUs.
 * AudioOffsetUs and VisualOffsetUs are presentation settings and never change judgment.
 */
USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmCalibrationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 InputOffsetUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 AudioOffsetUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 VisualOffsetUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	bool bCalibrated = false;

	/** Device/profile the measurement applies to. A different device invalidates it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	FName DeviceScopeId = NAME_None;

	/** Robust spread of the accepted samples (scaled MAD). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int64 UncertaintyUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int32 SampleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rhythm")
	int32 RejectedSampleCount = 0;
};

USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmJudgmentResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FName NoteId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	EDocRhythmHitJudgment Judgment = EDocRhythmHitJudgment::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 JudgmentTimeUs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 ErrorUs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 PointsEarned = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 CurrentCombo = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bHoldStarted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bHoldCompleted = false;

	/** True for misses generated by the timeline rather than by an input. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bAutoMiss = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 AttemptId = 0;
};

USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmTimelineInfo
{
	GENERATED_BODY()

	/** Chart time from the transport (no input or visual offsets). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 CurrentChartTimeUs = 0;

	/** Chart time with the visual scroll offset applied (presentation only). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 VisualChartTimeUs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	EDocRhythmChallengeState State = EDocRhythmChallengeState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 CurrentCombo = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 CurrentScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 RemainingNotes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 AttemptId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 SessionGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 TransportGeneration = 0;
};

USTRUCT(BlueprintType)
struct DOCRHYTHMCHALLENGESRUNTIME_API FDocRhythmResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FName ChartId = NAME_None;

	/** Hash computed from the chart's timing content. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FString ContentHash;

	/** Author-declared chart version string, if any. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FString DeclaredChartVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FString ProfileHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 TotalScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 MaxPossibleScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 PerfectCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 GreatCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 GoodCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 MissCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 MaxCombo = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 FinalCombo = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 TotalNotes = 0;

	/** Score as a percentage of the maximum (never divides by zero; empty charts are rejected at load). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	float AccuracyPercent = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bCompleted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bHasAssistance = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	EDocRhythmResultCategory Category = EDocRhythmResultCategory::Strict;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 TimingQualityFlags = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	FDocRhythmCalibrationSettings CalibrationUsed;

	/** True only when a playback provider reported real audible output. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	bool bAudibleReference = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int32 TransportGenerations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rhythm")
	int64 AttemptId = 0;
};
