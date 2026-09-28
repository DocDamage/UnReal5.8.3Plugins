#include "DocReplayGhostSubsystem.h"
#include "Engine/World.h"

namespace DocGhostSubsystemPrivate
{
	static bool IsFiniteTransform(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FVector S = T.GetScale3D();
		const FQuat Q = T.GetRotation();
		return !L.ContainsNaN() && !S.ContainsNaN() && FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) && FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W)
			&& Q.SizeSquared() > 0.25;
	}
}

void UDocReplayGhostSubsystem::Deinitialize()
{
	for (TPair<FGuid, FActivePlayback>& Kvp : ActivePlaybacks)
	{
		DestroySurrogates(Kvp.Value);
	}
	ActivePlaybacks.Empty();
	ActiveRecordings.Empty();
	OpenRequests.Empty();
	Super::Deinitialize();
}

FDocSystemResult UDocReplayGhostSubsystem::RegisterGhostVisual(FName VisualId, TSubclassOf<ADocGhostSurrogateActor> SurrogateClass)
{
	if (VisualId.IsNone() || !SurrogateClass)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Visual id and surrogate class required"));
	}
	VisualCatalog.Add(VisualId, SurrogateClass);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocReplayGhostSubsystem::StartRecording(const FString& RecordingName, UDocGhostProfile* Profile, const TArray<FDocGhostTrackInfo>& Tracks, FGuid& OutRecordingId)
{
	OutRecordingId.Invalidate();
	const UDocGhostProfile* Settings = Profile ? Profile : GetDefault<UDocGhostProfile>();
	const FDocSystemResult Valid = Settings->ValidateProfile();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	if (Tracks.IsEmpty() || Tracks.Num() > Settings->MaxTracks)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Need 1..%d tracks"), Settings->MaxTracks));
	}
	TSet<FName> Ids;
	for (const FDocGhostTrackInfo& Track : Tracks)
	{
		bool bDup = false;
		Ids.Add(Track.TrackId, &bDup);
		if (Track.TrackId.IsNone() || Track.VisualId.IsNone() || bDup)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Tracks need unique ids and a visual id"));
		}
	}
	FActiveRecording Rec;
	Rec.Header.RecordingId = FGuid::NewGuid();
	Rec.Header.RecordingName = RecordingName.Left(MaxAnnotationChars);
	Rec.Header.SampleCadenceSeconds = Settings->SampleCadenceSeconds;
	Rec.Header.GapThresholdSeconds = Settings->GetEffectiveGapThreshold();
	Rec.Header.GapPolicy = Settings->GapPolicy;
	Rec.Header.ChunkDurationSeconds = Settings->ChunkDurationSeconds;
	Rec.Header.bHasVisualFallback = Settings->bAllowVisualFallback;
	Rec.Header.Tracks = Tracks;
	if (const UWorld* World = GetWorld())
	{
		Rec.Header.WorldNamespace = World->GetFName();
		Rec.StartWorldTime = World->GetTimeSeconds();
	}
	Rec.MaxDuration = Settings->MaxDurationSeconds;
	Rec.MaxSamplesPerTrack = Settings->MaxSamplesPerTrack;
	Rec.PositionThreshold = Settings->PositionChangeThreshold;
	Rec.RotationThresholdDegrees = Settings->RotationChangeThresholdDegrees;
	OutRecordingId = Rec.Header.RecordingId;
	ActiveRecordings.Add(OutRecordingId, MoveTemp(Rec));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::RecordSampleAt(const FGuid& RecordingId, FName TrackId, double TimeSeconds, const FTransform& InTransform, FName ActionToken, bool bIsDiscontinuity)
{
	FActiveRecording* Rec = ActiveRecordings.Find(RecordingId);
	if (!Rec)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown recording"));
	}
	if (!Rec->Header.Tracks.ContainsByPredicate([TrackId](const FDocGhostTrackInfo& T) { return T.TrackId == TrackId; }))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Track not declared for this recording"));
	}
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < 0.0 || !DocGhostSubsystemPrivate::IsFiniteTransform(InTransform))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Sample time/transform must be finite"));
	}
	if (TimeSeconds > Rec->MaxDuration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Recording reached its maximum duration"));
	}
	TArray<FDocGhostSample>& Samples = Rec->Samples.FindOrAdd(TrackId);
	if (Samples.Num() >= Rec->MaxSamplesPerTrack)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Track sample buffer is full"));
	}
	if (Samples.Num() > 0)
	{
		const FDocGhostSample& Last = Samples.Last();
		const double Delta = TimeSeconds - Last.Timestamp;
		if (Delta <= 0.0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Sample times must increase"));
		}
		if (!bIsDiscontinuity && Delta + 1.0e-6 < Rec->Header.SampleCadenceSeconds) // cadence is authored as float; allow float rounding
		{
			return FDocSystemResult::MakeNoChange(TEXT("Faster than the declared cadence"));
		}
		const bool bMoved = FVector::Dist(Last.Transform.GetLocation(), InTransform.GetLocation()) > Rec->PositionThreshold;
		const bool bTurned = FMath::RadiansToDegrees(Last.Transform.GetRotation().AngularDistance(InTransform.GetRotation())) > Rec->RotationThresholdDegrees;
		if (!bIsDiscontinuity && ActionToken.IsNone() && !bMoved && !bTurned && Delta < 0.5 * Rec->Header.GapThresholdSeconds)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Unchanged within thresholds"));
		}
	}
	FDocGhostSample Sample;
	Sample.Timestamp = TimeSeconds;
	Sample.Transform = InTransform;
	Sample.Transform.NormalizeRotation();
	Sample.ActionToken = ActionToken;
	Sample.bIsDiscontinuity = bIsDiscontinuity;
	Samples.Add(Sample);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::RecordSample(const FGuid& RecordingId, FName TrackId, const FTransform& InTransform, FName ActionToken, bool bIsDiscontinuity)
{
	const FActiveRecording* Rec = ActiveRecordings.Find(RecordingId);
	const UWorld* World = GetWorld();
	if (!Rec || !World)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown recording or no world clock"));
	}
	return RecordSampleAt(RecordingId, TrackId, World->GetTimeSeconds() - Rec->StartWorldTime, InTransform, ActionToken, bIsDiscontinuity);
}

FDocSystemResult UDocReplayGhostSubsystem::AddAnnotation(const FGuid& RecordingId, FName TrackId, double TimeSeconds, const FString& Text)
{
	FActiveRecording* Rec = ActiveRecordings.Find(RecordingId);
	if (!Rec)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown recording"));
	}
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < 0.0 || TimeSeconds > Rec->MaxDuration || Text.Len() > MaxAnnotationChars
		|| (!TrackId.IsNone() && !Rec->Header.Tracks.ContainsByPredicate([TrackId](const FDocGhostTrackInfo& T) { return T.TrackId == TrackId; })))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid annotation"));
	}
	if (Rec->Annotations.Num() >= FDocGhostRecordingIO::MaxAnnotations)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Annotation limit reached"));
	}
	FDocGhostAnnotation Annotation;
	Annotation.Timestamp = TimeSeconds;
	Annotation.TrackId = TrackId;
	Annotation.Text = Text;
	Rec->Annotations.Add(Annotation);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::StopRecording(const FGuid& RecordingId, FDocGhostRecording& OutFinalRecording)
{
	OutFinalRecording = FDocGhostRecording();
	FActiveRecording Rec;
	if (!ActiveRecordings.RemoveAndCopyValue(RecordingId, Rec))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown recording"));
	}
	FDocGhostRecording R;
	R.Header = Rec.Header;
	const double ChunkDuration = R.Header.ChunkDurationSeconds;
	double Duration = 0.0;
	int32 Frames = 0;
	int32 ChunkIndex = 0;
	for (const FDocGhostTrackInfo& Track : R.Header.Tracks)
	{
		const TArray<FDocGhostSample>* Samples = Rec.Samples.Find(Track.TrackId);
		if (!Samples)
		{
			continue;
		}
		FDocGhostTrackChunk* Current = nullptr;
		int64 CurrentWindow = -1;
		for (const FDocGhostSample& Sample : *Samples)
		{
			int64 Window = static_cast<int64>(FMath::FloorToDouble(Sample.Timestamp / ChunkDuration));
			while (Window > 0 && static_cast<double>(Window) * ChunkDuration > Sample.Timestamp) { --Window; }
			while (static_cast<double>(Window + 1) * ChunkDuration <= Sample.Timestamp) { ++Window; }
			if (!Current || Window != CurrentWindow)
			{
				FDocGhostTrackChunk Chunk;
				Chunk.ChunkIndex = ChunkIndex++;
				Chunk.TrackId = Track.TrackId;
				Chunk.StartTime = static_cast<double>(Window) * ChunkDuration;
				Chunk.EndTime = static_cast<double>(Window + 1) * ChunkDuration;
				Current = &R.Chunks.Add_GetRef(Chunk);
				CurrentWindow = Window;
			}
			Current->Samples.Add(Sample);
			Duration = FMath::Max(Duration, Sample.Timestamp);
			++Frames;
		}
	}
	if (Frames == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Empty recording discarded"));
	}
	R.Header.TotalDurationSeconds = Duration;
	R.Header.FrameCount = Frames;
	for (FDocGhostAnnotation& Annotation : Rec.Annotations)
	{
		Annotation.Timestamp = FMath::Min(Annotation.Timestamp, Duration);
	}
	Rec.Annotations.StableSort([](const FDocGhostAnnotation& A, const FDocGhostAnnotation& B) { return A.Timestamp < B.Timestamp; });
	R.Annotations = Rec.Annotations;
	R.Header.bFinalized = true;
	R.Header.PayloadCrc = static_cast<int64>(R.ComputePayloadCrc());
	const FDocSystemResult Valid = R.ValidateRecording();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	OutFinalRecording = MoveTemp(R);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::CancelRecording(const FGuid& RecordingId)
{
	return ActiveRecordings.Remove(RecordingId) > 0
		? FDocSystemResult::MakeSuccess()
		: FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown recording"));
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocReplayGhostSubsystem::StartPlayback(const FDocGhostRecording& InRecording, bool bLooping, FGuid& OutSessionId)
{
	OutSessionId.Invalidate();
	const FDocSystemResult Valid = InRecording.ValidateRecording();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No world"));
	}
	if (ActivePlaybacks.Num() >= MaxConcurrentSessions)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Too many concurrent ghost sessions"));
	}

	// Resolve every visual before spawning anything; file data never names classes.
	EDocGhostVisualCompatibility Compatibility = EDocGhostVisualCompatibility::Full;
	TMap<FName, UClass*> Classes;
	for (const FDocGhostTrackInfo& Track : InRecording.Header.Tracks)
	{
		UClass* Class = nullptr;
		if (const TSubclassOf<ADocGhostSurrogateActor>* Registered = VisualCatalog.Find(Track.VisualId))
		{
			Class = Registered->Get();
		}
		else if (Track.VisualId == TEXT("Default"))
		{
			Class = ADocGhostSurrogateActor::StaticClass();
		}
		if (!Class)
		{
			if (!InRecording.Header.bHasVisualFallback)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
					FString::Printf(TEXT("Visual %s is not approved and the recording allows no fallback"), *Track.VisualId.ToString()));
			}
			Class = ADocGhostSurrogateActor::StaticClass();
			Compatibility = EDocGhostVisualCompatibility::TransformOnlyFallback;
		}
		Classes.Add(Track.TrackId, Class);
	}

	FActivePlayback Playback;
	Playback.Session.SessionId = FGuid::NewGuid();
	Playback.Session.RecordingId = InRecording.Header.RecordingId;
	Playback.Session.State = EDocGhostPlaybackState::Playing;
	Playback.Session.bLooping = bLooping;
	Playback.Session.Generation = NextGeneration++;
	Playback.Session.VisualCompatibility = Compatibility;
	Playback.Recording = InRecording;
	for (const TPair<FName, UClass*>& Kvp : Classes)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		ADocGhostSurrogateActor* Surrogate = World->SpawnActor<ADocGhostSurrogateActor>(Kvp.Value, FTransform::Identity, Params);
		if (Surrogate)
		{
			Surrogate->EnforceIsolation();
			Surrogate->SetActorHiddenInGame(true);
		}
		Playback.Surrogates.Add(Kvp.Key, Surrogate);
	}
	OutSessionId = Playback.Session.SessionId;
	FActivePlayback& Stored = ActivePlaybacks.Add(OutSessionId, MoveTemp(Playback));
	ApplyState(Stored, /*bEmitTokens*/ false);
	return FDocSystemResult::MakeSuccess();
}

void UDocReplayGhostSubsystem::ApplyState(FActivePlayback& Playback, bool bEmitTokens, int32* OutVisits)
{
	int32 TotalVisits = 0;
	for (TPair<FName, TWeakObjectPtr<ADocGhostSurrogateActor>>& Kvp : Playback.Surrogates)
	{
		FDocGhostTrackState State;
		int32 Visits = 0;
		Playback.Recording.EvaluateTrack(Playback.Session.CurrentTime, Kvp.Key, State, &Visits);
		TotalVisits += Visits;
		const bool bShow = State.bVisible && Playback.Session.State != EDocGhostPlaybackState::Stopped;
		if (ADocGhostSurrogateActor* Surrogate = Kvp.Value.Get())
		{
			Surrogate->SetActorHiddenInGame(!bShow);
			Surrogate->UpdateGhostTransform(State.Transform);
		}
		FName& Last = Playback.LastTokens.FindOrAdd(Kvp.Key);
		if (bEmitTokens && !State.ActionToken.IsNone() && State.ActionToken != Last)
		{
			OnPresentationEvent.Broadcast(Playback.Session.SessionId, Kvp.Key, TEXT("Token"), State.ActionToken.ToString());
		}
		Last = State.ActionToken;
	}
	if (OutVisits)
	{
		*OutVisits = TotalVisits;
	}
}

void UDocReplayGhostSubsystem::FireAnnotations(FActivePlayback& Playback, double FromExclusive, double ToInclusive)
{
	for (const FDocGhostAnnotation& Annotation : Playback.Recording.Annotations)
	{
		if (Annotation.Timestamp > FromExclusive && Annotation.Timestamp <= ToInclusive)
		{
			OnPresentationEvent.Broadcast(Playback.Session.SessionId, Annotation.TrackId, TEXT("Annotation"), Annotation.Text);
		}
	}
}

FDocSystemResult UDocReplayGhostSubsystem::AdvancePlayback(const FGuid& SessionId, float DeltaSeconds)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown or closed session"));
	}
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Delta must be finite and >= 0"));
	}
	FDocGhostPlaybackSession& S = Playback->Session;
	if (S.State != EDocGhostPlaybackState::Playing)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Not playing"));
	}
	const double Duration = Playback->Recording.Header.TotalDurationSeconds;
	const double Previous = S.CurrentTime;
	double Next = Previous + static_cast<double>(DeltaSeconds) * static_cast<double>(S.PlaybackRate);
	if (S.PlaybackRate >= 0.0f)
	{
		if (Next >= Duration)
		{
			FireAnnotations(*Playback, Previous, Duration);
			if (S.bLooping && Duration > 0.0)
			{
				Next = FMath::Fmod(Next - Duration, Duration);
				FireAnnotations(*Playback, -1.0, Next);
			}
			else
			{
				Next = Duration;
				S.State = EDocGhostPlaybackState::Completed;
			}
		}
		else
		{
			FireAnnotations(*Playback, Previous, Next);
		}
	}
	else if (Next <= 0.0)
	{
		// Reverse playback never fires annotations; it stops at the start.
		Next = 0.0;
		S.State = EDocGhostPlaybackState::Paused;
	}
	S.CurrentTime = Next;
	ApplyState(*Playback, /*bEmitTokens*/ S.PlaybackRate > 0.0f);
	return FDocSystemResult::MakeSuccess();
}

void UDocReplayGhostSubsystem::AdvanceAll(float DeltaSeconds)
{
	TArray<FGuid> Ids;
	ActivePlaybacks.GetKeys(Ids);
	for (const FGuid& Id : Ids)
	{
		AdvancePlayback(Id, DeltaSeconds);
	}
}

FDocSystemResult UDocReplayGhostSubsystem::PausePlayback(const FGuid& SessionId)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	if (Playback->Session.State == EDocGhostPlaybackState::Paused)
	{
		return FDocSystemResult::MakeNoChange();
	}
	if (Playback->Session.State != EDocGhostPlaybackState::Playing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Only a playing session can pause"));
	}
	Playback->Session.State = EDocGhostPlaybackState::Paused;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::ResumePlayback(const FGuid& SessionId)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	switch (Playback->Session.State)
	{
	case EDocGhostPlaybackState::Playing:
		return FDocSystemResult::MakeNoChange();
	case EDocGhostPlaybackState::Paused:
		Playback->Session.State = EDocGhostPlaybackState::Playing; // continue from the paused time, never from zero
		return FDocSystemResult::MakeSuccess();
	default:
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Session is stopped/completed; seek first to choose where to continue"));
	}
}

FDocSystemResult UDocReplayGhostSubsystem::SeekPlayback(const FGuid& SessionId, double TargetTimeSeconds)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	if (!FMath::IsFinite(TargetTimeSeconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Seek target must be finite"));
	}
	FDocGhostPlaybackSession& S = Playback->Session;
	S.CurrentTime = FMath::Clamp(TargetTimeSeconds, 0.0, Playback->Recording.Header.TotalDurationSeconds);
	if (S.State == EDocGhostPlaybackState::Completed || S.State == EDocGhostPlaybackState::Stopped)
	{
		S.State = EDocGhostPlaybackState::Paused;
	}
	ApplyState(*Playback, /*bEmitTokens*/ false, &S.LastSeekChunkVisits);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::SetPlaybackRate(const FGuid& SessionId, float Rate)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	if (!FMath::IsFinite(Rate) || FMath::Abs(Rate) > 8.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Rate must be finite and within +/-8"));
	}
	Playback->Session.PlaybackRate = Rate;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::StopPlayback(const FGuid& SessionId)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	Playback->Session.State = EDocGhostPlaybackState::Stopped;
	ApplyState(*Playback, false);
	return FDocSystemResult::MakeSuccess();
}

void UDocReplayGhostSubsystem::DestroySurrogates(FActivePlayback& Playback)
{
	for (TPair<FName, TWeakObjectPtr<ADocGhostSurrogateActor>>& Kvp : Playback.Surrogates)
	{
		if (ADocGhostSurrogateActor* Surrogate = Kvp.Value.Get())
		{
			Surrogate->Destroy();
		}
	}
	Playback.Surrogates.Reset();
}

FDocSystemResult UDocReplayGhostSubsystem::ClosePlaybackSession(const FGuid& SessionId)
{
	FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	DestroySurrogates(*Playback);
	ActivePlaybacks.Remove(SessionId);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Async open
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocReplayGhostSubsystem::RequestOpenRecording(const TArray<uint8>& Bytes, bool bLooping, FGuid& OutRequestId)
{
	OutRequestId = FGuid::NewGuid();
	FOpenRequest Request;
	Request.Bytes = Bytes;
	Request.bLooping = bLooping;
	OpenRequests.Add(OutRequestId, MoveTemp(Request));
	return FDocSystemResult::MakeSuccess();
}

int32 UDocReplayGhostSubsystem::CompleteOpenRequests()
{
	int32 Completed = 0;
	for (TPair<FGuid, FOpenRequest>& Kvp : OpenRequests)
	{
		FOpenRequest& Request = Kvp.Value;
		if (Request.bDone)
		{
			continue;
		}
		FDocGhostRecording Recording;
		Request.Result = FDocGhostRecordingIO::Read(Request.Bytes, Recording);
		if (Request.Result.IsSuccess())
		{
			Request.Result = StartPlayback(Recording, Request.bLooping, Request.SessionId);
		}
		Request.bDone = true;
		Request.Bytes.Empty();
		++Completed;
	}
	return Completed;
}

FDocSystemResult UDocReplayGhostSubsystem::CancelOpenRequest(const FGuid& RequestId)
{
	FOpenRequest* Request = OpenRequests.Find(RequestId);
	if (!Request)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown request"));
	}
	if (Request->bDone)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already completed; close the session instead"));
	}
	OpenRequests.Remove(RequestId); // a late read completion finds nothing to start
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocReplayGhostSubsystem::QueryOpenRequest(const FGuid& RequestId, FGuid& OutSessionId) const
{
	OutSessionId.Invalidate();
	const FOpenRequest* Request = OpenRequests.Find(RequestId);
	if (!Request)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown or cancelled request"));
	}
	if (!Request->bDone)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Still opening"));
	}
	OutSessionId = Request->SessionId;
	return Request->Result;
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

bool UDocReplayGhostSubsystem::QueryTrackState(const FGuid& SessionId, FName TrackId, FDocGhostTrackState& OutState) const
{
	const FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	return Playback && Playback->Recording.EvaluateTrack(Playback->Session.CurrentTime, TrackId, OutState);
}

bool UDocReplayGhostSubsystem::QuerySession(const FGuid& SessionId, FDocGhostPlaybackSession& OutSession) const
{
	const FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	if (!Playback)
	{
		return false;
	}
	OutSession = Playback->Session;
	return true;
}

ADocGhostSurrogateActor* UDocReplayGhostSubsystem::GetSurrogateActor(const FGuid& SessionId, FName TrackId) const
{
	const FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	const TWeakObjectPtr<ADocGhostSurrogateActor>* Found = Playback ? Playback->Surrogates.Find(TrackId) : nullptr;
	return Found ? Found->Get() : nullptr;
}

int32 UDocReplayGhostSubsystem::GetSurrogateCount() const
{
	int32 Count = 0;
	for (const TPair<FGuid, FActivePlayback>& Kvp : ActivePlaybacks)
	{
		for (const TPair<FName, TWeakObjectPtr<ADocGhostSurrogateActor>>& S : Kvp.Value.Surrogates)
		{
			Count += S.Value.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

bool UDocReplayGhostSubsystem::NotifyVisualLoaded(const FGuid& SessionId, int32 Generation) const
{
	const FActivePlayback* Playback = ActivePlaybacks.Find(SessionId);
	return Playback && Playback->Session.Generation == Generation;
}

FDocSystemResult UDocReplayGhostSubsystem::QueryAnimationCapability() const
{
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
		TEXT("No notify/root-motion isolating animation adapter; ghosts use transform/pose proxies with animation paused"));
}
