#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "DocEventsTypes.h"
#include "Engine/TimerHandle.h"
#include "DocWaitGameplayEventAction.generated.h"

class UDocGameplayEventSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocWaitEventReceived, const FDocGameplayEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocWaitEventEnded);

/**
 * Blueprint Listen / Wait node (handoff 6.5).
 *
 * Lifetime: registered with the game instance while active, so it survives the
 * calling graph going out of scope. Exactly one terminal pin fires: OnTimedOut,
 * OnCancelled (Cancel() called, world/bus shut down, or no bus), or — for
 * one-shot waits — after the single OnEventReceived. A listening (not one-shot)
 * node stays active until Cancel() or teardown.
 */
UCLASS()
class DOCEVENTSRUNTIME_API UDocWaitGameplayEventAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * @param bOnlyTriggerOnce  Wait (true) or Listen (false).
	 * @param TimeoutSeconds    0 = no timeout. Measured in world gameplay time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Events", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UDocWaitGameplayEventAction* WaitForGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, EDocEventTagMatch Match,
		bool bOnlyTriggerOnce, float TimeoutSeconds, bool bFilterByScope, FDocEventScope Scope);

	UPROPERTY(BlueprintAssignable)
	FDocWaitEventReceived OnEventReceived;

	UPROPERTY(BlueprintAssignable)
	FDocWaitEventEnded OnTimedOut;

	UPROPERTY(BlueprintAssignable)
	FDocWaitEventEnded OnCancelled;

	UFUNCTION(BlueprintCallable, Category = "Doc|Events")
	void Cancel();

	virtual void Activate() override;

private:
	void HandleEvent(const FDocGameplayEvent& Event);
	void HandleTimeout();
	void HandleBusShutdown();
	void Finish(bool bCancelled, bool bTimedOut);

	TWeakObjectPtr<UObject> WorldContext;
	TWeakObjectPtr<UDocGameplayEventSubsystem> Bus;
	FGameplayTag Tag;
	FDocEventListenOptions Options;
	float Timeout = 0.f;
	FDocRequestHandle Subscription;
	FTimerHandle TimeoutHandle;
	FDelegateHandle ShutdownHandle;
	bool bFinished = false;
};
