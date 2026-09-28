#include "DocQueueStationComponent.h"
#include "DocServiceQueueSubsystem.h"
#include "Engine/World.h"

UDocQueueStationComponent::UDocQueueStationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UDocServiceQueueSubsystem* UDocQueueStationComponent::GetSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UDocServiceQueueSubsystem>() : nullptr;
}

void UDocQueueStationComponent::BeginPlay()
{
	Super::BeginPlay();
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (Subsystem && !StationId.IsNone() && Capacity > 0)
	{
		Subsystem->RegisterStation(StationId, Capacity, ServiceTags);
	}
}

void UDocQueueStationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (Subsystem && !StationId.IsNone())
	{
		Subsystem->UnregisterStation(StationId, EDocStationClosePolicy::CancelPendingReservations);
	}
	Super::EndPlay(EndPlayReason);
}

FDocSystemResult UDocQueueStationComponent::CreateOffer(FName QueueId, FDocAdmissionOffer& OutOffer)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}
	return Subsystem->CreateAdmissionOffer(QueueId, StationId, OutOffer);
}

FDocSystemResult UDocQueueStationComponent::StartService(const FGuid& TicketId)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}
	return Subsystem->StartService(StationId, TicketId);
}

FDocSystemResult UDocQueueStationComponent::CompleteService(const FGuid& TicketId)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}
	return Subsystem->CompleteService(StationId, TicketId);
}

FDocSystemResult UDocQueueStationComponent::SetOperationalState(EDocStationOperationalState NewState)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}
	return Subsystem->SetStationOperationalState(StationId, NewState);
}
