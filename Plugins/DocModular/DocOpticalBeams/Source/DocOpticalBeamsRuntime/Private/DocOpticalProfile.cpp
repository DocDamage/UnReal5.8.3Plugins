#include "DocOpticalProfile.h"

FDocSystemResult UDocOpticalProfile::ValidateProfile() const
{
	if (ProfileId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ProfileId is None"));
	}
	if (!FMath::IsFinite(Reflectivity) || Reflectivity < 0.0f || Reflectivity > 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Reflectivity must be between 0.0 and 1.0 (cannot amplify energy)"));
	}
	if (!FMath::IsFinite(TransmissionEfficiency) || TransmissionEfficiency < 0.0f || TransmissionEfficiency > 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("TransmissionEfficiency must be between 0.0 and 1.0 (cannot amplify energy)"));
	}
	if (SurfaceType == EDocBeamSurfaceType::Prism)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Prism (refraction/splitting) is a future capability; use Filter with ShiftedOutputChannel for relabeling."));
	}
	if (SurfaceType != EDocBeamSurfaceType::Filter && ShiftedOutputChannel.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ShiftedOutputChannel is only meaningful on a Filter."));
	}
	return FDocSystemResult::MakeSuccess();
}
