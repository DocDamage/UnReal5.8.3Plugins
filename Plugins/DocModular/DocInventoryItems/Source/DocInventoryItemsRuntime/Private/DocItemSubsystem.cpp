#include "DocItemSubsystem.h"
#include "DocInventoryItemsLog.h"
#include "DocEffectKey.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DateTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocItemSubsystem)

namespace DocInventoryTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equip, "Equip", "Logical equipment slots");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Head, "Equip.Head");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Body, "Equip.Body");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Hand, "Equip.Hand");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Primary, "Equip.Primary");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Secondary, "Equip.Secondary");
	UE_DEFINE_GAMEPLAY_TAG(Equip_Custom, "Equip.Custom");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Inventory, "Doc.Error.Inventory", "DocInventoryItems errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_UnknownDefinition, "Doc.Error.Inventory.UnknownDefinition");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_UnknownContainer, "Doc.Error.Inventory.UnknownContainer");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_UnknownInstance, "Doc.Error.Inventory.UnknownInstance");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Quantity, "Doc.Error.Inventory.Quantity");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Capacity, "Doc.Error.Inventory.Capacity");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Weight, "Doc.Error.Inventory.Weight");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Stack, "Doc.Error.Inventory.Stack");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_TagRule, "Doc.Error.Inventory.TagRule");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Unique, "Doc.Error.Inventory.Unique");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Slot, "Doc.Error.Inventory.Slot");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Access, "Doc.Error.Inventory.Access");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_StaleRevision, "Doc.Error.Inventory.StaleRevision");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Unsupported, "Doc.Error.Inventory.Unsupported");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Reserved, "Doc.Error.Inventory.Reserved");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_Conservation, "Doc.Error.Inventory.Conservation");
	UE_DEFINE_GAMEPLAY_TAG(Error_Inventory_ReceiptConflict, "Doc.Error.Inventory.ReceiptConflict");
}

static TWeakObjectPtr<UDocItemSubsystem> GDocItemTestOverride;

namespace DocInventoryPrivate
{
	bool IsQuantityOp(EDocInventoryOp Op)
	{
		switch (Op)
		{
		case EDocInventoryOp::Add: case EDocInventoryOp::Remove: case EDocInventoryOp::Transfer:
		case EDocInventoryOp::Split: case EDocInventoryOp::Consume: case EDocInventoryOp::Drop:
			return true;
		default:
			return false;
		}
	}

	bool IsPartialFailure(const FGameplayTag& Tag)
	{
		return Tag == DocInventoryTags::Error_Inventory_Capacity || Tag == DocInventoryTags::Error_Inventory_Weight
			|| Tag == DocInventoryTags::Error_Inventory_Slot || Tag == DocInventoryTags::Error_Inventory_Stack;
	}

	bool CheckedMul(int64 A, int64 B, int64& Out)
	{
		if (A < 0 || B < 0) { return false; }
		if (A != 0 && B > MAX_int64 / A) { return false; }
		Out = A * B;
		return true;
	}

	void SumByItem(const FDocContainerRecord& C, TMap<FName, int64>& Out)
	{
		for (const FDocItemInstance& I : C.Items) { Out.FindOrAdd(I.ItemId) += I.Quantity; }
	}
}

using namespace DocInventoryPrivate;

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocItemSubsystem* UDocItemSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocItemSubsystem* Override = GDocItemTestOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDocItemSubsystem>() : nullptr;
}

void UDocItemSubsystem::SetSubsystemOverrideForTesting(UDocItemSubsystem* Override)
{
	GDocItemTestOverride = Override;
}

void UDocItemSubsystem::Deinitialize()
{
	WorldComponents.Reset(); // never keeps old worlds alive
	PendingEvents.Reset();
	Super::Deinitialize();
}

bool UDocItemSubsystem::HasAuthority() const
{
	if (AuthorityOverride.IsSet())
	{
		return AuthorityOverride.GetValue();
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return !World || World->GetNetMode() != NM_Client;
}

const UDocInventorySettings* UDocItemSubsystem::Settings() const
{
	return GetDefault<UDocInventorySettings>();
}

bool UDocItemSubsystem::CanAccess(const FDocOwnerScope& Requester, const FDocContainerRecord& Container) const
{
	// Private containers are addressable only by their owner scope; shared/party containers by any trusted requester.
	return Container.Owner.Kind != EDocOwnerScopeKind::PlayerProfile || Container.Owner == Requester;
}

// ---------------------------------------------------------------------------
// Catalog and containers
// ---------------------------------------------------------------------------

FDocSystemResult UDocItemSubsystem::RegisterDefinition(UDocItemDefinition* Definition)
{
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No definition"));
	}
	TArray<FString> Errors;
	Definition->FindProblems(Errors);
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(Errors, TEXT("; ")));
	}
	if (const TObjectPtr<UDocItemDefinition>* Existing = Definitions.Find(Definition->ItemId))
	{
		return *Existing == Definition ? FDocSystemResult::MakeNoChange(TEXT("Already registered"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Duplicate ItemId %s"), *Definition->ItemId.ToString()));
	}
	Definitions.Add(Definition->ItemId, Definition);
	DefinitionRefs.Add(Definition);
	return FDocSystemResult::MakeSuccess();
}

const UDocItemDefinition* UDocItemSubsystem::FindDefinition(FName ItemId) const
{
	const TObjectPtr<UDocItemDefinition>* Found = Definitions.Find(ItemId);
	return Found ? Found->Get() : nullptr;
}

void UDocItemSubsystem::RegisterActionHandler(FName ActionId, TSharedPtr<IDocItemActionHandler> Handler)
{
	if (Handler.IsValid()) { ActionHandlers.Add(ActionId, Handler); } else { ActionHandlers.Remove(ActionId); }
}

FDocSystemResult UDocItemSubsystem::RegisterContainer(FName ContainerId, const FDocOwnerScope& Owner, const FDocContainerRules& Rules)
{
	if (ContainerId.IsNone() || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Container id and owner are required"));
	}
	if (const FDocContainerRecord* Existing = Containers.Find(ContainerId))
	{
		if (Existing->Owner != Owner)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Container already exists for another owner"), DocInventoryTags::Error_Inventory_Access);
		}
		return FDocSystemResult::MakeNoChange(TEXT("Container exists; membership kept"));
	}
	FDocContainerRecord& C = Containers.Add(ContainerId);
	C.ContainerId = ContainerId;
	C.Owner = Owner;
	C.Rules = Rules;
	C.Revision = 1;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocItemSubsystem::GetContainer(const FDocOwnerScope& Viewer, FName ContainerId, FDocContainerRecord& OutRecord) const
{
	const FDocContainerRecord* C = Containers.Find(ContainerId);
	if (!C)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown container"), DocInventoryTags::Error_Inventory_UnknownContainer);
	}
	if (!CanAccess(Viewer, *C))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Private container"), DocInventoryTags::Error_Inventory_Access);
	}
	OutRecord = *C;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocItemSubsystem::GetContainerDeltas(const FDocOwnerScope& Viewer, FName ContainerId, int64 SinceRevision, TArray<FDocInventoryDelta>& OutDeltas, bool& bOutNeedsResnapshot) const
{
	OutDeltas.Reset();
	bOutNeedsResnapshot = false;
	FDocContainerRecord Record;
	const FDocSystemResult Access = GetContainer(Viewer, ContainerId, Record);
	if (!Access.IsSuccess())
	{
		return Access;
	}
	if (SinceRevision >= Record.Revision)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Up to date"));
	}
	const TArray<FDocInventoryDelta>* Log = DeltaLog.Find(ContainerId);
	const int64 Oldest = (Log && Log->Num() > 0) ? (*Log)[0].Revision : Record.Revision + 1;
	if (SinceRevision + 1 < Oldest)
	{
		bOutNeedsResnapshot = true; // gap: the view must take a full snapshot
		return FDocSystemResult::MakeSuccess();
	}
	if (Log)
	{
		for (const FDocInventoryDelta& D : *Log)
		{
			if (D.Revision > SinceRevision) { OutDeltas.Add(D); }
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FName UDocItemSubsystem::FindInstanceContainer(const FGuid& InstanceId) const
{
	const FName* Found = InstanceIndex.Find(InstanceId);
	return Found ? *Found : NAME_None;
}

void UDocItemSubsystem::RebuildIndex()
{
	InstanceIndex.Reset();
	for (const TPair<FName, FDocContainerRecord>& Pair : Containers)
	{
		for (const FDocItemInstance& I : Pair.Value.Items) { InstanceIndex.Add(I.InstanceId, Pair.Key); }
	}
	for (const TPair<FGuid, FDocWorldItemRecord>& Pair : WorldItems)
	{
		InstanceIndex.Add(Pair.Value.Item.InstanceId, FName(*FString::Printf(TEXT("World:%s"), *Pair.Key.ToString())));
	}
}

int64 UDocItemSubsystem::CountItem(FName ItemId) const
{
	int64 Total = 0;
	for (const TPair<FName, FDocContainerRecord>& Pair : Containers)
	{
		for (const FDocItemInstance& I : Pair.Value.Items) { if (I.ItemId == ItemId) { Total += I.Quantity; } }
	}
	for (const TPair<FGuid, FDocWorldItemRecord>& Pair : WorldItems)
	{
		if (Pair.Value.Item.ItemId == ItemId) { Total += Pair.Value.Item.Quantity; }
	}
	for (const FDocItemInstance& I : Quarantined) { if (I.ItemId == ItemId) { Total += I.Quantity; } }
	return Total;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool UDocItemSubsystem::StacksCompatible(const FDocItemInstance& A, const FDocItemInstance& B, const UDocItemDefinition& Def) const
{
	// Display names never decide compatibility: only the configured stack key.
	if (A.ItemId != B.ItemId || A.DefinitionVersion != B.DefinitionVersion) { return false; }
	const FDocItemStackPolicy& P = Def.StackPolicy;
	if (P.bRequireEqualDurability && A.Durability != B.Durability) { return false; }
	if (P.bRequireEqualRuntimeTags && !(A.RuntimeTags == B.RuntimeTags)) { return false; }
	if (P.bRequireEqualBinding && A.BoundTo != B.BoundTo) { return false; }
	auto Value = [](const FDocItemInstance& I, FName Key) -> const FDocItemDataEntry*
	{
		return I.CustomData.FindByPredicate([Key](const FDocItemDataEntry& E) { return E.Key == Key; });
	};
	if (P.CustomDataKeys.IsEmpty())
	{
		if (A.CustomData.Num() != B.CustomData.Num()) { return false; }
		for (const FDocItemDataEntry& E : A.CustomData)
		{
			const FDocItemDataEntry* Other = Value(B, E.Key);
			if (!Other || !(*Other == E)) { return false; }
		}
		return true;
	}
	for (FName Key : P.CustomDataKeys)
	{
		const FDocItemDataEntry* VA = Value(A, Key);
		const FDocItemDataEntry* VB = Value(B, Key);
		if ((VA == nullptr) != (VB == nullptr) || (VA && !(*VA == *VB))) { return false; }
	}
	return true;
}

bool UDocItemSubsystem::AssignSlots(const FDocContainerRecord& C, const UDocItemDefinition& Def, FName Requested, TArray<FName>& OutSlots) const
{
	OutSlots.Reset();
	TSet<FName> Occupied;
	for (const FDocItemInstance& I : C.Items) { Occupied.Append(I.Slots); }
	TArray<FName> Candidates;
	for (const FDocInventorySlot& S : C.Rules.SlotTypes)
	{
		const bool bAccepts = C.Rules.Mode == EDocContainerMode::Equipment
			? (S.EquipTag.IsValid() && Def.EquipTags.HasTagExact(S.EquipTag))
			: (S.AcceptTags.IsEmpty() || Def.ItemTags.HasAny(S.AcceptTags));
		if (bAccepts) { Candidates.Add(S.SlotId); }
	}
	if (C.Rules.Mode == EDocContainerMode::Equipment && Def.bOccupiesAllEquipSlots)
	{
		// Multi-slot occupancy is atomic: every required slot or none.
		for (FName Slot : Candidates) { if (Occupied.Contains(Slot)) { return false; } }
		OutSlots = Candidates;
		return !OutSlots.IsEmpty();
	}
	if (!Requested.IsNone())
	{
		if (!Candidates.Contains(Requested) || Occupied.Contains(Requested)) { return false; }
		OutSlots.Add(Requested);
		return true;
	}
	for (FName Slot : Candidates)
	{
		if (!Occupied.Contains(Slot)) { OutSlots.Add(Slot); return true; }
	}
	return false;
}

bool UDocItemSubsystem::ValidateContainer(const FDocContainerRecord& C, FFailure& Fail) const
{
	auto Bad = [&Fail, &C](const FGameplayTag& Tag, const FString& Why, EDocResultOutcome Outcome = EDocResultOutcome::Conflict)
	{
		Fail.Tag = Tag;
		Fail.Outcome = Outcome;
		Fail.Diagnostic = FString::Printf(TEXT("%s: %s"), *C.ContainerId.ToString(), *Why);
		return false;
	};
	const FDocContainerRules& R = C.Rules;
	TSet<FName> SlotsUsed;
	TSet<FName> Unique;
	int64 Weight = 0;
	for (const FDocItemInstance& I : C.Items)
	{
		const UDocItemDefinition* Def = FindDefinition(I.ItemId);
		if (!Def) { return Bad(DocInventoryTags::Error_Inventory_UnknownDefinition, FString::Printf(TEXT("unknown item %s"), *I.ItemId.ToString()), EDocResultOutcome::NotFound); }
		const int32 MaxStack = R.bStacking ? Def->MaxStackSize : 1;
		if (I.Quantity < 1 || I.Quantity > MaxStack) { return Bad(DocInventoryTags::Error_Inventory_Stack, FString::Printf(TEXT("%s x%d exceeds stack limit %d"), *I.ItemId.ToString(), I.Quantity, MaxStack)); }
		if (Def->bIsContainer) { return Bad(DocInventoryTags::Error_Inventory_Unsupported, TEXT("nested containers are not supported"), EDocResultOutcome::Unsupported); }
		if (!R.AllowedTags.IsEmpty() && !Def->ItemTags.HasAny(R.AllowedTags)) { return Bad(DocInventoryTags::Error_Inventory_TagRule, FString::Printf(TEXT("%s is not allowed"), *I.ItemId.ToString())); }
		if (Def->ItemTags.HasAny(R.BlockedTags)) { return Bad(DocInventoryTags::Error_Inventory_TagRule, FString::Printf(TEXT("%s is blocked"), *I.ItemId.ToString())); }
		if (R.bUniqueItems)
		{
			bool bDup = false;
			Unique.Add(I.ItemId, &bDup);
			if (bDup) { return Bad(DocInventoryTags::Error_Inventory_Unique, FString::Printf(TEXT("%s is unique"), *I.ItemId.ToString())); }
		}
		if (Def->MaxDurability > 0 && (I.Durability < 0 || I.Durability > Def->MaxDurability)) { return Bad(DocInventoryTags::Error_Inventory_Stack, TEXT("durability out of range")); }
		if (I.CustomData.Num() > Settings()->MaxCustomDataEntries) { return Bad(DocInventoryTags::Error_Inventory_Stack, TEXT("too much custom data")); }
		if (R.UsesSlots())
		{
			if (I.Slots.IsEmpty()) { return Bad(DocInventoryTags::Error_Inventory_Slot, TEXT("item without a slot")); }
			for (FName SlotId : I.Slots)
			{
				const FDocInventorySlot* Slot = R.FindSlot(SlotId);
				if (!Slot) { return Bad(DocInventoryTags::Error_Inventory_Slot, FString::Printf(TEXT("unknown slot %s"), *SlotId.ToString())); }
				bool bTaken = false;
				SlotsUsed.Add(SlotId, &bTaken);
				if (bTaken) { return Bad(DocInventoryTags::Error_Inventory_Slot, FString::Printf(TEXT("slot %s occupied twice"), *SlotId.ToString())); }
				if (R.Mode == EDocContainerMode::Equipment ? !(Slot->EquipTag.IsValid() && Def->EquipTags.HasTagExact(Slot->EquipTag))
					: (!Slot->AcceptTags.IsEmpty() && !Def->ItemTags.HasAny(Slot->AcceptTags)))
				{
					return Bad(DocInventoryTags::Error_Inventory_Slot, FString::Printf(TEXT("%s does not fit slot %s"), *I.ItemId.ToString(), *SlotId.ToString()));
				}
			}
		}
		int64 ItemWeight = 0;
		if (!CheckedMul(Def->WeightUnits, I.Quantity, ItemWeight) || ItemWeight > MAX_int64 - Weight)
		{
			return Bad(DocInventoryTags::Error_Inventory_Weight, TEXT("weight overflow"));
		}
		Weight += ItemWeight;
	}
	if (!R.UsesSlots() && R.MaxSlots > 0 && C.Items.Num() > R.MaxSlots) { return Bad(DocInventoryTags::Error_Inventory_Capacity, FString::Printf(TEXT("%d stacks exceed %d slots"), C.Items.Num(), R.MaxSlots)); }
	if (R.MaxWeightUnits > 0 && Weight > R.MaxWeightUnits) { return Bad(DocInventoryTags::Error_Inventory_Weight, FString::Printf(TEXT("weight %lld exceeds %lld"), Weight, R.MaxWeightUnits)); }
	return true;
}

bool UDocItemSubsystem::PlaceItem(FDocContainerRecord& C, FDocItemInstance Item, FName DestSlot, bool bAllowMerge, FProposal& P, FFailure& Fail) const
{
	const UDocItemDefinition* Def = FindDefinition(Item.ItemId);
	if (!Def)
	{
		Fail = { DocInventoryTags::Error_Inventory_UnknownDefinition, FString::Printf(TEXT("Unknown item %s"), *Item.ItemId.ToString()), EDocResultOutcome::NotFound };
		return false;
	}
	int32 Remaining = Item.Quantity;
	const int32 MaxStack = C.Rules.bStacking ? Def->MaxStackSize : 1;
	if (bAllowMerge && MaxStack > 1)
	{
		for (FDocItemInstance& Existing : C.Items)
		{
			if (Remaining == 0) { break; }
			if (Existing.Quantity < MaxStack && StacksCompatible(Existing, Item, *Def) && (DestSlot.IsNone() || Existing.Slots.Contains(DestSlot)))
			{
				const int32 Add = FMath::Min(Remaining, MaxStack - Existing.Quantity);
				Existing.Quantity += Add;
				Remaining -= Add;
			}
		}
	}
	if (Remaining == 0)
	{
		P.Retired.Add(Item.InstanceId); // fully absorbed: the consumed id is tombstoned
		return true;
	}
	bool bIdUsed = false;
	while (Remaining > 0)
	{
		FDocItemInstance New = Item;
		New.Quantity = FMath::Min(Remaining, MaxStack);
		if (bIdUsed) { New.InstanceId = FGuid::NewGuid(); }
		bIdUsed = true;
		New.Slots.Reset();
		if (C.Rules.UsesSlots() && !AssignSlots(C, *Def, DestSlot, New.Slots))
		{
			Fail = { DocInventoryTags::Error_Inventory_Slot, FString::Printf(TEXT("%s: no free slot for %s"), *C.ContainerId.ToString(), *Item.ItemId.ToString()), EDocResultOutcome::Conflict };
			return false;
		}
		DestSlot = NAME_None; // an explicit slot applies to the first stack only
		C.Items.Add(New);
		Remaining -= New.Quantity;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Proposal (pure: works on copies)
// ---------------------------------------------------------------------------

bool UDocItemSubsystem::BuildProposal(const FDocInventoryTransaction& T, int32 Q, FProposal& P, FFailure& Fail) const
{
	auto Failed = [&Fail](EDocResultOutcome O, const FGameplayTag& Tag, const FString& D) { Fail.Outcome = O; Fail.Tag = Tag; Fail.Diagnostic = D; return false; };
	for (FName Id : { T.SourceContainer, T.DestContainer })
	{
		if (!Id.IsNone() && !P.Containers.Contains(Id))
		{
			if (const FDocContainerRecord* C = Containers.Find(Id)) { P.Containers.Add(Id, *C); }
		}
	}
	FDocContainerRecord* S = P.Containers.Find(T.SourceContainer);
	FDocContainerRecord* D = P.Containers.Find(T.DestContainer);
	auto TakeInstance = [&](FDocContainerRecord& From, const FGuid& Id, int32 Quantity, FDocItemInstance& Out) -> bool
	{
		const int32 Index = From.Items.IndexOfByPredicate([&Id](const FDocItemInstance& I) { return I.InstanceId == Id; });
		if (Index == INDEX_NONE) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Instance is not in the source container")); }
		FDocItemInstance& I = From.Items[Index];
		if (Quantity > I.Quantity) { return Failed(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Quantity, FString::Printf(TEXT("Requested %d, only %d available"), Quantity, I.Quantity)); }
		Out = I;
		Out.Slots.Reset();
		if (Quantity == I.Quantity)
		{
			From.Items.RemoveAt(Index); // whole instance moves with its id
		}
		else
		{
			I.Quantity -= Quantity;      // decremented exactly once
			Out.InstanceId = FGuid::NewGuid(); // the split creates exactly one new id
			Out.Quantity = Quantity;
		}
		return true;
	};

	switch (T.Op)
	{
	case EDocInventoryOp::Add:
	{
		const UDocItemDefinition* Def = FindDefinition(T.ItemId);
		if (!Def) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownDefinition, FString::Printf(TEXT("Unknown item %s"), *T.ItemId.ToString())); }
		if (T.InitialCustomData.Num() > Settings()->MaxCustomDataEntries) { return Failed(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Stack, TEXT("Too much custom data")); }
		FDocItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.ItemId = T.ItemId;
		Item.DefinitionVersion = Def->ContentVersion;
		Item.Quantity = Q;
		Item.Durability = T.InitialDurability >= 0 ? T.InitialDurability : (Def->MaxDurability > 0 ? Def->MaxDurability : -1);
		Item.CustomData = T.InitialCustomData;
		Item.CreationTimeUtcTicks = FDateTime::UtcNow().GetTicks();
		if (!PlaceItem(*D, Item, T.DestSlot, true, P, Fail)) { return false; }
		P.Moved = Q;
		break;
	}
	case EDocInventoryOp::Remove:
	case EDocInventoryOp::Consume:
	{
		FDocItemInstance Taken;
		if (!TakeInstance(*S, T.InstanceId, Q, Taken)) { return false; }
		if (Taken.InstanceId == T.InstanceId) { P.Retired.Add(T.InstanceId); }
		P.Moved = Q;
		break;
	}
	case EDocInventoryOp::Transfer:
	case EDocInventoryOp::Unequip:
	{
		const FDocItemInstance* Original = S->Find(T.InstanceId);
		const int32 Quantity = T.Op == EDocInventoryOp::Unequip ? (Original ? Original->Quantity : 0) : Q;
		FDocItemInstance Moving;
		if (!TakeInstance(*S, T.InstanceId, Quantity, Moving)) { return false; }
		if (!PlaceItem(*D, Moving, T.DestSlot, true, P, Fail)) { return false; }
		P.Moved = Quantity;
		break;
	}
	case EDocInventoryOp::Split:
	{
		const FDocItemInstance* I = S->Find(T.InstanceId);
		if (!I) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Unknown instance")); }
		if (Q >= I->Quantity) { return Failed(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Quantity, TEXT("A split must leave at least one in the original")); }
		FDocItemInstance Part;
		if (!TakeInstance(*S, T.InstanceId, Q, Part)) { return false; }
		if (!PlaceItem(*S, Part, T.DestSlot, false, P, Fail)) { return false; }
		P.Moved = Q;
		break;
	}
	case EDocInventoryOp::Merge:
	{
		if (T.InstanceId == T.OtherInstanceId) { return Failed(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Stack, TEXT("Cannot merge an instance into itself")); }
		FDocItemInstance* From = S->Find(T.InstanceId);
		FDocItemInstance* Into = S->Find(T.OtherInstanceId);
		if (!From || !Into) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Both instances must be in the container")); }
		const UDocItemDefinition* Def = FindDefinition(Into->ItemId);
		if (!Def || !StacksCompatible(*From, *Into, *Def)) { return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Stack, TEXT("Stack keys differ")); }
		const int32 MaxStack = S->Rules.bStacking ? Def->MaxStackSize : 1;
		const int32 Amount = FMath::Min(From->Quantity, MaxStack - Into->Quantity);
		if (Amount <= 0 || (Amount < From->Quantity && !T.bAllowPartial))
		{
			return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Stack, TEXT("The survivor stack has no room for the whole source"));
		}
		Into->Quantity += Amount;
		From->Quantity -= Amount;
		if (From->Quantity == 0)
		{
			const FGuid Consumed = From->InstanceId;
			S->Items.RemoveAll([&Consumed](const FDocItemInstance& I) { return I.InstanceId == Consumed; });
			P.Retired.Add(Consumed); // survivor keeps its id; the consumed id is tombstoned
		}
		P.Moved = Amount;
		break;
	}
	case EDocInventoryOp::Move:
	{
		FDocItemInstance* I = S->Find(T.InstanceId);
		if (!I) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Unknown instance")); }
		if (!S->Rules.UsesSlots()) { return Failed(EDocResultOutcome::Unsupported, DocInventoryTags::Error_Inventory_Unsupported, TEXT("List containers have no slots")); }
		const UDocItemDefinition* Def = FindDefinition(I->ItemId);
		const TArray<FName> OldSlots = I->Slots;
		I->Slots.Reset(); // free its own slot for the check
		TArray<FName> NewSlots;
		if (!Def || !AssignSlots(*S, *Def, T.DestSlot, NewSlots)) { return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Slot, TEXT("Destination slot unavailable (use Swap)")); }
		S->Find(T.InstanceId)->Slots = NewSlots;
		P.Moved = S->Find(T.InstanceId)->Quantity;
		break;
	}
	case EDocInventoryOp::Swap:
	{
		FDocContainerRecord& Other = D ? *D : *S;
		FDocItemInstance* A = S->Find(T.InstanceId);
		FDocItemInstance* B = Other.Find(T.OtherInstanceId);
		if (!A || !B || A == B) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Swap needs two distinct instances")); }
		if (&Other == S)
		{
			Swap(A->Slots, B->Slots);
		}
		else
		{
			FDocItemInstance CopyA = *A;
			FDocItemInstance CopyB = *B;
			Swap(CopyA.Slots, CopyB.Slots);
			const FGuid IdA = CopyA.InstanceId;
			const FGuid IdB = CopyB.InstanceId;
			S->Items.RemoveAll([&IdA](const FDocItemInstance& I) { return I.InstanceId == IdA; });
			Other.Items.RemoveAll([&IdB](const FDocItemInstance& I) { return I.InstanceId == IdB; });
			if (!S->Rules.UsesSlots()) { CopyB.Slots.Reset(); }
			if (!Other.Rules.UsesSlots()) { CopyA.Slots.Reset(); }
			S->Items.Add(CopyB);
			Other.Items.Add(CopyA);
		}
		P.Moved = 2;
		break;
	}
	case EDocInventoryOp::Equip:
	{
		if (!D || D->Rules.Mode != EDocContainerMode::Equipment) { return Failed(EDocResultOutcome::Unsupported, DocInventoryTags::Error_Inventory_Unsupported, TEXT("Destination is not an equipment container")); }
		const FDocItemInstance* I = S->Find(T.InstanceId);
		const UDocItemDefinition* Def = I ? FindDefinition(I->ItemId) : nullptr;
		if (!I || !Def) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("Unknown instance")); }
		if (Def->EquipTags.IsEmpty()) { return Failed(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Slot, TEXT("Item is not equippable")); }
		FDocItemInstance Moving;
		if (!TakeInstance(*S, T.InstanceId, 1, Moving)) { return false; }
		if (!AssignSlots(*D, *Def, T.DestSlot, Moving.Slots)) { return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Slot, TEXT("Required equipment slots are not free")); }
		D->Items.Add(Moving);
		P.Moved = 1;
		break;
	}
	case EDocInventoryOp::Pickup:
	{
		const FDocWorldItemRecord* World = WorldItems.Find(T.WorldItemId);
		if (!World) { return Failed(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownInstance, TEXT("World item no longer exists")); }
		if (T.ExpectedSourceRevision >= 0 && T.ExpectedSourceRevision != World->Revision) { return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_StaleRevision, TEXT("World item changed")); }
		if (World->ReservedBy.IsValid() && World->ReservedBy != T.RequestId && World->ReservationRemaining > 0.0)
		{
			return Failed(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Reserved, TEXT("Another claimant holds the pickup reservation"));
		}
		if (!PlaceItem(*D, World->Item, T.DestSlot, true, P, Fail)) { return false; }
		P.RemovedWorldItem = T.WorldItemId;
		P.Moved = World->Item.Quantity;
		break;
	}
	case EDocInventoryOp::Drop:
	{
		if (!SpawnProvider.IsValid()) { return Failed(EDocResultOutcome::Unsupported, DocInventoryTags::Error_Inventory_Unsupported, TEXT("No approved spawn provider")); }
		FDocItemInstance Moving;
		if (!TakeInstance(*S, T.InstanceId, Q, Moving)) { return false; }
		FDocWorldItemRecord Record;
		Record.WorldItemId = FGuid::NewGuid();
		Record.Item = Moving;
		Record.Transform = T.DropTransform;
		Record.Revision = 1;
		const FDocSystemResult Can = SpawnProvider->CanSpawn(Record);
		if (!Can.IsSuccess()) { return Failed(Can.Outcome, DocInventoryTags::Error_Inventory_Unsupported, FString::Printf(TEXT("Spawn refused: %s"), *Can.Diagnostic)); }
		P.NewWorldItem = Record;
		P.Moved = Q;
		break;
	}
	}

	for (const TPair<FName, FDocContainerRecord>& Pair : P.Containers)
	{
		if (!ValidateContainer(Pair.Value, Fail)) { return false; }
	}
	return true;
}

int64 UDocItemSubsystem::HashTransaction(const FDocInventoryTransaction& T)
{
	FString Data;
	for (const FDocItemDataEntry& E : T.InitialCustomData) { Data += FString::Printf(TEXT("%s=%lld/%s;"), *E.Key.ToString(), E.IntValue, *E.StringValue); }
	const FString Canonical = FString::Printf(TEXT("%d|%s|%s|%s|%s|%s|%s|%s|%d|%s|%lld|%lld|%d|%d|%s|%d|%s|%s"),
		static_cast<int32>(T.Op), *T.Requester.ToString(), *T.SourceContainer.ToString(), *T.DestContainer.ToString(),
		*T.InstanceId.ToString(), *T.OtherInstanceId.ToString(), *T.WorldItemId.ToString(), *T.ItemId.ToString(), T.Quantity,
		*T.DestSlot.ToString(), T.ExpectedSourceRevision, T.ExpectedDestRevision, T.bAllowPartial ? 1 : 0, T.bAuthorizedSourceOrSink ? 1 : 0,
		*T.Reason, T.InitialDurability, *Data, *T.DropTransform.ToString());
	return FDocReceiptLedger::HashString(Canonical);
}

// ---------------------------------------------------------------------------
// Execute / commit
// ---------------------------------------------------------------------------

FDocInventoryTransactionResult UDocItemSubsystem::PreviewTransaction(const FDocInventoryTransaction& Transaction) const
{
	return const_cast<UDocItemSubsystem*>(this)->Execute(Transaction, false);
}

FDocInventoryTransactionResult UDocItemSubsystem::SubmitTransaction(const FDocInventoryTransaction& Transaction)
{
	FDocInventoryTransactionResult Result = Execute(Transaction, true);
	if (!bCommitting)
	{
		TArray<TFunction<void()>> Events = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		for (TFunction<void()>& Event : Events) { Event(); } // observers see complete committed state
		FlushQueued();
	}
	return Result;
}

void UDocItemSubsystem::FlushQueued()
{
	int32 Guard = 0;
	while (!QueuedTransactions.IsEmpty() && Guard++ < 256)
	{
		const FDocInventoryTransaction Next = QueuedTransactions[0];
		QueuedTransactions.RemoveAt(0);
		SubmitTransaction(Next);
	}
}

bool UDocItemSubsystem::GetTransactionResult(const FGuid& RequestId, FDocInventoryTransactionResult& OutResult) const
{
	if (const FDocInventoryTransactionResult* R = Results.Find(RequestId))
	{
		OutResult = *R;
		return true;
	}
	return false;
}

FDocInventoryTransactionResult UDocItemSubsystem::Execute(const FDocInventoryTransaction& T, bool bCommit)
{
	FDocInventoryTransactionResult Result;
	Result.RequestId = T.RequestId;
	Result.Source = T.SourceContainer;
	Result.Destination = T.DestContainer;
	auto Fail = [&Result, &T, this, bCommit](EDocResultOutcome Outcome, const FGameplayTag& Tag, const FString& Diagnostic, bool bRecord = true)
	{
		Result.Result = FDocSystemResult::MakeFailure(Outcome, Diagnostic, Tag);
		Result.FailureReason = Tag;
		Result.RemainingQuantity = T.Quantity;
		if (bRecord && bCommit && T.RequestId.IsValid() && !Receipts.Contains(T.RequestId))
		{
			Results.Add(T.RequestId, Result);
			FailedResultOrder.Add(T.RequestId);
			while (FailedResultOrder.Num() > Settings()->MaxReceipts)
			{
				if (!Receipts.Contains(FailedResultOrder[0])) { Results.Remove(FailedResultOrder[0]); }
				FailedResultOrder.RemoveAt(0);
			}
		}
		return Result;
	};

	if (bCommit && bCommitting)
	{
		// Reentrant request during a commit: queued until the commit finishes.
		QueuedTransactions.Add(T);
		Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Queued behind the current commit; see GetTransactionResult"));
		return Result;
	}
	if (!HasAuthority())
	{
		return Fail(EDocResultOutcome::PermissionDenied, DocInventoryTags::Error_Inventory_Access, TEXT("Inventory transactions are server-authoritative"));
	}
	if (!T.Requester.IsValid() || (bCommit && !T.RequestId.IsValid()))
	{
		return Fail(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Access, TEXT("Requester scope and RequestId are required"));
	}
	const int64 Hash = HashTransaction(T);
	if (const FDocInventoryReceipt* Receipt = Receipts.Find(T.RequestId))
	{
		if (Receipt->PayloadHash == Hash)
		{
			return Receipt->Result; // retry: the original receipt, never a second transfer
		}
		// Never overwrite the committed result stored under this key.
		return Fail(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_ReceiptConflict, TEXT("RequestId reused with a different payload"), false);
	}
	if (IsQuantityOp(T.Op) && T.Quantity <= 0)
	{
		return Fail(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Quantity, FString::Printf(TEXT("Invalid quantity %d"), T.Quantity));
	}
	if ((T.Op == EDocInventoryOp::Add || T.Op == EDocInventoryOp::Remove) && !T.bAuthorizedSourceOrSink)
	{
		return Fail(EDocResultOutcome::PermissionDenied, DocInventoryTags::Error_Inventory_Access, TEXT("Add/Remove are authorized grants/sinks only"));
	}
	if (T.Op == EDocInventoryOp::Remove && T.Reason.IsEmpty())
	{
		return Fail(EDocResultOutcome::InvalidInput, DocInventoryTags::Error_Inventory_Access, TEXT("A sink needs a recorded reason"));
	}

	// Addressed containers: existence, access, state and expected revisions.
	TArray<TPair<FName, int64>> Addressed;
	const bool bNeedsSource = T.Op != EDocInventoryOp::Add && T.Op != EDocInventoryOp::Pickup;
	const bool bNeedsDest = T.Op == EDocInventoryOp::Add || T.Op == EDocInventoryOp::Transfer || T.Op == EDocInventoryOp::Equip
		|| T.Op == EDocInventoryOp::Unequip || T.Op == EDocInventoryOp::Pickup || (T.Op == EDocInventoryOp::Swap && !T.DestContainer.IsNone());
	if (bNeedsSource) { Addressed.Add(TPair<FName, int64>(T.SourceContainer, T.ExpectedSourceRevision)); }
	if (bNeedsDest) { Addressed.Add(TPair<FName, int64>(T.DestContainer, T.ExpectedDestRevision)); }
	for (const TPair<FName, int64>& A : Addressed)
	{
		const FDocContainerRecord* C = Containers.Find(A.Key);
		if (!C) { return Fail(EDocResultOutcome::NotFound, DocInventoryTags::Error_Inventory_UnknownContainer, FString::Printf(TEXT("Unknown container %s"), *A.Key.ToString())); }
		if (!CanAccess(T.Requester, *C)) { return Fail(EDocResultOutcome::PermissionDenied, DocInventoryTags::Error_Inventory_Access, FString::Printf(TEXT("No access to %s"), *A.Key.ToString())); }
		if (!C->Rules.bEnabled || C->Rules.bReadOnly) { return Fail(EDocResultOutcome::Unsupported, DocInventoryTags::Error_Inventory_Unsupported, FString::Printf(TEXT("%s is disabled or read-only"), *A.Key.ToString())); }
		if (A.Value >= 0 && A.Value != C->Revision) { return Fail(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_StaleRevision, FString::Printf(TEXT("%s changed (revision %lld, expected %lld)"), *A.Key.ToString(), C->Revision, A.Value)); }
	}
	// After existence/access/revision checks, so it cannot probe private or missing containers.
	if (T.Op == EDocInventoryOp::Transfer && T.SourceContainer == T.DestContainer)
	{
		Result.Result = FDocSystemResult::MakeNoChange(TEXT("Same source and destination"));
		Result.bSuccess = true;
		return Result;
	}
	for (const FGuid& Id : { T.InstanceId, T.OtherInstanceId })
	{
		const FReservation* R = Id.IsValid() ? Reservations.Find(Id) : nullptr;
		if (R && R->RequestId != T.RequestId)
		{
			return Fail(EDocResultOutcome::Conflict, DocInventoryTags::Error_Inventory_Reserved, TEXT("Instance is reserved by an action in progress"));
		}
	}

	// Build and validate the proposal on copies; AllowPartial searches the largest valid quantity.
	FProposal Proposal;
	FFailure Failure;
	int32 Granted = T.Quantity;
	bool bOk = BuildProposal(T, T.Quantity, Proposal, Failure);
	if (!bOk && T.bAllowPartial && (T.Op == EDocInventoryOp::Add || T.Op == EDocInventoryOp::Transfer) && IsPartialFailure(Failure.Tag) && T.Quantity > 1)
	{
		int32 Lo = 1, Hi = T.Quantity - 1, Best = 0;
		FProposal BestProposal;
		while (Lo <= Hi)
		{
			const int32 Mid = Lo + (Hi - Lo) / 2;
			FProposal Trial;
			FFailure TrialFail;
			if (BuildProposal(T, Mid, Trial, TrialFail)) { Best = Mid; BestProposal = MoveTemp(Trial); Lo = Mid + 1; }
			else { Hi = Mid - 1; }
		}
		if (Best > 0)
		{
			Proposal = MoveTemp(BestProposal);
			Granted = Best;
			bOk = true;
		}
	}
	if (!bOk)
	{
		return Fail(Failure.Outcome == EDocResultOutcome::Unset ? EDocResultOutcome::Failed : Failure.Outcome, Failure.Tag, Failure.Diagnostic);
	}

	// Conservation: only Add/Remove/Consume are sources or sinks.
	TMap<FName, int64> Before, After;
	for (const TPair<FName, FDocContainerRecord>& Pair : Proposal.Containers)
	{
		SumByItem(Containers[Pair.Key], Before);
		SumByItem(Pair.Value, After);
	}
	if (Proposal.RemovedWorldItem.IsSet()) { const FDocItemInstance& I = WorldItems[Proposal.RemovedWorldItem.GetValue()].Item; Before.FindOrAdd(I.ItemId) += I.Quantity; }
	if (Proposal.NewWorldItem.IsSet()) { const FDocItemInstance& I = Proposal.NewWorldItem->Item; After.FindOrAdd(I.ItemId) += I.Quantity; }
	TSet<FName> Items;
	for (const TPair<FName, int64>& P : Before) { Items.Add(P.Key); }
	for (const TPair<FName, int64>& P : After) { Items.Add(P.Key); }
	for (FName Item : Items)
	{
		int64 Expected = Before.FindRef(Item);
		if (T.Op == EDocInventoryOp::Add && Item == T.ItemId) { Expected += Proposal.Moved; }
		if ((T.Op == EDocInventoryOp::Remove || T.Op == EDocInventoryOp::Consume) && Containers[T.SourceContainer].Find(T.InstanceId) && Containers[T.SourceContainer].Find(T.InstanceId)->ItemId == Item) { Expected -= Proposal.Moved; }
		if (After.FindRef(Item) != Expected)
		{
			return Fail(EDocResultOutcome::Failed, DocInventoryTags::Error_Inventory_Conservation, FString::Printf(TEXT("Conservation violated for %s"), *Item.ToString()));
		}
	}

	Result.MovedQuantity = Proposal.Moved;
	if (T.Op == EDocInventoryOp::Merge)
	{
		const FDocItemInstance* From = Containers[T.SourceContainer].Find(T.InstanceId);
		Result.RemainingQuantity = From ? From->Quantity - Proposal.Moved : 0;
	}
	else if (IsQuantityOp(T.Op))
	{
		Result.RemainingQuantity = T.Quantity - Granted;
	}
	Result.bPartialSuccess = Result.RemainingQuantity > 0;
	Result.bSuccess = true;
	Result.Result = FDocSystemResult::MakeSuccess();
	if (Result.bPartialSuccess)
	{
		Result.Result.Diagnostic = FString::Printf(TEXT("PartialSuccess: %d of %d"), Result.MovedQuantity, Result.MovedQuantity + Result.RemainingQuantity);
	}
	if (!bCommit)
	{
		Result.Result.Diagnostic += TEXT(" (preview)");
		return Result;
	}

	// Commit all affected records together, then post-commit effects.
	TMap<FName, FDocContainerRecord> Originals;
	for (const TPair<FName, FDocContainerRecord>& Pair : Proposal.Containers) { Originals.Add(Pair.Key, Containers[Pair.Key]); }
	bCommitting = true;
	CommitProposal(T, Proposal, Result);
	if (Proposal.NewWorldItem.IsSet())
	{
		const FDocSystemResult Spawned = SpawnProvider.IsValid() ? SpawnProvider->Spawn(Proposal.NewWorldItem.GetValue())
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Spawn provider disappeared"));
		if (!Spawned.IsSuccess())
		{
			// Recoverable escrow policy: the item returns to its source; nothing is lost or duplicated.
			WorldItems.Remove(Proposal.NewWorldItem->WorldItemId);
			FDocInventoryTransactionResult Reverted;
			for (TPair<FName, FDocContainerRecord>& Pair : Originals)
			{
				FDocContainerRecord Restored = Pair.Value;
				RecordDeltas(Containers[Pair.Key], Restored, Reverted);
				Containers[Pair.Key] = Restored;
			}
			RebuildIndex();
			bCommitting = false;
			Result = FDocInventoryTransactionResult();
			Result.RequestId = T.RequestId;
			Result.Source = T.SourceContainer;
			Result.CommittedRevisions = Reverted.CommittedRevisions;
			Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("Spawn failed (%s); item retained in source"), *Spawned.Diagnostic), DocInventoryTags::Error_Inventory_Unsupported);
			Result.FailureReason = DocInventoryTags::Error_Inventory_Unsupported;
			Result.RemainingQuantity = T.Quantity;
			Results.Add(T.RequestId, Result);
			FailedResultOrder.Add(T.RequestId); // same retention bound as other failed results
			while (FailedResultOrder.Num() > Settings()->MaxReceipts)
			{
				if (!Receipts.Contains(FailedResultOrder[0])) { Results.Remove(FailedResultOrder[0]); }
				FailedResultOrder.RemoveAt(0);
			}
			return Result;
		}
		Result.WorldItemId = Proposal.NewWorldItem->WorldItemId;
	}
	bCommitting = false;

	FDocInventoryReceipt Receipt;
	Receipt.RequestId = T.RequestId;
	Receipt.PayloadHash = Hash;
	Receipt.Result = Result;
	Receipts.Add(T.RequestId, Receipt);
	ReceiptOrder.Add(T.RequestId);
	while (ReceiptOrder.Num() > Settings()->MaxReceipts)
	{
		Receipts.Remove(ReceiptOrder[0]);
		Results.Remove(ReceiptOrder[0]);
		ReceiptOrder.RemoveAt(0);
	}
	Results.Add(T.RequestId, Result);
	return Result;
}

void UDocItemSubsystem::RecordDeltas(const FDocContainerRecord& Before, FDocContainerRecord& After, FDocInventoryTransactionResult& Result)
{
	TArray<FDocInventoryDelta> Deltas;
	const int64 NewRevision = Before.Revision + 1;
	for (FDocItemInstance& I : After.Items)
	{
		const FDocItemInstance* Old = Before.Find(I.InstanceId);
		if (!Old || Old->Quantity != I.Quantity || Old->Slots != I.Slots)
		{
			FDocInventoryDelta& D = Deltas.AddDefaulted_GetRef();
			D.ContainerId = After.ContainerId;
			D.Revision = NewRevision;
			D.InstanceId = I.InstanceId;
			D.ItemId = I.ItemId;
			D.Kind = Old ? EDocInventoryDeltaKind::Changed : EDocInventoryDeltaKind::Added;
			D.OldQuantity = Old ? Old->Quantity : 0;
			D.NewQuantity = I.Quantity;
			I.Revision = (Old ? Old->Revision : 0) + 1;
		}
	}
	for (const FDocItemInstance& I : Before.Items)
	{
		if (!After.Find(I.InstanceId))
		{
			FDocInventoryDelta& D = Deltas.AddDefaulted_GetRef();
			D.ContainerId = After.ContainerId;
			D.Revision = NewRevision;
			D.InstanceId = I.InstanceId;
			D.ItemId = I.ItemId;
			D.Kind = EDocInventoryDeltaKind::Removed;
			D.OldQuantity = I.Quantity;
		}
	}
	if (Deltas.IsEmpty())
	{
		After.Revision = Before.Revision;
		return;
	}
	After.Revision = NewRevision;
	FDocContainerRevision& Rev = Result.CommittedRevisions.AddDefaulted_GetRef();
	Rev.ContainerId = After.ContainerId;
	Rev.Revision = NewRevision;
	TArray<FDocInventoryDelta>& Log = DeltaLog.FindOrAdd(After.ContainerId);
	Log.Append(Deltas);
	const int32 Max = Settings()->MaxDeltasPerContainer;
	if (Log.Num() > Max)
	{
		const int64 TrimmedRevision = Log[Log.Num() - Max - 1].Revision;
		Log.RemoveAt(0, Log.Num() - Max);
		// Never keep part of a revision: a view must get all of it or resnapshot.
		while (Log.Num() > 0 && Log[0].Revision == TrimmedRevision) { Log.RemoveAt(0); }
	}
	for (const FDocInventoryDelta& D : Deltas) { Result.AffectedInstances.AddUnique(D.InstanceId); }
	Result.Deltas.Append(Deltas);
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocItemSubsystem>(this), Id = After.ContainerId, Deltas]()
	{
		if (UDocItemSubsystem* This = WeakThis.Get())
		{
			This->OnContainerChangedNative.Broadcast(Id, Deltas);
			This->OnContainerChanged.Broadcast(Id, Deltas);
		}
	});
}

void UDocItemSubsystem::CommitProposal(const FDocInventoryTransaction& T, FProposal& P, FDocInventoryTransactionResult& Result)
{
	for (TPair<FName, FDocContainerRecord>& Pair : P.Containers)
	{
		FDocContainerRecord& Live = Containers[Pair.Key];
		RecordDeltas(Live, Pair.Value, Result);
		Live = Pair.Value;
	}
	for (const FGuid& Id : P.Retired)
	{
		RetiredInstances.Add(Id); // handles to merged/consumed ids now resolve to "retired"
	}
	if (P.RemovedWorldItem.IsSet())
	{
		const FGuid WorldId = P.RemovedWorldItem.GetValue();
		WorldItems.Remove(WorldId);
		if (const TWeakObjectPtr<UDocWorldItemComponent>* Component = WorldComponents.Find(WorldId))
		{
			// The world representation goes away only after the inventory commit succeeded.
			PendingEvents.Add([Weak = *Component]() { if (UDocWorldItemComponent* C = Weak.Get()) { C->NotifyPickedUp(); } });
		}
		WorldComponents.Remove(WorldId);
	}
	if (P.NewWorldItem.IsSet())
	{
		WorldItems.Add(P.NewWorldItem->WorldItemId, P.NewWorldItem.GetValue());
	}
	RebuildIndex();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

FDocSystemResult UDocItemSubsystem::UseItem(const FDocOwnerScope& Requester, FName ContainerId, const FGuid& InstanceId, FName ActionId, const FGuid& RequestId)
{
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Item actions run on the authority"), DocInventoryTags::Error_Inventory_Access);
	}
	if (!RequestId.IsValid() || !Requester.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Requester scope and RequestId are required"), DocInventoryTags::Error_Inventory_Access);
	}
	if (const FDocSystemResult* Done = UseResults.Find(RequestId))
	{
		return *Done; // a retried use never runs the action or consumes again
	}
	if (PendingUses.Contains(RequestId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Action already pending for this request"));
	}
	const FDocContainerRecord* C = Containers.Find(ContainerId);
	if (!C || !CanAccess(Requester, *C))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("No access"), DocInventoryTags::Error_Inventory_Access);
	}
	const FDocItemInstance* I = C->Find(InstanceId);
	const UDocItemDefinition* Def = I ? FindDefinition(I->ItemId) : nullptr;
	if (!I || !Def)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown instance"), DocInventoryTags::Error_Inventory_UnknownInstance);
	}
	const TSharedPtr<IDocItemActionHandler> Handler = ActionHandlers.FindRef(ActionId); // kept alive even if it unregisters itself
	if (!Def->UseActions.Contains(ActionId) || !Handler.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Action not supported for this item"), DocInventoryTags::Error_Inventory_Unsupported);
	}
	if (Reservations.Contains(InstanceId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Item already reserved"), DocInventoryTags::Error_Inventory_Reserved);
	}
	FDocItemActionContext Context;
	Context.RequestId = RequestId;
	Context.Requester = Requester;
	Context.ContainerId = ContainerId;
	Context.Item = *I;
	Context.ActionId = ActionId;
	const FDocSystemResult Can = Handler->CanExecute(Context);
	if (!Can.IsSuccess())
	{
		return Can;
	}
	// validate/reserve -> execute -> commit consumption.
	Reservations.Add(InstanceId, FReservation{ RequestId, Settings()->UseReservationSeconds });
	const FDocItemActionOutcome Outcome = Handler->Execute(Context);
	if (Outcome.State == FDocItemActionOutcome::EState::Pending)
	{
		PendingUses.Add(RequestId, FPendingUse{ Requester, ContainerId, InstanceId });
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Action pending; item reserved"));
	}
	return CompleteUse(RequestId, Outcome.ConsumeQuantity, Outcome.State == FDocItemActionOutcome::EState::Completed ? Outcome.Result
		: (Outcome.Result.IsSuccess() ? FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Action failed")) : Outcome.Result));
}

FDocSystemResult UDocItemSubsystem::CompleteUse(const FGuid& RequestId, int32 ConsumeQuantity, const FDocSystemResult& Result)
{
	FPendingUse Use;
	if (const FPendingUse* Pending = PendingUses.Find(RequestId))
	{
		Use = *Pending;
		PendingUses.Remove(RequestId);
	}
	else
	{
		// Synchronous completion from UseItem: find the reservation by request.
		for (const TPair<FGuid, FReservation>& Pair : Reservations)
		{
			if (Pair.Value.RequestId == RequestId) { Use.InstanceId = Pair.Key; break; }
		}
		Use.ContainerId = FindInstanceContainer(Use.InstanceId);
		if (const FDocContainerRecord* C = Containers.Find(Use.ContainerId)) { Use.Requester = C->Owner; }
	}
	const FReservation* Reservation = Reservations.Find(Use.InstanceId);
	if (!Use.InstanceId.IsValid() || !Reservation || Reservation->RequestId != RequestId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No reservation for this request (expired or completed)"), DocInventoryTags::Error_Inventory_Reserved);
	}
	if (!Result.IsSuccess() || ConsumeQuantity <= 0)
	{
		Reservations.Remove(Use.InstanceId); // failure (or a zero-cost success) consumes nothing
		return RememberUse(RequestId, Result);
	}
	FDocInventoryTransaction Consume;
	Consume.RequestId = RequestId;
	Consume.Op = EDocInventoryOp::Consume;
	Consume.Requester = Use.Requester;
	Consume.SourceContainer = Use.ContainerId;
	Consume.InstanceId = Use.InstanceId;
	Consume.Quantity = ConsumeQuantity;
	Consume.Reason = TEXT("Use");
	const FDocInventoryTransactionResult Committed = SubmitTransaction(Consume);
	Reservations.Remove(Use.InstanceId);
	return RememberUse(RequestId, Committed.Result);
}

FDocSystemResult UDocItemSubsystem::RememberUse(const FGuid& RequestId, const FDocSystemResult& Result)
{
	if (!UseResults.Contains(RequestId))
	{
		UseResultOrder.Add(RequestId);
	}
	UseResults.Add(RequestId, Result);
	while (UseResultOrder.Num() > Settings()->MaxReceipts)
	{
		UseResults.Remove(UseResultOrder[0]);
		UseResultOrder.RemoveAt(0);
	}
	return Result;
}

// ---------------------------------------------------------------------------
// World items
// ---------------------------------------------------------------------------

FDocSystemResult UDocItemSubsystem::CreateWorldItem(FName ItemId, int32 Quantity, const FTransform& Transform, FGuid& OutWorldItemId)
{
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("World items are created on the authority"));
	}
	const UDocItemDefinition* Def = FindDefinition(ItemId);
	if (!Def || Quantity <= 0 || Quantity > Def->MaxStackSize)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unknown item or invalid quantity"));
	}
	FDocWorldItemRecord Record;
	Record.WorldItemId = FGuid::NewGuid();
	Record.Item.InstanceId = FGuid::NewGuid();
	Record.Item.ItemId = ItemId;
	Record.Item.DefinitionVersion = Def->ContentVersion;
	Record.Item.Quantity = Quantity;
	Record.Item.Durability = Def->MaxDurability > 0 ? Def->MaxDurability : -1;
	Record.Item.CreationTimeUtcTicks = FDateTime::UtcNow().GetTicks();
	Record.Transform = Transform;
	Record.Revision = 1;
	OutWorldItemId = Record.WorldItemId;
	WorldItems.Add(Record.WorldItemId, Record);
	RebuildIndex();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocItemSubsystem::ReserveWorldItem(const FGuid& WorldItemId, const FGuid& ClaimRequestId, float Seconds)
{
	FDocWorldItemRecord* Record = WorldItems.Find(WorldItemId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("World item no longer exists"));
	}
	if (Record->ReservedBy.IsValid() && Record->ReservedBy != ClaimRequestId && Record->ReservationRemaining > 0.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Already claimed"), DocInventoryTags::Error_Inventory_Reserved);
	}
	Record->ReservedBy = ClaimRequestId;
	Record->ReservationRemaining = FMath::Max(0.1f, Seconds);
	return FDocSystemResult::MakeSuccess();
}

bool UDocItemSubsystem::GetWorldItem(const FGuid& WorldItemId, FDocWorldItemRecord& OutRecord) const
{
	if (const FDocWorldItemRecord* R = WorldItems.Find(WorldItemId))
	{
		OutRecord = *R;
		return true;
	}
	return false;
}

void UDocItemSubsystem::BindWorldItemComponent(UDocWorldItemComponent* Component)
{
	if (Component && Component->WorldItemId.IsValid())
	{
		WorldComponents.Add(Component->WorldItemId, Component);
	}
}

void UDocItemSubsystem::UnbindWorldItemComponent(UDocWorldItemComponent* Component)
{
	if (Component)
	{
		const TWeakObjectPtr<UDocWorldItemComponent>* Bound = WorldComponents.Find(Component->WorldItemId);
		if (Bound && Bound->Get() == Component)
		{
			WorldComponents.Remove(Component->WorldItemId); // unload: the logical record stays
		}
	}
}

void UDocItemSubsystem::AdvanceClock(double Seconds)
{
	if (Seconds <= 0.0)
	{
		return;
	}
	for (TPair<FGuid, FDocWorldItemRecord>& Pair : WorldItems)
	{
		if (Pair.Value.ReservedBy.IsValid())
		{
			Pair.Value.ReservationRemaining -= Seconds;
			if (Pair.Value.ReservationRemaining <= 0.0) { Pair.Value.ReservedBy.Invalidate(); Pair.Value.ReservationRemaining = 0.0; }
		}
	}
	TArray<FGuid> Expired;
	for (TPair<FGuid, FReservation>& Pair : Reservations)
	{
		Pair.Value.Remaining -= Seconds;
		if (Pair.Value.Remaining <= 0.0) { Expired.Add(Pair.Key); }
	}
	for (const FGuid& Id : Expired)
	{
		const FGuid Request = Reservations[Id].RequestId;
		Reservations.Remove(Id); // released without consumption
		PendingUses.Remove(Request);
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocInventorySaveData UDocItemSubsystem::CaptureState() const
{
	FDocInventorySaveData Data;
	for (const TPair<FName, FDocContainerRecord>& Pair : Containers)
	{
		if (Pair.Value.Owner.IsPersistable()) { Data.Containers.Add(Pair.Value); }
	}
	Data.Containers.Sort([](const FDocContainerRecord& A, const FDocContainerRecord& B) { return A.ContainerId.LexicalLess(B.ContainerId); });
	for (const TPair<FGuid, FDocWorldItemRecord>& Pair : WorldItems)
	{
		FDocWorldItemRecord Record = Pair.Value;
		Record.ReservedBy.Invalidate(); // reservations are transient
		Record.ReservationRemaining = 0.0;
		Data.WorldItems.Add(Record);
	}
	Data.Quarantined = Quarantined;
	for (const FGuid& Id : ReceiptOrder)
	{
		if (const FDocInventoryReceipt* R = Receipts.Find(Id)) { Data.Receipts.Add(*R); }
	}
	return Data;
}

FDocSystemResult UDocItemSubsystem::RestoreState(const FDocInventorySaveData& Data)
{
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocInventorySaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unsupported inventory schema %d"), Data.SchemaVersion));
	}
	// 1. Containers and instances: identity uniqueness across every record (no duplicate membership).
	TSet<FName> ContainerIds;
	TSet<FGuid> InstanceIds;
	auto CheckInstance = [&InstanceIds](const FDocItemInstance& I) -> bool
	{
		bool bDup = false;
		InstanceIds.Add(I.InstanceId, &bDup);
		return I.InstanceId.IsValid() && !bDup && I.Quantity > 0;
	};
	for (const FDocContainerRecord& C : Data.Containers)
	{
		bool bDup = false;
		ContainerIds.Add(C.ContainerId, &bDup);
		if (C.ContainerId.IsNone() || bDup || !C.Owner.IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Container missing, duplicated or ownerless"));
		}
		for (const FDocItemInstance& I : C.Items)
		{
			if (!CheckInstance(I)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Duplicate instance membership or invalid quantity")); }
		}
	}
	TSet<FGuid> WorldIds;
	for (const FDocWorldItemRecord& W : Data.WorldItems)
	{
		bool bDupWorld = false;
		WorldIds.Add(W.WorldItemId, &bDupWorld);
		if (!W.WorldItemId.IsValid() || bDupWorld) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("World item id missing or duplicated")); }
		if (!CheckInstance(W.Item)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("World item duplicates a container item")); }
	}
	// Containers that the save does not mention (session containers, ones registered before the
	// restore) are kept; their items take part in the uniqueness check.
	for (const TPair<FName, FDocContainerRecord>& Pair : Containers)
	{
		if (ContainerIds.Contains(Pair.Key)) { continue; }
		for (const FDocItemInstance& I : Pair.Value.Items)
		{
			if (!CheckInstance(I)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Restored data duplicates a live session item")); }
		}
	}
	for (const FDocItemInstance& I : Data.Quarantined)
	{
		if (!CheckInstance(I)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Quarantined item duplicates another record")); }
	}

	// 2. Memberships: unknown definitions are quarantined (preserved), never deleted.
	TMap<FName, FDocContainerRecord> NewContainers;
	for (const TPair<FName, FDocContainerRecord>& Pair : Containers)
	{
		if (!ContainerIds.Contains(Pair.Key)) { NewContainers.Add(Pair.Key, Pair.Value); }
	}
	TArray<FDocItemInstance> NewQuarantine = Data.Quarantined;
	for (FDocContainerRecord C : Data.Containers)
	{
		for (int32 i = C.Items.Num() - 1; i >= 0; --i)
		{
			if (!FindDefinition(C.Items[i].ItemId))
			{
				NewQuarantine.Add(C.Items[i]);
				C.Items.RemoveAt(i);
			}
		}
		C.Revision += 1; // views resnapshot
		NewContainers.Add(C.ContainerId, C);
	}
	TMap<FGuid, FDocWorldItemRecord> NewWorld;
	for (const FDocWorldItemRecord& W : Data.WorldItems)
	{
		if (FindDefinition(W.Item.ItemId)) { NewWorld.Add(W.WorldItemId, W); } else { NewQuarantine.Add(W.Item); }
	}

	// 3. Apply, rebuild derived state, then notify views once (no equip/grant events are replayed).
	Containers = MoveTemp(NewContainers);
	WorldItems = MoveTemp(NewWorld);
	Quarantined = MoveTemp(NewQuarantine);
	Receipts.Reset();
	ReceiptOrder.Reset();
	Results.Reset();
	FailedResultOrder.Reset();
	UseResults.Reset();
	UseResultOrder.Reset();
	RetiredInstances.Reset();
	QueuedTransactions.Reset();
	for (const FDocInventoryReceipt& R : Data.Receipts)
	{
		Receipts.Add(R.RequestId, R);
		ReceiptOrder.Add(R.RequestId);
		Results.Add(R.RequestId, R.Result);
	}
	Reservations.Reset();
	PendingUses.Reset();
	DeltaLog.Reset();
	RebuildIndex();
	OnStateRefreshedNative.Broadcast();
	OnStateRefreshed.Broadcast();
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

void UDocInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocItemSubsystem* Service = UDocItemSubsystem::Get(this))
	{
		Service->RegisterContainer(ContainerId, OwnerScope, Rules);
	}
}

bool UDocInventoryComponent::GetContents(FDocContainerRecord& OutRecord) const
{
	const UDocItemSubsystem* Service = UDocItemSubsystem::Get(this);
	return Service && Service->GetContainer(OwnerScope, ContainerId, OutRecord).IsSuccess();
}

void UDocWorldItemComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocItemSubsystem* Service = UDocItemSubsystem::Get(this))
	{
		Service->BindWorldItemComponent(this);
	}
}

void UDocWorldItemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocItemSubsystem* Service = UDocItemSubsystem::Get(this))
	{
		Service->UnbindWorldItemComponent(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UDocWorldItemComponent::NotifyPickedUp()
{
	OnPickedUp.Broadcast(WorldItemId);
	if (bDestroyOwnerOnPickup)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->Destroy();
		}
	}
}
