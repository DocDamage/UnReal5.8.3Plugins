// Tests for Core primitives shared by several features (owner scope, receipts,
// control arbitration, condition combination, feature-record serialization).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTags.h"
#include "DocOwnerScope.h"
#include "DocSharedTypes.h"
#include "DocEffectKey.h"
#include "DocControlClaimArbiter.h"
#include "DocReferencePlayerControlProvider.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocCoreSharedTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FDocEffectKey MakeKey(int64 Ordinal, FName Action)
	{
		FDocEffectKey Key;
		Key.Owner = FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(1, 1, 1, 1));
		Key.CampaignEpoch = FGuid(2, 2, 2, 2);
		Key.ProducerInstanceId = FGuid(3, 3, 3, 3);
		Key.TransitionOrdinal = Ordinal;
		Key.ActionId = Action;
		return Key;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreOwnerScopeTest, "Doc.Core.OwnerScope", DocCoreSharedTests::Flags)
bool FDocCoreOwnerScopeTest::RunTest(const FString& Parameters)
{
	const FDocOwnerScope None;
	TestFalse(TEXT("Default scope invalid"), None.IsValid());

	const FDocOwnerScope P1(EDocOwnerScopeKind::PlayerProfile, FGuid(1, 0, 0, 0));
	const FDocOwnerScope P2(EDocOwnerScopeKind::PlayerProfile, FGuid(2, 0, 0, 0));
	const FDocOwnerScope Shared(EDocOwnerScopeKind::SharedWorld, FGuid(1, 0, 0, 0));
	const FDocOwnerScope Session(EDocOwnerScopeKind::Session, FGuid(9, 0, 0, 0));
	TestTrue(TEXT("Profile valid"), P1.IsValid());
	TestNotEqual(TEXT("Different subjects differ"), P1, P2);
	TestNotEqual(TEXT("Same subject, different kind differ (no implicit merge)"), P1, Shared);
	TestTrue(TEXT("Profile persistable"), P1.IsPersistable());
	TestFalse(TEXT("Session not persistable"), Session.IsPersistable());

	TSet<FDocOwnerScope> Set{ P1, P2, Shared, P1 };
	TestEqual(TEXT("Hash keeps distinct scopes"), Set.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreReceiptLedgerTest, "Doc.Core.ReceiptLedger", DocCoreSharedTests::Flags)
bool FDocCoreReceiptLedgerTest::RunTest(const FString& Parameters)
{
	using namespace DocCoreSharedTests;
	FDocReceiptLedger Ledger;
	const FDocEffectKey K1 = MakeKey(1, TEXT("GiveItem"));

	TestEqual(TEXT("Unknown key is New"), Ledger.Check(K1, 100), EDocReceiptCheck::New);

	FDocEffectReceipt R;
	R.Key = K1;
	R.PayloadHash = 100;
	R.Result = FDocSystemResult::MakeSuccess(77);
	R.CommittedRevision = 5;
	TestTrue(TEXT("First record stored"), Ledger.Record(R));
	TestFalse(TEXT("Second record for same key ignored"), Ledger.Record(R));

	FDocEffectReceipt Existing;
	TestEqual(TEXT("Same payload is Duplicate"), Ledger.Check(K1, 100, &Existing), EDocReceiptCheck::Duplicate);
	TestEqual(TEXT("Duplicate returns original result op"), Existing.Result.OperationId, static_cast<int64>(77));
	TestEqual(TEXT("Different payload is Conflict"), Ledger.Check(K1, 101), EDocReceiptCheck::Conflict);
	TestEqual(TEXT("Different ordinal is New"), Ledger.Check(MakeKey(2, TEXT("GiveItem")), 100), EDocReceiptCheck::New);
	TestEqual(TEXT("Different action is New"), Ledger.Check(MakeKey(1, TEXT("Other")), 100), EDocReceiptCheck::New);

	// Persist and restore.
	const TArray<FDocEffectReceipt> Saved = Ledger.GetAll();
	FDocReceiptLedger Restored;
	Restored.RestoreAll(Saved);
	TestEqual(TEXT("Restored ledger still reports Duplicate"), Restored.Check(K1, 100), EDocReceiptCheck::Duplicate);

	// Bounded eviction is oldest-first.
	FDocReceiptLedger Bounded(2);
	for (int64 i = 1; i <= 3; ++i)
	{
		FDocEffectReceipt X; X.Key = MakeKey(i, TEXT("A")); X.PayloadHash = i; Bounded.Record(X);
	}
	TestEqual(TEXT("Bounded size"), Bounded.Num(), 2);
	TestEqual(TEXT("Oldest evicted"), Bounded.Check(MakeKey(1, TEXT("A")), 1), EDocReceiptCheck::New);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreControlArbiterTest, "Doc.Core.ControlArbiter", DocCoreSharedTests::Flags)
bool FDocCoreControlArbiterTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UObject> Scope(NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UObject> OwnerA(NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UObject> OwnerB(NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage(), NAME_None, RF_Transient));

	FDocControlClaimArbiter Arbiter;

	FDocControlClaimRequest Inspect;
	Inspect.Owner = OwnerA.Get();
	Inspect.Capabilities.AddTag(DocCoreTags::Control_Input);
	Inspect.Capabilities.AddTag(DocCoreTags::Control_Camera);
	Inspect.Priority = 0;

	FDocControlClaimRequest Sequence;
	Sequence.Owner = OwnerB.Get();
	Sequence.Capabilities.AddTag(DocCoreTags::Control_Camera);
	Sequence.Priority = 0;

	const FDocRequestHandle HA = Arbiter.Add(Inspect, Scope.Get());
	TestTrue(TEXT("Input claim covers Input.Movement (hierarchy)"), Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Input_Movement));
	TestFalse(TEXT("Pause not claimed"), Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Pause));
	TestEqual(TEXT("A owns camera"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Camera), HA);

	const FDocRequestHandle HB = Arbiter.Add(Sequence, Scope.Get());
	TestEqual(TEXT("Equal priority: newer claim owns camera"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Camera), HB);
	TestEqual(TEXT("A still owns input"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Input), HA);

	// Releasing the OLDER claim must not disturb the newer owner (no stale restore).
	TestTrue(TEXT("Release A"), Arbiter.Remove(HA, Scope.Get()));
	TestFalse(TEXT("Release A again is harmless"), Arbiter.Remove(HA, Scope.Get()));
	TestEqual(TEXT("B still owns camera after A released"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Camera), HB);
	TestFalse(TEXT("Input no longer claimed"), Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Input_Look));

	// Higher priority wins regardless of order.
	FDocControlClaimRequest Modal = Sequence;
	Modal.Owner = OwnerA.Get();
	Modal.Priority = 10;
	const FDocRequestHandle HM = Arbiter.Add(Modal, Scope.Get());
	FDocControlClaimRequest Later = Sequence;
	Later.Priority = 5;
	const FDocRequestHandle HL = Arbiter.Add(Later, Scope.Get());
	TestEqual(TEXT("Priority beats recency"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Camera), HM);

	// Dead owners are pruned.
	OwnerA.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int32 Pruned = Arbiter.RemoveClaimsWithDeadOwners();
	TestEqual(TEXT("Dead-owner claim pruned"), Pruned, 1);
	TestEqual(TEXT("Next best owner after prune"), Arbiter.GetEffectiveClaim(DocCoreTags::Control_Camera), HL);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreConditionCombineTest, "Doc.Core.ConditionCombine", DocCoreSharedTests::Flags)
bool FDocCoreConditionCombineTest::RunTest(const FString& Parameters)
{
	const FDocConditionResult Yes = FDocConditionResult::Satisfied(3);
	const FDocConditionResult No = FDocConditionResult::Unsatisfied(DocCoreTags::Error_NotFound);
	const FDocConditionResult Unknown = FDocConditionResult::Unavailable(DocCoreTags::Error_Unavailable);

	TestEqual(TEXT("AND all yes"), FDocConditionResult::CombineAll({ Yes, Yes }).State, EDocConditionState::Satisfied);
	TestEqual(TEXT("AND with no"), FDocConditionResult::CombineAll({ Yes, No }).State, EDocConditionState::Unsatisfied);
	TestEqual(TEXT("AND: unavailable dominates"), FDocConditionResult::CombineAll({ No, Unknown }).State, EDocConditionState::Unavailable);
	TestEqual(TEXT("AND empty is satisfied"), FDocConditionResult::CombineAll({}).State, EDocConditionState::Satisfied);

	TestEqual(TEXT("OR any yes"), FDocConditionResult::CombineAny({ No, Unknown, Yes }).State, EDocConditionState::Satisfied);
	TestEqual(TEXT("OR no + unavailable -> unsatisfied"), FDocConditionResult::CombineAny({ Unknown, No }).State, EDocConditionState::Unsatisfied);
	TestEqual(TEXT("OR only unavailable"), FDocConditionResult::CombineAny({ Unknown }).State, EDocConditionState::Unavailable);
	TestEqual(TEXT("OR empty is unsatisfied"), FDocConditionResult::CombineAny({}).State, EDocConditionState::Unsatisfied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreFeatureRecordSerializationTest, "Doc.Core.FeatureRecordSerialization", DocCoreSharedTests::Flags)
bool FDocCoreFeatureRecordSerializationTest::RunTest(const FString& Parameters)
{
	FDocFeatureRecord Original;
	Original.FeatureId = TEXT("DocTest");
	Original.SchemaVersion = 3;
	Original.Owner = FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(4, 5, 6, 7));
	Original.Revision.Bump();
	Original.Payload = { 1, 2, 3, 250 };

	TArray<uint8> Bytes;
	DocCoreSerialization::Encode(Original, Bytes);
	TestTrue(TEXT("Encoded non-empty"), Bytes.Num() > 12);

	FDocFeatureRecord Decoded;
	TestTrue(TEXT("Decode succeeds"), DocCoreSerialization::Decode(Decoded, Bytes));
	TestEqual(TEXT("FeatureId"), Decoded.FeatureId, Original.FeatureId);
	TestEqual(TEXT("Schema"), Decoded.SchemaVersion, 3);
	TestEqual(TEXT("Owner"), Decoded.Owner, Original.Owner);
	TestEqual(TEXT("Revision"), Decoded.Revision, Original.Revision);
	TestTrue(TEXT("Payload"), Decoded.Payload == Original.Payload);

	// Corruption and truncation are rejected, never partially applied.
	TArray<uint8> Corrupt = Bytes;
	Corrupt.Last() ^= 0xFF;
	FDocFeatureRecord Bad;
	TestFalse(TEXT("Corrupt body rejected by CRC"), DocCoreSerialization::Decode(Bad, Corrupt));
	TestEqual(TEXT("Rejected decode leaves defaults"), Bad.SchemaVersion, 0);

	TArray<uint8> Truncated(Bytes.GetData(), Bytes.Num() - 3);
	TestFalse(TEXT("Truncated rejected"), DocCoreSerialization::Decode(Bad, Truncated));
	TestFalse(TEXT("Empty rejected"), DocCoreSerialization::Decode(Bad, TArray<uint8>()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
