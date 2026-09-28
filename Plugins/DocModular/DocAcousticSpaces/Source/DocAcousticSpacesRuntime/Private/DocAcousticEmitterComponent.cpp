#include "DocAcousticEmitterComponent.h"
#include "DocAcousticSpaceSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocAcousticEmitterComponent::UDocAcousticEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocAcousticEmitterComponent::OnRegister()
{
	Super::OnRegister();
	if (EmitterId.IsNone())
	{
		return; // registered explicitly once an id is assigned
	}
	if (UWorld* World = GetWorld())
	{
		if (UDocAcousticSpaceSubsystem* Subsystem = World->GetSubsystem<UDocAcousticSpaceSubsystem>())
		{
			const FDocSystemResult Result = Subsystem->RegisterEmitter(this);
			LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
		}
	}
}

void UDocAcousticEmitterComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocAcousticSpaceSubsystem* Subsystem = World->GetSubsystem<UDocAcousticSpaceSubsystem>())
		{
			Subsystem->UnregisterEmitter(this);
		}
	}
	Super::OnUnregister();
}

void UDocAcousticEmitterComponent::RecomputeTarget()
{
	TargetEffectiveGain = FMath::Max(0.0f, BaseGain) * FMath::Clamp(TransmissionMultiplier, 0.0f, 1.0f);
	TargetEffectiveCutoffHz = FMath::Clamp(FMath::Min(BaseCutoffHz, RestrictiveCutoffHz), 20.0f, 20000.0f);
}

void UDocAcousticEmitterComponent::BroadcastCurrent()
{
	OnParametersUpdated.Broadcast(EmitterId, CurrentEffectiveGain, CurrentEffectiveCutoffHz);
	OnParametersUpdatedNative.Broadcast(EmitterId, CurrentEffectiveGain, CurrentEffectiveCutoffHz);
}

void UDocAcousticEmitterComponent::SetTargetParameters(float InTransmissionMultiplier, float InRestrictiveCutoffHz, bool bSnap)
{
	if (!FMath::IsFinite(InTransmissionMultiplier) || !FMath::IsFinite(InRestrictiveCutoffHz))
	{
		return;
	}
	TransmissionMultiplier = FMath::Clamp(InTransmissionMultiplier, 0.0f, 1.0f);
	RestrictiveCutoffHz = FMath::Clamp(InRestrictiveCutoffHz, 20.0f, 20000.0f);
	RecomputeTarget();

	if (bSnap || SmoothingInterpSpeed <= 0.0f)
	{
		CurrentEffectiveGain = TargetEffectiveGain;
		CurrentEffectiveCutoffHz = TargetEffectiveCutoffHz;
		BroadcastCurrent();
	}
}

void UDocAcousticEmitterComponent::AdvanceSmoothing(float DeltaSeconds)
{
	RecomputeTarget(); // host baseline may have changed since the last acoustic update
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	float NewGain = TargetEffectiveGain;
	float NewCutoff = TargetEffectiveCutoffHz;
	if (SmoothingInterpSpeed > 0.0f)
	{
		NewGain = FMath::FInterpTo(CurrentEffectiveGain, TargetEffectiveGain, DeltaSeconds, SmoothingInterpSpeed);
		NewCutoff = FMath::FInterpTo(CurrentEffectiveCutoffHz, TargetEffectiveCutoffHz, DeltaSeconds, SmoothingInterpSpeed);
	}

	if (!FMath::IsNearlyEqual(NewGain, CurrentEffectiveGain, 1e-4f) || !FMath::IsNearlyEqual(NewCutoff, CurrentEffectiveCutoffHz, 1.0f))
	{
		CurrentEffectiveGain = NewGain;
		CurrentEffectiveCutoffHz = NewCutoff;
		BroadcastCurrent();
	}
}

void UDocAcousticEmitterComponent::ResetSmoothing()
{
	RecomputeTarget();
	CurrentEffectiveGain = TargetEffectiveGain;
	CurrentEffectiveCutoffHz = TargetEffectiveCutoffHz;
	BroadcastCurrent();
}

void UDocAcousticEmitterComponent::ClearAcousticInfluence()
{
	TransmissionMultiplier = 1.0f;
	RestrictiveCutoffHz = 20000.0f;
	bHasEvaluatedLocation = false;
	ResetSmoothing();
}

FVector UDocAcousticEmitterComponent::GetEmitterLocation() const
{
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}
