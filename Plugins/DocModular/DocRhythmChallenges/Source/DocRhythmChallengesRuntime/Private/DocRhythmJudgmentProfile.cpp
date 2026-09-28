#include "DocRhythmJudgmentProfile.h"
#include "Misc/SecureHash.h"

const FDocRhythmWindowSet& UDocRhythmJudgmentProfile::GetWindows(bool bEarly, FDocRhythmWindowSet& Scratch) const
{
	if (bUseAsymmetricWindows)
	{
		return bEarly ? EarlyWindows : LateWindows;
	}
	Scratch.PerfectUs = PerfectHalfWindowUs;
	Scratch.GreatUs = GreatHalfWindowUs;
	Scratch.GoodUs = GoodHalfWindowUs;
	Scratch.MissUs = MissHalfWindowUs;
	return Scratch;
}

EDocRhythmHitJudgment UDocRhythmJudgmentProfile::EvaluateError(int64 ErrorUs) const
{
	FDocRhythmWindowSet Scratch;
	const FDocRhythmWindowSet& W = GetWindows(ErrorUs < 0, Scratch);
	const int64 AbsError = ErrorUs < 0 ? -ErrorUs : ErrorUs;
	if (AbsError <= W.PerfectUs)
	{
		return EDocRhythmHitJudgment::Perfect;
	}
	if (AbsError <= W.GreatUs)
	{
		return EDocRhythmHitJudgment::Great;
	}
	if (AbsError <= W.GoodUs)
	{
		return EDocRhythmHitJudgment::Good;
	}
	if (AbsError <= W.MissUs)
	{
		return EDocRhythmHitJudgment::Miss;
	}
	return EDocRhythmHitJudgment::None;
}

int32 UDocRhythmJudgmentProfile::GetPointsForJudgment(EDocRhythmHitJudgment Judgment) const
{
	switch (Judgment)
	{
	case EDocRhythmHitJudgment::Perfect: return PerfectPoints;
	case EDocRhythmHitJudgment::Great:   return GreatPoints;
	case EDocRhythmHitJudgment::Good:    return GoodPoints;
	case EDocRhythmHitJudgment::Miss:    return MissPoints;
	default:                             return 0;
	}
}

int64 UDocRhythmJudgmentProfile::GetLateMissWindowUs() const
{
	FDocRhythmWindowSet Scratch;
	return GetWindows(false, Scratch).MissUs;
}

FDocSystemResult UDocRhythmJudgmentProfile::ValidateProfile() const
{
	if (ProfileId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ProfileId is None"));
	}
	auto CheckSet = [](const FDocRhythmWindowSet& W) -> bool
	{
		return W.PerfectUs > 0 && W.GreatUs >= W.PerfectUs && W.GoodUs >= W.GreatUs && W.MissUs >= W.GoodUs && W.MissUs <= 2000000;
	};
	FDocRhythmWindowSet Scratch;
	if (!CheckSet(GetWindows(true, Scratch)) || !CheckSet(GetWindows(false, Scratch)))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Timing windows must be positive, non-decreasing (Perfect <= Great <= Good <= Miss) and <= 2s"));
	}
	if (PerfectPoints < 0 || PerfectPoints > MaxPoints || GreatPoints < 0 || GoodPoints < 0 || MissPoints < 0
		|| GreatPoints > PerfectPoints || GoodPoints > GreatPoints || MissPoints > GoodPoints)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Points must satisfy 0 <= Miss <= Good <= Great <= Perfect <= %d"), MaxPoints));
	}
	if (!FMath::IsFinite(MinHoldPercent) || MinHoldPercent < 0.0f || MinHoldPercent > 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("MinHoldPercent must be between 0.0 and 1.0"));
	}
	if (MaxAudioDriftUs <= 0 || HitchThresholdUs <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Drift and hitch thresholds must be positive"));
	}
	return FDocSystemResult::MakeSuccess();
}

FString UDocRhythmJudgmentProfile::ComputeProfileHash() const
{
	FDocRhythmWindowSet A, B;
	const FDocRhythmWindowSet& E = GetWindows(true, A);
	const FDocRhythmWindowSet& L = GetWindows(false, B);
	const FString Signature = FString::Printf(TEXT("%s|E%lld,%lld,%lld,%lld|L%lld,%lld,%lld,%lld|P%d,%d,%d,%d|H%.6f|F%d|A%d"),
		*ProfileId.ToString(), E.PerfectUs, E.GreatUs, E.GoodUs, E.MissUs, L.PerfectUs, L.GreatUs, L.GoodUs, L.MissUs,
		PerfectPoints, GreatPoints, GoodPoints, MissPoints, MinHoldPercent, static_cast<int32>(FocusLossPolicy), bIsAssisted ? 1 : 0);
	return FMD5::HashAnsiString(*Signature);
}
