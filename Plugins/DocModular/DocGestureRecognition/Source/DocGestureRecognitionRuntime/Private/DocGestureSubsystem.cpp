#include "DocGestureSubsystem.h"
#include "DocGestureRecognitionLog.h"
#include "Misc/SecureHash.h"

namespace DocGesturePrivate
{
	constexpr int32 MaxRecentSessions = 16;
	constexpr int32 MaxRememberedDispatches = 64;
	constexpr int32 MaxCompiledCache = 256;
}

void UDocGestureSubsystem::Deinitialize()
{
	FinishStroke(/*bCancelled*/ true);
	ActiveTemplateSet = nullptr;
	ActiveModel.Reset();
	RecentSessions.Reset();
	CompiledCache.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocGestureSubsystem::RegisterTemplateSet(UDocGestureTemplateSet* InTemplateSet)
{
	if (!InTemplateSet)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No template set"));
	}
	const FDocSystemResult Valid = InTemplateSet->ValidateSet();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}

	TSharedPtr<FModel> Model = MakeShared<FModel>();
	Model->SetId = InTemplateSet->SetId;
	Model->SetVersion = InTemplateSet->Version;
	FString Signature = FString::Printf(TEXT("%s|%d|A%d"), *InTemplateSet->SetId.ToString(), InTemplateSet->Version, FDocGestureRecognizer::AlgorithmVersion);
	for (const TObjectPtr<UDocGestureTemplate>& Template : InTemplateSet->Templates)
	{
		const FString Hash = Template->ComputeContentHash();
		const FDocGestureCompiledTemplate* Cached = CompiledCache.Find(Hash);
		FDocGestureCompiledTemplate Compiled;
		if (Cached)
		{
			Compiled = *Cached;
		}
		else
		{
			if (!FDocGestureRecognizer::Compile(*Template, Compiled))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Template %s could not be normalized"), *Template->GestureId.ToString()));
			}
			if (CompiledCache.Num() >= DocGesturePrivate::MaxCompiledCache)
			{
				CompiledCache.Reset();
			}
			CompiledCache.Add(Hash, Compiled);
		}
		Signature += TEXT("|") + Hash;
		Model->Templates.Add(MoveTemp(Compiled));
	}
	Model->ModelHash = FMD5::HashAnsiString(*Signature);

	if (ActiveModel.IsValid() && ActiveModel->ModelHash == Model->ModelHash)
	{
		ActiveTemplateSet = InTemplateSet;
		return FDocSystemResult::MakeNoChange(TEXT("Identical model already registered"));
	}
	ActiveModel = Model;
	ActiveTemplateSet = InTemplateSet;
	return FDocSystemResult::MakeSuccess();
}

FString UDocGestureSubsystem::GetActiveModelHash() const
{
	return ActiveModel.IsValid() ? ActiveModel->ModelHash : FString();
}

TSharedPtr<const UDocGestureSubsystem::FModel> UDocGestureSubsystem::FindModelForSession(const FGuid& SessionId) const
{
	if (SessionId.IsValid())
	{
		for (const TPair<FGuid, TSharedPtr<const FModel>>& Entry : RecentSessions)
		{
			if (Entry.Key == SessionId)
			{
				return Entry.Value;
			}
		}
	}
	return ActiveModel;
}

void UDocGestureSubsystem::FillModelFields(const FModel& Model, FDocGestureRecognitionResult& Result) const
{
	Result.TemplateSetId = Model.SetId;
	Result.TemplateVersion = Model.SetVersion;
	Result.ModelHash = Model.ModelHash;
	Result.AlgorithmVersion = FDocGestureRecognizer::AlgorithmVersion;
}

// ---------------------------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocGestureSubsystem::SetCaptureSettings(const FDocGestureCaptureSettings& InSettings)
{
	if (InSettings.SurfaceSize.ContainsNaN() || InSettings.SurfaceSize.X < 0.0 || InSettings.SurfaceSize.Y < 0.0
		|| !FMath::IsFinite(InSettings.AnalogSpeedUnitsPerSecond) || InSettings.AnalogSpeedUnitsPerSecond <= 0.0f
		|| !FMath::IsFinite(InSettings.AnalogDeadZone) || InSettings.AnalogDeadZone < 0.0f || InSettings.AnalogDeadZone >= 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid capture settings"));
	}
	CaptureSettings = InSettings;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocGestureSubsystem::BeginStroke(const FDocOwnerScope& InOwner, EDocGestureInputSource InputSource)
{
	if (!InOwner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Stroke needs a valid owner"));
	}
	if (InputSource == EDocGestureInputSource::AccessibleAlternative)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Use SelectAccessibleAlternative for non-drawing input"));
	}
	if (!ActiveModel.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No template set registered"));
	}
	++CurrentGeneration;
	bHasEndedStroke = false;
	ActiveStroke = FDocGestureStroke();
	ActiveStroke.Owner = InOwner;
	ActiveStroke.SessionId = FGuid::NewGuid();
	ActiveStroke.Generation = CurrentGeneration;
	ActiveStroke.InputSource = InputSource;
	ActiveStroke.bCoarseSampling = InputSource == EDocGestureInputSource::AnalogStick;
	ActiveStrokeModel = ActiveModel;
	bStrokeActive = true;
	AnalogCursor = CaptureSettings.SurfaceSize * 0.5;

	RecentSessions.Add(TPair<FGuid, TSharedPtr<const FModel>>(ActiveStroke.SessionId, ActiveModel));
	if (RecentSessions.Num() > DocGesturePrivate::MaxRecentSessions)
	{
		RecentSessions.RemoveAt(0);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocGestureSubsystem::FinishStroke(bool bCancelled)
{
	if (!bStrokeActive)
	{
		return;
	}
	ActiveStroke.bIsComplete = true;
	ActiveStroke.bIsCancelled = bCancelled;
	if (!bCancelled)
	{
		EndedStroke = ActiveStroke;
		bHasEndedStroke = true;
	}
	ActiveStroke = FDocGestureStroke();
	ActiveStrokeModel.Reset();
	bStrokeActive = false;
}

FDocSystemResult UDocGestureSubsystem::AppendPoints(const TArray<FVector2D>& NewPoints, float DeltaSeconds)
{
	if (!bStrokeActive)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No stroke in capture"));
	}
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Delta time must be finite and >= 0"));
	}
	for (const FVector2D& P : NewPoints)
	{
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Non-finite point refused"));
		}
	}
	if (ActiveStroke.Duration + DeltaSeconds > MaxStrokeDurationSeconds)
	{
		FinishStroke(/*bCancelled*/ true);
		++CurrentGeneration;
		return FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Stroke exceeded the duration limit and was cancelled"));
	}
	if (ActiveStroke.Points.Num() + NewPoints.Num() > GetEffectiveMaxPoints())
	{
		FinishStroke(/*bCancelled*/ true);
		++CurrentGeneration;
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Stroke exceeded the point limit and was cancelled"));
	}
	ActiveStroke.Duration += DeltaSeconds;

	const bool bBounded = CaptureSettings.SurfaceSize.X > 0.0 && CaptureSettings.SurfaceSize.Y > 0.0;
	for (const FVector2D& P : NewPoints)
	{
		const bool bOutside = bBounded && (P.X < 0.0 || P.Y < 0.0 || P.X > CaptureSettings.SurfaceSize.X || P.Y > CaptureSettings.SurfaceSize.Y);
		if (!bOutside)
		{
			ActiveStroke.Points.Add(P);
			continue;
		}
		const FVector2D Clamped(FMath::Clamp(P.X, 0.0, CaptureSettings.SurfaceSize.X), FMath::Clamp(P.Y, 0.0, CaptureSettings.SurfaceSize.Y));
		switch (CaptureSettings.LeavePolicy)
		{
		case EDocGesturePointerLeavePolicy::Clamp:
			ActiveStroke.Points.Add(Clamped);
			break;
		case EDocGesturePointerLeavePolicy::EndStroke:
			ActiveStroke.Points.Add(Clamped);
			FinishStroke(/*bCancelled*/ false);
			return FDocSystemResult::MakeNoChange(TEXT("Pointer left the surface; stroke ended"));
		case EDocGesturePointerLeavePolicy::Cancel:
		default:
			FinishStroke(/*bCancelled*/ true);
			++CurrentGeneration;
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Pointer left the surface; stroke cancelled"));
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocGestureSubsystem::AppendAnalogSample(FVector2D StickDeflection, float DeltaSeconds)
{
	if (!bStrokeActive || ActiveStroke.InputSource != EDocGestureInputSource::AnalogStick)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No analog-stick stroke in capture"));
	}
	if (StickDeflection.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid analog sample"));
	}
	if (ActiveStroke.Points.IsEmpty())
	{
		ActiveStroke.Points.Add(AnalogCursor);
	}
	FVector2D Stick = StickDeflection;
	const double Magnitude = Stick.Size();
	if (Magnitude > 1.0)
	{
		Stick /= Magnitude;
	}
	if (Magnitude < CaptureSettings.AnalogDeadZone)
	{
		// Time still passes (and counts toward the duration limit), but the cursor does not move.
		return AppendPoints({}, DeltaSeconds);
	}
	AnalogCursor += Stick * static_cast<double>(CaptureSettings.AnalogSpeedUnitsPerSecond) * static_cast<double>(DeltaSeconds);
	return AppendPoints({ AnalogCursor }, DeltaSeconds);
}

FDocSystemResult UDocGestureSubsystem::EndStroke(FDocGestureStroke& OutStroke)
{
	OutStroke = FDocGestureStroke();
	if (bStrokeActive)
	{
		FinishStroke(/*bCancelled*/ false);
	}
	if (!bHasEndedStroke)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No stroke to end"));
	}
	OutStroke = EndedStroke;
	bHasEndedStroke = false;
	EndedStroke = FDocGestureStroke();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocGestureSubsystem::CancelStroke()
{
	const bool bWasActive = bStrokeActive || bHasEndedStroke;
	FinishStroke(/*bCancelled*/ true);
	bHasEndedStroke = false;
	EndedStroke = FDocGestureStroke();
	++CurrentGeneration;
	return bWasActive ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("No stroke to cancel"));
}

void UDocGestureSubsystem::NotifyInputDeviceChanged()
{
	if (bStrokeActive)
	{
		CancelStroke();
	}
}

void UDocGestureSubsystem::InvalidateCurrentSession()
{
	CancelStroke();
}

// ---------------------------------------------------------------------------------------------
// Recognition
// ---------------------------------------------------------------------------------------------

TArray<FDocGestureCandidate> UDocGestureSubsystem::ScoreAll(const FModel& Model, const FDocGestureStroke& Stroke, int32& OutWork) const
{
	TArray<FDocGestureCandidate> Candidates;
	OutWork = 0;
	for (const FDocGestureCompiledTemplate& Template : Model.Templates)
	{
		FDocGestureCandidate Candidate;
		if (FDocGestureRecognizer::Score(Stroke.Points, Template, Candidate, OutWork))
		{
			Candidates.Add(Candidate);
		}
	}
	Candidates.Sort([](const FDocGestureCandidate& A, const FDocGestureCandidate& B)
	{
		if (A.Similarity != B.Similarity)
		{
			return A.Similarity > B.Similarity;
		}
		return A.GestureId.LexicalLess(B.GestureId); // stable, deterministic tie-break
	});
	return Candidates;
}

TArray<FDocGestureCandidate> UDocGestureSubsystem::QueryCandidates(const FDocGestureStroke& Stroke)
{
	const TSharedPtr<const FModel> Model = FindModelForSession(Stroke.SessionId);
	if (!Model.IsValid() || Stroke.Points.Num() > GetEffectiveMaxPoints() || Stroke.IsDegenerate(0.0f))
	{
		return {};
	}
	int32 Work = 0;
	return ScoreAll(*Model, Stroke, Work);
}

FDocGestureRecognitionResult UDocGestureSubsystem::RecognizeStroke(const FDocGestureStroke& Stroke)
{
	FDocGestureRecognitionResult Result;
	Result.RequestId = FGuid::NewGuid();
	Result.Owner = Stroke.Owner;
	Result.RequestGeneration = Stroke.Generation;
	Result.InputSource = Stroke.InputSource;
	Result.AlgorithmVersion = FDocGestureRecognizer::AlgorithmVersion;

	if (Stroke.bIsCancelled)
	{
		Result.Status = EDocGestureRecognitionStatus::Cancelled;
		Result.FailureReason = TEXT("Stroke was cancelled");
		return Result;
	}
	if (Stroke.Generation != CurrentGeneration)
	{
		Result.Status = EDocGestureRecognitionStatus::Cancelled;
		Result.FailureReason = TEXT("Stale stroke: a newer gesture has started");
		return Result;
	}
	if (!Stroke.Owner.IsValid())
	{
		Result.Status = EDocGestureRecognitionStatus::InvalidStroke;
		Result.FailureReason = TEXT("Stroke has no owner");
		return Result;
	}
	const TSharedPtr<const FModel> Model = FindModelForSession(Stroke.SessionId);
	if (!Model.IsValid())
	{
		Result.Status = EDocGestureRecognitionStatus::NotRecognized;
		Result.FailureReason = TEXT("No template set registered");
		return Result;
	}
	FillModelFields(*Model, Result);
	if (Stroke.Points.Num() > GetEffectiveMaxPoints() || !FMath::IsFinite(Stroke.Duration) || Stroke.Duration < 0.0f || Stroke.Duration > MaxStrokeDurationSeconds)
	{
		Result.Status = EDocGestureRecognitionStatus::InvalidStroke;
		Result.FailureReason = TEXT("Stroke exceeds point/duration bounds");
		return Result;
	}
	if (Stroke.IsDegenerate(MinStrokePathLength))
	{
		Result.Status = EDocGestureRecognitionStatus::InvalidStroke;
		Result.FailureReason = TEXT("Stroke is degenerate, non-finite or too short");
		return Result;
	}

	Result.Candidates = ScoreAll(*Model, Stroke, Result.WorkUnits);
	if (Result.Candidates.IsEmpty())
	{
		Result.Status = EDocGestureRecognitionStatus::NotRecognized;
		Result.FailureReason = TEXT("Stroke could not be normalized");
		return Result;
	}
	const FDocGestureCandidate& Top = Result.Candidates[0];
	const FDocGestureCompiledTemplate* TopTemplate = Model->Find(Top.GestureId);
	Result.RunnerUpMargin = Result.Candidates.Num() > 1 ? Top.Similarity - Result.Candidates[1].Similarity : Top.Similarity;
	if (!TopTemplate || Top.Similarity < TopTemplate->MinSimilarityThreshold)
	{
		Result.Status = EDocGestureRecognitionStatus::NotRecognized;
		Result.FailureReason = TEXT("Best similarity below the acceptance threshold");
		return Result;
	}
	if (Result.Candidates.Num() > 1 && Result.RunnerUpMargin < TopTemplate->MinRunnerUpMargin)
	{
		Result.Status = EDocGestureRecognitionStatus::Ambiguous;
		Result.FailureReason = TEXT("Runner-up too close");
		return Result;
	}
	Result.Status = EDocGestureRecognitionStatus::Recognized;
	Result.MatchedGestureId = Top.GestureId;
	Result.Similarity = Top.Similarity;
	return Result;
}

FDocGestureRecognitionResult UDocGestureSubsystem::SelectAccessibleAlternative(FName GestureId, const FDocOwnerScope& InOwner)
{
	FDocGestureRecognitionResult Result;
	Result.RequestId = FGuid::NewGuid();
	Result.Owner = InOwner;
	Result.InputSource = EDocGestureInputSource::AccessibleAlternative;
	Result.AlgorithmVersion = FDocGestureRecognizer::AlgorithmVersion;
	Result.RequestGeneration = CurrentGeneration;
	if (!InOwner.IsValid())
	{
		Result.Status = EDocGestureRecognitionStatus::InvalidStroke;
		Result.FailureReason = TEXT("Alternative input needs a valid owner");
		return Result;
	}
	if (!ActiveModel.IsValid())
	{
		Result.Status = EDocGestureRecognitionStatus::NotRecognized;
		Result.FailureReason = TEXT("No template set registered");
		return Result;
	}
	FillModelFields(*ActiveModel, Result);
	const FDocGestureCompiledTemplate* Template = ActiveModel->Find(GestureId);
	if (!Template || !Template->bAllowAccessibleAlternative)
	{
		Result.Status = EDocGestureRecognitionStatus::NotRecognized;
		Result.FailureReason = TEXT("Gesture is not offered through the alternative in the active set");
		return Result;
	}
	// Choosing the alternative starts a new gesture, superseding any drawing in progress.
	FinishStroke(/*bCancelled*/ true);
	bHasEndedStroke = false;
	++CurrentGeneration;
	Result.RequestGeneration = CurrentGeneration;
	Result.Status = EDocGestureRecognitionStatus::Recognized;
	Result.MatchedGestureId = GestureId;
	Result.Similarity = 1.0f;
	Result.RunnerUpMargin = 1.0f;
	FDocGestureCandidate Candidate;
	Candidate.GestureId = GestureId;
	Candidate.DisplayName = Template->DisplayName;
	Candidate.Similarity = 1.0f;
	Result.Candidates.Add(Candidate);
	return Result;
}

FDocSystemResult UDocGestureSubsystem::DispatchResult(const FDocGestureRecognitionResult& Result)
{
	if (Result.Status != EDocGestureRecognitionStatus::Recognized || Result.MatchedGestureId.IsNone() || !Result.Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Only recognized, owned results can be dispatched"));
	}
	if (DispatchedRequests.Contains(Result.RequestId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Result already dispatched"));
	}
	if (Result.RequestGeneration != CurrentGeneration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Stale result: a newer gesture superseded it"));
	}
	if (!ActiveModel.IsValid() || Result.ModelHash != ActiveModel->ModelHash)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Template set changed since this result was produced"));
	}
	DispatchedRequests.Add(Result.RequestId);
	if (DispatchedRequests.Num() > DocGesturePrivate::MaxRememberedDispatches)
	{
		DispatchedRequests.RemoveAt(0);
	}
	// The gesture is consumed: any other result of this generation is now stale.
	++CurrentGeneration;
	OnGestureDispatched.Broadcast(Result);
	return FDocSystemResult::MakeSuccess();
}
