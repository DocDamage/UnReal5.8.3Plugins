#include "DocAcousticPlaybackAdapter.h"

FDocSystemResult UDocAcousticReferencePlaybackAdapter::ApplyAcousticParameters(FName EmitterId, float FinalGain, float FinalCutoffHz)
{
	if (EmitterId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("EmitterId is empty"));
	}

	FDocAcousticEmitterState& State = EmitterStates.FindOrAdd(EmitterId);
	State.AppliedGain = FinalGain;
	State.AppliedCutoffHz = FinalCutoffHz;
	State.bIsActive = true;
	State.ApplyCount++;

	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAcousticReferencePlaybackAdapter::ResetAcousticParameters(FName EmitterId, float BaselineGain, float BaselineCutoffHz)
{
	if (EmitterId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("EmitterId is empty"));
	}

	if (FDocAcousticEmitterState* State = EmitterStates.Find(EmitterId))
	{
		State->AppliedGain = BaselineGain;
		State->AppliedCutoffHz = BaselineCutoffHz;
		State->bIsActive = false;
	}

	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAcousticReferencePlaybackAdapter::SetActiveListener(FName ListenerId)
{
	if (ListenerId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("ListenerId is empty"));
	}

	ActiveListenerId = ListenerId;
	return FDocSystemResult::MakeSuccess();
}

bool UDocAcousticReferencePlaybackAdapter::GetEmitterState(FName EmitterId, FDocAcousticEmitterState& OutState) const
{
	if (const FDocAcousticEmitterState* Found = EmitterStates.Find(EmitterId))
	{
		OutState = *Found;
		return true;
	}
	return false;
}
