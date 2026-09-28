#include "DocQueueParticipantComponent.h"
#include "DocServiceQueueSubsystem.h"
#include "Engine/World.h"

UDocQueueParticipantComponent::UDocQueueParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ParticipantId = FGuid::NewGuid();
}

UDocServiceQueueSubsystem* UDocQueueParticipantComponent::GetSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UDocServiceQueueSubsystem>() : nullptr;
}

void UDocQueueParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!ParticipantId.IsValid())
	{
		ParticipantId = FGuid::NewGuid();
	}
}

void UDocQueueParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasActiveTicket())
	{
		LeaveQueue();
	}
	Super::EndPlay(EndPlayReason);
}

FDocSystemResult UDocQueueParticipantComponent::JoinQueue(FName QueueId, int32 PartySize, int32 PriorityClass)
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}

	FDocSystemResult Result = Subsystem->JoinQueue(QueueId, ParticipantId, PartySize, PriorityClass, CurrentTicket);
	return Result;
}

FDocSystemResult UDocQueueParticipantComponent::LeaveQueue()
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}

	if (!CurrentTicket.TicketId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No active ticket to leave."));
	}

	FDocSystemResult Result = Subsystem->LeaveQueue(CurrentTicket.TicketId);
	RefreshTicket();
	return Result;
}

FDocSystemResult UDocQueueParticipantComponent::AcceptCurrentOffer()
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}

	RefreshTicket(); // Offers are made by stations; pick up the current one.
	if (!CurrentTicket.AssignedOfferId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No active offer assigned to ticket."));
	}

	FDocStationReservation Res;
	FDocSystemResult Result = Subsystem->AcceptOffer(CurrentTicket.AssignedOfferId, Res);
	RefreshTicket();
	return Result;
}

FDocSystemResult UDocQueueParticipantComponent::DeclineCurrentOffer()
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}

	RefreshTicket();
	if (!CurrentTicket.AssignedOfferId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No active offer assigned to ticket."));
	}

	FDocSystemResult Result = Subsystem->DeclineOffer(CurrentTicket.AssignedOfferId);
	RefreshTicket();
	return Result;
}

FDocSystemResult UDocQueueParticipantComponent::ConfirmArrival()
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocServiceQueueSubsystem not available."));
	}

	if (!CurrentTicket.TicketId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No ticket to confirm arrival."));
	}

	FDocSystemResult Result = Subsystem->ConfirmArrival(CurrentTicket.TicketId);
	RefreshTicket();
	return Result;
}

bool UDocQueueParticipantComponent::RefreshTicket()
{
	UDocServiceQueueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !CurrentTicket.TicketId.IsValid())
	{
		return false;
	}
	FDocQueueTicket Latest;
	if (Subsystem->QueryTicket(CurrentTicket.TicketId, Latest).IsSuccess())
	{
		CurrentTicket = Latest;
		return true;
	}
	return false;
}

bool UDocQueueParticipantComponent::HasActiveTicket() const
{
	FDocQueueTicket Latest = CurrentTicket;
	if (UDocServiceQueueSubsystem* Subsystem = GetSubsystem())
	{
		if (CurrentTicket.TicketId.IsValid())
		{
			Subsystem->QueryTicket(CurrentTicket.TicketId, Latest);
		}
	}
	return Latest.TicketId.IsValid() &&
		Latest.State != EDocQueueTicketState::Completed &&
		Latest.State != EDocQueueTicketState::Cancelled &&
		Latest.State != EDocQueueTicketState::Expired &&
		Latest.State != EDocQueueTicketState::Rejected;
}
