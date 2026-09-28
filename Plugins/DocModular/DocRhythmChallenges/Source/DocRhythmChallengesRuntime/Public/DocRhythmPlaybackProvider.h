#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocRhythmPlaybackProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocRhythmPlaybackProvider : public UInterface
{
	GENERATED_BODY()
};

/** Backing-track transport. The subsystem checks it every update: loss faults the attempt, drift re-anchors it. */
class DOCRHYTHMCHALLENGESRUNTIME_API IDocRhythmPlaybackProvider
{
	GENERATED_BODY()

public:
	virtual bool StartPlayback(const FString& AudioSource, int64 StartTimeUs) = 0;
	virtual void StopPlayback() = 0;
	virtual void PausePlayback() = 0;
	virtual void ResumePlayback() = 0;
	/** Current audio position in chart microseconds, or < 0 when the backend cannot report it. */
	virtual int64 GetPlaybackPositionUs() const = 0;
	virtual bool IsPlaybackActive() const = 0;
	/** True only if pause/resume keep audio aligned within the profile's drift tolerance. */
	virtual bool SupportsPauseResume() const { return false; }
	/** True only when real sound reaches an output device. */
	virtual bool IsAudible() const { return false; }
};
