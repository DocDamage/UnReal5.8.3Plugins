#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocRhythmTypes.h"
#include "DocRhythmJudgmentProfile.generated.h"

/**
 * Judgment windows, scoring and hold/assist rules.
 * Window edges are inclusive (|error| <= window). Negative error = early, positive = late.
 */
UCLASS(BlueprintType)
class DOCRHYTHMCHALLENGESRUNTIME_API UDocRhythmJudgmentProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxPoints = 1000000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	FName ProfileId = TEXT("Default");

	// Symmetric windows (used unless bUseAsymmetricWindows)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (ClampMin = "1"))
	int64 PerfectHalfWindowUs = 30000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (ClampMin = "1"))
	int64 GreatHalfWindowUs = 60000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (ClampMin = "1"))
	int64 GoodHalfWindowUs = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (ClampMin = "1"))
	int64 MissHalfWindowUs = 150000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows")
	bool bUseAsymmetricWindows = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (EditCondition = "bUseAsymmetricWindows"))
	FDocRhythmWindowSet EarlyWindows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing Windows", meta = (EditCondition = "bUseAsymmetricWindows"))
	FDocRhythmWindowSet LateWindows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
	int32 PerfectPoints = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
	int32 GreatPoints = 750;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
	int32 GoodPoints = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
	int32 MissPoints = 0;

	/** A hold counts once the release happens at or after Start + round(Duration * MinHoldPercent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinHoldPercent = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holds")
	EDocRhythmFocusLossPolicy FocusLossPolicy = EDocRhythmFocusLossPolicy::BreakHold;

	/** Audio position drift beyond this re-anchors the transport (new transport generation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "1"))
	int64 MaxAudioDriftUs = 20000;

	/** A gap between timeline updates larger than this flags ClockHitchDetected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "1"))
	int64 HitchThresholdUs = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistance")
	bool bIsAssisted = false;

	/** Early windows for negative errors, late windows for positive ones. */
	const FDocRhythmWindowSet& GetWindows(bool bEarly, FDocRhythmWindowSet& Scratch) const;

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	EDocRhythmHitJudgment EvaluateError(int64 ErrorUs) const;

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	int32 GetPointsForJudgment(EDocRhythmHitJudgment Judgment) const;

	/** Latest positive error that can still select a note. */
	int64 GetLateMissWindowUs() const;

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult ValidateProfile() const;

	/** Stable hash of everything that affects judgment and score. */
	FString ComputeProfileHash() const;
};
