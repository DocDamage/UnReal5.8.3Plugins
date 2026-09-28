#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocBroadcastTypes.h"
#include "DocBroadcastProgramDefinition.h"
#include "DocBroadcastChannelDefinition.h"
#include "DocBroadcastPlaybackProvider.h"
#include "DocBroadcastReceiverComponent.h"
#include "DocBroadcastSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace DocBroadcastTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr double Tol = 1.0e-4;

	UDocBroadcastProgramDefinition* Program(FName Id, double Duration, bool bCanSeek = true, bool bKnownDuration = true)
	{
		UDocBroadcastProgramDefinition* P = NewObject<UDocBroadcastProgramDefinition>(GetTransientPackage());
		P->ProgramId = Id;
		P->Title = FText::FromName(Id);
		P->DurationSeconds = Duration;
		P->bCanSeek = bCanSeek;
		P->bKnownDuration = bKnownDuration;
		return P;
	}

	FDocBroadcastScheduleEntry Entry(UDocBroadcastProgramDefinition* P, double Start, double Duration)
	{
		FDocBroadcastScheduleEntry E;
		E.EntryId = FGuid::NewGuid();
		E.Program = P;
		E.StartOffsetSeconds = Start;
		E.DurationSeconds = Duration;
		return E;
	}

	UDocBroadcastChannelDefinition* Channel(FName Id, const TArray<FDocBroadcastScheduleEntry>& Entries, bool bLooping = true, double Gap = 0.0)
	{
		UDocBroadcastChannelDefinition* C = NewObject<UDocBroadcastChannelDefinition>(GetTransientPackage());
		C->ChannelId = Id;
		C->DisplayName = FText::FromName(Id);
		C->Schedule = Entries;
		C->bIsLooping = bLooping;
		C->GapDurationSeconds = Gap;
		return C;
	}

	FDocBroadcastInterruption Interruption(const FGuid& Owner, UDocBroadcastProgramDefinition* P, int32 Priority = 10,
		EDocBroadcastInterruptionPolicy Policy = EDocBroadcastInterruptionPolicy::ContinueUnderlyingSchedule,
		EDocBroadcastArbitration Arbitration = EDocBroadcastArbitration::Queue)
	{
		FDocBroadcastInterruption I;
		I.OwnerId = Owner;
		I.Program = P;
		I.Priority = Priority;
		I.ResumePolicy = Policy;
		I.Arbitration = Arbitration;
		return I;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocBroadcastSubsystem* Subsystem = nullptr;
		UDocBroadcastSyntheticProvider* Provider = nullptr;

		FFixture()
		{
			Subsystem = TW.GetSubsystem<UDocBroadcastSubsystem>();
			Provider = NewObject<UDocBroadcastSyntheticProvider>(Subsystem);
			Subsystem->SetPlaybackProvider(Provider);
		}

		UDocBroadcastReceiverComponent* Receiver(FName PreTuned = NAME_None)
		{
			AActor* Actor = TW.Spawn<AActor>();
			UDocBroadcastReceiverComponent* R = NewObject<UDocBroadcastReceiverComponent>(Actor);
			R->TunedChannelId = PreTuned;
			Actor->AddInstanceComponent(R);
			R->RegisterComponent();
			return R;
		}

		FName Now(FName ChannelId, double& Cursor, bool* bInterruption = nullptr) const
		{
			FName Id;
			bool bInt = false;
			Subsystem->QueryNowPlaying(ChannelId, Id, Cursor, bInt);
			if (bInterruption)
			{
				*bInterruption = bInt;
			}
			return Id;
		}

		FDocBroadcastTransport Transport(FName ChannelId) const
		{
			FDocBroadcastTransport T;
			Subsystem->QueryTransport(ChannelId, T);
			return T;
		}
	};
}

// BRC-01
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastScheduleBoundaryTest, FAutomationTestBase, "Doc.Broadcast.ScheduleBoundary", DocBroadcastTests::Flags)
bool FDocBroadcastScheduleBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	UDocBroadcastProgramDefinition* A = Program(TEXT("Prog.A"), 10.0);
	UDocBroadcastProgramDefinition* B = Program(TEXT("Prog.B"), 15.0);
	UDocBroadcastChannelDefinition* Def = Channel(TEXT("Ch.Main"), { Entry(A, 0.0, 10.0), Entry(B, 10.0, 15.0) }, true, 5.0);
	TestTrue(TEXT("Schedule validates"), Def->ValidateChannel().IsSuccess());
	TestEqual(TEXT("Total includes trailing gap"), Def->GetTotalDuration(), 30.0, Tol);

	FDocBroadcastResolvedSlot Slot;
	TestTrue(TEXT("Resolve 0"), Def->ResolveSchedule(0.0, Slot) && Slot.bHasProgram && Slot.EntryIndex == 0);
	TestTrue(TEXT("Just before boundary stays in A"), Def->ResolveSchedule(9.999, Slot) && Slot.EntryIndex == 0);
	TestTrue(TEXT("Boundary is half-open: 10 is B"), Def->ResolveSchedule(10.0, Slot) && Slot.EntryIndex == 1);
	TestEqual(TEXT("B cursor at boundary"), Slot.ProgramCursorSeconds, 0.0, Tol);
	TestTrue(TEXT("Gap resolves"), Def->ResolveSchedule(25.0, Slot));
	TestFalse(TEXT("Gap has no program"), Slot.bHasProgram);
	TestEqual(TEXT("Gap slot end"), Slot.SlotEndOffset, 30.0, Tol);
	TestTrue(TEXT("Wrap to A"), Def->ResolveSchedule(30.0, Slot) && Slot.EntryIndex == 0 && Slot.LoopIndex == 1);
	TestTrue(TEXT("Far offset resolves directly"), Def->ResolveSchedule(1.0e6 + 12.0, Slot) && Slot.EntryIndex == 1);
	TestEqual(TEXT("Far offset cursor"), Slot.ProgramCursorSeconds, 12.0, 1.0e-3);
	TestFalse(TEXT("NaN offset refused"), Def->ResolveSchedule(std::numeric_limits<double>::quiet_NaN(), Slot));

	UDocBroadcastChannelDefinition* Once = Channel(TEXT("Ch.Once"), { Entry(A, 0.0, 10.0) }, false);
	TestFalse(TEXT("Non-looping schedule ends"), Once->ResolveSchedule(10.0, Slot));

	TestEqual(TEXT("Overlap rejected"), Channel(TEXT("Ch.Bad1"), { Entry(A, 0.0, 10.0), Entry(B, 5.0, 10.0) })->ValidateChannel().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Zero-length rejected"), Channel(TEXT("Ch.Bad2"), { Entry(A, 0.0, 0.0) })->ValidateChannel().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Empty schedule rejected"), Channel(TEXT("Ch.Bad3"), {})->ValidateChannel().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Slot longer than program rejected"), Channel(TEXT("Ch.Bad4"), { Entry(A, 0.0, 11.0) })->ValidateChannel().Outcome, EDocResultOutcome::InvalidConfiguration);

	// A long time jump selects the current program directly: one change event, no replay of skipped programs.
	FFixture F;
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Def).IsChanged());
	TestEqual(TEXT("Duplicate id conflicts"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.Main"), { Entry(A, 0.0, 10.0) })).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Same definition is NoChange"), F.Subsystem->RegisterChannel(Def).Outcome, EDocResultOutcome::NoChange);
	int32 Changes = 0;
	F.Subsystem->OnProgramChanged.AddLambda([&Changes](FName, FName, bool) { ++Changes; });
	F.Subsystem->AdvanceTime(3012.0f);
	double Cursor = 0.0;
	TestEqual(TEXT("Jumped straight to B"), F.Now(TEXT("Ch.Main"), Cursor), FName(TEXT("Prog.B")));
	TestEqual(TEXT("Cursor after jump"), Cursor, 2.0, Tol);
	TestEqual(TEXT("Exactly one change event for the jump"), Changes, 1);
	return true;
}

// BRC-02
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastLateReceiverTest, FAutomationTestBase, "Doc.Broadcast.LateReceiver", DocBroadcastTests::Flags)
bool FDocBroadcastLateReceiverTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	UDocBroadcastProgramDefinition* A = Program(TEXT("Prog.Long"), 60.0);
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.Radio"), { Entry(A, 0.0, 60.0) })).IsSuccess());
	UDocBroadcastReceiverComponent* Early = F.Receiver();
	TestTrue(TEXT("Tune early receiver"), F.Subsystem->TuneReceiver(Early->ReceiverId, TEXT("Ch.Radio")).IsChanged());
	const int32 OpensAfterRegister = F.Provider->OpenCount;
	const int32 Generation = F.Transport(TEXT("Ch.Radio")).TransportGeneration;

	F.Subsystem->AdvanceTime(37.0f);
	UDocBroadcastReceiverComponent* Late = F.Receiver();
	TestTrue(TEXT("Tune late receiver"), F.Subsystem->TuneReceiver(Late->ReceiverId, TEXT("Ch.Radio")).IsChanged());
	TestEqual(TEXT("Late receiver joins at 37s"), Late->LastObservedCursor, 37.0, Tol);
	TestEqual(TEXT("Same program"), Late->LastObservedProgramId, FName(TEXT("Prog.Long")));
	TestEqual(TEXT("Early receiver shares the cursor"), Early->LastObservedCursor, 37.0, Tol);
	TestEqual(TEXT("Joining does not reopen the channel"), F.Provider->OpenCount, OpensAfterRegister);
	TestEqual(TEXT("Joining does not bump the transport"), F.Transport(TEXT("Ch.Radio")).TransportGeneration, Generation);

	F.Subsystem->AdvanceTime(3.0f);
	TestEqual(TEXT("Both advance together (early)"), Early->LastObservedCursor, 40.0, Tol);
	TestEqual(TEXT("Both advance together (late)"), Late->LastObservedCursor, 40.0, Tol);

	UDocBroadcastReceiverComponent* PreTuned = F.Receiver(TEXT("Ch.Radio"));
	TestEqual(TEXT("Receiver pre-tuned in data joins on register"), PreTuned->LastObservedCursor, 40.0, Tol);
	TestEqual(TEXT("Observed state is Playing"), PreTuned->LastObservedState, EDocBroadcastTransportState::Playing);
	return true;
}

// BRC-03
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastMuteIndependenceTest, FAutomationTestBase, "Doc.Broadcast.MuteIndependence", DocBroadcastTests::Flags)
bool FDocBroadcastMuteIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.TV"), { Entry(Program(TEXT("Prog.Show"), 60.0), 0.0, 60.0) })).IsSuccess());
	UDocBroadcastReceiverComponent* R1 = F.Receiver(TEXT("Ch.TV"));
	UDocBroadcastReceiverComponent* R2 = F.Receiver(TEXT("Ch.TV"));
	F.Subsystem->AdvanceTime(10.0f);
	const FDocBroadcastTransport Before = F.Transport(TEXT("Ch.TV"));
	const int32 Opens = F.Provider->OpenCount;

	TestTrue(TEXT("Mute R1"), F.Subsystem->MuteReceiver(R1->ReceiverId, true).IsChanged());
	TestEqual(TEXT("Mute again is NoChange"), F.Subsystem->MuteReceiver(R1->ReceiverId, true).Outcome, EDocResultOutcome::NoChange);
	TestTrue(TEXT("R2 volume"), F.Subsystem->SetReceiverVolume(R2->ReceiverId, 0.25f).IsChanged());
	TestEqual(TEXT("NaN volume refused"), F.Subsystem->SetReceiverVolume(R2->ReceiverId, std::numeric_limits<float>::quiet_NaN()).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Detune R1"), F.Subsystem->TuneReceiver(R1->ReceiverId, NAME_None).IsChanged());
	TestTrue(TEXT("Retune R1"), F.Subsystem->TuneReceiver(R1->ReceiverId, TEXT("Ch.TV")).IsChanged());
	TestEqual(TEXT("Unknown channel refused"), F.Subsystem->TuneReceiver(R1->ReceiverId, TEXT("Ch.Missing")).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Refused tune keeps the old channel"), R1->TunedChannelId, FName(TEXT("Ch.TV")));

	const FDocBroadcastTransport After = F.Transport(TEXT("Ch.TV"));
	TestEqual(TEXT("Channel generation untouched"), After.TransportGeneration, Before.TransportGeneration);
	TestEqual(TEXT("Channel offset untouched"), After.ScheduleOffsetSeconds, Before.ScheduleOffsetSeconds, Tol);
	TestEqual(TEXT("No reopen"), F.Provider->OpenCount, Opens);
	TestEqual(TEXT("Retuned receiver is at the live cursor"), R1->LastObservedCursor, 10.0, Tol);
	TestEqual(TEXT("Muted receiver is silent"), R1->GetEffectiveGain(), 0.0f);
	TestEqual(TEXT("Other receiver keeps its own volume"), R2->GetEffectiveGain(), 0.25f);

	R2->DestroyComponent();
	TestEqual(TEXT("Unloaded receiver removed"), F.Subsystem->GetReceiverCount(), 1);
	F.Subsystem->AdvanceTime(1.0f);
	TestEqual(TEXT("Channel keeps running without it"), F.Transport(TEXT("Ch.TV")).ScheduleOffsetSeconds, 11.0, Tol);
	return true;
}

// BRC-04
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastInterruptionResumeTest, FAutomationTestBase, "Doc.Broadcast.InterruptionResume", DocBroadcastTests::Flags)
bool FDocBroadcastInterruptionResumeTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	const FName Ch = TEXT("Ch.PA");
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(Ch, { Entry(Program(TEXT("Prog.Music"), 120.0), 0.0, 120.0) })).IsSuccess());
	UDocBroadcastProgramDefinition* Ann = Program(TEXT("Prog.Announce"), 5.0);
	TArray<EDocBroadcastInterruptionStatus> Ended;
	F.Subsystem->OnInterruptionEnded.AddLambda([&Ended](FName, const FGuid&, EDocBroadcastInterruptionStatus S) { Ended.Add(S); });

	F.Subsystem->AdvanceTime(20.0f);
	FGuid PauseId;
	TestTrue(TEXT("Push pause-underlying"), F.Subsystem->PushInterruption(Ch, Interruption(FGuid::NewGuid(), Ann, 10, EDocBroadcastInterruptionPolicy::PauseUnderlyingCursor), PauseId).IsChanged());
	double Cursor = 0.0;
	bool bInt = false;
	TestEqual(TEXT("Announcement plays"), F.Now(Ch, Cursor, &bInt), FName(TEXT("Prog.Announce")));
	TestTrue(TEXT("Flagged as interruption"), bInt);
	TestEqual(TEXT("State Interrupted"), F.Transport(Ch).State, EDocBroadcastTransportState::Interrupted);
	F.Subsystem->AdvanceTime(2.0f);
	TestEqual(TEXT("Underlying held during announcement"), F.Transport(Ch).ScheduleOffsetSeconds, 20.0, Tol);
	F.Subsystem->AdvanceTime(3.0f);
	TestEqual(TEXT("Back to music"), F.Now(Ch, Cursor), FName(TEXT("Prog.Music")));
	TestEqual(TEXT("Pause policy resumes at the paused offset"), Cursor, 20.0, Tol);
	FDocBroadcastInterruption Record;
	TestTrue(TEXT("Record kept"), F.Subsystem->QueryInterruption(Ch, PauseId, Record));
	TestEqual(TEXT("Completed"), Record.Status, EDocBroadcastInterruptionStatus::Completed);

	FGuid ContinueId;
	TestTrue(TEXT("Push continue-schedule"), F.Subsystem->PushInterruption(Ch, Interruption(FGuid::NewGuid(), Ann), ContinueId).IsChanged());
	F.Subsystem->AdvanceTime(5.0f);
	TestEqual(TEXT("Continue policy returns to where the schedule advanced"), F.Now(Ch, Cursor), FName(TEXT("Prog.Music")));
	TestEqual(TEXT("Cursor advanced by the announcement length"), Cursor, 25.0, Tol);
	TestEqual(TEXT("Two completion events"), Ended.Num(), 2);
	TestEqual(TEXT("Playing again"), F.Transport(Ch).State, EDocBroadcastTransportState::Playing);
	return true;
}

// BRC-05
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastOwnerAndExpiryTest, FAutomationTestBase, "Doc.Broadcast.OwnerAndExpiry", DocBroadcastTests::Flags)
bool FDocBroadcastOwnerAndExpiryTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	const FName Ch = TEXT("Ch.Station");
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(Ch, { Entry(Program(TEXT("Prog.Music"), 120.0), 0.0, 120.0) })).IsSuccess());
	UDocBroadcastProgramDefinition* Ann1 = Program(TEXT("Prog.Ann1"), 10.0);
	UDocBroadcastProgramDefinition* Ann2 = Program(TEXT("Prog.Ann2"), 10.0);
	UDocBroadcastProgramDefinition* Ann3 = Program(TEXT("Prog.Ann3"), 10.0);
	const FGuid O1 = FGuid::NewGuid(), O2 = FGuid::NewGuid(), O3 = FGuid::NewGuid();
	double Cursor = 0.0;

	FGuid I1, I2, I3, I4, I5;
	TestTrue(TEXT("O1 normal"), F.Subsystem->PushInterruption(Ch, Interruption(O1, Ann1, 10), I1).IsSuccess());
	TestTrue(TEXT("O2 high"), F.Subsystem->PushInterruption(Ch, Interruption(O2, Ann2, 20), I2).IsSuccess());
	TestEqual(TEXT("Higher priority plays"), F.Now(Ch, Cursor), FName(TEXT("Prog.Ann2")));

	TestEqual(TEXT("O1 cannot release O2's claim"), F.Subsystem->ReleaseInterruption(Ch, I2, O1).Outcome, EDocResultOutcome::PermissionDenied);
	TestEqual(TEXT("O2 still on air"), F.Now(Ch, Cursor), FName(TEXT("Prog.Ann2")));
	TestTrue(TEXT("O1 releases its own"), F.Subsystem->ReleaseInterruption(Ch, I1, O1).IsChanged());
	TestEqual(TEXT("Releasing O1 does not stop O2"), F.Now(Ch, Cursor), FName(TEXT("Prog.Ann2")));

	FDocBroadcastInterruption Queued = Interruption(O3, Ann3, 0);
	Queued.ExpiryTimeSeconds = F.Transport(Ch).ClockSeconds + 2.0;
	TestTrue(TEXT("Low-priority queued with expiry"), F.Subsystem->PushInterruption(Ch, Queued, I3).IsSuccess());
	F.Subsystem->AdvanceTime(3.0f);
	FDocBroadcastInterruption Record;
	TestTrue(TEXT("Expired record"), F.Subsystem->QueryInterruption(Ch, I3, Record));
	TestEqual(TEXT("Queued request expired"), Record.Status, EDocBroadcastInterruptionStatus::Expired);
	TestFalse(TEXT("It never played"), Record.bHasStarted);
	TestEqual(TEXT("O2 kept playing"), F.Now(Ch, Cursor), FName(TEXT("Prog.Ann2")));
	TestEqual(TEXT("O2 played 3s"), Cursor, 3.0, Tol);

	TestEqual(TEXT("Reject policy refuses under higher priority"), F.Subsystem->PushInterruption(Ch, Interruption(O1, Ann1, 10, EDocBroadcastInterruptionPolicy::ContinueUnderlyingSchedule, EDocBroadcastArbitration::Reject), I4).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Rejected record"), F.Subsystem->QueryInterruption(Ch, I4, Record) && Record.Status == EDocBroadcastInterruptionStatus::Rejected);

	TestTrue(TEXT("Replace policy"), F.Subsystem->PushInterruption(Ch, Interruption(O3, Ann3, 30, EDocBroadcastInterruptionPolicy::ContinueUnderlyingSchedule, EDocBroadcastArbitration::Replace), I5).IsSuccess());
	TestTrue(TEXT("O2 replaced"), F.Subsystem->QueryInterruption(Ch, I2, Record) && Record.Status == EDocBroadcastInterruptionStatus::Replaced);
	TestEqual(TEXT("Replacement plays"), F.Now(Ch, Cursor), FName(TEXT("Prog.Ann3")));
	TestEqual(TEXT("Releasing an ended claim is NoChange"), F.Subsystem->ReleaseInterruption(Ch, I2, O2).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Unknown id"), F.Subsystem->ReleaseInterruption(Ch, FGuid::NewGuid(), O2).Outcome, EDocResultOutcome::NotFound);
	FGuid Unused;
	TestEqual(TEXT("Owner handle required"), F.Subsystem->PushInterruption(Ch, Interruption(FGuid(), Ann1), Unused).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

// BRC-06
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastAsyncOpenFailureTest, FAutomationTestBase, "Doc.Broadcast.AsyncOpenFailure", DocBroadcastTests::Flags)
bool FDocBroadcastAsyncOpenFailureTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	UDocBroadcastProgramDefinition* A = Program(TEXT("Prog.A"), 10.0);
	UDocBroadcastProgramDefinition* B = Program(TEXT("Prog.B"), 10.0);
	F.Provider->bDeferred = true;
	const FName Ch = TEXT("Ch.Async");
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(Ch, { Entry(A, 0.0, 10.0), Entry(B, 10.0, 10.0) })).IsSuccess());
	TestEqual(TEXT("Open requested is not readiness"), F.Transport(Ch).State, EDocBroadcastTransportState::Loading);
	TArray<int64> Pending = F.Provider->GetPendingTickets();
	TestEqual(TEXT("One pending open"), Pending.Num(), 1);
	const int64 StaleA = Pending.Num() > 0 ? Pending[0] : 0;

	F.Subsystem->AdvanceTime(11.0f);
	TestFalse(TEXT("Late ready for the replaced program is ignored"), F.Provider->DeliverTicket(StaleA, /*bForceSuccess*/ true));
	TestEqual(TEXT("Still loading B"), F.Transport(Ch).State, EDocBroadcastTransportState::Loading);
	TestEqual(TEXT("Delivering B"), F.Provider->DeliverAll(), 1);
	TestEqual(TEXT("B playing"), F.Transport(Ch).State, EDocBroadcastTransportState::Playing);

	const FGuid Owner = FGuid::NewGuid();
	FGuid AnnId;
	TestTrue(TEXT("Push announcement"), F.Subsystem->PushInterruption(Ch, Interruption(Owner, Program(TEXT("Prog.Ann"), 5.0)), AnnId).IsSuccess());
	Pending = F.Provider->GetPendingTickets();
	const int64 AnnTicket = Pending.Num() > 0 ? Pending[0] : 0;
	TestTrue(TEXT("Release before it is ready"), F.Subsystem->ReleaseInterruption(Ch, AnnId, Owner).IsChanged());
	TestFalse(TEXT("Cancelled announcement cannot start on a late callback"), F.Provider->DeliverTicket(AnnTicket, true));
	TestTrue(TEXT("Not interrupted"), F.Transport(Ch).State != EDocBroadcastTransportState::Interrupted);
	F.Provider->DeliverAll();
	TestEqual(TEXT("Back to B"), F.Transport(Ch).State, EDocBroadcastTransportState::Playing);

	// Failure policies (immediate provider).
	F.Provider->bDeferred = false;
	F.Provider->FailingProgramIds = { TEXT("Prog.Bad") };
	UDocBroadcastProgramDefinition* Bad = Program(TEXT("Prog.Bad"), 10.0);
	UDocBroadcastProgramDefinition* Good = Program(TEXT("Prog.Good"), 10.0);

	UDocBroadcastChannelDefinition* RetryDef = Channel(TEXT("Ch.Retry"), { Entry(Bad, 0.0, 10.0), Entry(Good, 10.0, 10.0) });
	RetryDef->SourceFailurePolicy = EDocBroadcastSourceFailurePolicy::RetryBounded;
	RetryDef->MaxOpenRetries = 2;
	int32 Opens = F.Provider->OpenCount;
	TestTrue(TEXT("Register retry channel"), F.Subsystem->RegisterChannel(RetryDef).IsSuccess());
	TestEqual(TEXT("Bounded: 1 + 2 retries"), F.Provider->OpenCount - Opens, 3);
	TestEqual(TEXT("Then silent"), F.Transport(TEXT("Ch.Retry")).State, EDocBroadcastTransportState::UnavailableSource);
	TestEqual(TEXT("Failures counted"), F.Transport(TEXT("Ch.Retry")).SourceFailureCount, 3);

	UDocBroadcastChannelDefinition* GapDef = Channel(TEXT("Ch.Gap"), { Entry(Bad, 0.0, 10.0), Entry(Good, 10.0, 10.0) });
	GapDef->SourceFailurePolicy = EDocBroadcastSourceFailurePolicy::SilenceGap;
	Opens = F.Provider->OpenCount;
	TestTrue(TEXT("Register gap channel"), F.Subsystem->RegisterChannel(GapDef).IsSuccess());
	TestEqual(TEXT("No retry"), F.Provider->OpenCount - Opens, 1);
	UDocBroadcastChannelDefinition* SkipDef = Channel(TEXT("Ch.Skip"), { Entry(Bad, 0.0, 10.0), Entry(Good, 10.0, 10.0) });
	SkipDef->SourceFailurePolicy = EDocBroadcastSourceFailurePolicy::Skip;
	TestTrue(TEXT("Register skip channel"), F.Subsystem->RegisterChannel(SkipDef).IsSuccess());
	double Cursor = 0.0;
	TestEqual(TEXT("Skip jumps to the next program"), F.Now(TEXT("Ch.Skip"), Cursor), FName(TEXT("Prog.Good")));
	TestEqual(TEXT("Skip starts it from the top"), Cursor, 0.0, Tol);
	TestEqual(TEXT("Skip playing"), F.Transport(TEXT("Ch.Skip")).State, EDocBroadcastTransportState::Playing);

	F.Subsystem->AdvanceTime(5.0f);
	TestEqual(TEXT("Silence gap keeps schedule time"), F.Transport(TEXT("Ch.Gap")).ScheduleOffsetSeconds, 5.0, Tol);
	TestEqual(TEXT("Still silent"), F.Transport(TEXT("Ch.Gap")).State, EDocBroadcastTransportState::UnavailableSource);
	F.Subsystem->AdvanceTime(5.0f);
	TestEqual(TEXT("Next program plays after the gap"), F.Transport(TEXT("Ch.Gap")).State, EDocBroadcastTransportState::Playing);
	TestEqual(TEXT("Retry channel recovers on the next program"), F.Transport(TEXT("Ch.Retry")).State, EDocBroadcastTransportState::Playing);

	FGuid BadAnn;
	TestTrue(TEXT("Push failing announcement"), F.Subsystem->PushInterruption(TEXT("Ch.Gap"), Interruption(Owner, Bad), BadAnn).IsSuccess());
	FDocBroadcastInterruption Record;
	TestTrue(TEXT("Failed announcement ends"), F.Subsystem->QueryInterruption(TEXT("Ch.Gap"), BadAnn, Record) && Record.Status == EDocBroadcastInterruptionStatus::SourceFailed);
	TestEqual(TEXT("Schedule resumes"), F.Transport(TEXT("Ch.Gap")).State, EDocBroadcastTransportState::Playing);
	return true;
}

// BRC-07
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastSeekCapabilityTest, FAutomationTestBase, "Doc.Broadcast.SeekCapability", DocBroadcastTests::Flags)
bool FDocBroadcastSeekCapabilityTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	UDocBroadcastProgramDefinition* Unknown = Program(TEXT("Prog.Stream"), 60.0, true, /*bKnownDuration*/ false);
	UDocBroadcastProgramDefinition* NoSeek = Program(TEXT("Prog.NoSeek"), 60.0, /*bCanSeek*/ false);
	UDocBroadcastProgramDefinition* Fine = Program(TEXT("Prog.Fine"), 60.0);
	TestEqual(TEXT("Unknown duration fails explicitly"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.U"), { Entry(Unknown, 0.0, 60.0) })).Outcome, EDocResultOutcome::Unsupported);
	TestEqual(TEXT("Unseekable scheduled program fails explicitly"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.N"), { Entry(NoSeek, 0.0, 60.0) })).Outcome, EDocResultOutcome::Unsupported);
	F.Provider->bCanSeek = false;
	TestEqual(TEXT("Backend without seeking fails"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.P1"), { Entry(Fine, 0.0, 60.0) })).Outcome, EDocResultOutcome::Unsupported);
	F.Provider->bCanSeek = true;
	F.Provider->bKnownDuration = false;
	TestEqual(TEXT("Backend without duration fails"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.P2"), { Entry(Fine, 0.0, 60.0) })).Outcome, EDocResultOutcome::Unsupported);
	F.Provider->bKnownDuration = true;

	const FName Ch = TEXT("Ch.Seek");
	TestTrue(TEXT("Register seekable"), F.Subsystem->RegisterChannel(Channel(Ch, { Entry(Fine, 0.0, 60.0) })).IsSuccess());
	FGuid Unused;
	TestEqual(TEXT("Unknown-duration announcement fails"), F.Subsystem->PushInterruption(Ch, Interruption(FGuid::NewGuid(), Unknown), Unused).Outcome, EDocResultOutcome::Unsupported);

	F.Subsystem->AdvanceTime(10.0f);
	const int32 Revision = F.Transport(Ch).ScheduleRevision;
	TestTrue(TEXT("Authorized seek"), F.Subsystem->SeekChannelAuthorized(Ch, 30.0).IsChanged());
	double Cursor = 0.0;
	F.Now(Ch, Cursor);
	TestEqual(TEXT("Cursor moved"), Cursor, 30.0, Tol);
	TestEqual(TEXT("Backend reopened at the target"), F.Provider->LastOpenCursorSeconds, 30.0, Tol);
	TestEqual(TEXT("Revision bumped"), F.Transport(Ch).ScheduleRevision, Revision + 1);
	TestEqual(TEXT("NaN seek"), F.Subsystem->SeekChannelAuthorized(Ch, std::numeric_limits<double>::quiet_NaN()).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Seek past the slot"), F.Subsystem->SeekChannelAuthorized(Ch, 60.0).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Negative seek"), F.Subsystem->SeekChannelAuthorized(Ch, -1.0).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Push announcement"), F.Subsystem->PushInterruption(Ch, Interruption(FGuid::NewGuid(), Program(TEXT("Prog.Ann"), 5.0)), Unused).IsSuccess());
	TestEqual(TEXT("No schedule seek while interrupted"), F.Subsystem->SeekChannelAuthorized(Ch, 5.0).Outcome, EDocResultOutcome::Conflict);
	return true;
}

// BRC-08
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastUnloadRestoreTest, FAutomationTestBase, "Doc.Broadcast.UnloadRestore", DocBroadcastTests::Flags)
bool FDocBroadcastUnloadRestoreTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	UDocBroadcastChannelDefinition* Def = Channel(TEXT("Ch.Save"), { Entry(Program(TEXT("Prog.A"), 30.0), 0.0, 30.0), Entry(Program(TEXT("Prog.B"), 30.0), 30.0, 30.0) });
	FDocBroadcastTransport Saved;
	{
		FFixture F;
		TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Def).IsSuccess());
		UDocBroadcastReceiverComponent* R = F.Receiver(TEXT("Ch.Save"));
		F.Subsystem->AdvanceTime(45.0f);
		Saved = F.Subsystem->CaptureTransportState(TEXT("Ch.Save"));
		TestEqual(TEXT("Saved in B"), Saved.CurrentProgramId, FName(TEXT("Prog.B")));

		R->UnregisterComponent();
		TestEqual(TEXT("Unloaded"), F.Subsystem->GetReceiverCount(), 0);
		F.Subsystem->AdvanceTime(10.0f);
		R->RegisterComponent();
		TestEqual(TEXT("Reloaded receiver rejoins the live cursor"), R->LastObservedCursor, 25.0, Tol);
		TestEqual(TEXT("Channel was not reset by the unload"), F.Transport(TEXT("Ch.Save")).ScheduleOffsetSeconds, 55.0, Tol);
	}

	FFixture G;
	TestTrue(TEXT("Register in new world"), G.Subsystem->RegisterChannel(Def).IsSuccess());
	FGuid Lease;
	TestTrue(TEXT("Transient lease"), G.Subsystem->PushInterruption(TEXT("Ch.Save"), Interruption(FGuid::NewGuid(), Program(TEXT("Prog.Ann"), 5.0)), Lease).IsSuccess());
	int32 ProgramEvents = 0, EndEvents = 0;
	G.Subsystem->OnProgramChanged.AddLambda([&ProgramEvents](FName, FName, bool) { ++ProgramEvents; });
	G.Subsystem->OnInterruptionEnded.AddLambda([&EndEvents](FName, const FGuid&, EDocBroadcastInterruptionStatus) { ++EndEvents; });

	const FDocSystemResult Restored = G.Subsystem->StageRestoreTransportState(Saved);
	TestTrue(TEXT("Restore"), Restored.IsChanged());
	double Cursor = 0.0;
	TestEqual(TEXT("Restored program"), G.Now(TEXT("Ch.Save"), Cursor), FName(TEXT("Prog.B")));
	TestEqual(TEXT("Restored cursor"), Cursor, 15.0, Tol);
	TestEqual(TEXT("No program events replayed"), ProgramEvents, 0);
	TestEqual(TEXT("No interruption events replayed"), EndEvents, 0);
	FDocBroadcastInterruption Record;
	TestTrue(TEXT("Stale lease cancelled"), G.Subsystem->QueryInterruption(TEXT("Ch.Save"), Lease, Record) && Record.Status == EDocBroadcastInterruptionStatus::Cancelled);
	UDocBroadcastReceiverComponent* R2 = G.Receiver(TEXT("Ch.Save"));
	TestEqual(TEXT("New receiver joins restored cursor"), R2->LastObservedCursor, 15.0, Tol);

	FDocBroadcastTransport Migrated = Saved;
	Migrated.ScheduleVersion = 99;
	const FDocSystemResult MigratedResult = G.Subsystem->StageRestoreTransportState(Migrated);
	TestTrue(TEXT("Version mismatch migrates"), MigratedResult.IsSuccess());
	TestTrue(TEXT("Migration reported"), MigratedResult.Diagnostic.Contains(TEXT("ScheduleVersionMigrated")));

	FDocBroadcastTransport Corrupt = Saved;
	Corrupt.AnchorEpochSeconds = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("Corrupt save refused"), G.Subsystem->StageRestoreTransportState(Corrupt).Outcome, EDocResultOutcome::InvalidInput);
	FDocBroadcastTransport Missing = Saved;
	Missing.ChannelId = TEXT("Ch.Gone");
	TestEqual(TEXT("Unknown channel"), G.Subsystem->StageRestoreTransportState(Missing).Outcome, EDocResultOutcome::NotFound);
	return true;
}

// BRC-09
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastClockMappingTest, FAutomationTestBase, "Doc.Broadcast.ClockMapping", DocBroadcastTests::Flags)
bool FDocBroadcastClockMappingTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	UDocBroadcastProgramDefinition* A = Program(TEXT("Prog.A"), 10.0);
	UDocBroadcastProgramDefinition* B = Program(TEXT("Prog.B"), 10.0);
	UDocBroadcastProgramDefinition* C = Program(TEXT("Prog.C"), 10.0);
	const TArray<FDocBroadcastScheduleEntry> Entries = { Entry(A, 0.0, 10.0), Entry(B, 10.0, 10.0), Entry(C, 20.0, 10.0) };
	UDocBroadcastChannelDefinition* RealDef = Channel(TEXT("Ch.Real"), Entries);
	UDocBroadcastChannelDefinition* FicDef = Channel(TEXT("Ch.Fic"), Entries);
	FicDef->ClockPolicy = EDocBroadcastClockPolicy::ReanchorAtScheduleBoundary;
	TestTrue(TEXT("Register real"), F.Subsystem->RegisterChannel(RealDef).IsSuccess());
	TestTrue(TEXT("Register fictional"), F.Subsystem->RegisterChannel(FicDef).IsSuccess());
	TestTrue(TEXT("Scale real"), F.Subsystem->SetScheduleClockScale(TEXT("Ch.Real"), 60.0f).IsSuccess());
	TestTrue(TEXT("Scale fictional"), F.Subsystem->SetScheduleClockScale(TEXT("Ch.Fic"), 60.0f).IsSuccess());

	F.Subsystem->AdvanceTime(5.0f);
	double Cursor = 0.0;
	TestEqual(TEXT("Real: A"), F.Now(TEXT("Ch.Real"), Cursor), FName(TEXT("Prog.A")));
	TestEqual(TEXT("Programs play at normal speed (real)"), Cursor, 5.0, Tol);
	TestEqual(TEXT("Fictional: still A mid-program"), F.Now(TEXT("Ch.Fic"), Cursor), FName(TEXT("Prog.A")));
	TestEqual(TEXT("Programs play at normal speed (fictional)"), Cursor, 5.0, Tol);

	F.Subsystem->AdvanceTime(5.0f);
	TestEqual(TEXT("FollowRealTransport moves on to B"), F.Now(TEXT("Ch.Real"), Cursor), FName(TEXT("Prog.B")));
	TestEqual(TEXT("B from the top"), Cursor, 0.0, Tol);
	// Fictional clock is at 600s -> 600 mod 30 = 0 -> entry A, started from its beginning.
	TestEqual(TEXT("Reanchor jumps to the fictional clock's entry at the boundary"), F.Now(TEXT("Ch.Fic"), Cursor), FName(TEXT("Prog.A")));
	TestEqual(TEXT("Reanchored entry starts from the top"), Cursor, 0.0, Tol);
	TestEqual(TEXT("Reanchor recorded"), F.Transport(TEXT("Ch.Fic")).ScheduleRevision, 1);
	TestEqual(TEXT("Real channel never reanchors"), F.Transport(TEXT("Ch.Real")).ScheduleRevision, 0);
	TestEqual(TEXT("Invalid scale"), F.Subsystem->SetScheduleClockScale(TEXT("Ch.Fic"), std::numeric_limits<float>::quiet_NaN()).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

// BRC-10
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocBroadcastCookedAudioTest, FAutomationTestBase, "Doc.Broadcast.CookedAudio", DocBroadcastTests::Flags)
bool FDocBroadcastCookedAudioTest::RunTest(const FString& Parameters)
{
	using namespace DocBroadcastTests;
	FFixture F;
	TestTrue(TEXT("Register"), F.Subsystem->RegisterChannel(Channel(TEXT("Ch.Audio"), { Entry(Program(TEXT("Prog.A"), 10.0), 0.0, 10.0) })).IsSuccess());
	TestFalse(TEXT("Logic-only provider is not audible"), F.Provider->GetCapabilities(nullptr).bAudible);
	double Tolerance = -1.0;
	const FDocSystemResult Measured = F.Subsystem->MeasureLocalSyncTolerance(TEXT("Ch.Audio"), Tolerance);
	TestEqual(TEXT("No measurement is claimed"), Measured.Outcome, EDocResultOutcome::Unsupported);
	TestEqual(TEXT("No tolerance value invented"), Tolerance, 0.0);
	AddInfo(TEXT("BRC-10: bundled cooked playback and local sync tolerance are not measured in automation; run the manual cooked-audio gate."));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
