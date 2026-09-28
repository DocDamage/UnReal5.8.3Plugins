#include "DocRhythmChallengeSubsystem.h"
#include "DocRhythmChallengesLog.h"
#include "Engine/LocalPlayer.h"
#include "HAL/PlatformTime.h"

namespace DocRhythmPrivate
{
	static int64 Median(TArray<int64> Values)
	{
		if (Values.IsEmpty())
		{
			return 0;
		}
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return (Values.Num() % 2 == 1) ? Values[Mid] : (Values[Mid - 1] + Values[Mid]) / 2;
	}

	static int64 AbsI64(int64 V) { return V < 0 ? -V : V; }

	constexpr int32 DegradingFlags = static_cast<int32>(EDocRhythmTimingQualityFlags::ClockHitchDetected)
		| static_cast<int32>(EDocRhythmTimingQualityFlags::LowPrecisionInput)
		| static_cast<int32>(EDocRhythmTimingQualityFlags::CalibrationUncertain)
		| static_cast<int32>(EDocRhythmTimingQualityFlags::AudioReanchored)
		| static_cast<int32>(EDocRhythmTimingQualityFlags::NonComparable);
}

UDocRhythmChallengeSubsystem* UDocRhythmChallengeSubsystem::Get(const ULocalPlayer* Player)
{
	return Player ? Player->GetSubsystem<UDocRhythmChallengeSubsystem>() : nullptr;
}

void UDocRhythmChallengeSubsystem::Deinitialize()
{
	CancelChallenge();
	Super::Deinitialize();
}

void UDocRhythmChallengeSubsystem::Tick(float DeltaTime)
{
	if (IsAttemptRunning() && CurrentState != EDocRhythmChallengeState::Paused)
	{
		UpdateTimeline(GetNowUs());
	}
}

bool UDocRhythmChallengeSubsystem::IsTickable() const
{
	return !IsTemplate() && (CurrentState == EDocRhythmChallengeState::Playing || CurrentState == EDocRhythmChallengeState::CountIn);
}

TStatId UDocRhythmChallengeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocRhythmChallengeSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------------------------
// Providers and clock
// ---------------------------------------------------------------------------------------------

void UDocRhythmChallengeSubsystem::SetClockProvider(UObject* InProvider)
{
	ClockProviderObj = (InProvider && Cast<IDocRhythmClockProvider>(InProvider)) ? InProvider : nullptr;
}

void UDocRhythmChallengeSubsystem::SetPlaybackProvider(UObject* InProvider)
{
	PlaybackProviderObj = (InProvider && Cast<IDocRhythmPlaybackProvider>(InProvider)) ? InProvider : nullptr;
}

IDocRhythmPlaybackProvider* UDocRhythmChallengeSubsystem::GetPlayback() const
{
	return PlaybackProviderObj ? Cast<IDocRhythmPlaybackProvider>(PlaybackProviderObj.Get()) : nullptr;
}

int64 UDocRhythmChallengeSubsystem::GetNowUs() const
{
	if (ClockProviderObj)
	{
		if (const IDocRhythmClockProvider* Clock = Cast<IDocRhythmClockProvider>(ClockProviderObj.Get()))
		{
			return Clock->GetCurrentTimeUs();
		}
	}
	return static_cast<int64>(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64()) * 1000000.0);
}

bool UDocRhythmChallengeSubsystem::IsAttemptRunning() const
{
	return CurrentState == EDocRhythmChallengeState::CountIn || CurrentState == EDocRhythmChallengeState::Playing || CurrentState == EDocRhythmChallengeState::Paused;
}

bool UDocRhythmChallengeSubsystem::MapToChartUs(int64 MonotonicUs, int64& OutChartUs) const
{
	OutChartUs = 0;
	if (MonotonicUs < AttemptStartUs)
	{
		return false; // belongs to an earlier attempt
	}
	int64 Paused = 0;
	for (const FPauseInterval& Interval : PauseIntervals)
	{
		if (MonotonicUs >= Interval.EndUs)
		{
			Paused += Interval.EndUs - Interval.StartUs;
		}
		else if (MonotonicUs >= Interval.StartUs)
		{
			return false; // happened while paused: not eligible
		}
	}
	if (CurrentState == EDocRhythmChallengeState::Paused && MonotonicUs >= PauseOpenStartUs)
	{
		return false;
	}
	OutChartUs = MonotonicUs - EpochUs - Paused;
	return true;
}

int64 UDocRhythmChallengeSubsystem::CurrentChartUs() const
{
	const int64 Now = CurrentState == EDocRhythmChallengeState::Paused ? PauseOpenStartUs : GetNowUs();
	int64 Paused = 0;
	for (const FPauseInterval& Interval : PauseIntervals)
	{
		Paused += Interval.EndUs - Interval.StartUs;
	}
	return Now - EpochUs - Paused;
}

void UDocRhythmChallengeSubsystem::SetState(EDocRhythmChallengeState NewState)
{
	if (CurrentState != NewState)
	{
		CurrentState = NewState;
		OnStateChanged.Broadcast(CurrentState);
		OnStateChangedNative.Broadcast(CurrentState);
	}
}

void UDocRhythmChallengeSubsystem::StopPlaybackIfAny()
{
	if (IDocRhythmPlaybackProvider* Playback = GetPlayback())
	{
		if (bPlaybackStarted)
		{
			Playback->StopPlayback();
		}
	}
	bPlaybackStarted = false;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocRhythmChallengeSubsystem::LoadChart(UDocRhythmChart* InChart, UDocRhythmJudgmentProfile* InProfile)
{
	if (!InChart)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Cannot load a null chart"));
	}
	if (IsAttemptRunning())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Cancel the running attempt before loading another chart"));
	}
	const FDocSystemResult ChartValid = InChart->ValidateChart();
	if (!ChartValid.IsSuccess())
	{
		return ChartValid;
	}
	UDocRhythmJudgmentProfile* Profile = InProfile ? InProfile : InChart->DefaultJudgmentProfile.Get();
	if (!Profile)
	{
		Profile = NewObject<UDocRhythmJudgmentProfile>(this);
	}
	const FDocSystemResult ProfileValid = Profile->ValidateProfile();
	if (!ProfileValid.IsSuccess())
	{
		return ProfileValid;
	}

	LoadedChart = InChart;
	ActiveProfile = Profile;
	ChartHash = InChart->ComputeContentHash();
	ProfileHash = Profile->ComputeProfileHash();
	ActiveNotes.Reset();
	for (const FDocRhythmNote& Note : InChart->GetSortedNotes())
	{
		FActiveNote Active;
		Active.Note = Note;
		ActiveNotes.Add(Active);
	}
	bHasFinalResult = false;
	FinalResult = FDocRhythmResult();
	SetState(EDocRhythmChallengeState::Ready);
	return FDocSystemResult::MakeSuccess();
}

void UDocRhythmChallengeSubsystem::StartAttempt()
{
	++CurrentAttemptId;
	++CurrentSessionGeneration;
	++TransportGeneration;
	AttemptTransportGenerations = 1;

	ActiveNotes.Reset();
	for (const FDocRhythmNote& Note : LoadedChart->GetSortedNotes())
	{
		FActiveNote Active;
		Active.Note = Note;
		ActiveNotes.Add(Active);
	}
	PauseIntervals.Reset();
	SeenInputIds.Reset();
	SeenInputKeys.Reset();
	CurrentCombo = 0;
	MaxCombo = 0;
	CurrentScore = 0;
	PerfectCount = GreatCount = GoodCount = MissCount = 0;
	TimingQualityFlags = ActiveProfile->bIsAssisted ? static_cast<int32>(EDocRhythmTimingQualityFlags::Assisted) : 0;
	AttemptCalibration = CalibrationSettings;
	bHasFinalResult = false;
	FinalResult = FDocRhythmResult();
	bAudibleThisAttempt = false;
	bPlaybackStarted = false;

	const int64 Now = GetNowUs();
	AttemptStartUs = Now;
	EpochUs = Now + LoadedChart->CountInUs;
	LastUpdateUs = Now;
	PauseOpenStartUs = 0;
}

FDocSystemResult UDocRhythmChallengeSubsystem::BeginChallenge()
{
	if (!LoadedChart || !ActiveProfile)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No chart loaded"));
	}
	if (IsAttemptRunning())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("An attempt is running; use RestartChallenge"));
	}
	StartAttempt();
	if (LoadedChart->CountInUs > 0)
	{
		SetState(EDocRhythmChallengeState::CountIn);
		return FDocSystemResult::MakeSuccess();
	}
	if (IDocRhythmPlaybackProvider* Playback = GetPlayback())
	{
		if (!Playback->StartPlayback(LoadedChart->AudioSourceIdentifier, 0))
		{
			SetState(EDocRhythmChallengeState::Ready);
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Backing track failed to start"));
		}
		bPlaybackStarted = true;
		bAudibleThisAttempt = Playback->IsAudible();
	}
	SetState(EDocRhythmChallengeState::Playing);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocRhythmChallengeSubsystem::RestartChallenge()
{
	if (!LoadedChart || !ActiveProfile)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No chart loaded to restart"));
	}
	StopPlaybackIfAny();
	// Leave the running states so BeginChallenge accepts the new attempt; the old one produces no result.
	CurrentState = EDocRhythmChallengeState::Ready;
	return BeginChallenge();
}

FDocSystemResult UDocRhythmChallengeSubsystem::CancelChallenge()
{
	if (!IsAttemptRunning())
	{
		return FDocSystemResult::MakeNoChange(TEXT("No attempt running"));
	}
	StopPlaybackIfAny();
	SetState(EDocRhythmChallengeState::Aborted);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocRhythmChallengeSubsystem::PauseChallenge()
{
	if (CurrentState != EDocRhythmChallengeState::Playing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Challenge is not playing"));
	}
	IDocRhythmPlaybackProvider* Playback = GetPlayback();
	if (Playback && bPlaybackStarted && !Playback->SupportsPauseResume())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Playback backend cannot pause with the required precision; restart instead"));
	}
	PauseOpenStartUs = FMath::Max(GetNowUs(), LastUpdateUs);
	if (Playback && bPlaybackStarted)
	{
		Playback->PausePlayback();
	}
	SetState(EDocRhythmChallengeState::Paused);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocRhythmChallengeSubsystem::ResumeChallenge()
{
	if (CurrentState != EDocRhythmChallengeState::Paused)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Challenge is not paused"));
	}
	const int64 Now = FMath::Max(GetNowUs(), PauseOpenStartUs);
	FPauseInterval Interval;
	Interval.StartUs = PauseOpenStartUs;
	Interval.EndUs = Now;
	PauseIntervals.Add(Interval);
	LastUpdateUs = Now;
	if (IDocRhythmPlaybackProvider* Playback = GetPlayback())
	{
		if (bPlaybackStarted)
		{
			Playback->ResumePlayback();
		}
	}
	SetState(EDocRhythmChallengeState::Playing);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Judgment
// ---------------------------------------------------------------------------------------------

void UDocRhythmChallengeSubsystem::Judge(FActiveNote& Active, EDocRhythmHitJudgment Judgment, int64 JudgmentTimeUs, int64 ErrorUs, bool bAutoMiss, FDocRhythmJudgmentResult& OutResult)
{
	OutResult = FDocRhythmJudgmentResult();
	if (Active.Judgment != EDocRhythmHitJudgment::None)
	{
		return; // one terminal judgment per note
	}
	const bool bWasHold = Active.Note.Kind == EDocRhythmNoteKind::Hold;
	Active.Judgment = Judgment;
	Active.ErrorUs = ErrorUs;
	Active.bHoldActive = false;

	const int32 Points = ActiveProfile->GetPointsForJudgment(Judgment);
	switch (Judgment)
	{
	case EDocRhythmHitJudgment::Perfect: ++PerfectCount; break;
	case EDocRhythmHitJudgment::Great:   ++GreatCount; break;
	case EDocRhythmHitJudgment::Good:    ++GoodCount; break;
	default:                             ++MissCount; break;
	}
	if (Judgment == EDocRhythmHitJudgment::Miss)
	{
		CurrentCombo = 0;
	}
	else
	{
		++CurrentCombo;
		MaxCombo = FMath::Max(MaxCombo, CurrentCombo);
	}
	CurrentScore += Points;

	OutResult.NoteId = Active.Note.NoteId;
	OutResult.Judgment = Judgment;
	OutResult.JudgmentTimeUs = JudgmentTimeUs;
	OutResult.ErrorUs = ErrorUs;
	OutResult.PointsEarned = Points;
	OutResult.CurrentCombo = CurrentCombo;
	OutResult.bHoldCompleted = bWasHold && Judgment != EDocRhythmHitJudgment::Miss;
	OutResult.bAutoMiss = bAutoMiss;
	OutResult.AttemptId = CurrentAttemptId;
	OnHitJudged.Broadcast(OutResult);
	OnHitJudgedNative.Broadcast(OutResult);
}

FDocSystemResult UDocRhythmChallengeSubsystem::SubmitInput(const FDocRhythmInputSample& Input, FDocRhythmJudgmentResult& OutJudgment)
{
	OutJudgment = FDocRhythmJudgmentResult();
	OutJudgment.AttemptId = CurrentAttemptId;
	if (CurrentState == EDocRhythmChallengeState::Paused)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Challenge is paused; inputs are not eligible"));
	}
	if (CurrentState != EDocRhythmChallengeState::Playing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Challenge is not playing"));
	}
	if (Input.AttemptId != 0 && Input.AttemptId != CurrentAttemptId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Input belongs to another attempt"));
	}
	if (Input.LaneId < 0 || Input.LaneId >= LoadedChart->LaneCount)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unknown lane"));
	}
	if (Input.InputEventId.IsValid() && SeenInputIds.Contains(Input.InputEventId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Duplicate input event ignored"));
	}

	const int64 Now = GetNowUs();
	int64 Timestamp = Input.MonotonicTimestampUs;
	bool bLowPrecision = Input.Source == EDocRhythmTimestampSource::DispatchEstimate || Input.Source == EDocRhythmTimestampSource::Unknown;
	if (Timestamp <= 0)
	{
		Timestamp = Now; // processing time: flagged, never presented as the real input time
		bLowPrecision = true;
	}
	if (Timestamp > Now)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Input timestamp is in the future"));
	}
	const FString Key = FString::Printf(TEXT("%d|%d|%lld"), Input.LaneId, static_cast<int32>(Input.InputType), Timestamp);
	if (SeenInputKeys.Contains(Key))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Duplicate input event ignored"));
	}
	int64 ChartUs = 0;
	if (!MapToChartUs(Timestamp, ChartUs))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Input timestamp is outside this attempt's live transport (before start or during a pause)"));
	}
	if (Input.InputEventId.IsValid())
	{
		SeenInputIds.Add(Input.InputEventId);
	}
	SeenInputKeys.Add(Key);
	if (bLowPrecision)
	{
		TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::LowPrecisionInput);
	}

	const int64 JudgmentTimeUs = ChartUs - AttemptCalibration.InputOffsetUs;

	if (Input.InputType == EDocRhythmInputType::Press)
	{
		// Rule: the earliest pending note whose error is a hit; otherwise the earliest pending note in its miss window.
		FActiveNote* Hit = nullptr;
		FActiveNote* MissCandidate = nullptr;
		EDocRhythmHitJudgment HitJudgment = EDocRhythmHitJudgment::None;
		for (FActiveNote& Candidate : ActiveNotes)
		{
			if (Candidate.Note.LaneId != Input.LaneId || Candidate.Judgment != EDocRhythmHitJudgment::None || Candidate.bHoldActive)
			{
				continue;
			}
			const EDocRhythmHitJudgment J = ActiveProfile->EvaluateError(JudgmentTimeUs - Candidate.Note.TimeOffsetUs);
			if (J == EDocRhythmHitJudgment::None)
			{
				continue;
			}
			if (J != EDocRhythmHitJudgment::Miss)
			{
				Hit = &Candidate;
				HitJudgment = J;
				break;
			}
			if (!MissCandidate)
			{
				MissCandidate = &Candidate;
			}
		}
		FActiveNote* Target = Hit ? Hit : MissCandidate;
		if (!Target)
		{
			return FDocSystemResult::MakeNoChange(TEXT("No note in timing window"));
		}
		const int64 ErrorUs = JudgmentTimeUs - Target->Note.TimeOffsetUs;
		const EDocRhythmHitJudgment Judgment = Hit ? HitJudgment : EDocRhythmHitJudgment::Miss;
		if (Target->Note.Kind == EDocRhythmNoteKind::Hold && Judgment != EDocRhythmHitJudgment::Miss)
		{
			Target->bHoldActive = true;
			Target->StartJudgment = Judgment;
			Target->ErrorUs = ErrorUs;
			OutJudgment.NoteId = Target->Note.NoteId;
			OutJudgment.Judgment = Judgment;
			OutJudgment.JudgmentTimeUs = JudgmentTimeUs;
			OutJudgment.ErrorUs = ErrorUs;
			OutJudgment.CurrentCombo = CurrentCombo;
			OutJudgment.bHoldStarted = true;
			OnHitJudged.Broadcast(OutJudgment);
			OnHitJudgedNative.Broadcast(OutJudgment);
			return FDocSystemResult::MakeSuccess();
		}
		Judge(*Target, Judgment, JudgmentTimeUs, ErrorUs, /*bAutoMiss*/ false, OutJudgment);
		return FDocSystemResult::MakeSuccess();
	}

	// Release: only an active hold in this lane cares; stray releases never create misses.
	for (FActiveNote& Candidate : ActiveNotes)
	{
		if (Candidate.Note.LaneId == Input.LaneId && Candidate.bHoldActive)
		{
			const int64 Required = Candidate.Note.TimeOffsetUs
				+ static_cast<int64>(FMath::RoundToDouble(static_cast<double>(Candidate.Note.DurationUs) * static_cast<double>(ActiveProfile->MinHoldPercent)));
			const bool bHeldLongEnough = JudgmentTimeUs >= Required;
			Judge(Candidate, bHeldLongEnough ? Candidate.StartJudgment : EDocRhythmHitJudgment::Miss, JudgmentTimeUs, Candidate.ErrorUs, false, OutJudgment);
			return FDocSystemResult::MakeSuccess();
		}
	}
	return FDocSystemResult::MakeNoChange(TEXT("No active hold in lane"));
}

void UDocRhythmChallengeSubsystem::UpdateTimeline(int64 CurrentMonotonicUs)
{
	if (!LoadedChart || !ActiveProfile)
	{
		return;
	}
	if (CurrentState == EDocRhythmChallengeState::CountIn)
	{
		LastUpdateUs = FMath::Max(LastUpdateUs, CurrentMonotonicUs);
		if (CurrentMonotonicUs < EpochUs)
		{
			return;
		}
		if (IDocRhythmPlaybackProvider* Playback = GetPlayback())
		{
			if (!Playback->StartPlayback(LoadedChart->AudioSourceIdentifier, 0))
			{
				TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::NonComparable);
				SetState(EDocRhythmChallengeState::TimingFault);
				return;
			}
			bPlaybackStarted = true;
			bAudibleThisAttempt = Playback->IsAudible();
		}
		SetState(EDocRhythmChallengeState::Playing);
	}
	if (CurrentState != EDocRhythmChallengeState::Playing)
	{
		return;
	}

	if (CurrentMonotonicUs - LastUpdateUs > ActiveProfile->HitchThresholdUs)
	{
		TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::ClockHitchDetected);
	}
	LastUpdateUs = FMath::Max(LastUpdateUs, CurrentMonotonicUs);

	int64 Paused = 0;
	for (const FPauseInterval& Interval : PauseIntervals)
	{
		Paused += Interval.EndUs - Interval.StartUs;
	}
	int64 ChartUs = CurrentMonotonicUs - EpochUs - Paused;

	if (IDocRhythmPlaybackProvider* Playback = GetPlayback())
	{
		if (bPlaybackStarted)
		{
			if (!Playback->IsPlaybackActive())
			{
				// Audio device/transport loss: the attempt can no longer be judged against the music.
				TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::NonComparable);
				StopPlaybackIfAny();
				SetState(EDocRhythmChallengeState::TimingFault);
				return;
			}
			const int64 Position = Playback->GetPlaybackPositionUs();
			if (Position >= 0)
			{
				const int64 Drift = Position - ChartUs;
				if (DocRhythmPrivate::AbsI64(Drift) > ActiveProfile->MaxAudioDriftUs)
				{
					EpochUs -= Drift;
					ChartUs += Drift;
					++TransportGeneration;
					++AttemptTransportGenerations;
					TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::AudioReanchored);
				}
			}
		}
	}

	const int64 JudgmentNowUs = ChartUs - AttemptCalibration.InputOffsetUs;
	const int64 LateMiss = ActiveProfile->GetLateMissWindowUs();
	FDocRhythmJudgmentResult Scratch;
	for (FActiveNote& Active : ActiveNotes)
	{
		if (Active.bHoldActive && JudgmentNowUs >= Active.Note.TimeOffsetUs + Active.Note.DurationUs)
		{
			Judge(Active, Active.StartJudgment, JudgmentNowUs, Active.ErrorUs, false, Scratch);
		}
		else if (Active.Judgment == EDocRhythmHitJudgment::None && !Active.bHoldActive && JudgmentNowUs > Active.Note.TimeOffsetUs + LateMiss)
		{
			Judge(Active, EDocRhythmHitJudgment::Miss, JudgmentNowUs, LateMiss + 1, /*bAutoMiss*/ true, Scratch);
		}
	}

	bool bAllJudged = true;
	for (const FActiveNote& Active : ActiveNotes)
	{
		if (Active.Judgment == EDocRhythmHitJudgment::None)
		{
			bAllJudged = false;
			break;
		}
	}
	if (bAllJudged && ChartUs >= LoadedChart->DurationUs)
	{
		CompleteChallenge();
	}
}

void UDocRhythmChallengeSubsystem::CompleteChallenge()
{
	StopPlaybackIfAny();
	SetState(EDocRhythmChallengeState::Completed);
	BuildResult(FinalResult);
	bHasFinalResult = true;
	OnChallengeCompleted.Broadcast(FinalResult);
	OnChallengeCompletedNative.Broadcast(FinalResult);
}

void UDocRhythmChallengeSubsystem::OnFocusLost()
{
	if (CurrentState != EDocRhythmChallengeState::Playing || !ActiveProfile)
	{
		return;
	}
	if (ActiveProfile->FocusLossPolicy == EDocRhythmFocusLossPolicy::PauseChallenge && PauseChallenge().IsSuccess())
	{
		return;
	}
	const int64 JudgmentNowUs = CurrentChartUs() - AttemptCalibration.InputOffsetUs;
	FDocRhythmJudgmentResult Scratch;
	for (FActiveNote& Active : ActiveNotes)
	{
		if (Active.bHoldActive)
		{
			Judge(Active, EDocRhythmHitJudgment::Miss, JudgmentNowUs, Active.ErrorUs, false, Scratch);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Queries and results
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocRhythmChallengeSubsystem::QueryTimeline(FDocRhythmTimelineInfo& OutInfo) const
{
	OutInfo = FDocRhythmTimelineInfo();
	OutInfo.State = CurrentState;
	OutInfo.CurrentCombo = CurrentCombo;
	OutInfo.CurrentScore = CurrentScore;
	OutInfo.AttemptId = CurrentAttemptId;
	OutInfo.SessionGeneration = CurrentSessionGeneration;
	OutInfo.TransportGeneration = TransportGeneration;
	if (IsAttemptRunning())
	{
		OutInfo.CurrentChartTimeUs = CurrentChartUs();
		OutInfo.VisualChartTimeUs = OutInfo.CurrentChartTimeUs + CalibrationSettings.VisualOffsetUs;
	}
	for (const FActiveNote& Active : ActiveNotes)
	{
		OutInfo.RemainingNotes += Active.Judgment == EDocRhythmHitJudgment::None ? 1 : 0;
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocRhythmChallengeSubsystem::BuildResult(FDocRhythmResult& Out) const
{
	Out = FDocRhythmResult();
	Out.ChartId = LoadedChart->ChartId;
	Out.ContentHash = ChartHash;
	Out.DeclaredChartVersion = LoadedChart->ContentHash;
	Out.ProfileId = ActiveProfile->ProfileId;
	Out.ProfileHash = ProfileHash;
	Out.TotalScore = CurrentScore;
	Out.MaxPossibleScore = LoadedChart->CalculateMaxPossibleScore(ActiveProfile.Get());
	Out.PerfectCount = PerfectCount;
	Out.GreatCount = GreatCount;
	Out.GoodCount = GoodCount;
	Out.MissCount = MissCount;
	Out.MaxCombo = MaxCombo;
	Out.FinalCombo = CurrentCombo;
	Out.TotalNotes = ActiveNotes.Num();
	Out.AccuracyPercent = Out.MaxPossibleScore > 0 ? static_cast<float>(static_cast<double>(CurrentScore) * 100.0 / static_cast<double>(Out.MaxPossibleScore)) : 0.0f;
	Out.bCompleted = CurrentState == EDocRhythmChallengeState::Completed;
	Out.bHasAssistance = ActiveProfile->bIsAssisted;
	Out.TimingQualityFlags = TimingQualityFlags;
	Out.CalibrationUsed = AttemptCalibration;
	Out.bAudibleReference = bAudibleThisAttempt;
	Out.TransportGenerations = AttemptTransportGenerations;
	Out.AttemptId = CurrentAttemptId;
	if (Out.bHasAssistance)
	{
		Out.Category = EDocRhythmResultCategory::Assisted;
	}
	else if ((TimingQualityFlags & DocRhythmPrivate::DegradingFlags) != 0)
	{
		Out.Category = EDocRhythmResultCategory::Degraded;
	}
	else
	{
		Out.Category = EDocRhythmResultCategory::Strict;
	}
}

FDocSystemResult UDocRhythmChallengeSubsystem::GetResult(FDocRhythmResult& OutResult) const
{
	OutResult = FDocRhythmResult();
	if (!LoadedChart || !ActiveProfile)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No chart loaded"));
	}
	if (bHasFinalResult)
	{
		OutResult = FinalResult;
	}
	else
	{
		BuildResult(OutResult);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocRhythmChallengeSubsystem::RunCalibration(const TArray<int64>& ObservedErrorsUs, FName DeviceScopeId, FDocRhythmCalibrationSettings& OutCalibration)
{
	OutCalibration = CalibrationSettings;
	if (IsAttemptRunning())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Cannot calibrate during an attempt"));
	}
	if (ObservedErrorsUs.Num() < MinCalibrationSamples)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady,
			FString::Printf(TEXT("CalibrationInsufficient: %d samples, need %d"), ObservedErrorsUs.Num(), MinCalibrationSamples));
	}
	using namespace DocRhythmPrivate;
	const int64 FirstMedian = Median(ObservedErrorsUs);
	TArray<int64> Deviations;
	for (int64 V : ObservedErrorsUs)
	{
		Deviations.Add(AbsI64(V - FirstMedian));
	}
	const double Mad = static_cast<double>(Median(Deviations));
	const double Threshold = FMath::Max(3.0 * 1.4826 * Mad, 5000.0);
	TArray<int64> Inliers;
	for (int64 V : ObservedErrorsUs)
	{
		if (static_cast<double>(AbsI64(V - FirstMedian)) <= Threshold)
		{
			Inliers.Add(V);
		}
	}
	if (Inliers.Num() < MinCalibrationSamples)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady,
			FString::Printf(TEXT("CalibrationInsufficient: only %d consistent samples"), Inliers.Num()));
	}
	const int64 Estimate = Median(Inliers);
	Deviations.Reset();
	for (int64 V : Inliers)
	{
		Deviations.Add(AbsI64(V - Estimate));
	}
	const int64 Uncertainty = static_cast<int64>(FMath::RoundToDouble(1.4826 * static_cast<double>(Median(Deviations))));
	if (Uncertainty > MaxCalibrationUncertaintyUs)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady,
			FString::Printf(TEXT("CalibrationInsufficient: uncertainty %lld us exceeds %lld us"), Uncertainty, MaxCalibrationUncertaintyUs));
	}
	FDocRhythmCalibrationSettings Result = CalibrationSettings;
	Result.InputOffsetUs = Estimate; // positive = inputs observed late; subtracted during judgment
	Result.bCalibrated = true;
	Result.DeviceScopeId = DeviceScopeId;
	Result.UncertaintyUs = Uncertainty;
	Result.SampleCount = Inliers.Num();
	Result.RejectedSampleCount = ObservedErrorsUs.Num() - Inliers.Num();
	CalibrationSettings = Result;
	OutCalibration = Result;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocRhythmChallengeSubsystem::SetCalibrationSettings(const FDocRhythmCalibrationSettings& InSettings)
{
	if (IsAttemptRunning())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Cannot change input calibration during an attempt"));
	}
	CalibrationSettings = InSettings;
	return FDocSystemResult::MakeSuccess();
}

void UDocRhythmChallengeSubsystem::SetVisualOffsetUs(int64 VisualOffsetUs)
{
	CalibrationSettings.VisualOffsetUs = VisualOffsetUs;
}

void UDocRhythmChallengeSubsystem::NotifyInputDeviceChanged(FName DeviceScopeId)
{
	if (CalibrationSettings.bCalibrated && CalibrationSettings.DeviceScopeId != DeviceScopeId)
	{
		CalibrationSettings.bCalibrated = false;
		CalibrationSettings.InputOffsetUs = 0;
		CalibrationSettings.UncertaintyUs = 0;
		CalibrationSettings.SampleCount = 0;
		CalibrationSettings.DeviceScopeId = DeviceScopeId;
		if (IsAttemptRunning())
		{
			TimingQualityFlags |= static_cast<int32>(EDocRhythmTimingQualityFlags::CalibrationUncertain)
				| static_cast<int32>(EDocRhythmTimingQualityFlags::NonComparable);
		}
		UE_LOG(LogDocRhythm, Log, TEXT("Input device changed; calibration invalidated"));
	}
}
