#include "DocAudioPlaybackBackend.h"
#include "DocAdaptiveAudioLog.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace DocAudioBackendPrivate
{
	EAudioFaderCurve ToEngineCurve(EDocAudioFadeCurve Curve)
	{
		switch (Curve)
		{
		case EDocAudioFadeCurve::Logarithmic: return EAudioFaderCurve::Logarithmic;
		case EDocAudioFadeCurve::SCurve: return EAudioFaderCurve::SCurve;
		case EDocAudioFadeCurve::Sin: return EAudioFaderCurve::Sin;
		default: return EAudioFaderCurve::Linear;
		}
	}
}

// ---------------------------------------------------------------------------
// Native
// ---------------------------------------------------------------------------

int32 FDocNativeAudioBackend::Play(UWorld* World, const FPlayParams& Params)
{
	if (!World || !Params.Sound)
	{
		return 0;
	}
	UAudioComponent* Component = nullptr;
	if (Params.AttachTo)
	{
		Component = UGameplayStatics::SpawnSoundAttached(Params.Sound, Params.AttachTo, NAME_None, FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset, /*bStopWhenAttachedToDestroyed*/ true, Params.Volume, 1.f, 0.f,
			nullptr, nullptr, /*bAutoDestroy*/ false);
	}
	else
	{
		Component = UGameplayStatics::CreateSound2D(World, Params.Sound, Params.Volume, 1.f, 0.f, nullptr,
			/*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
	}
	if (!Component)
	{
		return 0; // no audio device, or the engine refused the sound
	}
	Component->SetVolumeMultiplier(Params.Volume);
	if (Params.FadeInSeconds > 0.f)
	{
		// FadeIn (re)starts playback from StartTime with the fade applied.
		Component->FadeIn(Params.FadeInSeconds, 1.f, Params.StartTime, DocAudioBackendPrivate::ToEngineCurve(Params.Curve));
	}
	else if (!Component->IsPlaying())
	{
		Component->Play(Params.StartTime); // 2D components are created stopped; attached ones already play
	}
	const int32 Id = NextVoice++;
	Voices.Add(Id, Component);
	return Id;
}

void FDocNativeAudioBackend::FadeOutAndStop(int32 Voice, float Seconds, EDocAudioFadeCurve Curve)
{
	UAudioComponent* Component = Voices.FindRef(Voice).Get();
	if (!Component)
	{
		Voices.Remove(Voice);
		return;
	}
	if (Seconds <= 0.f)
	{
		Stop(Voice);
		return;
	}
	// FadeOut stops the component at the end of the fade; IsPlaying then turns false and the
	// director releases the voice (DestroyComponent in Stop).
	Component->FadeOut(Seconds, 0.f, DocAudioBackendPrivate::ToEngineCurve(Curve));
}

void FDocNativeAudioBackend::Stop(int32 Voice)
{
	TWeakObjectPtr<UAudioComponent> Weak;
	if (Voices.RemoveAndCopyValue(Voice, Weak))
	{
		if (UAudioComponent* Component = Weak.Get())
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
}

void FDocNativeAudioBackend::SetVolume(int32 Voice, float Volume)
{
	if (UAudioComponent* Component = Voices.FindRef(Voice).Get())
	{
		Component->SetVolumeMultiplier(Volume);
	}
}

bool FDocNativeAudioBackend::IsPlaying(int32 Voice) const
{
	const UAudioComponent* Component = Voices.FindRef(Voice).Get();
	return Component && Component->IsPlaying();
}

void FDocNativeAudioBackend::StopAll()
{
	TArray<int32> Ids;
	Voices.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		Stop(Id);
	}
}

// ---------------------------------------------------------------------------
// Null / muted
// ---------------------------------------------------------------------------

int32 FDocNullAudioBackend::Play(UWorld* World, const FPlayParams& Params)
{
	if (!Params.Sound || bFailPlays)
	{
		return 0;
	}
	++TotalPlays;
	const int32 Id = NextVoice++;
	Voices.Add(Id, FVoice{ Params.Sound, Params.Volume, false });
	return Id;
}

void FDocNullAudioBackend::FadeOutAndStop(int32 Voice, float Seconds, EDocAudioFadeCurve Curve)
{
	if (Seconds <= 0.f)
	{
		Voices.Remove(Voice);
		return;
	}
	if (FVoice* V = Voices.Find(Voice))
	{
		V->bFadingOut = true; // the director stops it when its fade deadline passes
	}
}

void FDocNullAudioBackend::Stop(int32 Voice)
{
	Voices.Remove(Voice);
}

void FDocNullAudioBackend::SetVolume(int32 Voice, float Volume)
{
	if (FVoice* V = Voices.Find(Voice))
	{
		V->Volume = Volume;
	}
}

bool FDocNullAudioBackend::IsPlaying(int32 Voice) const
{
	return Voices.Contains(Voice);
}

void FDocNullAudioBackend::StopAll()
{
	Voices.Reset();
}

int32 FDocNullAudioBackend::CountPlaying(const USoundBase* Sound) const
{
	int32 Count = 0;
	for (const TPair<int32, FVoice>& Pair : Voices)
	{
		if (Pair.Value.Sound.Get() == Sound && !Pair.Value.bFadingOut)
		{
			++Count;
		}
	}
	return Count;
}
