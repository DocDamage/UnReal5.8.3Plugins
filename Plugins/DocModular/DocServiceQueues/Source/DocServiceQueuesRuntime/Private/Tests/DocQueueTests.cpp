#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocServiceQueueSubsystem.h"
#include "DocQueueStationComponent.h"
#include "DocQueueParticipantComponent.h"
#include "DocServiceQueueDefinition.h"
#include "UObject/Package.h"
#include "Engine/World.h"

namespace DocQueueTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UDocServiceQueueDefinition* CreateTestDefinition(FName QueueId, EDocQueueOrderingPolicy Policy = EDocQueueOrderingPolicy::StrictFIFO, int32 MaxBypass = 3, float Timeout = 10.0f)
	{
		UDocServiceQueueDefinition* Def = NewObject<UDocServiceQueueDefinition>(GetTransientPackage());
		Def->QueueId = QueueId;
		Def->OrderingPolicy = Policy;
		Def->MaxBypassCount = MaxBypass;
		Def->OfferTimeoutSeconds = Timeout;
		Def->ArrivalTimeoutSeconds = Timeout;
		return Def;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocServiceQueueSubsystem* Subsystem = nullptr;
		TSharedRef<double> Clock = MakeShared<double>(0.0);

		FFixture()
		{
			Subsystem = TW.GetSubsystem<UDocServiceQueueSubsystem>();
			check(Subsystem);
			TSharedRef<double> C = Clock;
			Subsystem->SetClockOverride([C]() { return *C; });
		}

		void SetTime(double T) { *Clock = T; }

		FDocQueueTicket Join(FName QueueId, int32 Party, int32 Priority = 0)
		{
			FDocQueueTicket T;
			Subsystem->JoinQueue(QueueId, FGuid::NewGuid(), Party, Priority, T);
			return T;
		}

		FDocStationInfo Station(FName StationId) const
		{
			FDocStationInfo Info;
			Subsystem->QueryStation(StationId, Info);
			return Info;
		}

		EDocQueueTicketState State(const FGuid& TicketId) const
		{
			FDocQueueTicket T;
			Subsystem->QueryTicket(TicketId, T);
			return T.State;
		}
	};
}

// QUE-01: Doc.Queue.FIFOAndPriority
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueFIFOAndPriorityTest, FAutomationTestBase, "Doc.Queue.FIFOAndPriority", DocQueueTests::Flags)
bool FDocQueueFIFOAndPriorityTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;

	// Strict FIFO by join ordinal.
	{
		const FName Q = TEXT("Q_FIFO");
		F.Subsystem->RegisterQueue(CreateTestDefinition(Q));
		F.Subsystem->RegisterStation(TEXT("Station1"), 5, FGameplayTagContainer());
		const FDocQueueTicket T1 = F.Join(Q, 1);
		const FDocQueueTicket T2 = F.Join(Q, 1);
		F.Join(Q, 1);
		FDocAdmissionOffer O1, O2;
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station1"), O1);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station1"), O2);
		TestEqual(TEXT("First offer is the FIFO head"), O1.TicketId, T1.TicketId);
		TestEqual(TEXT("Second offer is next in FIFO"), O2.TicketId, T2.TicketId);
	}

	// Priority then FIFO, with stable ties.
	{
		const FName Q = TEXT("Q_Priority");
		UDocServiceQueueDefinition* Def = CreateTestDefinition(Q, EDocQueueOrderingPolicy::PriorityThenFIFO);
		Def->MaxSelfAssignedPriority = 10;
		F.Subsystem->RegisterQueue(Def);
		F.Subsystem->RegisterStation(TEXT("StationP"), 5, FGameplayTagContainer());
		F.Join(Q, 1, 1);
		const FDocQueueTicket THigh = F.Join(Q, 1, 10);
		const FDocQueueTicket TMid = F.Join(Q, 1, 5);
		const FDocQueueTicket TTie = F.Join(Q, 1, 5);
		FDocAdmissionOffer O1, O2, O3;
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationP"), O1);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationP"), O2);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationP"), O3);
		TestEqual(TEXT("Highest priority first"), O1.TicketId, THigh.TicketId);
		TestEqual(TEXT("Equal priority: earlier join first"), O2.TicketId, TMid.TicketId);
		TestEqual(TEXT("Equal priority: later join second"), O3.TicketId, TTie.TicketId);
	}

	// Participant-supplied priority is a request; the authority grants priority.
	{
		const FName Q = TEXT("Q_Untrusted");
		F.Subsystem->RegisterQueue(CreateTestDefinition(Q, EDocQueueOrderingPolicy::PriorityThenFIFO));
		F.Subsystem->RegisterStation(TEXT("StationU"), 5, FGameplayTagContainer());
		const FDocQueueTicket TA = F.Join(Q, 1, 0);
		const FDocQueueTicket TB = F.Join(Q, 1, 99);
		const FDocQueueTicket TC = F.Join(Q, 1, 0);
		TestEqual(TEXT("Self-assigned priority is clamped"), TB.PriorityClass, 0);
		TestEqual(TEXT("The request is kept for inspection"), TB.RequestedPriority, 99);
		TestTrue(TEXT("Authority grants priority"), F.Subsystem->SetTicketPriority(TC.TicketId, 5).IsSuccess());
		FDocAdmissionOffer O1, O2;
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationU"), O1);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationU"), O2);
		TestEqual(TEXT("Granted priority first"), O1.TicketId, TC.TicketId);
		TestEqual(TEXT("Then FIFO, not the self-claimed 99"), O2.TicketId, TA.TicketId);
	}

	// Duplicate join keys.
	{
		const FName Q = TEXT("Q_Dup");
		F.Subsystem->RegisterQueue(CreateTestDefinition(Q));
		const FGuid P = FGuid::NewGuid();
		FDocQueueTicket First, Again, Other;
		TestTrue(TEXT("Join"), F.Subsystem->JoinQueue(Q, P, 2, 0, First).IsChanged());
		const FDocSystemResult Repeat = F.Subsystem->JoinQueue(Q, P, 2, 0, Again);
		TestEqual(TEXT("Same participant and party returns the existing ticket"), Repeat.Outcome, EDocResultOutcome::NoChange);
		TestEqual(TEXT("Existing ticket id"), Again.TicketId, First.TicketId);
		TestEqual(TEXT("Different party for the same key is a conflict"), F.Subsystem->JoinQueue(Q, P, 3, 0, Other).Outcome, EDocResultOutcome::Conflict);
		FDocQueueView View;
		F.Subsystem->QueryQueueView(Q, View);
		TestEqual(TEXT("Only one ticket queued"), View.TotalWaitingTickets, 1);
	}
	return true;
}

// QUE-02: Doc.Queue.MultiStationReservation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueMultiStationReservationTest, FAutomationTestBase, "Doc.Queue.MultiStationReservation", DocQueueTests::Flags)
bool FDocQueueMultiStationReservationTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Multi");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q));
	F.Subsystem->RegisterStation(TEXT("StationA"), 2, FGameplayTagContainer());
	F.Subsystem->RegisterStation(TEXT("StationB"), 2, FGameplayTagContainer());

	const FDocQueueTicket T1 = F.Join(Q, 1);
	FDocAdmissionOffer OfferA, OfferB;
	TestTrue(TEXT("Station A offers T1"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationA"), OfferA).IsSuccess());
	TestEqual(TEXT("Offer A is T1"), OfferA.TicketId, T1.TicketId);
	TestFalse(TEXT("Station B cannot also reserve T1"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationB"), OfferB).IsSuccess());
	TestEqual(TEXT("Station B holds nothing"), F.Station(TEXT("StationB")).ReservedSeats, 0);

	const FDocQueueTicket T2 = F.Join(Q, 1);
	TestTrue(TEXT("Station B offers the next ticket"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationB"), OfferB).IsSuccess());
	TestEqual(TEXT("Offer B is T2"), OfferB.TicketId, T2.TicketId);

	FDocServiceBatch Batch;
	TestFalse(TEXT("A batch cannot take already-offered tickets"), F.Subsystem->FormBatch(Q, TEXT("StationA"), Batch).IsSuccess());
	return true;
}

// QUE-03: Doc.Queue.GroupAtomicity
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueGroupAtomicityTest, FAutomationTestBase, "Doc.Queue.GroupAtomicity", DocQueueTests::Flags)
bool FDocQueueGroupAtomicityTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Group");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q));
	F.Subsystem->RegisterStation(TEXT("Station3"), 3, FGameplayTagContainer());

	const FDocQueueTicket Big = F.Join(Q, 4);
	FDocAdmissionOffer Offer;
	TestFalse(TEXT("Party of 4 cannot partially enter a 3-seat station"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), Offer).IsSuccess());
	TestEqual(TEXT("Reserved seats stays 0"), F.Station(TEXT("Station3")).ReservedSeats, 0);
	FDocQueueView View;
	F.Subsystem->QueryQueueView(Q, View);
	TestEqual(TEXT("Explicit reason for the oversized group"), View.HeadBlockedReason, FName(TEXT("OversizedForStation")));

	// The oversized group does not block the queue forever.
	const FDocQueueTicket Pair = F.Join(Q, 2);
	TestTrue(TEXT("Party behind the oversized group is served"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), Offer).IsSuccess());
	TestEqual(TEXT("It is the pair"), Offer.TicketId, Pair.TicketId);
	TestEqual(TEXT("Oversized group still waiting"), F.State(Big.TicketId), EDocQueueTicketState::Waiting);

	// Only 1 seat left: a pair is not split.
	F.Join(Q, 2);
	TestFalse(TEXT("A pair does not fit into one seat"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), Offer).IsSuccess());
	TestEqual(TEXT("No partial reservation"), F.Station(TEXT("Station3")).ReservedSeats, 2);
	return true;
}

// QUE-04: Doc.Queue.BypassAndStarvation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueBypassAndStarvationTest, FAutomationTestBase, "Doc.Queue.BypassAndStarvation", DocQueueTests::Flags)
bool FDocQueueBypassAndStarvationTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;

	// Bypass-count limit.
	{
		const FName Q = TEXT("Q_Starve");
		F.Subsystem->RegisterQueue(CreateTestDefinition(Q, EDocQueueOrderingPolicy::FirstFitWithBypassLimit, 2));
		F.Subsystem->RegisterStation(TEXT("Station3"), 3, FGameplayTagContainer());
		const FDocQueueTicket TFirst = F.Join(Q, 1);
		const FDocQueueTicket TLarge = F.Join(Q, 3);
		const FDocQueueTicket S1 = F.Join(Q, 1);
		const FDocQueueTicket S2 = F.Join(Q, 1);
		const FDocQueueTicket S3 = F.Join(Q, 1);

		FDocAdmissionOffer O0, O1, O2, O3, O4;
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), O0);
		TestEqual(TEXT("Head served first"), O0.TicketId, TFirst.TicketId);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), O1);
		TestEqual(TEXT("First bypass offers S1"), O1.TicketId, S1.TicketId);
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), O2);
		TestEqual(TEXT("Second bypass offers S2"), O2.TicketId, S2.TicketId);
		F.Subsystem->DeclineOffer(O1.OfferId);
		F.Subsystem->DeclineOffer(O2.OfferId);

		TestFalse(TEXT("Bypass limit stops a third bypass"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), O3).IsSuccess());
		FDocQueueTicket Large;
		F.Subsystem->QueryTicket(TLarge.TicketId, Large);
		TestEqual(TEXT("Bypass counted only when someone was actually admitted"), Large.BypassCount, 2);
		FDocQueueView View;
		F.Subsystem->QueryQueueView(Q, View);
		TestEqual(TEXT("Reason visible"), View.HeadBlockedReason, FName(TEXT("BypassLimitReached")));
		TestEqual(TEXT("S3 was not offered"), F.State(S3.TicketId), EDocQueueTicketState::Waiting);

		F.Subsystem->LeaveQueue(TFirst.TicketId); // Leaving (not declining) frees the seat without re-queueing ahead
		TestTrue(TEXT("Once capacity frees, the protected group is served"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station3"), O4).IsSuccess());
		TestEqual(TEXT("It is the large group"), O4.TicketId, TLarge.TicketId);
	}

	// Age limit.
	{
		const FName Q = TEXT("Q_Age");
		UDocServiceQueueDefinition* Def = CreateTestDefinition(Q, EDocQueueOrderingPolicy::FirstFitWithBypassLimit, 100);
		Def->MaxBypassAgeSeconds = 5.0f;
		F.Subsystem->RegisterQueue(Def);
		F.Subsystem->RegisterStation(TEXT("Station2"), 2, FGameplayTagContainer());
		F.SetTime(0.0);
		F.Join(Q, 1);
		const FDocQueueTicket Big = F.Join(Q, 2);
		const FDocQueueTicket Small = F.Join(Q, 1);
		FDocAdmissionOffer O0, O1, O2;
		F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station2"), O0);
		F.SetTime(1.0);
		TestTrue(TEXT("Young group may be bypassed"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station2"), O1).IsSuccess());
		TestEqual(TEXT("Bypassed by the small party"), O1.TicketId, Small.TicketId);
		F.Subsystem->DeclineOffer(O1.OfferId);
		F.SetTime(6.0);
		TestFalse(TEXT("After the age limit it can no longer be bypassed"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("Station2"), O2).IsSuccess());
		TestEqual(TEXT("Big group still waiting"), F.State(Big.TicketId), EDocQueueTicketState::Waiting);
	}
	return true;
}

// QUE-05: Doc.Queue.OfferExpiry
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueOfferExpiryTest, FAutomationTestBase, "Doc.Queue.OfferExpiry", DocQueueTests::Flags)
bool FDocQueueOfferExpiryTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Expiry");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q, EDocQueueOrderingPolicy::StrictFIFO, 3, 5.0f));
	F.Subsystem->RegisterStation(TEXT("StationExp"), 4, FGameplayTagContainer());

	const FDocQueueTicket TA = F.Join(Q, 2);
	F.Join(Q, 1);
	FDocAdmissionOffer OA, OB;
	F.SetTime(0.0);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationExp"), OA); // deadline 5
	F.SetTime(3.0);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationExp"), OB); // deadline 8
	TestEqual(TEXT("Both offers hold seats"), F.Station(TEXT("StationExp")).ReservedSeats, 3);

	F.SetTime(6.0);
	TestEqual(TEXT("One offer expired"), F.Subsystem->CheckExpirations(), 1);
	TestEqual(TEXT("Exactly A's two seats released"), F.Station(TEXT("StationExp")).ReservedSeats, 1);
	TestEqual(TEXT("A expired"), F.State(TA.TicketId), EDocQueueTicketState::Expired);
	FDocStationReservation Res;
	TestFalse(TEXT("Late accept of A rejected"), F.Subsystem->AcceptOffer(OA.OfferId, Res).IsSuccess());
	TestTrue(TEXT("B still accepts"), F.Subsystem->AcceptOffer(OB.OfferId, Res).IsSuccess());
	TestEqual(TEXT("B's seat is now a reservation"), F.Station(TEXT("StationExp")).ReservedSeats, 1);

	// Accept exactly at the deadline, without a sweep in between.
	const FDocQueueTicket TC = F.Join(Q, 1);
	FDocAdmissionOffer OC;
	F.SetTime(10.0);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationExp"), OC);
	F.SetTime(15.0);
	TestEqual(TEXT("Accept at the deadline times out"), F.Subsystem->AcceptOffer(OC.OfferId, Res).Outcome, EDocResultOutcome::TimedOut);
	TestEqual(TEXT("And releases C's seat"), F.Station(TEXT("StationExp")).ReservedSeats, 1);
	TestEqual(TEXT("C expired"), F.State(TC.TicketId), EDocQueueTicketState::Expired);
	return true;
}

// QUE-06: Doc.Queue.ArrivalAndCompletion
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueArrivalAndCompletionTest, FAutomationTestBase, "Doc.Queue.ArrivalAndCompletion", DocQueueTests::Flags)
bool FDocQueueArrivalAndCompletionTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Flow");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q, EDocQueueOrderingPolicy::StrictFIFO, 3, 10.0f));
	F.Subsystem->RegisterStation(TEXT("StationFlow"), 2, FGameplayTagContainer());

	const FDocQueueTicket T1 = F.Join(Q, 1);
	FDocAdmissionOffer Offer;
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationFlow"), Offer);
	FDocStationReservation Reservation;
	F.Subsystem->AcceptOffer(Offer.OfferId, Reservation);
	TestEqual(TEXT("Reserved 1"), F.Station(TEXT("StationFlow")).ReservedSeats, 1);

	TestTrue(TEXT("Arrival"), F.Subsystem->ConfirmArrival(T1.TicketId).IsSuccess());
	TestEqual(TEXT("Arrival is Ready, not service"), F.State(T1.TicketId), EDocQueueTicketState::Ready);
	TestEqual(TEXT("Arrival does not start service"), F.Station(TEXT("StationFlow")).InServiceSeats, 0);

	F.Subsystem->StartService(TEXT("StationFlow"), T1.TicketId);
	TestEqual(TEXT("Reserved -> in service"), F.Station(TEXT("StationFlow")).ReservedSeats, 0);
	TestEqual(TEXT("In service 1"), F.Station(TEXT("StationFlow")).InServiceSeats, 1);
	TestEqual(TEXT("Service start is not completion"), F.State(T1.TicketId), EDocQueueTicketState::InService);

	TestTrue(TEXT("Complete"), F.Subsystem->CompleteService(TEXT("StationFlow"), T1.TicketId).IsChanged());
	TestEqual(TEXT("Duplicate completion is a no-op"), F.Subsystem->CompleteService(TEXT("StationFlow"), T1.TicketId).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Seats freed once"), F.Station(TEXT("StationFlow")).InServiceSeats, 0);

	// No-show: the reservation expires on the arrival deadline and a late arrival cannot resurrect it.
	const FDocQueueTicket T2 = F.Join(Q, 2);
	F.SetTime(20.0);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("StationFlow"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Reservation); // arrival deadline 30
	F.SetTime(30.0);
	TestEqual(TEXT("Sweep expires the no-show"), F.Subsystem->CheckExpirations(), 1);
	TestEqual(TEXT("No-show seats released"), F.Station(TEXT("StationFlow")).ReservedSeats, 0);
	TestFalse(TEXT("Late arrival refused"), F.Subsystem->ConfirmArrival(T2.TicketId).IsSuccess());
	TestEqual(TEXT("Ticket expired"), F.State(T2.TicketId), EDocQueueTicketState::Expired);
	return true;
}

// QUE-07: Doc.Queue.BatchTimeout
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueBatchTimeoutTest, FAutomationTestBase, "Doc.Queue.BatchTimeout", DocQueueTests::Flags)
bool FDocQueueBatchTimeoutTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Batch");
	UDocServiceQueueDefinition* Def = CreateTestDefinition(Q);
	Def->MinBatchSize = 3;
	Def->BatchWaitSeconds = 10.0f;
	Def->ArrivalTimeoutSeconds = 100.0f;
	F.Subsystem->RegisterQueue(Def);
	F.Subsystem->RegisterStation(TEXT("Ride"), 6, FGameplayTagContainer());

	F.SetTime(0.0);
	const FDocQueueTicket T1 = F.Join(Q, 1);
	F.Join(Q, 1);
	FDocServiceBatch Batch;
	TestFalse(TEXT("Two seats is below the minimum of three"), F.Subsystem->FormBatch(Q, TEXT("Ride"), Batch).IsSuccess());
	TestEqual(TEXT("A refused batch reserves nothing"), F.Station(TEXT("Ride")).ReservedSeats, 0);
	TestEqual(TEXT("Tickets still waiting"), F.State(T1.TicketId), EDocQueueTicketState::Waiting);

	const FDocQueueTicket T3 = F.Join(Q, 1);
	TestTrue(TEXT("Minimum reached: batch starts"), F.Subsystem->FormBatch(Q, TEXT("Ride"), Batch).IsSuccess());
	TestEqual(TEXT("Three tickets admitted together"), Batch.AdmittedTicketIds.Num(), 3);
	TestEqual(TEXT("Three seats reserved in one commit"), F.Station(TEXT("Ride")).ReservedSeats, 3);
	FDocQueueTicket Check;
	F.Subsystem->QueryTicket(T3.TicketId, Check);
	TestEqual(TEXT("Batch tickets are Reserved"), Check.State, EDocQueueTicketState::Reserved);
	TestEqual(TEXT("Batch tickets share the batch id"), Check.BatchId, Batch.BatchId);

	// A lone rider waits until the batch wait expires.
	F.SetTime(1.0);
	const FDocQueueTicket T4 = F.Join(Q, 1);
	F.SetTime(5.0);
	TestFalse(TEXT("Partial batch refused before the wait expires"), F.Subsystem->FormBatch(Q, TEXT("Ride"), Batch).IsSuccess());
	F.SetTime(11.0);
	TestTrue(TEXT("Partial batch starts once the wait expires"), F.Subsystem->FormBatch(Q, TEXT("Ride"), Batch).IsSuccess());
	TestEqual(TEXT("Only the lone rider"), Batch.AdmittedTicketIds.Num(), 1);
	TestEqual(TEXT("It is T4"), Batch.AdmittedTicketIds.Num() == 1 ? Batch.AdmittedTicketIds[0] : FGuid(), T4.TicketId);

	// A full station starts even below the minimum.
	F.Subsystem->RegisterStation(TEXT("Pair"), 2, FGameplayTagContainer());
	F.Join(Q, 1);
	F.Join(Q, 1);
	TestTrue(TEXT("Full capacity starts the batch"), F.Subsystem->FormBatch(Q, TEXT("Pair"), Batch).IsSuccess());
	TestEqual(TEXT("Two seats"), Batch.TotalSeats, 2);
	return true;
}

// QUE-08: Doc.Queue.ClosureAndOwnerLoss
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueClosureAndOwnerLossTest, FAutomationTestBase, "Doc.Queue.ClosureAndOwnerLoss", DocQueueTests::Flags)
bool FDocQueueClosureAndOwnerLossTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FFixture F;
	const FName Q = TEXT("Q_Close");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q));
	F.Subsystem->RegisterStation(TEXT("S"), 3, FGameplayTagContainer());
	FDocAdmissionOffer Offer;
	FDocStationReservation Res;

	// Station closes mid-offer.
	const FDocQueueTicket T1 = F.Join(Q, 2);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->SetStationOperationalState(TEXT("S"), EDocStationOperationalState::Closed, EDocStationClosePolicy::CancelPendingReservations);
	TestEqual(TEXT("Closed station holds no seats"), F.Station(TEXT("S")).ReservedSeats, 0);
	TestEqual(TEXT("Ticket back to Waiting"), F.State(T1.TicketId), EDocQueueTicketState::Waiting);

	// Participant departure while reserved, then while in service.
	F.Subsystem->SetStationOperationalState(TEXT("S"), EDocStationOperationalState::Open);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	TestTrue(TEXT("Leave while reserved"), F.Subsystem->LeaveQueue(T1.TicketId).IsChanged());
	TestEqual(TEXT("Reservation released"), F.Station(TEXT("S")).ReservedSeats, 0);
	TestEqual(TEXT("Leaving again is a no-op"), F.Subsystem->LeaveQueue(T1.TicketId).Outcome, EDocResultOutcome::NoChange);

	const FDocQueueTicket T2 = F.Join(Q, 2);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	F.Subsystem->StartService(TEXT("S"), T2.TicketId);
	TestTrue(TEXT("Leave while in service"), F.Subsystem->LeaveQueue(T2.TicketId).IsChanged());
	TestEqual(TEXT("In-service seats released"), F.Station(TEXT("S")).InServiceSeats, 0);

	// Leaving a completed ticket does not overwrite it.
	const FDocQueueTicket T3 = F.Join(Q, 1);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	F.Subsystem->StartService(TEXT("S"), T3.TicketId);
	F.Subsystem->CompleteService(TEXT("S"), T3.TicketId);
	F.Subsystem->LeaveQueue(T3.TicketId);
	TestEqual(TEXT("Completed stays Completed"), F.State(T3.TicketId), EDocQueueTicketState::Completed);

	// Drain: existing reservations continue, no new offers.
	const FDocQueueTicket T4 = F.Join(Q, 1);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	F.Join(Q, 1);
	F.Subsystem->SetStationOperationalState(TEXT("S"), EDocStationOperationalState::Closing, EDocStationClosePolicy::DrainCurrentService);
	TestFalse(TEXT("Draining station makes no new offers"), F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer).IsSuccess());
	TestTrue(TEXT("Draining station still serves its reservation"), F.Subsystem->StartService(TEXT("S"), T4.TicketId).IsSuccess());

	// Emergency abort with an occupant: blocked, not a free seat.
	F.Subsystem->SetStationOperationalState(TEXT("S"), EDocStationOperationalState::Closed, EDocStationClosePolicy::EmergencyAbort);
	TestEqual(TEXT("Station blocked"), F.Station(TEXT("S")).State, EDocStationOperationalState::Blocked);
	TestEqual(TEXT("Occupied seat is still counted"), F.Station(TEXT("S")).InServiceSeats, 1);
	TestFalse(TEXT("Cannot reopen until evacuation is confirmed"), F.Subsystem->SetStationOperationalState(TEXT("S"), EDocStationOperationalState::Open).IsSuccess());
	TestTrue(TEXT("Evacuation confirmed"), F.Subsystem->ConfirmEvacuated(TEXT("S")).IsSuccess());
	TestEqual(TEXT("Seat freed"), F.Station(TEXT("S")).InServiceSeats, 0);
	TestEqual(TEXT("Station closed"), F.Station(TEXT("S")).State, EDocStationOperationalState::Closed);
	TestEqual(TEXT("Occupant ticket cancelled"), F.State(T4.TicketId), EDocQueueTicketState::Cancelled);

	// Station destruction returns its reservations to the queue.
	F.Subsystem->RegisterStation(TEXT("S2"), 2, FGameplayTagContainer());
	F.Subsystem->RegisterStation(TEXT("S3"), 2, FGameplayTagContainer());
	const FName Q2 = TEXT("Q_Close2");
	F.Subsystem->RegisterQueue(CreateTestDefinition(Q2));
	const FDocQueueTicket T5 = F.Join(Q2, 1);
	F.Subsystem->CreateAdmissionOffer(Q2, TEXT("S2"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	TestTrue(TEXT("Station destroyed"), F.Subsystem->UnregisterStation(TEXT("S2")).IsSuccess());
	TestEqual(TEXT("Its ticket waits again"), F.State(T5.TicketId), EDocQueueTicketState::Waiting);
	TestTrue(TEXT("Another station can take it"), F.Subsystem->CreateAdmissionOffer(Q2, TEXT("S3"), Offer).IsSuccess());

	// Queue removal releases the station seats it held.
	TestTrue(TEXT("Queue removed"), F.Subsystem->UnregisterQueue(Q2).IsSuccess());
	TestEqual(TEXT("No orphan seats"), F.Station(TEXT("S3")).ReservedSeats, 0);

	// A closed queue takes no new joins.
	F.Subsystem->SetQueueClosed(Q, true);
	FDocQueueTicket Refused;
	TestEqual(TEXT("Closed queue refuses joins"), F.Subsystem->JoinQueue(Q, FGuid::NewGuid(), 1, 0, Refused).Outcome, EDocResultOutcome::Unavailable);
	return true;
}

// QUE-09: Doc.Queue.RestoreRevalidation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueRestoreRevalidationTest, FAutomationTestBase, "Doc.Queue.RestoreRevalidation", DocQueueTests::Flags)
bool FDocQueueRestoreRevalidationTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	const FName Q = TEXT("Q_Restore");
	UDocServiceQueueDefinition* Def = CreateTestDefinition(Q);

	FFixture F;
	F.Subsystem->RegisterQueue(Def);
	F.Subsystem->RegisterStation(TEXT("S"), 4, FGameplayTagContainer());
	const FDocQueueTicket TWait = F.Join(Q, 1);
	FDocAdmissionOffer Offer;
	FDocStationReservation Res;
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer); // TWait offered
	const FDocQueueTicket TSvc = F.Join(Q, 2);
	F.Subsystem->CreateAdmissionOffer(Q, TEXT("S"), Offer);
	F.Subsystem->AcceptOffer(Offer.OfferId, Res);
	F.Subsystem->StartService(TEXT("S"), TSvc.TicketId);
	const FDocQueueTicket TLater = F.Join(Q, 1);

	FDocQueueSnapshot Snapshot;
	TestTrue(TEXT("Capture"), F.Subsystem->CaptureQueue(Q, Snapshot).IsSuccess());
	TestEqual(TEXT("3 tickets captured"), Snapshot.Tickets.Num(), 3);
	const FDocQueueTicket DuplicateRecord = Snapshot.Tickets[0];
	Snapshot.Tickets.Add(DuplicateRecord); // duplicate record

	// Restoring over live state releases the holds it replaces.
	TestTrue(TEXT("Restore in place"), F.Subsystem->StageRestore(Q, Snapshot).IsSuccess());
	TestEqual(TEXT("No reserved seats left behind"), F.Station(TEXT("S")).ReservedSeats, 0);
	TestEqual(TEXT("No in-service seats charged"), F.Station(TEXT("S")).InServiceSeats, 0);

	// Restore into a fresh world.
	FFixture G;
	G.Subsystem->RegisterQueue(Def);
	G.Subsystem->RegisterStation(TEXT("S"), 4, FGameplayTagContainer());
	TestTrue(TEXT("Restore"), G.Subsystem->StageRestore(Q, Snapshot).IsSuccess());
	FDocQueueView View;
	G.Subsystem->QueryQueueView(Q, View);
	TestEqual(TEXT("Offered ticket waits for a new lease"), G.State(TWait.TicketId), EDocQueueTicketState::Waiting);
	TestEqual(TEXT("Duplicate record ignored: 2 waiting"), View.TotalWaitingTickets, 2);
	TestEqual(TEXT("FIFO kept: head is the earliest"), View.HeadTicketId, TWait.TicketId);
	TestEqual(TEXT("In-service ticket needs reconciliation"), G.State(TSvc.TicketId), EDocQueueTicketState::ReconciliationRequired);
	TestEqual(TEXT("Nothing charged on restore"), G.Station(TEXT("S")).InServiceSeats, 0);

	TestTrue(TEXT("Provider confirms the service is still running"), G.Subsystem->ReconcileTicket(TSvc.TicketId, EDocQueueTicketState::InService, TEXT("S")).IsSuccess());
	TestEqual(TEXT("Seats charged once, after confirmation"), G.Station(TEXT("S")).InServiceSeats, 2);
	TestTrue(TEXT("Service completes normally"), G.Subsystem->CompleteService(TEXT("S"), TSvc.TicketId).IsChanged());

	FDocQueueTicket Fresh;
	G.Subsystem->JoinQueue(Q, FGuid::NewGuid(), 1, 0, Fresh);
	TestTrue(TEXT("New joins get later ordinals"), Fresh.JoinOrdinal > TLater.JoinOrdinal);

	FDocQueueSnapshot Future = Snapshot;
	Future.SchemaVersion = 99;
	TestFalse(TEXT("Unsupported schema refused"), G.Subsystem->StageRestore(Q, Future).IsSuccess());
	return true;
}

// QUE-10: Doc.Queue.IsolatedHeadless
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocQueueIsolatedHeadlessTest, FAutomationTestBase, "Doc.Queue.IsolatedHeadless", DocQueueTests::Flags)
bool FDocQueueIsolatedHeadlessTest::RunTest(const FString& Parameters)
{
	using namespace DocQueueTests;
	FDocScopedTestWorld TW;
	UDocServiceQueueSubsystem* Subsystem = TW.GetSubsystem<UDocServiceQueueSubsystem>();
	TestNotNull(TEXT("Subsystem valid"), Subsystem);

	const FName Q = TEXT("Q_Headless");
	Subsystem->RegisterQueue(CreateTestDefinition(Q));

	AActor* StationActor = TW.Spawn<AActor>();
	UDocQueueStationComponent* StationComp = NewObject<UDocQueueStationComponent>(StationActor, TEXT("StationComp"));
	StationComp->StationId = TEXT("AutoStation");
	StationComp->Capacity = 2;
	StationComp->RegisterComponent();

	AActor* PartActor = TW.Spawn<AActor>();
	UDocQueueParticipantComponent* PartComp = NewObject<UDocQueueParticipantComponent>(PartActor, TEXT("PartComp"));
	PartComp->RegisterComponent();

	if (!StationActor->HasActorBegunPlay()) { StationActor->DispatchBeginPlay(); }
	if (!PartActor->HasActorBegunPlay()) { PartActor->DispatchBeginPlay(); }

	TestTrue(TEXT("Participant joins queue"), PartComp->JoinQueue(Q, 1, 0).IsSuccess());
	TestTrue(TEXT("HasActiveTicket"), PartComp->HasActiveTicket());

	FDocAdmissionOffer Offer;
	TestTrue(TEXT("Station creates offer"), StationComp->CreateOffer(Q, Offer).IsSuccess());

	// The participant learns about the offer from the subsystem; nothing is copied in by hand.
	TestTrue(TEXT("Participant accepts the offer it was given"), PartComp->AcceptCurrentOffer().IsSuccess());
	TestEqual(TEXT("Component sees Reserved"), PartComp->CurrentTicket.State, EDocQueueTicketState::Reserved);
	TestTrue(TEXT("Confirm arrival"), PartComp->ConfirmArrival().IsSuccess());
	TestTrue(TEXT("Start service"), StationComp->StartService(PartComp->CurrentTicket.TicketId).IsSuccess());
	TestTrue(TEXT("Complete service"), StationComp->CompleteService(PartComp->CurrentTicket.TicketId).IsSuccess());

	PartComp->RefreshTicket();
	TestEqual(TEXT("Ticket completed"), PartComp->CurrentTicket.State, EDocQueueTicketState::Completed);
	TestFalse(TEXT("No longer active"), PartComp->HasActiveTicket());

	FDocStationInfo Info;
	Subsystem->QueryStation(TEXT("AutoStation"), Info);
	TestEqual(TEXT("Station empty"), Info.ReservedSeats + Info.InServiceSeats, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
