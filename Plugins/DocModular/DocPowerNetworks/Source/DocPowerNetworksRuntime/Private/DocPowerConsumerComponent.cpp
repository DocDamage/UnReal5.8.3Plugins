#include "DocPowerConsumerComponent.h"

UDocPowerConsumerComponent::UDocPowerConsumerComponent()
{
	NodeKind = EDocPowerNodeKind::Consumer;
}

void UDocPowerConsumerComponent::UpdateSupply(double InDeliveredWatts, EDocPowerConsumerState InState)
{
	DeliveredWatts = InDeliveredWatts;
	CurrentState = InState;

	OnPowerSupplyChanged.Broadcast(NodeId, DeliveredWatts, CurrentState);
	OnPowerSupplyChangedNative.Broadcast(NodeId, DeliveredWatts, CurrentState);
}
