#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocGhostTypes.h"
#include "DocGhostDefinitions.h"
#include "DocGhostSurrogateActor.h"
#include "DocReplayGhostSubsystem.generated.h"

/** Replay-only presentation channel (annotations and action tokens). Never routed to live gameplay events. */
DECLARE_MULTICAST_DELEGATE_FourParams(FDocGhostPresentationEvent, const FGuid& /*SessionId*/, FName /*TrackId*/, FName /*Kind: Annotation|Token*/, const FString& /*Payload*/);

/**
 * Records observed motion per track and plays it back through isolated visual surrogates, one per track.
 * Recording and playback have separate handles; playback is driven by AdvancePlayback/AdvanceAll.
 */
UCLASS()
class DOCREPLAYGHOSTSRUNTIME_API UDocReplayGhostSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxConcurrentSessions = 8;
	static constexpr int32 MaxAnnotationChars = 256;

	virtual void Deinitialize() override;

	// Visual catalog

	/** Registers an approved visual. Recordings refer to visuals only by these ids. "Default" is built in. */
	FDocSystemResult RegisterGhostVisual(FName VisualId, TSubclassOf<ADocGhostSurrogateActor> SurrogateClass);

	// Recording

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult StartRecording(const FString& RecordingName, UDocGhostProfile* Profile, const TArray<FDocGhostTrackInfo>& Tracks, FGuid& OutRecordingId);

	/** Records an observed sample at its actual time (seconds since recording start). Never fills in skipped time. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult RecordSampleAt(const FGuid& RecordingId, FName TrackId, double TimeSeconds, const FTransform& InTransform, FName ActionToken = NAME_None, bool bIsDiscontinuity = false);

	/** Records at the current world time relative to the recording start. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult RecordSample(const FGuid& RecordingId, FName TrackId, const FTransform& InTransform, FName ActionToken = NAME_None, bool bIsDiscontinuity = false);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult AddAnnotation(const FGuid& RecordingId, FName TrackId, double TimeSeconds, const FString& Text);

	/** Finalizes: builds per-track chunks and the integrity checksum. An empty recording is discarded. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult StopRecording(const FGuid& RecordingId, FDocGhostRecording& OutFinalRecording);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult CancelRecording(const FGuid& RecordingId);

	// Playback

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult StartPlayback(const FDocGhostRecording& InRecording, bool bLooping, FGuid& OutSessionId);

	/** Queues an asynchronous open of recording bytes; completed by CompleteOpenRequests (the read completion). */
	FDocSystemResult RequestOpenRecording(const TArray<uint8>& Bytes, bool bLooping, FGuid& OutRequestId);
	int32 CompleteOpenRequests();
	FDocSystemResult CancelOpenRequest(const FGuid& RequestId);
	FDocSystemResult QueryOpenRequest(const FGuid& RequestId, FGuid& OutSessionId) const;

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult PausePlayback(const FGuid& SessionId);

	/** Continues from the paused time. Only valid while paused. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult ResumePlayback(const FGuid& SessionId);

	/** Reconstructs visual state at the target through the chunk index. Fires no annotations or tokens. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult SeekPlayback(const FGuid& SessionId, double TargetTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult SetPlaybackRate(const FGuid& SessionId, float Rate);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult StopPlayback(const FGuid& SessionId);

	/** Destroys surrogates, invalidates the generation and forgets the session. */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	FDocSystemResult ClosePlaybackSession(const FGuid& SessionId);

	/** Advances a session by real seconds times its rate. Annotations/tokens fire on forward playback only. */
	FDocSystemResult AdvancePlayback(const FGuid& SessionId, float DeltaSeconds);
	void AdvanceAll(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	bool QueryTrackState(const FGuid& SessionId, FName TrackId, FDocGhostTrackState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	bool QuerySession(const FGuid& SessionId, FDocGhostPlaybackSession& OutSession) const;

	UFUNCTION(BlueprintPure, Category = "Ghost")
	ADocGhostSurrogateActor* GetSurrogateActor(const FGuid& SessionId, FName TrackId) const;

	UFUNCTION(BlueprintPure, Category = "Ghost")
	int32 GetSurrogateCount() const;

	/** Late asset/visual load callbacks: accepted only for the live session generation. */
	bool NotifyVisualLoaded(const FGuid& SessionId, int32 Generation) const;

	/** No notify/root-motion isolating animation adapter ships; ghosts are transform/pose proxies. */
	FDocSystemResult QueryAnimationCapability() const;

	FDocGhostPresentationEvent OnPresentationEvent;

private:
	struct FActiveRecording
	{
		FDocGhostRecordingHeader Header;
		TMap<FName, TArray<FDocGhostSample>> Samples;
		TArray<FDocGhostAnnotation> Annotations;
		double StartWorldTime = 0.0;
		double MaxDuration = 3600.0;
		int32 MaxSamplesPerTrack = 200000;
		float PositionThreshold = 1.0f;
		float RotationThresholdDegrees = 1.0f;
	};

	struct FActivePlayback
	{
		FDocGhostPlaybackSession Session;
		FDocGhostRecording Recording;
		TMap<FName, TWeakObjectPtr<ADocGhostSurrogateActor>> Surrogates;
		TMap<FName, FName> LastTokens;
	};

	struct FOpenRequest
	{
		TArray<uint8> Bytes;
		bool bLooping = false;
		bool bDone = false;
		FDocSystemResult Result;
		FGuid SessionId;
	};

	void ApplyState(FActivePlayback& Playback, bool bEmitTokens, int32* OutVisits = nullptr);
	void FireAnnotations(FActivePlayback& Playback, double FromExclusive, double ToInclusive);
	void DestroySurrogates(FActivePlayback& Playback);

	UPROPERTY()
	TMap<FName, TSubclassOf<ADocGhostSurrogateActor>> VisualCatalog;

	TMap<FGuid, FActiveRecording> ActiveRecordings;
	TMap<FGuid, FActivePlayback> ActivePlaybacks;
	TMap<FGuid, FOpenRequest> OpenRequests;
	int32 NextGeneration = 1;
};
