#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocRhythmTypes.h"
#include "DocRhythmClockProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocRhythmClockProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Monotonic time source shared by the transport mapping and input timestamps. It must never run backwards
 * and is never paused; pauses are tracked by the subsystem as intervals on this clock.
 */
class DOCRHYTHMCHALLENGESRUNTIME_API IDocRhythmClockProvider
{
	GENERATED_BODY()

public:
	virtual int64 GetCurrentTimeUs() const = 0;
	virtual EDocRhythmTimestampSource GetTimestampSource() const { return EDocRhythmTimestampSource::SyntheticClock; }
};
