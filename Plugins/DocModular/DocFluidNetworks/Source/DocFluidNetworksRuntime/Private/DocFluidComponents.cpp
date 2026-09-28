#include "DocFluidComponents.h"
#include "DocFluidNetworkSubsystem.h"
#include "Engine/World.h"

UDocFluidReservoirComponent::UDocFluidReservoirComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocFluidReservoirComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UDocFluidNetworkSubsystem* Subsystem = World->GetSubsystem<UDocFluidNetworkSubsystem>())
		{
			Subsystem->RegisterReservoirComponent(this);
		}
	}
}

void UDocFluidReservoirComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UDocFluidNetworkSubsystem* Subsystem = World->GetSubsystem<UDocFluidNetworkSubsystem>())
		{
			Subsystem->UnregisterReservoirComponent(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FDocFluidReservoirState UDocFluidReservoirComponent::GetState() const
{
	FDocFluidReservoirState State;
	State.ReservoirId = ReservoirId;
	State.FluidDefinitionId = FluidDefinitionId;
	State.Capacity = Capacity;
	State.CurrentVolume = CurrentVolume;
	return State;
}

UDocFluidValveComponent::UDocFluidValveComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UDocFluidValveComponent::SetOpening(float InOpening)
{
	if (!FMath::IsFinite(InOpening) || InOpening < 0.0f || InOpening > 1.0f)
	{
		return false;
	}

	ValveOpening = InOpening;
	return true;
}

UDocFluidPumpComponent::UDocFluidPumpComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocFluidPumpComponent::SetPumpEnabled(bool bEnabled)
{
	bPumpEnabled = bEnabled;
	Status = bEnabled ? EDocFluidPumpStatus::Enabled : EDocFluidPumpStatus::Disabled;
}
