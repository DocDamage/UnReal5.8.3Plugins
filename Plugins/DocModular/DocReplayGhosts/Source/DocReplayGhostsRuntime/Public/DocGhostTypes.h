#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocGhostTypes.generated.h"

UENUM(BlueprintType)
enum class EDocGhostPlaybackState : uint8
{
	Stopped,
	Playing,
	Paused,
	Completed
};

/** What a ghost shows across a sampling gap (a stretch with no recorded samples). */
UENUM(BlueprintType)
enum class EDocGhostGapPolicy : uint8
{
	/** Keep the last recorded pose (flagged as in-gap). */
	Hold,
	/** Hide the ghost until samples resume. */
	Hide,
	/** Jump to the next recorded pose (flagged as in-gap). */
	Snap
};

/** How faithfully a recording's visuals could be honoured at playback. */
UENUM(BlueprintType)
enum class EDocGhostVisualCompatibility : uint8
{
	Full,
	/** A referenced visual is unavailable; a plain transform proxy is shown instead (explicitly labelled). */
	TransformOnlyFallback
};

/** One observed sample on one track. Times are the actual observed times, never synthesized. */
USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double Timestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FTransform Transform = FTransform::Identity;

	/** Optional presentation token (e.g. "jump" pose). Never executed as gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName ActionToken = NAME_None;

	/** Teleport/rebase/spawn/attachment change: never interpolate into this sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	bool bIsDiscontinuity = false;
};

USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostTrackInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName TrackId = NAME_None;

	/** Approved visual id resolved through the subsystem's visual catalog. Never a class path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName VisualId = TEXT("Default");
};

/** Samples of one track within one time window; the chunk list per track is the seek index. */
USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostTrackChunk
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 ChunkIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName TrackId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double StartTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double EndTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	TArray<FDocGhostSample> Samples;
};

USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostAnnotation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double Timestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName TrackId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FString Text;
};

USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostRecordingHeader
{
	GENERATED_BODY()

	static constexpr int32 CurrentFormatVersion = 2;
	static constexpr int32 CurrentAlgorithmVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FGuid RecordingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FString RecordingName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 FormatVersion = CurrentFormatVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 AlgorithmVersion = CurrentAlgorithmVersion;

	/** Source build/content identity supplied by the recorder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FString ContentHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName WorldNamespace = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double SampleCadenceSeconds = 0.05;

	/** Intervals longer than this are gaps (no interpolation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double GapThresholdSeconds = 0.15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	EDocGhostGapPolicy GapPolicy = EDocGhostGapPolicy::Hold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double ChunkDurationSeconds = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	TArray<FDocGhostTrackInfo> Tracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	double TotalDurationSeconds = 0.0;

	/** Total samples across all tracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 FrameCount = 0;

	/** When a visual is unavailable, playback may fall back to a labelled transform-only proxy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	bool bHasVisualFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	bool bFinalized = false;

	/** CRC32 of the sample payload, checked before playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int64 PayloadCrc = 0;
};

/** Reconstructed presentation state of one track at a time. */
USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostTrackState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	FName TrackId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	bool bVisible = false;

	/** Inside a sampling gap (quality flag). */
	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	bool bInGap = false;

	/** Between a sample and a following discontinuity (not interpolated). */
	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	bool bAtDiscontinuity = false;

	/** Token of the most recent sample at or before the time. */
	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	FName ActionToken = NAME_None;
};

USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostRecording
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FDocGhostRecordingHeader Header;

	/** Per-track, time-ordered chunks. This is the only sample storage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	TArray<FDocGhostTrackChunk> Chunks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	TArray<FDocGhostAnnotation> Annotations;

	/**
	 * Evaluates one track at a time using the chunk index (binary search, no scan from zero).
	 * Returns false when the track does not exist. OutChunkVisits counts chunks touched.
	 */
	bool EvaluateTrack(double Time, FName TrackId, FDocGhostTrackState& OutState, int32* OutChunkVisits = nullptr) const;

	/** Structural, numeric and integrity validation. Does not resolve visuals. */
	FDocSystemResult ValidateRecording() const;

	uint32 ComputePayloadCrc() const;

	const FDocGhostTrackInfo* FindTrack(FName TrackId) const
	{
		return Header.Tracks.FindByPredicate([TrackId](const FDocGhostTrackInfo& T) { return T.TrackId == TrackId; });
	}

	/** Collects the ordered chunk indices (into Chunks) belonging to a track. */
	void GetTrackChunkIndices(FName TrackId, TArray<int32>& OutIndices) const;

private:
	/** Lazily built per-track chunk index (recordings are immutable once finalized). */
	mutable TMap<FName, TArray<int32>> TrackIndexCache;
};

/** Local recording file format: magic, version, header, chunks, annotations; bounded and CRC-checked. */
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostRecordingIO
{
	static constexpr uint32 Magic = 0x4F484744; // "DGHO"
	static constexpr int32 MaxTracks = 16;
	static constexpr int32 MaxChunks = 100000;
	static constexpr int32 MaxSamplesPerChunk = 100000;
	static constexpr int32 MaxTotalSamples = 1000000;
	static constexpr int32 MaxAnnotations = 1024;
	static constexpr int32 MaxStringBytes = 1024;
	static constexpr double MaxDurationSeconds = 24.0 * 3600.0;

	static bool Write(const FDocGhostRecording& Recording, TArray<uint8>& OutBytes);

	/** Never modifies the input. Unsupported versions return Unsupported; malformed data returns InvalidInput. */
	static FDocSystemResult Read(const TArray<uint8>& Bytes, FDocGhostRecording& OutRecording);
};

USTRUCT(BlueprintType)
struct DOCREPLAYGHOSTSRUNTIME_API FDocGhostPlaybackSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	FGuid RecordingId;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	EDocGhostPlaybackState State = EDocGhostPlaybackState::Stopped;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	double CurrentTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	float PlaybackRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	bool bLooping = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	int32 Generation = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	EDocGhostVisualCompatibility VisualCompatibility = EDocGhostVisualCompatibility::Full;

	/** Chunks touched by the most recent seek (index efficiency diagnostic). */
	UPROPERTY(BlueprintReadOnly, Category = "Ghost")
	int32 LastSeekChunkVisits = 0;
};
