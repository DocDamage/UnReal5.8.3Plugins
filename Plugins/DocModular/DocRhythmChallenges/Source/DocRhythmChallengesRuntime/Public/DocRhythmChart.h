#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocRhythmTypes.h"
#include "DocRhythmJudgmentProfile.h"
#include "DocRhythmChart.generated.h"

/**
 * An authored chart. Canonical note times are integer microseconds. Beat-based authoring converts with
 * BeatToMicroseconds (round half away from zero at constant tempo).
 */
UCLASS(BlueprintType)
class DOCRHYTHMCHALLENGESRUNTIME_API UDocRhythmChart : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxNotes = 100000;
	static constexpr int64 MaxDurationUs = 3600ll * 1000000ll;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart")
	FName ChartId = NAME_None;

	/** Optional author-declared version label. The computed hash is always reported separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart")
	FString ContentHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart", meta = (ClampMin = "1"))
	int64 DurationUs = 0;

	/** Chart time of audio position 0 (applied by the playback provider when starting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart")
	int64 ChartOffsetUs = 0;

	/** Silent lead-in before chart time 0 (CountIn state). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart", meta = (ClampMin = "0"))
	int64 CountInUs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart", meta = (ClampMin = "1.0"))
	float BPM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart", meta = (ClampMin = "1"))
	int32 LaneCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart")
	TArray<FDocRhythmNote> Notes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chart")
	TObjectPtr<UDocRhythmJudgmentProfile> DefaultJudgmentProfile;

	/** Soft object path of the backing track (a USoundBase for the audio-component provider). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FString AudioSourceIdentifier;

	/** Authored audio length. When > 0 it must match DurationUs within 1 ms; mismatches are reported, never rescaled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	int64 AudioDurationUs = 0;

	UFUNCTION(BlueprintCallable, Category = "Chart")
	FDocSystemResult ValidateChart() const;

	UFUNCTION(BlueprintCallable, Category = "Chart")
	int64 CalculateMaxPossibleScore(const UDocRhythmJudgmentProfile* InProfile = nullptr) const;

	/** Notes ordered by time, then NoteId (lexical). Does not modify the asset. */
	TArray<FDocRhythmNote> GetSortedNotes() const;

	/** Hash of all timing content (ids, lanes, kinds, times, durations, offsets, audio id). */
	FString ComputeContentHash() const;

	/** Constant-tempo beat conversion, rounded half away from zero. */
	UFUNCTION(BlueprintPure, Category = "Chart")
	int64 BeatToMicroseconds(double Beat) const;
};
