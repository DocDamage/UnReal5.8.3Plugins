# DocModularCore API Overview

All types are game-thread only unless stated.

## Results
- `FDocSystemResult { Outcome, ErrorTag, UserMessage, Diagnostic, OperationId }`.
- `MakeSuccess(OpId)`, `MakeFailure(Outcome, Diagnostic, [ErrorTag], [UserMessage], [OpId])`. An empty ErrorTag becomes the outcome's default `Doc.Error.*` tag.
- `MakeNoChange([Diagnostic], [OpId])` — valid request already satisfied, nothing mutated.
- `IsSuccess()` is true for `Succeeded` and `NoChange`. `IsChanged()` is true only for `Succeeded`.
- Failure outcomes: `Unsupported` (capability not installed/implemented), `Unavailable` (optional provider absent at runtime), `NotReady`, `NotFound`, `InvalidConfiguration` (authored data), `InvalidInput` (request values), `PermissionDenied`, `Conflict` (stale revision / concurrent change), `Cancelled`, `TimedOut`, `Failed` (execution; use tag `Doc.Error.Storage` for storage failures).
- Default outcome `Unset` is a failure. Passing `Succeeded`/`NoChange`/`Unset` to `MakeFailure` is a programming error (ensure, converted to `Failed`).
- The enum is append-only; numeric values are pinned by `static_assert`.

## Request handles
- `FDocRequestHandle` — (OperationId, Epoch). Both `Transient`: persistent serialization drops them; a restored handle is unset. Never save a handle.
- `TDocHandleTable<Payload>` — owned by a subsystem. `Add(Scope, Payload)`, `Validate(Handle, Scope)` → `Invalid | Active | Stale | WrongScope`, `Find`, `Remove` (idempotent, never touches other entries), `RemoveAllForScope` (world teardown), `Reset` (new epoch; every outstanding handle becomes Stale).
- Scope is normally the owning `UWorld`. Scopes are stored as `FObjectKey`; the table holds no strong references.
- Requirements for owners: release the payload's resources when `Remove` returns true; call `RemoveAllForScope`/`Reset` on world teardown.

## Persistent identity
- `FDocPersistentObjectId { WorldNamespace, InstanceScope, LocalObjectGuid }`. Valid = namespace and local GUID set. Zero InstanceScope = world root.
- `ComposeInstanceScope(Parent, PlacementGuid)` — deterministic v1 SHA-1 composition (see `Docs/DECISIONS.md` D-006). Pinned by golden tests; never change in place.
- `ToString()` / `Parse()` — `NNNN…:SSSS…:LLLL…` (three 32-hex-digit GUIDs). `operator<<` for binary (48 bytes).
- `FDocWorldObjectReference { PersistentId, CachedObject(weak, transient) }` — equality by ID only. `GetResolvedObject()` returns the cache only if alive and its `IDocPersistentIdentity` still matches.

## Context
- `FDocGameplayContext { World, Instigator, Target, LocalPlayer, ContextTags, CorrelationId }` — all weak. `IsValidForWorld(World)` rejects cross-world and dead references. `ResolveAuthority()` → `Unknown | Standalone | Authority | Remote`.

## Interfaces
| Interface | Functions | Contract highlights |
|---|---|---|
| `IDocPersistentIdentity` | `GetDocPersistentId()` | Stable for the object's logical life; invalid ID if unassigned (never freshly generated) |
| `IDocGameplayTagProvider` | `GetDocOwnedTags(out Tags)` | Append-only, side-effect free |
| `IDocMutableGameplayTagProvider` | `ApplyDocTagDelta(Context, Add, Remove)` | Owner-attributed; one owner cannot remove another owner's grants |
| `IDocWorldStateProvider` | `QueryDocWorldState(Tag, MatchMode)` → `Unknown/No/Yes` | Unknown ≠ No |
| `IDocPlayerControlProvider` | `AcquireDocControl`, `ReleaseDocControl`, `IsDocControlClaimActive` | Overlapping claims; release recomputes effective state, never restores stale snapshots; no player-0 fallback |

## Expansion primitives (added with their first consumers, D-015)
- `FDocOwnerScope { Kind, SubjectId, CampaignNamespace }` — `IsValid()`, `IsPersistable()` (Session scopes are never saved), `ToString()`, hashable. A scope received from a client is a claim; features resolve the caller's allowed scope from trusted host context.
- `FDocConditionResult { State, Reason, UserReason, Diagnostic, EvaluatedRevision }` — `Satisfied / Unsatisfied / Unavailable`. `CombineAll` (empty = Satisfied; any Unavailable wins over Unsatisfied) and `CombineAny`. Missing providers are Unavailable, never satisfied.
- `EDocClockDomain` — Simulation (feature-owned game time), WorldGameplay (pauses and dilates with the world), RealTime (monotonic, ignores pause), WallClock (opt-in OS clock for offline progress).
- `FDocRecordRevision { Epoch, Revision }` — `Bump()`, `NewEpoch()`; stale revisions and other epochs are rejected by owners.
- `FDocFeatureRecord` + `DocCoreSerialization::Encode/Decode<T>` — tagged-property bytes for plain snapshot structs.
- `FDocEffectKey { Owner, CampaignEpoch, ProducerInstanceId, TransitionOrdinal, ActionId }`, `FDocEffectReceipt`, `FDocReceiptLedger` — `Check(Key, PayloadHash)` returns New, Duplicate (same payload: return the stored receipt) or Conflict (changed payload), `Record`, FIFO bound, `GetAll` / `RestoreAll`, `HashString` / `HashBytes`.
- `FDocControlClaimArbiter` — pure arbitration: a claim covers a capability when it holds the tag or an ancestor; effective owner = highest priority, then most recent. Removing a claim recomputes; nothing is snapshotted.
- `UDocReferencePlayerControlProvider` — packaged `IDocPlayerControlProvider` for one local player (move/look ignore counts, cursor, pause, camera arbitration).
- `UDocPlayerControlSubsystem` (LocalPlayer) — one provider per local player; without a provider every acquire is Unavailable.
- Tags added: `Doc.Control.Cursor`, `Doc.Control.Focus` (D-014).
