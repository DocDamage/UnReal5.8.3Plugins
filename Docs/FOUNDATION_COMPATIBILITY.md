# Foundation Compatibility Audit (Milestone 5.0)

Audit of the Modules 11–20 handoff (Section 14.1, Appendix D) against what exists in this repository.

- **Audited:** 2026-09-27
- **Repository state:** only `DocModularCore` exists (M1.1). It is **Implemented / Unverified**: it has never been compiled or tested. None of modules 1–10 beyond Core, and none of modules 11–20, exists.
- **Method:** source reading only. No build ran, so "compatible" means compatible by source inspection, not proven.

## D.1 contract-by-contract

| Contract (Appendix D.1) | What exists | Decision | Evidence / blocker |
|---|---|---|---|
| `FDocSystemResult` | `Public/DocSystemResult.h`: closed `EDocResultOutcome` + extensible `ErrorTag` + `UserMessage` + `Diagnostic` + `OperationId` | **Reuse.** Missing expansion outcomes were added (D-012); see *Result outcome gap* below. Expansion features add child error tags (`Doc.Error.<Feature>.*`), never a second result type | Source only; not compiled |
| `FDocGameplayContext` | `Public/DocGameplayContext.h`: world, instigator, target, local player, tags, correlation ID; weak refs | **Reuse.** Has no owner scope; the expansion's `FDocOwnerScope` is a separate value (below), not a new context type | Source only |
| `FDocPersistentObjectId` | `Public/DocPersistentObjectId.h`: (WorldNamespace, InstanceScope, LocalObjectGuid), `ComposeInstanceScope` v1 | **Reuse as is.** Same triple the expansion requires (Section 2.4) | Golden-value test authored, not run |
| `FDocWorldObjectReference` | Same header: ID + transient weak cache, equality by ID | **Reuse as is** | Source only |
| Transient handle primitives | `Public/DocRequestHandle.h`: `FDocRequestHandle` (OperationId + Epoch, Transient), `TDocHandleTable` with scope keys, idempotent release | **Reuse.** Matches 2.5 "operation ID, owner/context generation". Queryable request *state* (Pending/Running/…) stays per feature, as in Rev 2. Feature handles wrap it as typed structs | Tests authored, not run |
| `IDocPersistentIdentity` | `Public/Interfaces/DocPersistentIdentity.h` | **Reuse as is** | Source only |
| `IDocGameplayTagProvider` (+ mutable) | `Public/Interfaces/DocGameplayTagProvider.h`: read interface; separate `IDocMutableGameplayTagProvider` with owner-attributed deltas | **Reuse.** Matches 2.6 "grant/remove require a declared mutable provider … source-owned grant semantics" | Source only |
| `IDocWorldStateProvider` | `Public/Interfaces/DocWorldStateProvider.h`: `QueryDocWorldState` → `Unknown/No/Yes` | **Reuse** for world-state queries. It is not the expansion's general condition outcome (below) | Source only |
| `IDocPlayerControlProvider` | `Public/Interfaces/DocPlayerControlProvider.h` + `FDocControlClaimRequest`; tags `Doc.Control.Camera/Input/Input.Movement/Input.Look/Pause/HUD` | **Reuse.** Expansion needs cursor and focus claims (2.5, 13.4): add `Doc.Control.Cursor` and `Doc.Control.Focus` tags when UI or Dialogue presentation is built. Additive | Source only |
| Feature snapshot / DocSave adapter, restore barrier | **Absent.** DocSave (Rev 2 M2.3) not started | **Blocked on foundation.** Expansion bases define their own capture/stage/apply/finalize contracts and work in memory without DocSave (2.8) | DocSave not implemented |
| `FDocOwnerScope` (expansion proposal) | **Absent** | **Add to Core later, when the first consumer needs it** (MapNavigation 5.2 is the first owner-scoped base). It is shared by ≥5 expansion features, so Core is the right home. Proposed shape: `ScopeKind` (PlayerProfile / SharedWorld / Party / Session), `SubjectId` (FGuid), `CampaignNamespace` (FGuid) | Not needed by SurfaceFeedback (5.1) |
| Pure condition outcome (2.6) | **Absent**. `EDocTriState` is close but carries no evidence, revision, or reason | **Add with the first condition consumer** (Dialogue 6.2, maybe Schedules 6.1): `Satisfied / Unsatisfied / Unavailable` + reason tag + revision. Candidate for Core because Dialogue, Objectives, Unlocks and Schedules all need it | Not needed yet |
| Record revision + epoch (2.4) | Partial: handle tables have epochs; there is no durable revision type | **Add with the first persistable expansion base.** Keep separate from handle epochs, which are process-local and not durable | Not needed yet |
| Effect key + receipt (2.7, 13.3) | **Absent** | **Add when QuestObjectives (6.3) or InventoryItems (7.2) is implemented.** The key's value type may go in Core (shared by Quest, Inventory, Unlock, Dialogue, Knowledge); outbox/receipt storage stays in each feature | Not needed yet |

## Result outcome gap

Section 2.9 lists outcomes the current enum cannot express:

| Expansion outcome | Current Core | Proposed |
|---|---|---|
| Success | `Succeeded` | — |
| NoChange | missing | Add `NoChange`. **Decision needed:** should `IsSuccess()` be true for it? Proposed: yes, plus a new `IsChanged()` helper, since KNO-01 treats a duplicate discovery as a non-error |
| InvalidInput | missing (`InvalidConfiguration` covers authored data only) | Add `InvalidInput` (bad request values), distinct from authored configuration |
| NotFound, NotReady, Unsupported, PermissionDenied, Cancelled, TimedOut | present | — |
| Unavailable | missing | Add `Unavailable`: an optional provider or dependency is absent at runtime. `Unsupported` stays for capabilities that aren't installed or implemented |
| Conflict | missing | Add `Conflict`: stale revision, concurrent edit, reservation held |
| Execution / storage failure | `Failed` | Keep `Failed`. Express storage failure as a `Doc.Error.Storage` child tag, not a new enum value |

**Status: applied 2026-09-27 (D-012 accepted).** `NoChange` counts as success (`IsSuccess()` true, `IsChanged()` false). Implemented in source; not yet compiled or tested.

## Other findings

1. **Bridge location conflict.** Rev 2 §2.1 shows a root `Bridges/` staging folder; the expansion §23 puts bridges in `Plugins/DocModularBridges/`, which the dev host discovers. Adopted: `Plugins/DocModularBridges/` (D-013). The empty root `Bridges/` is kept only as a distribution-staging location.
2. **Execution order vs module numbers.** The modules are numbered Map = 11, Weather = 12, Surface = 13, but §14.2 builds SurfaceFeedback first (5.1), then Map (5.2), then Weather (5.3). We follow §14.
3. **Companion filename.** The expansion refers to `UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff_v2.md`. This repository stores that document as `Docs/UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff.md` (same Revision 2 content).
4. **Isolated SurfaceFeedback fixture** (a §14.1 exit item) is deferred, and so is all expansion code, by the ordering decision D-011.
5. **Engine gate** is unchanged: 5.8.3, CL 58210709 (`ENGINE_COMPATIBILITY.md`). The expansion's D.2 native-API ledger (Physical Materials, CommonUI, Enhanced Input user settings, `UGameUserSettings`, Smart Objects, StateTree, Mass, Niagara) has **not** been started. Fill it per integration when its milestone begins.

## Milestone 5.0 exit status

| Exit item (§14.1) | State |
|---|---|
| Core-only consumer build | **Not Run**. Waiting on `Scripts/Verify-M1.1.ps1` |
| Proposed compatibility changes recorded | Done (this file; D-011 to D-015) |
| Exact blocker/evidence state | Done: foundation M1.1 unverified; modules 1–10 not started |
| Isolated SurfaceFeedback fixture ready | Deferred (D-011) |
| Existing source intact | Yes. This audit changed no source files |
