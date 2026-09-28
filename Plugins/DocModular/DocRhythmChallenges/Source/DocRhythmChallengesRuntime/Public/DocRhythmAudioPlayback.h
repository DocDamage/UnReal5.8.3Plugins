#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocRhythmPlaybackProvider.h"
#include "DocRhythmAudioPlayback.generated.h"

class UAudioComponent;

/**
 * Native audible reference path: plays the chart's backing track (a USoundBase soft path) through a 2D
 * UAudioComponent. It cannot report a precise playback position and does not claim precise pause, so
 * PauseChallenge returns Unsupported with it (restart instead). Quartz scheduling belongs to a bridge.
 */
UCLASS()
class DOCRHYTHMCHALLENGESRUNTIME_API UDocRhythmAudioComponentPlayback : public UObject, public IDocRhythmPlaybackProvider
{
	GENERATED_BODY()

public:
	/** Any object in the world the sound should play in. */
	UPROPERTY()
	TWeakObjectPtr<UObject> WorldContext;

	UPROPERTY()
	FString LastError;

	virtual bool StartPlayback(const FString& AudioSource, int64 StartTimeUs) override;
	virtual void StopPlayback() override;
	virtual void PausePlayback() override;
	virtual void ResumePlayback() override;
	virtual int64 GetPlaybackPositionUs() const override { return -1; }
	virtual bool IsPlaybackActive() const override;
	virtual bool SupportsPauseResume() const override { return false; }
	virtual bool IsAudible() const override;

private:
	UPROPERTY()
	TObjectPtr<UAudioComponent> ActiveComponent;
};
