#include "DocInteractionInputComponent.h"
#include "DocInteractionComponents.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionInputComponent)

UDocInteractorComponent* UDocInteractionInputComponent::ResolveInteractor()
{
	if (UDocInteractorComponent* Explicit = Interactor.Get())
	{
		return Explicit;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (UDocInteractorComponent* OnOwner = Owner->FindComponentByClass<UDocInteractorComponent>())
	{
		return OnOwner;
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			return Pawn->FindComponentByClass<UDocInteractorComponent>();
		}
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (AController* Controller = Pawn->GetController())
		{
			return Controller->FindComponentByClass<UDocInteractorComponent>();
		}
	}
	return nullptr;
}

bool UDocInteractionInputComponent::BindToInputComponent(UInputComponent* InputComponent)
{
	UnbindInput();
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Enhanced)
	{
		return false;
	}
	BoundInput = Enhanced;
	if (InteractAction)
	{
		BindingHandles.Add(Enhanced->BindAction(InteractAction.Get(), ETriggerEvent::Started, this, &UDocInteractionInputComponent::HandleInteractStarted).GetHandle());
		BindingHandles.Add(Enhanced->BindAction(InteractAction.Get(), ETriggerEvent::Completed, this, &UDocInteractionInputComponent::HandleInteractCompleted).GetHandle());
		BindingHandles.Add(Enhanced->BindAction(InteractAction.Get(), ETriggerEvent::Canceled, this, &UDocInteractionInputComponent::HandleInteractCompleted).GetHandle());
	}
	if (NextOptionAction)
	{
		BindingHandles.Add(Enhanced->BindAction(NextOptionAction.Get(), ETriggerEvent::Started, this, &UDocInteractionInputComponent::HandleNext).GetHandle());
	}
	if (PreviousOptionAction)
	{
		BindingHandles.Add(Enhanced->BindAction(PreviousOptionAction.Get(), ETriggerEvent::Started, this, &UDocInteractionInputComponent::HandlePrevious).GetHandle());
	}
	if (CancelAction)
	{
		BindingHandles.Add(Enhanced->BindAction(CancelAction.Get(), ETriggerEvent::Started, this, &UDocInteractionInputComponent::HandleCancel).GetHandle());
	}
	return true;
}

void UDocInteractionInputComponent::UnbindInput()
{
	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(BoundInput.Get()))
	{
		for (const uint32 Handle : BindingHandles)
		{
			Enhanced->RemoveBindingByHandle(Handle);
		}
	}
	BindingHandles.Reset();
	BoundInput.Reset();
}

void UDocInteractionInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInput();
	Super::EndPlay(EndPlayReason);
}

void UDocInteractionInputComponent::HandleInteractStarted()
{
	if (UDocInteractorComponent* Target = ResolveInteractor())
	{
		Target->BeginSelectedInteraction();
	}
}

void UDocInteractionInputComponent::HandleInteractCompleted()
{
	if (UDocInteractorComponent* Target = ResolveInteractor())
	{
		Target->EndActiveInteraction();
	}
}

void UDocInteractionInputComponent::HandleNext()
{
	if (UDocInteractorComponent* Target = ResolveInteractor())
	{
		Target->SelectNextOption();
	}
}

void UDocInteractionInputComponent::HandlePrevious()
{
	if (UDocInteractorComponent* Target = ResolveInteractor())
	{
		Target->SelectPreviousOption();
	}
}

void UDocInteractionInputComponent::HandleCancel()
{
	if (UDocInteractorComponent* Target = ResolveInteractor())
	{
		Target->CancelActiveInteraction();
	}
}
