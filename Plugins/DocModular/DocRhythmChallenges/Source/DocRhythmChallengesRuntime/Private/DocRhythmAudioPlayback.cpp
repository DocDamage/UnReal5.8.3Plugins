#include "DocRhythmAudioPlayback.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

bool UDocRhythmAudioComponentPlayback::StartPlayback(const FString& AudioSource, int64 StartTimeUs)
{
	StopPlayback();
	LastError.Reset();
	UObject* Context = WorldContext.Get();
	if (!Context)
	{
		LastError = TEXT("No world context");
		return false;
	}
	if (AudioSource.IsEmpty())
	{
		LastError = TEXT("Chart has no audio source");
		return false;
	}
	USoundBase* Sound = Cast<USoundBase>(FSoftObjectPath(AudioSource).TryLoad());
	if (!Sound)
	{
		LastError = FString::Printf(TEXT("Audio source %s did not load as a USoundBase"), *AudioSource);
		return false;
	}
	const float StartSeconds = static_cast<float>(FMath::Max<int64>(0, StartTimeUs)) / 1.0e6f;
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(Context, Sound, 1.0f, 1.0f, StartSeconds, nullptr, false, /*bAutoDestroy*/ false);
	if (!Component)
	{
		LastError = TEXT("No audio component (no audio device?)");
		return false;
	}
	ActiveComponent = Component;
	Component->Play(StartSeconds);
	return true;
}

void UDocRhythmAudioComponentPlayback::StopPlayback()
{
	if (ActiveComponent)
	{
		ActiveComponent->Stop();
		ActiveComponent->DestroyComponent();
		ActiveComponent = nullptr;
	}
}

void UDocRhythmAudioComponentPlayback::PausePlayback()
{
	if (ActiveComponent)
	{
		ActiveComponent->SetPaused(true);
	}
}

void UDocRhythmAudioComponentPlayback::ResumePlayback()
{
	if (ActiveComponent)
	{
		ActiveComponent->SetPaused(false);
	}
}

bool UDocRhythmAudioComponentPlayback::IsPlaybackActive() const
{
	return ActiveComponent && ActiveComponent->IsPlaying();
}

bool UDocRhythmAudioComponentPlayback::IsAudible() const
{
	return GEngine && GEngine->GetMainAudioDeviceRaw() != nullptr;
}
