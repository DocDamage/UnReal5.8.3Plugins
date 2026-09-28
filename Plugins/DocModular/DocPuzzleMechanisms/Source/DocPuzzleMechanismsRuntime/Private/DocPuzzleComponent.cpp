#include "DocPuzzleComponent.h"
#include "DocPuzzleMechanismsLog.h"
#include "Engine/World.h"

UDocPuzzleComponent::UDocPuzzleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InstanceId = FGuid::NewGuid();
}

UDocPuzzleSubsystem* UDocPuzzleComponent::GetSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UDocPuzzleSubsystem>() : nullptr;
}

void UDocPuzzleComponent::BeginPlay()
{
	Super::BeginPlay();

	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (bAutoRegisterOnBeginPlay && Definition)
	{
		Subsystem->RegisterPuzzle(InstanceId, Definition, Scope);

		Subsystem->OnInputAccepted.AddDynamic(this, &UDocPuzzleComponent::HandleSubsystemInputAccepted);
		Subsystem->OnProgressChanged.AddDynamic(this, &UDocPuzzleComponent::HandleSubsystemProgressChanged);
		Subsystem->OnAttemptFailed.AddDynamic(this, &UDocPuzzleComponent::HandleSubsystemAttemptFailed);
		Subsystem->OnPuzzleSolved.AddDynamic(this, &UDocPuzzleComponent::HandleSubsystemPuzzleSolved);
		Subsystem->OnResetCommitted.AddDynamic(this, &UDocPuzzleComponent::HandleSubsystemResetCommitted);

		if (bAutoStartAttempt)
		{
			FGuid AttemptId;
			StartAttempt(AttemptId);
		}
	}
}

void UDocPuzzleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (Subsystem)
	{
		Subsystem->OnInputAccepted.RemoveDynamic(this, &UDocPuzzleComponent::HandleSubsystemInputAccepted);
		Subsystem->OnProgressChanged.RemoveDynamic(this, &UDocPuzzleComponent::HandleSubsystemProgressChanged);
		Subsystem->OnAttemptFailed.RemoveDynamic(this, &UDocPuzzleComponent::HandleSubsystemAttemptFailed);
		Subsystem->OnPuzzleSolved.RemoveDynamic(this, &UDocPuzzleComponent::HandleSubsystemPuzzleSolved);
		Subsystem->OnResetCommitted.RemoveDynamic(this, &UDocPuzzleComponent::HandleSubsystemResetCommitted);

		Subsystem->UnregisterPuzzle(InstanceId);
	}

	Super::EndPlay(EndPlayReason);
}

FDocSystemResult UDocPuzzleComponent::StartAttempt(FGuid& OutAttemptId)
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}
	return Subsystem->StartAttempt(InstanceId, OutAttemptId);
}

FDocSystemResult UDocPuzzleComponent::SubmitInput(FName InputId, const FDocPuzzleInputValue& Value, const FGuid& SourceId)
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}

	FDocPuzzleInputEvent Event;
	Event.InstanceId = InstanceId;
	Event.AttemptId = Subsystem->GetActiveAttemptId(InstanceId);
	Event.SourceId = SourceId.IsValid() ? SourceId : FGuid::NewGuid();
	Event.SourceSequenceNumber = ++LocalSequenceCounter;
	Event.InputId = InputId;
	Event.Value = Value;
	UWorld* World = GetWorld();
	Event.AcceptedTimestamp = World ? World->GetTimeSeconds() : 0.0;

	return Subsystem->SubmitInput(Event);
}

FDocSystemResult UDocPuzzleComponent::SubmitTrigger(FName InputId, const FGuid& SourceId)
{
	return SubmitInput(InputId, FDocPuzzleInputValue::MakeTrigger(), SourceId);
}

FDocSystemResult UDocPuzzleComponent::SubmitBoolean(FName InputId, bool bValue, const FGuid& SourceId)
{
	return SubmitInput(InputId, FDocPuzzleInputValue::MakeBoolean(bValue), SourceId);
}

FDocSystemResult UDocPuzzleComponent::SubmitScalar(FName InputId, float Value, const FGuid& SourceId)
{
	return SubmitInput(InputId, FDocPuzzleInputValue::MakeScalar(Value), SourceId);
}

FDocSystemResult UDocPuzzleComponent::SubmitSymbol(FName InputId, FName Symbol, const FGuid& SourceId)
{
	return SubmitInput(InputId, FDocPuzzleInputValue::MakeSymbol(Symbol), SourceId);
}

FDocSystemResult UDocPuzzleComponent::SetContributor(FName InputId, const FDocPuzzleInputValue& Value, const FGuid& SourceId)
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}
	return Subsystem->SetInputContributor(InstanceId, SourceId, InputId, Value);
}

FDocSystemResult UDocPuzzleComponent::RemoveContributor(FName InputId, const FGuid& SourceId)
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}
	return Subsystem->RemoveInputContributor(InstanceId, SourceId, InputId);
}

FDocSystemResult UDocPuzzleComponent::RequestReset()
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}
	return Subsystem->RequestReset(InstanceId);
}

FDocSystemResult UDocPuzzleComponent::GetEvaluation(FDocPuzzleEvaluation& OutEvaluation) const
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("DocPuzzleSubsystem not available."));
	}
	return Subsystem->EvaluatePuzzle(InstanceId, OutEvaluation);
}

EDocPuzzleState UDocPuzzleComponent::GetState() const
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	return Subsystem ? Subsystem->GetPuzzleState(InstanceId) : EDocPuzzleState::Inactive;
}

bool UDocPuzzleComponent::IsSolved() const
{
	UDocPuzzleSubsystem* Subsystem = GetSubsystem();
	return Subsystem ? Subsystem->IsPuzzleSolved(InstanceId) : false;
}

void UDocPuzzleComponent::HandleSubsystemInputAccepted(const FGuid& InInstanceId, const FDocPuzzleInputEvent& Event)
{
	if (InInstanceId == InstanceId)
	{
		OnInputAccepted.Broadcast(InInstanceId, Event);
	}
}

void UDocPuzzleComponent::HandleSubsystemProgressChanged(const FGuid& InInstanceId, const FDocPuzzleEvaluation& Evaluation)
{
	if (InInstanceId == InstanceId)
	{
		OnProgressChanged.Broadcast(InInstanceId, Evaluation);
		OnProgressChangedNative.Broadcast(InInstanceId, Evaluation);
	}
}

void UDocPuzzleComponent::HandleSubsystemAttemptFailed(const FGuid& InInstanceId, const FGuid& AttemptId)
{
	if (InInstanceId == InstanceId)
	{
		OnAttemptFailed.Broadcast(InInstanceId, AttemptId);
	}
}

void UDocPuzzleComponent::HandleSubsystemPuzzleSolved(const FGuid& InInstanceId, const FGuid& AttemptId)
{
	if (InInstanceId == InstanceId)
	{
		OnPuzzleSolved.Broadcast(InInstanceId, AttemptId);
		OnPuzzleSolvedNative.Broadcast(InInstanceId, AttemptId);
	}
}

void UDocPuzzleComponent::HandleSubsystemResetCommitted(const FGuid& InInstanceId, int32 NewResetEpoch)
{
	if (InInstanceId == InstanceId)
	{
		OnResetCommitted.Broadcast(InInstanceId, NewResetEpoch);
	}
}
