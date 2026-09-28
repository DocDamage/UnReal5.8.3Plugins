#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocInteractionInputComponent.generated.h"

class UInputAction;
class UInputComponent;
class UDocInteractorComponent;

/**
 * Optional Enhanced Input adapter (handoff 5.5). Binds the project's own input
 * actions to an interactor. It never adds, removes or clears mapping contexts:
 * the project owns its mappings.
 *
 * Usage: add next to (or on the same actor as) a UDocInteractorComponent, assign
 * actions, then call BindToInputComponent from SetupPlayerInputComponent or after
 * possession. Unbind happens automatically on EndPlay.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINTERACTIONRUNTIME_API UDocInteractionInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Pressed: begin selected interaction. Released: complete Continuous/Repeated, cancel unfinished holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> NextOptionAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> PreviousOptionAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> CancelAction;

	/** Interactor to drive. If unset, the first UDocInteractorComponent on the owner (or its pawn/controller) is used. */
	UPROPERTY(BlueprintReadWrite, Category = "Input")
	TWeakObjectPtr<UDocInteractorComponent> Interactor;

	/** Returns false if InputComponent is not an Enhanced Input component. Rebinding first removes previous bindings. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	bool BindToInputComponent(UInputComponent* InputComponent);

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void UnbindInput();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UDocInteractorComponent* ResolveInteractor();
	void HandleInteractStarted();
	void HandleInteractCompleted();
	void HandleNext();
	void HandlePrevious();
	void HandleCancel();

	TWeakObjectPtr<UInputComponent> BoundInput;
	TArray<uint32> BindingHandles;
};
