#include "DocGhostTypes.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

namespace DocGhostPrivate
{
	constexpr int32 MaxAnnotationChars = 256;

	/** Strings are stored as a bounded count of UTF-16 code units (no conversion libraries involved). */
	static void WriteString(FArchive& Ar, const FString& Value)
	{
		int32 Len = FMath::Min(Value.Len(), FDocGhostRecordingIO::MaxStringBytes / 2);
		Ar << Len;
		for (int32 i = 0; i < Len; ++i)
		{
			uint16 Unit = static_cast<uint16>(Value[i]);
			Ar << Unit;
		}
	}

	static int64 Remaining(const FArchive& Ar)
	{
		return const_cast<FArchive&>(Ar).TotalSize() - const_cast<FArchive&>(Ar).Tell();
	}

	template <typename T>
	static bool ReadPod(FArchive& Ar, T& Value)
	{
		if (Ar.IsError() || Remaining(Ar) < static_cast<int64>(sizeof(T)))
		{
			return false;
		}
		Ar << Value;
		return !Ar.IsError();
	}

	static bool ReadString(FArchive& Ar, FString& Out)
	{
		int32 Len = 0;
		if (!ReadPod(Ar, Len) || Len < 0 || Len > FDocGhostRecordingIO::MaxStringBytes / 2 || Remaining(Ar) < static_cast<int64>(Len) * 2)
		{
			return false;
		}
		Out.Reset(Len);
		for (int32 i = 0; i < Len; ++i)
		{
			uint16 Unit = 0;
			Ar << Unit;
			Out.AppendChar(static_cast<TCHAR>(Unit));
		}
		return !Ar.IsError();
	}

	static void WriteTransform(FArchive& Ar, const FTransform& T)
	{
		FVector L = T.GetLocation();
		FQuat Q = T.GetRotation();
		FVector S = T.GetScale3D();
		Ar << L.X << L.Y << L.Z << Q.X << Q.Y << Q.Z << Q.W << S.X << S.Y << S.Z;
	}

	static bool ReadTransform(FArchive& Ar, FTransform& T)
	{
		double V[10] = { 0 };
		for (double& D : V)
		{
			if (!ReadPod(Ar, D))
			{
				return false;
			}
		}
		T.SetLocation(FVector(V[0], V[1], V[2]));
		T.SetRotation(FQuat(V[3], V[4], V[5], V[6]));
		T.SetScale3D(FVector(V[7], V[8], V[9]));
		return true;
	}

	static void WritePayload(FArchive& Ar, const FDocGhostRecording& R)
	{
		int32 ChunkCount = R.Chunks.Num();
		Ar << ChunkCount;
		for (const FDocGhostTrackChunk& C : R.Chunks)
		{
			int32 Index = C.ChunkIndex;
			double Start = C.StartTime;
			double End = C.EndTime;
			int32 Count = C.Samples.Num();
			Ar << Index;
			WriteString(Ar, C.TrackId.ToString());
			Ar << Start << End << Count;
			for (const FDocGhostSample& S : C.Samples)
			{
				double Time = S.Timestamp;
				uint8 bDisc = S.bIsDiscontinuity ? 1 : 0;
				Ar << Time;
				WriteTransform(Ar, S.Transform);
				Ar << bDisc;
				WriteString(Ar, S.ActionToken.IsNone() ? FString() : S.ActionToken.ToString());
			}
		}
		int32 AnnotationCount = R.Annotations.Num();
		Ar << AnnotationCount;
		for (const FDocGhostAnnotation& A : R.Annotations)
		{
			double Time = A.Timestamp;
			Ar << Time;
			WriteString(Ar, A.TrackId.IsNone() ? FString() : A.TrackId.ToString());
			WriteString(Ar, A.Text);
		}
	}

	static bool IsFiniteTransform(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FVector S = T.GetScale3D();
		const FQuat Q = T.GetRotation();
		return FMath::IsFinite(L.X) && FMath::IsFinite(L.Y) && FMath::IsFinite(L.Z)
			&& FMath::IsFinite(S.X) && FMath::IsFinite(S.Y) && FMath::IsFinite(S.Z)
			&& FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) && FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W)
			&& Q.SizeSquared() > 0.25 && FMath::Abs(L.X) < 1.0e9 && FMath::Abs(L.Y) < 1.0e9 && FMath::Abs(L.Z) < 1.0e9;
	}
}

// ---------------------------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------------------------

void FDocGhostRecording::GetTrackChunkIndices(FName TrackId, TArray<int32>& OutIndices) const
{
	if (const TArray<int32>* Cached = TrackIndexCache.Find(TrackId))
	{
		OutIndices = *Cached;
		return;
	}
	OutIndices.Reset();
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		if (Chunks[Index].TrackId == TrackId && Chunks[Index].Samples.Num() > 0)
		{
			OutIndices.Add(Index);
		}
	}
	OutIndices.Sort([this](int32 A, int32 B) { return Chunks[A].StartTime < Chunks[B].StartTime; });
	TrackIndexCache.Add(TrackId, OutIndices);
}

bool FDocGhostRecording::EvaluateTrack(double Time, FName TrackId, FDocGhostTrackState& OutState, int32* OutChunkVisits) const
{
	OutState = FDocGhostTrackState();
	OutState.TrackId = TrackId;
	if (!FindTrack(TrackId) || !FMath::IsFinite(Time))
	{
		return false;
	}
	const TArray<int32>* IndicesPtr = TrackIndexCache.Find(TrackId);
	TArray<int32> Built;
	if (!IndicesPtr)
	{
		GetTrackChunkIndices(TrackId, Built);
		IndicesPtr = TrackIndexCache.Find(TrackId);
	}
	const TArray<int32>& Indices = *IndicesPtr;
	int32 Visits = 0;
	if (Indices.IsEmpty())
	{
		return true; // track never sampled: nothing to show
	}

	// Largest chunk whose first sample is at or before Time.
	int32 Low = 0;
	int32 High = Indices.Num() - 1;
	int32 Found = INDEX_NONE;
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		++Visits;
		if (Chunks[Indices[Mid]].Samples[0].Timestamp <= Time)
		{
			Found = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}
	if (Found == INDEX_NONE)
	{
		if (OutChunkVisits) { *OutChunkVisits = Visits; }
		return true; // before the track's first sample: not present yet
	}

	const TArray<FDocGhostSample>& Samples = Chunks[Indices[Found]].Samples;
	int32 SLow = 0;
	int32 SHigh = Samples.Num() - 1;
	int32 A = 0;
	while (SLow <= SHigh)
	{
		const int32 Mid = SLow + (SHigh - SLow) / 2;
		if (Samples[Mid].Timestamp <= Time)
		{
			A = Mid;
			SLow = Mid + 1;
		}
		else
		{
			SHigh = Mid - 1;
		}
	}
	const FDocGhostSample& SampleA = Samples[A];
	const FDocGhostSample* SampleB = nullptr;
	if (A + 1 < Samples.Num())
	{
		SampleB = &Samples[A + 1];
	}
	else if (Found + 1 < Indices.Num())
	{
		++Visits;
		SampleB = &Chunks[Indices[Found + 1]].Samples[0];
	}
	if (OutChunkVisits)
	{
		*OutChunkVisits = Visits;
	}

	OutState.bVisible = true;
	OutState.ActionToken = SampleA.ActionToken;
	OutState.Transform = SampleA.Transform;
	if (!SampleB)
	{
		return true; // after the last sample: hold
	}
	if (SampleB->bIsDiscontinuity)
	{
		OutState.bAtDiscontinuity = true; // never interpolate through a teleport/rebase
		return true;
	}
	const double Span = SampleB->Timestamp - SampleA.Timestamp;
	if (Span > Header.GapThresholdSeconds)
	{
		OutState.bInGap = true;
		switch (Header.GapPolicy)
		{
		case EDocGhostGapPolicy::Hide:
			OutState.bVisible = false;
			break;
		case EDocGhostGapPolicy::Snap:
			OutState.Transform = SampleB->Transform;
			break;
		default:
			break;
		}
		return true;
	}
	const double Alpha = Span > 0.0 ? FMath::Clamp((Time - SampleA.Timestamp) / Span, 0.0, 1.0) : 0.0;
	FQuat QA = SampleA.Transform.GetRotation().GetNormalized();
	FQuat QB = SampleB->Transform.GetRotation().GetNormalized();
	if ((QA | QB) < 0.0)
	{
		QB = FQuat(-QB.X, -QB.Y, -QB.Z, -QB.W); // shortest path
	}
	const FQuat Rotation = FQuat::Slerp(QA, QB, Alpha).GetNormalized();
	const FVector Location = FMath::Lerp(SampleA.Transform.GetLocation(), SampleB->Transform.GetLocation(), Alpha);
	const FVector Scale = FMath::Lerp(SampleA.Transform.GetScale3D(), SampleB->Transform.GetScale3D(), Alpha);
	OutState.Transform = FTransform(Rotation, Location, Scale);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

uint32 FDocGhostRecording::ComputePayloadCrc() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	DocGhostPrivate::WritePayload(Writer, *this);
	return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
}

FDocSystemResult FDocGhostRecording::ValidateRecording() const
{
	auto Bad = [](const FString& Why) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, Why); };
	if (Header.FormatVersion != FDocGhostRecordingHeader::CurrentFormatVersion || Header.AlgorithmVersion > FDocGhostRecordingHeader::CurrentAlgorithmVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Unsupported recording version %d/%d"), Header.FormatVersion, Header.AlgorithmVersion));
	}
	if (!Header.bFinalized)
	{
		return Bad(TEXT("Recording was never finalized"));
	}
	if (Header.Tracks.IsEmpty() || Header.Tracks.Num() > FDocGhostRecordingIO::MaxTracks)
	{
		return Bad(TEXT("Track count out of range"));
	}
	TSet<FName> TrackIds;
	for (const FDocGhostTrackInfo& T : Header.Tracks)
	{
		bool bDup = false;
		TrackIds.Add(T.TrackId, &bDup);
		if (T.TrackId.IsNone() || T.VisualId.IsNone() || bDup)
		{
			return Bad(TEXT("Tracks need unique ids and a visual id"));
		}
	}
	const double Duration = Header.TotalDurationSeconds;
	if (!FMath::IsFinite(Duration) || Duration < 0.0 || Duration > FDocGhostRecordingIO::MaxDurationSeconds
		|| !FMath::IsFinite(Header.SampleCadenceSeconds) || Header.SampleCadenceSeconds <= 0.0
		|| !FMath::IsFinite(Header.GapThresholdSeconds) || Header.GapThresholdSeconds <= 0.0
		|| !FMath::IsFinite(Header.ChunkDurationSeconds) || Header.ChunkDurationSeconds <= 0.0)
	{
		return Bad(TEXT("Invalid timing header"));
	}
	if (Chunks.Num() > FDocGhostRecordingIO::MaxChunks)
	{
		return Bad(TEXT("Too many chunks"));
	}
	TMap<FName, double> LastTime;
	int32 Total = 0;
	double MaxTime = -1.0;
	TArray<int32> Order;
	for (int32 i = 0; i < Chunks.Num(); ++i)
	{
		Order.Add(i);
	}
	Order.StableSort([this](int32 A, int32 B) { return Chunks[A].StartTime < Chunks[B].StartTime; });
	for (int32 Index : Order)
	{
		const FDocGhostTrackChunk& C = Chunks[Index];
		if (!TrackIds.Contains(C.TrackId))
		{
			return Bad(TEXT("Chunk references an unknown track"));
		}
		if (!FMath::IsFinite(C.StartTime) || !FMath::IsFinite(C.EndTime) || C.StartTime < 0.0 || C.EndTime <= C.StartTime)
		{
			return Bad(TEXT("Invalid chunk range"));
		}
		if (C.Samples.IsEmpty() || C.Samples.Num() > FDocGhostRecordingIO::MaxSamplesPerChunk)
		{
			return Bad(TEXT("Invalid chunk sample count"));
		}
		for (const FDocGhostSample& S : C.Samples)
		{
			if (!FMath::IsFinite(S.Timestamp) || S.Timestamp < C.StartTime || S.Timestamp >= C.EndTime)
			{
				return Bad(TEXT("Sample outside its chunk"));
			}
			double& Last = LastTime.FindOrAdd(C.TrackId, -1.0);
			if (S.Timestamp <= Last)
			{
				return Bad(TEXT("Sample times must strictly increase per track"));
			}
			Last = S.Timestamp;
			if (!DocGhostPrivate::IsFiniteTransform(S.Transform))
			{
				return Bad(TEXT("Non-finite or degenerate transform"));
			}
			MaxTime = FMath::Max(MaxTime, S.Timestamp);
			++Total;
		}
	}
	if (Total != Header.FrameCount || Total > FDocGhostRecordingIO::MaxTotalSamples)
	{
		return Bad(TEXT("Frame count does not match the samples"));
	}
	if (Total > 0 && !FMath::IsNearlyEqual(MaxTime, Duration, 1.0e-6))
	{
		return Bad(TEXT("Duration does not match the samples"));
	}
	if (Annotations.Num() > FDocGhostRecordingIO::MaxAnnotations)
	{
		return Bad(TEXT("Too many annotations"));
	}
	for (const FDocGhostAnnotation& A : Annotations)
	{
		if (!FMath::IsFinite(A.Timestamp) || A.Timestamp < 0.0 || A.Timestamp > Duration + 1.0e-6
			|| (!A.TrackId.IsNone() && !TrackIds.Contains(A.TrackId)) || A.Text.Len() > DocGhostPrivate::MaxAnnotationChars)
		{
			return Bad(TEXT("Invalid annotation"));
		}
	}
	if (static_cast<uint32>(Header.PayloadCrc) != ComputePayloadCrc())
	{
		return Bad(TEXT("Integrity check failed"));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// File format
// ---------------------------------------------------------------------------------------------

bool FDocGhostRecordingIO::Write(const FDocGhostRecording& R, TArray<uint8>& OutBytes)
{
	using namespace DocGhostPrivate;
	OutBytes.Reset();
	FMemoryWriter Ar(OutBytes);
	uint32 M = Magic;
	int32 Version = R.Header.FormatVersion;
	int32 Algorithm = R.Header.AlgorithmVersion;
	Ar << M << Version << Algorithm;
	uint32 IdParts[4] = { R.Header.RecordingId.A, R.Header.RecordingId.B, R.Header.RecordingId.C, R.Header.RecordingId.D };
	for (uint32& Part : IdParts)
	{
		Ar << Part;
	}
	WriteString(Ar, R.Header.RecordingName);
	WriteString(Ar, R.Header.ContentHash);
	WriteString(Ar, R.Header.WorldNamespace.IsNone() ? FString() : R.Header.WorldNamespace.ToString());
	double Cadence = R.Header.SampleCadenceSeconds;
	double Gap = R.Header.GapThresholdSeconds;
	uint8 Policy = static_cast<uint8>(R.Header.GapPolicy);
	double ChunkDuration = R.Header.ChunkDurationSeconds;
	double Duration = R.Header.TotalDurationSeconds;
	int32 Frames = R.Header.FrameCount;
	uint8 Fallback = R.Header.bHasVisualFallback ? 1 : 0;
	uint8 Finalized = R.Header.bFinalized ? 1 : 0;
	int64 Crc = R.Header.PayloadCrc;
	Ar << Cadence << Gap << Policy << ChunkDuration << Duration << Frames << Fallback << Finalized << Crc;
	int32 TrackCount = R.Header.Tracks.Num();
	Ar << TrackCount;
	for (const FDocGhostTrackInfo& T : R.Header.Tracks)
	{
		WriteString(Ar, T.TrackId.ToString());
		WriteString(Ar, T.VisualId.ToString());
	}
	WritePayload(Ar, R);
	return !Ar.IsError();
}

FDocSystemResult FDocGhostRecordingIO::Read(const TArray<uint8>& Bytes, FDocGhostRecording& OutRecording)
{
	using namespace DocGhostPrivate;
	OutRecording = FDocGhostRecording();
	auto Bad = [](const TCHAR* Why) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, Why); };
	FMemoryReader Ar(Bytes);
	uint32 M = 0;
	int32 Version = 0;
	int32 Algorithm = 0;
	if (!ReadPod(Ar, M) || !ReadPod(Ar, Version) || !ReadPod(Ar, Algorithm))
	{
		return Bad(TEXT("Truncated recording"));
	}
	if (M != Magic)
	{
		return Bad(TEXT("Not a ghost recording"));
	}
	if (Version != FDocGhostRecordingHeader::CurrentFormatVersion || Algorithm > FDocGhostRecordingHeader::CurrentAlgorithmVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Unsupported recording version %d/%d"), Version, Algorithm));
	}
	FDocGhostRecording R;
	R.Header.FormatVersion = Version;
	R.Header.AlgorithmVersion = Algorithm;
	uint32 IdParts[4] = { 0, 0, 0, 0 };
	for (uint32& Part : IdParts)
	{
		if (!ReadPod(Ar, Part))
		{
			return Bad(TEXT("Truncated header"));
		}
	}
	R.Header.RecordingId = FGuid(IdParts[0], IdParts[1], IdParts[2], IdParts[3]);
	FString WorldNs;
	if (!ReadString(Ar, R.Header.RecordingName) || !ReadString(Ar, R.Header.ContentHash) || !ReadString(Ar, WorldNs))
	{
		return Bad(TEXT("Malformed header strings"));
	}
	R.Header.WorldNamespace = WorldNs.IsEmpty() ? NAME_None : FName(*WorldNs);
	uint8 Policy = 0;
	uint8 Fallback = 0;
	uint8 Finalized = 0;
	if (!ReadPod(Ar, R.Header.SampleCadenceSeconds) || !ReadPod(Ar, R.Header.GapThresholdSeconds) || !ReadPod(Ar, Policy)
		|| !ReadPod(Ar, R.Header.ChunkDurationSeconds) || !ReadPod(Ar, R.Header.TotalDurationSeconds) || !ReadPod(Ar, R.Header.FrameCount)
		|| !ReadPod(Ar, Fallback) || !ReadPod(Ar, Finalized) || !ReadPod(Ar, R.Header.PayloadCrc))
	{
		return Bad(TEXT("Truncated header"));
	}
	if (Policy > static_cast<uint8>(EDocGhostGapPolicy::Snap))
	{
		return Bad(TEXT("Unknown gap policy"));
	}
	R.Header.GapPolicy = static_cast<EDocGhostGapPolicy>(Policy);
	R.Header.bHasVisualFallback = Fallback != 0;
	R.Header.bFinalized = Finalized != 0;
	int32 TrackCount = 0;
	if (!ReadPod(Ar, TrackCount) || TrackCount < 1 || TrackCount > MaxTracks)
	{
		return Bad(TEXT("Track count out of range"));
	}
	for (int32 i = 0; i < TrackCount; ++i)
	{
		FString TrackId;
		FString VisualId;
		if (!ReadString(Ar, TrackId) || !ReadString(Ar, VisualId) || TrackId.IsEmpty() || VisualId.IsEmpty())
		{
			return Bad(TEXT("Malformed track table"));
		}
		FDocGhostTrackInfo Info;
		Info.TrackId = FName(*TrackId);
		Info.VisualId = FName(*VisualId);
		R.Header.Tracks.Add(Info);
	}
	int32 ChunkCount = 0;
	// Minimum bytes per chunk: index + string length + start + end + count.
	if (!ReadPod(Ar, ChunkCount) || ChunkCount < 0 || ChunkCount > MaxChunks || Remaining(Ar) < static_cast<int64>(ChunkCount) * 28)
	{
		return Bad(TEXT("Chunk count out of range"));
	}
	constexpr int64 MinSampleBytes = 8 + 80 + 1 + 4;
	int32 TotalSamples = 0;
	for (int32 c = 0; c < ChunkCount; ++c)
	{
		FDocGhostTrackChunk Chunk;
		FString TrackId;
		int32 Count = 0;
		if (!ReadPod(Ar, Chunk.ChunkIndex) || !ReadString(Ar, TrackId) || !ReadPod(Ar, Chunk.StartTime) || !ReadPod(Ar, Chunk.EndTime) || !ReadPod(Ar, Count))
		{
			return Bad(TEXT("Malformed chunk"));
		}
		if (Count < 0 || Count > MaxSamplesPerChunk || TotalSamples + Count > MaxTotalSamples || Remaining(Ar) < static_cast<int64>(Count) * MinSampleBytes)
		{
			return Bad(TEXT("Sample count out of range"));
		}
		Chunk.TrackId = FName(*TrackId);
		TotalSamples += Count;
		Chunk.Samples.SetNum(Count);
		for (FDocGhostSample& S : Chunk.Samples)
		{
			uint8 bDisc = 0;
			FString Token;
			if (!ReadPod(Ar, S.Timestamp) || !ReadTransform(Ar, S.Transform) || !ReadPod(Ar, bDisc) || !ReadString(Ar, Token))
			{
				return Bad(TEXT("Malformed sample"));
			}
			S.bIsDiscontinuity = bDisc != 0;
			S.ActionToken = Token.IsEmpty() ? NAME_None : FName(*Token);
		}
		R.Chunks.Add(MoveTemp(Chunk));
	}
	int32 AnnotationCount = 0;
	if (!ReadPod(Ar, AnnotationCount) || AnnotationCount < 0 || AnnotationCount > MaxAnnotations)
	{
		return Bad(TEXT("Annotation count out of range"));
	}
	for (int32 a = 0; a < AnnotationCount; ++a)
	{
		FDocGhostAnnotation Annotation;
		FString TrackId;
		if (!ReadPod(Ar, Annotation.Timestamp) || !ReadString(Ar, TrackId) || !ReadString(Ar, Annotation.Text))
		{
			return Bad(TEXT("Malformed annotation"));
		}
		Annotation.TrackId = TrackId.IsEmpty() ? NAME_None : FName(*TrackId);
		R.Annotations.Add(Annotation);
	}
	if (Ar.IsError() || Remaining(Ar) != 0)
	{
		return Bad(TEXT("Truncated or trailing data"));
	}
	const FDocSystemResult Valid = R.ValidateRecording();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	OutRecording = MoveTemp(R);
	return FDocSystemResult::MakeSuccess();
}
