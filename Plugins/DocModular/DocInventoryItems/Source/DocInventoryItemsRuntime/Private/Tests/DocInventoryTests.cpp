// DocInventoryItems automation tests (INV-01..12 base logic). No UI, GAS, crafting,
// combat or Save plugin: spawn and action effects use native test providers.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocItemSubsystem.h"
#include "DocCoreTestUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocInventoryTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Food, "Test.Inventory.Food");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Weapon, "Test.Inventory.Weapon");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cursed, "Test.Inventory.Cursed");

	const FGuid Campaign(7, 7, 7, 7);
	FDocOwnerScope Player(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0x1A1A, 1, 2, static_cast<uint32>(N)), Campaign); }
	FDocOwnerScope Shared() { return FDocOwnerScope(EDocOwnerScopeKind::SharedWorld, FGuid(0x1A1A, 9, 9, 9), Campaign); }
	FDocOwnerScope SessionScope() { return FDocOwnerScope(EDocOwnerScopeKind::Session, FGuid(0x1A1A, 5, 5, 5), Campaign); }

	UDocItemDefinition* MakeItem(FName Id, int32 MaxStack = 1, int64 Weight = 0, const FGameplayTag& Tag = FGameplayTag())
	{
		UDocItemDefinition* D = NewObject<UDocItemDefinition>(GetTransientPackage());
		D->ItemId = Id;
		D->MaxStackSize = MaxStack;
		D->WeightUnits = Weight;
		if (Tag.IsValid()) { D->ItemTags.AddTag(Tag); }
		return D;
	}

	FDocContainerRules ListRules(int32 MaxSlots = 0, int64 MaxWeight = 0)
	{
		FDocContainerRules R;
		R.Mode = EDocContainerMode::List;
		R.MaxSlots = MaxSlots;
		R.MaxWeightUnits = MaxWeight;
		return R;
	}

	FDocInventorySlot MakeSlot(FName Id, const FGameplayTag& EquipTag = FGameplayTag())
	{
		FDocInventorySlot S;
		S.SlotId = Id;
		S.EquipTag = EquipTag;
		return S;
	}

	FDocInventoryTransaction Tx(EDocInventoryOp Op, const FDocOwnerScope& Requester)
	{
		FDocInventoryTransaction T;
		T.RequestId = FGuid::NewGuid();
		T.Op = Op;
		T.Requester = Requester;
		return T;
	}

	class FTestSpawner final : public IDocItemSpawnProvider
	{
	public:
		bool bCanSpawn = true;
		bool bSpawnSucceeds = true;
		int32 Spawns = 0;
		TFunction<void()> DuringSpawn;

		virtual FDocSystemResult CanSpawn(const FDocWorldItemRecord&) const override
		{
			return bCanSpawn ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("No room here"));
		}
		virtual FDocSystemResult Spawn(const FDocWorldItemRecord&) override
		{
			++Spawns;
			if (DuringSpawn) { DuringSpawn(); }
			return bSpawnSucceeds ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Actor spawn failed"));
		}
	};

	class FTestAction final : public IDocItemActionHandler
	{
	public:
		FDocItemActionOutcome::EState State = FDocItemActionOutcome::EState::Completed;
		int32 Consume = 1;
		int32 Executes = 0;

		virtual FDocItemActionOutcome Execute(const FDocItemActionContext&) override
		{
			++Executes;
			FDocItemActionOutcome O;
			O.State = State;
			O.ConsumeQuantity = Consume;
			O.Result = State == FDocItemActionOutcome::EState::Failed ? FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Effect refused")) : FDocSystemResult::MakeSuccess();
			return O;
		}
	};

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocItemSubsystem> Keep;
		UDocItemSubsystem* S = nullptr;
		TArray<TPair<FName, int32>> Events; // container, delta count
		int32 Refreshes = 0;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			S = NewService();
			Keep.Reset(S);
			UDocItemSubsystem::SetSubsystemOverrideForTesting(S);
			S->OnContainerChangedNative.AddLambda([this](FName Id, const TArray<FDocInventoryDelta>& D) { Events.Add(TPair<FName, int32>(Id, D.Num())); });
			S->OnStateRefreshedNative.AddLambda([this]() { ++Refreshes; });
		}
		~FFixture()
		{
			S->OnContainerChangedNative.Clear();
			S->OnStateRefreshedNative.Clear();
			UDocItemSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}
		UDocItemSubsystem* NewService() const
		{
			UDocItemSubsystem* Service = NewObject<UDocItemSubsystem>(GameInstance.Get());
			Service->SetAuthorityOverrideForTesting(true);
			return Service;
		}

		FDocInventoryTransactionResult Grant(FName Container, FName Item, int32 Quantity, const FDocOwnerScope& Requester, bool bPartial = false)
		{
			FDocInventoryTransaction T = Tx(EDocInventoryOp::Add, Requester);
			T.DestContainer = Container;
			T.ItemId = Item;
			T.Quantity = Quantity;
			T.bAuthorizedSourceOrSink = true;
			T.bAllowPartial = bPartial;
			return S->SubmitTransaction(T);
		}

		FDocContainerRecord Get(FName Container, const FDocOwnerScope& Viewer) const
		{
			FDocContainerRecord R;
			S->GetContainer(Viewer, Container, R);
			return R;
		}

		int32 Count(FName Container, FName Item, const FDocOwnerScope& Viewer) const
		{
			int32 N = 0;
			const FDocContainerRecord Record = Get(Container, Viewer);
			for (const FDocItemInstance& I : Record.Items) { if (I.ItemId == Item) { N += I.Quantity; } }
			return N;
		}

		FGuid First(FName Container, FName Item, const FDocOwnerScope& Viewer) const
		{
			const FDocContainerRecord Record = Get(Container, Viewer);
			for (const FDocItemInstance& I : Record.Items) { if (I.ItemId == Item) { return I.InstanceId; } }
			return FGuid();
		}
	};

	/** Every instance id lives in exactly one record and the index agrees. */
	bool UniqueMembership(FAutomationTestBase& Test, const UDocItemSubsystem* S)
	{
		const FDocInventorySaveData Data = S->CaptureState();
		TSet<FGuid> Seen;
		bool bOk = true;
		auto Visit = [&](const FDocItemInstance& I, FName Where)
		{
			bool bDup = false;
			Seen.Add(I.InstanceId, &bDup);
			if (bDup || I.Quantity < 1) { Test.AddError(FString::Printf(TEXT("Instance %s duplicated or empty"), *I.InstanceId.ToString())); bOk = false; }
			if (!Where.IsNone() && S->FindInstanceContainer(I.InstanceId) != Where) { Test.AddError(TEXT("Index disagrees with membership")); bOk = false; }
		};
		for (const FDocContainerRecord& C : Data.Containers) { for (const FDocItemInstance& I : C.Items) { Visit(I, C.ContainerId); } }
		for (const FDocWorldItemRecord& W : Data.WorldItems) { Visit(W.Item, NAME_None); }
		for (const FDocItemInstance& I : Data.Quarantined) { Visit(I, NAME_None); }
		return bOk;
	}
}

using namespace DocInventoryTests;

// INV-01 (sources)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryAddTest, "Doc.Inventory.Add", DocInventoryTests::Flags)
bool FDocInventoryAddTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Potion"), 10));
	TestEqual(TEXT("Duplicate definition id rejected"), F.S->RegisterDefinition(MakeItem(TEXT("Potion"), 5)).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Bag"), F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules()).IsSuccess());
	TestEqual(TEXT("Re-register keeps membership"), F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules()).Outcome, EDocResultOutcome::NoChange);

	FDocInventoryTransaction Spoofed = Tx(EDocInventoryOp::Add, Player(1));
	Spoofed.DestContainer = TEXT("Bag");
	Spoofed.ItemId = TEXT("Potion");
	Spoofed.Quantity = 5;
	FDocInventoryTransactionResult R = F.S->SubmitTransaction(Spoofed);
	TestFalse(TEXT("Unauthorized grant fails"), R.bSuccess);
	TestEqual(TEXT("Unauthorized grant reason"), R.FailureReason, DocInventoryTags::Error_Inventory_Access.GetTag());
	TestEqual(TEXT("Unauthorized grant outcome"), R.Result.Outcome, EDocResultOutcome::PermissionDenied);

	R = F.Grant(TEXT("Bag"), TEXT("Potion"), 0, Player(1));
	TestEqual(TEXT("Zero quantity"), R.FailureReason, DocInventoryTags::Error_Inventory_Quantity.GetTag());
	TestEqual(TEXT("Zero quantity outcome"), R.Result.Outcome, EDocResultOutcome::InvalidInput);
	R = F.Grant(TEXT("Bag"), TEXT("Potion"), -3, Player(1));
	TestEqual(TEXT("Negative quantity"), R.FailureReason, DocInventoryTags::Error_Inventory_Quantity.GetTag());
	R = F.Grant(TEXT("Bag"), TEXT("Nope"), 1, Player(1));
	TestEqual(TEXT("Unknown definition"), R.FailureReason, DocInventoryTags::Error_Inventory_UnknownDefinition.GetTag());
	R = F.Grant(TEXT("Missing"), TEXT("Potion"), 1, Player(1));
	TestEqual(TEXT("Unknown container"), R.FailureReason, DocInventoryTags::Error_Inventory_UnknownContainer.GetTag());
	TestEqual(TEXT("Failures mutate nothing"), F.Get(TEXT("Bag"), Player(1)).Revision, static_cast<int64>(1));
	TestEqual(TEXT("No events for failures"), F.Events.Num(), 0);

	R = F.Grant(TEXT("Bag"), TEXT("Potion"), 5, Player(1));
	TestTrue(TEXT("Grant succeeds"), R.bSuccess);
	TestEqual(TEXT("Moved"), R.MovedQuantity, 5);
	TestEqual(TEXT("Revision committed"), R.CommittedRevisions.Num(), 1);
	TestEqual(TEXT("One stack"), F.Get(TEXT("Bag"), Player(1)).Items.Num(), 1);
	R = F.Grant(TEXT("Bag"), TEXT("Potion"), 7, Player(1));
	TestTrue(TEXT("Second grant"), R.bSuccess);
	const FDocContainerRecord Bag = F.Get(TEXT("Bag"), Player(1));
	TestEqual(TEXT("Fills the stack then opens another"), Bag.Items.Num(), 2);
	TestEqual(TEXT("Total"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 12);
	TestEqual(TEXT("Revision per commit"), Bag.Revision, static_cast<int64>(3));
	TestEqual(TEXT("Events after commits"), F.Events.Num(), 2);
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-01 (sinks)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryRemoveTest, "Doc.Inventory.Remove", DocInventoryTests::Flags)
bool FDocInventoryRemoveTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Potion"), 10));
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());
	F.Grant(TEXT("Bag"), TEXT("Potion"), 5, Player(1));
	const FGuid Id = F.First(TEXT("Bag"), TEXT("Potion"), Player(1));

	FDocInventoryTransaction T = Tx(EDocInventoryOp::Remove, Player(1));
	T.SourceContainer = TEXT("Bag");
	T.InstanceId = Id;
	T.Quantity = 3;
	T.bAuthorizedSourceOrSink = true;
	FDocInventoryTransactionResult R = F.S->SubmitTransaction(T);
	TestEqual(TEXT("Sink without reason"), R.Result.Outcome, EDocResultOutcome::InvalidInput);

	T.RequestId = FGuid::NewGuid();
	T.Reason = TEXT("Quest turn-in");
	T.bAuthorizedSourceOrSink = false;
	R = F.S->SubmitTransaction(T);
	TestEqual(TEXT("Unauthorized sink"), R.Result.Outcome, EDocResultOutcome::PermissionDenied);

	T.RequestId = FGuid::NewGuid();
	T.bAuthorizedSourceOrSink = true;
	T.Quantity = 6;
	R = F.S->SubmitTransaction(T);
	TestEqual(TEXT("More than held"), R.FailureReason, DocInventoryTags::Error_Inventory_Quantity.GetTag());
	TestEqual(TEXT("Remaining reported"), R.RemainingQuantity, 6);

	T.RequestId = FGuid::NewGuid();
	T.Quantity = 3;
	R = F.S->SubmitTransaction(T);
	TestTrue(TEXT("Partial removal"), R.bSuccess);
	TestEqual(TEXT("Two left"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 2);
	TestEqual(TEXT("Identity kept"), F.First(TEXT("Bag"), TEXT("Potion"), Player(1)), Id);
	TestFalse(TEXT("Not retired"), F.S->IsInstanceRetired(Id));

	FDocInventoryTransaction Eat = Tx(EDocInventoryOp::Consume, Player(1));
	Eat.SourceContainer = TEXT("Bag");
	Eat.InstanceId = Id;
	Eat.Quantity = 2;
	R = F.S->SubmitTransaction(Eat);
	TestTrue(TEXT("Owner consumption needs no grant flag"), R.bSuccess);
	TestEqual(TEXT("Empty"), F.Get(TEXT("Bag"), Player(1)).Items.Num(), 0);
	TestTrue(TEXT("Consumed id retired"), F.S->IsInstanceRetired(Id));
	TestEqual(TEXT("Index cleared"), F.S->FindInstanceContainer(Id), FName(NAME_None));
	TestEqual(TEXT("Conservation total"), F.S->CountItem(TEXT("Potion")), static_cast<int64>(0));
	return true;
}

// INV-02
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryTransferTest, "Doc.Inventory.Transfer", DocInventoryTests::Flags)
bool FDocInventoryTransferTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Gem"), 1, 0, TAG_Weapon));
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());
	F.S->RegisterContainer(TEXT("Chest"), Shared(), ListRules());
	FDocContainerRules FoodOnly = ListRules();
	FoodOnly.AllowedTags.AddTag(TAG_Food);
	F.S->RegisterContainer(TEXT("Pantry"), Shared(), FoodOnly);
	F.Grant(TEXT("Bag"), TEXT("Gem"), 1, Player(1));
	const FGuid Id = F.First(TEXT("Bag"), TEXT("Gem"), Player(1));
	const int64 BagRevision = F.Get(TEXT("Bag"), Player(1)).Revision;

	// Failed destination: nothing leaves the source.
	FDocInventoryTransaction Bad = Tx(EDocInventoryOp::Transfer, Player(1));
	Bad.SourceContainer = TEXT("Bag");
	Bad.DestContainer = TEXT("Pantry");
	Bad.InstanceId = Id;
	FDocInventoryTransactionResult R = F.S->SubmitTransaction(Bad);
	TestEqual(TEXT("Destination rule"), R.FailureReason, DocInventoryTags::Error_Inventory_TagRule.GetTag());
	TestEqual(TEXT("Source untouched"), F.Get(TEXT("Bag"), Player(1)).Revision, BagRevision);
	TestEqual(TEXT("Still in bag"), F.S->FindInstanceContainer(Id), FName(TEXT("Bag")));

	// Two requests for the last item built against the same revision.
	FDocInventoryTransaction A = Tx(EDocInventoryOp::Transfer, Player(1));
	A.SourceContainer = TEXT("Bag");
	A.DestContainer = TEXT("Chest");
	A.InstanceId = Id;
	A.ExpectedSourceRevision = BagRevision;
	FDocInventoryTransaction B = A;
	B.RequestId = FGuid::NewGuid();
	B.DestContainer = TEXT("Pantry");
	R = F.S->SubmitTransaction(A);
	TestTrue(TEXT("First claimant wins"), R.bSuccess);
	TestEqual(TEXT("Whole instance keeps its id"), F.First(TEXT("Chest"), TEXT("Gem"), Shared()), Id);
	TestEqual(TEXT("Two records committed"), R.CommittedRevisions.Num(), 2);
	R = F.S->SubmitTransaction(B);
	TestEqual(TEXT("Second claimant stale"), R.FailureReason, DocInventoryTags::Error_Inventory_StaleRevision.GetTag());
	FDocInventoryTransaction C = A;
	C.RequestId = FGuid::NewGuid();
	C.ExpectedSourceRevision = -1;
	R = F.S->SubmitTransaction(C);
	TestEqual(TEXT("Without revision: instance gone"), R.FailureReason, DocInventoryTags::Error_Inventory_UnknownInstance.GetTag());
	TestEqual(TEXT("Exactly one gem exists"), F.S->CountItem(TEXT("Gem")), static_cast<int64>(1));

	FDocInventoryTransaction Same = Tx(EDocInventoryOp::Transfer, Shared());
	Same.SourceContainer = TEXT("Chest");
	Same.DestContainer = TEXT("Chest");
	Same.InstanceId = Id;
	TestEqual(TEXT("Same container is NoChange"), F.S->SubmitTransaction(Same).Result.Outcome, EDocResultOutcome::NoChange);
	FDocInventoryTransaction Probe = Tx(EDocInventoryOp::Transfer, Player(2));
	Probe.SourceContainer = TEXT("Bag");
	Probe.DestContainer = TEXT("Bag");
	Probe.InstanceId = Id;
	const FDocInventoryTransactionResult ProbeResult = F.S->SubmitTransaction(Probe);
	TestFalse(TEXT("Same-container shortcut cannot probe a private container"), ProbeResult.bSuccess);
	TestTrue(TEXT("Probe is not reported as NoChange"), ProbeResult.Result.Outcome != EDocResultOutcome::NoChange);
	FDocInventoryTransaction Missing = Tx(EDocInventoryOp::Transfer, Shared());
	Missing.SourceContainer = TEXT("Nowhere");
	Missing.DestContainer = TEXT("Nowhere");
	Missing.InstanceId = Id;
	TestFalse(TEXT("Same-container shortcut cannot probe a missing container"), F.S->SubmitTransaction(Missing).bSuccess);
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-03
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventorySplitMergeTest, "Doc.Inventory.SplitMerge", DocInventoryTests::Flags)
bool FDocInventorySplitMergeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Arrow"), 50));
	UDocItemDefinition* Sword = MakeItem(TEXT("Sword"), 5);
	Sword->MaxDurability = 100;
	F.S->RegisterDefinition(Sword);
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());

	FDocInventoryTransaction Add = Tx(EDocInventoryOp::Add, Player(1));
	Add.DestContainer = TEXT("Bag");
	Add.ItemId = TEXT("Arrow");
	Add.Quantity = 20;
	Add.bAuthorizedSourceOrSink = true;
	FDocItemDataEntry Fletch;
	Fletch.Key = TEXT("Fletching");
	Fletch.StringValue = TEXT("Goose");
	Add.InitialCustomData.Add(Fletch);
	F.S->SubmitTransaction(Add);
	const FGuid Original = F.First(TEXT("Bag"), TEXT("Arrow"), Player(1));

	FDocInventoryTransaction Split = Tx(EDocInventoryOp::Split, Player(1));
	Split.SourceContainer = TEXT("Bag");
	Split.InstanceId = Original;
	Split.Quantity = 20;
	TestEqual(TEXT("Split must leave one"), F.S->SubmitTransaction(Split).FailureReason, DocInventoryTags::Error_Inventory_Quantity.GetTag());
	Split.RequestId = FGuid::NewGuid();
	Split.Quantity = 5;
	FDocInventoryTransactionResult R = F.S->SubmitTransaction(Split);
	TestTrue(TEXT("Split"), R.bSuccess);
	FDocContainerRecord Bag = F.Get(TEXT("Bag"), Player(1));
	TestEqual(TEXT("Two stacks"), Bag.Items.Num(), 2);
	const FDocItemInstance* Kept = Bag.Find(Original);
	const FDocItemInstance* Part = Bag.Items.FindByPredicate([&Original](const FDocItemInstance& I) { return I.InstanceId != Original; });
	TestTrue(TEXT("Original keeps its id"), Kept && Kept->Quantity == 15);
	TestTrue(TEXT("Exactly one new id"), Part && Part->Quantity == 5 && Part->InstanceId.IsValid());
	TestTrue(TEXT("Custom data retained"), Part && Part->CustomData.Num() == 1 && Part->CustomData[0] == Fletch);
	const FGuid PartId = Part ? Part->InstanceId : FGuid();

	FDocInventoryTransaction Merge = Tx(EDocInventoryOp::Merge, Player(1));
	Merge.SourceContainer = TEXT("Bag");
	Merge.InstanceId = PartId;
	Merge.OtherInstanceId = PartId;
	TestEqual(TEXT("Self merge"), F.S->SubmitTransaction(Merge).Result.Outcome, EDocResultOutcome::InvalidInput);
	Merge.RequestId = FGuid::NewGuid();
	Merge.OtherInstanceId = Original;
	R = F.S->SubmitTransaction(Merge);
	TestTrue(TEXT("Merge"), R.bSuccess);
	TestEqual(TEXT("Merged amount"), R.MovedQuantity, 5);
	Bag = F.Get(TEXT("Bag"), Player(1));
	TestEqual(TEXT("One stack"), Bag.Items.Num(), 1);
	TestTrue(TEXT("Survivor keeps id"), Bag.Find(Original) && Bag.Find(Original)->Quantity == 20);
	TestTrue(TEXT("Consumed id tombstoned"), F.S->IsInstanceRetired(PartId));

	// Durability is part of the stack key: no averaging.
	F.Grant(TEXT("Bag"), TEXT("Sword"), 1, Player(1));
	FDocInventoryTransaction Worn = Tx(EDocInventoryOp::Add, Player(1));
	Worn.DestContainer = TEXT("Bag");
	Worn.ItemId = TEXT("Sword");
	Worn.InitialDurability = 40;
	Worn.bAuthorizedSourceOrSink = true;
	F.S->SubmitTransaction(Worn);
	Bag = F.Get(TEXT("Bag"), Player(1));
	TArray<FGuid> Swords;
	for (const FDocItemInstance& I : Bag.Items) { if (I.ItemId == FName(TEXT("Sword"))) { Swords.Add(I.InstanceId); } }
	TestEqual(TEXT("Different durability does not stack"), Swords.Num(), 2);
	if (Swords.Num() == 2)
	{
		FDocInventoryTransaction Mixed = Tx(EDocInventoryOp::Merge, Player(1));
		Mixed.SourceContainer = TEXT("Bag");
		Mixed.InstanceId = Swords[0];
		Mixed.OtherInstanceId = Swords[1];
		TestEqual(TEXT("Mixed durability merge refused"), F.S->SubmitTransaction(Mixed).FailureReason, DocInventoryTags::Error_Inventory_Stack.GetTag());
		const FDocContainerRecord After = F.Get(TEXT("Bag"), Player(1));
		const FDocItemInstance* W = After.Find(Swords[1]);
		TestTrue(TEXT("Durability retained"), W && (W->Durability == 40 || W->Durability == 100));
	}
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-04
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryCapacityTest, "Doc.Inventory.Capacity", DocInventoryTests::Flags)
bool FDocInventoryCapacityTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Potion"), 10));
	F.S->RegisterDefinition(MakeItem(TEXT("Ore"), 10, 30));
	F.S->RegisterDefinition(MakeItem(TEXT("Anvil"), 10, MAX_int64 / 2));
	F.S->RegisterContainer(TEXT("Pouch"), Player(1), ListRules(2));
	F.S->RegisterContainer(TEXT("Cart"), Player(1), ListRules(0, 100));
	F.S->RegisterContainer(TEXT("Yard"), Player(1), ListRules());

	FDocInventoryTransactionResult R = F.Grant(TEXT("Pouch"), TEXT("Potion"), 25, Player(1));
	TestEqual(TEXT("All-or-nothing capacity"), R.FailureReason, DocInventoryTags::Error_Inventory_Capacity.GetTag());
	TestEqual(TEXT("Nothing added"), F.Count(TEXT("Pouch"), TEXT("Potion"), Player(1)), 0);
	R = F.Grant(TEXT("Pouch"), TEXT("Potion"), 20, Player(1));
	TestTrue(TEXT("Exact boundary fits"), R.bSuccess && !R.bPartialSuccess);
	FDocInventoryTransaction Clear = Tx(EDocInventoryOp::Remove, Player(1));
	Clear.bAuthorizedSourceOrSink = true;
	Clear.Reason = TEXT("Test reset");
	Clear.SourceContainer = TEXT("Pouch");
	const FDocContainerRecord Pouch = F.Get(TEXT("Pouch"), Player(1));
	for (const FDocItemInstance& I : Pouch.Items)
	{
		Clear.RequestId = FGuid::NewGuid();
		Clear.InstanceId = I.InstanceId;
		Clear.Quantity = I.Quantity;
		F.S->SubmitTransaction(Clear);
	}
	R = F.Grant(TEXT("Pouch"), TEXT("Potion"), 25, Player(1), true);
	TestTrue(TEXT("Partial success"), R.bSuccess && R.bPartialSuccess);
	TestEqual(TEXT("Moved what fits"), R.MovedQuantity, 20);
	TestEqual(TEXT("Remainder reported"), R.RemainingQuantity, 5);
	TestEqual(TEXT("Contents"), F.Count(TEXT("Pouch"), TEXT("Potion"), Player(1)), 20);

	R = F.Grant(TEXT("Cart"), TEXT("Ore"), 4, Player(1));
	TestEqual(TEXT("Weight limit"), R.FailureReason, DocInventoryTags::Error_Inventory_Weight.GetTag());
	R = F.Grant(TEXT("Cart"), TEXT("Ore"), 4, Player(1), true);
	TestEqual(TEXT("Weight partial"), R.MovedQuantity, 3);
	TestEqual(TEXT("Weight remainder"), R.RemainingQuantity, 1);

	R = F.Grant(TEXT("Yard"), TEXT("Anvil"), 3, Player(1));
	TestEqual(TEXT("Weight overflow is detected, not wrapped"), R.FailureReason, DocInventoryTags::Error_Inventory_Weight.GetTag());
	TestEqual(TEXT("Overflow adds nothing"), F.Count(TEXT("Yard"), TEXT("Anvil"), Player(1)), 0);

	FDocInventoryTransaction Preview = Tx(EDocInventoryOp::Add, Player(1));
	Preview.DestContainer = TEXT("Yard");
	Preview.ItemId = TEXT("Potion");
	Preview.Quantity = 3;
	Preview.bAuthorizedSourceOrSink = true;
	const int64 YardRevision = F.Get(TEXT("Yard"), Player(1)).Revision;
	TestTrue(TEXT("Preview estimates success"), F.S->PreviewTransaction(Preview).bSuccess);
	TestEqual(TEXT("Preview mutates nothing"), F.Get(TEXT("Yard"), Player(1)).Revision, YardRevision);
	FDocInventoryTransactionResult Unused;
	TestFalse(TEXT("Preview records no result"), F.S->GetTransactionResult(Preview.RequestId, Unused));
	return true;
}

// INV-05
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryTagRulesTest, "Doc.Inventory.TagRules", DocInventoryTests::Flags)
bool FDocInventoryTagRulesTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Bread"), 10, 0, TAG_Food));
	F.S->RegisterDefinition(MakeItem(TEXT("Axe"), 1, 0, TAG_Weapon));
	F.S->RegisterDefinition(MakeItem(TEXT("Idol"), 1, 0, TAG_Cursed));
	F.S->RegisterDefinition(MakeItem(TEXT("Key"), 1));
	UDocItemDefinition* Great = MakeItem(TEXT("Greatsword"), 1, 0, TAG_Weapon);
	Great->EquipTags.AddTag(DocInventoryTags::Equip_Primary);
	Great->EquipTags.AddTag(DocInventoryTags::Equip_Secondary);
	Great->bOccupiesAllEquipSlots = true;
	F.S->RegisterDefinition(Great);
	UDocItemDefinition* Dagger = MakeItem(TEXT("Dagger"), 1, 0, TAG_Weapon);
	Dagger->EquipTags.AddTag(DocInventoryTags::Equip_Secondary);
	F.S->RegisterDefinition(Dagger);

	FDocContainerRules FoodOnly = ListRules();
	FoodOnly.AllowedTags.AddTag(TAG_Food);
	FDocContainerRules NoCursed = ListRules();
	NoCursed.BlockedTags.AddTag(TAG_Cursed);
	FDocContainerRules Unique = ListRules();
	Unique.bUniqueItems = true;
	FDocContainerRules Gear;
	Gear.Mode = EDocContainerMode::Equipment;
	Gear.SlotTypes = { MakeSlot(TEXT("MainHand"), DocInventoryTags::Equip_Primary), MakeSlot(TEXT("OffHand"), DocInventoryTags::Equip_Secondary) };
	F.S->RegisterContainer(TEXT("Pantry"), Player(1), FoodOnly);
	F.S->RegisterContainer(TEXT("Bag"), Player(1), NoCursed);
	F.S->RegisterContainer(TEXT("Keyring"), Player(1), Unique);
	F.S->RegisterContainer(TEXT("Gear"), Player(1), Gear);

	TestTrue(TEXT("Allowed tag"), F.Grant(TEXT("Pantry"), TEXT("Bread"), 2, Player(1)).bSuccess);
	TestEqual(TEXT("Not in allowed tags"), F.Grant(TEXT("Pantry"), TEXT("Axe"), 1, Player(1)).FailureReason, DocInventoryTags::Error_Inventory_TagRule.GetTag());
	TestEqual(TEXT("Blocked tag"), F.Grant(TEXT("Bag"), TEXT("Idol"), 1, Player(1)).FailureReason, DocInventoryTags::Error_Inventory_TagRule.GetTag());
	TestTrue(TEXT("First key"), F.Grant(TEXT("Keyring"), TEXT("Key"), 1, Player(1)).bSuccess);
	TestEqual(TEXT("Unique"), F.Grant(TEXT("Keyring"), TEXT("Key"), 1, Player(1)).FailureReason, DocInventoryTags::Error_Inventory_Unique.GetTag());

	F.Grant(TEXT("Bag"), TEXT("Greatsword"), 1, Player(1));
	F.Grant(TEXT("Bag"), TEXT("Dagger"), 1, Player(1));
	F.Grant(TEXT("Bag"), TEXT("Bread"), 1, Player(1));
	const FGuid GreatId = F.First(TEXT("Bag"), TEXT("Greatsword"), Player(1));
	const FGuid DaggerId = F.First(TEXT("Bag"), TEXT("Dagger"), Player(1));

	auto Equip = [&F](const FGuid& Id)
	{
		FDocInventoryTransaction T = Tx(EDocInventoryOp::Equip, Player(1));
		T.SourceContainer = TEXT("Bag");
		T.DestContainer = TEXT("Gear");
		T.InstanceId = Id;
		return F.S->SubmitTransaction(T);
	};
	TestEqual(TEXT("Not equippable"), Equip(F.First(TEXT("Bag"), TEXT("Bread"), Player(1))).FailureReason, DocInventoryTags::Error_Inventory_Slot.GetTag());
	TestTrue(TEXT("Dagger equipped"), Equip(DaggerId).bSuccess);
	const FDocContainerRecord GearA = F.Get(TEXT("Gear"), Player(1));
	const FDocItemInstance* EquippedDagger = GearA.Find(DaggerId);
	TestTrue(TEXT("Dagger in off hand"), EquippedDagger && EquippedDagger->Slots == TArray<FName>({ FName(TEXT("OffHand")) }));
	TestEqual(TEXT("Two-hander needs both slots"), Equip(GreatId).FailureReason, DocInventoryTags::Error_Inventory_Slot.GetTag());
	TestEqual(TEXT("Failed equip leaves item in bag"), F.S->FindInstanceContainer(GreatId), FName(TEXT("Bag")));

	FDocInventoryTransaction Unequip = Tx(EDocInventoryOp::Unequip, Player(1));
	Unequip.SourceContainer = TEXT("Gear");
	Unequip.DestContainer = TEXT("Bag");
	Unequip.InstanceId = DaggerId;
	TestTrue(TEXT("Unequip"), F.S->SubmitTransaction(Unequip).bSuccess);
	TestTrue(TEXT("Two-hander equips"), Equip(GreatId).bSuccess);
	const FDocContainerRecord GearB = F.Get(TEXT("Gear"), Player(1));
	const FDocItemInstance* EquippedGreat = GearB.Find(GreatId);
	TestTrue(TEXT("Occupies both slots atomically"), EquippedGreat && EquippedGreat->Slots.Num() == 2);
	TestEqual(TEXT("Dagger has no free slot"), Equip(DaggerId).FailureReason, DocInventoryTags::Error_Inventory_Slot.GetTag());
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-06
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventorySaveRestoreTest, "Doc.Inventory.SaveRestore", DocInventoryTests::Flags)
bool FDocInventorySaveRestoreTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocItemDefinition* Potion = MakeItem(TEXT("Potion"), 10);
	UDocItemDefinition* Helmet = MakeItem(TEXT("Helmet"));
	Helmet->EquipTags.AddTag(DocInventoryTags::Equip_Head);
	UDocItemDefinition* Relic = MakeItem(TEXT("Relic"));
	F.S->RegisterDefinition(Potion);
	F.S->RegisterDefinition(Helmet);
	F.S->RegisterDefinition(Relic);
	FDocContainerRules Gear;
	Gear.Mode = EDocContainerMode::Equipment;
	Gear.SlotTypes = { MakeSlot(TEXT("Head"), DocInventoryTags::Equip_Head) };
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());
	F.S->RegisterContainer(TEXT("Gear"), Player(1), Gear);
	F.S->RegisterContainer(TEXT("Scratch"), SessionScope(), ListRules());
	const FDocInventoryTransactionResult Reward = F.Grant(TEXT("Bag"), TEXT("Potion"), 4, Player(1));
	F.Grant(TEXT("Bag"), TEXT("Helmet"), 1, Player(1));
	F.Grant(TEXT("Bag"), TEXT("Relic"), 1, Player(1));
	F.Grant(TEXT("Scratch"), TEXT("Potion"), 1, SessionScope());
	const FGuid HelmetId = F.First(TEXT("Bag"), TEXT("Helmet"), Player(1));
	const FGuid PotionId = F.First(TEXT("Bag"), TEXT("Potion"), Player(1));
	const FGuid RelicId = F.First(TEXT("Bag"), TEXT("Relic"), Player(1));
	FDocInventoryTransaction Equip = Tx(EDocInventoryOp::Equip, Player(1));
	Equip.SourceContainer = TEXT("Bag");
	Equip.DestContainer = TEXT("Gear");
	Equip.InstanceId = HelmetId;
	F.S->SubmitTransaction(Equip);
	FGuid WorldId;
	F.S->CreateWorldItem(TEXT("Potion"), 2, FTransform(FVector(100, 0, 0)), WorldId);

	const FDocInventorySaveData Saved = F.S->CaptureState();
	TestEqual(TEXT("Session container not saved"), Saved.Containers.Num(), 2);
	TestEqual(TEXT("World item saved"), Saved.WorldItems.Num(), 1);

	// A fresh service without the Relic definition.
	UDocItemSubsystem* Fresh = F.NewService();
	TStrongObjectPtr<UDocItemSubsystem> KeepFresh(Fresh);
	Fresh->RegisterDefinition(Potion);
	Fresh->RegisterDefinition(Helmet);
	int32 Refreshes = 0;
	Fresh->OnStateRefreshedNative.AddLambda([&Refreshes]() { ++Refreshes; });
	TestTrue(TEXT("Restore"), Fresh->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("One refresh"), Refreshes, 1);
	TestEqual(TEXT("Missing definition quarantined"), Fresh->GetQuarantinedCount(), 1);
	TestEqual(TEXT("Quarantined relic still counted"), Fresh->CountItem(TEXT("Relic")), static_cast<int64>(1));
	TestEqual(TEXT("Instance identity"), Fresh->FindInstanceContainer(PotionId), FName(TEXT("Bag")));
	TestEqual(TEXT("Equipment membership"), Fresh->FindInstanceContainer(HelmetId), FName(TEXT("Gear")));
	FDocContainerRecord GearRecord;
	Fresh->GetContainer(Player(1), TEXT("Gear"), GearRecord);
	TestTrue(TEXT("Equipment slot restored"), GearRecord.Find(HelmetId) && GearRecord.Find(HelmetId)->Slots.Contains(FName(TEXT("Head"))));
	FDocWorldItemRecord World;
	TestTrue(TEXT("World item restored"), Fresh->GetWorldItem(WorldId, World) && World.Item.Quantity == 2);
	TestEqual(TEXT("Quarantine survives the next save"), Fresh->CaptureState().Quarantined.Num(), 1);
	TestEqual(TEXT("Relic not in a container"), Fresh->FindInstanceContainer(RelicId), FName(NAME_None));

	// Restart-safe reward retry: the same request replays the receipt.
	FDocInventoryTransaction Retry = Tx(EDocInventoryOp::Add, Player(1));
	Retry.RequestId = Reward.RequestId;
	Retry.DestContainer = TEXT("Bag");
	Retry.ItemId = TEXT("Potion");
	Retry.Quantity = 4;
	Retry.bAuthorizedSourceOrSink = true;
	const FDocInventoryTransactionResult Replayed = Fresh->SubmitTransaction(Retry);
	TestTrue(TEXT("Receipt replayed"), Replayed.bSuccess);
	TestEqual(TEXT("No second grant"), Fresh->CountItem(TEXT("Potion")), static_cast<int64>(4 + 2));

	// Duplicate membership is rejected and leaves the service untouched.
	FDocInventorySaveData Corrupt = Saved;
	if (Corrupt.Containers.Num() == 2 && Corrupt.Containers[0].Items.Num() > 0)
	{
		Corrupt.Containers[1].Items.Add(Corrupt.Containers[0].Items[0]);
	}
	const int64 Before = Fresh->CountItem(TEXT("Potion"));
	TestEqual(TEXT("Duplicate membership rejected"), Fresh->RestoreState(Corrupt).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Rejected restore changes nothing"), Fresh->CountItem(TEXT("Potion")), Before);
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, Fresh));
	Fresh->OnStateRefreshedNative.Clear();

	// World item ids must be present and unique.
	FDocInventorySaveData DupWorld = Saved;
	if (DupWorld.WorldItems.Num() == 1)
	{
		FDocWorldItemRecord Copy = DupWorld.WorldItems[0];
		Copy.Item.InstanceId = FGuid::NewGuid();
		DupWorld.WorldItems.Add(Copy);
	}
	TestEqual(TEXT("Duplicate world item id rejected"), F.S->RestoreState(DupWorld).Outcome, EDocResultOutcome::InvalidInput);

	// Restoring into a live service keeps the session container the save never contained.
	TestTrue(TEXT("Restore into live service"), F.S->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("Session container survives restore"), F.Count(TEXT("Scratch"), TEXT("Potion"), SessionScope()), 1);
	TestEqual(TEXT("Saved containers restored"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 4);
	TestTrue(TEXT("Membership unique after live restore"), UniqueMembership(*this, F.S));
	return true;
}

// INV-07
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventorySwapReentrancyTest, "Doc.Inventory.SwapAndReentrancy", DocInventoryTests::Flags)
bool FDocInventorySwapReentrancyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Bread"), 1, 0, TAG_Food));
	F.S->RegisterDefinition(MakeItem(TEXT("Axe"), 1, 0, TAG_Weapon));
	FDocContainerRules Slots;
	Slots.Mode = EDocContainerMode::Slot;
	Slots.SlotTypes = { MakeSlot(TEXT("A")), MakeSlot(TEXT("B")) };
	FDocContainerRules FoodOnly = ListRules();
	FoodOnly.AllowedTags.AddTag(TAG_Food);
	F.S->RegisterContainer(TEXT("Belt"), Player(1), Slots);
	F.S->RegisterContainer(TEXT("Pantry"), Player(1), FoodOnly);
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());
	F.Grant(TEXT("Belt"), TEXT("Bread"), 1, Player(1));
	F.Grant(TEXT("Belt"), TEXT("Axe"), 1, Player(1));
	F.Grant(TEXT("Pantry"), TEXT("Bread"), 1, Player(1));
	const FGuid BreadId = F.First(TEXT("Belt"), TEXT("Bread"), Player(1));
	const FGuid AxeId = F.First(TEXT("Belt"), TEXT("Axe"), Player(1));
	const FGuid PantryBread = F.First(TEXT("Pantry"), TEXT("Bread"), Player(1));

	FDocInventoryTransaction SwapSlots = Tx(EDocInventoryOp::Swap, Player(1));
	SwapSlots.SourceContainer = TEXT("Belt");
	SwapSlots.InstanceId = BreadId;
	SwapSlots.OtherInstanceId = AxeId;
	TestTrue(TEXT("In-container swap"), F.S->SubmitTransaction(SwapSlots).bSuccess);
	const FDocContainerRecord Belt = F.Get(TEXT("Belt"), Player(1));
	const FDocItemInstance* BeltBread = Belt.Find(BreadId);
	const FDocItemInstance* BeltAxe = Belt.Find(AxeId);
	TestTrue(TEXT("Slots exchanged"), BeltBread && BeltAxe && BeltBread->Slots == TArray<FName>({ FName(TEXT("B")) }) && BeltAxe->Slots == TArray<FName>({ FName(TEXT("A")) }));

	// Two-way validation: the axe may not enter the pantry, so neither side moves.
	FDocInventoryTransaction Cross = Tx(EDocInventoryOp::Swap, Player(1));
	Cross.SourceContainer = TEXT("Belt");
	Cross.DestContainer = TEXT("Pantry");
	Cross.InstanceId = AxeId;
	Cross.OtherInstanceId = PantryBread;
	TestEqual(TEXT("Atomic swap refused"), F.S->SubmitTransaction(Cross).FailureReason, DocInventoryTags::Error_Inventory_TagRule.GetTag());
	TestEqual(TEXT("Axe stays"), F.S->FindInstanceContainer(AxeId), FName(TEXT("Belt")));
	TestEqual(TEXT("Bread stays"), F.S->FindInstanceContainer(PantryBread), FName(TEXT("Pantry")));
	Cross.RequestId = FGuid::NewGuid();
	Cross.InstanceId = BreadId;
	TestTrue(TEXT("Valid cross swap"), F.S->SubmitTransaction(Cross).bSuccess);
	TestEqual(TEXT("Belt bread moved"), F.S->FindInstanceContainer(BreadId), FName(TEXT("Pantry")));
	TestEqual(TEXT("Pantry bread moved"), F.S->FindInstanceContainer(PantryBread), FName(TEXT("Belt")));

	// A reentrant request from a provider during commit is queued, not interleaved.
	TSharedPtr<FTestSpawner> Spawner = MakeShared<FTestSpawner>();
	F.S->SetSpawnProvider(Spawner);
	FDocInventoryTransaction Inner = Tx(EDocInventoryOp::Transfer, Player(1));
	Inner.SourceContainer = TEXT("Pantry");
	Inner.DestContainer = TEXT("Bag");
	Inner.InstanceId = BreadId;
	FDocInventoryTransactionResult InnerImmediate;
	Spawner->DuringSpawn = [&F, &Inner, &InnerImmediate]() { InnerImmediate = F.S->SubmitTransaction(Inner); };
	int64 ObservedBeltRevision = -1;
	F.S->OnContainerChangedNative.AddLambda([&F, &ObservedBeltRevision](FName Id, const TArray<FDocInventoryDelta>&)
	{
		if (Id == FName(TEXT("Belt"))) { ObservedBeltRevision = F.Get(TEXT("Belt"), Player(1)).Revision; }
	});
	FDocInventoryTransaction Drop = Tx(EDocInventoryOp::Drop, Player(1));
	Drop.SourceContainer = TEXT("Belt");
	Drop.InstanceId = AxeId;
	const FDocInventoryTransactionResult Dropped = F.S->SubmitTransaction(Drop);
	TestTrue(TEXT("Drop"), Dropped.bSuccess);
	TestEqual(TEXT("Inner request was queued"), InnerImmediate.Result.Outcome, EDocResultOutcome::NotReady);
	FDocInventoryTransactionResult InnerFinal;
	TestTrue(TEXT("Queued request ran after the commit"), F.S->GetTransactionResult(Inner.RequestId, InnerFinal) && InnerFinal.bSuccess);
	TestEqual(TEXT("Bread reached the bag"), F.S->FindInstanceContainer(BreadId), FName(TEXT("Bag")));
	TestEqual(TEXT("Observer saw committed state"), ObservedBeltRevision, F.Get(TEXT("Belt"), Player(1)).Revision);
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-08
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryRetryReceiptTest, "Doc.Inventory.RetryReceipt", DocInventoryTests::Flags)
bool FDocInventoryRetryReceiptTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Coin"), 999));
	F.S->RegisterContainer(TEXT("Purse"), Player(1), ListRules());
	F.S->RegisterContainer(TEXT("Vault"), Shared(), ListRules());

	FDocInventoryTransaction Reward = Tx(EDocInventoryOp::Add, Player(1));
	Reward.DestContainer = TEXT("Purse");
	Reward.ItemId = TEXT("Coin");
	Reward.Quantity = 50;
	Reward.bAuthorizedSourceOrSink = true;
	const FDocInventoryTransactionResult First = F.S->SubmitTransaction(Reward);
	TestTrue(TEXT("Reward"), First.bSuccess);
	const FDocInventoryTransactionResult Again = F.S->SubmitTransaction(Reward);
	TestTrue(TEXT("Retry returns the original"), Again.bSuccess && Again.CommittedRevisions.Num() == First.CommittedRevisions.Num()
		&& Again.CommittedRevisions[0].Revision == First.CommittedRevisions[0].Revision);
	TestEqual(TEXT("No duplicate grant"), F.Count(TEXT("Purse"), TEXT("Coin"), Player(1)), 50);

	FDocInventoryTransaction Changed = Reward;
	Changed.Quantity = 500;
	const FDocInventoryTransactionResult Conflict = F.S->SubmitTransaction(Changed);
	TestEqual(TEXT("Changed payload"), Conflict.FailureReason, DocInventoryTags::Error_Inventory_ReceiptConflict.GetTag());
	TestEqual(TEXT("Changed payload outcome"), Conflict.Result.Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Still 50"), F.Count(TEXT("Purse"), TEXT("Coin"), Player(1)), 50);
	FDocInventoryTransactionResult Stored;
	TestTrue(TEXT("Committed result still queryable"), F.S->GetTransactionResult(Reward.RequestId, Stored));
	TestTrue(TEXT("Conflict did not overwrite the committed result"), Stored.bSuccess);
	TestTrue(TEXT("Original retry still replays after a conflict"), F.S->SubmitTransaction(Reward).bSuccess);
	TestEqual(TEXT("Still 50 after replay"), F.Count(TEXT("Purse"), TEXT("Coin"), Player(1)), 50);

	FDocInventoryTransaction Stale = Tx(EDocInventoryOp::Transfer, Player(1));
	Stale.SourceContainer = TEXT("Purse");
	Stale.DestContainer = TEXT("Vault");
	Stale.InstanceId = F.First(TEXT("Purse"), TEXT("Coin"), Player(1));
	Stale.Quantity = 10;
	Stale.ExpectedSourceRevision = 1;
	TestEqual(TEXT("Stale revision"), F.S->SubmitTransaction(Stale).FailureReason, DocInventoryTags::Error_Inventory_StaleRevision.GetTag());
	Stale.ExpectedSourceRevision = F.Get(TEXT("Purse"), Player(1)).Revision;
	Stale.ExpectedDestRevision = 999;
	Stale.RequestId = FGuid::NewGuid();
	TestEqual(TEXT("Stale destination"), F.S->SubmitTransaction(Stale).FailureReason, DocInventoryTags::Error_Inventory_StaleRevision.GetTag());

	FDocInventoryTransaction NoKey = Reward;
	NoKey.RequestId.Invalidate();
	TestEqual(TEXT("Commit requires a key"), F.S->SubmitTransaction(NoKey).Result.Outcome, EDocResultOutcome::InvalidInput);

	// Restart: the receipt is saved with the containers and still suppresses the duplicate.
	const FDocInventorySaveData Saved = F.S->CaptureState();
	TestEqual(TEXT("Receipts saved"), Saved.Receipts.Num(), 1);
	UDocItemSubsystem* Restarted = F.NewService();
	TStrongObjectPtr<UDocItemSubsystem> KeepRestarted(Restarted);
	Restarted->RegisterDefinition(const_cast<UDocItemDefinition*>(F.S->FindDefinition(TEXT("Coin"))));
	Restarted->RestoreState(Saved);
	const FDocInventoryTransactionResult AfterRestart = Restarted->SubmitTransaction(Reward);
	TestTrue(TEXT("Replayed after restart"), AfterRestart.bSuccess);
	TestEqual(TEXT("Restart-safe: no duplicate"), Restarted->CountItem(TEXT("Coin")), static_cast<int64>(50));
	return true;
}

// INV-09
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryPickupRaceTest, "Doc.Inventory.PickupRace", DocInventoryTests::Flags)
bool FDocInventoryPickupRaceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocScopedTestWorld TestWorld; // declared after the fixture: destroyed first
	F.S->RegisterDefinition(MakeItem(TEXT("Gem"), 1));
	F.S->RegisterDefinition(MakeItem(TEXT("Rock"), 1));
	F.S->RegisterContainer(TEXT("BagA"), Player(1), ListRules());
	F.S->RegisterContainer(TEXT("BagB"), Player(2), ListRules());
	F.S->RegisterContainer(TEXT("Full"), Player(1), ListRules(1));
	F.Grant(TEXT("Full"), TEXT("Rock"), 1, Player(1));

	FGuid WorldId;
	TestTrue(TEXT("World item"), F.S->CreateWorldItem(TEXT("Gem"), 1, FTransform::Identity, WorldId).IsSuccess());
	FDocWorldItemRecord Record;
	F.S->GetWorldItem(WorldId, Record);
	const FGuid GemId = Record.Item.InstanceId;

	AActor* Actor = TestWorld.Spawn<AActor>();
	UDocWorldItemComponent* Component = Actor ? NewObject<UDocWorldItemComponent>(Actor) : nullptr;
	TWeakObjectPtr<AActor> WeakActor(Actor);
	if (Component)
	{
		Component->WorldItemId = WorldId;
		F.S->BindWorldItemComponent(Component);
	}

	auto Pickup = [&F, &WorldId](const FDocOwnerScope& Who, FName Bag, const FGuid& Request)
	{
		FDocInventoryTransaction T = Tx(EDocInventoryOp::Pickup, Who);
		T.RequestId = Request;
		T.DestContainer = Bag;
		T.WorldItemId = WorldId;
		return F.S->SubmitTransaction(T);
	};

	// Commit failure: the world record stays and nothing is duplicated.
	FDocInventoryTransactionResult R = Pickup(Player(1), TEXT("Full"), FGuid::NewGuid());
	TestEqual(TEXT("Full container"), R.FailureReason, DocInventoryTags::Error_Inventory_Capacity.GetTag());
	TestTrue(TEXT("World record kept"), F.S->GetWorldItem(WorldId, Record));
	TestTrue(TEXT("Actor kept"), WeakActor.IsValid());

	const FGuid ClaimA = FGuid::NewGuid();
	const FGuid ClaimB = FGuid::NewGuid();
	TestTrue(TEXT("A reserves"), F.S->ReserveWorldItem(WorldId, ClaimA, 2.f).IsSuccess());
	TestEqual(TEXT("B cannot reserve"), F.S->ReserveWorldItem(WorldId, ClaimB, 2.f).Outcome, EDocResultOutcome::Conflict);
	R = Pickup(Player(2), TEXT("BagB"), ClaimB);
	TestEqual(TEXT("B pickup refused"), R.FailureReason, DocInventoryTags::Error_Inventory_Reserved.GetTag());
	R = Pickup(Player(1), TEXT("BagA"), ClaimA);
	TestTrue(TEXT("A picks up"), R.bSuccess);
	TestEqual(TEXT("Same instance id"), F.S->FindInstanceContainer(GemId), FName(TEXT("BagA")));
	TestFalse(TEXT("World record gone"), F.S->GetWorldItem(WorldId, Record));
	TestFalse(TEXT("Actor destroyed after commit"), WeakActor.IsValid());
	R = Pickup(Player(2), TEXT("BagB"), FGuid::NewGuid());
	TestEqual(TEXT("Late claimant"), R.Result.Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("One gem"), F.S->CountItem(TEXT("Gem")), static_cast<int64>(1));

	// Unload is not pickup: the record survives an unbound component; reservations expire.
	TestTrue(TEXT("Second world item"), F.S->CreateWorldItem(TEXT("Gem"), 1, FTransform::Identity, WorldId).IsSuccess());
	AActor* Second = TestWorld.Spawn<AActor>();
	UDocWorldItemComponent* SecondComponent = Second ? NewObject<UDocWorldItemComponent>(Second) : nullptr;
	if (SecondComponent)
	{
		SecondComponent->WorldItemId = WorldId;
		F.S->BindWorldItemComponent(SecondComponent);
		F.S->UnbindWorldItemComponent(SecondComponent); // streaming unload
	}
	TestTrue(TEXT("Record survives unload"), F.S->GetWorldItem(WorldId, Record));
	TestTrue(TEXT("C reserves"), F.S->ReserveWorldItem(WorldId, FGuid::NewGuid(), 1.f).IsSuccess());
	TestEqual(TEXT("Reserved"), Pickup(Player(2), TEXT("BagB"), FGuid::NewGuid()).FailureReason, DocInventoryTags::Error_Inventory_Reserved.GetTag());
	F.S->AdvanceClock(1.5);
	TestTrue(TEXT("After expiry another claimant succeeds"), Pickup(Player(2), TEXT("BagB"), FGuid::NewGuid()).bSuccess);
	TestTrue(TEXT("Unloaded actor untouched"), IsValid(Second));
	TestEqual(TEXT("Two gems total"), F.S->CountItem(TEXT("Gem")), static_cast<int64>(2));
	TestTrue(TEXT("Membership unique"), UniqueMembership(*this, F.S));
	return true;
}

// INV-10
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryDropUseTest, "Doc.Inventory.DropAndUseFailure", DocInventoryTests::Flags)
bool FDocInventoryDropUseTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocItemDefinition* Potion = MakeItem(TEXT("Potion"), 10);
	Potion->UseActions = { TEXT("Drink"), TEXT("Brew") };
	F.S->RegisterDefinition(Potion);
	F.S->RegisterContainer(TEXT("Bag"), Player(1), ListRules());
	F.Grant(TEXT("Bag"), TEXT("Potion"), 5, Player(1));
	const FGuid Id = F.First(TEXT("Bag"), TEXT("Potion"), Player(1));

	FDocInventoryTransaction Drop = Tx(EDocInventoryOp::Drop, Player(1));
	Drop.SourceContainer = TEXT("Bag");
	Drop.InstanceId = Id;
	Drop.Quantity = 5;
	TestEqual(TEXT("No spawn provider"), F.S->SubmitTransaction(Drop).Result.Outcome, EDocResultOutcome::Unsupported);

	TSharedPtr<FTestSpawner> Spawner = MakeShared<FTestSpawner>();
	F.S->SetSpawnProvider(Spawner);
	Spawner->bCanSpawn = false;
	Drop.RequestId = FGuid::NewGuid();
	TestFalse(TEXT("CanSpawn refused before removal"), F.S->SubmitTransaction(Drop).bSuccess);
	TestEqual(TEXT("No spawn attempted"), Spawner->Spawns, 0);

	Spawner->bCanSpawn = true;
	Spawner->bSpawnSucceeds = false;
	Drop.RequestId = FGuid::NewGuid();
	FDocInventoryTransactionResult R = F.S->SubmitTransaction(Drop);
	TestFalse(TEXT("Spawn failure"), R.bSuccess);
	TestEqual(TEXT("Item retained in source"), F.S->FindInstanceContainer(Id), FName(TEXT("Bag")));
	TestEqual(TEXT("Quantity retained"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 5);
	TestEqual(TEXT("Conserved"), F.S->CountItem(TEXT("Potion")), static_cast<int64>(5));

	Spawner->bSpawnSucceeds = true;
	Drop.RequestId = FGuid::NewGuid();
	Drop.Quantity = 2;
	R = F.S->SubmitTransaction(Drop);
	TestTrue(TEXT("Drop"), R.bSuccess);
	FDocWorldItemRecord World;
	TestTrue(TEXT("World record"), F.S->GetWorldItem(R.WorldItemId, World) && World.Item.Quantity == 2);
	TestEqual(TEXT("Three left"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 3);
	TestEqual(TEXT("Conserved after drop"), F.S->CountItem(TEXT("Potion")), static_cast<int64>(5));

	TSharedPtr<FTestAction> Drink = MakeShared<FTestAction>();
	TSharedPtr<FTestAction> Brew = MakeShared<FTestAction>();
	F.S->RegisterActionHandler(TEXT("Drink"), Drink);
	F.S->RegisterActionHandler(TEXT("Brew"), Brew);
	TestEqual(TEXT("Unknown action"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Throw"), FGuid::NewGuid()).Outcome, EDocResultOutcome::Unsupported);

	Drink->State = FDocItemActionOutcome::EState::Failed;
	const FGuid FailedRequest = FGuid::NewGuid();
	TestFalse(TEXT("Action failure"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Drink"), FailedRequest).IsSuccess());
	Drink->State = FDocItemActionOutcome::EState::Completed;
	TestFalse(TEXT("Retried failed use keeps its failure"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Drink"), FailedRequest).IsSuccess());
	TestEqual(TEXT("Retried failed use never runs the action again"), Drink->Executes, 1);
	TestEqual(TEXT("Failure consumes nothing"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 3);
	Drink->State = FDocItemActionOutcome::EState::Completed;
	const FGuid DrinkRequest = FGuid::NewGuid();
	TestTrue(TEXT("Use"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Drink"), DrinkRequest).IsSuccess());
	TestEqual(TEXT("Consumed after success"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 2);
	F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Drink"), DrinkRequest);
	TestEqual(TEXT("Retried use never runs twice"), Drink->Executes, 2);
	TestEqual(TEXT("Retried use never consumes twice"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 2);

	// Pending action: the item is reserved; expiry releases it without consumption.
	Brew->State = FDocItemActionOutcome::EState::Pending;
	const FGuid BrewRequest = FGuid::NewGuid();
	TestEqual(TEXT("Pending"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Brew"), BrewRequest).Outcome, EDocResultOutcome::NotReady);
	FDocInventoryTransaction Relocate = Drop;
	Relocate.RequestId = FGuid::NewGuid();
	Relocate.Quantity = 1;
	TestEqual(TEXT("Reserved item cannot move"), F.S->SubmitTransaction(Relocate).FailureReason, DocInventoryTags::Error_Inventory_Reserved.GetTag());
	TestEqual(TEXT("Second use conflicts"), F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Drink"), FGuid::NewGuid()).Outcome, EDocResultOutcome::Conflict);
	F.S->AdvanceClock(GetDefault<UDocInventorySettings>()->UseReservationSeconds + 1.0);
	TestEqual(TEXT("Late completion finds no reservation"), F.S->CompleteUse(BrewRequest, 1, FDocSystemResult::MakeSuccess()).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Expiry consumed nothing"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 2);

	const FGuid Brew2 = FGuid::NewGuid();
	F.S->UseItem(Player(1), TEXT("Bag"), Id, TEXT("Brew"), Brew2);
	TestTrue(TEXT("Pending completion commits"), F.S->CompleteUse(Brew2, 1, FDocSystemResult::MakeSuccess()).IsSuccess());
	TestEqual(TEXT("Consumed once"), F.Count(TEXT("Bag"), TEXT("Potion"), Player(1)), 1);
	TestTrue(TEXT("Released: item movable again"), F.S->PreviewTransaction(Relocate).bSuccess);
	return true;
}

// INV-11
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryNetworkPrivacyTest, "Doc.Inventory.NetworkPrivacy", DocInventoryTests::Flags)
bool FDocInventoryNetworkPrivacyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Coin"), 999));
	F.S->RegisterContainer(TEXT("Purse1"), Player(1), ListRules());
	F.S->RegisterContainer(TEXT("Purse2"), Player(2), ListRules());
	F.S->RegisterContainer(TEXT("Chest"), Shared(), ListRules());
	F.Grant(TEXT("Purse1"), TEXT("Coin"), 10, Player(1));
	const FGuid Id = F.First(TEXT("Purse1"), TEXT("Coin"), Player(1));

	FDocContainerRecord Peek;
	TestEqual(TEXT("Private container hidden"), F.S->GetContainer(Player(2), TEXT("Purse1"), Peek).Outcome, EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Shared container visible"), F.S->GetContainer(Player(2), TEXT("Chest"), Peek).IsSuccess());
	TestEqual(TEXT("Hijacking another owner's id"), F.S->RegisterContainer(TEXT("Purse1"), Player(2), ListRules()).Outcome, EDocResultOutcome::Conflict);

	FDocInventoryTransaction Steal = Tx(EDocInventoryOp::Transfer, Player(2));
	Steal.SourceContainer = TEXT("Purse1");
	Steal.DestContainer = TEXT("Purse2");
	Steal.InstanceId = Id;
	Steal.Quantity = 10;
	TestEqual(TEXT("Unauthorized container"), F.S->SubmitTransaction(Steal).Result.Outcome, EDocResultOutcome::PermissionDenied);

	FDocInventoryTransaction Spoof = Tx(EDocInventoryOp::Add, Player(2));
	Spoof.DestContainer = TEXT("Purse2");
	Spoof.ItemId = TEXT("Coin");
	Spoof.Quantity = 9999;
	TestEqual(TEXT("Spoofed grant"), F.S->SubmitTransaction(Spoof).Result.Outcome, EDocResultOutcome::PermissionDenied);

	F.S->SetAuthorityOverrideForTesting(false);
	FDocInventoryTransaction ClientMove = Tx(EDocInventoryOp::Transfer, Player(1));
	ClientMove.SourceContainer = TEXT("Purse1");
	ClientMove.DestContainer = TEXT("Chest");
	ClientMove.InstanceId = Id;
	ClientMove.Quantity = 1;
	TestEqual(TEXT("Client cannot commit"), F.S->SubmitTransaction(ClientMove).Result.Outcome, EDocResultOutcome::PermissionDenied);
	TestEqual(TEXT("Client cannot use"), F.S->UseItem(Player(1), TEXT("Purse1"), Id, TEXT("Any"), FGuid::NewGuid()).Outcome, EDocResultOutcome::PermissionDenied);
	F.S->SetAuthorityOverrideForTesting(true);

	// Delta window: a view that falls behind the retained window must resnapshot.
	UDocInventorySettings* Settings = GetMutableDefault<UDocInventorySettings>();
	const int32 SavedMax = Settings->MaxDeltasPerContainer;
	Settings->MaxDeltasPerContainer = 2;
	const int64 Start = F.Get(TEXT("Chest"), Shared()).Revision;
	for (int32 i = 0; i < 4; ++i)
	{
		ClientMove.RequestId = FGuid::NewGuid();
		F.S->SubmitTransaction(ClientMove);
	}
	Settings->MaxDeltasPerContainer = SavedMax;
	const int64 Latest = F.Get(TEXT("Chest"), Shared()).Revision;
	TestEqual(TEXT("Four chest commits"), Latest, Start + 4);
	TArray<FDocInventoryDelta> Deltas;
	bool bResnapshot = false;
	F.S->GetContainerDeltas(Shared(), TEXT("Chest"), Latest - 1, Deltas, bResnapshot);
	TestTrue(TEXT("Incremental delta"), !bResnapshot && Deltas.Num() == 1 && Deltas[0].Revision == Latest);
	F.S->GetContainerDeltas(Shared(), TEXT("Chest"), Start, Deltas, bResnapshot);
	TestTrue(TEXT("Gap requires resnapshot"), bResnapshot);
	F.S->GetContainerDeltas(Shared(), TEXT("Chest"), 0, Deltas, bResnapshot);
	TestTrue(TEXT("Late join resnapshots"), bResnapshot);
	TestEqual(TEXT("Up to date"), F.S->GetContainerDeltas(Shared(), TEXT("Chest"), Latest, Deltas, bResnapshot).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Deltas are access-checked"), F.S->GetContainerDeltas(Player(2), TEXT("Purse1"), 0, Deltas, bResnapshot).Outcome, EDocResultOutcome::PermissionDenied);

	// Trimming never keeps part of a revision: three stacks per grant, window of four deltas.
	F.S->RegisterDefinition(MakeItem(TEXT("Pebble"), 1));
	F.S->RegisterContainer(TEXT("Tray"), Shared(), ListRules());
	Settings->MaxDeltasPerContainer = 4;
	const FDocInventoryTransactionResult FirstBatch = F.Grant(TEXT("Tray"), TEXT("Pebble"), 3, Shared());
	const int64 FirstRevision = F.Get(TEXT("Tray"), Shared()).Revision;
	const FDocInventoryTransactionResult SecondBatch = F.Grant(TEXT("Tray"), TEXT("Pebble"), 3, Shared());
	Settings->MaxDeltasPerContainer = SavedMax;
	const int64 SecondRevision = F.Get(TEXT("Tray"), Shared()).Revision;
	TestTrue(TEXT("Both batches committed"), FirstBatch.bSuccess && SecondBatch.bSuccess && SecondRevision == FirstRevision + 1);
	TestEqual(TEXT("Each batch is three stacks"), F.Get(TEXT("Tray"), Shared()).Items.Num(), 6);
	F.S->GetContainerDeltas(Shared(), TEXT("Tray"), FirstRevision - 1, Deltas, bResnapshot);
	TestTrue(TEXT("Partly trimmed revision forces a resnapshot"), bResnapshot);
	TestEqual(TEXT("No partial revision is served"), Deltas.Num(), 0);
	F.S->GetContainerDeltas(Shared(), TEXT("Tray"), FirstRevision, Deltas, bResnapshot);
	TestFalse(TEXT("Whole later revision needs no resnapshot"), bResnapshot);
	TestEqual(TEXT("All of the later revision is served"), Deltas.Num(), 3);
	bool bAllLatest = Deltas.Num() > 0;
	for (const FDocInventoryDelta& D : Deltas) { bAllLatest &= D.Revision == SecondRevision; }
	TestTrue(TEXT("Served deltas all belong to the later revision"), bAllLatest);
	TestEqual(TEXT("Coins conserved"), F.S->CountItem(TEXT("Coin")), static_cast<int64>(10));
	return true;
}

// INV-12 (base part): random sequences preserve every invariant.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInventoryInvariantsTest, "Doc.Inventory.RandomInvariants", DocInventoryTests::Flags)
bool FDocInventoryInvariantsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeItem(TEXT("Arrow"), 20, 1));
	F.S->RegisterDefinition(MakeItem(TEXT("Bread"), 5, 2, TAG_Food));
	F.S->RegisterDefinition(MakeItem(TEXT("Axe"), 1, 10, TAG_Weapon));
	const TArray<FName> Ids = { TEXT("C0"), TEXT("C1"), TEXT("C2") };
	FDocContainerRules Pantry = ListRules(6);
	Pantry.AllowedTags.AddTag(TAG_Food);
	F.S->RegisterContainer(Ids[0], Shared(), ListRules(8, 120));
	F.S->RegisterContainer(Ids[1], Shared(), ListRules(4));
	F.S->RegisterContainer(Ids[2], Shared(), Pantry);
	F.Grant(Ids[0], TEXT("Arrow"), 45, Shared());
	F.Grant(Ids[0], TEXT("Bread"), 7, Shared());
	F.Grant(Ids[1], TEXT("Axe"), 1, Shared());
	F.Grant(Ids[1], TEXT("Axe"), 1, Shared());
	const int64 Arrows = F.S->CountItem(TEXT("Arrow"));
	const int64 Bread = F.S->CountItem(TEXT("Bread"));
	const int64 Axes = F.S->CountItem(TEXT("Axe"));

	FRandomStream Random(20260927);
	int32 Committed = 0;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		const FName From = Ids[Random.RandRange(0, 2)];
		const FDocContainerRecord Source = F.Get(From, Shared());
		if (Source.Items.IsEmpty()) { continue; }
		const FDocItemInstance& Pick = Source.Items[Random.RandRange(0, Source.Items.Num() - 1)];
		FDocInventoryTransaction T = Tx(EDocInventoryOp::Transfer, Shared());
		T.SourceContainer = From;
		T.InstanceId = Pick.InstanceId;
		switch (Random.RandRange(0, 3))
		{
		case 0:
			T.DestContainer = Ids[Random.RandRange(0, 2)];
			T.Quantity = Random.RandRange(1, Pick.Quantity);
			T.bAllowPartial = Random.RandRange(0, 1) == 1;
			break;
		case 1:
			T.Op = EDocInventoryOp::Split;
			T.Quantity = FMath::Max(1, Pick.Quantity / 2);
			break;
		case 2:
		{
			T.Op = EDocInventoryOp::Merge;
			const FDocItemInstance& Other = Source.Items[Random.RandRange(0, Source.Items.Num() - 1)];
			T.OtherInstanceId = Other.InstanceId;
			T.bAllowPartial = true;
			break;
		}
		default:
		{
			T.Op = EDocInventoryOp::Swap;
			T.DestContainer = Ids[Random.RandRange(0, 2)];
			const FDocContainerRecord Dest = F.Get(T.DestContainer, Shared());
			if (Dest.Items.IsEmpty()) { continue; }
			T.OtherInstanceId = Dest.Items[Random.RandRange(0, Dest.Items.Num() - 1)].InstanceId;
			break;
		}
		}
		if (F.S->SubmitTransaction(T).bSuccess) { ++Committed; }
		if (F.S->CountItem(TEXT("Arrow")) != Arrows || F.S->CountItem(TEXT("Bread")) != Bread || F.S->CountItem(TEXT("Axe")) != Axes)
		{
			AddError(FString::Printf(TEXT("Conservation broke at step %d"), Step));
			break;
		}
		if (!UniqueMembership(*this, F.S)) { break; }
	}
	for (const FName Id : Ids)
	{
		const FDocContainerRecord C = F.Get(Id, Shared());
		TestTrue(TEXT("Stack limits"), !C.Items.ContainsByPredicate([&F](const FDocItemInstance& I) { return I.Quantity > F.S->FindDefinition(I.ItemId)->MaxStackSize; }));
		TestTrue(TEXT("Slot limits"), C.Rules.MaxSlots == 0 || C.Items.Num() <= C.Rules.MaxSlots);
	}
	TestTrue(TEXT("Some transactions committed"), Committed > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
