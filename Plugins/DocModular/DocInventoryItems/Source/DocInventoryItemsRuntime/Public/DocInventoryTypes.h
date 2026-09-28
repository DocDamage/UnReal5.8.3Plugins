#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/SoftObjectPtr.h"
#include "DocOwnerScope.h"
#include "DocSystemResult.h"
#include "DocInventoryTypes.generated.h"

class UTexture2D;
class UStaticMesh;

/**
 * DocInventoryItems data model (Modules 11-20 handoff, Section 10; Module 18).
 *
 * Name mapping to the handoff's semantic contracts:
 *   ItemSubsystem               -> UDocItemSubsystem (GameInstance)
 *   InventoryComponent          -> UDocInventoryComponent
 *   ItemDefinition              -> UDocItemDefinition
 *   ItemInstance / ItemInstanceId -> FDocItemInstance / FDocItemInstance::InstanceId (FGuid)
 *   InventorySlot               -> FDocInventorySlot
 *   InventoryTransaction        -> FDocInventoryTransaction
 *   InventoryTransactionResult  -> FDocInventoryTransactionResult
 *   UWorldItemComponent         -> UDocWorldItemComponent
 */
namespace DocInventoryTags
{
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Head);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Body);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Hand);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Primary);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Secondary);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip_Custom);

	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_UnknownDefinition);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_UnknownContainer);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_UnknownInstance);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Quantity);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Capacity);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Weight);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Stack);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_TagRule);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Unique);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Slot);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Access);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_StaleRevision);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Unsupported);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Reserved);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_Conservation);
	DOCINVENTORYITEMSRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Inventory_ReceiptConflict);
}

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

/** Which instance facts must match for two stacks to merge (ItemId always must). */
USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocItemStackPolicy
{
	GENERATED_BODY()

	/** Mixed-durability stacks are forbidden unless this is false (no averaging model exists). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bRequireEqualDurability = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bRequireEqualRuntimeTags = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bRequireEqualBinding = true;

	/** Custom data keys that must match. Empty = all custom data must match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FName> CustomDataKeys;
};

/** ItemDefinition. Immutable at runtime. */
UCLASS(BlueprintType)
class DOCINVENTORYITEMSRUNTIME_API UDocItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") int32 ContentVersion = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (MultiLine = true)) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") TSoftObjectPtr<UStaticMesh> WorldMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FGameplayTagContainer ItemTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1")) int32 MaxStackSize = 1;
	/** Weight per unit in integer base units (e.g. grams). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "0")) int64 WeightUnits = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") TMap<FName, FString> BaseProperties;
	/** Registered action handler ids (Use, Equip, Drop, Inspect, Consume, Custom...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") TArray<FName> UseActions;
	/** Equipment slot tags this item may occupy (Equip.*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (Categories = "Equip")) FGameplayTagContainer EquipTags;
	/** Equipping occupies every slot whose tag is in EquipTags, atomically (e.g. two-handed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") bool bOccupiesAllEquipSlots = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FGameplayTag Category;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FGameplayTag RarityTag;
	/** 0 = no durability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "0")) int32 MaxDurability = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FDocItemStackPolicy StackPolicy;
	/** Items that are containers cannot be placed in containers (nesting is not supported by the base). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") bool bIsContainer = false;

	void FindProblems(TArray<FString>& OutErrors) const;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocItemDefinition"), GetFName()); }
};

// ---------------------------------------------------------------------------
// Instances and containers
// ---------------------------------------------------------------------------

/** Bounded, typed instance data (never an arbitrary UObject graph). */
USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocItemDataEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") FName Key;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") int64 IntValue = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") FString StringValue;

	friend bool operator==(const FDocItemDataEntry& A, const FDocItemDataEntry& B) { return A.Key == B.Key && A.IntValue == B.IntValue && A.StringValue == B.StringValue; }
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocItemInstance
{
	GENERATED_BODY()

	/** Stable authoritative identity (ItemInstanceId). */
	UPROPERTY(BlueprintReadOnly, Category = "Item") FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Item") FName ItemId;
	UPROPERTY(BlueprintReadOnly, Category = "Item") int32 DefinitionVersion = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Item") int32 Quantity = 0;
	/** -1 = no durability. */
	UPROPERTY(BlueprintReadOnly, Category = "Item") int32 Durability = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Item") FGameplayTagContainer RuntimeTags;
	UPROPERTY(BlueprintReadOnly, Category = "Item") TArray<FDocItemDataEntry> CustomData;
	/** Binding (soulbound etc.). Ownership itself is derived from container membership. */
	UPROPERTY(BlueprintReadOnly, Category = "Item") FDocOwnerScope BoundTo;
	UPROPERTY(BlueprintReadOnly, Category = "Item") int64 CreationTimeUtcTicks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Item") int64 Revision = 0;
	/** Slot membership (None in list containers). Multi-slot equipment lists every occupied slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Item") TArray<FName> Slots;
};

/** InventorySlot: a stable authored slot identity (never a persistent array index). */
USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") FName SlotId;
	/** Items must carry one of these tags (empty = any). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory") FGameplayTagContainer AcceptTags;
	/** Equipment containers: the logical Equip.* tag of this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (Categories = "Equip")) FGameplayTag EquipTag;
};

UENUM(BlueprintType)
enum class EDocContainerMode : uint8
{
	/** Unrestricted list (MaxSlots limits stack count when > 0). */
	List,
	/** Capacity-limited authored slots. */
	Slot,
	/** Logical equipment slots (no mesh attach, no ability grant in the base). */
	Equipment,
	Storage,
	/** Semantic tag only: no currency or commerce. */
	Vendor,
	/** Semantic tag only: no random generation. */
	Loot,
	Custom
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocContainerRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") EDocContainerMode Mode = EDocContainerMode::List;
	/** Items must have one of these tags (empty = any). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGameplayTagContainer AllowedTags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGameplayTagContainer BlockedTags;
	/** List/Storage: max stacks (0 = unlimited). Slot/Equipment use the authored slots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0")) int32 MaxSlots = 0;
	/** 0 = unlimited. Integer base units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0")) int64 MaxWeightUnits = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bStacking = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") TArray<FDocInventorySlot> SlotTypes;
	/** At most one instance per ItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bUniqueItems = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bReadOnly = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int32 RulesVersion = 1;

	const FDocInventorySlot* FindSlot(FName SlotId) const
	{
		return SlotTypes.FindByPredicate([SlotId](const FDocInventorySlot& S) { return S.SlotId == SlotId; });
	}
	bool UsesSlots() const { return Mode == EDocContainerMode::Slot || Mode == EDocContainerMode::Equipment; }
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocContainerRecord
{
	GENERATED_BODY()

	/** Stable logical identity; survives actor unload. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName ContainerId;
	/** Access policy: PlayerProfile owners are private; SharedWorld/Party are accessible to any trusted requester. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FDocContainerRules Rules;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocItemInstance> Items;

	const FDocItemInstance* Find(const FGuid& Id) const { return Items.FindByPredicate([&Id](const FDocItemInstance& I) { return I.InstanceId == Id; }); }
	FDocItemInstance* Find(const FGuid& Id) { return Items.FindByPredicate([&Id](const FDocItemInstance& I) { return I.InstanceId == Id; }); }
};

/** A logical item lying in the world (or in drop escrow). Exactly one record owns the instance. */
USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocWorldItemRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGuid WorldItemId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FDocItemInstance Item;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FTransform Transform;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int64 Revision = 0;
	/** Pickup reservation (claimant request id) and its expiry on the real-time clock. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGuid ReservedBy;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") double ReservationRemaining = 0.0;
};

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocInventoryOp : uint8
{
	/** Authorized grant (source). */
	Add,
	/** Authorized sink with a reason. */
	Remove,
	Transfer,
	Split,
	Merge,
	Swap,
	/** Owner consumption (sink). */
	Consume,
	/** Slot move inside one container. */
	Move,
	Equip,
	Unequip,
	/** World item -> container (same instance id). */
	Pickup,
	/** Container -> world item through an approved spawn provider. */
	Drop
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocInventoryTransaction
{
	GENERATED_BODY()

	/** Idempotency key: a retry with the same payload returns the original result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGuid RequestId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") EDocInventoryOp Op = EDocInventoryOp::Transfer;
	/** Trusted requester scope (resolved by host code, never taken from a client claim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FDocOwnerScope Requester;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FName SourceContainer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FName DestContainer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGuid InstanceId;
	/** Merge survivor / Swap partner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGuid OtherInstanceId;
	/** Pickup/Drop world record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGuid WorldItemId;
	/** Add only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int32 Quantity = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FName DestSlot;
	/** -1 = any revision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int64 ExpectedSourceRevision = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int64 ExpectedDestRevision = -1;
	/** AllOrNothing (false) is the default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bAllowPartial = false;
	/** Host-set: this Add/Remove is an authorized grant/sink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bAuthorizedSourceOrSink = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FString Reason;
	/** Add: initial instance data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int32 InitialDurability = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") TArray<FDocItemDataEntry> InitialCustomData;
	/** Drop: where to spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FTransform DropTransform;
};

UENUM(BlueprintType)
enum class EDocInventoryDeltaKind : uint8
{
	Added,
	Removed,
	Changed
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocInventoryDelta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName ContainerId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName ItemId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") EDocInventoryDeltaKind Kind = EDocInventoryDeltaKind::Changed;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int32 OldQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int32 NewQuantity = 0;
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocContainerRevision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName ContainerId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int64 Revision = 0;
};

/** InventoryTransactionResult. */
USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocInventoryTransactionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FDocSystemResult Result;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") bool bSuccess = false;
	/** Success with RemainingQuantity > 0; only with bAllowPartial. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") bool bPartialSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGameplayTag FailureReason;
	/** Quantity fulfilled (moved, added, removed, split off, merged). */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int32 MovedQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int32 RemainingQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName Source;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FName Destination;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocContainerRevision> CommittedRevisions;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FGuid> AffectedInstances;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocInventoryDelta> Deltas;
	/** Drop: the world record created (or None when the item was retained). */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FGuid WorldItemId;
	/** Post-commit effect status (bridges). */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") FString EffectStatus;
};

USTRUCT()
struct DOCINVENTORYITEMSRUNTIME_API FDocInventoryReceipt
{
	GENERATED_BODY()

	UPROPERTY() FGuid RequestId;
	UPROPERTY() int64 PayloadHash = 0;
	UPROPERTY() FDocInventoryTransactionResult Result;
};

USTRUCT(BlueprintType)
struct DOCINVENTORYITEMSRUNTIME_API FDocInventorySaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory") int32 SchemaVersion = CurrentSchemaVersion;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocContainerRecord> Containers;
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocWorldItemRecord> WorldItems;
	/** Instances whose definitions are missing (preserved, never deleted). */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory") TArray<FDocItemInstance> Quarantined;
	UPROPERTY() TArray<FDocInventoryReceipt> Receipts;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Inventory Items"))
class DOCINVENTORYITEMSRUNTIME_API UDocInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Receipt retention (replay window, in transactions). Receipts are saved with containers. */
	UPROPERTY(Config, EditAnywhere, Category = "Receipts", meta = (ClampMin = "1")) int32 MaxReceipts = 4096;

	/** Deltas kept per container for incremental views; older gaps require a resnapshot. */
	UPROPERTY(Config, EditAnywhere, Category = "Views", meta = (ClampMin = "1")) int32 MaxDeltasPerContainer = 64;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1")) int32 MaxCustomDataEntries = 16;

	/** Seconds an async Use reservation lives before it is released without consumption. */
	UPROPERTY(Config, EditAnywhere, Category = "Actions", meta = (ClampMin = "0.1")) float UseReservationSeconds = 10.f;
};
