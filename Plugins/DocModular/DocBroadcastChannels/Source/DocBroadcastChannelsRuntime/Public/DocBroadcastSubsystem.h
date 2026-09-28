#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocBroadcastTypes.h"
#include "DocBroadcastChannelDefinition.h"
#include "DocBroadcastProgramDefinition.h"
#include "DocBroadcastReceiverComponent.h"
#include "DocBroadcastPlaybackProvider.h"
#include "DocBroadcastSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocBroadcastProgramChangedEvent, FName /*ChannelId*/, FName /*ProgramId (None = silence)*/, bool /*bIsInterruption*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocBroadcastInterruptionEndedEvent, FName /*ChannelId*/, const FGuid& /*InterruptionId*/, EDocBroadcastInterruptionStatus /*Status*/);

/**
 * Owns logical broadcast channels: schedule resolution, a shared per-channel transport, prioritized
 * interruptions and receiver tuning. The transport is driven explicitly by AdvanceTime (game code or a
 * Time bridge calls it); the subsystem does not tick on its own.
 */
UCLASS()
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** Replace the playback backend. Default is the logic-only synthetic provider (no sound). */
	void SetPlaybackProvider(UDocBroadcastPlaybackProvider* Provider);
	UDocBroadcastPlaybackProvider* GetPlaybackProvider();

	// Channels

	/** Validates the definition and provider capabilities, then starts the channel at StartEpoch. Duplicate ids are a Conflict. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult RegisterChannel(UDocBroadcastChannelDefinition* ChannelDef, double StartEpoch = 0.0);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult UnregisterChannel(FName ChannelId);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool QueryTransport(FName ChannelId, FDocBroadcastTransport& OutTransport) const;

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool QueryNowPlaying(FName ChannelId, FName& OutProgramId, double& OutProgramCursor, bool& bOutIsInterruption) const;

	// Receivers

	FDocSystemResult RegisterReceiver(UDocBroadcastReceiverComponent* Receiver);
	void UnregisterReceiver(UDocBroadcastReceiverComponent* Receiver);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult TuneReceiver(const FGuid& ReceiverId, FName ChannelId);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult SetReceiverVolume(const FGuid& ReceiverId, float Volume);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult MuteReceiver(const FGuid& ReceiverId, bool bMuted);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool QueryReceiver(const FGuid& ReceiverId, FDocBroadcastReceiverState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Broadcast")
	int32 GetReceiverCount() const { return Receivers.Num(); }

	// Transport and timing

	/** Authorized pause of the whole channel (holds the schedule). */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult SetTransportPaused(FName ChannelId, bool bPaused);

	/** Authorized seek within the current scheduled program. Fails while interrupted or for unseekable sources. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult SeekChannelAuthorized(FName ChannelId, double TargetProgramCursor);

	/** Advances every channel by real transport seconds. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	void AdvanceTime(float DeltaSeconds);

	/** Fictional clock speed (e.g. 60 = one game minute per real second). Only ReanchorAtScheduleBoundary channels use it. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult SetScheduleClockScale(FName ChannelId, float Scale);

	// Interruptions

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult PushInterruption(FName ChannelId, const FDocBroadcastInterruption& Request, FGuid& OutInterruptionId);

	/** Only the owner may release. Releasing never affects another owner's request. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult ReleaseInterruption(FName ChannelId, const FGuid& InterruptionId, const FGuid& OwnerId);

	/** Live or recently finished (bounded history) interruption record. */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool QueryInterruption(FName ChannelId, const FGuid& InterruptionId, FDocBroadcastInterruption& OutRecord) const;

	// Provider callback

	/** Called by the playback provider. Returns false when the ticket is stale and the result was ignored. */
	bool NotifyOpenResult(int64 Ticket, bool bSuccess, const FString& Reason);

	// Persistence

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocBroadcastTransport CaptureTransportState(FName ChannelId) const;

	/**
	 * Restores anchors/clock for an already registered channel without emitting program events.
	 * Live interruption leases are not persisted; any present are cancelled. A schedule version mismatch
	 * is migrated by resolving the saved offset against the current schedule (diagnostic ScheduleVersionMigrated).
	 */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult StageRestoreTransportState(const FDocBroadcastTransport& InTransport);

	/**
	 * Measured local receiver synchronization tolerance. Unsupported unless the provider is audible and an audio
	 * device exists; the logic-only provider never claims a measurement.
	 */
	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocSystemResult MeasureLocalSyncTolerance(FName ChannelId, double& OutToleranceSeconds);

	FDocBroadcastProgramChangedEvent OnProgramChanged;
	FDocBroadcastInterruptionEndedEvent OnInterruptionEnded;

private:
	struct FContentKey
	{
		bool bIsInterruption = false;
		FGuid InterruptionId;
		int32 EntryIndex = INDEX_NONE;
		int64 LoopIndex = 0;

		bool IsNone() const { return !bIsInterruption && EntryIndex == INDEX_NONE; }
		bool operator==(const FContentKey& Other) const
		{
			return bIsInterruption == Other.bIsInterruption && InterruptionId == Other.InterruptionId
				&& EntryIndex == Other.EntryIndex && LoopIndex == Other.LoopIndex;
		}
		bool operator!=(const FContentKey& Other) const { return !(*this == Other); }
	};

	struct FChannelRuntime
	{
		UDocBroadcastChannelDefinition* Definition = nullptr; // kept alive by RegisteredDefinitions
		double AnchorEpochSeconds = 0.0;
		double ClockSeconds = 0.0;
		double HeldSeconds = 0.0;
		double FictionalSeconds = 0.0;
		float ClockScale = 1.0f;
		bool bPaused = false;
		int32 Generation = 1;
		int32 ScheduleRevision = 0;
		int64 NextOrdinal = 1;
		TArray<FDocBroadcastInterruption> Interruptions; // non-terminal only
		TArray<FDocBroadcastInterruption> History;       // bounded terminal records
		EDocBroadcastTransportState State = EDocBroadcastTransportState::NoProgram;
		FContentKey Current;
		int64 PendingTicket = 0;
		int32 OpenAttempts = 0;
		bool bCurrentSourceFailed = false;
		int32 SourceFailureCount = 0;
	};

	UPROPERTY()
	TArray<TObjectPtr<UDocBroadcastChannelDefinition>> RegisteredDefinitions;

	UPROPERTY()
	TArray<TObjectPtr<UDocBroadcastProgramDefinition>> InterruptionPrograms;

	UPROPERTY()
	TObjectPtr<UDocBroadcastPlaybackProvider> PlaybackProvider;

	TMap<FName, FChannelRuntime> Channels;
	TMap<FGuid, TWeakObjectPtr<UDocBroadcastReceiverComponent>> Receivers;
	int64 NextTicket = 1;
	int32 RefreshDepth = 0;

	static double UnderlyingOffset(const FChannelRuntime& Channel);
	static const FDocBroadcastInterruption* FindActiveInterruption(const FChannelRuntime& Channel);
	static FDocBroadcastInterruption* FindActiveInterruptionMutable(FChannelRuntime& Channel);
	FDocSystemResult CheckProgramCapabilities(const UDocBroadcastProgramDefinition* Program, bool bRequireSeek);
	void ExpireQueued(FName ChannelId, FChannelRuntime& Channel);
	void EndInterruption(FName ChannelId, FChannelRuntime& Channel, const FGuid& InterruptionId, EDocBroadcastInterruptionStatus Status, const FString& Reason);
	void RefreshContent(FName ChannelId, FChannelRuntime& Channel, bool bEmitEvents, bool bForceReopen = false);
	void BeginOpen(FName ChannelId, FChannelRuntime& Channel);
	void CancelPendingOpen(FChannelRuntime& Channel);
	void HandleOpenFailure(FName ChannelId, FChannelRuntime& Channel, const FString& Reason);
	void ApplyReanchor(FChannelRuntime& Channel);
	void ComputeNowPlaying(const FChannelRuntime& Channel, FName& OutProgramId, double& OutCursor, bool& bOutInterruption) const;
	void SyncReceivers(FName ChannelId, const FChannelRuntime& Channel);
	void SyncReceiver(UDocBroadcastReceiverComponent& Receiver) const;
	void ReleaseInterruptionProgram(const FDocBroadcastInterruption& Record);
};
