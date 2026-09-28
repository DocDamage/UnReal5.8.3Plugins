# DocModularCore

Shared low-level contracts for the DocModular gameplay plugin suite. Core contains **no gameplay feature**: no event bus, save system, interaction framework, or service locator.

**Status:** Implemented / Unverified. Source exists; it has **not yet been compiled or tested** on Unreal Engine 5.8.3 (see `Docs/DEVELOPMENT_STATUS.md` at the repository root).

| | |
|---|---|
| Engine | Unreal Engine 5.8.3 (CL 58210709), Win64. Other versions untested. |
| Distribution | Source only. Blueprint-only consumers still need a compiler or a matching precompiled build. |
| Essential dependencies | Engine modules Core, CoreUObject, Engine, GameplayTags. No plugin dependencies. |
| Optional bridges | None. |

## What it provides

| Type | Purpose |
|---|---|
| `FDocSystemResult`, `EDocResultOutcome` | Typed results; default is `Unset` (a failure), never success-shaped |
| `DocCoreTags` (`Doc.Error.*`, `Doc.Control.*`) | Native error and control-capability tags |
| `FDocGameplayContext` | Explicit world / instigator / target / local-player context; weak refs only |
| `FDocRequestHandle`, `TDocHandleTable<T>` | Transient request handles with stale / cross-scope rejection and idempotent release |
| `FDocPersistentObjectId`, `FDocWorldObjectReference` | Durable (namespace, instance scope, local GUID) identity; deterministic nested-instance scopes |
| `IDocPersistentIdentity` | Object supplies its durable ID without needing DocSave |
| `IDocGameplayTagProvider`, `IDocMutableGameplayTagProvider` | Blueprint-implementable read / authorized-mutation tag contracts |
| `IDocWorldStateProvider` | Narrow tri-state world-state query (not a state store) |
| `IDocPlayerControlProvider`, `FDocControlClaimRequest` | Per-local-player camera/input/pause lease contract |
| `FDocOwnerScope` | Durable owner of feature state (player profile, shared world, party, session) plus campaign namespace (D-015) |
| `FDocConditionResult`, `EDocConditionState` | Pure Satisfied / Unsatisfied / Unavailable condition outcome with reason and revision; `CombineAll` / `CombineAny` |
| `EDocClockDomain`, `FDocRecordRevision`, `FDocFeatureRecord`, `DocCoreSerialization` | Shared clock vocabulary, record revision + epoch, and tagged-property struct encoding for feature save records |
| `FDocEffectKey`, `FDocEffectReceipt`, `FDocReceiptLedger` | Idempotent effect keys and a bounded receipt ledger (retry returns the original; changed payload is a conflict) |
| `FDocControlClaimArbiter`, `UDocReferencePlayerControlProvider`, `UDocPlayerControlSubsystem` | Claim arbitration (priority, then most recent), a packaged reference provider, and the per-local-player provider registry |
| `FDocScopedTestWorld` (`DocCoreTestUtils.h`) | Development-only test world helper (`WITH_DEV_AUTOMATION_TESTS`) |
| `UDocCoreBlueprintLibrary` | Blueprint nodes for the above |

## Install

1. Copy `DocModularCore` into your project's `Plugins/` folder (any subfolder depth, but not inside another plugin).
2. Enable **Doc Modular Core** in the plugin browser, or add `{ "Name": "DocModularCore", "Enabled": true }` to your `.uproject`.
3. C++ consumers: add `"DocModularCoreRuntime"` to your module's dependencies.

Uninstall: remove the plugin; nothing is written to project config.

## Tests

Automation tests live under `Doc.Core.*`. From the repository root:

```powershell
.\Scripts\Build-Host.ps1     -EngineRoot 'C:\Program Files\UE_5.8'
.\Scripts\Run-Automation.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -Filter 'Doc.Core' -ExpectedMinimumTests 12
```

See `Docs/` in this plugin for API, Blueprint, C++, and debugging notes.
