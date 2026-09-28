#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocGhostTypes.h"
#include "DocGhostSurrogateActor.h"
#include "DocGhostDefinitions.generated.h"

/** Authoring description of a track (id + approved visual). */
UCLASS(BlueprintType)
class DOCREPLAYGHOSTSRUNTIME_API UDocGhostTrackDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName TrackId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FGameplayTag TrackTag;

	/** Visual id registered with UDocReplayGhostSubsystem::RegisterGhostVisual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FName VisualId = TEXT("Default");

	FDocGhostTrackInfo ToTrackInfo() const
	{
		FDocGhostTrackInfo Info;
		Info.TrackId = TrackId;
		Info.VisualId = VisualId;
		return Info;
	}
};

/** Sampling and bounds for a recording. */
UCLASS(BlueprintType)
class DOCREPLAYGHOSTSRUNTIME_API UDocGhostProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	FString ProfileName;

	/** Minimum spacing between accepted samples (except discontinuities). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float SampleCadenceSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float MaxDurationSeconds = 3600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 MaxTracks = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	int32 MaxSamplesPerTrack = 200000;

	/** Samples that moved/rotated less than this are skipped (until the gap threshold would be reached). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float PositionChangeThreshold = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float RotationChangeThresholdDegrees = 1.0f;

	/** Intervals longer than this are gaps (0 = three cadences). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float GapThresholdSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	EDocGhostGapPolicy GapPolicy = EDocGhostGapPolicy::Hold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	float ChunkDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	bool bAllowVisualFallback = true;

	FDocSystemResult ValidateProfile() const;

	double GetEffectiveGapThreshold() const
	{
		return GapThresholdSeconds > 0.0f ? static_cast<double>(GapThresholdSeconds) : 3.0 * static_cast<double>(SampleCadenceSeconds);
	}
};
