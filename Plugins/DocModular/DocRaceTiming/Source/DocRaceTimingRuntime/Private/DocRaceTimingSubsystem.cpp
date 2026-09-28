#include "DocRaceTimingSubsystem.h"

namespace DocRaceTimingPrivate
{
	const FName ReasonMissedGate(TEXT("MissedGate"));
	const FName ReasonFalseStart(TEXT("FalseStart"));
	const FName ReasonFalseStartRejected(TEXT("FalseStartRejected"));
	const FName ReasonAborted(TEXT("Aborted"));
	const FName ReasonRestoredMidRun(TEXT("RestoredMidRun"));
	const FName PenaltyFalseStart(TEXT("FalseStart"));
	constexpr float DefaultMaxPenaltySeconds = 600.0f;

	static bool IsTerminal(EDocRaceState State)
	{
		return State == EDocRaceState::Finished || State == EDocRaceState::Invalid || State == EDocRaceState::Aborted;
	}

	static bool AcceptsSamples(EDocRaceState State)
	{
		return State == EDocRaceState::Ready || State == EDocRaceState::Countdown || State == EDocRaceState::Running;
	}
}

FString UDocRaceTimingSubsystem::MakePBKey(FName CourseId, int32 Version, FName Assistance, bool bPractice)
{
	return FString::Printf(TEXT("%s_v%d_%s_%s"), *CourseId.ToString(), Version, *Assistance.ToString(), bPractice ? TEXT("Practice") : TEXT("Competitive"));
}

double UDocRaceTimingSubsystem::RunClock(const FDocRaceRun& Run, double Timestamp)
{
	return FMath::Max(0.0, Timestamp - Run.RunStartTime - Run.PausedSeconds);
}

double UDocRaceTimingSubsystem::ResolveTimestamp(double Timestamp)
{
	return Timestamp > 0.0 ? Timestamp : FPlatformTime::Seconds();
}

void UDocRaceTimingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocRaceTimingSubsystem::Deinitialize()
{
	ActiveRuns.Empty();
	StoredResults.Empty();
	PersonalBests.Empty();
	RegisteredParticipants.Empty();
	Super::Deinitialize();
}

bool UDocRaceTimingSubsystem::RegisterParticipant(const FGuid& ParticipantId)
{
	if (!ParticipantId.IsValid())
	{
		return false;
	}
	RegisteredParticipants.Add(ParticipantId);
	return true;
}

bool UDocRaceTimingSubsystem::BeginRun(const FGuid& ParticipantId, UDocRaceCourseDefinition* CourseDef, FGuid& OutRunId, int32 Laps, FName AssistanceCategory)
{
	if (!ParticipantId.IsValid() || !CourseDef || CourseDef->OrderedGates.Num() == 0)
	{
		return false;
	}

	// Course authoring check: every gate needs a stable, unique id and finite geometry.
	TSet<FName> SeenGateIds;
	for (const FDocRaceGateDefinition& Gate : CourseDef->OrderedGates)
	{
		bool bDuplicate = false;
		SeenGateIds.Add(Gate.GateId, &bDuplicate);
		if (Gate.GateId.IsNone() || bDuplicate || Gate.Location.ContainsNaN() || Gate.ForwardDirection.ContainsNaN() || Gate.ForwardDirection.IsNearlyZero())
		{
			return false;
		}
	}

	OutRunId = FGuid::NewGuid();

	FActiveRunData RunData;
	RunData.CourseDef = CourseDef;
	RunData.Run.RunId = OutRunId;
	RunData.Run.ParticipantId = ParticipantId;
	RunData.Run.CourseId = CourseDef->CourseId;
	RunData.Run.CourseVersion = CourseDef->CourseVersion;
	RunData.Run.AssistanceCategory = AssistanceCategory;
	RunData.Run.State = EDocRaceState::Ready;
	RunData.Run.CurrentLap = 1;
	RunData.Run.TargetLaps = Laps > 0 ? Laps : FMath::Max(1, CourseDef->DefaultLaps);
	RunData.Run.ExpectedGateIndex = 0;
	RunData.Run.RunStartTime = 0.0;
	RunData.Run.bHasLastSample = false;

	ActiveRuns.Add(OutRunId, RunData);
	return true;
}

bool UDocRaceTimingSubsystem::StartCountdown(const FGuid& RunId, double GoTimestamp)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || !RunData->CourseDef || RunData->CourseDef->bRunningStart || RunData->Run.State != EDocRaceState::Ready)
	{
		return false;
	}
	if (!FMath::IsFinite(GoTimestamp) || GoTimestamp <= 0.0 || GoTimestamp <= RunData->Run.LastAcceptedTimestamp)
	{
		return false;
	}
	RunData->Run.State = EDocRaceState::Countdown;
	RunData->Run.StartBoundaryTime = GoTimestamp;
	return true;
}

bool UDocRaceTimingSubsystem::SubmitPositionSample(const FGuid& RunId, const FVector& Position, double Timestamp)
{
	using namespace DocRaceTimingPrivate;

	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || !RunData->CourseDef || !AcceptsSamples(RunData->Run.State) || RunData->Run.bPaused)
	{
		return false;
	}
	if (Position.ContainsNaN() || !FMath::IsFinite(Timestamp))
	{
		return false;
	}
	Timestamp = ResolveTimestamp(Timestamp);

	FDocRaceRun& Run = RunData->Run;
	const UDocRaceCourseDefinition* Course = RunData->CourseDef;

	// One monotonic clock per run: never accept a sample at or before the previous one.
	if (Run.LastAcceptedTimestamp >= 0.0 && Timestamp <= Run.LastAcceptedTimestamp)
	{
		return false;
	}
	Run.LastAcceptedTimestamp = Timestamp;

	// Standing start: the run clock starts at the go boundary, not at a sample or crossing.
	if (Run.State == EDocRaceState::Countdown && Timestamp >= Run.StartBoundaryTime)
	{
		Run.State = EDocRaceState::Running;
		Run.RunStartTime = Run.StartBoundaryTime;
	}

	if (!Run.bHasLastSample)
	{
		Run.LastSamplePosition = Position;
		Run.LastSampleTime = Timestamp;
		Run.bHasLastSample = true;
		if (Run.State == EDocRaceState::Running)
		{
			Run.ElapsedTime = (float)RunClock(Run, Timestamp);
		}
		return true;
	}

	struct FGateCrossing
	{
		int32 GateIndex;
		float Fraction;
		FName GateId;
	};

	TArray<FGateCrossing> Crossings;
	const int32 NumGates = Course->OrderedGates.Num();
	for (int32 i = 0; i < NumGates; ++i)
	{
		const FDocRaceGateDefinition& Gate = Course->OrderedGates[i];
		float Frac = 0.0f;
		FVector CrossPt;
		if (Gate.TestSweptCrossing(Run.LastSamplePosition, Position, Frac, CrossPt))
		{
			FGateCrossing C{ i, Frac, Gate.GateId };
			Crossings.Add(C);
		}
	}

	// RAC-03: increasing intersection fraction, then stable gate id.
	Crossings.Sort([](const FGateCrossing& A, const FGateCrossing& B)
	{
		if (A.Fraction != B.Fraction)
		{
			return A.Fraction < B.Fraction;
		}
		return A.GateId.Compare(B.GateId) < 0;
	});

	const double SegmentStart = Run.LastSampleTime;
	TArray<FName> PassedGates;
	bool bFinishedNow = false;

	for (const FGateCrossing& Crossing : Crossings)
	{
		// Linear-motion estimate of when the plane was crossed, on the sample clock.
		const double CrossTime = SegmentStart + (Timestamp - SegmentStart) * (double)Crossing.Fraction;
		const bool bIsRaceStart = Crossing.GateIndex == 0 && Run.CurrentLap == 1 && Run.ExpectedGateIndex == 0;

		if (Run.State != EDocRaceState::Running && !bIsRaceStart)
		{
			continue; // Not started yet: no other gate is eligible.
		}

		if (bIsRaceStart)
		{
			if (Course->bRunningStart)
			{
				if (Run.State != EDocRaceState::Running)
				{
					Run.State = EDocRaceState::Running;
					Run.RunStartTime = CrossTime;
				}
			}
			else
			{
				if (Run.State == EDocRaceState::Ready)
				{
					continue; // No countdown yet: the start line is not armed.
				}
				if (CrossTime < Run.StartBoundaryTime)
				{
					++Run.FalseStartCount;
					if (Course->FalseStartPolicy == EDocFalseStartPolicy::RejectStart)
					{
						Run.StatusReason = ReasonFalseStartRejected;
						continue; // Crossing not counted; the participant must cross again after go.
					}
					if (Course->FalseStartPolicy == EDocFalseStartPolicy::Invalidate)
					{
						Run.State = EDocRaceState::Invalid;
						Run.StatusReason = ReasonFalseStart;
						break;
					}
					// Penalty: the crossing counts, the penalty is recorded once.
					if (!Run.AppliedPenaltyIds.Contains(PenaltyFalseStart))
					{
						Run.AppliedPenaltyIds.Add(PenaltyFalseStart);
						FDocRacePenalty Pen;
						Pen.PenaltyId = PenaltyFalseStart;
						Pen.PenaltySeconds = FMath::Clamp(Course->FalseStartPenaltySeconds, 0.0f, Course->MaxPenaltySeconds);
						Pen.Reason = TEXT("False start");
						Run.AppliedPenalties.Add(Pen);
					}
				}
				if (Run.State != EDocRaceState::Running)
				{
					Run.State = EDocRaceState::Running;
					Run.RunStartTime = Run.StartBoundaryTime;
				}
			}
		}

		if (Crossing.GateIndex == Run.ExpectedGateIndex)
		{
			FDocRaceSplit Split;
			Split.GateId = Crossing.GateId;
			Split.LapNumber = Run.CurrentLap;
			Split.SplitTimeSeconds = (float)RunClock(Run, CrossTime);
			const float PreviousSplit = Run.Splits.Num() > 0 ? Run.Splits.Last().SplitTimeSeconds : 0.0f;
			Split.SegmentDurationSeconds = FMath::Max(0.0f, Split.SplitTimeSeconds - PreviousSplit);
			Run.Splits.Add(Split);
			PassedGates.Add(Crossing.GateId);
			Run.LastPassedGateIndex = Crossing.GateIndex;

			Run.ExpectedGateIndex++;
			if (Run.ExpectedGateIndex >= NumGates)
			{
				if (Run.CurrentLap < Run.TargetLaps)
				{
					Run.CurrentLap++;
					Run.ExpectedGateIndex = 0;
				}
				else
				{
					Run.State = EDocRaceState::Finished;
					Run.ElapsedTime = Split.SplitTimeSeconds;
					bFinishedNow = true;
					break;
				}
			}
		}
		else if (Crossing.GateIndex < Run.ExpectedGateIndex || Crossing.GateIndex == Run.LastPassedGateIndex)
		{
			// Hysteresis: re-crossing a gate already passed (jitter back and forth) grants and costs nothing.
			continue;
		}
		else
		{
			// A later gate before the expected one: a required checkpoint was skipped (RAC-06).
			Run.State = EDocRaceState::Invalid;
			Run.StatusReason = ReasonMissedGate;
			break;
		}
	}

	Run.LastSamplePosition = Position;
	Run.LastSampleTime = Timestamp;
	if (Run.State == EDocRaceState::Running)
	{
		Run.ElapsedTime = (float)RunClock(Run, Timestamp);
	}

	// Notify only after all state is updated; listeners may add runs and invalidate RunData.
	for (const FName& GateId : PassedGates)
	{
		OnGatePassed.Broadcast(RunId, GateId);
	}
	if (bFinishedNow)
	{
		FDocRaceResult Res;
		FinalizeResult(RunId, Res);
	}
	return true;
}

bool UDocRaceTimingSubsystem::NotifyDiscontinuity(const FGuid& RunId)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData)
	{
		return false;
	}

	// Discontinuity breaks the swept segment; the next sample does not interpolate across the teleport (RAC-04).
	RunData->Run.bHasLastSample = false;
	return true;
}

bool UDocRaceTimingSubsystem::PauseRun(const FGuid& RunId, double Timestamp)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || !RunData->CourseDef || RunData->Run.State != EDocRaceState::Running || RunData->Run.bPaused)
	{
		return false;
	}
	if (RunData->CourseDef->PausePolicy != EDocRacePausePolicy::PracticeAllowPause)
	{
		return false; // Competitive runs never pause; the clock keeps running.
	}
	if (!FMath::IsFinite(Timestamp))
	{
		return false;
	}
	Timestamp = ResolveTimestamp(Timestamp);
	FDocRaceRun& Run = RunData->Run;
	if (Run.LastAcceptedTimestamp >= 0.0 && Timestamp < Run.LastAcceptedTimestamp)
	{
		return false;
	}
	Run.bPaused = true;
	Run.PauseStartTime = Timestamp;
	Run.bPractice = true;
	Run.bHasLastSample = false; // Movement during a pause never grants gates.
	return true;
}

bool UDocRaceTimingSubsystem::ResumeRun(const FGuid& RunId, double Timestamp)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || !RunData->Run.bPaused || !FMath::IsFinite(Timestamp))
	{
		return false;
	}
	Timestamp = ResolveTimestamp(Timestamp);
	FDocRaceRun& Run = RunData->Run;
	if (Timestamp < Run.PauseStartTime)
	{
		return false;
	}
	Run.PausedSeconds += Timestamp - Run.PauseStartTime;
	Run.bPaused = false;
	Run.LastAcceptedTimestamp = FMath::Max(Run.LastAcceptedTimestamp, Timestamp);
	return true;
}

bool UDocRaceTimingSubsystem::ApplyPenalty(const FGuid& RunId, FName PenaltyId, float Seconds, const FString& Reason)
{
	using namespace DocRaceTimingPrivate;

	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || PenaltyId.IsNone())
	{
		return false;
	}
	FDocRaceRun& Run = RunData->Run;

	// Idempotency: duplicate penalty ID reports are ignored (RAC-07)
	if (Run.AppliedPenaltyIds.Contains(PenaltyId))
	{
		return true;
	}
	if (Run.bResultCommitted)
	{
		return false; // A committed result never changes.
	}
	const float MaxPenalty = RunData->CourseDef ? RunData->CourseDef->MaxPenaltySeconds : DefaultMaxPenaltySeconds;
	if (!FMath::IsFinite(Seconds) || Seconds < 0.0f || Seconds > MaxPenalty)
	{
		return false;
	}

	Run.AppliedPenaltyIds.Add(PenaltyId);
	FDocRacePenalty Pen;
	Pen.PenaltyId = PenaltyId;
	Pen.PenaltySeconds = Seconds;
	Pen.Reason = Reason;
	Run.AppliedPenalties.Add(Pen);
	return true;
}

bool UDocRaceTimingSubsystem::AbortRun(const FGuid& RunId)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData || DocRaceTimingPrivate::IsTerminal(RunData->Run.State))
	{
		return false;
	}

	RunData->Run.State = EDocRaceState::Aborted;
	RunData->Run.StatusReason = DocRaceTimingPrivate::ReasonAborted;
	RunData->Run.bPaused = false;
	return true;
}

bool UDocRaceTimingSubsystem::FinalizeResult(const FGuid& RunId, FDocRaceResult& OutResult)
{
	FActiveRunData* RunData = ActiveRuns.Find(RunId);
	if (!RunData)
	{
		return false;
	}

	// Idempotency: duplicate finish returns existing committed result (RAC-07)
	if (RunData->Run.bResultCommitted)
	{
		for (const FDocRaceResult& Res : StoredResults)
		{
			if (Res.RunId == RunId)
			{
				OutResult = Res;
				return true;
			}
		}
		return false;
	}

	FDocRaceRun& Run = RunData->Run;
	if (!DocRaceTimingPrivate::IsTerminal(Run.State))
	{
		return false; // A live run has no result yet.
	}

	OutResult = FDocRaceResult();
	OutResult.RunId = Run.RunId;
	OutResult.ParticipantId = Run.ParticipantId;
	OutResult.CourseId = Run.CourseId;
	OutResult.CourseVersion = Run.CourseVersion;
	OutResult.AssistanceCategory = Run.AssistanceCategory;
	OutResult.State = Run.State;
	OutResult.StatusReason = Run.StatusReason;
	OutResult.bPracticeResult = Run.bPractice;
	OutResult.ElapsedTimeSeconds = Run.ElapsedTime;

	double TotalPenalty = 0.0;
	for (const FDocRacePenalty& Pen : Run.AppliedPenalties)
	{
		TotalPenalty += Pen.PenaltySeconds;
	}
	const double FinalTime = (double)OutResult.ElapsedTimeSeconds + TotalPenalty;
	OutResult.PenaltySeconds = (float)FMath::Min(TotalPenalty, (double)MAX_flt);
	OutResult.FinalTimeSeconds = (float)FMath::Min(FinalTime, (double)MAX_flt);
	OutResult.Splits = Run.Splits;
	OutResult.Penalties = Run.AppliedPenalties;

	// Personal Best check (RAC-08): only finished runs, keyed by exact course/version/assistance/practice.
	if (Run.State == EDocRaceState::Finished)
	{
		const FString PBKey = MakePBKey(Run.CourseId, Run.CourseVersion, Run.AssistanceCategory, Run.bPractice);
		const FDocRaceResult* ExistingPB = PersonalBests.Find(PBKey);
		if (!ExistingPB || OutResult.FinalTimeSeconds < ExistingPB->FinalTimeSeconds)
		{
			OutResult.bIsPersonalBest = true;
			PersonalBests.Add(PBKey, OutResult);
		}
		OutResult.bRewardGranted = true;
	}

	Run.bResultCommitted = true;
	StoredResults.Add(OutResult);
	const FDocRaceResult Broadcasted = OutResult;
	OnRaceFinished.Broadcast(Broadcasted);
	return true;
}

bool UDocRaceTimingSubsystem::QueryRun(const FGuid& RunId, FDocRaceRun& OutRun) const
{
	if (const FActiveRunData* Data = ActiveRuns.Find(RunId))
	{
		OutRun = Data->Run;
		return true;
	}
	return false;
}

bool UDocRaceTimingSubsystem::QuerySplits(const FGuid& RunId, TArray<FDocRaceSplit>& OutSplits) const
{
	if (const FActiveRunData* Data = ActiveRuns.Find(RunId))
	{
		OutSplits = Data->Run.Splits;
		return true;
	}
	return false;
}

bool UDocRaceTimingSubsystem::QueryPersonalBest(FName CourseId, int32 CourseVersion, FName AssistanceCategory, FDocRaceResult& OutBestResult, bool bPractice) const
{
	const FString PBKey = MakePBKey(CourseId, CourseVersion, AssistanceCategory, bPractice);
	if (const FDocRaceResult* Best = PersonalBests.Find(PBKey))
	{
		OutBestResult = *Best;
		return true;
	}
	return false;
}

TArray<FDocRaceRun> UDocRaceTimingSubsystem::CaptureActiveRuns() const
{
	TArray<FDocRaceRun> Out;
	for (const TPair<FGuid, FActiveRunData>& Pair : ActiveRuns)
	{
		if (!Pair.Value.Run.bResultCommitted)
		{
			Out.Add(Pair.Value.Run);
		}
	}
	return Out;
}

void UDocRaceTimingSubsystem::RebuildPersonalBests()
{
	PersonalBests.Empty();
	for (const FDocRaceResult& Res : StoredResults)
	{
		if (Res.State == EDocRaceState::Finished)
		{
			const FString PBKey = MakePBKey(Res.CourseId, Res.CourseVersion, Res.AssistanceCategory, Res.bPracticeResult);
			const FDocRaceResult* ExistingPB = PersonalBests.Find(PBKey);
			if (!ExistingPB || Res.FinalTimeSeconds < ExistingPB->FinalTimeSeconds)
			{
				PersonalBests.Add(PBKey, Res);
			}
		}
	}
}

void UDocRaceTimingSubsystem::StageRestore(const TArray<FDocRaceResult>& InResults, const TArray<FDocRaceRun>& InActiveRuns)
{
	using namespace DocRaceTimingPrivate;

	// Committed results: one per RunId. Restore never re-broadcasts OnRaceFinished or grants anything new.
	StoredResults.Reset();
	TSet<FGuid> CommittedIds;
	for (const FDocRaceResult& Res : InResults)
	{
		bool bDuplicate = false;
		CommittedIds.Add(Res.RunId, &bDuplicate);
		if (Res.RunId.IsValid() && !bDuplicate)
		{
			StoredResults.Add(Res);
		}
	}
	RebuildPersonalBests();

	// Mid-run state never resumes as a valid competitive run: it comes back Aborted, with no reward.
	for (const FDocRaceRun& SavedRun : InActiveRuns)
	{
		if (!SavedRun.RunId.IsValid() || CommittedIds.Contains(SavedRun.RunId) || ActiveRuns.Contains(SavedRun.RunId))
		{
			continue;
		}
		FActiveRunData Restored;
		Restored.Run = SavedRun;
		Restored.Run.State = EDocRaceState::Aborted;
		Restored.Run.StatusReason = ReasonRestoredMidRun;
		Restored.Run.bResultCommitted = false;
		Restored.Run.bHasLastSample = false;
		Restored.Run.bPaused = false;
		Restored.CourseDef = nullptr;
		ActiveRuns.Add(SavedRun.RunId, Restored);
	}
}

void UDocRaceTimingSubsystem::RestoreResults(const TArray<FDocRaceResult>& InResults)
{
	StageRestore(InResults, TArray<FDocRaceRun>());
}
