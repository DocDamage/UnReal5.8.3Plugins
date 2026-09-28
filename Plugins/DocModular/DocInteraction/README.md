# DocInteraction

Universal interactions between arbitrary actors (Rev 2 handoff, Section 5).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags; **EnhancedInput** plugin (only for the optional input adapter component) |
| Not included | Event/Sequence/Inspection actions (bridges `DocInteractionEvents`, `DocInteractionSequences`, `DocInteractionInspection`), Smart Objects and GAS bridges, network request transport |
| Network | Authority-aware only (`bRequiresAuthority`, Authority condition). No RPC transport; INT-08 not started |

## Pieces

| Type | Role |
|---|---|
| `UDocInteractableComponent` | On any actor. Inline definitions and/or a shared `UDocInteractionProfile` data asset. Enable/disable, priority, interaction point offset |
| `UDocInteractorComponent` | On any actor (Character not required). Instanced target providers, deterministic candidate selection with focus hysteresis, options view model, Begin/End/Cancel, Next/Previous |
| Providers | Line Trace, Sphere Trace, Capsule Trace, Overlap, Cursor Trace, Proximity (registry, no physics), Explicit Target |
| Conditions | Required Tags, Blocked Tags, Distance, Facing Angle, Cooldown, One Time Use, Authority, Required Class/Interface, custom (C++/Blueprint subclass) |
| Actions | Apply Tag Delta (needs `IDocMutableGameplayTagProvider`), Notify Receiver, Set Target Enabled, Spawn Actor, Destroy Target, Attach/Detach, Play Sound, Request Animation (adapter), custom |
| `UDocInteractionSubsystem` | Sessions, reservations, revalidation, commit, compensation, use records |
| `IDocInteractionReceiver` | Actor-side notifications (phases and receiver/animation actions) |
| `UDocInteractionInputComponent` | Binds your Enhanced Input actions to an interactor. Never touches mapping contexts |

Modes: **Instant**, **HoldToComplete** (gameplay clock; progress 0..1; commit after revalidation), **Continuous** (actions at start; ends on release/MaxDuration), **Repeated** (actions every interval; MaxRepeats). Concurrency: ExclusiveTarget, ExclusiveDefinition, Shared; always one session per interactor.

Actions are **not transactional**: they run in order, fail fast, report `ExecutedActions`, and only reversible actions are compensated (`CompensatedActions`). Spawn/Destroy are irreversible.

## Quick start (Blueprint)

1. Add **Doc Interactor** to your pawn (or any actor); add a Line Trace or Overlap provider.
2. Add **Doc Interactable** to a door; add a definition `Open` with a Notify Receiver action; implement **Doc Interaction Receiver** on the door.
3. Add **Doc Interaction Input** to the pawn, assign your `IA_Interact`, call **Bind To Input Component** in Setup Player Input Component.
4. Bind `OnFocusChanged` / `GetFocusedOptions` to your own prompt widget.

## Tests

`Doc.Interaction.*`: BasicInteraction, ConditionFailure, HoldInteraction, ContinuousAndRepeated, Concurrency, PartialFailure, CandidateSelection.

## Known limitations

- Trace providers need real collision; automated tests cover Proximity/Explicit only.
- Cooldown/one-time records are runtime only (persistence needs a Save bridge).
- The debugger panel is not implemented; session info and candidates are queryable.
