#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocBroadcastTypes.h"
#include "DocBroadcastReceiverComponent.generated.h"

/**
 * A radio/TV/speaker that tunes into a shared channel. It owns only its own tuning, volume and mute;
 * it never owns or resets channel time. Unloading and reloading rejoins the channel's current cursor.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocBroadcastReceiverComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Stable id. Two live receivers with the same id are refused (see LastRegistrationError). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FGuid ReceiverId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	FName TunedChannelId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast", meta = (ClampMin = "0", ClampMax = "1"))
	float Volume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	bool bIsMuted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
	bool bIsReceiverEnabled = true;

	// Observed channel state (written by the subsystem)

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FName LastObservedProgramId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Broadcast")
	double LastObservedCursor = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Broadcast")
	EDocBroadcastTransportState LastObservedState = EDocBroadcastTransportState::NoProgram;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FString LastRegistrationError;

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool TuneToChannel(FName InChannelId);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool SetVolume(float InVolume);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	bool SetMuted(bool bInMuted);

	UFUNCTION(BlueprintCallable, Category = "Broadcast")
	FDocBroadcastReceiverState GetReceiverState() const;

	/** Effective output gain after mute/enable (0 when silent). */
	UFUNCTION(BlueprintPure, Category = "Broadcast")
	float GetEffectiveGain() const;
};
