// DocAdaptiveAudio automation tests (AUD-01, -02, -03, -06 logic). They use the
// logical-only backend, so they prove director behaviour, not audible output
// (AUD-04/05 need an audio-enabled cooked run).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocAdaptiveAudioSubsystem.h"
#include "Components/SceneComponent.h"
#include "NativeGameplayTags.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

namespace DocAudioTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Night, "Test.DocAudio.Night");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Low, "Test.DocAudio.Profile.Low");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_High, "Test.DocAudio.Profile.High");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Other, "Test.DocAudio.Profile.Other");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Base, "Test.DocAudio.Layer.Base");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Drums, "Test.DocAudio.Layer.Drums");

	USoundWave* MakeWave(const TCHAR* Prefix)
	{
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), USoundWave::StaticClass(), FName(Prefix));
		return NewObject<USoundWave>(GetTransientPackage(), Name);
	}

	/** A soft path to an object that does not exist yet (created later to simulate a slow load). */
	FSoftObjectPath LatePath(FName& OutName)
	{
		OutName = FName(*FString::Printf(TEXT("DocAudioLate_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		return FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *GetTransientPackage()->GetPathName(), *OutName.ToString()));
	}

	UDocAudioLayerDefinition* MakeLayer(const FSoftObjectPath& Sound, const FGameplayTag& LayerTag)
	{
		UDocAudioLayerDefinition* Layer = NewObject<UDocAudioLayerDefinition>(GetTransientPackage());
		Layer->LayerTag = LayerTag;
		Layer->Sound = TSoftObjectPtr<USoundBase>(Sound);
		return Layer;
	}

	UDocAudioStateProfile* MakeProfile(const FGameplayTag& Id, const FGameplayTag& Channel, int32 Priority, const TArray<UDocAudioLayerDefinition*>& Layers)
	{
		UDocAudioStateProfile* Profile = NewObject<UDocAudioStateProfile>(GetTransientPackage());
		Profile->ProfileId = Id;
		Profile->Channel = Channel;
		Profile->Priority = Priority;
		Profile->FadeInSeconds = 0.5f;
		Profile->FadeOutSeconds = 0.5f;
		for (UDocAudioLayerDefinition* Layer : Layers)
		{
			FDocAudioLayerRef Ref;
			Ref.Layer = Layer;
			Profile->Layers.Add(Ref);
		}
		return Profile;
	}

	UDocAudioStateProfile* MakeSimple(const FGameplayTag& Id, const FGameplayTag& Channel, int32 Priority, USoundWave*& OutWave)
	{
		OutWave = MakeWave(TEXT("DocAudioWave"));
		return MakeProfile(Id, Channel, Priority, { MakeLayer(FSoftObjectPath(OutWave), TAG_Base) });
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocAdaptiveAudioSubsystem* Audio = nullptr;
		TSharedPtr<FDocNullAudioBackend> Backend = MakeShared<FDocNullAudioBackend>(TEXT("Test"));

		FFixture()
		{
			Audio = TW.GetSubsystem<UDocAdaptiveAudioSubsystem>();
			if (Audio)
			{
				Audio->SetPlaybackBackend(Backend);
				Audio->SetRandomSeed(1234);
			}
		}
		AActor* Owner() { return TW.Spawn<AActor>(); }
		void Advance(float Seconds, int32 Steps = 4) { for (int32 i = 0; i < Steps; ++i) { Audio->AdvanceForTesting(Seconds / Steps); } }
		int32 Playing(const USoundBase* Sound) const { return Backend->CountPlaying(Sound); }
	};

	/** Deferred loader: tests decide when (and in which order) loads complete. */
	struct FDeferredLoads
	{
		TArray<TFunction<void()>> Callbacks;
		void Install(UDocAdaptiveAudioSubsystem* Audio)
		{
			Audio->SetLoaderForTesting([this](const TArray<FSoftObjectPath>&, TFunction<void()> Done) { Callbacks.Add(MoveTemp(Done)); });
		}
		void Complete(int32 Index) { if (Callbacks.IsValidIndex(Index)) { Callbacks[Index](); } }
	};
}

using namespace DocAudioTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioPriorityTest, "Doc.Audio.PriorityAndChannels", Flags)
bool FDocAudioPriorityTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	USoundWave* LowWave = nullptr;
	USoundWave* HighWave = nullptr;
	USoundWave* AmbWave = nullptr;
	UDocAudioStateProfile* Low = MakeSimple(TAG_Low, DocAudioTags::Music, 0, LowWave);
	UDocAudioStateProfile* High = MakeSimple(TAG_High, DocAudioTags::Music, 10, HighWave);
	UDocAudioStateProfile* Amb = MakeSimple(TAG_Other, DocAudioTags::Ambience, 0, AmbWave);
	AActor* A = F.Owner();
	AActor* B = F.Owner();

	const FDocAudioRequestInfo RLow = F.Audio->RequestAudioState(Low, A);
	TestTrue(TEXT("Low requested"), RLow.Result.IsSuccess() && RLow.Handle.IsSet());
	TestEqual(TEXT("Low plays"), F.Playing(LowWave), 1);
	F.Audio->RequestAudioState(Amb, A);
	TestEqual(TEXT("Ambience plays on its own channel"), F.Playing(AmbWave), 1);

	const FDocAudioRequestInfo RHigh = F.Audio->RequestAudioState(High, B);
	TestEqual(TEXT("High plays"), F.Playing(HighWave), 1);
	TestEqual(TEXT("Low fading (not steady)"), F.Playing(LowWave), 0);
	F.Advance(1.f);
	TestEqual(TEXT("Low voice released after fade"), F.Backend->GetVoices().Num(), 2);
	TestEqual(TEXT("Ambience untouched by music priority"), F.Playing(AmbWave), 1);

	TestTrue(TEXT("Release high"), F.Audio->ReleaseAudioState(RHigh.Handle, B).IsSuccess());
	TestEqual(TEXT("Second release is NoChange"), F.Audio->ReleaseAudioState(RHigh.Handle, B).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Wrong owner denied"), F.Audio->ReleaseAudioState(RLow.Handle, B).Outcome, EDocResultOutcome::PermissionDenied);
	TestEqual(TEXT("Still-valid lower request becomes effective"), F.Playing(LowWave), 1);
	TestEqual(TEXT("Debug shows low"), F.Audio->GetChannelDebug(DocAudioTags::Music).PlayingProfiles.Num(), 1);

	F.Audio->ReleaseAudioState(RLow.Handle, A);
	F.Advance(1.f);
	TestEqual(TEXT("Music silent"), F.Playing(LowWave) + F.Playing(HighWave), 0);
	TestEqual(TEXT("Music released"), F.Audio->GetChannelDebug(DocAudioTags::Music).State, EDocAudioTransitionState::Released);
	TestEqual(TEXT("Invalid profile"), F.Audio->RequestAudioState(nullptr, A).Result.Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioNoResurrectTest, "Doc.Audio.RemovedRequestsNotRestored", Flags)
bool FDocAudioNoResurrectTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	USoundWave* LowWave = nullptr;
	USoundWave* HighWave = nullptr;
	UDocAudioStateProfile* Low = MakeSimple(TAG_Low, DocAudioTags::Music, 0, LowWave);
	UDocAudioStateProfile* High = MakeSimple(TAG_High, DocAudioTags::Music, 10, HighWave);
	AActor* A = F.Owner();
	AActor* B = F.Owner();
	F.Audio->RequestAudioState(Low, A);
	const FDocAudioRequestInfo RHigh = F.Audio->RequestAudioState(High, B);

	A->Destroy(); // owner of the underlying request goes away while overridden
	F.Advance(0.1f, 1);
	F.Audio->ReleaseAudioState(RHigh.Handle, B);
	F.Advance(1.f);
	TestEqual(TEXT("Expired underlying request is not restored"), F.Playing(LowWave), 0);
	TestEqual(TEXT("No music"), F.Backend->GetVoices().Num(), 0);

	// Owner destroyed while its own request is effective: released on the next tick.
	AActor* C = F.Owner();
	F.Audio->RequestAudioState(High, C);
	TestEqual(TEXT("High plays"), F.Playing(HighWave), 1);
	C->Destroy();
	F.Advance(1.f);
	TestEqual(TEXT("Dead owner's music released"), F.Backend->GetVoices().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioSupersedeTest, "Doc.Audio.SupersededAndFailedLoads", Flags)
bool FDocAudioSupersedeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	FDeferredLoads Loads;
	Loads.Install(F.Audio);

	USoundWave* BaseWave = nullptr;
	UDocAudioStateProfile* Base = MakeSimple(TAG_Low, DocAudioTags::Music, 0, BaseWave); // already loaded
	AActor* Owner = F.Owner();
	F.Audio->RequestAudioState(Base, Owner);
	TestEqual(TEXT("Base plays"), F.Playing(BaseWave), 1);

	FName NameA, NameB, NameC;
	UDocAudioStateProfile* A = MakeProfile(TAG_Other, DocAudioTags::Music, 5, { MakeLayer(LatePath(NameA), TAG_Base) });
	UDocAudioStateProfile* B = MakeProfile(TAG_High, DocAudioTags::Music, 10, { MakeLayer(LatePath(NameB), TAG_Base) });
	F.Audio->RequestAudioState(A, Owner);
	TestEqual(TEXT("A preloading"), F.Audio->GetChannelDebug(DocAudioTags::Music).State, EDocAudioTransitionState::AssetPreloading);
	TestEqual(TEXT("Previous keeps playing while loading"), F.Playing(BaseWave), 1);
	F.Audio->RequestAudioState(B, Owner);

	USoundWave* WaveA = NewObject<USoundWave>(GetTransientPackage(), NameA);
	const int32 PlaysBefore = F.Backend->TotalPlays;
	Loads.Complete(0); // A's late completion
	TestEqual(TEXT("Superseded load starts nothing"), F.Backend->TotalPlays, PlaysBefore);
	TestEqual(TEXT("A silent"), F.Playing(WaveA), 0);

	USoundWave* WaveB = NewObject<USoundWave>(GetTransientPackage(), NameB);
	Loads.Complete(1);
	TestEqual(TEXT("B plays"), F.Playing(WaveB), 1);
	TestEqual(TEXT("Base fading out"), F.Playing(BaseWave), 0);

	// Released owner during load.
	AActor* Temp = F.Owner();
	UDocAudioStateProfile* C = MakeProfile(TAG_Other, DocAudioTags::Music, 50, { MakeLayer(LatePath(NameC), TAG_Base) });
	const FDocAudioRequestInfo RC = F.Audio->RequestAudioState(C, Temp);
	F.Audio->ReleaseAudioState(RC.Handle, Temp);
	USoundWave* WaveC = NewObject<USoundWave>(GetTransientPackage(), NameC);
	Loads.Complete(2);
	TestEqual(TEXT("Released request's load starts nothing"), F.Playing(WaveC), 0);
	TestEqual(TEXT("B still playing"), F.Playing(WaveB), 1);

	// Failed asset: KeepPrevious retains B.
	FName Missing;
	UDocAudioStateProfile* D = MakeProfile(TAG_Other, DocAudioTags::Music, 60, { MakeLayer(LatePath(Missing), TAG_Base) });
	F.Audio->RequestAudioState(D, Owner);
	Loads.Complete(3); // never created → fails to resolve
	const FDocAudioChannelDebug Debug = F.Audio->GetChannelDebug(DocAudioTags::Music);
	TestEqual(TEXT("Failure reported"), Debug.State, EDocAudioTransitionState::Failed);
	TestTrue(TEXT("Diagnostic names the load failure"), Debug.LastDiagnostic.Contains(TEXT("Failed to load")));
	TestEqual(TEXT("KeepPrevious keeps B"), F.Playing(WaveB), 1);

	// Failed asset with Silence policy.
	FName Missing2;
	UDocAudioStateProfile* E = MakeProfile(TAG_Other, DocAudioTags::Music, 70, { MakeLayer(LatePath(Missing2), TAG_Base) });
	E->FailurePolicy = EDocAudioFailurePolicy::Silence;
	F.Audio->RequestAudioState(E, Owner);
	Loads.Complete(4);
	TestEqual(TEXT("Silence policy fades B"), F.Playing(WaveB), 0);

	// Timeout.
	FName Slow;
	UDocAudioStateProfile* G = MakeProfile(TAG_Other, DocAudioTags::Ambience, 0, { MakeLayer(LatePath(Slow), TAG_Base) });
	F.Audio->RequestAudioState(G, Owner);
	F.Advance(GetDefault<UDocAdaptiveAudioSettings>()->LoadTimeoutSeconds + 1.f, 2);
	TestEqual(TEXT("Timed out"), F.Audio->GetChannelDebug(DocAudioTags::Ambience).State, EDocAudioTransitionState::TimedOut);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioLayersTest, "Doc.Audio.LayersOneShotsLimits", Flags)
bool FDocAudioLayersTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	USoundWave* BaseWave = MakeWave(TEXT("DocAudioBase"));
	USoundWave* DrumWave = MakeWave(TEXT("DocAudioDrums"));
	UDocAudioStateProfile* P = MakeProfile(TAG_Low, DocAudioTags::Music, 0,
		{ MakeLayer(FSoftObjectPath(BaseWave), TAG_Base), MakeLayer(FSoftObjectPath(DrumWave), TAG_Drums) });
	P->Layers[1].bEnabledByDefault = false;
	USoundWave* ShotA = MakeWave(TEXT("DocAudioShotA"));
	USoundWave* ShotB = MakeWave(TEXT("DocAudioShotB"));
	P->RandomOneShots = { TSoftObjectPtr<USoundBase>(ShotA), TSoftObjectPtr<USoundBase>(ShotB) };
	P->OneShotMinIntervalSeconds = 1.f;
	P->OneShotMaxIntervalSeconds = 1.f;
	P->MaxConcurrentOneShots = 1;
	P->MaxVoices = 3;

	F.Audio->RequestAudioState(P, F.Owner());
	TestEqual(TEXT("Only default layer"), F.Playing(BaseWave) + F.Playing(DrumWave), 1);
	TestTrue(TEXT("Enable drums"), F.Audio->SetLayerEnabled(DocAudioTags::Music, TAG_Drums, true).IsSuccess());
	TestEqual(TEXT("Drums on"), F.Playing(DrumWave), 1);
	TestEqual(TEXT("Same request is NoChange"), F.Audio->SetLayerEnabled(DocAudioTags::Music, TAG_Drums, true).Outcome, EDocResultOutcome::NoChange);

	F.Advance(1.1f, 2);
	const int32 Shots = F.Playing(ShotA) + F.Playing(ShotB);
	TestEqual(TEXT("One one-shot started"), Shots, 1);
	F.Advance(1.1f, 2);
	TestEqual(TEXT("Concurrency bound holds"), F.Playing(ShotA) + F.Playing(ShotB), 1);
	TestTrue(TEXT("Voice limit holds"), F.Audio->GetChannelDebug(DocAudioTags::Music).VoiceCount <= 3);

	// Loop rule: a finished looping layer restarts; a finished one-shot is released.
	int32 BaseVoice = 0;
	for (const TPair<int32, FDocNullAudioBackend::FVoice>& V : F.Backend->GetVoices()) { if (V.Value.Sound.Get() == BaseWave) { BaseVoice = V.Key; } }
	const int32 PlaysBefore = F.Backend->TotalPlays;
	F.Backend->FinishVoice(BaseVoice);
	F.Advance(0.01f, 1);
	TestEqual(TEXT("Looping layer restarted"), F.Playing(BaseWave), 1);
	TestTrue(TEXT("Restart was a new play"), F.Backend->TotalPlays > PlaysBefore);

	TestTrue(TEXT("Disable base"), F.Audio->SetLayerEnabled(DocAudioTags::Music, TAG_Base, false).IsSuccess());
	F.Advance(1.f);
	TestEqual(TEXT("Base faded and released"), F.Playing(BaseWave), 0);
	TestFalse(TEXT("Debug no longer lists base"), F.Audio->GetChannelDebug(DocAudioTags::Music).EnabledLayers.Contains(TAG_Base));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioQuantizationTest, "Doc.Audio.Quantization", Flags)
bool FDocAudioQuantizationTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	USoundWave* Wave = nullptr;
	UDocAudioStateProfile* Bar = MakeSimple(TAG_Low, DocAudioTags::Music, 0, Wave);
	Bar->Quantization = EDocAudioQuantization::Bar;
	AActor* Owner = F.Owner();

	TestFalse(TEXT("No scheduler by default"), F.Audio->HasSchedulingCapability());
	F.Audio->RequestAudioState(Bar, Owner);
	TestEqual(TEXT("Applied immediately without scheduler"), F.Playing(Wave), 1);
	TestTrue(TEXT("Fallback reported"), F.Audio->GetChannelDebug(DocAudioTags::Music).LastDiagnostic.Contains(TEXT("unavailable")));

	TArray<TFunction<void()>> Fires;
	F.Audio->SetScheduler([&Fires](EDocAudioQuantization, const UDocAudioStateProfile*, TFunction<void()> Fire) { Fires.Add(MoveTemp(Fire)); return true; });
	USoundWave* W1 = nullptr;
	USoundWave* W2 = nullptr;
	UDocAudioStateProfile* P1 = MakeSimple(TAG_High, DocAudioTags::Music, 5, W1);
	UDocAudioStateProfile* P2 = MakeSimple(TAG_Other, DocAudioTags::Music, 9, W2);
	P1->Quantization = P2->Quantization = EDocAudioQuantization::Beat;
	F.Audio->RequestAudioState(P1, Owner);
	TestEqual(TEXT("Scheduled"), F.Audio->GetChannelDebug(DocAudioTags::Music).State, EDocAudioTransitionState::Scheduled);
	TestEqual(TEXT("Not yet playing"), F.Playing(W1), 0);
	F.Audio->RequestAudioState(P2, Owner); // supersedes the scheduled P1
	if (TestEqual(TEXT("Two schedules"), Fires.Num(), 2))
	{
		Fires[0]();
		TestEqual(TEXT("Stale scheduled start ignored"), F.Playing(W1), 0);
		Fires[1]();
		TestEqual(TEXT("P2 starts at its boundary"), F.Playing(W2), 1);
	}
	F.Audio->SetScheduler(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioOneShotContextTest, "Doc.Audio.StingersAndContext", Flags)
bool FDocAudioOneShotContextTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	USoundWave* StingWave = nullptr;
	UDocAudioStateProfile* Sting = MakeSimple(TAG_Other, DocAudioTags::Stinger, 0, StingWave);
	AActor* Owner = F.Owner();
	F.Audio->RequestAudioState(Sting, Owner);
	F.Audio->RequestAudioState(Sting, Owner);
	TestEqual(TEXT("Additive one-shots overlap"), F.Playing(StingWave), 2);
	TArray<int32> Ids;
	F.Backend->GetVoices().GetKeys(Ids);
	for (const int32 Id : Ids) { F.Backend->FinishVoice(Id); }
	F.Advance(0.1f, 1);
	TestEqual(TEXT("Finished one-shots released"), F.Audio->GetChannelDebug(DocAudioTags::Stinger).VoiceCount, 0);

	USoundWave* NightWave = nullptr;
	UDocAudioStateProfile* Night = MakeSimple(TAG_Low, DocAudioTags::Ambience, 0, NightWave);
	Night->Condition = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TAG_Night));
	F.Audio->RequestAudioState(Night, Owner);
	TestEqual(TEXT("Condition unmet: silent"), F.Playing(NightWave), 0);
	F.Audio->SetContextTags(TEXT("Time"), FGameplayTagContainer(TAG_Night));
	TestEqual(TEXT("Context satisfied: plays"), F.Playing(NightWave), 1);
	F.Audio->ClearContextSource(TEXT("Time"));
	F.Advance(1.f);
	TestEqual(TEXT("Context removed: released"), F.Playing(NightWave), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioTeardownTest, "Doc.Audio.TeardownAndChurn", Flags)
bool FDocAudioTeardownTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDocNullAudioBackend> Backend;
	{
		FFixture F;
		if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
		Backend = F.Backend;
		USoundWave* W1 = nullptr;
		USoundWave* W2 = nullptr;
		UDocAudioStateProfile* P1 = MakeSimple(TAG_Low, DocAudioTags::Music, 1, W1);
		UDocAudioStateProfile* P2 = MakeSimple(TAG_High, DocAudioTags::Music, 1, W2);
		AActor* Owner = F.Owner();
		for (int32 i = 0; i < 50; ++i)
		{
			const FDocAudioRequestInfo R = F.Audio->RequestAudioState((i % 2) ? P1 : P2, Owner);
			F.Advance(0.05f, 1);
			F.Audio->ReleaseAudioState(R.Handle, Owner);
		}
		F.Advance(2.f);
		TestEqual(TEXT("Churn leaves no voices"), Backend->GetVoices().Num(), 0);
		F.Audio->RequestAudioState(P1, Owner);
		TestEqual(TEXT("Playing before teardown"), Backend->GetVoices().Num(), 1);
	}
	TestEqual(TEXT("World teardown stops owned voices"), Backend->GetVoices().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocAudioEmitterTest, "Doc.Audio.EmitterBudget", Flags)
bool FDocAudioEmitterTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.Audio)) { return false; }
	UDocAdaptiveAudioSettings* Settings = GetMutableDefault<UDocAdaptiveAudioSettings>();
	const int32 SavedBudget = Settings->MaxActiveEmitters;
	Settings->MaxActiveEmitters = 2;

	USoundWave* Wave = MakeWave(TEXT("DocAudioEmitter"));
	TArray<UDocAudioEmitterComponent*> Emitters;
	for (const FVector& Location : { FVector(100, 0, 0), FVector(200, 0, 0), FVector(300, 0, 0), FVector(100000, 0, 0) })
	{
		AActor* Actor = F.Owner();
		UDocAudioEmitterComponent* Emitter = NewObject<UDocAudioEmitterComponent>(Actor);
		Emitter->Sound = Wave;
		Emitter->MaxDistance = 1000.f;
		Emitter->SetWorldLocation(Location);
		Emitter->RegisterComponent();
		Emitter->SetWorldLocation(Location);
		Emitters.Add(Emitter);
	}
	F.Audio->SetListenerLocationsOverride({ FVector::ZeroVector });
	F.Advance(0.6f, 2);
	TestEqual(TEXT("Budget respected"), F.Audio->GetActiveEmitterCount(), 2);
	TestTrue(TEXT("Nearest active"), Emitters[0]->IsEmitterActive() && Emitters[1]->IsEmitterActive());
	TestFalse(TEXT("Out of range inactive"), Emitters[3]->IsEmitterActive());

	Emitters[2]->Priority = 10;
	F.Advance(0.6f, 2);
	TestTrue(TEXT("Priority wins a slot"), Emitters[2]->IsEmitterActive());
	TestEqual(TEXT("Still within budget"), F.Audio->GetActiveEmitterCount(), 2);

	Emitters[2]->GetOwner()->Destroy();
	F.Advance(0.6f, 2);
	TestEqual(TEXT("Destroyed emitter released its slot"), F.Audio->GetActiveEmitterCount(), 2);
	Settings->MaxActiveEmitters = SavedBudget;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
