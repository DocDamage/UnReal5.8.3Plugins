# DocSave

Versioned persistence for persistent objects, runtime spawns, tombstones and feature records (Rev 2 handoff, Section 13). It sits alongside Unreal's `USaveGame` and does not replace it.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Owner | `UDocSaveGameSubsystem` (game instance): keeps plain records across map travel, never actors |
| Storage | `FDocLocalFileSaveBackend`: `<Saved>/<SaveFolder>/<Slot>.docsav`, with `.bak` kept as last-known-good |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Platform/cloud backends, encryption/authentication, Streaming/Activation/Regions bridges, editor tools (id repair, save inspector) |

## Identity

`UDocSaveableComponent` gives its actor a `FDocPersistentObjectId` made of `(WorldNamespace, InstanceScope, LocalObjectGuid)`:

- **LocalObjectGuid.** Authored and serialized. It is regenerated only on editor duplicate or paste, and kept on load, stream-in and PIE duplication.
- **WorldNamespace.** A deterministic GUID derived from the map package, with the PIE prefix stripped. Renaming a map therefore needs a migration. You can set `WorldNamespaceOverride` instead.
- **InstanceScope.** Set by whatever places content inside a repeated level instance. Use `FDocPersistentObjectId::ComposeInstanceScope`.
- **Duplicate ids.** These are logged as errors and are never silently re-assigned.

Level actors (net-startup) count as authored. Any other actor is treated as runtime-spawned unless a spawner calls `MarkRuntimeSpawned(false)` before BeginPlay.

## Lifecycle

| State | Meaning |
|---|---|
| Registered | Live |
| StreamedOut | Unloaded by streaming or travel; its latest state is captured at EndPlay and re-applied when it streams back in |
| Virtualized | Represented by another system (`MarkVirtualized`) |
| IntentionallyDestroyed | A tombstone, written only by `DestroyPersistently` / `MarkPersistentlyDestroyed` (authority only). Returning actors are suppressed |

EndPlay alone never counts as destruction.

## Participants

Participants implement `IDocSaveParticipant` on the actor or on any of its components. The contract has three calls:

- **Capture.** Returns typed `FDocSavePayload`s, each with a stable `ComponentKey`, a `Version` and an `FInstancedStruct`. Capture must have no side effects.
- **Restore.** Runs under the restore barrier (`IsRestoring()`).
- **ResolveReferences.** A second pass that runs after every object has been restored.

Two allowlists apply, both set in Project Settings → Plugins → Doc Save or in code:

- **Payload structs.** A payload struct must be on the allowlist to be saved or loaded. Use `AllowedPayloadStructs` or `AllowPayloadStruct`.
- **Runtime-spawned classes.** These are recreated on load only if allowlisted (`AllowedRuntimeSpawnClasses` or `AllowRuntimeSpawnClass`). Restore never loads a class named by save data. Allowlisted classes must already be loaded, or the record is skipped and reported.

## Save and load

- **Save** (`SaveToSlot`, async). Captures everything on the game thread in one pass, including retained records of unloaded objects, and encodes an immutable body. A worker then assigns the revision, wraps it in the envelope and writes it.
- **Revisions.** The revision is always above the one the slot already holds, including slots saved in earlier sessions. An older capture can never replace a newer save.
- **Concurrent saves.** Writes to one slot are serialized. Requests that arrive during a write are coalesced into the next one, and every caller learns the revision it actually got.
- **Envelope.** Magic, format version, header, then the payload. The payload is zlib-compressed by default. CRC32 of the body detects corruption; it does not prove authenticity.
- **Load** (`LoadFromSlot`, async). Reads and validates on a worker with bounded reads, size limits and integrity checks. Decoding and migration happen on the game thread. Every record is validated before anything is replaced, so a bad file never clears progress. If the current file is unusable, the `.bak` is tried and the result's diagnostic says so.
- **Restore.** Applies records to registered participants, destroys tombstoned actors and recreates allowlisted runtime spawns with their saved ids. It then resolves references and releases the barrier.
- **Migrations.** `RegisterCurrentVersion(TypeId, N)` plus `RegisterMigration(TypeId, From, Func)` upgrade stored bytes one step at a time. Payloads newer than the build supports are rejected, not guessed at.
- **Feature records.** `SetFeatureRecord` / `GetFeatureRecord` store feature-owned `FDocFeatureRecord`s without Save knowing what they mean.
- **Blueprint.** The async nodes **Save Game To Slot Async** and **Load Game From Slot Async** each fire exactly one of OnSuccess or OnFailure.
- **Slot names.** 1–64 characters of `[A-Za-z0-9_-]`. Paths and traversal are refused.

`FDocLocalFileSaveBackend` writes a temp file, verifies it, moves the current file to `.bak`, then moves the temp file into place. It does **not** advertise atomic replace, because a rename-based commit has not been fault-tested as atomic on each platform.

## Tests

Tests live under `Doc.Save.*`: Envelope.RoundTrip, Envelope.RejectsBadFiles, SlotNames, LocalBackend.FaultInjection, RoundTrip, StreamedOutRetention, Tombstone, RuntimeSpawnRestore, Migration, Revisions, BadFileKeepsProgress and PayloadAllowlist.

- **How they run.** Subsystem tests use an in-memory backend injected with `SetSubsystemOverrideForTesting`, and the blocking save/load entry points.
- **Not covered: async coalescing.** Async coalescing across overlapping requests is not covered by automation.
- **Not covered: real crash and power-loss interruption.** This needs a packaged build on target hardware.
