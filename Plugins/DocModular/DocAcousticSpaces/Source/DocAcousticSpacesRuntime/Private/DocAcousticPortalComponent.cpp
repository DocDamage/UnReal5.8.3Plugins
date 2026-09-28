#include "DocAcousticPortalComponent.h"
#include "DocAcousticSpaceSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace DocAcousticPortalPrivate
{
	static UDocAcousticSpaceSubsystem* GetAcousticSubsystem(const UActorComponent* Comp)
	{
		UWorld* World = Comp ? Comp->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UDocAcousticSpaceSubsystem>() : nullptr;
	}
}

UDocAcousticPortalComponent::UDocAcousticPortalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocAcousticPortalComponent::SetOpenness(float InOpenness)
{
	if (!FMath::IsFinite(InOpenness))
	{
		return;
	}
	const float Clamped = FMath::Clamp(InOpenness, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Openness, Clamped, 1e-4f))
	{
		Openness = Clamped;
		if (UDocAcousticSpaceSubsystem* Subsystem = DocAcousticPortalPrivate::GetAcousticSubsystem(this))
		{
			Subsystem->NotifyPortalChanged(PortalId);
		}
		OnOpennessChanged.Broadcast(PortalId, Openness);
		OnOpennessChangedNative.Broadcast(PortalId, Openness);
	}
}

void UDocAcousticPortalComponent::SetPortalEnabled(bool bEnabled)
{
	if (bIsEnabled != bEnabled)
	{
		bIsEnabled = bEnabled;
		if (UDocAcousticSpaceSubsystem* Subsystem = DocAcousticPortalPrivate::GetAcousticSubsystem(this))
		{
			Subsystem->NotifyPortalChanged(PortalId);
		}
	}
}

void UDocAcousticPortalComponent::OnRegister()
{
	Super::OnRegister();
	if (UDocAcousticSpaceSubsystem* Subsystem = DocAcousticPortalPrivate::GetAcousticSubsystem(this))
	{
		const FDocSystemResult Result = Subsystem->RegisterPortal(this);
		LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
	}
}

void UDocAcousticPortalComponent::OnUnregister()
{
	if (UDocAcousticSpaceSubsystem* Subsystem = DocAcousticPortalPrivate::GetAcousticSubsystem(this))
	{
		Subsystem->UnregisterPortal(this);
	}
	Super::OnUnregister();
}

float UDocAcousticPortalComponent::GetEffectiveGain() const
{
	return ToLink().GetEffectiveGain();
}

float UDocAcousticPortalComponent::GetEffectiveCutoffHz() const
{
	return ToLink().GetEffectiveCutoffHz();
}

FVector UDocAcousticPortalComponent::GetPortalLocation() const
{
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FDocAcousticPortalLink UDocAcousticPortalComponent::ToLink() const
{
	FDocAcousticPortalLink Link;
	Link.PortalId = PortalId;
	Link.SpaceA = SpaceA;
	Link.SpaceB = SpaceB;
	Link.Openness = Openness;
	Link.MinTransmissionGain = MinTransmissionGain;
	Link.MaxTransmissionGain = MaxTransmissionGain;
	Link.MinCutoffHz = MinCutoffHz;
	Link.MaxCutoffHz = MaxCutoffHz;
	Link.bIsEnabled = bIsEnabled;
	Link.PortalLocation = GetPortalLocation();
	return Link;
}
