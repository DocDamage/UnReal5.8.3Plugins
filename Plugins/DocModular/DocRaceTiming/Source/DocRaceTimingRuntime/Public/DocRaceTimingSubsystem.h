#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocRaceTimingTypes.h"
#include "DocRaceCourseDefinition.h"
#include "DocRaceTimingSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnGatePassed, const FGuid&, RunId, FName, GateId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnRaceFinished, const FDocRaceResult&, Result);

UCLASS()
class DOCRACETIMINGRUNTIME_API UDocRaceTimingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Registration & Lifecycle API
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool RegisterParticipant(const FGuid& ParticipantId);

	/** Creates a run in Ready. Laps <= 0 uses the course default. Refuses courses with missing or duplicate gate ids. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool BeginRun(const FGuid& ParticipantId, UDocRaceCourseDefinition* CourseDef, FGuid& OutRunId, int32 Laps = 0, FName AssistanceCategory = TEXT("Default"));

	/** Standing-start courses only: Ready -> Countdown, with the go boundary on the sample clock. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool StartCountdown(const FGuid& RunId, double GoTimestamp);

	/**
	 * Trusted position sample. Timestamps are the run clock and must be strictly increasing; 0 means
	 * FPlatformTime::Seconds(), so do not mix explicit and default timestamps within one run.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool SubmitPositionSample(const FGuid& RunId, const FVector& Position, double Timestamp = 0.0);

	UFUNCTION(BlueprintCallable, Category = "Race")
	bool NotifyDiscontinuity(const FGuid& RunId);

	/** PracticeAllowPause courses only. Competitive runs refuse pause and keep their clock running. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool PauseRun(const FGuid& RunId, double Timestamp = 0.0);

	UFUNCTION(BlueprintCallable, Category = "Race")
	bool ResumeRun(const FGuid& RunId, double Timestamp = 0.0);

	/** Duplicate PenaltyIds return true without applying twice. Refused once the result is committed. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool ApplyPenalty(const FGuid& RunId, FName PenaltyId, float Seconds, const FString& Reason = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Race")
	bool AbortRun(const FGuid& RunId);

	/** Commits once for Finished, Invalid or Aborted runs; later calls return the committed result. Refused while the run is live. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	bool FinalizeResult(const FGuid& RunId, FDocRaceResult& OutResult);

	UFUNCTION(BlueprintPure, Category = "Race")
	bool QueryRun(const FGuid& RunId, FDocRaceRun& OutRun) const;

	UFUNCTION(BlueprintPure, Category = "Race")
	bool QuerySplits(const FGuid& RunId, TArray<FDocRaceSplit>& OutSplits) const;

	UFUNCTION(BlueprintPure, Category = "Race")
	bool QueryPersonalBest(FName CourseId, int32 CourseVersion, FName AssistanceCategory, FDocRaceResult& OutBestResult, bool bPractice = false) const;

	// State capture & restore
	UFUNCTION(BlueprintCallable, Category = "Race")
	TArray<FDocRaceResult> CaptureResults() const { return StoredResults; }

	/** Runs without a committed result. On restore they come back Aborted, never as a competitive resume. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	TArray<FDocRaceRun> CaptureActiveRuns() const;

	/** Restores committed results (deduplicated by RunId) and aborts any mid-run state. Never re-broadcasts or regrants. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StageRestore(const TArray<FDocRaceResult>& InResults, const TArray<FDocRaceRun>& InActiveRuns);

	UFUNCTION(BlueprintCallable, Category = "Race")
	void RestoreResults(const TArray<FDocRaceResult>& InResults);

	// Signals (broadcast after the run state is fully updated)
	UPROPERTY(BlueprintAssignable, Category = "Race")
	FDocOnGatePassed OnGatePassed;

	UPROPERTY(BlueprintAssignable, Category = "Race")
	FDocOnRaceFinished OnRaceFinished;

private:
	struct FActiveRunData
	{
		FDocRaceRun Run;
		TObjectPtr<UDocRaceCourseDefinition> CourseDef;
	};

	TSet<FGuid> RegisteredParticipants;
	TMap<FGuid, FActiveRunData> ActiveRuns;
	TArray<FDocRaceResult> StoredResults;
	TMap<FString, FDocRaceResult> PersonalBests;

	static FString MakePBKey(FName CourseId, int32 Version, FName Assistance, bool bPractice);
	static double RunClock(const FDocRaceRun& Run, double Timestamp);
	static double ResolveTimestamp(double Timestamp);
	void RebuildPersonalBests();
};
