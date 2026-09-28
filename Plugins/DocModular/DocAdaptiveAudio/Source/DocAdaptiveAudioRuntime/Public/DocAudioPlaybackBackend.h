#pragma once

#include "CoreMinimal.h"
#include "DocAdaptiveAudioTypes.h"

class UWorld;
class USceneComponent;
class UAudioComponent;

/**
 * Playback adapter (handoff 10.1). The subsystem is the logical director; the
 * backend owns real playback resources. Voice ids are backend-local and never 0.
 * All calls are game thread only.
 */
class DOCADAPTIVEAUDIORUNTIME_API IDocAudioPlaybackBackend
{
public:
	virtual ~IDocAudioPlaybackBackend() = default;

	struct FPlayParams
	{
		USoundBase* Sound = nullptr;
		float Volume = 1.f;
		float FadeInSeconds = 0.f;
		float StartTime = 0.f;
		EDocAudioFadeCurve Curve = EDocAudioFadeCurve::Linear;
		/** Attach to this component (positional). Null = 2D. */
		USceneComponent* AttachTo = nullptr;
	};

	/** Start a voice. Returns 0 on failure. */
	virtual int32 Play(UWorld* World, const FPlayParams& Params) = 0;
	/** Fade to silence then release the voice. 0 seconds = stop now. */
	virtual void FadeOutAndStop(int32 Voice, float Seconds, EDocAudioFadeCurve Curve) = 0;
	virtual void Stop(int32 Voice) = 0;
	virtual void SetVolume(int32 Voice, float Volume) = 0;
	/** False once the voice finished, was stopped, or its resources were destroyed. */
	virtual bool IsPlaying(int32 Voice) const = 0;
	virtual void StopAll() = 0;
	/** "Native", "Null (dedicated server)", "Muted (no audio device)"... */
	virtual FString Describe() const = 0;
};

/** Native Unreal playback: 2D/attached UAudioComponents with fades. */
class DOCADAPTIVEAUDIORUNTIME_API FDocNativeAudioBackend final : public IDocAudioPlaybackBackend
{
public:
	virtual int32 Play(UWorld* World, const FPlayParams& Params) override;
	virtual void FadeOutAndStop(int32 Voice, float Seconds, EDocAudioFadeCurve Curve) override;
	virtual void Stop(int32 Voice) override;
	virtual void SetVolume(int32 Voice, float Volume) override;
	virtual bool IsPlaying(int32 Voice) const override;
	virtual void StopAll() override;
	virtual FString Describe() const override { return TEXT("Native"); }

private:
	TMap<int32, TWeakObjectPtr<UAudioComponent>> Voices;
	int32 NextVoice = 1;
};

/**
 * Logical-only backend: allocates no playback resources. Used on dedicated servers
 * and on hosts without an audio device, so automation still exercises the director.
 * Voices "play" until stopped, or until their optional simulated duration elapses.
 */
class DOCADAPTIVEAUDIORUNTIME_API FDocNullAudioBackend : public IDocAudioPlaybackBackend
{
public:
	explicit FDocNullAudioBackend(FString InReason = TEXT("Null")) : Reason(MoveTemp(InReason)) {}

	struct FVoice
	{
		TWeakObjectPtr<USoundBase> Sound;
		float Volume = 1.f;
		bool bFadingOut = false;
	};

	virtual int32 Play(UWorld* World, const FPlayParams& Params) override;
	virtual void FadeOutAndStop(int32 Voice, float Seconds, EDocAudioFadeCurve Curve) override;
	virtual void Stop(int32 Voice) override;
	virtual void SetVolume(int32 Voice, float Volume) override;
	virtual bool IsPlaying(int32 Voice) const override;
	virtual void StopAll() override;
	virtual FString Describe() const override { return Reason; }

	/** Test/diagnostic view. */
	const TMap<int32, FVoice>& GetVoices() const { return Voices; }
	int32 CountPlaying(const USoundBase* Sound) const;
	/** Simulate a non-looping source ending. */
	void FinishVoice(int32 Voice) { Voices.Remove(Voice); }
	/** When true, Play fails (simulates an undecodable asset or exhausted device). */
	bool bFailPlays = false;
	int32 TotalPlays = 0;

private:
	TMap<int32, FVoice> Voices;
	int32 NextVoice = 1;
	FString Reason;
};
