# DocUnlocksProgression

DocUnlocksProgression provides generic entitlements: permanent and temporary grants, prerequisite graphs, progress thresholds and gates (Modules 11–20 handoff, Section 11; Module 19).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Service | `UDocUnlockSubsystem` (GameInstance). Records are partitioned by owner scope (which carries the campaign namespace) |
| World facade | `UDocUnlockWorldFacade`. Attaches the authority world and drives lease timers |
| Data | `UDocUnlockDefinition` (primary data asset). Categories are `Unlock.*` tags |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Quest, Knowledge, Item, Region and Dialogue condition bridges; inventory/quest/knowledge/sequence action bridges; network replication of private entitlements; debugger UI |

Unlocking an Area loads nothing, and unlocking an Ability implements nothing. Consumer adapters own those actions.

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| UnlockSubsystem | `UDocUnlockSubsystem` |
| UnlockDefinition | `UDocUnlockDefinition` |
| UnlockCondition | `FDocUnlockCondition` inside an `FDocUnlockExpression` |
| UnlockAction | `FDocUnlockAction` |
| UnlockRuntimeState | `FDocUnlockRecord` (facts) / `FDocUnlockSnapshot` (derived view) |

## Facts, not labels

Each record stores independent facts:

- the permanent entitlement, with its time and source
- source-owned temporary grants
- the administrative disabled flag
- visibility
- the effective-state baseline and a transition ordinal

Progress values and tag claims are stored per owner.

**Effective availability** requires all of the following:

- the unlock is not disabled
- it has one of: a permanent entitlement, a valid temporary grant, or (for `bPermanent = false` unlocks) current eligibility
- the `AvailabilityGate` is satisfied; an Unavailable gate fails closed

**Eligibility versus availability.** These are distinct:

- **Latching.** A permanent unlock latches its eligibility into an entitlement, so it stays earned if a prerequisite later becomes unavailable.
- **Live gates.** A live gate can make an unlock temporarily unusable without deleting the entitlement.

**Labels.** Locked, Unlocked, TemporarilyUnlocked and Disabled are derived. Hidden is a separate presentation flag (`bHiddenUntilAvailable` or a visibility override) and never an entitlement.

## Graphs and evaluation

**Expressions** are AND/OR/NOT trees stored as flat, bounded arrays and validated for indices, arity, self-reference and depth. Their leaves are:

- Prerequisite
- GameplayTag (granted claims, else the tag provider)
- NumericThreshold (owner progress; an unset value is a legitimate 0)
- WorldEvent, Time and Custom, through providers registered by id

**Missing inputs.** A missing prerequisite or provider is **Unavailable**, never satisfied.

**Registration** is atomic. It rejects duplicate ids, invalid expressions, self-edges and cycles across the whole loaded catalog, and reports the specific path (for example `M -> N -> M`).

**Policies:**

| Policy | When it evaluates |
|---|---|
| Manual | Only through `EvaluateUnlock` |
| EventDriven | Immediately, when an input changes |
| Batch | Marked dirty and shown as `bPending`; evaluated by a budgeted `EvaluateDirty` |
| Auto | Like EventDriven, and also when an owner registers |

**Dirty propagation.**

- **Reverse indexes.** Indexes by prerequisite, progress key, provider and tag mark only the affected nodes dirty.
- **Order.** Batches run in deterministic topological order.
- **Publishing.** Changes are published after the batch. Callbacks run after the commit, so reentrancy is bounded.
- **No change, no work.** Repeated inputs with no change do no work and replay no actions.

**Progress** supports SetValue, AddDelta and Observed. Non-finite values and magnitudes above `MaxProgressMagnitude` are rejected. Hysteresis and cooldowns are not built in; author them explicitly if needed.

## Temporary grants

`AcquireTemporaryGrant` and `ReleaseTemporaryGrant` are source-owned: only the granting source may release a grant. Releasing one grant never affects another grant or the permanent entitlement.

| Kind | Behaviour |
|---|---|
| Duration | Declared clock; RealTime by default, which does not age while the application is closed |
| Session | Expires on `EndSession` |
| Region | Valid while `NotifyRegionMembership` reports the owner inside; recomputed, never stored as a pointer |
| Condition | Fails closed while its provider is missing |
| Custom | Valid until released |

**Timers.**

- **Clocks.** The facade advances WorldGameplay (pauses and dilates with the world), Simulation and RealTime.
- **Jumps.** A jump is one step.
- **Backward steps** are ignored, so a grant that has expired never comes back.

## Effects, revocation, persistence, authority

**Actions.** UnlockActions and LockActions run only on real effective-state transitions. Re-evaluation, restore, rebuilds and UI refreshes never repeat them.

- **Effect keys.** Each action gets a Core effect key: owner, campaign, a deterministic id per unlock, transition ordinal, action id.
- **Delivery.** A failed delivery leaves the intent pending. `RetryPendingActions` resends it with the same key, and the entitlement is never toggled as a retry mechanism.
- **Tag claims** belong to their source. A tag stays granted while any source holds it, and an external tag provider hears only about the first claim and the last release.

**Revocation.** `RevokePermanent` is the only way to remove a permanent entitlement. It needs authority and an audit reason, and it runs the declared LockActions. Releasing a temporary grant, loading a save or closing a UI never removes one.

**Receipts.** `GrantPermanent` accepts a receipt key.

**Persistence.** `CaptureState` and `RestoreState` cover:

- entitlements
- persistent lease descriptors (non-persistent and session grants are dropped)
- progress
- tag claims
- pending intents
- audit log
- receipts

**Restore behaviour:**

- Unknown unlock records are quarantined and kept in future saves.
- Indexes are rebuilt.
- The effective baseline is re-established silently, with no actions and no events, followed by one `OnStateRefreshed`.

**Authority.** Mutations and `ValidateGate` require authority. `ValidateGate` also checks the owner revision the request was built against, so a stale client view is rejected.

- **Previews.** A UI preview is advisory only.
- **Privacy.** `GetSnapshotForViewer` returns a player's private entitlements only to that player.

## Tests

The tests are under `Doc.Unlock.*`:

- Basic
- Prerequisite
- AndCondition and OrCondition
- CycleDetection
- Temporary
- SaveRestore
- DisabledAndHidden
- DirtyPropagation
- ClockPolicy
- ActionReceipt
- AuthoritativeGate (local part)

These still need a transport or profiling:

- the real network profile (UNL-11)
- graph stress testing (UNL-12)
