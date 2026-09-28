#include "DocPhotoEvaluationProfile.h"

FDocSystemResult UDocPhotoEvaluationProfile::ValidateProfile() const
{
	if (ProfileId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ProfileId is empty"));
	}
	const float Values[] = { MinVisibleFraction, MinBoundsInFrameFraction, MinDistance, MaxDistance, MaxFacingAngleDegrees,
		MaxOffAxisAngleDegrees, MinFramingCoverage, WeightVisibility, WeightDistance, WeightFacing };
	for (const float V : Values)
	{
		if (!FMath::IsFinite(V) || V < 0.0f)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Profile values must be finite and non-negative"));
		}
	}
	if (MinDistance > MaxDistance)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("MinDistance exceeds MaxDistance"));
	}
	if (MinVisibleFraction > 1.0f || MinBoundsInFrameFraction > 1.0f || MinFramingCoverage > 1.0f
		|| MaxFacingAngleDegrees > 180.0f || MaxOffAxisAngleDegrees > 180.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Fractions must be <= 1 and angles <= 180"));
	}
	if (WeightVisibility + WeightDistance + WeightFacing <= 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("At least one score weight must be positive"));
	}
	return FDocSystemResult::MakeSuccess();
}

bool UDocPhotoEvaluationProfile::EvaluateObservations(const TArray<FDocPhotoSubjectObservation>& Observations, float& OutScore, FString& OutReason) const
{
	OutScore = 0.0f;
	const FDocSystemResult Valid = ValidateProfile();
	if (!Valid.IsSuccess())
	{
		OutReason = FString::Printf(TEXT("InvalidProfile: %s"), *Valid.ToString());
		return false;
	}

	const float WeightSum = WeightVisibility + WeightDistance + WeightFacing;
	const FDocPhotoSubjectObservation* Best = nullptr;
	float BestScore = -1.0f;
	FString FirstRejection = TEXT("NoSubjectInFrame");

	auto Reject = [&FirstRejection](const FDocPhotoSubjectObservation& Obs, const TCHAR* Why)
	{
		if (FirstRejection == TEXT("NoSubjectInFrame"))
		{
			FirstRejection = FString::Printf(TEXT("%s:%s"), *Obs.SubjectId.ToString(), Why);
		}
	};

	for (const FDocPhotoSubjectObservation& Obs : Observations)
	{
		if (RequiredTag.IsValid() && !Obs.DefinitionTag.MatchesTag(RequiredTag)) { continue; }
		if (Obs.bPoseStale) { Reject(Obs, TEXT("PoseStale")); continue; }
		if (Obs.bIsBehindCamera || Obs.InFrameSampleCount == 0) { Reject(Obs, TEXT("NotInFrame")); continue; }
		if (Obs.BoundsInFrameFraction < MinBoundsInFrameFraction) { Reject(Obs, TEXT("Cropped")); continue; }
		if (Obs.DistanceToCamera < MinDistance || Obs.DistanceToCamera > MaxDistance) { Reject(Obs, TEXT("Distance")); continue; }
		if (Obs.EstimatedVisibleFraction < MinVisibleFraction) { Reject(Obs, TEXT("Occluded")); continue; }
		if (Obs.FacingAngleDegrees > MaxFacingAngleDegrees) { Reject(Obs, TEXT("FacingAway")); continue; }
		if (Obs.OffAxisAngleDegrees > MaxOffAxisAngleDegrees) { Reject(Obs, TEXT("OffAxis")); continue; }
		const FVector2D Size = Obs.ScreenBounds.GetSize();
		if (FMath::Clamp(Size.X * Size.Y, 0.0, 1.0) < MinFramingCoverage) { Reject(Obs, TEXT("TooSmall")); continue; }

		const float DistanceRange = FMath::Max(MaxDistance - MinDistance, KINDA_SMALL_NUMBER);
		const float DistanceTerm = 1.0f - FMath::Clamp((Obs.DistanceToCamera - MinDistance) / DistanceRange, 0.0f, 1.0f);
		const float FacingTerm = MaxFacingAngleDegrees > 0.0f ? 1.0f - FMath::Clamp(Obs.FacingAngleDegrees / MaxFacingAngleDegrees, 0.0f, 1.0f) : 1.0f;
		const float Score = (WeightVisibility * Obs.EstimatedVisibleFraction + WeightDistance * DistanceTerm + WeightFacing * FacingTerm) / WeightSum;

		// Stable tie-break by subject id.
		if (Score > BestScore + KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(Score, BestScore) && Best && Obs.SubjectId.LexicalLess(Best->SubjectId)))
		{
			BestScore = Score;
			Best = &Obs;
		}
	}

	if (!Best)
	{
		OutReason = FirstRejection;
		return false;
	}
	OutScore = FMath::Clamp(BestScore, 0.0f, 1.0f);
	OutReason = FString::Printf(TEXT("Qualified:%s"), *Best->SubjectId.ToString());
	return true;
}
