// DocEvents automation tests (EVT-01..EVT-08). Expected orders/values are written
// out by hand, not computed with the dispatcher.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocGameplayEventSubsystem.h"
#include "DocEventsTestTypes.h"
#include "DocWaitGameplayEventAction.h"
#include "NativeGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace DocEventsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_A, "Test.DocEvents.A");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_A_Child, "Test.DocEvents.A.Child");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_B, "Test.DocEvents.B");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_State, "Test.DocEvents.State.PowerOn");

	FInstancedStruct MakePayload(int32 Value)
	{
		FDocEventsTestPayload P;
		P.Value = Value;
		return FInstancedStruct::Make(P);
	}

	FDocGameplayEvent Make(FGameplayTag Tag, int32 Int = 0)
	{
		FDocGameplayEvent E;
		E.EventTag = Tag;
		E.IntValue = Int;
		return E;
	}

	FDocEventListenOptions Opts(EDocEventTagMatch Match, int32 Priority = 0)
	{
		FDocEventListenOptions O;
		O.Match = Match;
		O.Priority = Priority;
		return O;
	}
}

using namespace DocEventsTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsBroadcastTest, "Doc.Events.Broadcast", Flags)
bool FDocEventsBroadcastTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus exists in game world"), Bus)) { return false; }

	TArray<int32> Received;
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent& E) { Received.Add(E.IntValue); }));

	TestTrue(TEXT("Broadcast succeeds"), Bus->BroadcastEvent(Make(TAG_A, 7)).IsSuccess());
	TestTrue(TEXT("Child not delivered to exact listener"), Bus->BroadcastEvent(Make(TAG_A_Child, 8)).IsSuccess());
	TestEqual(TEXT("Exact listener got one event"), Received.Num(), 1);
	if (Received.Num() == 1) { TestEqual(TEXT("Value"), Received[0], 7); }

	const FDocSystemResult NoTag = Bus->BroadcastEvent(FDocGameplayEvent());
	TestEqual(TEXT("Missing tag rejected"), NoTag.Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsHierarchicalTest, "Doc.Events.HierarchicalSubscription", Flags)
bool FDocEventsHierarchicalTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	int32 ParentCount = 0;
	int32 BothCount = 0;
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::IncludeChildren),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++ParentCount; }));
	// One subscription matching through two paths must still be delivered once.
	Bus->SubscribeNative(TAG_A_Child, Opts(EDocEventTagMatch::IncludeChildren),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++BothCount; }));

	Bus->BroadcastEvent(Make(TAG_A_Child));
	Bus->BroadcastEvent(Make(TAG_A));
	Bus->BroadcastEvent(Make(TAG_B));
	TestEqual(TEXT("Parent listener receives parent and child"), ParentCount, 2);
	TestEqual(TEXT("Child listener receives only child, once"), BothCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsUnsubscribeTest, "Doc.Events.Unsubscribe", Flags)
bool FDocEventsUnsubscribeTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	TArray<FString> Log;
	FDocRequestHandle HSelf, HOther, HLate;

	// Priority 10: unsubscribes itself.
	HSelf = Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 10),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&)
		{
			Log.Add(TEXT("self"));
			Bus->Unsubscribe(HSelf);
		}));
	// Priority 5: unsubscribes the lower-priority "other" and subscribes a new "late" listener.
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 5),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&)
		{
			Log.Add(TEXT("mid"));
			Bus->Unsubscribe(HOther);
			if (!HLate.IsSet())
			{
				HLate = Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 100),
					FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { Log.Add(TEXT("late")); }));
			}
		}));
	HOther = Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 1),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { Log.Add(TEXT("other")); }));

	Bus->BroadcastEvent(Make(TAG_A));
	TestEqual(TEXT("First dispatch: self, mid (other removed, late not retroactive)"), FString::Join(Log, TEXT(",")), FString(TEXT("self,mid")));

	Log.Reset();
	Bus->BroadcastEvent(Make(TAG_A));
	TestEqual(TEXT("Second dispatch: late (prio 100), mid"), FString::Join(Log, TEXT(",")), FString(TEXT("late,mid")));

	TestEqual(TEXT("Repeated unsubscribe is NoChange"), Bus->Unsubscribe(HSelf).Outcome, EDocResultOutcome::NoChange);

	// Owner destruction removes owner-bound listeners.
	TStrongObjectPtr<UObject> Owner(NewObject<UDocWaitGameplayEventAction>(GetTransientPackage(), NAME_None, RF_Transient));
	FDocEventListenOptions OwnedOpts = Opts(EDocEventTagMatch::Exact);
	OwnedOpts.Owner = Owner.Get();
	int32 OwnedCount = 0;
	const FDocRequestHandle HOwned = Bus->SubscribeNative(TAG_B, OwnedOpts,
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++OwnedCount; }));
	Bus->BroadcastEvent(Make(TAG_B));
	Owner.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	Bus->BroadcastEvent(Make(TAG_B));
	TestEqual(TEXT("Dead owner's listener not called"), OwnedCount, 1);
	TestFalse(TEXT("Dead owner's subscription removed"), Bus->IsSubscribed(HOwned));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsReentrancyTest, "Doc.Events.Reentrancy", Flags)
bool FDocEventsReentrancyTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	TArray<FString> Log;
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 10),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&)
		{
			Log.Add(TEXT("A1"));
			Bus->BroadcastEvent(Make(TAG_B)); // nested: queued until A finishes
		}));
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact, 0),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { Log.Add(TEXT("A2")); }));
	Bus->SubscribeNative(TAG_B, Opts(EDocEventTagMatch::Exact),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { Log.Add(TEXT("B")); }));

	Bus->BroadcastEvent(Make(TAG_A));
	TestEqual(TEXT("Nested event delivered after current event completes"), FString::Join(Log, TEXT(",")), FString(TEXT("A1,A2,B")));

	// Event storm: a listener that re-broadcasts itself forever is bounded by the drain limit.
	FDocEventBusLimits Limits;
	Limits.MaxEventsPerDrain = 5;
	Limits.MaxQueuedEvents = 100;
	Bus->SetLimitsForTesting(Limits);
	int32 StormCount = 0;
	Bus->SubscribeNative(TAG_A_Child, Opts(EDocEventTagMatch::Exact),
		FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&)
		{
			++StormCount;
			Bus->BroadcastEvent(Make(TAG_A_Child));
		}));
	Bus->BroadcastEvent(Make(TAG_A_Child));
	TestEqual(TEXT("Storm stops at drain limit within one call"), StormCount, 5);
	TestTrue(TEXT("Remaining work deferred, not lost"), Bus->GetStats().QueuedEvents >= 1);
	TestTrue(TEXT("Deferred counted"), Bus->GetStats().DeferredEvents >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsScopeIsolationTest, "Doc.Events.ScopeIsolation", Flags)
bool FDocEventsScopeIsolationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld W1;
	FDocScopedTestWorld W2;
	UDocGameplayEventSubsystem* Bus1 = W1.GetSubsystem<UDocGameplayEventSubsystem>();
	UDocGameplayEventSubsystem* Bus2 = W2.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus1"), Bus1) || !TestNotNull(TEXT("Bus2"), Bus2)) { return false; }
	TestNotEqual(TEXT("Each world has its own bus"), Bus1, Bus2);

	int32 In1 = 0, In2 = 0, TeamRed = 0;
	Bus1->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact), FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++In1; }));
	Bus2->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact), FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++In2; }));

	FDocEventListenOptions RedOnly = Opts(EDocEventTagMatch::Exact);
	RedOnly.bFilterByScope = true;
	RedOnly.ScopeFilter = FDocEventScope::ForKey(EDocEventScopeKind::Team, TEXT("Red"));
	Bus1->SubscribeNative(TAG_B, RedOnly, FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++TeamRed; }));

	Bus1->BroadcastEvent(Make(TAG_A));
	TestEqual(TEXT("World 1 listener"), In1, 1);
	TestEqual(TEXT("World 2 listener untouched"), In2, 0);

	FDocGameplayEvent Blue = Make(TAG_B);
	Blue.Scope = FDocEventScope::ForKey(EDocEventScopeKind::Team, TEXT("Blue"));
	FDocGameplayEvent Red = Make(TAG_B);
	Red.Scope = FDocEventScope::ForKey(EDocEventScopeKind::Team, TEXT("Red"));
	FDocGameplayEvent CustomRed = Make(TAG_B);
	CustomRed.Scope = FDocEventScope::ForKey(EDocEventScopeKind::Custom, TEXT("Red"));
	Bus1->BroadcastEvent(Blue);
	Bus1->BroadcastEvent(Red);
	Bus1->BroadcastEvent(CustomRed);
	TestEqual(TEXT("Only Team:Red delivered (kind and key must both match)"), TeamRed, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsRetainedTest, "Doc.Events.PersistentPayload", Flags)
bool FDocEventsRetainedTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	FDocEventBusLimits Limits;
	Limits.MaxRetainedValues = 2;
	Limits.HistoryCapacity = 2;
	Bus->SetLimitsForTesting(Limits);

	FDocGameplayEvent State = Make(TAG_State, 1);
	State.Payload = MakePayload(42);
	TestTrue(TEXT("Retain"), Bus->UpdateRetainedValue(State).IsSuccess());
	State.IntValue = 2;
	Bus->UpdateRetainedValue(State);

	FDocRetainedEventValue Value;
	TestTrue(TEXT("Latest found"), Bus->GetLatestPayload(TAG_State, FDocEventScope::WorldScope(), Value));
	TestEqual(TEXT("Revision increments per update"), Value.Revision, static_cast<int64>(2));
	TestEqual(TEXT("Latest value"), Value.Event.IntValue, 2);
	TestEqual(TEXT("World-state query sees retained state"),
		Bus->QueryDocWorldState_Implementation(TAG_State, EDocTagMatchMode::Exact), EDocTriState::Yes);

	// Replay on subscribe.
	int32 Replayed = 0;
	FDocEventListenOptions ReplayOpts = Opts(EDocEventTagMatch::Exact);
	ReplayOpts.bReplayRetained = true;
	Bus->SubscribeNative(TAG_State, ReplayOpts, FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent& E) { Replayed = E.IntValue; }));
	TestEqual(TEXT("Replay delivers latest retained value"), Replayed, 2);

	// Strong object references are refused.
	FDocGameplayEvent Strong = Make(TAG_A);
	Strong.Payload = FInstancedStruct::Make(FDocEventsTestStrongPayload());
	TestEqual(TEXT("Strong-ref payload not retainable"), Bus->UpdateRetainedValue(Strong).Outcome, EDocResultOutcome::Unsupported);

	// Eviction when full.
	FDocGameplayEvent Other1 = Make(TAG_A);
	Other1.Scope = FDocEventScope::ForKey(EDocEventScopeKind::Custom, TEXT("x"));
	FDocGameplayEvent Other2 = Make(TAG_B);
	Bus->UpdateRetainedValue(Other1);
	Bus->UpdateRetainedValue(Other2);
	TestEqual(TEXT("Retained bounded"), Bus->GetStats().RetainedValues, 2);
	TestTrue(TEXT("Eviction counted"), Bus->GetStats().EvictedRetained >= 1);

	// History capacity 2; four events were broadcast (State x2, A, B), so older entries were evicted.
	TestEqual(TEXT("After eviction, an unseen tag is Unknown, not No"),
		Bus->HasEventOccurred(TAG_A_Child, EDocEventTagMatch::Exact, false, FDocEventScope()), EDocTriState::Unknown);
	TestEqual(TEXT("Evicted-but-real event is Unknown, not No"),
		Bus->HasEventOccurred(TAG_State, EDocEventTagMatch::Exact, false, FDocEventScope()), EDocTriState::Unknown);
	TestEqual(TEXT("Recent event is Yes"), Bus->HasEventOccurred(TAG_B, EDocEventTagMatch::Exact, false, FDocEventScope()), EDocTriState::Yes);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsPayloadSchemaTest, "Doc.Events.PayloadSchema", Flags)
bool FDocEventsPayloadSchemaTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	Bus->RegisterPayloadSchema(TAG_A, FDocEventsTestPayload::StaticStruct());

	FDocGameplayEvent Good = Make(TAG_A);
	Good.Payload = MakePayload(1);
	TestTrue(TEXT("Matching payload accepted"), Bus->BroadcastEvent(Good).IsSuccess());

	FDocGameplayEvent Bad = Make(TAG_A);
	Bad.Payload = FInstancedStruct::Make(FDocEventsTestStrongPayload());
	const FDocSystemResult R = Bus->BroadcastEvent(Bad);
	TestEqual(TEXT("Mismatched payload rejected"), R.Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Diagnostic names the expected type"), R.Diagnostic.Contains(TEXT("DocEventsTestPayload")));

	FDocGameplayEvent Missing = Make(TAG_A);
	TestEqual(TEXT("Missing payload rejected"), Bus->BroadcastEvent(Missing).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocEventsScheduledTest, "Doc.Events.Scheduled", Flags)
bool FDocEventsScheduledTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocGameplayEventSubsystem* Bus = TW.GetSubsystem<UDocGameplayEventSubsystem>();
	if (!TestNotNull(TEXT("Bus"), Bus)) { return false; }

	int32 Count = 0;
	Bus->SubscribeNative(TAG_A, Opts(EDocEventTagMatch::Exact), FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++Count; }));

	Bus->BroadcastNextFrame(Make(TAG_A));
	const FDocRequestHandle Cancelled = Bus->BroadcastNextFrame(Make(TAG_A));
	const FDocRequestHandle Delayed = Bus->BroadcastAfterDelay(Make(TAG_A), 1000.f, EDocClockDomain::WorldGameplay);
	TestEqual(TEXT("Nothing delivered synchronously"), Count, 0);

	TestTrue(TEXT("Cancel succeeds"), Bus->CancelScheduledEvent(Cancelled).IsSuccess());
	TestEqual(TEXT("Second cancel is NoChange"), Bus->CancelScheduledEvent(Cancelled).Outcome, EDocResultOutcome::NoChange);

	Bus->ProcessPendingWork();
	TestEqual(TEXT("Next-frame event delivered once; cancelled one not"), Count, 1);
	TestEqual(TEXT("Delayed event still pending (world time has not advanced)"), Bus->GetStats().ScheduledEvents, 1);
	TestTrue(TEXT("Delayed can be cancelled"), Bus->CancelScheduledEvent(Delayed).IsSuccess());

	// One-shot listener.
	int32 Once = 0;
	FDocEventListenOptions OneShot = Opts(EDocEventTagMatch::Exact);
	OneShot.bOneShot = true;
	const FDocRequestHandle H = Bus->SubscribeNative(TAG_B, OneShot, FDocGameplayEventNativeDelegate::CreateLambda([&](const FDocGameplayEvent&) { ++Once; }));
	Bus->BroadcastEvent(Make(TAG_B));
	Bus->BroadcastEvent(Make(TAG_B));
	TestEqual(TEXT("One-shot delivered once"), Once, 1);
	TestFalse(TEXT("One-shot unregistered"), Bus->IsSubscribed(H));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
