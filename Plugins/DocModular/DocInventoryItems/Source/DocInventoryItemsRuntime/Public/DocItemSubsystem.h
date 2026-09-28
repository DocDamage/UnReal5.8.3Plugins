#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DocInventoryTypes.h"
#include "DocItemSubsystem.generated.h"

struct FDocItemActionContext
{
	FGuid RequestId;
	FDocOwnerScope Requester;
	FName ContainerId;
	FDocItemInstance Item;
	FName ActionId;
};

struct FDocItemActionOutcome
{
	enum class EState : uint8 { Completed, Pending, Failed };
	EState State = EState::Failed;
	/** Quantity to consume on success (committed only after the handler succeeds). */
	int32 ConsumeQuantity = 0;
	FDocSystemResult Result;
};

/**
 * Registered item action (Use, Equip, Drop, Inspect, Consume, Custom). CanExecute is pure.
 * External side effects of Execute cannot be rolled back universally: they are declared
 * non-atomic; consumption is committed only after Execute reports success.
 */
class DOCINVENTORYITEMSRUNTIME_API IDocItemActionHandler
{
public:
	virtual ~IDocItemActionHandler() = default;
	virtual FDocSystemResult CanExecute(const FDocItemActionContext& Context) const { return FDocSystemResult::MakeSuccess(); }
	virtual FDocItemActionOutcome Execute(const FDocItemActionContext& Context) = 0;
};

/** Approved world representation for drops. CanSpawn is checked before inventory removal. */
class DOCINVENTORYITEMSRUNTIME_API IDocItemSpawnProvider
{
public:
	virtual ~IDocItemSpawnProvider() = default;
	virtual FDocSystemResult CanSpawn(const FDocWorldItemRecord& Record) const = 0;
	virtual FDocSystemResult Spawn(const FDocWorldItemRecord& Record) = 0;
};

class UDocWorldItemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocInventoryChangeEvent, FName, ContainerId, const TArray<FDocInventoryDelta>&, Deltas);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocInventoryRefreshEvent);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocInventoryChangeNative, FName, const TArray<FDocInventoryDelta>&);

/**
 * Item service (ItemSubsystem, handoff Section 10). Owns the definition catalog and every
 * logical container, world item and quarantined instance, keyed by stable ids, so
 * membership never disappears because an actor unloads. Every instance belongs to exactly
 * one record.
 *
 * All mutations are transactions: resolve context -> check receipt and revisions -> build
 * proposed copies of the affected records -> validate every invariant (capacity, weight,
 * stacks, tags, uniqueness, slots, conservation) -> commit all records together and bump
 * revisions -> record the receipt -> publish immutable deltas after the commit.
 */
UCLASS()
class DOCINVENTORYITEMSRUNTIME_API UDocItemSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocItemSubsystem* Get(const UObject* WorldContextObject);

	// ---- Catalog ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocSystemResult RegisterDefinition(UDocItemDefinition* Definition);
	const UDocItemDefinition* FindDefinition(FName ItemId) const;

	void RegisterActionHandler(FName ActionId, TSharedPtr<IDocItemActionHandler> Handler);
	void SetSpawnProvider(TSharedPtr<IDocItemSpawnProvider> Provider) { SpawnProvider = Provider; }

	// ---- Containers ----
	/** Creates the logical container once; later calls keep its membership (NoChange). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocSystemResult RegisterContainer(FName ContainerId, const FDocOwnerScope& Owner, const FDocContainerRules& Rules);

	/** Access-checked read. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocSystemResult GetContainer(const FDocOwnerScope& Viewer, FName ContainerId, FDocContainerRecord& OutRecord) const;

	/** Deltas newer than SinceRevision; bOutNeedsResnapshot when the retained window has a gap. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocSystemResult GetContainerDeltas(const FDocOwnerScope& Viewer, FName ContainerId, int64 SinceRevision, TArray<FDocInventoryDelta>& OutDeltas, bool& bOutNeedsResnapshot) const;

	/** Which record holds an instance (None if retired or unknown). */
	FName FindInstanceContainer(const FGuid& InstanceId) const;
	bool IsInstanceRetired(const FGuid& InstanceId) const { return RetiredInstances.Contains(InstanceId); }

	// ---- Transactions ----
	/** Estimate at the current revisions; never a promise of a later commit. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocInventoryTransactionResult PreviewTransaction(const FDocInventoryTransaction& Transaction) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocInventoryTransactionResult SubmitTransaction(const FDocInventoryTransaction& Transaction);

	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	bool GetTransactionResult(const FGuid& RequestId, FDocInventoryTransactionResult& OutResult) const;

	// ---- Actions ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	FDocSystemResult UseItem(const FDocOwnerScope& Requester, FName ContainerId, const FGuid& InstanceId, FName ActionId, const FGuid& RequestId);

	/** Completion of a Pending action: success commits the consumption; failure releases the reservation. */
	FDocSystemResult CompleteUse(const FGuid& RequestId, int32 ConsumeQuantity, const FDocSystemResult& Result);

	// ---- World items ----
	/** Authorized creation of a logical world item (loot placement, authored pickups). */
	FDocSystemResult CreateWorldItem(FName ItemId, int32 Quantity, const FTransform& Transform, FGuid& OutWorldItemId);
	/** Short-lived pickup claim; competing claimants get Conflict until it expires. */
	FDocSystemResult ReserveWorldItem(const FGuid& WorldItemId, const FGuid& ClaimRequestId, float Seconds);
	bool GetWorldItem(const FGuid& WorldItemId, FDocWorldItemRecord& OutRecord) const;
	void BindWorldItemComponent(UDocWorldItemComponent* Component);
	void UnbindWorldItemComponent(UDocWorldItemComponent* Component);

	/** Real-time step for reservations. */
	void AdvanceClock(double Seconds);

	// ---- Persistence ----
	FDocInventorySaveData CaptureState() const;
	/** Containers/instances first, memberships second, uniqueness and conservation validated, then one refresh. */
	FDocSystemResult RestoreState(const FDocInventorySaveData& Data);

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Inventory") FDocInventoryChangeEvent OnContainerChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Inventory") FDocInventoryRefreshEvent OnStateRefreshed;
	FDocInventoryChangeNative OnContainerChangedNative;
	FSimpleMulticastDelegate OnStateRefreshedNative;

	// ---- Tests / diagnostics ----
	static void SetSubsystemOverrideForTesting(UDocItemSubsystem* Override);
	void SetAuthorityOverrideForTesting(TOptional<bool> bAuthority) { AuthorityOverride = bAuthority; }
	/** Total quantity of an item across containers, world items and quarantine (conservation checks). */
	int64 CountItem(FName ItemId) const;
	int32 GetQuarantinedCount() const { return Quarantined.Num(); }

	virtual void Deinitialize() override;

private:
	struct FFailure
	{
		FGameplayTag Tag;
		FString Diagnostic;
		EDocResultOutcome Outcome = EDocResultOutcome::Failed;
	};

	struct FProposal
	{
		TMap<FName, FDocContainerRecord> Containers;
		TArray<FGuid> Retired;
		TOptional<FDocWorldItemRecord> NewWorldItem;
		TOptional<FGuid> RemovedWorldItem;
		int32 Moved = 0;
	};

	struct FReservation
	{
		FGuid RequestId;
		double Remaining = 0.0;
	};

	struct FPendingUse
	{
		FDocOwnerScope Requester;
		FName ContainerId;
		FGuid InstanceId;
	};

	bool HasAuthority() const;
	const UDocInventorySettings* Settings() const;
	bool CanAccess(const FDocOwnerScope& Requester, const FDocContainerRecord& Container) const;
	static int64 HashTransaction(const FDocInventoryTransaction& T);

	FDocInventoryTransactionResult Execute(const FDocInventoryTransaction& T, bool bCommit);
	bool BuildProposal(const FDocInventoryTransaction& T, int32 Quantity, FProposal& Out, FFailure& Fail) const;
	bool ValidateContainer(const FDocContainerRecord& C, FFailure& Fail) const;
	bool StacksCompatible(const FDocItemInstance& A, const FDocItemInstance& B, const UDocItemDefinition& Def) const;
	bool PlaceItem(FDocContainerRecord& C, FDocItemInstance Item, FName DestSlot, bool bAllowMerge, FProposal& P, FFailure& Fail) const;
	bool AssignSlots(const FDocContainerRecord& C, const UDocItemDefinition& Def, FName Requested, TArray<FName>& OutSlots) const;
	void CommitProposal(const FDocInventoryTransaction& T, FProposal& P, FDocInventoryTransactionResult& Result);
	void RecordDeltas(const FDocContainerRecord& Before, FDocContainerRecord& After, FDocInventoryTransactionResult& Result);
	void RebuildIndex();
	void FlushQueued();
	FDocSystemResult RememberUse(const FGuid& RequestId, const FDocSystemResult& Result);

	TMap<FName, TObjectPtr<UDocItemDefinition>> Definitions;
	UPROPERTY() TArray<TObjectPtr<UDocItemDefinition>> DefinitionRefs;
	TMap<FName, TSharedPtr<IDocItemActionHandler>> ActionHandlers;
	TSharedPtr<IDocItemSpawnProvider> SpawnProvider;

	TMap<FName, FDocContainerRecord> Containers;
	TMap<FGuid, FDocWorldItemRecord> WorldItems;
	TArray<FDocItemInstance> Quarantined;
	TMap<FGuid, FName> InstanceIndex;
	TSet<FGuid> RetiredInstances;
	TMap<FName, TArray<FDocInventoryDelta>> DeltaLog;

	TMap<FGuid, FDocInventoryReceipt> Receipts;
	TArray<FGuid> ReceiptOrder;
	TMap<FGuid, FDocInventoryTransactionResult> Results;
	TArray<FGuid> FailedResultOrder; // failed results share the receipt retention bound
	/** Terminal results of item uses by request id (retries never run the action again). */
	TMap<FGuid, FDocSystemResult> UseResults;
	TArray<FGuid> UseResultOrder;

	TMap<FGuid, FReservation> Reservations; // instance -> use reservation
	TMap<FGuid, FPendingUse> PendingUses;    // request -> pending use
	TMap<FGuid, TWeakObjectPtr<UDocWorldItemComponent>> WorldComponents;

	bool bCommitting = false;
	TArray<FDocInventoryTransaction> QueuedTransactions;
	TArray<TFunction<void()>> PendingEvents;
	TOptional<bool> AuthorityOverride;
};

/**
 * Exposes a logical container on an actor (InventoryComponent). The container lives in
 * the item service; unloading the actor does not remove membership.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINVENTORYITEMSRUNTIME_API UDocInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FName ContainerId;
	/** Assigned by trusted host code. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FDocOwnerScope OwnerScope;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FDocContainerRules Rules;

	UFUNCTION(BlueprintCallable, Category = "Doc|Inventory")
	bool GetContents(FDocContainerRecord& OutRecord) const;

protected:
	virtual void BeginPlay() override;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocWorldItemPickedUp, FGuid, WorldItemId);

/**
 * Binds a logical world item record to an actor (UWorldItemComponent). The actor is hidden
 * or destroyed only after the inventory commit succeeds; streaming unload (EndPlay other
 * than Destroyed) is neither pickup nor destruction of the record.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCINVENTORYITEMSRUNTIME_API UDocWorldItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") FGuid WorldItemId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") bool bDestroyOwnerOnPickup = true;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Inventory") FDocWorldItemPickedUp OnPickedUp;

	/** Called by the item service after a committed pickup. */
	void NotifyPickedUp();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
