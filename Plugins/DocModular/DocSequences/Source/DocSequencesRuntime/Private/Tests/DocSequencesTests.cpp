// DocSequences automation tests (SEQ-01..SEQ-05 logic, SEQ-06 through a scripted
// prerequisite provider). A scripted playback backend stands in for Level Sequence
// players, so these prove orchestration, not Sequencer evaluation.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocSequenceSubsystem.h"
#include "Tests/DocSequencesTestTypes.h"
#include "LevelSequence.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"

namespace DocSequencesTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SeqA, "Sequence.Test.A");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SeqB, "Sequence.Test.B");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SeqC, "Sequence.Test.C");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Hero, "Sequence.Role.Test.Hero");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Prereq, "Test.DocSequences.Prereq");

	class FScriptedBackend final : public IDocSequencePlaybackBackend
	{
	public:
		struct FEntry { float Position = 0.f; float Duration = 5.f; bool bPaused = false; bool bFinished = false; TMap<FName, TArray<AActor*>> Bindings; };
		TMap<int64, FEntry> Entries;
		TMap<FString, float> Markers = { { TEXT("Mid"), 3.5f } };
		TArray<TPair<int64, bool>> Stops; // (session, restore)
		float Duration = 5.f;
		bool bFailStart = false;

		virtual bool Start(int64 S, const FStartParams& P, FString& OutError) override
		{
			if (bFailStart) { OutError = TEXT("Scripted start failure"); return false; }
			FEntry E; E.Duration = Duration; E.Bindings = P.Bindings; Entries.Add(S, E); return true;
		}
		virtual void Pause(int64 S) override { if (FEntry* E = Entries.Find(S)) { E->bPaused = true; } }
		virtual void Resume(int64 S) override { if (FEntry* E = Entries.Find(S)) { E->bPaused = false; } }
		virtual void Stop(int64 S, bool bRestore) override { Entries.Remove(S); Stops.Add(TPair<int64, bool>(S, bRestore)); }
		virtual void JumpToEndAndStop(int64 S) override { if (FEntry* E = Entries.Find(S)) { E->Position = E->Duration; E->bFinished = true; } }
		virtual bool JumpToMarker(int64 S, const FString& Label) override
		{
			FEntry* E = Entries.Find(S);
			const float* T = Markers.Find(Label);
			if (!E || !T) { return false; }
			E->Position = *T;
			return true;
		}
		virtual void Rebind(int64 S, const TMap<FName, TArray<AActor*>>& B) override { if (FEntry* E = Entries.Find(S)) { E->Bindings = B; } }
		virtual float GetPosition(int64 S) const override { const FEntry* E = Entries.Find(S); return E ? E->Position : 0.f; }
		virtual float GetDuration(int64 S) const override { const FEntry* E = Entries.Find(S); return E ? E->Duration : 0.f; }
		virtual bool IsFinished(int64 S) const override { const FEntry* E = Entries.Find(S); return !E || E->bFinished; }

		void Step(float Dt)
		{
			for (TPair<int64, FEntry>& Pair : Entries)
			{
				FEntry& E = Pair.Value;
				if (!E.bPaused && !E.bFinished)
				{
					E.Position = FMath::Min(E.Duration, E.Position + Dt);
					E.bFinished = E.Position >= E.Duration;
				}
			}
		}
	};

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocSequenceSubsystem* S = nullptr;
		TSharedPtr<FScriptedBackend> Backend = MakeShared<FScriptedBackend>();
		TArray<TPair<FName, bool>> Effects;
		TArray<TPair<FDocRequestHandle, EDocSequenceState>> Finished;

		FFixture()
		{
			S = TW.GetSubsystem<UDocSequenceSubsystem>();
			if (S)
			{
				S->SetPlaybackBackend(Backend);
				S->OnSequenceEffectNative.AddLambda([this](FDocRequestHandle, FName Id, FGameplayTag, bool bAuth) { Effects.Add(TPair<FName, bool>(Id, bAuth)); });
				S->OnSessionFinishedNative.AddLambda([this](FDocRequestHandle H, EDocSequenceState T, const FDocSystemResult&) { Finished.Add(TPair<FDocRequestHandle, EDocSequenceState>(H, T)); });
			}
		}

		void Run(float Seconds, float Step = 0.25f)
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Step)
			{
				Backend->Step(Step);
				S->AdvanceForTesting(Step);
			}
		}

		int32 CountEffect(FName Id) const { return Effects.FilterByPredicate([Id](const TPair<FName, bool>& E) { return E.Key == Id; }).Num(); }
		int32 CountFinished(const FDocRequestHandle& H) const { return Finished.FilterByPredicate([&H](const TPair<FDocRequestHandle, EDocSequenceState>& F) { return F.Key == H; }).Num(); }

		AActor* SpawnParticipant(const FGameplayTag& Role, UDocSequenceParticipantComponent** OutComponent = nullptr)
		{
			AActor* Actor = TW.Spawn<AActor>();
			UDocSequenceParticipantComponent* Component = NewObject<UDocSequenceParticipantComponent>(Actor);
			Component->Roles.AddTag(Role);
			Component->RegisterComponent();
			if (OutComponent) { *OutComponent = Component; }
			return Actor;
		}
	};

	UDocSequenceDefinition* MakeDef(const FGameplayTag& Tag, int32 Priority = 0, EDocSequenceArbitration Arbitration = EDocSequenceArbitration::Queue)
	{
		UDocSequenceDefinition* Def = NewObject<UDocSequenceDefinition>(GetTransientPackage());
		Def->SequenceTag = Tag;
		Def->Sequence = NewObject<ULevelSequence>(GetTransientPackage());
		Def->Priority = Priority;
		Def->Arbitration = Arbitration;
		return Def;
	}

	FDocSequenceRole MakeRole(const FGameplayTag& Role, EDocMissingParticipantPolicy Policy = EDocMissingParticipantPolicy::Fail)
	{
		FDocSequenceRole R;
		R.Role = Role;
		R.BindingTag = TEXT("Hero");
		R.MissingPolicy = Policy;
		R.WaitTimeoutSeconds = 2.f;
		return R;
	}

	FDocSequenceEffect MakeEffect(FName Id, float Time, bool bIrreversible = false, bool bCommitOnSkip = false)
	{
		FDocSequenceEffect E;
		E.EffectId = Id;
		E.TriggerTimeSeconds = Time;
		E.bIrreversible = bIrreversible;
		E.bCommitOnSkip = bCommitOnSkip;
		E.Class = bIrreversible ? EDocSequenceEffectClass::Authoritative : EDocSequenceEffectClass::PresentationOnly;
		return E;
	}
}

using namespace DocSequencesTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSequencesControlsTest, "Doc.Sequences.PlaybackControls", Flags)
bool FDocSequencesControlsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocSequenceParticipantComponent* Hero = nullptr;
	AActor* HeroActor = F.SpawnParticipant(TAG_Hero, &Hero);
	UDocSequenceDefinition* Def = MakeDef(TAG_SeqA);
	Def->Roles.Add(MakeRole(TAG_Hero));

	const FDocSequenceRequestInfo R = F.S->PlaySequence(Def, FDocSequencePlayParams());
	TestTrue(TEXT("Accepted"), R.Result.IsSuccess() && R.Session.IsSet());
	TestEqual(TEXT("Playing"), F.S->GetSessionState(R.Session), EDocSequenceState::Playing);
	TestTrue(TEXT("Playing by tag"), F.S->IsSequencePlaying(TAG_SeqA));
	const FScriptedBackend::FEntry* Entry = F.Backend->Entries.Find(R.Session.GetOperationId());
	TestTrue(TEXT("Hero bound through its role (no label search)"), Entry && Entry->Bindings.FindRef(TEXT("Hero")).Contains(HeroActor));

	TestTrue(TEXT("Pause"), F.S->PauseSequence(R.Session).IsSuccess());
	TestEqual(TEXT("Paused"), F.S->GetSessionState(R.Session), EDocSequenceState::Paused);
	TestEqual(TEXT("Pause twice NoChange"), F.S->PauseSequence(R.Session).Outcome, EDocResultOutcome::NoChange);
	F.Run(1.f);
	TestEqual(TEXT("No progress while paused"), F.Backend->GetPosition(R.Session.GetOperationId()), 0.f);
	TestTrue(TEXT("Resume"), F.S->ResumeSequence(R.Session).IsSuccess());
	F.Run(1.f);
	TestTrue(TEXT("Progress after resume"), F.Backend->GetPosition(R.Session.GetOperationId()) > 0.9f);

	TestTrue(TEXT("Jump to marker"), F.S->JumpToMarker(R.Session, TEXT("Mid")).IsSuccess());
	TestEqual(TEXT("Unknown marker"), F.S->JumpToMarker(R.Session, TEXT("Nope")).Outcome, EDocResultOutcome::NotFound);

	const FDocSequenceRequestInfo Restarted = F.S->RestartSequence(R.Session);
	TestTrue(TEXT("Restart is a new session"), Restarted.Session.IsSet() && Restarted.Session != R.Session);
	FDocSequenceSessionInfo Old;
	TestTrue(TEXT("Old session info kept"), F.S->GetSessionInfo(R.Session, Old));
	TestEqual(TEXT("Old session interrupted"), Old.Terminal, EDocSequenceState::Interrupted);
	TestEqual(TEXT("Old session finished exactly once"), F.CountFinished(R.Session), 1);

	F.Run(6.f);
	FDocSequenceSessionInfo Done;
	F.S->GetSessionInfo(Restarted.Session, Done);
	TestEqual(TEXT("Completed"), Done.Terminal, EDocSequenceState::Completed);
	TestEqual(TEXT("Finished"), F.S->GetSessionState(Restarted.Session), EDocSequenceState::Finished);
	TestFalse(TEXT("Not playing"), F.S->IsSequencePlaying(TAG_SeqA));
	TestEqual(TEXT("Stop after finish is NoChange"), F.S->StopSequence(Restarted.Session).Outcome, EDocResultOutcome::NoChange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSequencesArbitrationTest, "Doc.Sequences.Arbitration", Flags)
bool FDocSequencesArbitrationTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	const FDocSequenceRequestInfo A = F.S->PlaySequence(MakeDef(TAG_SeqA, 0), FDocSequencePlayParams());
	const FDocSequenceRequestInfo B = F.S->PlaySequence(MakeDef(TAG_SeqB, 0), FDocSequencePlayParams());
	const FDocSequenceRequestInfo C = F.S->PlaySequence(MakeDef(TAG_SeqC, 5), FDocSequencePlayParams());
	TestEqual(TEXT("B queued"), B.State, EDocSequenceState::Queued);
	FDocSequenceSessionInfo Info;
	F.S->GetSessionInfo(C.Session, Info);
	TestEqual(TEXT("Higher priority queued first"), Info.QueuePosition, 0);

	const FDocSequenceRequestInfo Rejected = F.S->PlaySequence(MakeDef(TAG_SeqB, 99, EDocSequenceArbitration::Reject), FDocSequencePlayParams());
	TestEqual(TEXT("Reject policy"), Rejected.Result.Outcome, EDocResultOutcome::Conflict);

	const FDocSequenceRequestInfo E = F.S->PlaySequence(MakeDef(TAG_SeqA, 10, EDocSequenceArbitration::ReplaceLowerPriority), FDocSequencePlayParams());
	TestEqual(TEXT("E replaced lower-priority A"), F.S->GetActiveSession(), E.Session);
	TestEqual(TEXT("A interrupted"), F.Finished.Last().Value, EDocSequenceState::Interrupted);

	F.S->StopSequence(E.Session);
	TestEqual(TEXT("C (priority) runs next"), F.S->GetActiveSession(), C.Session);

	TestTrue(TEXT("Cancel queued B"), F.S->StopSequence(B.Session).IsSuccess());
	TestEqual(TEXT("Queued cancellation observable"), F.CountFinished(B.Session), 1);
	TestEqual(TEXT("Queue empty"), F.S->GetQueueLength(), 0);

	// Owner dies while queued.
	AActor* Owner = F.TW.Spawn<AActor>();
	FDocSequencePlayParams Owned;
	Owned.Owner = Owner;
	const FDocSequenceRequestInfo Q = F.S->PlaySequence(MakeDef(TAG_SeqB, 0), Owned);
	Owner->Destroy();
	F.S->AdvanceForTesting(0.01f);
	TestEqual(TEXT("Dead owner's queued request removed"), F.CountFinished(Q.Session), 1);

	// Bounded queue.
	const int32 Max = GetDefault<UDocSequencesSettings>()->MaxQueueLength;
	for (int32 i = 0; i < Max; ++i) { F.S->PlaySequence(MakeDef(TAG_SeqB, 0), FDocSequencePlayParams()); }
	TestEqual(TEXT("Queue full"), F.S->PlaySequence(MakeDef(TAG_SeqB, 0), FDocSequencePlayParams()).Result.Outcome, EDocResultOutcome::Conflict);

	// Non-interruptible active session: an Interrupt request queues instead of interrupting.
	F.S->StopSequence(C.Session);
	while (F.S->GetQueueLength() > 0) { F.S->StopSequence(F.S->GetActiveSession()); }
	F.S->StopSequence(F.S->GetActiveSession());
	UDocSequenceDefinition* Locked = MakeDef(TAG_SeqA);
	Locked->bInterruptible = false;
	const FDocSequenceRequestInfo L = F.S->PlaySequence(Locked, FDocSequencePlayParams());
	const FDocSequenceRequestInfo I = F.S->PlaySequence(MakeDef(TAG_SeqB, 50, EDocSequenceArbitration::Interrupt), FDocSequencePlayParams());
	TestEqual(TEXT("Locked session keeps playing"), F.S->GetActiveSession(), L.Session);
	TestEqual(TEXT("Interrupt request queued"), I.State, EDocSequenceState::Queued);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSequencesFailuresTest, "Doc.Sequences.FailuresAndWaiting", Flags)
bool FDocSequencesFailuresTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	// Missing required participant, Fail policy: one terminal result.
	UDocSequenceDefinition* NeedsHero = MakeDef(TAG_SeqA);
	NeedsHero->Roles.Add(MakeRole(TAG_Hero));
	const FDocSequenceRequestInfo Missing = F.S->PlaySequence(NeedsHero, FDocSequencePlayParams());
	FDocSequenceSessionInfo Info;
	F.S->GetSessionInfo(Missing.Session, Info);
	TestEqual(TEXT("Failed"), Info.Terminal, EDocSequenceState::Failed);
	TestEqual(TEXT("NotFound"), Info.Result.Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("One terminal result"), F.CountFinished(Missing.Session), 1);

	// Missing asset.
	TArray<TFunction<void()>> Loads;
	F.S->SetLoaderForTesting([&Loads](const FSoftObjectPath&, TFunction<void()> Done) { Loads.Add(MoveTemp(Done)); });
	UDocSequenceDefinition* NoAsset = MakeDef(TAG_SeqB);
	NoAsset->Sequence = TSoftObjectPtr<ULevelSequence>(FSoftObjectPath(TEXT("/Game/DocSequencesTest/Missing.Missing")));
	const FDocSequenceRequestInfo Asset = F.S->PlaySequence(NoAsset, FDocSequencePlayParams());
	TestEqual(TEXT("Loading"), F.S->GetSessionState(Asset.Session), EDocSequenceState::Loading);
	if (Loads.Num() == 1) { Loads[0](); }
	F.S->GetSessionInfo(Asset.Session, Info);
	TestEqual(TEXT("Missing asset fails"), Info.Result.Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("One terminal result (asset)"), F.CountFinished(Asset.Session), 1);

	// Load timeout; a late completion afterwards changes nothing.
	const FDocSequenceRequestInfo Slow = F.S->PlaySequence(NoAsset, FDocSequencePlayParams());
	F.Run(GetDefault<UDocSequencesSettings>()->LoadTimeoutSeconds + 1.f, 1.f);
	F.S->GetSessionInfo(Slow.Session, Info);
	TestEqual(TEXT("Timed out"), Info.Result.Outcome, EDocResultOutcome::TimedOut);
	if (Loads.Num() == 2) { Loads[1](); }
	TestEqual(TEXT("Late completion ignored"), F.CountFinished(Slow.Session), 1);
	F.S->SetLoaderForTesting(nullptr);

	// WaitWithTimeout: participant appears later.
	UDocSequenceDefinition* Waits = MakeDef(TAG_SeqC);
	Waits->Roles.Add(MakeRole(TAG_Hero, EDocMissingParticipantPolicy::WaitWithTimeout));
	const FDocSequenceRequestInfo W = F.S->PlaySequence(Waits, FDocSequencePlayParams());
	TestEqual(TEXT("Waiting for participant"), F.S->GetSessionState(W.Session), EDocSequenceState::ResolvingBindings);
	AActor* HeroActor = F.SpawnParticipant(TAG_Hero);
	F.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("Plays once bound"), F.S->GetSessionState(W.Session), EDocSequenceState::Playing);

	// Participant destroyed mid-playback with WaitWithTimeout → paused, then times out.
	HeroActor->Destroy();
	F.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("Paused waiting for participant"), F.S->GetSessionState(W.Session), EDocSequenceState::Paused);
	F.Run(3.f, 0.5f);
	F.S->GetSessionInfo(W.Session, Info);
	TestEqual(TEXT("Timed out waiting"), Info.Result.Outcome, EDocResultOutcome::TimedOut);

	// Optional participant: plays without it.
	UDocSequenceDefinition* Optional = MakeDef(TAG_SeqA);
	Optional->Roles.Add(MakeRole(TAG_Hero, EDocMissingParticipantPolicy::MissingOptional));
	const FDocSequenceRequestInfo O = F.S->PlaySequence(Optional, FDocSequencePlayParams());
	TestEqual(TEXT("Optional role missing is allowed"), F.S->GetSessionState(O.Session), EDocSequenceState::Playing);
	F.S->StopSequence(O.Session);

	// Backend refuses to start.
	F.Backend->bFailStart = true;
	const FDocSequenceRequestInfo Refused = F.S->PlaySequence(MakeDef(TAG_SeqB), FDocSequencePlayParams());
	F.S->GetSessionInfo(Refused.Session, Info);
	TestEqual(TEXT("Start failure is terminal"), Info.Terminal, EDocSequenceState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSequencesEffectsTest, "Doc.Sequences.EffectsSkipReplay", Flags)
bool FDocSequencesEffectsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocSequenceDefinition* Def = MakeDef(TAG_SeqA);
	Def->Effects.Add(MakeEffect(TEXT("Unlock"), 1.f, /*Irreversible*/ true));
	Def->Effects.Add(MakeEffect(TEXT("Milestone"), 3.f, false, /*CommitOnSkip*/ true));
	Def->Effects.Add(MakeEffect(TEXT("Flash"), 4.f));
	FDocSequenceEffect Boom;
	Boom.EffectId = TEXT("Boom");
	Boom.EventTag = DocSequenceTags::Event_Explosion;
	Def->Effects.Add(Boom);

	const FDocSequenceRequestInfo R1 = F.S->PlaySequence(Def, FDocSequencePlayParams());
	F.Run(1.5f);
	TestEqual(TEXT("Irreversible effect fired once"), F.CountEffect(TEXT("Unlock")), 1);
	TestTrue(TEXT("Event mapped"), F.S->NotifySequenceEvent(DocSequenceTags::Event_Explosion).IsSuccess());
	TestEqual(TEXT("Event again is NoChange"), F.S->NotifySequenceEvent(DocSequenceTags::Event_Explosion).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Unmapped event"), F.S->NotifySequenceEvent(DocSequenceTags::Event_DoorOpen).Outcome, EDocResultOutcome::NotFound);

	TestTrue(TEXT("Skip"), F.S->SkipSequence(R1.Session).IsSuccess());
	TestEqual(TEXT("Milestone committed on skip"), F.CountEffect(TEXT("Milestone")), 1);
	TestEqual(TEXT("Presentation effect not committed on skip"), F.CountEffect(TEXT("Flash")), 0);
	FDocSequenceSessionInfo Info;
	F.S->GetSessionInfo(R1.Session, Info);
	TestEqual(TEXT("Skipped"), Info.Terminal, EDocSequenceState::Skipped);

	// Replay: new session; the irreversible effect is not applied again.
	const FDocSequenceRequestInfo R2 = F.S->PlaySequence(Def, FDocSequencePlayParams());
	F.Run(2.f);
	TestEqual(TEXT("Irreversible effect not re-applied on replay"), F.CountEffect(TEXT("Unlock")), 1);
	// Seek past an effect: not executed (no opt-in), and not executed later either.
	F.S->JumpToMarker(R2.Session, TEXT("Mid")); // 3.5s: past Milestone (3s)
	F.Run(3.f);
	TestEqual(TEXT("Seeked-past effect not executed"), F.CountEffect(TEXT("Milestone")), 1);
	TestEqual(TEXT("Later effect fires in normal playback"), F.CountEffect(TEXT("Flash")), 1);
	TestTrue(TEXT("Receipt recorded for irreversible effect"), F.S->GetEffectReceipts().Num() == 1);

	// CancelAndRestore: nothing committed, restore requested from the backend.
	UDocSequenceDefinition* Restore = MakeDef(TAG_SeqB);
	Restore->SkipPolicy = EDocSequenceSkipPolicy::CancelAndRestore;
	Restore->Effects.Add(MakeEffect(TEXT("Late"), 4.f, false, true));
	const FDocSequenceRequestInfo R3 = F.S->PlaySequence(Restore, FDocSequencePlayParams());
	F.S->SkipSequence(R3.Session);
	TestEqual(TEXT("CancelAndRestore commits nothing"), F.CountEffect(TEXT("Late")), 0);
	TestTrue(TEXT("Backend asked to restore state"), F.Backend->Stops.Num() > 0 && F.Backend->Stops.Last().Value);

	// Non-replayable.
	UDocSequenceDefinition* Once = MakeDef(TAG_SeqC);
	Once->bCanReplay = false;
	Once->bCanSkip = false;
	const FDocSequenceRequestInfo R4 = F.S->PlaySequence(Once, FDocSequencePlayParams());
	TestEqual(TEXT("Skip refused"), F.S->SkipSequence(R4.Session).Outcome, EDocResultOutcome::PermissionDenied);
	F.Run(6.f);
	TestEqual(TEXT("Replay refused after completion"), F.S->PlaySequence(Once, FDocSequencePlayParams()).Result.Outcome, EDocResultOutcome::PermissionDenied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSequencesControlReleaseTest, "Doc.Sequences.ControlAndPrerequisites", Flags)
bool FDocSequencesControlReleaseTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocSequenceParticipantComponent* Hero = nullptr;
	F.SpawnParticipant(TAG_Hero, &Hero);

	UDocSequenceDefinition* A = MakeDef(TAG_SeqA, 0, EDocSequenceArbitration::Interrupt);
	A->Roles.Add(MakeRole(TAG_Hero));
	A->RequestedControl.AddTag(DocSequenceTags::Control_AI);
	UDocSequenceDefinition* B = MakeDef(TAG_SeqB, 0, EDocSequenceArbitration::Interrupt);
	B->Roles.Add(MakeRole(TAG_Hero));
	B->RequestedControl.AddTag(DocSequenceTags::Control_AI);

	const FDocSequenceRequestInfo RA = F.S->PlaySequence(A, FDocSequencePlayParams());
	TestTrue(TEXT("AI handed to sequence"), Hero->IsAIControlledBySequence());
	const FDocSequenceRequestInfo RB = F.S->PlaySequence(B, FDocSequencePlayParams()); // interrupts A
	TestTrue(TEXT("Newer owner keeps control after A released"), Hero->IsAIControlledBySequence());
	F.S->StopSequence(RB.Session);
	TestFalse(TEXT("AI returned after last claim"), Hero->IsAIControlledBySequence());

	// Failure path also releases.
	const FDocSequenceRequestInfo RC = F.S->PlaySequence(A, FDocSequencePlayParams());
	TestTrue(TEXT("Controlled again"), Hero->IsAIControlledBySequence());
	Hero->GetOwner()->Destroy(); // required participant lost, Fail policy
	F.S->AdvanceForTesting(0.1f);
	FDocSequenceSessionInfo Info;
	F.S->GetSessionInfo(RC.Session, Info);
	TestEqual(TEXT("Failed on participant loss"), Info.Terminal, EDocSequenceState::Failed);

	// Prerequisites: held for the session, released on every terminal path.
	UDocSequencesTestPrerequisiteProvider* Provider = NewObject<UDocSequencesTestPrerequisiteProvider>();
	Provider->AddToRoot();
	Provider->Handled = TAG_Prereq;
	F.S->RegisterPrerequisiteProvider(Provider);
	UDocSequenceDefinition* NeedsChunk = MakeDef(TAG_SeqC);
	NeedsChunk->Prerequisites.AddTag(TAG_Prereq);

	const FDocSequenceRequestInfo P1 = F.S->PlaySequence(NeedsChunk, FDocSequencePlayParams());
	TestEqual(TEXT("Waiting for prerequisite"), F.S->GetSessionState(P1.Session), EDocSequenceState::Loading);
	F.S->StopSequence(P1.Session); // cancelled while acquiring
	TestTrue(TEXT("Released after cancel"), Provider->Released.Contains(P1.Session.GetOperationId()));
	if (Provider->Pending.Num() > 0) { Provider->Pending[0](true); } // stale readiness
	TestFalse(TEXT("Stale readiness starts nothing"), F.S->GetActiveSession().IsSet());

	const FDocSequenceRequestInfo P2 = F.S->PlaySequence(NeedsChunk, FDocSequencePlayParams());
	if (Provider->Pending.Num() > 1) { Provider->Pending[1](true); }
	TestEqual(TEXT("Plays once ready"), F.S->GetSessionState(P2.Session), EDocSequenceState::Playing);
	TestFalse(TEXT("Held during playback"), Provider->Released.Contains(P2.Session.GetOperationId()));
	F.Run(6.f);
	TestTrue(TEXT("Released on completion"), Provider->Released.Contains(P2.Session.GetOperationId()));

	const FDocSequenceRequestInfo P3 = F.S->PlaySequence(NeedsChunk, FDocSequencePlayParams());
	if (Provider->Pending.Num() > 2) { Provider->Pending[2](false); }
	F.S->GetSessionInfo(P3.Session, Info);
	TestEqual(TEXT("Unready prerequisite fails"), Info.Terminal, EDocSequenceState::Failed);
	TestTrue(TEXT("Released on failure"), Provider->Released.Contains(P3.Session.GetOperationId()));

	F.S->UnregisterPrerequisiteProvider(Provider);
	const FDocSequenceRequestInfo P4 = F.S->PlaySequence(NeedsChunk, FDocSequencePlayParams());
	F.S->GetSessionInfo(P4.Session, Info);
	TestEqual(TEXT("No provider: Unsupported"), Info.Result.Outcome, EDocResultOutcome::Unsupported);
	Provider->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
