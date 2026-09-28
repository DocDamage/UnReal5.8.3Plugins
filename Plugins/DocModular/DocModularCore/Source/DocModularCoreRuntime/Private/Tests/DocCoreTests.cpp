// DocModularCore automation tests.
// Requirement mapping: see Docs/REQUIREMENTS_TRACEABILITY.md (CORE-01..CORE-07).
// Expected values are independent references (hand-computed tables, or golden
// values computed outside Unreal with Python hashlib) — never recomputed with the
// function under test.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTags.h"
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "DocPersistentObjectId.h"
#include "DocGameplayContext.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "DocReferencePlayerControlProvider.h"

namespace DocCoreTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** Two throwaway objects standing in for two independent scopes (e.g. worlds). */
	struct FTwoScopes
	{
		TStrongObjectPtr<UObject> A{ NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage(), NAME_None, RF_Transient) };
		TStrongObjectPtr<UObject> B{ NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage(), NAME_None, RF_Transient) };
	};
}

// ---------------------------------------------------------------------------
// CORE-02: native tags register and resolve through the tag manager.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreTagRegistrationTest, "Doc.Core.TagRegistration", DocCoreTests::Flags)
bool FDocCoreTagRegistrationTest::RunTest(const FString& Parameters)
{
	const TCHAR* ExpectedNames[] = {
		TEXT("Doc.Error"), TEXT("Doc.Error.Unset"), TEXT("Doc.Error.Unsupported"), TEXT("Doc.Error.NotReady"),
		TEXT("Doc.Error.NotFound"), TEXT("Doc.Error.InvalidConfiguration"), TEXT("Doc.Error.PermissionDenied"),
		TEXT("Doc.Error.Cancelled"), TEXT("Doc.Error.TimedOut"), TEXT("Doc.Error.ExecutionFailed"),
		TEXT("Doc.Error.InvalidInput"), TEXT("Doc.Error.Unavailable"), TEXT("Doc.Error.Conflict"), TEXT("Doc.Error.Storage"),
		TEXT("Doc.Error.Handle.Invalid"), TEXT("Doc.Error.Handle.Stale"), TEXT("Doc.Error.Handle.WrongScope"),
		TEXT("Doc.Error.Identity.Invalid"),
		TEXT("Doc.Control"), TEXT("Doc.Control.Camera"), TEXT("Doc.Control.Input"), TEXT("Doc.Control.Input.Movement"),
		TEXT("Doc.Control.Input.Look"), TEXT("Doc.Control.Pause"), TEXT("Doc.Control.HUD")
	};

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (const TCHAR* Name : ExpectedNames)
	{
		const FGameplayTag Tag = Manager.RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
		TestTrue(FString::Printf(TEXT("Tag '%s' is registered"), Name), Tag.IsValid());
	}

	TestEqual(TEXT("Native handle matches requested tag"),
		FGameplayTag(DocCoreTags::Error_Handle_Stale),
		Manager.RequestGameplayTag(FName(TEXT("Doc.Error.Handle.Stale")), false));

	TestTrue(TEXT("Hierarchy: Doc.Error.Handle.Stale matches Doc.Error"),
		FGameplayTag(DocCoreTags::Error_Handle_Stale).MatchesTag(DocCoreTags::Error));
	return true;
}

// ---------------------------------------------------------------------------
// Results: default is never success; failure outcomes map to default tags.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreResultDefaultsTest, "Doc.Core.Result.Defaults", DocCoreTests::Flags)
bool FDocCoreResultDefaultsTest::RunTest(const FString& Parameters)
{
	const FDocSystemResult Default;
	TestFalse(TEXT("Default-constructed result is not success"), Default.IsSuccess());
	TestEqual(TEXT("Default outcome is Unset"), Default.Outcome, EDocResultOutcome::Unset);

	const FDocSystemResult Ok = FDocSystemResult::MakeSuccess(42);
	TestTrue(TEXT("MakeSuccess is success"), Ok.IsSuccess());
	TestFalse(TEXT("Success carries no error tag"), Ok.ErrorTag.IsValid());
	TestEqual(TEXT("Success keeps operation id"), Ok.OperationId, static_cast<int64>(42));
	TestTrue(TEXT("Succeeded is a change"), Ok.IsChanged());

	const FDocSystemResult Same = FDocSystemResult::MakeNoChange(TEXT("already discovered"), 7);
	TestTrue(TEXT("NoChange counts as success"), Same.IsSuccess());
	TestFalse(TEXT("NoChange is not a change"), Same.IsChanged());
	TestFalse(TEXT("NoChange carries no error tag"), Same.ErrorTag.IsValid());
	TestEqual(TEXT("NoChange keeps operation id"), Same.OperationId, static_cast<int64>(7));
	TestFalse(TEXT("Default (Unset) is not a change"), Default.IsChanged());

	struct FCase { EDocResultOutcome Outcome; const TCHAR* ExpectedTag; };
	const FCase Cases[] = {
		{ EDocResultOutcome::Unsupported, TEXT("Doc.Error.Unsupported") },
		{ EDocResultOutcome::NotReady, TEXT("Doc.Error.NotReady") },
		{ EDocResultOutcome::NotFound, TEXT("Doc.Error.NotFound") },
		{ EDocResultOutcome::InvalidConfiguration, TEXT("Doc.Error.InvalidConfiguration") },
		{ EDocResultOutcome::PermissionDenied, TEXT("Doc.Error.PermissionDenied") },
		{ EDocResultOutcome::Cancelled, TEXT("Doc.Error.Cancelled") },
		{ EDocResultOutcome::TimedOut, TEXT("Doc.Error.TimedOut") },
		{ EDocResultOutcome::Failed, TEXT("Doc.Error.ExecutionFailed") },
		{ EDocResultOutcome::InvalidInput, TEXT("Doc.Error.InvalidInput") },
		{ EDocResultOutcome::Unavailable, TEXT("Doc.Error.Unavailable") },
		{ EDocResultOutcome::Conflict, TEXT("Doc.Error.Conflict") },
	};
	for (const FCase& Case : Cases)
	{
		const FDocSystemResult R = FDocSystemResult::MakeFailure(Case.Outcome, TEXT("diag"));
		TestFalse(TEXT("Failure is not success"), R.IsSuccess());
		TestFalse(TEXT("Failure is not a change"), R.IsChanged());
		TestEqual(FString::Printf(TEXT("Default tag for %s"), Case.ExpectedTag), R.ErrorTag.GetTagName(), FName(Case.ExpectedTag));
	}

	const FDocSystemResult Custom = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("diag"), DocCoreTags::Error_Identity_Invalid);
	TestEqual(TEXT("Explicit error tag is kept"), Custom.ErrorTag, FGameplayTag(DocCoreTags::Error_Identity_Invalid));
	return true;
}

// ---------------------------------------------------------------------------
// CORE-03: handles reject stale / cross-scope use and tolerate repeated release.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreHandleLifecycleTest, "Doc.Core.Handle.Lifecycle", DocCoreTests::Flags)
bool FDocCoreHandleLifecycleTest::RunTest(const FString& Parameters)
{
	DocCoreTests::FTwoScopes Scopes;
	TDocHandleTable<int32> Table;

	TestEqual(TEXT("Default handle is Invalid"), Table.Validate(FDocRequestHandle(), Scopes.A.Get()), EDocHandleStatus::Invalid);

	const FDocRequestHandle H1 = Table.Add(Scopes.A.Get(), 100);
	const FDocRequestHandle H2 = Table.Add(Scopes.A.Get(), 200);
	const FDocRequestHandle HB = Table.Add(Scopes.B.Get(), 300);

	TestTrue(TEXT("Issued handles are set"), H1.IsSet() && H2.IsSet() && HB.IsSet());
	TestNotEqual(TEXT("Handles are unique"), H1, H2);
	TestEqual(TEXT("Active in own scope"), Table.Validate(H1, Scopes.A.Get()), EDocHandleStatus::Active);
	TestEqual(TEXT("Wrong scope rejected"), Table.Validate(H1, Scopes.B.Get()), EDocHandleStatus::WrongScope);
	TestNull(TEXT("Find with wrong scope returns null"), Table.Find(HB, Scopes.A.Get()));
	TestFalse(TEXT("Remove with wrong scope does nothing"), Table.Remove(HB, Scopes.A.Get()));
	TestEqual(TEXT("Cross-scope remove left entry intact"), Table.Num(), 3);

	int32 Removed = 0;
	TestTrue(TEXT("First release succeeds"), Table.Remove(H1, Scopes.A.Get(), &Removed));
	TestEqual(TEXT("Released payload returned"), Removed, 100);
	TestFalse(TEXT("Second release is a harmless no-op"), Table.Remove(H1, Scopes.A.Get()));
	TestFalse(TEXT("Third release is a harmless no-op"), Table.Remove(H1, Scopes.A.Get()));
	TestEqual(TEXT("Released handle is Stale"), Table.Validate(H1, Scopes.A.Get()), EDocHandleStatus::Stale);
	TestEqual(TEXT("Other entries unaffected by release"), Table.Num(), 2);
	if (const int32* P2 = Table.Find(H2, Scopes.A.Get()))
	{
		TestEqual(TEXT("Sibling payload intact"), *P2, 200);
	}
	else
	{
		AddError(TEXT("Sibling handle was lost by an unrelated release"));
	}

	// A handle from another table (another owner / world subsystem instance) is never Active here.
	TDocHandleTable<int32> OtherTable;
	const FDocRequestHandle Foreign = OtherTable.Add(Scopes.A.Get(), 1);
	TestEqual(TEXT("Handle from another table is Stale"), Table.Validate(Foreign, Scopes.A.Get()), EDocHandleStatus::Stale);
	TestFalse(TEXT("Foreign handle cannot remove anything"), Table.Remove(Foreign, Scopes.A.Get()));

	// A forged handle with the right epoch but unknown id is Stale.
	const FDocRequestHandle Forged = FDocHandleAllocator::MakeHandle(H2.GetOperationId() + 100000, Table.GetEpoch());
	TestEqual(TEXT("Forged handle is Stale"), Table.Validate(Forged, Scopes.A.Get()), EDocHandleStatus::Stale);

	// Reset: every outstanding handle becomes Stale, including after re-adding.
	const int32 OldEpoch = Table.GetEpoch();
	Table.Reset();
	TestNotEqual(TEXT("Reset moves to a new epoch"), Table.GetEpoch(), OldEpoch);
	TestEqual(TEXT("Pre-reset handle is Stale"), Table.Validate(H2, Scopes.A.Get()), EDocHandleStatus::Stale);
	const FDocRequestHandle H3 = Table.Add(Scopes.A.Get(), 400);
	TestEqual(TEXT("Post-reset handle is Active"), Table.Validate(H3, Scopes.A.Get()), EDocHandleStatus::Active);
	TestEqual(TEXT("Pre-reset handle still Stale after new adds"), Table.Validate(H2, Scopes.A.Get()), EDocHandleStatus::Stale);

	// Scope teardown removes only that scope.
	Table.Add(Scopes.B.Get(), 500);
	TestEqual(TEXT("RemoveAllForScope removes only B entries"), Table.RemoveAllForScope(Scopes.B.Get()), 1);
	TestEqual(TEXT("A entry survives B teardown"), Table.Validate(H3, Scopes.A.Get()), EDocHandleStatus::Active);
	return true;
}

// ---------------------------------------------------------------------------
// Handles are transient: persistent serialization drops them.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreHandleNotDurableTest, "Doc.Core.Handle.NotDurable", DocCoreTests::Flags)
bool FDocCoreHandleNotDurableTest::RunTest(const FString& Parameters)
{
	TDocHandleTable<int32> Table;
	FDocRequestHandle Original = Table.Add(nullptr, 1);
	TestTrue(TEXT("Original is set"), Original.IsSet());

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Writer, /*bLoadIfFindFails*/ false);
		FDocRequestHandle::StaticStruct()->SerializeItem(Ar, &Original, nullptr);
	}

	FDocRequestHandle Restored;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Reader, false);
		FDocRequestHandle::StaticStruct()->SerializeItem(Ar, &Restored, nullptr);
	}
	TestFalse(TEXT("A persistently serialized handle does not survive"), Restored.IsSet());
	TestNotEqual(TEXT("Restored handle differs from original"), Restored, Original);
	return true;
}

// ---------------------------------------------------------------------------
// CORE-04: persistent IDs distinguish repeated / nested instances (golden values).
// Golden values computed independently with Python:
//   sha1(b"DocModular.InstanceScope.v1" + pack('<4I', parent) + pack('<4I', placement))[:16]
//   read as '<4I'
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreIdentityRepeatedInstancesTest, "Doc.Core.Identity.RepeatedInstances", DocCoreTests::Flags)
bool FDocCoreIdentityRepeatedInstancesTest::RunTest(const FString& Parameters)
{
	const FGuid Root;
	const FGuid Placement1(0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
	const FGuid Placement2(0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu);

	const FGuid Golden1(0x04C8D0ABu, 0xBEB2A166u, 0x6AB221F6u, 0xBCE69580u);
	const FGuid Golden2(0x6A93A4F9u, 0x483F6DF6u, 0x54295831u, 0x8FBB7231u);
	const FGuid GoldenNested(0x83C2A68Cu, 0x796E4CD4u, 0xC965C550u, 0xE2DE5157u);

	const FGuid Scope1 = FDocPersistentObjectId::ComposeInstanceScope(Root, Placement1);
	const FGuid Scope2 = FDocPersistentObjectId::ComposeInstanceScope(Root, Placement2);
	const FGuid Nested = FDocPersistentObjectId::ComposeInstanceScope(Scope1, Placement2);

	TestEqual(TEXT("Scope1 matches golden"), Scope1, Golden1);
	TestEqual(TEXT("Scope2 matches golden"), Scope2, Golden2);
	TestEqual(TEXT("Nested scope matches golden"), Nested, GoldenNested);
	TestEqual(TEXT("Composition is deterministic"), FDocPersistentObjectId::ComposeInstanceScope(Root, Placement1), Scope1);
	TestNotEqual(TEXT("Nested(Scope1,P2) differs from Root(P2)"), Nested, Scope2);
	TestFalse(TEXT("Invalid placement yields invalid scope"), FDocPersistentObjectId::ComposeInstanceScope(Root, FGuid()).IsValid());

	// Same authored local GUID inside two placements of the same template -> distinct IDs.
	const FGuid Namespace(1u, 2u, 3u, 4u);
	const FGuid AuthoredLocal(0xDEADBEEFu, 0u, 0u, 7u);
	const FDocPersistentObjectId InFirst(Namespace, Scope1, AuthoredLocal);
	const FDocPersistentObjectId InSecond(Namespace, Scope2, AuthoredLocal);
	const FDocPersistentObjectId InNested(Namespace, Nested, AuthoredLocal);
	TestTrue(TEXT("IDs are valid"), InFirst.IsValid() && InSecond.IsValid() && InNested.IsValid());
	TestNotEqual(TEXT("Repeated placements differ"), InFirst, InSecond);
	TestNotEqual(TEXT("Nested placement differs"), InFirst, InNested);

	TSet<FDocPersistentObjectId> Set;
	Set.Add(InFirst); Set.Add(InSecond); Set.Add(InNested); Set.Add(InFirst);
	TestEqual(TEXT("Hashing keeps three distinct IDs"), Set.Num(), 3);

	TestFalse(TEXT("Missing namespace is invalid"), FDocPersistentObjectId(FGuid(), Scope1, AuthoredLocal).IsValid());
	TestFalse(TEXT("Missing local GUID is invalid"), FDocPersistentObjectId(Namespace, Scope1, FGuid()).IsValid());
	TestTrue(TEXT("Root scope is valid"), FDocPersistentObjectId(Namespace, Root, AuthoredLocal).IsValid());
	return true;
}

// ---------------------------------------------------------------------------
// CORE-04: persistent IDs survive binary, tagged, and text serialization.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreIdentitySerializationTest, "Doc.Core.Identity.Serialization", DocCoreTests::Flags)
bool FDocCoreIdentitySerializationTest::RunTest(const FString& Parameters)
{
	FDocPersistentObjectId Original(
		FGuid(0x01020304u, 0x05060708u, 0x090A0B0Cu, 0x0D0E0F10u),
		FGuid(0x04C8D0ABu, 0xBEB2A166u, 0x6AB221F6u, 0xBCE69580u),
		FGuid(0xCAFEF00Du, 0x1u, 0x2u, 0x3u));

	// Binary (operator<<).
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		Writer << Original;
		TestEqual(TEXT("Binary form is three GUIDs (48 bytes)"), Bytes.Num(), 48);

		FDocPersistentObjectId Loaded;
		FMemoryReader Reader(Bytes, true);
		Reader << Loaded;
		TestEqual(TEXT("Binary round trip"), Loaded, Original);
	}

	// Tagged property serialization (what a USaveGame / asset would use).
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WAr(Writer, false);
		FDocPersistentObjectId::StaticStruct()->SerializeItem(WAr, &Original, nullptr);

		FDocPersistentObjectId Loaded;
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive RAr(Reader, false);
		FDocPersistentObjectId::StaticStruct()->SerializeItem(RAr, &Loaded, nullptr);
		TestEqual(TEXT("Tagged round trip"), Loaded, Original);
	}

	// Text.
	{
		const FString Text = Original.ToString();
		TestEqual(TEXT("Text form"), Text,
			FString(TEXT("0102030405060708090A0B0C0D0E0F10:04C8D0ABBEB2A1666AB221F6BCE69580:CAFEF00D000000010000000200000003")));

		FDocPersistentObjectId Parsed;
		TestTrue(TEXT("Parse succeeds"), FDocPersistentObjectId::Parse(Text, Parsed));
		TestEqual(TEXT("Text round trip"), Parsed, Original);

		FDocPersistentObjectId Untouched = Original;
		TestFalse(TEXT("Empty rejected"), FDocPersistentObjectId::Parse(TEXT(""), Untouched));
		TestFalse(TEXT("Truncated rejected"), FDocPersistentObjectId::Parse(Text.LeftChop(1), Untouched));
		TestFalse(TEXT("Wrong separator rejected"), FDocPersistentObjectId::Parse(Text.Replace(TEXT(":"), TEXT("-")), Untouched));
		FString BadHex = Text; BadHex[5] = TEXT('Z');
		TestFalse(TEXT("Non-hex rejected"), FDocPersistentObjectId::Parse(BadHex, Untouched));
		TestEqual(TEXT("Failed parse leaves output unchanged"), Untouched, Original);
	}
	return true;
}

// ---------------------------------------------------------------------------
// CORE-05 (Core portion): contexts and handle scopes do not cross real worlds.
// Feature registries (world subsystems) repeat this with two PIE worlds.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocCoreWorldIsolationTest, "Doc.Core.WorldIsolation", DocCoreTests::Flags)
bool FDocCoreWorldIsolationTest::RunTest(const FString& Parameters)
{
	UWorld* WorldA = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	UWorld* WorldB = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World A created"), WorldA) || !TestNotNull(TEXT("World B created"), WorldB))
	{
		if (WorldA) { WorldA->DestroyWorld(false); }
		if (WorldB) { WorldB->DestroyWorld(false); }
		return false;
	}

	const FDocGameplayContext ContextA = FDocGameplayContext::Make(WorldA);
	TestTrue(TEXT("Context valid for its own world"), ContextA.IsValidForWorld(WorldA));
	TestFalse(TEXT("Context rejected by another world"), ContextA.IsValidForWorld(WorldB));
	TestFalse(TEXT("Context rejected for null world"), ContextA.IsValidForWorld(nullptr));
	TestEqual(TEXT("Standalone world has authority"), ContextA.ResolveAuthority(), EDocNetAuthority::Standalone);

	const FDocGameplayContext Empty;
	TestEqual(TEXT("No world -> Unknown authority"), Empty.ResolveAuthority(), EDocNetAuthority::Unknown);
	TestFalse(TEXT("No world -> no authority"), Empty.HasAuthority());

	TDocHandleTable<FString> Registry;
	const FDocRequestHandle InA = Registry.Add(WorldA, TEXT("A"));
	TestEqual(TEXT("World A handle active in A"), Registry.Validate(InA, WorldA), EDocHandleStatus::Active);
	TestEqual(TEXT("World A handle rejected in B"), Registry.Validate(InA, WorldB), EDocHandleStatus::WrongScope);

	WorldB->DestroyWorld(false);
	TestEqual(TEXT("Teardown of B leaves A's handle"), Registry.Validate(InA, WorldA), EDocHandleStatus::Active);
	WorldA->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
