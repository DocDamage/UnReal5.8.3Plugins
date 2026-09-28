#include "DocBeamReceiverComponent.h"
#include "DocOpticalBeamSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDocBeamReceiverComponent::UDocBeamReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocBeamReceiverComponent::OnRegister()
{
	Super::OnRegister();
	if (ReceiverId.IsNone() && GetOwner())
	{
		ReceiverId = GetOwner()->GetFName();
	}
	State.ReceiverId = ReceiverId;
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		const FDocSystemResult Result = Subsystem->RegisterReceiver(this);
		LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
	}
}

void UDocBeamReceiverComponent::OnUnregister()
{
	if (UDocOpticalBeamSubsystem* Subsystem = UDocOpticalBeamSubsystem::Get(GetWorld()))
	{
		Subsystem->UnregisterReceiver(this);
	}
	Super::OnUnregister();
}

bool UDocBeamReceiverComponent::Accepts(const FGameplayTag& Channel) const
{
	return AcceptedChannels.IsEmpty() || (Channel.IsValid() && Channel.MatchesAny(AcceptedChannels));
}

void UDocBeamReceiverComponent::RebuildView()
{
	TArray<FName> Keys;
	ContributionMap.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());

	State.EmitterContributions.Reset();
	State.Contributions.Reset();
	float Total = 0.0f;
	for (const FName& Key : Keys)
	{
		const FDocReceiverContribution& C = ContributionMap[Key];
		State.EmitterContributions.Add(Key, C.Intensity);
		State.Contributions.Add(C);
		if (!C.bHeldStale)
		{
			Total += C.Intensity;
		}
	}
	State.CurrentIntensity = Total;
	State.ReceiverId = ReceiverId;
	State.StateRevision++;
}

void UDocBeamReceiverComponent::ReplaceContribution(const FDocReceiverContribution& Contribution)
{
	if (Contribution.EmitterId.IsNone() || !FMath::IsFinite(Contribution.Intensity))
	{
		return;
	}
	FDocReceiverContribution Stored = Contribution;
	Stored.Intensity = FMath::Max(0.0f, Stored.Intensity);
	ContributionMap.Add(Stored.EmitterId, Stored);
	RebuildView();
}

void UDocBeamReceiverComponent::UpdateContribution(FName InEmitterId, float InIntensity, const FGameplayTag& InChannel)
{
	FDocReceiverContribution C;
	C.EmitterId = InEmitterId;
	C.Intensity = InIntensity;
	C.Channel = InChannel;
	ReplaceContribution(C);
}

void UDocBeamReceiverComponent::RemoveContribution(FName InEmitterId)
{
	if (ContributionMap.Remove(InEmitterId) > 0)
	{
		RebuildView();
	}
}

void UDocBeamReceiverComponent::ClearAllContributions()
{
	if (ContributionMap.Num() > 0)
	{
		ContributionMap.Empty();
		RebuildView();
	}
}

void UDocBeamReceiverComponent::SetContributionHeld(FName InEmitterId, bool bHeld)
{
	if (FDocReceiverContribution* C = ContributionMap.Find(InEmitterId))
	{
		if (C->bHeldStale != bHeld)
		{
			C->bHeldStale = bHeld;
			RebuildView();
		}
	}
}

bool UDocBeamReceiverComponent::EvaluateEligibility() const
{
	if (!bIsEnabled)
	{
		return false;
	}

	TArray<FName> Keys;
	ContributionMap.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());

	switch (AggregationMode)
	{
	case EDocBeamReceiverAggregationMode::AnyEligible:
		for (const FName& Key : Keys)
		{
			const FDocReceiverContribution& C = ContributionMap[Key];
			if (!C.bHeldStale && Accepts(C.Channel) && C.Intensity >= MinRequiredIntensity)
			{
				return true;
			}
		}
		return false;

	case EDocBeamReceiverAggregationMode::SumIntensity:
	{
		float Sum = 0.0f;
		bool bAny = false;
		for (const FName& Key : Keys)
		{
			const FDocReceiverContribution& C = ContributionMap[Key];
			if (!C.bHeldStale && Accepts(C.Channel))
			{
				Sum += C.Intensity;
				bAny = true;
			}
		}
		return bAny && Sum >= MinRequiredIntensity;
	}

	case EDocBeamReceiverAggregationMode::AllRequiredChannels:
	{
		if (AcceptedChannels.IsEmpty())
		{
			return false; // no declared channels: nothing can satisfy "all required"
		}
		for (const FGameplayTag& Required : AcceptedChannels)
		{
			bool bFound = false;
			for (const FName& Key : Keys)
			{
				const FDocReceiverContribution& C = ContributionMap[Key];
				if (!C.bHeldStale && C.Channel.MatchesTag(Required) && C.Intensity >= MinRequiredIntensity)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				return false;
			}
		}
		return true;
	}

	default:
		return false;
	}
}

void UDocBeamReceiverComponent::AdvanceDwell(float DeltaSeconds)
{
	if (DeltaSeconds < 0.0f || !FMath::IsFinite(DeltaSeconds))
	{
		return;
	}

	if (State.bLatched)
	{
		State.State = EDocBeamReceiverState::Activated;
		return;
	}

	if (EvaluateEligibility())
	{
		State.RemainingHysteresisSeconds = ReleaseHysteresisSeconds;
		if (!State.bIsActivated)
		{
			State.DwellTimeSeconds += DeltaSeconds;
			State.State = EDocBeamReceiverState::Dwelling;
			if (State.DwellTimeSeconds + UE_KINDA_SMALL_NUMBER >= RequiredDwellTimeSeconds)
			{
				SetActivated(true);
			}
		}
		else
		{
			State.State = EDocBeamReceiverState::Activated;
		}
		return;
	}

	// Not eligible: continuous dwell is lost immediately.
	State.DwellTimeSeconds = 0.0f;
	if (!State.bIsActivated)
	{
		State.State = EDocBeamReceiverState::Inactive;
		return;
	}

	State.RemainingHysteresisSeconds -= DeltaSeconds;
	if (State.RemainingHysteresisSeconds > UE_KINDA_SMALL_NUMBER)
	{
		State.State = EDocBeamReceiverState::Hysteresis;
	}
	else
	{
		State.RemainingHysteresisSeconds = 0.0f;
		SetActivated(false);
	}
}

void UDocBeamReceiverComponent::ResetLatch()
{
	if (State.bLatched)
	{
		State.bLatched = false;
		State.DwellTimeSeconds = 0.0f;
		SetActivated(false);
	}
}

void UDocBeamReceiverComponent::SetActivated(bool bNewActivated)
{
	if (State.bIsActivated == bNewActivated)
	{
		return;
	}
	State.bIsActivated = bNewActivated;
	State.State = bNewActivated ? EDocBeamReceiverState::Activated : EDocBeamReceiverState::Inactive;
	if (bNewActivated && bLatchOnActivate)
	{
		State.bLatched = true;
	}
	State.StateRevision++;

	const FDocReceiverState Committed = State;
	if (bNewActivated)
	{
		OnReceiverActivated.Broadcast(Committed);
		OnReceiverActivatedNative.Broadcast(Committed);
	}
	else
	{
		OnReceiverDeactivated.Broadcast(Committed);
		OnReceiverDeactivatedNative.Broadcast(Committed);
	}
}

FDocReceiverSnapshot UDocBeamReceiverComponent::CaptureSnapshot() const
{
	FDocReceiverSnapshot Snapshot;
	Snapshot.ReceiverId = ReceiverId;
	Snapshot.bLatchedActive = State.bLatched && State.bIsActivated;
	Snapshot.StateRevision = State.StateRevision;
	return Snapshot;
}

void UDocBeamReceiverComponent::RestoreSnapshot(const FDocReceiverSnapshot& Snapshot)
{
	ContributionMap.Empty();
	State.DwellTimeSeconds = 0.0f;
	State.RemainingHysteresisSeconds = 0.0f;
	const bool bLatchedActive = bLatchOnActivate && Snapshot.bLatchedActive;
	State.bLatched = bLatchedActive;
	State.bIsActivated = bLatchedActive;
	State.State = bLatchedActive ? EDocBeamReceiverState::Activated : EDocBeamReceiverState::Inactive;
	RebuildView();
	State.StateRevision = FMath::Max(State.StateRevision, Snapshot.StateRevision + 1);
}
