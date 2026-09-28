#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;
class ULevelSequence;
class ALevelSequenceActor;

/**
 * Playback adapter for one world. Sessions are identified by the subsystem's session id.
 * Game thread only. The native implementation drives Level Sequence players; tests
 * substitute a scripted backend.
 */
class DOCSEQUENCESRUNTIME_API IDocSequencePlaybackBackend
{
public:
	virtual ~IDocSequencePlaybackBackend() = default;

	struct FStartParams
	{
		UWorld* World = nullptr;
		ULevelSequence* Sequence = nullptr;
		/** Binding tag → actors (Sequencer binding tags). */
		TMap<FName, TArray<AActor*>> Bindings;
		bool bRestoreStateOnFinish = false;
	};

	virtual bool Start(int64 Session, const FStartParams& Params, FString& OutError) = 0;
	virtual void Pause(int64 Session) = 0;
	virtual void Resume(int64 Session) = 0;
	/** Stop and release playback resources. bRestoreState overrides the finish policy (CancelAndRestore). */
	virtual void Stop(int64 Session, bool bRestoreState) = 0;
	/** Evaluate the end state and stop (FinishAtEndState). Does not fire crossed gameplay events. */
	virtual void JumpToEndAndStop(int64 Session) = 0;
	/** Move to a named marked frame without executing crossed events. False if the label does not exist. */
	virtual bool JumpToMarker(int64 Session, const FString& Label) = 0;
	/** Re-apply bindings (a participant came back while waiting). */
	virtual void Rebind(int64 Session, const TMap<FName, TArray<AActor*>>& Bindings) = 0;
	virtual float GetPosition(int64 Session) const = 0;
	virtual float GetDuration(int64 Session) const = 0;
	/** Ended on its own (reached the end) or its resources are gone. */
	virtual bool IsFinished(int64 Session) const = 0;
};

/** Level Sequence players (ALevelSequenceActor spawned per session). */
class DOCSEQUENCESRUNTIME_API FDocLevelSequenceBackend final : public IDocSequencePlaybackBackend
{
public:
	virtual bool Start(int64 Session, const FStartParams& Params, FString& OutError) override;
	virtual void Pause(int64 Session) override;
	virtual void Resume(int64 Session) override;
	virtual void Stop(int64 Session, bool bRestoreState) override;
	virtual void JumpToEndAndStop(int64 Session) override;
	virtual bool JumpToMarker(int64 Session, const FString& Label) override;
	virtual void Rebind(int64 Session, const TMap<FName, TArray<AActor*>>& Bindings) override;
	virtual float GetPosition(int64 Session) const override;
	virtual float GetDuration(int64 Session) const override;
	virtual bool IsFinished(int64 Session) const override;

private:
	struct FEntry
	{
		TWeakObjectPtr<ALevelSequenceActor> Actor;
		bool bEndedByUs = false;
	};
	TMap<int64, FEntry> Entries;
};
