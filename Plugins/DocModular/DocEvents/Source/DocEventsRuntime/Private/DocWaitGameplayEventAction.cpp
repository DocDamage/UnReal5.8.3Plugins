#include "DocWaitGameplayEventAction.h"
#include "DocGameplayEventSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocWaitGameplayEventAction)

UDocWaitGameplayEventAction* UDocWaitGameplayEventAction::WaitForGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag,
	EDocEventTagMatch Match, bool bOnlyTriggerOnce, float TimeoutSeconds, bool bFilterByScope, FDocEventScope Scope)
{
	UDocWaitGameplayEventAction* Action = NewObject<UDocWaitGameplayEventAction>();
	Action->WorldContext = WorldContextObject;
	Action->Tag = EventTag;
	Action->Options.Match = Match;
	Action->Options.bOneShot = bOnlyTriggerOnce;
	Action->Options.bFilterByScope = bFilterByScope;
	Action->Options.ScopeFilter = Scope;
	Action->Timeout = FMath::Max(0.f, TimeoutSeconds);
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UDocWaitGameplayEventAction::Activate()
{
	UDocGameplayEventSubsystem* Subsystem = UDocGameplayEventSubsystem::Get(WorldContext.Get());
	if (!Subsystem || !Tag.IsValid())
	{
		Finish(/*bCancelled*/ true, false);
		return;
	}
	Bus = Subsystem;
	ShutdownHandle = Subsystem->OnBusShutdown.AddUObject(this, &UDocWaitGameplayEventAction::HandleBusShutdown);

	FDocEventListenOptions ListenOptions = Options;
	ListenOptions.bOneShot = false; // the action enforces one-shot itself so it can finish cleanly
	ListenOptions.Owner = this;
	Subscription = Subsystem->SubscribeNative(Tag, ListenOptions,
		FDocGameplayEventNativeDelegate::CreateUObject(this, &UDocWaitGameplayEventAction::HandleEvent));

	if (Timeout > 0.f)
	{
		if (UWorld* World = Subsystem->GetWorld())
		{
			World->GetTimerManager().SetTimer(TimeoutHandle,
				FTimerDelegate::CreateUObject(this, &UDocWaitGameplayEventAction::HandleTimeout), Timeout, false);
		}
	}
}

void UDocWaitGameplayEventAction::HandleEvent(const FDocGameplayEvent& Event)
{
	if (bFinished)
	{
		return;
	}
	OnEventReceived.Broadcast(Event);
	if (Options.bOneShot)
	{
		Finish(false, false);
	}
}

void UDocWaitGameplayEventAction::HandleTimeout()
{
	Finish(false, true);
}

void UDocWaitGameplayEventAction::HandleBusShutdown()
{
	// The bus is going away: its subscriptions and timers are released with it.
	Bus.Reset();
	Finish(true, false);
}

void UDocWaitGameplayEventAction::Cancel()
{
	Finish(true, false);
}

void UDocWaitGameplayEventAction::Finish(bool bCancelled, bool bTimedOut)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	if (UDocGameplayEventSubsystem* Subsystem = Bus.Get())
	{
		Subsystem->Unsubscribe(Subscription);
		Subsystem->OnBusShutdown.Remove(ShutdownHandle);
		if (UWorld* World = Subsystem->GetWorld())
		{
			World->GetTimerManager().ClearTimer(TimeoutHandle);
		}
	}

	if (bTimedOut)
	{
		OnTimedOut.Broadcast();
	}
	else if (bCancelled)
	{
		OnCancelled.Broadcast();
	}
	SetReadyToDestroy();
}
