#include "DocBeamEmitterComponent.h"
#include "DocOpticalBeamSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocBeamEmitterComponent::UDocBeamEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocBeamEmitterComponent::OnRegister()
{
	Super::OnRegister();
	if (EmitterId.IsNone() && GetOwner())
	{
		EmitterId = GetOwner()->GetFName();
	}
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		const FDocSystemResult Result = Subsystem->RegisterEmitter(this);
		LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
	}
}

void UDocBeamEmitterComponent::OnUnregister()
{
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		Subsystem->UnregisterEmitter(this);
	}
	Super::OnUnregister();
}

FVector UDocBeamEmitterComponent::GetWorldBeamOrigin() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform().TransformPosition(LocalOffset);
	}
	return LocalOffset;
}

FVector UDocBeamEmitterComponent::GetWorldBeamDirection() const
{
	if (LocalDirection.ContainsNaN())
	{
		return FVector::ZeroVector;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
	}
	return LocalDirection.GetSafeNormal();
}

void UDocBeamEmitterComponent::SetEmitterEnabled(bool bInEnabled)
{
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		Subsystem->SetEmitterEnabled(EmitterId, bInEnabled);
	}
	else
	{
		bIsEnabled = bInEnabled;
	}
}
