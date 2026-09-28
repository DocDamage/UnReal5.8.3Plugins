#include "DocMechanicalComponents.h"
#include "DocMechanicalNetworkSubsystem.h"
#include "DocMechanicalNetworksLog.h"
#include "Engine/World.h"

UDocMechanicalNodeComponent::UDocMechanicalNodeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocMechanicalNodeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UDocMechanicalNetworkSubsystem* Subsystem = World->GetSubsystem<UDocMechanicalNetworkSubsystem>())
		{
			Subsystem->RegisterNodeComponent(this);
		}
	}
}

void UDocMechanicalNodeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UDocMechanicalNetworkSubsystem* Subsystem = World->GetSubsystem<UDocMechanicalNetworkSubsystem>())
		{
			Subsystem->UnregisterNodeComponent(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FDocMechanicalNodeState UDocMechanicalNodeComponent::GetState() const
{
	FDocMechanicalNodeState State;
	State.NodeId = NodeId;
	State.AppliedLoadTorque = AppliedLoadTorque;
	State.DetachedPolicy = DetachedPolicy;
	State.MaxOperatingSpeed = MaxOperatingSpeed;
	return State;
}

UDocDriveSourceComponent::UDocDriveSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

static UDocMechanicalNetworkSubsystem* GetMechanicalSubsystem(const UActorComponent* Comp)
{
	UWorld* World = Comp ? Comp->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDocMechanicalNetworkSubsystem>() : nullptr;
}

void UDocDriveSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocMechanicalNetworkSubsystem* Subsystem = GetMechanicalSubsystem(this))
	{
		FDocDriveSourceState State;
		State.SourceId = SourceId;
		State.AttachedNodeId = AttachedNodeId;
		State.RequestedSpeed = RequestedSpeed;
		State.TorqueCapacity = TorqueCapacity;
		State.bIsEnabled = bIsEnabled;
		if (!Subsystem->RegisterDriveSource(State))
		{
			UE_LOG(LogDocMechanical, Warning, TEXT("Drive source %s refused: %s"), *SourceId.ToString(), *Subsystem->GetLastTopologyError());
		}
	}
}

void UDocDriveSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocMechanicalNetworkSubsystem* Subsystem = GetMechanicalSubsystem(this))
	{
		Subsystem->UnregisterDriveSource(SourceId);
	}
	Super::EndPlay(EndPlayReason);
}

void UDocDriveSourceComponent::SetRequestedSpeed(float InSpeed)
{
	UDocMechanicalNetworkSubsystem* Subsystem = GetMechanicalSubsystem(this);
	if (!Subsystem || Subsystem->SetSourceSpeed(SourceId, InSpeed))
	{
		RequestedSpeed = InSpeed;
	}
}

void UDocDriveSourceComponent::SetTorqueCapacity(float InCapacity)
{
	UDocMechanicalNetworkSubsystem* Subsystem = GetMechanicalSubsystem(this);
	if (!Subsystem || Subsystem->SetSourceCapacity(SourceId, InCapacity))
	{
		TorqueCapacity = InCapacity;
	}
}

UDocClutchComponent::UDocClutchComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocClutchComponent::SetEngaged(bool bInEngaged)
{
	// The subsystem decides: engaging is refused when it would make the topology invalid.
	UDocMechanicalNetworkSubsystem* Subsystem = GetMechanicalSubsystem(this);
	if (!Subsystem || Subsystem->SetClutchState(EdgeId, bInEngaged))
	{
		bIsEngaged = bInEngaged;
	}
}
