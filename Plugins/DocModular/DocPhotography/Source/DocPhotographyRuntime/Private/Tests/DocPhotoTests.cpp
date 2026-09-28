#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocPhotoTypes.h"
#include "DocPhotoEvaluationProfile.h"
#include "DocPhotographableComponent.h"
#include "DocPhotographySubsystem.h"
#include "DocPhotoProviders.h"
#include "GameplayTagsManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

namespace DocPhotoTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayTag Tag(const FName& TagName = TEXT("Doc.Test.PhotoSubject"))
	{
		UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();
		FGameplayTag T = Mgr.RequestGameplayTag(TagName, false);
		return T.IsValid() ? T : Mgr.AddNativeGameplayTag(TagName);
	}

	FDocOwnerScope Owner(uint32 N)
	{
		return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(N, 0, 0, 1));
	}

	struct FFixture
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocPhotographySubsystem> Subsystem;
		UDocPhotoSyntheticImageSource* Source = nullptr;
		UDocPhotoMemoryBlobStore* Store = nullptr;

		explicit FFixture(UDocPhotoMemoryBlobStore* SharedStore = nullptr)
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			Subsystem.Reset(NewObject<UDocPhotographySubsystem>(Player.Get()));
			Source = NewObject<UDocPhotoSyntheticImageSource>(Subsystem.Get());
			Store = SharedStore ? SharedStore : NewObject<UDocPhotoMemoryBlobStore>(Subsystem.Get());
			Subsystem->SetImageSource(Source);
			Subsystem->SetBlobStore(Store);
		}

		UDocPhotographySubsystem* operator->() const { return Subsystem.Get(); }
	};

	UDocPhotographableComponent* SpawnSubject(FDocScopedTestWorld& TW, FName Id, const FVector& Location, FGameplayTag SubjectTag = FGameplayTag(), const FRotator& Rotation = FRotator::ZeroRotator)
	{
		AActor* Actor = TW.Spawn<AActor>(Location);
		UDocPhotographableComponent* Comp = NewObject<UDocPhotographableComponent>(Actor);
		Comp->SubjectId = Id;
		Comp->SubjectTag = SubjectTag.IsValid() ? SubjectTag : Tag();
		Actor->SetRootComponent(Comp);
		Comp->RegisterComponent();
		Actor->SetActorLocationAndRotation(Location, Rotation);
		return Comp;
	}

	FDocPhotoCaptureRequest Request(const FDocOwnerScope& O, const FVector& CamLoc = FVector::ZeroVector, const FRotator& CamRot = FRotator::ZeroRotator, int32 W = 64, int32 H = 64)
	{
		FDocPhotoCaptureRequest R;
		R.OwnerScope = O;
		R.CameraLocation = CamLoc;
		R.CameraRotation = CamRot;
		R.FOVDegrees = 90.0f;
		R.Width = W;
		R.Height = H;
		return R;
	}

	UDocPhotoEvaluationProfile* Profile(FGameplayTag Required = FGameplayTag(), float MinVisible = 0.2f)
	{
		UDocPhotoEvaluationProfile* P = NewObject<UDocPhotoEvaluationProfile>(GetTransientPackage());
		P->ProfileId = TEXT("TestProfile");
		P->RequiredTag = Required;
		P->MinVisibleFraction = MinVisible;
		P->MinBoundsInFrameFraction = 0.5f;
		return P;
	}

	/** Request + pump; returns the committed record (or false with the capture status). */
	bool Take(FFixture& F, UWorld* World, const FDocPhotoCaptureRequest& Req, UDocPhotoEvaluationProfile* P, FDocPhotoRecord& OutRecord, FDocPhotoCaptureStatus* OutStatus = nullptr)
	{
		FGuid RequestId;
		const FDocSystemResult Result = F->RequestPhoto(World, Req, P, RequestId);
		F->Pump();
		FDocPhotoCaptureStatus Status;
		F->QueryCapture(RequestId, Status);
		if (OutStatus)
		{
			*OutStatus = Status;
		}
		return Result.IsSuccess() && Status.State == EDocPhotoCaptureState::Committed && F->QueryPhoto(Req.OwnerScope, Status.PhotoId, OutRecord);
	}

	const FDocPhotoSubjectObservation* Find(const FDocPhotoRecord& R, FName Id)
	{
		return R.Observations.FindByPredicate([Id](const FDocPhotoSubjectObservation& O) { return O.SubjectId == Id; });
	}
}

// PHO-01: Metrics use the capture projection/aspect, including off-screen/behind cases
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoCameraProjectionTest, FAutomationTestBase, "Doc.Photo.CameraProjection", DocPhotoTests::Flags)
bool FDocPhotoCameraProjectionTest::RunTest(const FString& Parameters)
{
	FVector2D UV;
	bool bIn = false, bBehind = false;
	const FVector Cam(0.0, 0.0, 0.0);

	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(500, 0, 0), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Centre projects to (0.5, 0.5)"), bIn && UV.Equals(FVector2D(0.5, 0.5), 1e-4));
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(500, 250, 0), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Horizontal FOV: y = x/2 lands at u = 0.75"), FMath::IsNearlyEqual(UV.X, 0.75, 1e-4));
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(500, 0, 250), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Aspect 1: z = x/2 lands at v = 0.25"), FMath::IsNearlyEqual(UV.Y, 0.25, 1e-4));
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(500, 0, 250), Cam, FRotator::ZeroRotator, 90.0f, 2.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Aspect 2 (wide): the same point reaches the top edge"), FMath::IsNearlyEqual(UV.Y, 0.0, 1e-4));
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(500, 600, 0), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Off-screen point is in front but out of frame"), !bIn && !bBehind);
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(-100, 0, 0), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Behind the camera"), bBehind && !bIn);
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(0.5, 0, 0), Cam, FRotator::ZeroRotator, 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Inside the near plane counts as behind"), bBehind);
	UDocPhotographySubsystem::ProjectWorldPointToScreenUV(FVector(0, 500, 0), Cam, FRotator(0, 90, 0), 90.0f, 1.0f, UV, bIn, bBehind);
	TestTrue(TEXT("Uses the capture camera's rotation"), bIn && UV.Equals(FVector2D(0.5, 0.5), 1e-4));

	// Through the pipeline, with the capture's own aspect (128x64 = 2:1).
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	DocPhotoTests::SpawnSubject(TW, TEXT("Front"), FVector(500, 0, 0));
	DocPhotoTests::SpawnSubject(TW, TEXT("Behind"), FVector(-500, 0, 0));
	DocPhotoTests::SpawnSubject(TW, TEXT("Edge"), FVector(500, 520, 0));
	FDocPhotoRecord Record;
	TestTrue(TEXT("Captured"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1), Cam, FRotator::ZeroRotator, 128, 64), nullptr, Record));
	TestTrue(TEXT("Record stores the capture aspect"), FMath::IsNearlyEqual(Record.AspectRatio, 2.0f, 1e-4f));
	const FDocPhotoSubjectObservation* Front = DocPhotoTests::Find(Record, TEXT("Front"));
	const FDocPhotoSubjectObservation* Behind = DocPhotoTests::Find(Record, TEXT("Behind"));
	const FDocPhotoSubjectObservation* Edge = DocPhotoTests::Find(Record, TEXT("Edge"));
	TestTrue(TEXT("Front subject in frame"), Front && Front->bInFrustum && Front->InFrameSampleCount == Front->SampleCount && FMath::IsNearlyEqual(Front->BoundsInFrameFraction, 1.0f, 1e-3f));
	TestTrue(TEXT("Behind subject flagged, nothing in frame"), Behind && Behind->bIsBehindCamera && Behind->InFrameSampleCount == 0);
	TestTrue(TEXT("Edge subject partly cropped"), Edge && Edge->BoundsInFrameFraction > 0.0f && Edge->BoundsInFrameFraction < 1.0f);
	TestTrue(TEXT("Cropped bounds clipped to the image"), Edge && Edge->ScreenBounds.Max.X <= 1.0 + 1e-6);

	return true;
}

// PHO-02: Sample-based visibility returns declared estimates, not false pixel accuracy
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoOcclusionEstimateTest, FAutomationTestBase, "Doc.Photo.OcclusionEstimate", DocPhotoTests::Flags)
bool FDocPhotoOcclusionEstimateTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	DocPhotoTests::SpawnSubject(TW, TEXT("Statue"), FVector(500, 0, 0));

	FDocPhotoRecord Record;
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Record);
	const FDocPhotoSubjectObservation* Clear = DocPhotoTests::Find(Record, TEXT("Statue"));
	TestTrue(TEXT("Unobstructed: every sample visible"), Clear && Clear->VisibleSampleCount == Clear->SampleCount && FMath::IsNearlyEqual(Clear->EstimatedVisibleFraction, 1.0f));

	// A deterministic occluder over the upper half: the two upper samples are blocked.
	F->SetVisibilityTester([](UWorld*, const FVector&, const FVector& To, const AActor*) { return To.Z < 10.0; });
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Record);
	const FDocPhotoSubjectObservation* Half = DocPhotoTests::Find(Record, TEXT("Statue"));
	TestTrue(TEXT("Five samples, all in frame"), Half && Half->SampleCount == 5 && Half->InFrameSampleCount == 5);
	TestTrue(TEXT("Three of five samples visible"), Half && Half->VisibleSampleCount == 3);
	TestTrue(TEXT("Estimate is the declared sample fraction (0.6), not a pixel measure"), Half && FMath::IsNearlyEqual(Half->EstimatedVisibleFraction, 0.6f, 1e-4f));

	FDocPhotoRecord Strict, Lenient;
	F->EvaluatePhotoRecord(DocPhotoTests::Owner(1), Record.PhotoId, DocPhotoTests::Profile(FGameplayTag(), 0.7f), Strict);
	F->EvaluatePhotoRecord(DocPhotoTests::Owner(1), Record.PhotoId, DocPhotoTests::Profile(FGameplayTag(), 0.5f), Lenient);
	TestFalse(TEXT("0.6 fails a 0.7 visibility requirement"), Strict.bCriteriaMet);
	TestTrue(TEXT("Failure reason names occlusion"), Strict.EvaluationReason.Contains(TEXT("Occluded")));
	TestTrue(TEXT("0.6 passes a 0.5 visibility requirement"), Lenient.bCriteriaMet);

	return true;
}

// PHO-03: Moving subjects and delayed capture enforce snapshot/time tolerance
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoFrameCoherenceTest, FAutomationTestBase, "Doc.Photo.FrameCoherence", DocPhotoTests::Flags)
bool FDocPhotoFrameCoherenceTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	UDocPhotographableComponent* Runner = DocPhotoTests::SpawnSubject(TW, TEXT("Runner"), FVector(500, 0, 0));
	UDocPhotoEvaluationProfile* P = DocPhotoTests::Profile();

	// Coherent capture is scored.
	FDocPhotoRecord Record;
	TestTrue(TEXT("Still subject captured"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), P, Record));
	TestEqual(TEXT("Coherent capture is evaluated"), Record.EvaluationStatus, EDocPhotoEvaluationStatus::Satisfied);

	// Subject moves between the snapshot and the pixels.
	F.Source->bDeferred = true;
	FGuid RequestId;
	F->RequestPhoto(TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), P, RequestId);
	Runner->GetOwner()->SetActorLocation(FVector(500, 100, 0));
	F->Pump();
	FDocPhotoCaptureStatus Status;
	F->QueryCapture(RequestId, Status);
	TestEqual(TEXT("Image still committed"), Status.State, EDocPhotoCaptureState::Committed);
	F->QueryPhoto(DocPhotoTests::Owner(1), Status.PhotoId, Record);
	TestEqual(TEXT("Not scored against moved subject"), Record.EvaluationStatus, EDocPhotoEvaluationStatus::Unavailable);
	TestEqual(TEXT("Reason declared"), Record.EvaluationReason, FString(TEXT("SubjectMovedDuringCapture")));
	TestTrue(TEXT("Observation flagged stale"), DocPhotoTests::Find(Record, TEXT("Runner")) && DocPhotoTests::Find(Record, TEXT("Runner"))->bPoseStale);
	TestFalse(TEXT("Criteria never met by an incoherent capture"), Record.bCriteriaMet);
	FDocPhotoRecord Reevaluated;
	F->EvaluatePhotoRecord(DocPhotoTests::Owner(1), Record.PhotoId, P, Reevaluated);
	TestEqual(TEXT("Re-evaluation keeps it unavailable"), Reevaluated.EvaluationStatus, EDocPhotoEvaluationStatus::Unavailable);

	// Pixels arriving later than the time tolerance.
	F.Source->bDeferred = false;
	F.Source->SimulatedDelaySeconds = 2.0f;
	TestTrue(TEXT("Late capture committed"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), P, Record));
	TestEqual(TEXT("Late pixels not scored"), Record.EvaluationStatus, EDocPhotoEvaluationStatus::Unavailable);
	TestTrue(TEXT("Late reason declared"), Record.EvaluationReason.StartsWith(TEXT("CaptureTooLate")));
	TestTrue(TEXT("Both timestamps recorded"), Record.CapturedTimestamp - Record.Timestamp > 1.9);

	return true;
}

// PHO-04: Valid image/no qualifying subject remains a successful capture
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoCaptureVsEvaluationTest, FAutomationTestBase, "Doc.Photo.CaptureVsEvaluation", DocPhotoTests::Flags)
bool FDocPhotoCaptureVsEvaluationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	DocPhotoTests::SpawnSubject(TW, TEXT("Rock"), FVector(500, 0, 0), DocPhotoTests::Tag(TEXT("Doc.Test.Rock")));

	FDocPhotoRecord Record;
	TestTrue(TEXT("Capture succeeds"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), DocPhotoTests::Profile(DocPhotoTests::Tag(TEXT("Doc.Test.Bird"))), Record));
	TestEqual(TEXT("Image stored"), Record.StorageStatus, EDocPhotoStorageStatus::Available);
	TestEqual(TEXT("Evaluation is a separate result"), Record.EvaluationStatus, EDocPhotoEvaluationStatus::NotSatisfied);
	TestFalse(TEXT("Criteria not met"), Record.bCriteriaMet);

	TArray64<uint8> Png;
	TestTrue(TEXT("Stored image loads and verifies"), F->LoadPhotoImage(DocPhotoTests::Owner(1), Record.PhotoId, Png).IsSuccess());
	TestTrue(TEXT("Real PNG bytes"), Png.Num() > 8 && Png[0] == 0x89 && Png[1] == 'P' && Png[2] == 'N' && Png[3] == 'G');
	TestEqual(TEXT("Recorded size matches"), Record.EncodedBytes, static_cast<int64>(Png.Num()));
	TestEqual(TEXT("Hash is a SHA-1 hex digest"), Record.ImageHash.Len(), 40);

	return true;
}

// PHO-05: Cancellation and player/world loss release resources and ignore late callbacks
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoAsyncCancellationTest, FAutomationTestBase, "Doc.Photo.AsyncCancellation", DocPhotoTests::Flags)
bool FDocPhotoAsyncCancellationTest::RunTest(const FString& Parameters)
{
	DocPhotoTests::FFixture F;
	F.Source->bDeferred = true;
	{
		FDocScopedTestWorld TW;
		FGuid RequestId;
		TestTrue(TEXT("Deferred request accepted"), F->RequestPhoto(TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, RequestId).IsSuccess());
		FDocPhotoCaptureStatus Status;
		F->QueryCapture(RequestId, Status);
		TestEqual(TEXT("Pending render"), Status.State, EDocPhotoCaptureState::PendingRender);
		TestTrue(TEXT("Cancel"), F->CancelCapture(RequestId).IsSuccess());
		TestEqual(TEXT("Source released the ticket"), F.Source->GetPendingCount(), 0);
		TArray<FColor> Pixels;
		Pixels.Init(FColor::Red, 64 * 64);
		TestFalse(TEXT("Late pixels for a cancelled ticket are ignored"), F->SubmitPixels(Status.Ticket, 64, 64, Pixels, 0.0));
		F->Pump();
		F->QueryCapture(RequestId, Status);
		TestEqual(TEXT("Stays cancelled"), Status.State, EDocPhotoCaptureState::Cancelled);
		TestEqual(TEXT("No record"), F->ListPhotos(DocPhotoTests::Owner(1)).Num(), 0);
		TestEqual(TEXT("Cancelling again is NoChange"), F->CancelCapture(RequestId).Outcome, EDocResultOutcome::NoChange);

		// Committed captures cannot be "un-taken".
		F.Source->bDeferred = false;
		FDocPhotoRecord Record;
		FDocPhotoCaptureStatus Done;
		DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Record, &Done);
		TestEqual(TEXT("Cancel after commit is a Conflict"), F->CancelCapture(Done.RequestId).Outcome, EDocResultOutcome::Conflict);
		TestEqual(TEXT("Committed photo remains"), F->ListPhotos(DocPhotoTests::Owner(1)).Num(), 1);
		F.Source->bDeferred = true;
	}

	// World loss while pending.
	FGuid Orphan;
	{
		FDocScopedTestWorld Doomed;
		F->RequestPhoto(Doomed.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Orphan);
	}
	F->Pump();
	FDocPhotoCaptureStatus Lost;
	F->QueryCapture(Orphan, Lost);
	TestEqual(TEXT("World loss fails the capture"), Lost.State, EDocPhotoCaptureState::Failed);
	TestEqual(TEXT("Reason WorldLost"), Lost.FailureReason, FString(TEXT("WorldLost")));
	TestEqual(TEXT("No record from a lost world"), F->ListPhotos(DocPhotoTests::Owner(1)).Num(), 1);
	TestEqual(TEXT("No active captures left"), F->GetActiveCaptureCount(), 0);

	return true;
}

// PHO-06: Write failure cannot produce a committed available-image record
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoStorageCommitTest, FAutomationTestBase, "Doc.Photo.StorageCommit", DocPhotoTests::Flags)
bool FDocPhotoStorageCommitTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	int32 Events = 0;
	bool bBlobExistedAtEvent = false;
	UDocPhotoMemoryBlobStore* Store = F.Store;
	F->OnPhotoCapturedNative.AddLambda([&Events, &bBlobExistedAtEvent, Store](const FDocPhotoRecord& R)
	{
		++Events;
		bBlobExistedAtEvent = Store->BlobExists(R.BlobKey);
	});

	F.Store->bFailWrites = true;
	FDocPhotoRecord Record;
	FDocPhotoCaptureStatus Status;
	TestFalse(TEXT("Write failure: no committed photo"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Record, &Status));
	TestEqual(TEXT("Capture failed"), Status.State, EDocPhotoCaptureState::Failed);
	TestTrue(TEXT("Failure reason names storage"), Status.FailureReason.StartsWith(TEXT("StorageWriteFailed")));
	TestEqual(TEXT("No record"), F->ListPhotos(DocPhotoTests::Owner(1)).Num(), 0);
	TestEqual(TEXT("No orphan blob"), F.Store->GetBlobCount(), 0);
	TestEqual(TEXT("No success event"), Events, 0);

	F.Store->bFailWrites = false;
	TestTrue(TEXT("Write succeeds"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1)), nullptr, Record, &Status));
	TestEqual(TEXT("One event"), Events, 1);
	TestTrue(TEXT("Blob existed before the record was announced"), bBlobExistedAtEvent);
	TestTrue(TEXT("Record references a stored blob"), F.Store->BlobExists(Record.BlobKey));
	TestTrue(TEXT("Blob key is a controlled relative key"), UDocPhotoBlobStore::IsValidKey(Record.BlobKey));
	TestFalse(TEXT("Path traversal keys are refused"), UDocPhotoBlobStore::IsValidKey(TEXT("../../Windows/evil.png")));
	TestFalse(TEXT("Absolute keys are refused"), UDocPhotoBlobStore::IsValidKey(TEXT("C:/Users/me/photo.png")));

	return true;
}

// PHO-07: Oversized images, concurrency, and byte quotas are enforced
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoGalleryBoundsTest, FAutomationTestBase, "Doc.Photo.GalleryBounds", DocPhotoTests::Flags)
bool FDocPhotoGalleryBoundsTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	const FDocOwnerScope Me = DocPhotoTests::Owner(1);
	FGuid Id;

	TestEqual(TEXT("Oversized image refused before any work"),
		F->RequestPhoto(TW.World, DocPhotoTests::Request(Me, FVector::ZeroVector, FRotator::ZeroRotator, 10000, 64), nullptr, Id).Outcome, EDocResultOutcome::InvalidInput);
	FDocPhotoCaptureRequest NoOwner = DocPhotoTests::Request(FDocOwnerScope());
	TestEqual(TEXT("Missing owner refused"), F->RequestPhoto(TW.World, NoOwner, nullptr, Id).Outcome, EDocResultOutcome::InvalidInput);

	FDocPhotoGalleryQuota Quota;
	Quota.MaxConcurrentCaptures = 1;
	Quota.MaxPhotos = 2;
	F->SetGalleryQuota(Quota);

	F.Source->bDeferred = true;
	TestTrue(TEXT("First concurrent capture accepted"), F->RequestPhoto(TW.World, DocPhotoTests::Request(Me), nullptr, Id).IsSuccess());
	TestEqual(TEXT("Second concurrent capture refused"), F->RequestPhoto(TW.World, DocPhotoTests::Request(Me), nullptr, Id).Outcome, EDocResultOutcome::Conflict);
	F->Pump();
	F.Source->bDeferred = false;

	FDocPhotoRecord Record;
	TestTrue(TEXT("Second photo"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(Me), nullptr, Record));
	TestEqual(TEXT("Full gallery refuses new captures"), F->RequestPhoto(TW.World, DocPhotoTests::Request(Me), nullptr, Id).Outcome, EDocResultOutcome::Failed);
	TestEqual(TEXT("Existing photos are never evicted"), F->ListPhotos(Me).Num(), 2);
	TestTrue(TEXT("Another owner has its own quota"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(2)), nullptr, Record));

	Quota.MaxPhotos = 10;
	Quota.MaxBytes = 16;
	F->SetGalleryQuota(Quota);
	FDocPhotoCaptureStatus Status;
	TestFalse(TEXT("Byte quota refuses the commit"), DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(3)), nullptr, Record, &Status));
	TestEqual(TEXT("Reason ByteQuotaExceeded"), Status.FailureReason, FString(TEXT("ByteQuotaExceeded")));
	TestEqual(TEXT("No record for the refused capture"), F->ListPhotos(DocPhotoTests::Owner(3)).Num(), 0);

	FDocPhotoGallerySnapshot Bad;
	Bad.OwnerScope = Me;
	FDocPhotoRecord Huge;
	Huge.PhotoId = FGuid::NewGuid();
	Huge.OwnerScope = Me;
	Huge.ImageWidth = 99999;
	Huge.ImageHeight = 10;
	Huge.BlobKey = TEXT("x/y.png");
	Bad.Records.Add(Huge);
	TestFalse(TEXT("Restore of an oversized record refused"), F->StageRestore(Bad).IsSuccess());
	TestEqual(TEXT("Gallery untouched"), F->ListPhotos(Me).Num(), 2);

	return true;
}

// PHO-08: Galleries and private subject metadata do not leak between owners
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoOwnerIsolationTest, FAutomationTestBase, "Doc.Photo.OwnerIsolation", DocPhotoTests::Flags)
bool FDocPhotoOwnerIsolationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	const FDocOwnerScope Alice = DocPhotoTests::Owner(1);
	const FDocOwnerScope Bob = DocPhotoTests::Owner(2);

	UDocPhotographableComponent* Diary = DocPhotoTests::SpawnSubject(TW, TEXT("Diary"), FVector(500, 0, 0));
	Diary->bIsPrivate = true;
	Diary->OwnerScope = Alice;
	DocPhotoTests::SpawnSubject(TW, TEXT("Tree"), FVector(500, 100, 0));

	FDocPhotoRecord AlicePhoto, BobPhoto;
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(Alice), nullptr, AlicePhoto);
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(Bob), nullptr, BobPhoto);
	TestNotNull(TEXT("Alice sees her private subject"), DocPhotoTests::Find(AlicePhoto, TEXT("Diary")));
	TestNull(TEXT("Bob's record has no trace of it"), DocPhotoTests::Find(BobPhoto, TEXT("Diary")));
	TestNotNull(TEXT("Public subject visible to Bob"), DocPhotoTests::Find(BobPhoto, TEXT("Tree")));

	FDocPhotoRecord Probe;
	TestFalse(TEXT("Bob cannot query Alice's photo"), F->QueryPhoto(Bob, AlicePhoto.PhotoId, Probe));
	TestEqual(TEXT("Bob lists only his own"), F->ListPhotos(Bob).Num(), 1);
	TArray64<uint8> Bytes;
	TestEqual(TEXT("Bob cannot load Alice's image"), F->LoadPhotoImage(Bob, AlicePhoto.PhotoId, Bytes).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Bob cannot delete Alice's photo"), F->DeletePhoto(Bob, AlicePhoto.PhotoId).Outcome, EDocResultOutcome::NotFound);
	TestTrue(TEXT("Alice's photo intact"), F->QueryPhoto(Alice, AlicePhoto.PhotoId, Probe));
	TestTrue(TEXT("Alice deletes her own"), F->DeletePhoto(Alice, AlicePhoto.PhotoId).IsSuccess());
	TestFalse(TEXT("Blob removed with the record"), F.Store->BlobExists(AlicePhoto.BlobKey));

	// Separate worlds keep separate subject registries (no process-global list).
	FDocScopedTestWorld Other;
	FDocPhotoRecord Empty;
	DocPhotoTests::Take(F, Other.World, DocPhotoTests::Request(Bob), nullptr, Empty);
	TestEqual(TEXT("Subjects of another world are not observed"), Empty.Observations.Num(), 0);

	return true;
}

// PHO-09: Missing images are reported without erasing unrelated valid records
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoRestoreMissingBlobTest, FAutomationTestBase, "Doc.Photo.RestoreMissingBlob", DocPhotoTests::Flags)
bool FDocPhotoRestoreMissingBlobTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	const FDocOwnerScope Me = DocPhotoTests::Owner(1);
	DocPhotoTests::SpawnSubject(TW, TEXT("Lighthouse"), FVector(500, 0, 0));

	FDocPhotoRecord First, Second;
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(Me), nullptr, First);
	DocPhotoTests::Take(F, TW.World, DocPhotoTests::Request(Me), nullptr, Second);
	const FDocPhotoGallerySnapshot Saved = F->CaptureGalleryMetadata(Me);
	TestEqual(TEXT("Two records saved"), Saved.Records.Num(), 2);

	F.Store->DeleteBlob(First.BlobKey); // lost outside the game

	DocPhotoTests::FFixture Loaded(F.Store);
	TestTrue(TEXT("Restore"), Loaded->StageRestore(Saved).IsSuccess());
	FDocPhotoRecord R1, R2;
	TestTrue(TEXT("Missing-image record kept"), Loaded->QueryPhoto(Me, First.PhotoId, R1));
	TestEqual(TEXT("Marked ImageUnavailable"), R1.StorageStatus, EDocPhotoStorageStatus::ImageUnavailable);
	TestEqual(TEXT("Its metadata survives"), R1.Observations.Num(), First.Observations.Num());
	TestTrue(TEXT("Unrelated record still available"), Loaded->QueryPhoto(Me, Second.PhotoId, R2) && R2.StorageStatus == EDocPhotoStorageStatus::Available);
	TArray64<uint8> Bytes;
	TestEqual(TEXT("Loading the missing image reports NotFound"), Loaded->LoadPhotoImage(Me, First.PhotoId, Bytes).Outcome, EDocResultOutcome::NotFound);
	TestTrue(TEXT("Loading the other image verifies"), Loaded->LoadPhotoImage(Me, Second.PhotoId, Bytes).IsSuccess());
	TestTrue(TEXT("Record not erased by the failed load"), Loaded->QueryPhoto(Me, First.PhotoId, R1));

	FDocPhotoGallerySnapshot Foreign = Saved;
	Foreign.Records[0].OwnerScope = DocPhotoTests::Owner(9);
	TestFalse(TEXT("A record of another owner in the snapshot is refused"), Loaded->StageRestore(Foreign).IsSuccess());
	TestEqual(TEXT("Existing gallery intact after refusal"), Loaded->ListPhotos(Me).Num(), 2);

	return true;
}

// PHO-10: Actual cooked scene capture/readback/encoding works with graphics enabled
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPhotoCookedRenderedCaptureTest, FAutomationTestBase, "Doc.Photo.CookedRenderedCapture", DocPhotoTests::Flags)
bool FDocPhotoCookedRenderedCaptureTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	DocPhotoTests::FFixture F;
	UDocSceneCapturePhotoSource* Scene = NewObject<UDocSceneCapturePhotoSource>(F.Subsystem.Get());
	F->SetImageSource(Scene);

	FGuid RequestId;
	const FDocSystemResult Result = F->RequestPhoto(TW.World, DocPhotoTests::Request(DocPhotoTests::Owner(1), FVector::ZeroVector, FRotator::ZeroRotator, 64, 64), nullptr, RequestId);
	if (!UDocSceneCapturePhotoSource::IsRenderingAvailable())
	{
		// Honest in NullRHI runs: no pixels are invented. The cooked graphics fixture remains a manual gate.
		TestEqual(TEXT("Scene capture reports Unsupported without a real RHI"), Result.Outcome, EDocResultOutcome::Unsupported);
		FDocPhotoCaptureStatus Status;
		F->QueryCapture(RequestId, Status);
		TestEqual(TEXT("Capture failed instead of faking an image"), Status.State, EDocPhotoCaptureState::Failed);
		TestEqual(TEXT("No record"), F->ListPhotos(DocPhotoTests::Owner(1)).Num(), 0);
		AddInfo(TEXT("Rendered capture not exercised: this run has no real RHI (graphics fixture required)."));
		return true;
	}

	TestTrue(TEXT("Scene capture started"), Result.IsSuccess());
	FDocPhotoCaptureStatus Status;
	for (int32 i = 0; i < 100; ++i)
	{
		FlushRenderingCommands();
		F->Pump();
		F->QueryCapture(RequestId, Status);
		if (Status.State == EDocPhotoCaptureState::Committed || Status.State == EDocPhotoCaptureState::Failed)
		{
			break;
		}
	}
	TestEqual(TEXT("Rendered capture committed"), Status.State, EDocPhotoCaptureState::Committed);
	TArray64<uint8> Png;
	TestTrue(TEXT("Rendered image stored as PNG"), F->LoadPhotoImage(DocPhotoTests::Owner(1), Status.PhotoId, Png).IsSuccess() && Png.Num() > 8);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
