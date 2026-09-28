# DocInventoryItems

DocInventoryItems provides item definitions, item instances, logical containers, atomic inventory transactions, logical equipment slots and world-item pickup and drop (Modules 11–20 handoff, Section 10; Module 18).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Service | `UDocItemSubsystem` (GameInstance). Owns the definition catalog and every container, world item and quarantined instance, keyed by stable ids |
| Components | `UDocInventoryComponent` exposes a logical container on an actor. `UDocWorldItemComponent` binds a world item record to an actor |
| Data | `UDocItemDefinition` (primary data asset). Equipment slots are `Equip.*` tags |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | UI, GAS ability grants, mesh attachment, crafting, currency or vendors, loot generation, nested containers, replication transport, Save and Quest bridges, editor tooling |

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| ItemSubsystem | `UDocItemSubsystem` |
| InventoryComponent | `UDocInventoryComponent` |
| ItemDefinition | `UDocItemDefinition` |
| ItemInstance / ItemInstanceId | `FDocItemInstance` / `FDocItemInstance::InstanceId` |
| InventorySlot | `FDocInventorySlot` |
| InventoryTransaction / Result | `FDocInventoryTransaction` / `FDocInventoryTransactionResult` |
| UWorldItemComponent | `UDocWorldItemComponent` |

## Identity and membership

- **One record per instance.** Every instance belongs to exactly one container, world item or the quarantine list. Membership never depends on an actor being loaded, and unloading an actor keeps its container and world items.
- **Stable ids.** A whole-stack move keeps the instance id. A split creates exactly one new id. A merge keeps the survivor's id and tombstones the consumed one (`IsInstanceRetired`).
- **Slots** are authored `SlotId` names, never array indexes. Multi-slot equipment lists every slot it occupies.
- **Stack keys.** Stacks merge only when `ItemId`, definition version and the configured `FDocItemStackPolicy` facts match. Display names never decide compatibility, and mixed-durability stacks are refused (no averaging model).
- **Weights** are integers (base units such as grams) with overflow detection.

## Transactions

Every mutation goes through `SubmitTransaction`:

1. Authority, requester scope and `RequestId` are required.
2. A receipt with the same payload returns the original result; a changed payload is a `Conflict` (`Doc.Error.Inventory.ReceiptConflict`).
3. Quantity is validated. `Add` and `Remove` need `bAuthorizedSourceOrSink`, and `Remove` needs a reason.
4. Every addressed container is checked for existence, access, enabled/read-only state and the expected revision.
5. Items reserved by an action in progress are refused.
6. The change is built on copies, and every invariant is checked: stacks, slots, tags, uniqueness, capacity, weight and conservation.
7. All affected records commit together, and each changed record gets a new revision.
8. Deltas are published after the commit, so observers always see the complete state.

**Operations:** Add, Remove, Transfer, Split, Merge, Swap, Consume, Move, Equip, Unequip, Pickup, Drop.

**All or nothing** is the default. With `bAllowPartial`, Add and Transfer commit the largest quantity that fits and report `bPartialSuccess` and `RemainingQuantity`.

**Previews.** `PreviewTransaction` is an estimate at the current revisions. It never mutates or records a result.

**Reentrancy.** A request submitted while a commit is in progress (for example from a spawn provider) returns `NotReady` and runs after the commit. Its final result is available through `GetTransactionResult`.

**Receipts.** Only successful transactions are receipted, so a failed request can be retried with the same key. Receipts are FIFO-bounded (`MaxReceipts`) and saved with the containers, so a reward retried after a restart is not granted twice.

## Equipment

Equipment containers use `Equip.*` slot tags. An item with `bOccupiesAllEquipSlots` takes every matching slot atomically or none. Equipping is purely logical: meshes and abilities belong to consumer bridges.

## Actions and world items

**Actions.** `UseItem` runs a registered `IDocItemActionHandler`:

- **Order.** The item is reserved, the action executes, then consumption commits. A failed action consumes nothing.
- **Pending actions** keep the item reserved until `CompleteUse` is called or `UseReservationSeconds` passes on the real-time clock (`AdvanceClock`). An expired reservation is released without consumption.
- **Retries.** A retried use with the same request id never runs the action or consumes a second time.
- **Side effects.** External effects are declared non-atomic.

**Pickup.**

- **Claims.** `ReserveWorldItem` gives one claimant a short-lived claim; the others get `Conflict` until it expires.
- **Identity.** Pickup moves the same instance id into the container.
- **Actor.** The bound actor is destroyed only after the commit succeeds.
- **Failure.** A failed commit keeps the world record.

**Drop.**

- **Provider.** A drop needs an approved `IDocItemSpawnProvider`.
- **Before removal.** `CanSpawn` is checked before anything is removed.
- **Spawn failure.** If `Spawn` fails after the commit, the item returns to its source with the same id, and nothing is lost or duplicated.

## Persistence, privacy and views

**Persistence.** `CaptureState` and `RestoreState` cover containers (except Session-scoped ones), world items, quarantine and receipts.

- **Validation.** Restore rejects duplicate membership and leaves the service unchanged.
- **Missing definitions.** Items whose definition is missing are quarantined, still counted and kept in future saves.
- **After restore.** Derived indexes are rebuilt and views are notified once. No equip or grant events are replayed.
- **Transient state.** Reservations and retired-id tombstones are not saved.

**Privacy.** PlayerProfile containers are readable and writable only by their owner scope. SharedWorld and Party containers accept any trusted requester. Requester scopes come from host code, never from a client claim.

**Views.** `GetContainerDeltas` returns the deltas since a revision, or asks for a resnapshot when the retained window (`MaxDeltasPerContainer`) has a gap or the view is joining late.

## Tests

The tests are under `Doc.Inventory.*`:

- Add, Remove (INV-01)
- Transfer (INV-02)
- SplitMerge (INV-03)
- Capacity (INV-04)
- TagRules (INV-05)
- SaveRestore (INV-06)
- SwapAndReentrancy (INV-07)
- RetryReceipt (INV-08)
- PickupRace (INV-09)
- DropAndUseFailure (INV-10)
- NetworkPrivacy (INV-11, local part)
- RandomInvariants (INV-12, base part)

These still need a transport or profiling:

- the real network profile (replication, late join over the wire)
- load testing under concurrency
