// DocKnowledgeCodex automation tests (KNO-01..11 base logic). No UI, Inspection,
// Dialogue or Save feature. Async search completion is driven by a test dispatcher.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocKnowledgeSubsystem.h"
#include "DocSharedTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocKnowledgeTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Dragon, "Test.Knowledge.Dragon");

	const FGuid Campaign(9, 9, 9, 9);
	FDocOwnerScope Player(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0xC0DE, 1, 2, static_cast<uint32>(N)), Campaign); }
	FDocOwnerScope Shared() { return FDocOwnerScope(EDocOwnerScopeKind::SharedWorld, FGuid(0xC0DE, 9, 9, 9), Campaign); }

	UDocKnowledgeEntry* MakeEntry(FName Id, const FGameplayTag& Category, const TCHAR* Title, const TCHAR* Summary = TEXT(""), const TCHAR* Body = TEXT(""))
	{
		UDocKnowledgeEntry* E = NewObject<UDocKnowledgeEntry>(GetTransientPackage());
		E->EntryId = Id;
		E->Category = Category;
		E->Title = FText::FromString(Title);
		E->Summary = FText::FromString(Summary);
		E->Body = FText::FromString(Body);
		return E;
	}

	FDocKnowledgeRelation Relation(FName Target, EDocKnowledgeRelationType Type = EDocKnowledgeRelationType::Character)
	{
		FDocKnowledgeRelation R;
		R.Type = Type;
		R.TargetEntryId = Target;
		return R;
	}

	FDocKnowledgeGrant Grant(const TCHAR* Cause = TEXT("Test"))
	{
		FDocKnowledgeGrant G;
		G.SourceId = TEXT("Test");
		G.Cause = Cause;
		return G;
	}

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocKnowledgeSubsystem> Keep;
		UDocKnowledgeSubsystem* S = nullptr;
		TArray<FDocKnowledgeChange> Discovered;
		TArray<FDocKnowledgeChange> Updated;
		TArray<FDocKnowledgeChange> Read;
		int32 Refreshes = 0;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			S = NewObject<UDocKnowledgeSubsystem>(GameInstance.Get());
			Keep.Reset(S);
			int64 Clock = 1000;
			S->SetUtcClockForTesting([Clock]() mutable { return Clock += 10; });
			S->OnEntryDiscoveredNative.AddLambda([this](const FDocKnowledgeChange& C) { Discovered.Add(C); });
			S->OnEntryUpdatedNative.AddLambda([this](const FDocKnowledgeChange& C) { Updated.Add(C); });
			S->OnEntryReadNative.AddLambda([this](const FDocKnowledgeChange& C) { Read.Add(C); });
			S->OnStateRefreshedNative.AddLambda([this]() { ++Refreshes; });
		}

		~FFixture()
		{
			S->OnEntryDiscoveredNative.Clear();
			S->OnEntryUpdatedNative.Clear();
			S->OnEntryReadNative.Clear();
			S->OnStateRefreshedNative.Clear();
		}

		FDocKnowledgeVisibleEntry View(const FDocOwnerScope& Owner, FName Id) const
		{
			FDocKnowledgeVisibleEntry V;
			S->GetVisibleEntry(Owner, Id, V);
			return V;
		}

		FDocKnowledgeSearchPage Search(const FDocOwnerScope& Owner, const TCHAR* Text, int32 PageSize = 25, int32 Offset = 0) const
		{
			FDocKnowledgeSearchQuery Q;
			Q.Text = Text;
			Q.PageSize = PageSize;
			Q.Offset = Offset;
			return S->SearchEntriesNow(Owner, Q);
		}
	};
}

using namespace DocKnowledgeTests;

// KNO-01
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeDiscoverTest, "Doc.Knowledge.Discover", DocKnowledgeTests::Flags)
bool FDocKnowledgeDiscoverTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TestTrue(TEXT("Register"), F.S->RegisterEntry(MakeEntry(TEXT("Wyrm"), DocKnowledgeTags::Knowledge_Creature, TEXT("Wyrm"))).IsSuccess());
	TestFalse(TEXT("Unknown before discovery"), F.S->IsDiscovered(Player(1), TEXT("Wyrm")));
	TestTrue(TEXT("Discover"), F.S->DiscoverEntry(Player(1), TEXT("Wyrm"), Grant()).IsChanged());
	const FDocSystemResult Again = F.S->DiscoverEntry(Player(1), TEXT("Wyrm"), Grant());
	TestTrue(TEXT("Repeat is NoChange"), Again.Outcome == EDocResultOutcome::NoChange);
	TestEqual(TEXT("One discovery notification"), F.Discovered.Num(), 1);
	const FDocKnowledgeRuntimeState State = F.S->GetEntryState(Player(1), TEXT("Wyrm"));
	TestEqual(TEXT("No update counted"), State.UpdateCount, 0);
	TestTrue(TEXT("Discovery time stored"), State.DiscoveryTimeUtcTicks > 0);
	TestTrue(TEXT("Label"), State.GetDisplayLabel() == EDocKnowledgeDisplayLabel::Discovered);

	FDocKnowledgeGrant Keyed = Grant(TEXT("Quest reward"));
	Keyed.EffectKey.Owner = Player(2);
	Keyed.EffectKey.ProducerInstanceId = FGuid::NewGuid();
	Keyed.EffectKey.TransitionOrdinal = 3;
	Keyed.EffectKey.ActionId = TEXT("LearnWyrm");
	TestTrue(TEXT("Keyed grant"), F.S->DiscoverEntry(Player(2), TEXT("Wyrm"), Keyed).IsChanged());
	TestTrue(TEXT("Duplicate delivery returns the original result"), F.S->DiscoverEntry(Player(2), TEXT("Wyrm"), Keyed).IsChanged());
	TestEqual(TEXT("Still one notification for player 2"), F.Discovered.Num(), 2);
	TestTrue(TEXT("Same key, different payload"), F.S->RevealEntry(Player(2), TEXT("Wyrm"), 3, Keyed).Outcome == EDocResultOutcome::Conflict);
	TestTrue(TEXT("Unknown entry"), F.S->DiscoverEntry(Player(1), TEXT("Nope"), Grant()).Outcome == EDocResultOutcome::NotFound);
	return true;
}

// KNO-02
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeUpdateTest, "Doc.Knowledge.Update", DocKnowledgeTests::Flags)
bool FDocKnowledgeUpdateTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* Journal = MakeEntry(TEXT("Journal"), DocKnowledgeTags::Knowledge_Document, TEXT("Journal"));
	FDocKnowledgeSection Page2;
	Page2.SectionId = TEXT("Page2");
	Page2.Body = FText::FromString(TEXT("The second page"));
	Journal->Sections = { Page2 };
	UDocKnowledgeEntry* Fixed = MakeEntry(TEXT("Fixed"), DocKnowledgeTags::Knowledge_Tutorial, TEXT("Fixed"));
	Fixed->bCanUpdate = false;
	Fixed->Sections = { Page2 };
	F.S->RegisterEntry(Journal);
	F.S->RegisterEntry(Fixed);
	const FDocOwnerScope P = Player(1);

	TestTrue(TEXT("Update needs discovery"), F.S->UpdateEntry(P, TEXT("Journal"), TEXT("Page2"), Grant()).Outcome == EDocResultOutcome::NotReady);
	F.S->DiscoverEntry(P, TEXT("Journal"), Grant());
	const int64 Before = F.S->GetEntryState(P, TEXT("Journal")).VisibleRevision;
	TestTrue(TEXT("Section revealed"), F.S->UpdateEntry(P, TEXT("Journal"), TEXT("Page2"), Grant()).IsChanged());
	const FDocKnowledgeRuntimeState After = F.S->GetEntryState(P, TEXT("Journal"));
	TestEqual(TEXT("Visible revision advanced"), After.VisibleRevision, Before + 1);
	TestEqual(TEXT("Update counted"), After.UpdateCount, 1);
	TestTrue(TEXT("Repeat is NoChange"), F.S->UpdateEntry(P, TEXT("Journal"), TEXT("Page2"), Grant()).Outcome == EDocResultOutcome::NoChange);
	TestEqual(TEXT("Not counted twice"), F.S->GetEntryState(P, TEXT("Journal")).UpdateCount, 1);
	TestEqual(TEXT("One update notification"), F.Updated.Num(), 1);
	TestEqual(TEXT("Section visible"), F.View(P, TEXT("Journal")).Sections.Num(), 1);

	F.S->DiscoverEntry(P, TEXT("Fixed"), Grant());
	TestTrue(TEXT("CanUpdate enforced"), F.S->UpdateEntry(P, TEXT("Fixed"), TEXT("Page2"), Grant()).ErrorTag == DocKnowledgeTags::Error_Knowledge_CannotUpdate);
	return true;
}

// KNO-03
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeMultiStageTest, "Doc.Knowledge.MultiStage", DocKnowledgeTests::Flags)
bool FDocKnowledgeMultiStageTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* Wyrm = MakeEntry(TEXT("Wyrm"), DocKnowledgeTags::Knowledge_Creature, TEXT("Wyrm"), TEXT("A great serpent"), TEXT("Lairs under the mountain"));
	Wyrm->StageLabels = { FText::FromString(TEXT("Unknown")), FText::FromString(TEXT("Name")), FText::FromString(TEXT("Summary")), FText::FromString(TEXT("Full")) };
	F.S->RegisterEntry(Wyrm);
	const FDocOwnerScope P = Player(1);

	F.S->DiscoverEntry(P, TEXT("Wyrm"), Grant());
	FDocKnowledgeVisibleEntry V = F.View(P, TEXT("Wyrm"));
	TestTrue(TEXT("Stage 1: name only"), !V.Title.IsEmpty() && V.Summary.IsEmpty() && V.Body.IsEmpty());
	TestTrue(TEXT("Stage label"), V.StageLabel.ToString() == TEXT("Name"));
	F.S->RevealEntry(P, TEXT("Wyrm"), 2, Grant());
	V = F.View(P, TEXT("Wyrm"));
	TestTrue(TEXT("Stage 2: summary"), !V.Summary.IsEmpty() && V.Body.IsEmpty());
	TestTrue(TEXT("Lower reveal is NoChange (monotonic)"), F.S->RevealEntry(P, TEXT("Wyrm"), 1, Grant()).Outcome == EDocResultOutcome::NoChange);
	F.S->RevealEntry(P, TEXT("Wyrm"), 3, Grant());
	TestFalse(TEXT("Stage 3: body"), F.View(P, TEXT("Wyrm")).Body.IsEmpty());
	TestEqual(TEXT("Reveals counted as updates"), F.S->GetEntryState(P, TEXT("Wyrm")).UpdateCount, 2);

	TestTrue(TEXT("Conceal"), F.S->ConcealEntry(P, TEXT("Wyrm"), true).IsChanged());
	FDocKnowledgeVisibleEntry Hidden;
	TestFalse(TEXT("Hidden from player queries"), F.S->GetVisibleEntry(P, TEXT("Wyrm"), Hidden));
	TestTrue(TEXT("Discovery history kept"), F.S->IsDiscovered(P, TEXT("Wyrm")) && F.S->GetEntryState(P, TEXT("Wyrm")).Stage == 3);
	F.S->ConcealEntry(P, TEXT("Wyrm"), false);

	F.S->MarkRead(P, TEXT("Wyrm"), F.S->GetEntryState(P, TEXT("Wyrm")).VisibleRevision);
	TestTrue(TEXT("Revoke is an explicit operation"), F.S->RevokeReveal(P, TEXT("Wyrm"), 1, true).IsChanged());
	const FDocKnowledgeRuntimeState Revoked = F.S->GetEntryState(P, TEXT("Wyrm"));
	TestEqual(TEXT("Stage lowered"), Revoked.Stage, 1);
	TestTrue(TEXT("Removed content is not unread"), Revoked.GetDisplayLabel() == EDocKnowledgeDisplayLabel::Read);
	TestTrue(TEXT("Admin Unknown is illegal"), F.S->SetEntryState(P, TEXT("Wyrm"), EDocKnowledgeDisplayLabel::Unknown).ErrorTag == DocKnowledgeTags::Error_Knowledge_IllegalTransition);
	TestTrue(TEXT("Admin Completed"), F.S->SetEntryState(P, TEXT("Wyrm"), EDocKnowledgeDisplayLabel::Completed).IsChanged());
	return true;
}

// KNO-04
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeRelationshipsTest, "Doc.Knowledge.Relationships", DocKnowledgeTests::Flags)
bool FDocKnowledgeRelationshipsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* A = MakeEntry(TEXT("A"), DocKnowledgeTags::Knowledge_Character, TEXT("Alda"));
	UDocKnowledgeEntry* B = MakeEntry(TEXT("B"), DocKnowledgeTags::Knowledge_Character, TEXT("Bren"));
	UDocKnowledgeEntry* C = MakeEntry(TEXT("C"), DocKnowledgeTags::Knowledge_Location, TEXT("Cairn"));
	A->Relations = { Relation(TEXT("B")), Relation(TEXT("Ghost")) }; // Ghost is dangling
	B->Relations = { Relation(TEXT("A")), Relation(TEXT("C"), EDocKnowledgeRelationType::Location) }; // A<->B cycle
	for (UDocKnowledgeEntry* E : { A, B, C }) { F.S->RegisterEntry(E); }
	const FDocOwnerScope P = Player(1);

	TArray<FString> Errors, Warnings;
	F.S->ValidateCatalog(Errors, Warnings);
	TestTrue(TEXT("Dangling reference reported"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("Ghost")); }));

	F.S->DiscoverEntry(P, TEXT("A"), Grant());
	TestEqual(TEXT("Undiscovered targets are not linked"), F.View(P, TEXT("A")).Relations.Num(), 0);
	F.S->DiscoverEntry(P, TEXT("B"), Grant());
	TestEqual(TEXT("Visible link, dangling hidden"), F.View(P, TEXT("A")).Relations.Num(), 1);
	F.S->DiscoverEntry(P, TEXT("C"), Grant());
	TestEqual(TEXT("Directed: C links nowhere"), F.View(P, TEXT("C")).Relations.Num(), 0);

	const TArray<FName> Depth1 = F.S->GetRelatedVisibleEntries(P, TEXT("A"), 1, 32);
	TestTrue(TEXT("Depth 1"), Depth1.Num() == 1 && Depth1[0] == FName(TEXT("B")));
	const TArray<FName> Depth3 = F.S->GetRelatedVisibleEntries(P, TEXT("A"), 3, 32);
	TestEqual(TEXT("Cycle bounded by visited set"), Depth3.Num(), 2);
	TestEqual(TEXT("Result limit"), F.S->GetRelatedVisibleEntries(P, TEXT("A"), 3, 1).Num(), 1);
	return true;
}

// KNO-05
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeMarkReadTest, "Doc.Knowledge.MarkRead", DocKnowledgeTests::Flags)
bool FDocKnowledgeMarkReadTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterEntry(MakeEntry(TEXT("Clue"), DocKnowledgeTags::Knowledge_Clue, TEXT("Muddy boot"), TEXT("Size nine"), TEXT("Left by the gate")));
	const FDocOwnerScope P = Player(1);
	F.S->DiscoverEntry(P, TEXT("Clue"), Grant());
	const int64 Displayed = F.View(P, TEXT("Clue")).VisibleRevision;

	F.S->RevealEntry(P, TEXT("Clue"), 2, Grant()); // arrives while the widget is still open
	TestTrue(TEXT("Mark the displayed revision"), F.S->MarkRead(P, TEXT("Clue"), Displayed).IsChanged());
	TestTrue(TEXT("Unseen update stays unread"), F.S->GetEntryState(P, TEXT("Clue")).GetDisplayLabel() == EDocKnowledgeDisplayLabel::Updated);
	TestTrue(TEXT("Stale mark is NoChange"), F.S->MarkRead(P, TEXT("Clue"), Displayed).Outcome == EDocResultOutcome::NoChange);
	TestTrue(TEXT("Never-displayed revision rejected"), F.S->MarkRead(P, TEXT("Clue"), Displayed + 99).ErrorTag == DocKnowledgeTags::Error_Knowledge_StaleRevision);
	TestTrue(TEXT("Mark current"), F.S->MarkRead(P, TEXT("Clue"), F.View(P, TEXT("Clue")).VisibleRevision).IsChanged());
	TestTrue(TEXT("Read"), F.S->GetEntryState(P, TEXT("Clue")).GetDisplayLabel() == EDocKnowledgeDisplayLabel::Read);
	TestEqual(TEXT("Two read notifications"), F.Read.Num(), 2);
	TestTrue(TEXT("Read time stored"), F.S->GetEntryState(P, TEXT("Clue")).ReadTimeUtcTicks > 0);
	return true;
}

// KNO-06
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeSaveRestoreTest, "Doc.Knowledge.SaveRestore", DocKnowledgeTests::Flags)
bool FDocKnowledgeSaveRestoreTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* Old = MakeEntry(TEXT("OldName"), DocKnowledgeTags::Knowledge_History, TEXT("War"));
	UDocKnowledgeEntry* Gone = MakeEntry(TEXT("Gone"), DocKnowledgeTags::Knowledge_History, TEXT("Cut content"));
	UDocKnowledgeEntry* Kept = MakeEntry(TEXT("Kept"), DocKnowledgeTags::Knowledge_Mechanic, TEXT("Parry"));
	for (UDocKnowledgeEntry* E : { Old, Gone, Kept }) { F.S->RegisterEntry(E); }
	const FDocOwnerScope P = Player(1);
	F.S->DiscoverEntry(P, TEXT("OldName"), Grant());
	F.S->DiscoverEntry(P, TEXT("Gone"), Grant());
	F.S->DiscoverEntry(P, TEXT("Kept"), Grant());
	F.S->MarkRead(P, TEXT("Kept"), F.View(P, TEXT("Kept")).VisibleRevision);
	const FDocKnowledgeRuntimeState KeptBefore = F.S->GetEntryState(P, TEXT("Kept"));

	TArray<uint8> Bytes;
	DocCoreSerialization::Encode(F.S->CaptureState(), Bytes);
	FDocKnowledgeSaveData Saved;
	TestTrue(TEXT("Round trip"), DocCoreSerialization::Decode(Saved, Bytes));

	// New content: OldName renamed via redirect, Gone removed, Kept revised.
	UDocKnowledgeSubsystem* S2 = NewObject<UDocKnowledgeSubsystem>(F.GameInstance.Get());
	TStrongObjectPtr<UDocKnowledgeSubsystem> Keep2(S2);
	int32 Toasts = 0;
	S2->OnEntryDiscoveredNative.AddLambda([&Toasts](const FDocKnowledgeChange&) { ++Toasts; });
	UDocKnowledgeCatalog* Catalog = NewObject<UDocKnowledgeCatalog>(GetTransientPackage());
	UDocKnowledgeEntry* NewName = MakeEntry(TEXT("NewName"), DocKnowledgeTags::Knowledge_History, TEXT("The War"));
	UDocKnowledgeEntry* KeptV2 = MakeEntry(TEXT("Kept"), DocKnowledgeTags::Knowledge_Mechanic, TEXT("Parry"));
	KeptV2->ContentRevision = 2;
	Catalog->Entries = { NewName, KeptV2 };
	Catalog->EntryRedirects.Add(TEXT("OldName"), TEXT("NewName"));
	S2->RegisterCatalog(Catalog);
	TestTrue(TEXT("Restore"), S2->RestoreState(Saved).IsSuccess());

	TestEqual(TEXT("No discovery toasts on restore"), Toasts, 0);
	TestTrue(TEXT("Redirected entry keeps progress"), S2->IsDiscovered(P, TEXT("NewName")));
	const FDocKnowledgeRuntimeState GoneState = S2->GetEntryState(P, TEXT("Gone"));
	TestTrue(TEXT("Removed definition tombstoned, not completed"), GoneState.bQuarantined && !GoneState.bCompleted);
	TestFalse(TEXT("Tombstone is not a discovered fact"), S2->IsDiscovered(P, TEXT("Gone")));
	TestTrue(TEXT("Tombstone preserved in saves"), S2->CaptureState().Owners[0].Entries.ContainsByPredicate([](const FDocKnowledgeRuntimeState& X) { return X.EntryId == FName(TEXT("Gone")); }));
	const FDocKnowledgeRuntimeState KeptAfter = S2->GetEntryState(P, TEXT("Kept"));
	TestEqual(TEXT("Timestamps restored"), KeptAfter.DiscoveryTimeUtcTicks, KeptBefore.DiscoveryTimeUtcTicks);
	TestEqual(TEXT("Read revision restored"), KeptAfter.ReadRevision, KeptBefore.ReadRevision);
	TestEqual(TEXT("Restore never counts updates"), KeptAfter.UpdateCount, KeptBefore.UpdateCount);
	TestTrue(TEXT("Revised content shows as updated"), KeptAfter.GetDisplayLabel() == EDocKnowledgeDisplayLabel::Updated);
	S2->OnEntryDiscoveredNative.Clear();
	return true;
}

// KNO-07
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeSearchPrivacyTest, "Doc.Knowledge.SearchPrivacy", DocKnowledgeTests::Flags)
bool FDocKnowledgeSearchPrivacyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* Traitor = MakeEntry(TEXT("Traitor"), DocKnowledgeTags::Knowledge_Character, TEXT("Captain Voss"), TEXT("Secretly a traitor"), TEXT("Sold the gate codes"));
	Traitor->Tags.AddTag(TAG_Dragon);
	UDocKnowledgeEntry* Hint = MakeEntry(TEXT("Hint"), DocKnowledgeTags::Knowledge_Tutorial, TEXT("Sprinting"));
	Hint->bHiddenUntilDiscovered = false; // listed as a placeholder
	UDocKnowledgeEntry* Friend = MakeEntry(TEXT("Friend"), DocKnowledgeTags::Knowledge_Character, TEXT("Mara"));
	Friend->Relations = { Relation(TEXT("Traitor")) };
	for (UDocKnowledgeEntry* E : { Traitor, Hint, Friend }) { F.S->RegisterEntry(E); }
	const FDocOwnerScope P = Player(1);

	TestEqual(TEXT("Hidden title not searchable"), F.Search(P, TEXT("voss")).TotalMatches, 0);
	TestEqual(TEXT("Counts exclude hidden entries"), F.Search(P, TEXT("")).TotalMatches, 1);
	FDocKnowledgeVisibleEntry Placeholder = F.View(P, TEXT("Hint"));
	TestTrue(TEXT("Placeholder has no text"), Placeholder.bPlaceholder && Placeholder.Title.IsEmpty());
	TestEqual(TEXT("Placeholder never matches text"), F.Search(P, TEXT("sprint")).TotalMatches, 0);
	TestEqual(TEXT("Hidden tags not queryable"), F.S->GetEntriesByTag(P, TAG_Dragon).Num(), 0);

	F.S->DiscoverEntry(P, TEXT("Friend"), Grant());
	TestEqual(TEXT("Link to undiscovered entry hidden"), F.View(P, TEXT("Friend")).Relations.Num(), 0);
	FDocKnowledgeSearchQuery Related;
	Related.RelatedTo = TEXT("Traitor");
	TestEqual(TEXT("Relationship filter does not leak"), F.S->SearchEntriesNow(P, Related).TotalMatches, 0);

	F.S->DiscoverEntry(P, TEXT("Traitor"), Grant());
	TestEqual(TEXT("Title visible after discovery"), F.Search(P, TEXT("voss")).TotalMatches, 1);
	TestEqual(TEXT("Unrevealed summary still private"), F.Search(P, TEXT("traitor")).TotalMatches, 0);
	TestEqual(TEXT("Unrevealed body still private"), F.Search(P, TEXT("gate codes")).TotalMatches, 0);
	F.S->RevealEntry(P, TEXT("Traitor"), 3, Grant());
	TestEqual(TEXT("Body searchable once revealed"), F.Search(P, TEXT("gate codes")).TotalMatches, 1);
	TestEqual(TEXT("Other players see nothing"), F.Search(Player(2), TEXT("voss")).TotalMatches, 0);

	const FDocKnowledgeSearchPage Page = F.Search(P, TEXT(""), 2, 0);
	TestTrue(TEXT("Paginated"), Page.EntryIds.Num() == 2 && Page.TotalMatches == 3 && Page.bHasMore);
	return true;
}

// KNO-08
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeCultureChangeTest, "Doc.Knowledge.CultureChange", DocKnowledgeTests::Flags)
bool FDocKnowledgeCultureChangeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterEntry(MakeEntry(TEXT("Map"), DocKnowledgeTags::Knowledge_Location, TEXT("Harbor")));
	const FDocOwnerScope P = Player(1);
	F.S->DiscoverEntry(P, TEXT("Map"), Grant());
	F.Search(P, TEXT("harbor"));
	const int32 Builds = F.S->GetIndexBuildCount();
	F.Search(P, TEXT("harb"));
	TestEqual(TEXT("Cached while nothing changes"), F.S->GetIndexBuildCount(), Builds);
	F.S->HandleCultureChanged();
	TestEqual(TEXT("Still found after culture change"), F.Search(P, TEXT("harbor")).TotalMatches, 1);
	TestEqual(TEXT("Culture change rebuilds the index"), F.S->GetIndexBuildCount(), Builds + 1);
	TestTrue(TEXT("Record ids untouched"), F.S->IsDiscovered(P, TEXT("Map")));
	// Cooked second-culture text needs a packaged build (see README).
	return true;
}

// KNO-09
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeAsyncSearchTest, "Doc.Knowledge.AsyncSearch", DocKnowledgeTests::Flags)
bool FDocKnowledgeAsyncSearchTest::RunTest(const FString& Parameters)
{
	FFixture F;
	for (int32 i = 0; i < 30; ++i)
	{
		const FName Id(*FString::Printf(TEXT("Note%02d"), i));
		F.S->RegisterEntry(MakeEntry(Id, DocKnowledgeTags::Knowledge_Document, *FString::Printf(TEXT("Note %02d"), i)));
		F.S->DiscoverEntry(Player(1), Id, Grant());
	}
	TArray<TPair<UDocKnowledgeSubsystem::FSearchWork, UDocKnowledgeSubsystem::FSearchApply>> Pending;
	F.S->SetSearchDispatcherForTesting([&Pending](UDocKnowledgeSubsystem::FSearchWork Work, UDocKnowledgeSubsystem::FSearchApply Apply)
	{
		Pending.Add(TPair<UDocKnowledgeSubsystem::FSearchWork, UDocKnowledgeSubsystem::FSearchApply>(MoveTemp(Work), MoveTemp(Apply)));
	});
	auto RunPending = [&Pending](int32 Index) { Pending[Index].Value(Pending[Index].Key()); };

	TArray<FDocSystemResult> Results;
	TArray<FDocKnowledgeSearchPage> Pages;
	auto Callback = [&Results, &Pages](const FDocKnowledgeSearchPage& Page, const FDocSystemResult& R) { Pages.Add(Page); Results.Add(R); };
	FDocKnowledgeSearchQuery Q;
	Q.Text = TEXT("note");
	Q.PageSize = 10;
	Q.Offset = 10;

	F.S->SearchEntries(Player(1), Q, Callback);
	RunPending(0);
	TestTrue(TEXT("Page delivered"), Results.Num() == 1 && Results[0].IsSuccess() && Pages[0].EntryIds.Num() == 10 && Pages[0].TotalMatches == 30);
	TestEqual(TEXT("Second page starts at 10"), Pages[0].EntryIds[0], FName(TEXT("Note10")));

	const FDocRequestHandle Old = F.S->SearchEntries(Player(1), Q, Callback);
	F.S->CancelSearch(Old); // filter changed
	TestTrue(TEXT("Cancelled once"), Results.Num() == 2 && Results[1].Outcome == EDocResultOutcome::Cancelled);
	RunPending(1);
	TestEqual(TEXT("Late completion ignored"), Results.Num(), 2);

	F.S->SearchEntries(Player(1), Q, Callback);
	F.S->ConcealEntry(Player(1), TEXT("Note00"), true); // visible content changed meanwhile
	RunPending(2);
	TestTrue(TEXT("Stale generation rejected"), Results.Num() == 3 && Results[2].ErrorTag == DocKnowledgeTags::Error_Knowledge_SearchStale);

	F.S->SearchEntries(Player(1), Q, Callback);
	F.S->RemoveOwner(Player(1));
	TestTrue(TEXT("Owner removal cancels"), Results.Num() == 4 && Results[3].Outcome == EDocResultOutcome::Cancelled);
	RunPending(3);
	TestEqual(TEXT("No callback after owner removal"), Results.Num(), 4);
	return true;
}

// KNO-10
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeMediaFallbackTest, "Doc.Knowledge.MediaFallback", DocKnowledgeTests::Flags)
bool FDocKnowledgeMediaFallbackTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocKnowledgeEntry* Tape = MakeEntry(TEXT("Tape"), DocKnowledgeTags::Knowledge_Document, TEXT("Recording 7"));
	FDocKnowledgeMedia Audio;
	Audio.MediaId = TEXT("Voice");
	Audio.Type = EDocKnowledgeMediaType::Audio;
	Audio.Transcript = FText::FromString(TEXT("Meet me at dawn."));
	Audio.RevealStage = 1;
	FDocKnowledgeMedia Web;
	Web.MediaId = TEXT("Stream");
	Web.Type = EDocKnowledgeMediaType::External;
	Web.ExternalUrl = TEXT("https://example.invalid/stream");
	Web.RevealStage = 1;
	FDocKnowledgeMedia Photo;
	Photo.MediaId = TEXT("Photo");
	Photo.Asset = FSoftObjectPath(TEXT("/Game/Missing/T_Photo.T_Photo"));
	Photo.RevealStage = 1;
	Tape->Media = { Audio, Web, Photo };
	F.S->RegisterEntry(Tape);
	F.S->DiscoverEntry(Player(1), TEXT("Tape"), Grant());

	const FDocKnowledgeVisibleEntry V = F.View(Player(1), TEXT("Tape"));
	TestEqual(TEXT("Three descriptors"), V.Media.Num(), 3);
	if (V.Media.Num() == 3)
	{
		TestFalse(TEXT("Missing audio asset unavailable"), V.Media[0].bAvailable);
		TestTrue(TEXT("Transcript fallback"), V.Media[0].FallbackText.ToString() == TEXT("Meet me at dawn."));
		TestFalse(TEXT("External source needs a configured provider"), V.Media[1].bAvailable);
		TestTrue(TEXT("Title fallback without captions"), V.Media[1].FallbackText.ToString() == TEXT("Recording 7"));
		TestTrue(TEXT("Declared asset path is a descriptor, not a load"), V.Media[2].bAvailable);
	}
	return true;
}

// KNO-11
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocKnowledgeOwnerScopeTest, "Doc.Knowledge.OwnerScope", DocKnowledgeTests::Flags)
bool FDocKnowledgeOwnerScopeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterEntry(MakeEntry(TEXT("Relic"), DocKnowledgeTags::Knowledge_Item, TEXT("Relic")));
	F.S->RegisterEntry(MakeEntry(TEXT("Diary"), DocKnowledgeTags::Knowledge_Document, TEXT("Diary")));

	F.S->DiscoverEntry(Player(1), TEXT("Diary"), Grant());
	TestFalse(TEXT("Profiles are separate"), F.S->IsDiscovered(Player(2), TEXT("Diary")));

	F.S->LinkSharedScope(Player(1), Shared());
	F.S->LinkSharedScope(Player(2), Shared());
	F.S->DiscoverEntry(Shared(), TEXT("Relic"), Grant());
	TestTrue(TEXT("Shared discovery visible to P1"), F.S->IsDiscovered(Player(1), TEXT("Relic")));
	TestTrue(TEXT("Shared discovery visible to P2"), F.S->IsDiscovered(Player(2), TEXT("Relic")));
	F.S->MarkRead(Player(1), TEXT("Relic"), F.View(Player(1), TEXT("Relic")).VisibleRevision);
	TestTrue(TEXT("P1 read"), F.S->GetEntryState(Player(1), TEXT("Relic")).GetDisplayLabel() == EDocKnowledgeDisplayLabel::Read);
	TestTrue(TEXT("Read state stays per player"), F.S->GetEntryState(Player(2), TEXT("Relic")).GetDisplayLabel() == EDocKnowledgeDisplayLabel::Discovered);
	TestFalse(TEXT("Private discovery not shared"), F.S->IsDiscovered(Player(2), TEXT("Diary")));
	F.S->UnlinkSharedScope(Player(2));
	TestFalse(TEXT("Unlinked player no longer sees shared discoveries"), F.S->IsDiscovered(Player(2), TEXT("Relic")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
