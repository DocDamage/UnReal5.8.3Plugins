#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DocSystemResult.h"
#include "IDocAcousticPlaybackAdapter.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UDocAcousticPlaybackAdapter : public UInterface
{
	GENERATED_BODY()
};

/**
 * Applies acoustic results to playback. The subsystem only calls it for emitters that hold an acoustic claim.
 * Implementations own per-emitter runtime parameters; they must never edit shared attenuation assets.
 */
class DOCACOUSTICSPACESRUNTIME_API IDocAcousticPlaybackAdapter
{
	GENERATED_BODY()

public:
	/** FinalGain already includes the host's current base gain; do not multiply it onto a previous value. */
	virtual FDocSystemResult ApplyAcousticParameters(FName EmitterId, float FinalGain, float FinalCutoffHz) = 0;

	/** Claim released: return the emitter to the host's current baseline (not a historical snapshot). */
	virtual FDocSystemResult ResetAcousticParameters(FName EmitterId, float BaselineGain, float BaselineCutoffHz) = 0;

	/** The single listener this mix follows. */
	virtual FDocSystemResult SetActiveListener(FName ListenerId) = 0;
	virtual FName GetActiveListener() const = 0;

	/** True only for a backend that can mix emitters separately per listener. The reference adapter returns false. */
	virtual bool SupportsIndependentListenerMixes() const { return false; }
};
