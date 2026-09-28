#include "DocGhostDefinitions.h"

FDocSystemResult UDocGhostProfile::ValidateProfile() const
{
	const bool bValid = FMath::IsFinite(SampleCadenceSeconds) && SampleCadenceSeconds > 0.0f
		&& FMath::IsFinite(MaxDurationSeconds) && MaxDurationSeconds > 0.0f && MaxDurationSeconds <= FDocGhostRecordingIO::MaxDurationSeconds
		&& MaxTracks >= 1 && MaxTracks <= FDocGhostRecordingIO::MaxTracks
		&& MaxSamplesPerTrack >= 2 && MaxSamplesPerTrack <= FDocGhostRecordingIO::MaxTotalSamples
		&& FMath::IsFinite(PositionChangeThreshold) && PositionChangeThreshold >= 0.0f
		&& FMath::IsFinite(RotationChangeThresholdDegrees) && RotationChangeThresholdDegrees >= 0.0f
		&& FMath::IsFinite(GapThresholdSeconds) && GapThresholdSeconds >= 0.0f
		&& FMath::IsFinite(ChunkDurationSeconds) && ChunkDurationSeconds > 0.0f;
	return bValid ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Ghost profile values out of range"));
}
