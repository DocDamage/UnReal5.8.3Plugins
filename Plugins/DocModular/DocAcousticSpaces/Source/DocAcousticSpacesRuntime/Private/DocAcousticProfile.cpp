#include "DocAcousticProfile.h"

FDocSystemResult UDocAcousticProfile::ValidateProfile() const
{
	if (ProfileId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ProfileId is empty"));
	}

	if (DecayTimeSeconds <= 0.0f || !FMath::IsFinite(DecayTimeSeconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("DecayTimeSeconds must be positive and finite"));
	}

	if (HighFrequencyCutoffHz < 20.0f || !FMath::IsFinite(HighFrequencyCutoffHz))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("HighFrequencyCutoffHz must be >= 20Hz and finite"));
	}

	return FDocSystemResult::MakeSuccess();
}
