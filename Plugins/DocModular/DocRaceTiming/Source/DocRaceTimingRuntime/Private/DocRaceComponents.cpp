#include "DocRaceComponents.h"
#include "DocRaceTimingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocRaceGateComponent::UDocRaceGateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocRaceGateComponent::OnRegister()
{
	Super::OnRegister();
	GateDefinition.Location = GetComponentLocation();
	GateDefinition.ForwardDirection = GetForwardVector();
}

UDocRaceParticipantComponent::UDocRaceParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ParticipantId = FGuid::NewGuid();
}

bool UDocRaceParticipantComponent::NotifyDiscontinuity()
{
	UWorld* World = GetWorld();
	UDocRaceTimingSubsystem* Timing = World ? World->GetSubsystem<UDocRaceTimingSubsystem>() : nullptr;
	return Timing && ActiveRunId.IsValid() && Timing->NotifyDiscontinuity(ActiveRunId);
}

bool UDocRaceParticipantComponent::SubmitCurrentPosition(double Timestamp)
{
	UWorld* World = GetWorld();
	UDocRaceTimingSubsystem* Timing = World ? World->GetSubsystem<UDocRaceTimingSubsystem>() : nullptr;
	return Timing && GetOwner() && ActiveRunId.IsValid() && Timing->SubmitPositionSample(ActiveRunId, GetCurrentPosition(), Timestamp);
}

FVector UDocRaceParticipantComponent::GetCurrentPosition() const
{
	AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}
