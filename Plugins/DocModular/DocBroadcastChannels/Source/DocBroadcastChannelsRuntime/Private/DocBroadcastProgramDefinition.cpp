#include "DocBroadcastProgramDefinition.h"

FDocSystemResult UDocBroadcastProgramDefinition::ValidateProgram(bool bRequireSeek) const
{
	if (ProgramId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Program has no ProgramId"));
	}
	if (!bKnownDuration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			FString::Printf(TEXT("Program %s has no known duration"), *ProgramId.ToString()));
	}
	if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0 || DurationSeconds > 1.0e7)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Program %s duration must be finite and in (0, 1e7]"), *ProgramId.ToString()));
	}
	if (bRequireSeek && !bCanSeek)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			FString::Printf(TEXT("Program %s cannot seek; scheduled programs need seeking for late join"), *ProgramId.ToString()));
	}
	if (!FMath::IsFinite(AuthoredVolume) || AuthoredVolume < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("AuthoredVolume must be finite and >= 0"));
	}
	return FDocSystemResult::MakeSuccess();
}
