#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocRhythmClockProvider.h"
#include "DocRhythmPlaybackProvider.h"
#include "DocRhythmTestTypes.generated.h"

/** Deterministic monotonic clock driven by tests. */
UCLASS()
class UDocRhythmMockClock : public UObject, public IDocRhythmClockProvider
{
	GENERATED_BODY()

public:
	int64 MockTimeUs = 1000000;

	virtual int64 GetCurrentTimeUs() const override { return MockTimeUs; }
};

/** Scriptable backing-track transport. Position < 0 means "unknown" unless a test sets it. */
UCLASS()
class UDocRhythmMockPlayback : public UObject, public IDocRhythmPlaybackProvider
{
	GENERATED_BODY()

public:
	bool bActive = false;
	bool bPaused = false;
	bool bSupportPause = true;
	bool bFailStart = false;
	int64 PositionUs = -1;
	int32 StartCount = 0;
	int32 PauseCount = 0;
	FString LastSource;

	virtual bool StartPlayback(const FString& AudioSource, int64 StartTimeUs) override
	{
		++StartCount;
		LastSource = AudioSource;
		if (bFailStart)
		{
			return false;
		}
		bActive = true;
		bPaused = false;
		return true;
	}
	virtual void StopPlayback() override { bActive = false; bPaused = false; }
	virtual void PausePlayback() override { bPaused = true; ++PauseCount; }
	virtual void ResumePlayback() override { bPaused = false; }
	virtual int64 GetPlaybackPositionUs() const override { return PositionUs; }
	virtual bool IsPlaybackActive() const override { return bActive; }
	virtual bool SupportsPauseResume() const override { return bSupportPause; }
	virtual bool IsAudible() const override { return false; }
};
