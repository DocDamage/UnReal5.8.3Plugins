#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IDocAcousticPlaybackAdapter.h"
#include "DocAcousticPlaybackAdapter.generated.h"

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticEmitterState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float AppliedGain = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float AppliedCutoffHz = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bIsActive = false;

	/** Number of Apply calls received; lets tests prove the pipeline ran. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 ApplyCount = 0;
};

UCLASS(BlueprintType)
class DOCACOUSTICSPACESRUNTIME_API UDocAcousticReferencePlaybackAdapter : public UObject, public IDocAcousticPlaybackAdapter
{
	GENERATED_BODY()

public:
	virtual FDocSystemResult ApplyAcousticParameters(FName EmitterId, float FinalGain, float FinalCutoffHz) override;
	virtual FDocSystemResult ResetAcousticParameters(FName EmitterId, float BaselineGain, float BaselineCutoffHz) override;
	virtual FDocSystemResult SetActiveListener(FName ListenerId) override;
	virtual FName GetActiveListener() const override { return ActiveListenerId; }

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool GetEmitterState(FName EmitterId, FDocAcousticEmitterState& OutState) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ActiveListenerId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TMap<FName, FDocAcousticEmitterState> EmitterStates;
};
