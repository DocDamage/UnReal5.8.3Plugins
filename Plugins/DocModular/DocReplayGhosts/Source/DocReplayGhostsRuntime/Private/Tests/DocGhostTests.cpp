#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocGhostTypes.h"
#include "DocGhostDefinitions.h"
#include "DocGhostSurrogateActor.h"
#include "DocReplayGhostSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocGhostTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FGhostKey
	{
		FName Track;
		double Time;
		FVector Location;
		double Yaw = 0.0;
		bool bDiscontinuity = false;
		FName Token = NAME_None;
	};

	struct FNote
	{
		double Time;
		FString Text;
	};

	UDocGhostProfile* Profile(float Cadence = 0.05f, float Gap = 5.0f, EDocGhostGapPolicy Policy = EDocGhostGapPolicy::Hold, float Chunk = 2.0f)
	{
		UDocGhostProfile* P = NewObject<UDocGhostProfile>(GetTransientPackage());
		P->SampleCadenceSeconds = Cadence;
		P->GapThresholdSeconds = Gap;
		P->GapPolicy = Policy;
		P->ChunkDurationSeconds = Chunk;
		return P;
	}

	FDocGhostTrackInfo Track(FName Id, FName Visual = TEXT("Default"))
	{
		FDocGhostTrackInfo T;
		T.TrackId = Id;
		T.VisualId = Visual;
		return T;
	}

	FTransform Pose(const FVector& L, double Yaw) { return FTransform(FRotator(0.0, Yaw, 0.0), L); }

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocReplayGhostSubsystem* Subsystem = nullptr;
		FFixture() { Subsystem = TW.GetSubsystem<UDocReplayGhostSubsystem>(); }
		UDocReplayGhostSubsystem* operator->() const { return Subsystem; }

		FDocGhostRecording Record(UDocGhostProfile* P, const TArray<FDocGhostTrackInfo>& Tracks, const TArray<FGhostKey>& Keys,
			const TArray<FNote>& Notes = {}, bool bAllowFallback = true)
		{
			P->bAllowVisualFallback = bAllowFallback;
			FGuid Id;
			Subsystem->StartRecording(TEXT("Test"), P, Tracks, Id);
			for (const FGhostKey& K : Keys)
			{
				Subsystem->RecordSampleAt(Id, K.Track, K.Time, Pose(K.Location, K.Yaw), K.Token, K.bDiscontinuity);
			}
			for (const FNote& N : Notes)
			{
				Subsystem->AddAnnotation(Id, NAME_None, N.Time, N.Text);
			}
			FDocGhostRecording R;
			Subsystem->StopRecording(Id, R);
			return R;
		}
	};

	FVector At(const FDocGhostRecording& R, FName TrackId, double Time, FDocGhostTrackState* OutState = nullptr)
	{
		FDocGhostTrackState S;
		R.EvaluateTrack(Time, TrackId, S);
		if (OutState) { *OutState = S; }
		return S.Transform.GetLocation();
	}
}

// GHO-01
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostSampleTimelineTest, FAutomationTestBase, "Doc.Ghost.SampleTimeline", DocGhostTests::Flags)
bool FDocGhostSampleTimelineTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	UDocGhostProfile* P = Profile(0.1f, 0.3f, EDocGhostGapPolicy::Hold, 0.5f);
	FGuid Id;
	TestTrue(TEXT("Start"), F->StartRecording(TEXT("Trial"), P, { Track(TEXT("Pawn")) }, Id).IsSuccess());
	const TArray<double> Times = { 0.0, 0.1, 0.2, 1.0, 1.1 };
	for (double T : Times)
	{
		TestTrue(*FString::Printf(TEXT("Sample at %.1f"), T), F->RecordSampleAt(Id, TEXT("Pawn"), T, Pose(FVector(T * 100.0, 0, 0), 0.0)).IsChanged());
	}
	TestEqual(TEXT("Faster than cadence skipped"), F->RecordSampleAt(Id, TEXT("Pawn"), 1.12, Pose(FVector(500, 0, 0), 0.0)).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Time going backwards refused"), F->RecordSampleAt(Id, TEXT("Pawn"), 0.5, Pose(FVector::ZeroVector, 0.0)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("NaN refused"), F->RecordSampleAt(Id, TEXT("Pawn"), std::numeric_limits<double>::quiet_NaN(), Pose(FVector::ZeroVector, 0.0)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Standing still is not re-recorded"), F->RecordSampleAt(Id, TEXT("Pawn"), 1.2, Pose(FVector(110, 0, 0), 0.0)).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Undeclared track"), F->RecordSampleAt(Id, TEXT("Other"), 1.3, Pose(FVector::ZeroVector, 0.0)).Outcome, EDocResultOutcome::NotFound);

	FDocGhostRecording R;
	TestTrue(TEXT("Stop"), F->StopRecording(Id, R).IsSuccess());
	TestEqual(TEXT("Only real samples"), R.Header.FrameCount, 5);
	TestEqual(TEXT("Duration is the last real sample"), R.Header.TotalDurationSeconds, 1.1, 1e-9);
	TArray<double> Stored;
	for (const FDocGhostTrackChunk& C : R.Chunks)
	{
		for (const FDocGhostSample& S : C.Samples)
		{
			Stored.Add(S.Timestamp);
		}
	}
	TestTrue(TEXT("Exact recorded timestamps, nothing invented in the gap"), Stored == Times);
	TestEqual(TEXT("Two chunks: the gap leaves windows empty"), R.Chunks.Num(), 2);
	FDocGhostTrackState State;
	At(R, TEXT("Pawn"), 0.6, &State);
	TestTrue(TEXT("Gap is flagged"), State.bInGap);
	TestEqual(TEXT("Normal interval interpolates"), At(R, TEXT("Pawn"), 0.15).X, 15.0, 1e-6);
	TestTrue(TEXT("Recording validates"), R.ValidateRecording().IsSuccess());

	FGuid Cancelled;
	F->StartRecording(TEXT("Cancel"), P, { Track(TEXT("Pawn")) }, Cancelled);
	F->RecordSampleAt(Cancelled, TEXT("Pawn"), 0.0, Pose(FVector::ZeroVector, 0.0));
	TestTrue(TEXT("Cancel"), F->CancelRecording(Cancelled).IsSuccess());
	FDocGhostRecording Nothing;
	TestEqual(TEXT("Cancelled recording is never advertised"), F->StopRecording(Cancelled, Nothing).Outcome, EDocResultOutcome::NotFound);
	FGuid Empty;
	F->StartRecording(TEXT("Empty"), P, { Track(TEXT("Pawn")) }, Empty);
	TestEqual(TEXT("Empty recording discarded"), F->StopRecording(Empty, Nothing).Outcome, EDocResultOutcome::Failed);
	return true;
}

// GHO-02
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostInterpolationTest, FAutomationTestBase, "Doc.Ghost.Interpolation", DocGhostTests::Flags)
bool FDocGhostInterpolationTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("A")), Track(TEXT("B")) }, {
		{ TEXT("A"), 0.0, FVector(0, 0, 0), 0.0 }, { TEXT("A"), 1.0, FVector(100, 0, 0), 90.0 },
		{ TEXT("B"), 0.0, FVector(0, 500, 0), 170.0 }, { TEXT("B"), 1.0, FVector(0, 500, 0), -170.0 } });
	FDocGhostTrackState A, B;
	TestEqual(TEXT("Linear position"), At(R, TEXT("A"), 0.5, &A).X, 50.0, 1e-3);
	TestEqual(TEXT("Slerped rotation"), A.Transform.Rotator().Yaw, 45.0, 0.01);
	TestTrue(TEXT("Other track evaluated on its own"), At(R, TEXT("B"), 0.5, &B).Equals(FVector(0, 500, 0), 1e-3));
	TestTrue(TEXT("Shortest path through 180, not through 0"), FMath::Abs(B.Transform.Rotator().Yaw) > 179.0);

	FGuid Session;
	TestTrue(TEXT("Play"), F->StartPlayback(R, false, Session).IsSuccess());
	F->AdvancePlayback(Session, 0.5f);
	ADocGhostSurrogateActor* GA = F->GetSurrogateActor(Session, TEXT("A"));
	ADocGhostSurrogateActor* GB = F->GetSurrogateActor(Session, TEXT("B"));
	TestTrue(TEXT("One surrogate per track"), GA && GB && GA != GB);
	TestTrue(TEXT("Surrogate A follows track A"), GA && GA->GetActorLocation().Equals(FVector(50, 0, 0), 1e-2));
	TestTrue(TEXT("Surrogate B follows track B"), GB && GB->GetActorLocation().Equals(FVector(0, 500, 0), 1e-2));
	return true;
}

// GHO-03
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostDiscontinuityTest, FAutomationTestBase, "Doc.Ghost.Discontinuity", DocGhostTests::Flags)
bool FDocGhostDiscontinuityTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P")) }, {
		{ TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 0.1, FVector(10, 0, 0) },
		{ TEXT("P"), 0.2, FVector(1000, 0, 0), 0.0, true }, { TEXT("P"), 0.3, FVector(1010, 0, 0) } });
	FDocGhostTrackState S;
	TestEqual(TEXT("No interpolation into a teleport"), At(R, TEXT("P"), 0.15, &S).X, 10.0, 1e-6);
	TestTrue(TEXT("Flagged"), S.bAtDiscontinuity);
	TestEqual(TEXT("After the teleport interpolation resumes"), At(R, TEXT("P"), 0.25).X, 1005.0, 1e-3);

	auto GapCase = [&](EDocGhostGapPolicy Policy)
	{
		return F.Record(Profile(0.05f, 0.15f, Policy), { Track(TEXT("P")) }, { { TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 1.0, FVector(100, 0, 0) } });
	};
	FDocGhostRecording Hold = GapCase(EDocGhostGapPolicy::Hold);
	TestEqual(TEXT("Hold keeps the last pose"), At(Hold, TEXT("P"), 0.5, &S).X, 0.0, 1e-6);
	TestTrue(TEXT("Hold visible and flagged"), S.bVisible && S.bInGap);
	FDocGhostRecording Hide = GapCase(EDocGhostGapPolicy::Hide);
	At(Hide, TEXT("P"), 0.5, &S);
	TestFalse(TEXT("Hide hides"), S.bVisible);
	FDocGhostRecording Snap = GapCase(EDocGhostGapPolicy::Snap);
	TestEqual(TEXT("Snap jumps"), At(Snap, TEXT("P"), 0.5, &S).X, 100.0, 1e-6);
	FGuid Session;
	F->StartPlayback(Hide, false, Session);
	F->AdvancePlayback(Session, 0.5f);
	ADocGhostSurrogateActor* Ghost = F->GetSurrogateActor(Session, TEXT("P"));
	TestTrue(TEXT("Hidden surrogate during the gap"), Ghost && Ghost->IsHidden());
	return true;
}

// GHO-04
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostNoLiveEffectsTest, FAutomationTestBase, "Doc.Ghost.NoLiveEffects", DocGhostTests::Flags)
bool FDocGhostNoLiveEffectsTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	AActor* Collectible = F.TW.Spawn<AActor>();
	USphereComponent* Sphere = NewObject<USphereComponent>(Collectible);
	Collectible->SetRootComponent(Sphere);
	Sphere->SetSphereRadius(20.0f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->RegisterComponent();
	Collectible->SetActorLocation(FVector(50, 0, 0));

	int32 Presentation = 0;
	F->OnPresentationEvent.AddLambda([&Presentation](const FGuid&, FName, FName, const FString&) { ++Presentation; });
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P")) }, { { TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 1.0, FVector(100, 0, 0) } }, { { 0.4, TEXT("Checkpoint!") } });
	FGuid Session;
	TestTrue(TEXT("Play"), F->StartPlayback(R, false, Session).IsSuccess());
	F->AdvancePlayback(Session, 0.5f);
	ADocGhostSurrogateActor* Ghost = F->GetSurrogateActor(Session, TEXT("P"));
	TestTrue(TEXT("Ghost passes through the collectible"), Ghost && Ghost->GetActorLocation().Equals(FVector(50, 0, 0), 1e-2));
	TestFalse(TEXT("No collision"), Ghost && Ghost->HasAnyCollision());
	TestFalse(TEXT("No overlap events"), Ghost && Ghost->HasAnyOverlapEvents());
	TestFalse(TEXT("Cannot affect the live world"), Ghost && Ghost->CanAffectLiveWorld());
	TestFalse(TEXT("Not replicated"), Ghost && Ghost->GetIsReplicated());
	TestFalse(TEXT("Collectible never overlapped"), Ghost && Sphere->IsOverlappingActor(Ghost));
	TestTrue(TEXT("Collectible untouched"), Collectible->GetActorLocation().Equals(FVector(50, 0, 0)));
	TestEqual(TEXT("Damage is refused"), Ghost ? Ghost->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr) : -1.0f, 0.0f);
	TestEqual(TEXT("Timeline events only on the replay channel"), Presentation, 1);
	return true;
}

// GHO-05
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostAnimationNotifyIsolationTest, FAutomationTestBase, "Doc.Ghost.AnimationNotifyIsolation", DocGhostTests::Flags)
bool FDocGhostAnimationNotifyIsolationTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	TestEqual(TEXT("No isolating animation adapter is claimed"), F->QueryAnimationCapability().Outcome, EDocResultOutcome::Unsupported);
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P")) }, {
		{ TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 0.5, FVector(50, 0, 0), 0.0, false, TEXT("Jump") }, { TEXT("P"), 1.0, FVector(100, 0, 0) } });
	FGuid Session;
	TestTrue(TEXT("Play"), F->StartPlayback(R, false, Session).IsSuccess());
	ADocGhostSurrogateActor* Ghost = F->GetSurrogateActor(Session, TEXT("P"));
	if (!Ghost)
	{
		AddError(TEXT("No surrogate"));
		return false;
	}
	// A presentation adapter attaches an animated mesh and a collider; isolation strips their live capabilities.
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Ghost);
	Mesh->SetupAttachment(Ghost->GetRootComponent());
	Mesh->RegisterComponent();
	UBoxComponent* Box = NewObject<UBoxComponent>(Ghost);
	Box->SetupAttachment(Ghost->GetRootComponent());
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetGenerateOverlapEvents(true);
	Box->RegisterComponent();
	TestTrue(TEXT("Adapter components would affect the world before isolation"), Ghost->CanAffectLiveWorld());
	Ghost->EnforceIsolation();
	TestFalse(TEXT("Isolated"), Ghost->CanAffectLiveWorld());
	TestTrue(TEXT("Animation paused: no notifies or root motion"), Mesh->bPauseAnims && !Mesh->IsComponentTickEnabled());

	int32 Tokens = 0;
	F->OnPresentationEvent.AddLambda([&Tokens](const FGuid&, FName, FName Kind, const FString& Payload) { Tokens += (Kind == TEXT("Token") && Payload == TEXT("Jump")) ? 1 : 0; });
	F->AdvancePlayback(Session, 0.6f);
	FDocGhostTrackState State;
	TestTrue(TEXT("State"), F->QueryTrackState(Session, TEXT("P"), State));
	TestEqual(TEXT("Token is presentation data"), State.ActionToken, FName(TEXT("Jump")));
	TestEqual(TEXT("Token delivered once, on the replay channel"), Tokens, 1);
	return true;
}

// GHO-06
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostIndexedSeekTest, FAutomationTestBase, "Doc.Ghost.IndexedSeek", DocGhostTests::Flags)
bool FDocGhostIndexedSeekTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	TArray<FGhostKey> Keys;
	for (int32 i = 0; i <= 2000; ++i)
	{
		const double T = i * 0.05;
		Keys.Add({ TEXT("P"), T, FVector(T * 100.0, 0, 0) });
	}
	const FDocGhostRecording R = F.Record(Profile(0.05f, 1.0f), { Track(TEXT("P")) }, Keys, { { 10.0, TEXT("A") }, { 50.0, TEXT("B") }, { 90.0, TEXT("C") } });
	TestEqual(TEXT("All samples kept"), R.Header.FrameCount, 2001);
	TestTrue(TEXT("Chunked"), R.Chunks.Num() > 40);
	int32 Events = 0;
	F->OnPresentationEvent.AddLambda([&Events](const FGuid&, FName, FName, const FString&) { ++Events; });
	FGuid Session;
	TestTrue(TEXT("Play"), F->StartPlayback(R, false, Session).IsSuccess());
	F->PausePlayback(Session);
	TestTrue(TEXT("Seek forward"), F->SeekPlayback(Session, 90.025).IsSuccess());
	FDocGhostTrackState State;
	F->QueryTrackState(Session, TEXT("P"), State);
	TestEqual(TEXT("Reconstructed at the target"), State.Transform.GetLocation().X, 9002.5, 1e-3);
	FDocGhostPlaybackSession Info;
	F->QuerySession(Session, Info);
	TestTrue(TEXT("Index lookup, not a scan from zero"), Info.LastSeekChunkVisits > 0 && Info.LastSeekChunkVisits <= 10);
	TestTrue(TEXT("Seek backward"), F->SeekPlayback(Session, 10.0).IsSuccess());
	F->QueryTrackState(Session, TEXT("P"), State);
	TestEqual(TEXT("Backward state"), State.Transform.GetLocation().X, 1000.0, 1e-3);
	TestEqual(TEXT("Seeking fires nothing"), Events, 0);

	F->SeekPlayback(Session, 49.9);
	F->ResumePlayback(Session);
	F->AdvancePlayback(Session, 0.2f);
	TestEqual(TEXT("Forward playback across 50s fires once"), Events, 1);
	F->SeekPlayback(Session, 49.9);
	TestEqual(TEXT("Seeking back fires nothing"), Events, 1);
	F->AdvancePlayback(Session, 0.2f);
	TestEqual(TEXT("Replaying the stretch presents it again (declared policy)"), Events, 2);
	return true;
}

// GHO-07
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostAsyncCloseTest, FAutomationTestBase, "Doc.Ghost.AsyncClose", DocGhostTests::Flags)
bool FDocGhostAsyncCloseTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P")) }, { { TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 1.0, FVector(100, 0, 0) } });
	TArray<uint8> Bytes;
	TestTrue(TEXT("Write"), FDocGhostRecordingIO::Write(R, Bytes));

	FGuid Request;
	F->RequestOpenRecording(Bytes, false, Request);
	TestTrue(TEXT("Cancel before the read completes"), F->CancelOpenRequest(Request).IsSuccess());
	F->CompleteOpenRequests();
	TestEqual(TEXT("Late completion starts nothing"), F->GetSurrogateCount(), 0);
	FGuid Session;
	TestEqual(TEXT("Cancelled request is gone"), F->QueryOpenRequest(Request, Session).Outcome, EDocResultOutcome::NotFound);

	F->RequestOpenRecording(Bytes, false, Request);
	TestEqual(TEXT("Pending"), F->QueryOpenRequest(Request, Session).Outcome, EDocResultOutcome::NotReady);
	TestEqual(TEXT("One completion"), F->CompleteOpenRequests(), 1);
	TestTrue(TEXT("Opened"), F->QueryOpenRequest(Request, Session).IsSuccess());
	TestEqual(TEXT("Surrogate spawned"), F->GetSurrogateCount(), 1);

	F->AdvancePlayback(Session, 0.3f);
	F->PausePlayback(Session);
	F->AdvancePlayback(Session, 1.0f);
	TestTrue(TEXT("Resume"), F->ResumePlayback(Session).IsChanged());
	F->AdvancePlayback(Session, 0.1f);
	FDocGhostPlaybackSession Info;
	F->QuerySession(Session, Info);
	TestEqual(TEXT("Resume continues from the paused time"), Info.CurrentTime, 0.4, 1e-6);

	TestTrue(TEXT("Stop"), F->StopPlayback(Session).IsSuccess());
	TestEqual(TEXT("Stopped playback cannot be resumed implicitly"), F->ResumePlayback(Session).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Advancing a stopped session does nothing"), F->AdvancePlayback(Session, 0.5f).Outcome, EDocResultOutcome::NoChange);
	const int32 Generation = Info.Generation;
	TestTrue(TEXT("Visual load for the live session accepted"), F->NotifyVisualLoaded(Session, Generation));
	TestTrue(TEXT("Close"), F->ClosePlaybackSession(Session).IsSuccess());
	TestFalse(TEXT("Late visual load after close is ignored"), F->NotifyVisualLoaded(Session, Generation));
	TestEqual(TEXT("Closed session cannot be revived"), F->AdvancePlayback(Session, 0.1f).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("No seek either"), F->SeekPlayback(Session, 0.5).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Surrogates released"), F->GetSurrogateCount(), 0);
	return true;
}

// GHO-08
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostMalformedRecordingTest, FAutomationTestBase, "Doc.Ghost.MalformedRecording", DocGhostTests::Flags)
bool FDocGhostMalformedRecordingTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P")) }, {
		{ TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 0.5, FVector(50, 0, 0) }, { TEXT("P"), 1.0, FVector(100, 0, 0) } }, { { 0.5, TEXT("Half") } });
	TArray<uint8> Bytes;
	FDocGhostRecordingIO::Write(R, Bytes);
	FDocGhostRecording Parsed;
	TestTrue(TEXT("Round trip"), FDocGhostRecordingIO::Read(Bytes, Parsed).IsSuccess());
	TestEqual(TEXT("Round trip keeps samples"), Parsed.Header.FrameCount, 3);

	int32 Accepted = 0;
	for (int32 Len = 0; Len < Bytes.Num(); ++Len)
	{
		TArray<uint8> Truncated(Bytes.GetData(), Len);
		Accepted += FDocGhostRecordingIO::Read(Truncated, Parsed).IsSuccess() ? 1 : 0;
	}
	TestEqual(TEXT("Every truncation is rejected"), Accepted, 0);
	TArray<uint8> Flipped = Bytes;
	Flipped[Flipped.Num() - 20] ^= 0x5A;
	TestFalse(TEXT("Corrupted payload rejected"), FDocGhostRecordingIO::Read(Flipped, Parsed).IsSuccess());
	TArray<uint8> Magic = Bytes;
	Magic[0] ^= 0xFF;
	TestEqual(TEXT("Wrong magic"), FDocGhostRecordingIO::Read(Magic, Parsed).Outcome, EDocResultOutcome::InvalidInput);
	TArray<uint8> Trailing = Bytes;
	Trailing.Add(0);
	TestFalse(TEXT("Trailing bytes rejected"), FDocGhostRecordingIO::Read(Trailing, Parsed).IsSuccess());

	auto Invalid = [](const FDocGhostRecording& Bad) { return Bad.ValidateRecording().Outcome == EDocResultOutcome::InvalidInput; };
	FDocGhostRecording NaN = R;
	NaN.Chunks[0].Samples[1].Transform.SetLocation(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0));
	TestTrue(TEXT("Non-finite transform"), Invalid(NaN));
	FDocGhostRecording Order = R;
	Order.Chunks[0].Samples[1].Timestamp = 0.0;
	TestTrue(TEXT("Out-of-order samples"), Invalid(Order));
	FDocGhostRecording Count = R;
	Count.Header.FrameCount = 99;
	TestTrue(TEXT("Count mismatch"), Invalid(Count));
	FDocGhostRecording Stranger = R;
	Stranger.Chunks[0].TrackId = TEXT("Nobody");
	TestTrue(TEXT("Chunk for an unknown track"), Invalid(Stranger));
	FDocGhostRecording Unfinished = R;
	Unfinished.Header.bFinalized = false;
	TestTrue(TEXT("Unfinalized"), Invalid(Unfinished));

	FDocGhostRecording ClassPath = F.Record(Profile(), { Track(TEXT("P"), TEXT("/Script/Engine.Pawn")) }, { { TEXT("P"), 0.0, FVector::ZeroVector }, { TEXT("P"), 1.0, FVector(10, 0, 0) } }, {}, /*bAllowFallback*/ false);
	FGuid Session;
	TestEqual(TEXT("A class path in a file never authorizes spawning"), F->StartPlayback(ClassPath, false, Session).Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Nothing spawned"), F->GetSurrogateCount(), 0);
	return true;
}

// GHO-09
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostVersionFallbackTest, FAutomationTestBase, "Doc.Ghost.VersionFallback", DocGhostTests::Flags)
bool FDocGhostVersionFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	const FDocGhostRecording R = F.Record(Profile(), { Track(TEXT("P"), TEXT("RetiredVisual")) }, { { TEXT("P"), 0.0, FVector::ZeroVector }, { TEXT("P"), 1.0, FVector(10, 0, 0) } });
	TArray<uint8> Bytes;
	FDocGhostRecordingIO::Write(R, Bytes);
	TArray<uint8> Future = Bytes;
	const int32 NewVersion = 99;
	FMemory::Memcpy(Future.GetData() + 4, &NewVersion, sizeof(int32));
	const TArray<uint8> Original = Future;
	FDocGhostRecording Parsed;
	TestEqual(TEXT("Unsupported version fails"), FDocGhostRecordingIO::Read(Future, Parsed).Outcome, EDocResultOutcome::Unsupported);
	TestTrue(TEXT("Original bytes untouched"), Future == Original);
	FDocGhostRecording InMemory = R;
	InMemory.Header.FormatVersion = 99;
	TestEqual(TEXT("Unsupported in memory too"), InMemory.ValidateRecording().Outcome, EDocResultOutcome::Unsupported);

	FGuid Session;
	TestTrue(TEXT("Missing visual with fallback allowed"), F->StartPlayback(R, false, Session).IsSuccess());
	FDocGhostPlaybackSession Info;
	F->QuerySession(Session, Info);
	TestEqual(TEXT("Explicitly labelled fallback"), Info.VisualCompatibility, EDocGhostVisualCompatibility::TransformOnlyFallback);
	TestTrue(TEXT("Plain proxy"), F->GetSurrogateActor(Session, TEXT("P")) && F->GetSurrogateActor(Session, TEXT("P"))->GetClass() == ADocGhostSurrogateActor::StaticClass());

	TestTrue(TEXT("Register visual"), F->RegisterGhostVisual(TEXT("RetiredVisual"), ADocGhostSurrogateActor::StaticClass()).IsSuccess());
	FGuid Full;
	TestTrue(TEXT("Play with the visual available"), F->StartPlayback(R, false, Full).IsSuccess());
	F->QuerySession(Full, Info);
	TestEqual(TEXT("Full compatibility"), Info.VisualCompatibility, EDocGhostVisualCompatibility::Full);

	FDocGhostRecording Strict = F.Record(Profile(), { Track(TEXT("P"), TEXT("OtherMissing")) }, { { TEXT("P"), 0.0, FVector::ZeroVector }, { TEXT("P"), 1.0, FVector(10, 0, 0) } }, {}, false);
	FGuid Refused;
	TestEqual(TEXT("No fallback allowed: refuse rather than pretend"), F->StartPlayback(Strict, false, Refused).Outcome, EDocResultOutcome::InvalidConfiguration);
	return true;
}

// GHO-10
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGhostCookedConcurrentPlaybackTest, FAutomationTestBase, "Doc.Ghost.CookedConcurrentPlayback", DocGhostTests::Flags)
bool FDocGhostCookedConcurrentPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DocGhostTests;
	FFixture F;
	AActor* Live = F.TW.Spawn<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Live);
	Live->SetRootComponent(Root);
	Root->RegisterComponent();

	const FDocGhostRecording R1 = F.Record(Profile(), { Track(TEXT("P")) }, { { TEXT("P"), 0.0, FVector(0, 0, 0) }, { TEXT("P"), 1.0, FVector(100, 0, 0) } });
	const FDocGhostRecording R2 = F.Record(Profile(), { Track(TEXT("P")) }, { { TEXT("P"), 0.0, FVector(0, 0, 300) }, { TEXT("P"), 1.0, FVector(0, 100, 300) } });
	FGuid S1, S2;
	TestTrue(TEXT("Session 1"), F->StartPlayback(R1, true, S1).IsSuccess());
	TestTrue(TEXT("Session 2"), F->StartPlayback(R2, false, S2).IsSuccess());
	for (int32 Step = 1; Step <= 5; ++Step)
	{
		Live->SetActorLocation(FVector(0, 1000.0 + Step * 10.0, 0));
		F->AdvanceAll(0.1f);
	}
	TestTrue(TEXT("Live gameplay unchanged by ghosts"), Live->GetActorLocation().Equals(FVector(0, 1050, 0)));
	TestTrue(TEXT("Ghost 1"), F->GetSurrogateActor(S1, TEXT("P"))->GetActorLocation().Equals(FVector(50, 0, 0), 1e-2));
	TestTrue(TEXT("Ghost 2"), F->GetSurrogateActor(S2, TEXT("P"))->GetActorLocation().Equals(FVector(0, 50, 300), 1e-2));

	TArray<FGuid> Extra;
	for (int32 i = 0; i < UDocReplayGhostSubsystem::MaxConcurrentSessions - 2; ++i)
	{
		FGuid S;
		TestTrue(TEXT("Within the session bound"), F->StartPlayback(R1, false, S).IsSuccess());
	}
	FGuid Over;
	TestEqual(TEXT("Session count is bounded"), F->StartPlayback(R1, false, Over).Outcome, EDocResultOutcome::Conflict);
	AddInfo(TEXT("GHO-10: concurrency and isolation are automated; cooked/packaged visual playback remains a manual gate."));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
