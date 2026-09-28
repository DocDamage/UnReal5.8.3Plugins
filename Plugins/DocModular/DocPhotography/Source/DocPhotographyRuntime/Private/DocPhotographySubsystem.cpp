#include "DocPhotographySubsystem.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ImageUtils.h"
#include "Misc/SecureHash.h"

namespace DocPhotoPrivate
{
	static constexpr double NearClipCm = 1.0;
	static constexpr int32 MaxRememberedCaptures = 64;

	static float AngleBetween(const FVector& A, const FVector& B)
	{
		const FVector NA = A.GetSafeNormal();
		const FVector NB = B.GetSafeNormal();
		if (NA.IsNearlyZero() || NB.IsNearlyZero())
		{
			return 0.0f;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NA, NB), -1.0, 1.0))));
	}
}

UDocPhotographySubsystem::UDocPhotographySubsystem()
{
}

void UDocPhotographySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocPhotographySubsystem::Deinitialize()
{
	// Player loss: pending captures end; late callbacks will find no ticket.
	for (TPair<FGuid, FActiveCapture>& Kvp : Captures)
	{
		if (!IsTerminal(Kvp.Value.State))
		{
			if (ImageSource)
			{
				ImageSource->CancelCapture(Kvp.Value.Ticket);
			}
			Kvp.Value.State = EDocPhotoCaptureState::Cancelled;
			Kvp.Value.Pixels.Empty();
		}
	}
	Captures.Empty();
	CaptureOrder.Empty();
	FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldTearDownHandle.Reset();
	WorldCleanupHandle.Reset();
	Super::Deinitialize();
}

void UDocPhotographySubsystem::Tick(float DeltaTime)
{
	Pump();
}

bool UDocPhotographySubsystem::IsTickable() const
{
	return !IsTemplate() && GetActiveCaptureCount() > 0;
}

TStatId UDocPhotographySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocPhotographySubsystem, STATGROUP_Tickables);
}

void UDocPhotographySubsystem::SetImageSource(UDocPhotoImageSource* Source)
{
	ImageSource = Source;
}

void UDocPhotographySubsystem::SetBlobStore(UDocPhotoBlobStore* Store)
{
	BlobStore = Store;
}

UDocPhotoImageSource* UDocPhotographySubsystem::GetImageSource()
{
	if (!ImageSource)
	{
		ImageSource = NewObject<UDocSceneCapturePhotoSource>(this);
	}
	return ImageSource;
}

UDocPhotoBlobStore* UDocPhotographySubsystem::GetBlobStore()
{
	if (!BlobStore)
	{
		BlobStore = NewObject<UDocPhotoFileBlobStore>(this);
	}
	return BlobStore;
}

void UDocPhotographySubsystem::SetVisibilityTester(TFunction<bool(UWorld*, const FVector&, const FVector&, const AActor*)> Tester)
{
	VisibilityTester = MoveTemp(Tester);
}

bool UDocPhotographySubsystem::IsWorldAlive(const FActiveCapture& Capture)
{
	const UWorld* World = Capture.World.Get();
	return !Capture.bWorldLost && IsValid(World) && !World->bIsTearingDown;
}

void UDocPhotographySubsystem::EnsureWorldDelegates()
{
	// Engine flags alone are not a reliable "world gone" signal after DestroyWorld, so listen for the events.
	if (!WorldTearDownHandle.IsValid())
	{
		WorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UDocPhotographySubsystem::HandleWorldGone);
	}
	if (!WorldCleanupHandle.IsValid())
	{
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UDocPhotographySubsystem::HandleWorldCleanup);
	}
}

void UDocPhotographySubsystem::HandleWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	HandleWorldGone(World);
}

void UDocPhotographySubsystem::HandleWorldGone(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (TPair<FGuid, FActiveCapture>& Kvp : Captures)
	{
		if (!IsTerminal(Kvp.Value.State) && Kvp.Value.World.Get() == World)
		{
			Kvp.Value.bWorldLost = true;
		}
	}
}

bool UDocPhotographySubsystem::IsTerminal(EDocPhotoCaptureState State)
{
	return State == EDocPhotoCaptureState::Committed || State == EDocPhotoCaptureState::Cancelled || State == EDocPhotoCaptureState::Failed;
}

int32 UDocPhotographySubsystem::GetActiveCaptureCount() const
{
	int32 Count = 0;
	for (const TPair<FGuid, FActiveCapture>& Kvp : Captures)
	{
		Count += IsTerminal(Kvp.Value.State) ? 0 : 1;
	}
	return Count;
}

FString UDocPhotographySubsystem::OwnerKey(const FDocOwnerScope& Owner)
{
	return FString::Printf(TEXT("%d_%s_%s"), static_cast<int32>(Owner.Kind), *Owner.SubjectId.ToString(EGuidFormats::Digits), *Owner.CampaignNamespace.ToString(EGuidFormats::Digits));
}

int32 UDocPhotographySubsystem::CountOwnerPhotos(const FDocOwnerScope& Owner) const
{
	int32 Count = 0;
	for (const TPair<FGuid, FDocPhotoRecord>& Kvp : GalleryRecords)
	{
		Count += Kvp.Value.OwnerScope == Owner ? 1 : 0;
	}
	return Count;
}

int64 UDocPhotographySubsystem::CountOwnerBytes(const FDocOwnerScope& Owner) const
{
	int64 Bytes = 0;
	for (const TPair<FGuid, FDocPhotoRecord>& Kvp : GalleryRecords)
	{
		Bytes += Kvp.Value.OwnerScope == Owner ? Kvp.Value.EncodedBytes : 0;
	}
	return Bytes;
}

// ---------------------------------------------------------------------------------------------
// Projection and observation
// ---------------------------------------------------------------------------------------------

void UDocPhotographySubsystem::ProjectWorldPointToScreenUV(const FVector& WorldPoint, const FVector& CameraLocation,
	const FRotator& CameraRotation, float FOVDegrees, float AspectRatio, FVector2D& OutScreenUV,
	bool& bOutInFrustum, bool& bOutBehindCamera)
{
	const FVector Local = CameraRotation.UnrotateVector(WorldPoint - CameraLocation); // X forward, Y right, Z up
	if (Local.X <= DocPhotoPrivate::NearClipCm)
	{
		bOutBehindCamera = true;
		bOutInFrustum = false;
		OutScreenUV = FVector2D(-1.0, -1.0);
		return;
	}
	bOutBehindCamera = false;
	const double TanHalf = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(FOVDegrees), 5.0, 170.0) * 0.5));
	const double Aspect = FMath::Max(0.01, static_cast<double>(AspectRatio));
	OutScreenUV.X = 0.5 + 0.5 * Local.Y / (Local.X * TanHalf);
	OutScreenUV.Y = 0.5 - 0.5 * Local.Z * Aspect / (Local.X * TanHalf);
	bOutInFrustum = OutScreenUV.X >= 0.0 && OutScreenUV.X <= 1.0 && OutScreenUV.Y >= 0.0 && OutScreenUV.Y <= 1.0;
}

void UDocPhotographySubsystem::SnapshotSubjects(UWorld& World, FActiveCapture& Capture) const
{
	const FDocPhotoCaptureRequest& R = Capture.Request;
	const float Aspect = static_cast<float>(R.Width) / static_cast<float>(FMath::Max(1, R.Height));

	TArray<UDocPhotographableComponent*> Candidates;
	if (const UDocPhotoSubjectRegistry* Registry = World.GetSubsystem<UDocPhotoSubjectRegistry>())
	{
		Capture.SnapshotRevision = Registry->GetRevision();
		for (UDocPhotographableComponent* Subject : Registry->GetSubjectsSorted())
		{
			// Private subjects are never observed (not even as metadata) for another owner.
			if (Subject->bIsPrivate && Subject->OwnerScope != R.OwnerScope)
			{
				continue;
			}
			Candidates.Add(Subject);
		}
	}
	// Bounded: nearest first, stable by id.
	Candidates.StableSort([&R](const UDocPhotographableComponent& A, const UDocPhotographableComponent& B)
	{
		return FVector::DistSquared(A.GetComponentLocation(), R.CameraLocation) < FVector::DistSquared(B.GetComponentLocation(), R.CameraLocation);
	});
	if (Candidates.Num() > GalleryQuota.MaxSubjectsPerCapture)
	{
		Candidates.SetNum(FMath::Max(0, GalleryQuota.MaxSubjectsPerCapture));
	}

	for (UDocPhotographableComponent* Subject : Candidates)
	{
		FDocPhotoSubjectObservation Obs;
		Obs.SubjectId = Subject->SubjectId;
		Obs.DefinitionTag = Subject->SubjectTag;
		Obs.SubjectVersion = Subject->SubjectVersion;
		Obs.WorldLocation = Subject->GetComponentLocation();
		Obs.Timestamp = Capture.RequestedTime;
		Obs.DistanceToCamera = static_cast<float>(FVector::Dist(R.CameraLocation, Obs.WorldLocation));
		Obs.OffAxisAngleDegrees = DocPhotoPrivate::AngleBetween(R.CameraRotation.Vector(), Obs.WorldLocation - R.CameraLocation);
		Obs.FacingAngleDegrees = DocPhotoPrivate::AngleBetween(Subject->GetForwardVector(), R.CameraLocation - Obs.WorldLocation);

		FVector2D CenterUV;
		bool bCenterBehind = false;
		ProjectWorldPointToScreenUV(Obs.WorldLocation, R.CameraLocation, R.CameraRotation, R.FOVDegrees, Aspect, CenterUV, Obs.bInFrustum, bCenterBehind);

		// Projected authored bounds (unclipped vs clipped to the image).
		FBox2D Unclipped(ForceInit);
		int32 Behind = 0;
		for (const FVector& Corner : Subject->GetWorldBoundsCorners())
		{
			FVector2D UV;
			bool bIn = false, bBehind = false;
			ProjectWorldPointToScreenUV(Corner, R.CameraLocation, R.CameraRotation, R.FOVDegrees, Aspect, UV, bIn, bBehind);
			if (bBehind)
			{
				++Behind;
				continue;
			}
			Unclipped += UV;
		}
		Obs.bIsBehindCamera = Behind == 8;
		Obs.bCrossesNearPlane = Behind > 0 && Behind < 8;
		if (Unclipped.bIsValid)
		{
			const FVector2D ClipMin(FMath::Max(Unclipped.Min.X, 0.0), FMath::Max(Unclipped.Min.Y, 0.0));
			const FVector2D ClipMax(FMath::Min(Unclipped.Max.X, 1.0), FMath::Min(Unclipped.Max.Y, 1.0));
			const double FullArea = Unclipped.GetArea();
			if (ClipMax.X > ClipMin.X && ClipMax.Y > ClipMin.Y)
			{
				Obs.ScreenBounds = FBox2D(ClipMin, ClipMax);
				Obs.BoundsInFrameFraction = FullArea > 0.0 ? static_cast<float>(Obs.ScreenBounds.GetArea() / FullArea) : 0.0f;
			}
		}

		// Sampled visibility.
		TArray<FVector> Samples = Subject->GetWorldSamplePoints();
		if (Samples.Num() > GalleryQuota.MaxSamplesPerSubject)
		{
			Samples.SetNum(FMath::Max(0, GalleryQuota.MaxSamplesPerSubject));
		}
		Obs.SampleCount = Samples.Num();
		for (const FVector& Sample : Samples)
		{
			FVector2D UV;
			bool bIn = false, bBehind = false;
			ProjectWorldPointToScreenUV(Sample, R.CameraLocation, R.CameraRotation, R.FOVDegrees, Aspect, UV, bIn, bBehind);
			if (!bIn)
			{
				continue;
			}
			++Obs.InFrameSampleCount;
			bool bVisible = true;
			if (VisibilityTester)
			{
				bVisible = VisibilityTester(&World, R.CameraLocation, Sample, Subject->GetOwner());
			}
			else
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(DocPhotoVisibility), false, Subject->GetOwner());
				const FVector To = Sample - (Sample - R.CameraLocation).GetSafeNormal(); // stop 1 cm short of the sample
				bVisible = !World.LineTraceSingleByChannel(Hit, R.CameraLocation, To, ECC_Visibility, Params);
			}
			Obs.VisibleSampleCount += bVisible ? 1 : 0;
		}
		Obs.EstimatedVisibleFraction = Obs.SampleCount > 0 ? static_cast<float>(Obs.VisibleSampleCount) / Obs.SampleCount : 0.0f;

		Capture.Observations.Add(Obs);
		Capture.SubjectRefs.Add(Subject);
	}
}

// ---------------------------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocPhotographySubsystem::RequestPhoto(UWorld* World, const FDocPhotoCaptureRequest& Request, UDocPhotoEvaluationProfile* Profile, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (!World)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No world"));
	}
	if (!Request.OwnerScope.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Capture needs a valid owner scope"));
	}
	if (Request.Width <= 0 || Request.Height <= 0 || Request.Width > GalleryQuota.MaxImageDimension || Request.Height > GalleryQuota.MaxImageDimension)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Image dimensions must be 1..%d"), GalleryQuota.MaxImageDimension));
	}
	if (!FMath::IsFinite(Request.FOVDegrees) || Request.FOVDegrees < 5.0f || Request.FOVDegrees > 170.0f
		|| Request.CameraLocation.ContainsNaN() || Request.CameraRotation.ContainsNaN()
		|| !FMath::IsFinite(Request.MaxTimeToleranceSeconds) || !FMath::IsFinite(Request.PoseToleranceCm))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid camera parameters"));
	}
	if (Profile)
	{
		const FDocSystemResult Valid = Profile->ValidateProfile();
		if (!Valid.IsSuccess())
		{
			return Valid;
		}
	}
	if (GetActiveCaptureCount() >= GalleryQuota.MaxConcurrentCaptures)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("MaxConcurrentCapturesExceeded"));
	}
	if (CountOwnerPhotos(Request.OwnerScope) >= GalleryQuota.MaxPhotos)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("GalleryFull: delete photos to take more (nothing is evicted automatically)"));
	}

	EnsureWorldDelegates();
	FActiveCapture Capture;
	Capture.RequestId = Request.RequestId.IsValid() ? Request.RequestId : FGuid::NewGuid();
	if (Captures.Contains(Capture.RequestId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("RequestId already used"));
	}
	Capture.Ticket = NextTicket++;
	Capture.Request = Request;
	Capture.Request.RequestId = Capture.RequestId;
	Capture.Request.AspectRatio = static_cast<float>(Request.Width) / static_cast<float>(Request.Height);
	Capture.World = World;
	Capture.WorldName = World->GetFName();
	Capture.Profile = Profile;
	Capture.RequestedTime = World->GetTimeSeconds();
	SnapshotSubjects(*World, Capture);

	const FGuid RequestId = Capture.RequestId;
	const int64 Ticket = Capture.Ticket;
	Captures.Add(RequestId, MoveTemp(Capture));
	CaptureOrder.Add(RequestId);

	const FDocSystemResult Begun = GetImageSource()->BeginCapture(World, Captures[RequestId].Request, Ticket, this);
	if (!Begun.IsSuccess())
	{
		Finish(Captures[RequestId], EDocPhotoCaptureState::Failed, Begun.ToString());
		OutRequestId = RequestId;
		return Begun;
	}
	FActiveCapture& Stored = Captures[RequestId];
	if (Stored.State == EDocPhotoCaptureState::PendingRender && !Cast<UDocPhotoSyntheticImageSource>(ImageSource))
	{
		Stored.State = EDocPhotoCaptureState::PendingReadback;
	}
	OutRequestId = RequestId;
	return FDocSystemResult::MakeSuccess();
}

UDocPhotographySubsystem::FActiveCapture* UDocPhotographySubsystem::FindByTicket(int64 Ticket)
{
	for (TPair<FGuid, FActiveCapture>& Kvp : Captures)
	{
		if (Kvp.Value.Ticket == Ticket)
		{
			return &Kvp.Value;
		}
	}
	return nullptr;
}

bool UDocPhotographySubsystem::SubmitPixels(int64 Ticket, int32 Width, int32 Height, const TArray<FColor>& Pixels, double CapturedTimeSeconds)
{
	FActiveCapture* Capture = FindByTicket(Ticket);
	if (!Capture || (Capture->State != EDocPhotoCaptureState::PendingRender && Capture->State != EDocPhotoCaptureState::PendingReadback))
	{
		return false; // late or unknown callback: ignored
	}
	if (!IsWorldAlive(*Capture))
	{
		Finish(*Capture, EDocPhotoCaptureState::Failed, TEXT("WorldLost"));
		return false;
	}
	if (Width != Capture->Request.Width || Height != Capture->Request.Height || Pixels.Num() != Width * Height)
	{
		Finish(*Capture, EDocPhotoCaptureState::Failed, TEXT("ReadbackSizeMismatch"));
		return false;
	}
	Capture->Width = Width;
	Capture->Height = Height;
	Capture->Pixels = Pixels; // immutable copy owned by this capture
	Capture->CapturedTime = CapturedTimeSeconds;
	Capture->State = EDocPhotoCaptureState::Encoding;
	return true;
}

bool UDocPhotographySubsystem::FailCapture(int64 Ticket, const FString& Reason)
{
	FActiveCapture* Capture = FindByTicket(Ticket);
	if (!Capture || IsTerminal(Capture->State))
	{
		return false;
	}
	Finish(*Capture, EDocPhotoCaptureState::Failed, Reason);
	return true;
}

void UDocPhotographySubsystem::Finish(FActiveCapture& Capture, EDocPhotoCaptureState State, const FString& Reason)
{
	Capture.State = State;
	Capture.FailureReason = Reason;
	Capture.Pixels.Empty(); // release the pixel copy
}

void UDocPhotographySubsystem::Pump()
{
	if (ImageSource)
	{
		ImageSource->Poll();
	}
	const TArray<FGuid> Order = CaptureOrder;
	for (const FGuid& Id : Order)
	{
		FActiveCapture* Capture = Captures.Find(Id);
		if (!Capture || IsTerminal(Capture->State))
		{
			continue;
		}
		if (!IsWorldAlive(*Capture))
		{
			if (ImageSource)
			{
				ImageSource->CancelCapture(Capture->Ticket);
			}
			Finish(*Capture, EDocPhotoCaptureState::Failed, TEXT("WorldLost"));
			continue;
		}
		if (Capture->State == EDocPhotoCaptureState::Encoding)
		{
			EncodeAndCommit(*Capture);
		}
	}
	PruneFinished();
}

void UDocPhotographySubsystem::EncodeAndCommit(FActiveCapture& Capture)
{
	// Encode the immutable copy.
	TArray64<uint8> Encoded;
	FImageUtils::PNGCompressImageArray(Capture.Width, Capture.Height, TArrayView64<const FColor>(Capture.Pixels.GetData(), Capture.Pixels.Num()), Encoded);
	Capture.Pixels.Empty();
	if (Encoded.Num() == 0)
	{
		Finish(Capture, EDocPhotoCaptureState::Failed, TEXT("EncodingFailed"));
		return;
	}
	Capture.State = EDocPhotoCaptureState::Committing;

	uint8 Digest[20];
	FSHA1::HashBuffer(Encoded.GetData(), static_cast<uint64>(Encoded.Num()), Digest);
	const FString Hash = BytesToHex(Digest, 20);

	// Frame coherence: pixels too late, or subjects moved, mean the snapshot does not describe the image.
	EDocPhotoEvaluationStatus Status = EDocPhotoEvaluationStatus::NotEvaluated;
	FString Reason;
	if (Capture.CapturedTime - Capture.RequestedTime > Capture.Request.MaxTimeToleranceSeconds)
	{
		Status = EDocPhotoEvaluationStatus::Unavailable;
		Reason = FString::Printf(TEXT("CaptureTooLate: %.3fs after the snapshot"), Capture.CapturedTime - Capture.RequestedTime);
	}
	bool bAnyStaleInFrame = false;
	for (int32 i = 0; i < Capture.Observations.Num(); ++i)
	{
		FDocPhotoSubjectObservation& Obs = Capture.Observations[i];
		const UDocPhotographableComponent* Subject = Capture.SubjectRefs.IsValidIndex(i) ? Capture.SubjectRefs[i].Get() : nullptr;
		if (!Subject || FVector::Dist(Subject->GetComponentLocation(), Obs.WorldLocation) > Capture.Request.PoseToleranceCm)
		{
			Obs.bPoseStale = true;
			bAnyStaleInFrame |= Obs.InFrameSampleCount > 0;
		}
	}
	if (bAnyStaleInFrame && Status != EDocPhotoEvaluationStatus::Unavailable)
	{
		Status = EDocPhotoEvaluationStatus::Unavailable;
		Reason = TEXT("SubjectMovedDuringCapture");
	}

	FDocPhotoRecord Record;
	Record.PhotoId = FGuid::NewGuid();
	Record.RequestId = Capture.RequestId;
	Record.OwnerScope = Capture.Request.OwnerScope;
	Record.WorldName = Capture.WorldName;
	Record.Timestamp = Capture.RequestedTime;
	Record.CapturedTimestamp = Capture.CapturedTime;
	Record.SubjectSnapshotRevision = Capture.SnapshotRevision;
	Record.CameraLocation = Capture.Request.CameraLocation;
	Record.CameraRotation = Capture.Request.CameraRotation;
	Record.FOVDegrees = Capture.Request.FOVDegrees;
	Record.AspectRatio = Capture.Request.AspectRatio;
	Record.ImageWidth = Capture.Width;
	Record.ImageHeight = Capture.Height;
	Record.ImageFormat = TEXT("PNG");
	Record.ImageHash = Hash;
	Record.EncodedBytes = Encoded.Num();
	Record.Observations = Capture.Observations;

	if (UDocPhotoEvaluationProfile* Profile = Capture.Profile.Get())
	{
		Record.ProfileId = Profile->ProfileId;
		Record.ProfileVersion = Profile->ProfileVersion;
		if (Status != EDocPhotoEvaluationStatus::Unavailable)
		{
			Record.bCriteriaMet = Profile->EvaluateObservations(Record.Observations, Record.EvaluationScore, Reason);
			Status = Record.bCriteriaMet ? EDocPhotoEvaluationStatus::Satisfied : EDocPhotoEvaluationStatus::NotSatisfied;
		}
	}
	else if (Status != EDocPhotoEvaluationStatus::Unavailable)
	{
		Reason = TEXT("NoEvaluationProfile");
	}
	Record.EvaluationStatus = Status;
	Record.EvaluationReason = Reason;

	// Quotas are re-checked with the real encoded size, before anything is written.
	if (CountOwnerPhotos(Record.OwnerScope) >= GalleryQuota.MaxPhotos)
	{
		Finish(Capture, EDocPhotoCaptureState::Failed, TEXT("GalleryFull"));
		return;
	}
	if (CountOwnerBytes(Record.OwnerScope) + Record.EncodedBytes > GalleryQuota.MaxBytes)
	{
		Finish(Capture, EDocPhotoCaptureState::Failed, TEXT("ByteQuotaExceeded"));
		return;
	}

	// Blob first, then the record that promises it.
	Record.BlobKey = FString::Printf(TEXT("%s/%s.png"), *OwnerKey(Record.OwnerScope), *Record.PhotoId.ToString(EGuidFormats::Digits));
	FString WriteError;
	UDocPhotoBlobStore* Store = GetBlobStore();
	if (!Store->WriteBlob(Record.BlobKey, Encoded, WriteError))
	{
		Store->DeleteBlob(Record.BlobKey); // no partial/orphan blob, no success-shaped record
		Finish(Capture, EDocPhotoCaptureState::Failed, FString::Printf(TEXT("StorageWriteFailed: %s"), *WriteError));
		return;
	}
	Record.StorageStatus = EDocPhotoStorageStatus::Available;

	GalleryRecords.Add(Record.PhotoId, Record);
	Capture.PhotoId = Record.PhotoId;
	Capture.State = EDocPhotoCaptureState::Committed;

	OnPhotoCaptured.Broadcast(Record);
	OnPhotoCapturedNative.Broadcast(Record);
}

void UDocPhotographySubsystem::PruneFinished()
{
	int32 Terminal = 0;
	for (const FGuid& Id : CaptureOrder)
	{
		const FActiveCapture* C = Captures.Find(Id);
		Terminal += (C && IsTerminal(C->State)) ? 1 : 0;
	}
	for (int32 i = 0; i < CaptureOrder.Num() && Terminal > DocPhotoPrivate::MaxRememberedCaptures; )
	{
		const FActiveCapture* C = Captures.Find(CaptureOrder[i]);
		if (C && IsTerminal(C->State))
		{
			Captures.Remove(CaptureOrder[i]);
			CaptureOrder.RemoveAt(i);
			--Terminal;
		}
		else
		{
			++i;
		}
	}
}

FDocSystemResult UDocPhotographySubsystem::CancelCapture(const FGuid& RequestId)
{
	FActiveCapture* Capture = Captures.Find(RequestId);
	if (!Capture)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown capture"));
	}
	if (Capture->State == EDocPhotoCaptureState::Committed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Already committed; delete the photo instead"));
	}
	if (IsTerminal(Capture->State))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Capture already finished"));
	}
	if (ImageSource)
	{
		ImageSource->CancelCapture(Capture->Ticket);
	}
	Finish(*Capture, EDocPhotoCaptureState::Cancelled, TEXT("Cancelled"));
	return FDocSystemResult::MakeSuccess();
}

bool UDocPhotographySubsystem::QueryCapture(const FGuid& RequestId, FDocPhotoCaptureStatus& OutStatus) const
{
	if (const FActiveCapture* Capture = Captures.Find(RequestId))
	{
		OutStatus.RequestId = RequestId;
		OutStatus.State = Capture->State;
		OutStatus.PhotoId = Capture->PhotoId;
		OutStatus.FailureReason = Capture->FailureReason;
		OutStatus.Ticket = Capture->Ticket;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Gallery
// ---------------------------------------------------------------------------------------------

bool UDocPhotographySubsystem::QueryPhoto(const FDocOwnerScope& Owner, const FGuid& PhotoId, FDocPhotoRecord& OutRecord) const
{
	const FDocPhotoRecord* Found = GalleryRecords.Find(PhotoId);
	if (!Found || Found->OwnerScope != Owner)
	{
		return false; // another owner's photo is indistinguishable from a missing one
	}
	OutRecord = *Found;
	return true;
}

TArray<FDocPhotoRecord> UDocPhotographySubsystem::ListPhotos(const FDocOwnerScope& ForOwner) const
{
	TArray<FDocPhotoRecord> Result;
	for (const TPair<FGuid, FDocPhotoRecord>& Kvp : GalleryRecords)
	{
		if (Kvp.Value.OwnerScope == ForOwner)
		{
			Result.Add(Kvp.Value);
		}
	}
	Result.Sort([](const FDocPhotoRecord& A, const FDocPhotoRecord& B)
	{
		return A.Timestamp == B.Timestamp ? A.PhotoId.ToString() < B.PhotoId.ToString() : A.Timestamp < B.Timestamp;
	});
	return Result;
}

FDocSystemResult UDocPhotographySubsystem::LoadPhotoImage(const FDocOwnerScope& Owner, const FGuid& PhotoId, TArray64<uint8>& OutEncoded)
{
	FDocPhotoRecord* Record = GalleryRecords.Find(PhotoId);
	if (!Record || Record->OwnerScope != Owner)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Photo not found"));
	}
	if (!GetBlobStore()->ReadBlob(Record->BlobKey, OutEncoded))
	{
		Record->StorageStatus = EDocPhotoStorageStatus::ImageUnavailable; // metadata kept
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("ImageUnavailable"));
	}
	if (OutEncoded.Num() != Record->EncodedBytes)
	{
		OutEncoded.Reset();
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Stored image size does not match the record"));
	}
	uint8 Digest[20];
	FSHA1::HashBuffer(OutEncoded.GetData(), static_cast<uint64>(OutEncoded.Num()), Digest);
	if (BytesToHex(Digest, 20) != Record->ImageHash)
	{
		OutEncoded.Reset();
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Stored image hash does not match the record"));
	}
	Record->StorageStatus = EDocPhotoStorageStatus::Available;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPhotographySubsystem::EvaluatePhotoRecord(const FDocOwnerScope& Owner, const FGuid& PhotoId, UDocPhotoEvaluationProfile* Profile, FDocPhotoRecord& OutRecord) const
{
	if (!QueryPhoto(Owner, PhotoId, OutRecord))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Photo not found"));
	}
	if (!Profile)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No profile"));
	}
	const FDocSystemResult Valid = Profile->ValidateProfile();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	OutRecord.ProfileId = Profile->ProfileId;
	OutRecord.ProfileVersion = Profile->ProfileVersion;
	if (OutRecord.EvaluationStatus == EDocPhotoEvaluationStatus::Unavailable)
	{
		return FDocSystemResult::MakeSuccess(); // incoherent captures stay unscored under any profile
	}
	OutRecord.bCriteriaMet = Profile->EvaluateObservations(OutRecord.Observations, OutRecord.EvaluationScore, OutRecord.EvaluationReason);
	OutRecord.EvaluationStatus = OutRecord.bCriteriaMet ? EDocPhotoEvaluationStatus::Satisfied : EDocPhotoEvaluationStatus::NotSatisfied;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPhotographySubsystem::DeletePhoto(const FDocOwnerScope& Owner, const FGuid& PhotoId)
{
	const FDocPhotoRecord* Record = GalleryRecords.Find(PhotoId);
	if (!Record || Record->OwnerScope != Owner)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Photo not found"));
	}
	GetBlobStore()->DeleteBlob(Record->BlobKey); // only the manifest-owned blob
	GalleryRecords.Remove(PhotoId);
	return FDocSystemResult::MakeSuccess();
}

void UDocPhotographySubsystem::SetGalleryQuota(const FDocPhotoGalleryQuota& NewQuota)
{
	GalleryQuota = NewQuota;
	GalleryQuota.MaxPhotos = FMath::Max(0, GalleryQuota.MaxPhotos);
	GalleryQuota.MaxBytes = FMath::Max<int64>(0, GalleryQuota.MaxBytes);
	GalleryQuota.MaxConcurrentCaptures = FMath::Max(1, GalleryQuota.MaxConcurrentCaptures);
	GalleryQuota.MaxImageDimension = FMath::Clamp(GalleryQuota.MaxImageDimension, 1, 8192);
	GalleryQuota.MaxSubjectsPerCapture = FMath::Max(0, GalleryQuota.MaxSubjectsPerCapture);
	GalleryQuota.MaxSamplesPerSubject = FMath::Max(0, GalleryQuota.MaxSamplesPerSubject);
	// Existing photos are never evicted by a smaller quota; new captures are refused until there is room.
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

FDocPhotoGallerySnapshot UDocPhotographySubsystem::CaptureGalleryMetadata(const FDocOwnerScope& Owner) const
{
	FDocPhotoGallerySnapshot Snapshot;
	Snapshot.OwnerScope = Owner;
	Snapshot.Records = ListPhotos(Owner);
	return Snapshot;
}

FDocSystemResult UDocPhotographySubsystem::StageRestore(const FDocPhotoGallerySnapshot& Snapshot)
{
	if (Snapshot.SchemaVersion != FDocPhotoGallerySnapshot::CurrentSchemaVersion || !Snapshot.OwnerScope.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported gallery schema or owner"));
	}
	if (Snapshot.Records.Num() > GalleryQuota.MaxPhotos)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Gallery exceeds the photo quota"));
	}
	TSet<FGuid> Seen;
	for (const FDocPhotoRecord& R : Snapshot.Records)
	{
		if (R.SchemaVersion != FDocPhotoRecord::CurrentSchemaVersion || !R.PhotoId.IsValid() || Seen.Contains(R.PhotoId)
			|| R.OwnerScope != Snapshot.OwnerScope
			|| R.ImageWidth <= 0 || R.ImageHeight <= 0 || R.ImageWidth > GalleryQuota.MaxImageDimension || R.ImageHeight > GalleryQuota.MaxImageDimension
			|| R.EncodedBytes < 0 || !UDocPhotoBlobStore::IsValidKey(R.BlobKey))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Invalid photo record %s"), *R.PhotoId.ToString()));
		}
		Seen.Add(R.PhotoId);
	}

	for (auto It = GalleryRecords.CreateIterator(); It; ++It)
	{
		if (It->Value.OwnerScope == Snapshot.OwnerScope)
		{
			It.RemoveCurrent();
		}
	}
	UDocPhotoBlobStore* Store = GetBlobStore();
	for (FDocPhotoRecord R : Snapshot.Records)
	{
		R.StorageStatus = Store->BlobExists(R.BlobKey) ? EDocPhotoStorageStatus::Available : EDocPhotoStorageStatus::ImageUnavailable;
		GalleryRecords.Add(R.PhotoId, R);
	}
	return FDocSystemResult::MakeSuccess();
}
