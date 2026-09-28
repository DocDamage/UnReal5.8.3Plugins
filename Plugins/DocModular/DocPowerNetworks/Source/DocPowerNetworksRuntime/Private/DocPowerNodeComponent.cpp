#include "DocPowerNodeComponent.h"
#include "DocPowerNetworkSubsystem.h"
#include "Engine/World.h"

UDocPowerNodeComponent::UDocPowerNodeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PortNames.Add(TEXT("Main"));
}

void UDocPowerNodeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegister)
	{
		if (UWorld* World = GetWorld())
		{
			if (UDocPowerNetworkSubsystem* Subsystem = World->GetSubsystem<UDocPowerNetworkSubsystem>())
			{
				Subsystem->RegisterNode(this);
			}
		}
	}
}

void UDocPowerNodeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAutoRegister && !NodeId.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			if (UDocPowerNetworkSubsystem* Subsystem = World->GetSubsystem<UDocPowerNetworkSubsystem>())
			{
				const bool bUnload = EndPlayReason == EEndPlayReason::RemovedFromWorld || EndPlayReason == EEndPlayReason::LevelTransition;
				if (bUnload && bRetainOnUnload)
				{
					Subsystem->SuspendNode(NodeId); // Energy, edges and latches survive; the actor is not kept alive.
				}
				else
				{
					Subsystem->UnregisterNode(NodeId);
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

TArray<FDocPowerPortId> UDocPowerNodeComponent::GetPortIds() const
{
	TArray<FDocPowerPortId> Result;
	Result.Reserve(PortNames.Num());
	for (const FName& PortName : PortNames)
	{
		Result.Add(FDocPowerPortId(NodeId, PortName));
	}
	return Result;
}
