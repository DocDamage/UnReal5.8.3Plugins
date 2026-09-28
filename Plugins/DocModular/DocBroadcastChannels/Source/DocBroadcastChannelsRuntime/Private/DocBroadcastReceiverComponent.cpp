#include "DocBroadcastReceiverComponent.h"
#include "DocBroadcastSubsystem.h"
#include "Engine/World.h"

UDocBroadcastReceiverComponent::UDocBroadcastReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		ReceiverId = FGuid::NewGuid();
	}
}

void UDocBroadcastReceiverComponent::OnRegister()
{
	Super::OnRegister();
	LastRegistrationError.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UDocBroadcastSubsystem* Subsystem = World->GetSubsystem<UDocBroadcastSubsystem>())
		{
			const FDocSystemResult Result = Subsystem->RegisterReceiver(this);
			if (!Result.IsSuccess())
			{
				LastRegistrationError = Result.ToString();
			}
		}
	}
}

void UDocBroadcastReceiverComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocBroadcastSubsystem* Subsystem = World->GetSubsystem<UDocBroadcastSubsystem>())
		{
			Subsystem->UnregisterReceiver(this);
		}
	}
	Super::OnUnregister();
}

bool UDocBroadcastReceiverComponent::TuneToChannel(FName InChannelId)
{
	UWorld* World = GetWorld();
	UDocBroadcastSubsystem* Subsystem = World ? World->GetSubsystem<UDocBroadcastSubsystem>() : nullptr;
	return Subsystem && Subsystem->TuneReceiver(ReceiverId, InChannelId).IsSuccess();
}

bool UDocBroadcastReceiverComponent::SetVolume(float InVolume)
{
	UWorld* World = GetWorld();
	UDocBroadcastSubsystem* Subsystem = World ? World->GetSubsystem<UDocBroadcastSubsystem>() : nullptr;
	return Subsystem && Subsystem->SetReceiverVolume(ReceiverId, InVolume).IsSuccess();
}

bool UDocBroadcastReceiverComponent::SetMuted(bool bInMuted)
{
	UWorld* World = GetWorld();
	UDocBroadcastSubsystem* Subsystem = World ? World->GetSubsystem<UDocBroadcastSubsystem>() : nullptr;
	return Subsystem && Subsystem->MuteReceiver(ReceiverId, bInMuted).IsSuccess();
}

FDocBroadcastReceiverState UDocBroadcastReceiverComponent::GetReceiverState() const
{
	FDocBroadcastReceiverState State;
	State.ReceiverId = ReceiverId;
	State.TunedChannelId = TunedChannelId;
	State.Volume = Volume;
	State.bIsMuted = bIsMuted;
	State.ObservedProgramId = LastObservedProgramId;
	State.ObservedCursorSeconds = LastObservedCursor;
	State.ObservedState = LastObservedState;
	State.bIsReceiverEnabled = bIsReceiverEnabled;
	return State;
}

float UDocBroadcastReceiverComponent::GetEffectiveGain() const
{
	const bool bAudibleState = LastObservedState == EDocBroadcastTransportState::Playing || LastObservedState == EDocBroadcastTransportState::Interrupted;
	return (bIsReceiverEnabled && !bIsMuted && bAudibleState) ? Volume : 0.0f;
}
