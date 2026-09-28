#include "DocSequencePlaybackBackend.h"
#include "DocSequencesLog.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Engine/World.h"

// Verified against the 5.8.3 headers only when the build runs (Implemented / Unverified).

bool FDocLevelSequenceBackend::Start(int64 Session, const FStartParams& Params, FString& OutError)
{
	if (!Params.World || !Params.Sequence)
	{
		OutError = TEXT("World and Level Sequence required");
		return false;
	}
	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = false;
	Settings.FinishCompletionStateOverride = Params.bRestoreStateOnFinish
		? EMovieSceneCompletionModeOverride::ForceRestoreState
		: EMovieSceneCompletionModeOverride::ForceKeepState;

	ALevelSequenceActor* Actor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(Params.World, Params.Sequence, Settings, Actor);
	if (!Player || !Actor)
	{
		OutError = TEXT("Could not create a Level Sequence player");
		return false;
	}
	for (const TPair<FName, TArray<AActor*>>& Binding : Params.Bindings)
	{
		// Binding tags authored in Sequencer; never actor-label searches.
		Actor->SetBindingByTag(Binding.Key, Binding.Value, /*bAllowBindingsFromAsset*/ false);
	}
	Entries.Add(Session, FEntry{ Actor, false });
	Player->Play();
	return true;
}

namespace DocSequenceBackendPrivate
{
	ULevelSequencePlayer* PlayerOf(const TWeakObjectPtr<ALevelSequenceActor>& Actor)
	{
		return Actor.IsValid() ? Actor->GetSequencePlayer() : nullptr;
	}
}

void FDocLevelSequenceBackend::Pause(int64 Session)
{
	if (const FEntry* Entry = Entries.Find(Session))
	{
		if (ULevelSequencePlayer* Player = DocSequenceBackendPrivate::PlayerOf(Entry->Actor)) { Player->Pause(); }
	}
}

void FDocLevelSequenceBackend::Resume(int64 Session)
{
	if (const FEntry* Entry = Entries.Find(Session))
	{
		if (ULevelSequencePlayer* Player = DocSequenceBackendPrivate::PlayerOf(Entry->Actor)) { Player->Play(); }
	}
}

void FDocLevelSequenceBackend::Stop(int64 Session, bool bRestoreState)
{
	FEntry Entry;
	if (!Entries.RemoveAndCopyValue(Session, Entry))
	{
		return;
	}
	if (ALevelSequenceActor* Actor = Entry.Actor.Get())
	{
		if (ULevelSequencePlayer* Player = Actor->GetSequencePlayer())
		{
			if (bRestoreState)
			{
				FMovieSceneSequencePlaybackSettings Settings = Actor->PlaybackSettings;
				Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
				Player->SetPlaybackSettings(Settings);
			}
			Player->Stop();
		}
		const UWorld* World = Actor->GetWorld();
		if (World && !World->bIsTearingDown)
		{
			Actor->Destroy();
		}
	}
}

void FDocLevelSequenceBackend::JumpToEndAndStop(int64 Session)
{
	if (FEntry* Entry = Entries.Find(Session))
	{
		if (ULevelSequencePlayer* Player = DocSequenceBackendPrivate::PlayerOf(Entry->Actor))
		{
			Entry->bEndedByUs = true;
			Player->GoToEndAndStop();
		}
	}
}

bool FDocLevelSequenceBackend::JumpToMarker(int64 Session, const FString& Label)
{
	const FEntry* Entry = Entries.Find(Session);
	ULevelSequencePlayer* Player = Entry ? DocSequenceBackendPrivate::PlayerOf(Entry->Actor) : nullptr;
	const ULevelSequence* Sequence = Player ? Cast<ULevelSequence>(Player->GetSequence()) : nullptr;
	const UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!MovieScene || !MovieScene->GetMarkedFrames().ContainsByPredicate([&Label](const FMovieSceneMarkedFrame& M) { return M.Label == Label; }))
	{
		return false;
	}
	// Jump (not Play/Scrub): Sequencer does not trigger events between the old and new positions.
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(Label, EUpdatePositionMethod::Jump));
	return true;
}

void FDocLevelSequenceBackend::Rebind(int64 Session, const TMap<FName, TArray<AActor*>>& Bindings)
{
	if (const FEntry* Entry = Entries.Find(Session))
	{
		if (ALevelSequenceActor* Actor = Entry->Actor.Get())
		{
			for (const TPair<FName, TArray<AActor*>>& Binding : Bindings)
			{
				Actor->SetBindingByTag(Binding.Key, Binding.Value, false);
			}
		}
	}
}

float FDocLevelSequenceBackend::GetPosition(int64 Session) const
{
	const FEntry* Entry = Entries.Find(Session);
	const ULevelSequencePlayer* Player = Entry ? DocSequenceBackendPrivate::PlayerOf(Entry->Actor) : nullptr;
	return Player ? (float)Player->GetCurrentTime().AsSeconds() : 0.f;
}

float FDocLevelSequenceBackend::GetDuration(int64 Session) const
{
	const FEntry* Entry = Entries.Find(Session);
	const ULevelSequencePlayer* Player = Entry ? DocSequenceBackendPrivate::PlayerOf(Entry->Actor) : nullptr;
	return Player ? (float)Player->GetDuration().AsSeconds() : 0.f;
}

bool FDocLevelSequenceBackend::IsFinished(int64 Session) const
{
	const FEntry* Entry = Entries.Find(Session);
	const ULevelSequencePlayer* Player = Entry ? DocSequenceBackendPrivate::PlayerOf(Entry->Actor) : nullptr;
	if (!Player)
	{
		return true;
	}
	return !Player->IsPlaying() && !Player->IsPaused();
}
