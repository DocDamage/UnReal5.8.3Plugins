#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "DocRhythmTypes.h"
#include "DocRhythmChart.h"
#include "DocRhythmJudgmentProfile.h"
#include "DocRhythmClockProvider.h"
#include "DocRhythmPlaybackProvider.h"
#include "DocRhythmChallengeSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocRhythmStateChangedSignature, EDocRhythmChallengeState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocRhythmHitJudgedSignature, const FDocRhythmJudgmentResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocRhythmChallengeCompletedSignature, const FDocRhythmResult&, Result);

DECLARE_MULTICAST_DELEGATE_OneParam(FDocRhythmStateChangedNative, EDocRhythmChallengeState);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocRhythmHitJudgedNative, const FDocRhythmJudgmentResult&);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocRhythmChallengeCompletedNative, const FDocRhythmResult&);

/**
 * Per-player rhythm challenge. Chart time is derived from a monotonic clock minus the attempt epoch and
 * recorded pause intervals; it is never advanced by accumulating frame deltas. Inputs are judged at their
 * own mapped timestamps, not at processing time.
 */
UCLASS(BlueprintType)
class DOCRHYTHMCHALLENGESRUNTIME_API UDocRhythmChallengeSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UDocRhythmChallengeSubsystem* Get(const ULocalPlayer* Player);

	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual TStatId GetStatId() const override;

	// Providers

	/** Monotonic time source (must implement IDocRhythmClockProvider). Default: platform high-resolution clock. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void SetClockProvider(UObject* InProvider);

	/** Backing-track transport (must implement IDocRhythmPlaybackProvider). */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void SetPlaybackProvider(UObject* InProvider);

	// Lifecycle

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult LoadChart(UDocRhythmChart* InChart, UDocRhythmJudgmentProfile* InProfile = nullptr);

	/** Starts a new attempt (new AttemptId and transport generation). Use RestartChallenge while one is running. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult BeginChallenge();

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult SubmitInput(const FDocRhythmInputSample& Input, FDocRhythmJudgmentResult& OutJudgment);

	/** Unsupported if the playback backend cannot pause precisely; the attempt then keeps running. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult PauseChallenge();

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult ResumeChallenge();

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult RestartChallenge();

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult CancelChallenge();

	/** Advances judgment to the given monotonic time: count-in, hold completion, auto-misses, faults and completion. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void UpdateTimeline(int64 CurrentMonotonicUs);

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult QueryTimeline(FDocRhythmTimelineInfo& OutInfo) const;

	/** Final result after completion, otherwise the live attempt summary. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult GetResult(FDocRhythmResult& OutResult) const;

	// Calibration and focus

	/**
	 * Robust calibration from signed observed errors (input minus expected, microseconds). Needs at least
	 * MinCalibrationSamples accepted samples after MAD outlier rejection and an uncertainty <= MaxCalibrationUncertaintyUs;
	 * otherwise returns NotReady with diagnostic "CalibrationInsufficient" and leaves settings unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult RunCalibration(const TArray<int64>& ObservedErrorsUs, FName DeviceScopeId, FDocRhythmCalibrationSettings& OutCalibration);

	/** Refused while an attempt is running (it would make the attempt non-comparable). */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	FDocSystemResult SetCalibrationSettings(const FDocRhythmCalibrationSettings& InSettings);

	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	const FDocRhythmCalibrationSettings& GetCalibrationSettings() const { return CalibrationSettings; }

	/** Presentation only: never changes judgment. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void SetVisualOffsetUs(int64 VisualOffsetUs);

	/** A calibration measured on another device is invalidated. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void NotifyInputDeviceChanged(FName DeviceScopeId);

	/** Applies the profile's focus-loss policy to active holds. */
	UFUNCTION(BlueprintCallable, Category = "Rhythm")
	void OnFocusLost();

	UFUNCTION(BlueprintPure, Category = "Rhythm")
	EDocRhythmChallengeState GetState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Rhythm")
	int64 GetAttemptId() const { return CurrentAttemptId; }

	UFUNCTION(BlueprintPure, Category = "Rhythm")
	int32 GetSessionGeneration() const { return CurrentSessionGeneration; }

	UFUNCTION(BlueprintPure, Category = "Rhythm")
	int32 GetTransportGeneration() const { return TransportGeneration; }

	int64 GetNowUs() const;

	static constexpr int32 MinCalibrationSamples = 8;
	static constexpr int64 MaxCalibrationUncertaintyUs = 30000;

	UPROPERTY(BlueprintAssignable, Category = "Rhythm|Events")
	FDocRhythmStateChangedSignature OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Rhythm|Events")
	FDocRhythmHitJudgedSignature OnHitJudged;

	UPROPERTY(BlueprintAssignable, Category = "Rhythm|Events")
	FDocRhythmChallengeCompletedSignature OnChallengeCompleted;

	FDocRhythmStateChangedNative OnStateChangedNative;
	FDocRhythmHitJudgedNative OnHitJudgedNative;
	FDocRhythmChallengeCompletedNative OnChallengeCompletedNative;

private:
	struct FActiveNote
	{
		FDocRhythmNote Note;
		EDocRhythmHitJudgment Judgment = EDocRhythmHitJudgment::None;
		EDocRhythmHitJudgment StartJudgment = EDocRhythmHitJudgment::None;
		int64 ErrorUs = 0;
		bool bHoldActive = false;
	};

	struct FPauseInterval
	{
		int64 StartUs = 0;
		int64 EndUs = 0;
	};

	void SetState(EDocRhythmChallengeState NewState);
	void StartAttempt();
	void StopPlaybackIfAny();
	bool MapToChartUs(int64 MonotonicUs, int64& OutChartUs) const;
	int64 CurrentChartUs() const;
	void Judge(FActiveNote& Active, EDocRhythmHitJudgment Judgment, int64 JudgmentTimeUs, int64 ErrorUs, bool bAutoMiss, FDocRhythmJudgmentResult& OutResult);
	void CompleteChallenge();
	void BuildResult(FDocRhythmResult& OutResult) const;
	bool IsAttemptRunning() const;
	IDocRhythmPlaybackProvider* GetPlayback() const;

	UPROPERTY()
	TObjectPtr<UDocRhythmChart> LoadedChart;

	UPROPERTY()
	TObjectPtr<UDocRhythmJudgmentProfile> ActiveProfile;

	UPROPERTY()
	TObjectPtr<UObject> ClockProviderObj;

	UPROPERTY()
	TObjectPtr<UObject> PlaybackProviderObj;

	TArray<FActiveNote> ActiveNotes;
	TArray<FPauseInterval> PauseIntervals;
	TSet<FGuid> SeenInputIds;
	TSet<FString> SeenInputKeys;
	EDocRhythmChallengeState CurrentState = EDocRhythmChallengeState::Inactive;
	FDocRhythmCalibrationSettings CalibrationSettings;
	FDocRhythmCalibrationSettings AttemptCalibration;
	FDocRhythmResult FinalResult;
	bool bHasFinalResult = false;
	bool bAudibleThisAttempt = false;
	bool bPlaybackStarted = false;
	FString ChartHash;
	FString ProfileHash;

	int64 CurrentAttemptId = 0;
	int32 CurrentSessionGeneration = 0;
	int32 TransportGeneration = 0;
	int32 AttemptTransportGenerations = 0;
	int64 AttemptStartUs = 0;
	int64 EpochUs = 0;
	int64 PauseOpenStartUs = 0;
	int64 LastUpdateUs = 0;
	int32 CurrentCombo = 0;
	int32 MaxCombo = 0;
	int64 CurrentScore = 0;
	int32 PerfectCount = 0;
	int32 GreatCount = 0;
	int32 GoodCount = 0;
	int32 MissCount = 0;
	int32 TimingQualityFlags = 0;
};
