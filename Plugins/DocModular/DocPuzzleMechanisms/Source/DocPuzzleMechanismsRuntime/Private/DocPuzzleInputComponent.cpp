#include "DocPuzzleInputComponent.h"

UDocPuzzleInputComponent::UDocPuzzleInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SourceId = FGuid::NewGuid();
}

void UDocPuzzleInputComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!SourceId.IsValid())
	{
		SourceId = FGuid::NewGuid();
	}
}

void UDocPuzzleInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearContributor();
	Super::EndPlay(EndPlayReason);
}

FDocSystemResult UDocPuzzleInputComponent::SendTrigger()
{
	if (!TargetPuzzleComponent.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("TargetPuzzleComponent is not bound."));
	}
	return TargetPuzzleComponent->SubmitTrigger(InputId, SourceId);
}

FDocSystemResult UDocPuzzleInputComponent::SetBoolean(bool bActive)
{
	if (!TargetPuzzleComponent.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("TargetPuzzleComponent is not bound."));
	}
	return TargetPuzzleComponent->SetContributor(InputId, FDocPuzzleInputValue::MakeBoolean(bActive), SourceId);
}

FDocSystemResult UDocPuzzleInputComponent::SetScalar(float Value)
{
	if (!TargetPuzzleComponent.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("TargetPuzzleComponent is not bound."));
	}
	return TargetPuzzleComponent->SetContributor(InputId, FDocPuzzleInputValue::MakeScalar(Value), SourceId);
}

FDocSystemResult UDocPuzzleInputComponent::SetSymbol(FName Symbol)
{
	if (!TargetPuzzleComponent.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("TargetPuzzleComponent is not bound."));
	}
	return TargetPuzzleComponent->SetContributor(InputId, FDocPuzzleInputValue::MakeSymbol(Symbol), SourceId);
}

FDocSystemResult UDocPuzzleInputComponent::ClearContributor()
{
	if (!TargetPuzzleComponent.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("TargetPuzzleComponent is not bound."));
	}
	return TargetPuzzleComponent->RemoveContributor(InputId, SourceId);
}
