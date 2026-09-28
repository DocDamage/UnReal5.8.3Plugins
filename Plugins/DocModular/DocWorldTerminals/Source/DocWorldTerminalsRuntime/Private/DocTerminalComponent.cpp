#include "DocTerminalComponent.h"
#include "DocWorldTerminalSubsystem.h"
#include "Engine/World.h"

UDocTerminalComponent::UDocTerminalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocTerminalComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld())
	{
		if (UDocWorldTerminalSubsystem* Subsystem = World->GetSubsystem<UDocWorldTerminalSubsystem>())
		{
			Subsystem->RegisterTerminalComponent(this);
		}
	}
}

void UDocTerminalComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocWorldTerminalSubsystem* Subsystem = World->GetSubsystem<UDocWorldTerminalSubsystem>())
		{
			Subsystem->UnregisterTerminalComponent(this);
		}
	}

	Super::OnUnregister();
}

void UDocTerminalComponent::SetPowerState(bool bInHasPower)
{
	bHasPower = bInHasPower;
	if (UWorld* World = GetWorld())
	{
		if (UDocWorldTerminalSubsystem* Subsystem = World->GetSubsystem<UDocWorldTerminalSubsystem>())
		{
			Subsystem->SetDevicePower(DeviceId, bHasPower);
		}
	}
}
