# DocWorldActivation

Gameplay-level LOD for **loaded** logical objects (Rev 2 handoff, Section 14). Streaming decides whether content is loaded; Activation decides how expensive its loaded representation is.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Director | `UDocWorldActivationSubsystem` (world) |
| Objects | `UDocWorldActivationComponent` + `UDocActivationPolicy` + `UDocActivationProfile` |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | `DocWorldActivationISM` (ISM/HISM adapter, ACT-06), `DocObjectPool` (ACT-07), Mass integration, Regions/Events policy bridges, network replication (ACT-09) |

## Tiers

`Activation.Dormant < Representation < Lightweight < Active < Full`. The order is set explicitly by `EDocActivationTier`; it is never derived from the lexical order of tag names.

| Tier | Base behaviour |
|---|---|
| Dormant | Minimal loaded-actor activity, set by the profile (for example tick off, hidden). A form with no actor at all needs a virtualization adapter. |
| Representation | Requires an `IDocActivationRepresentationAdapter`. When none is installed, the object gets the policy's `UnsupportedFallbackTier` and a diagnostic. The actor is never deleted. |
| Lightweight | Reduced tick interval for actor and components. |
| Active / Full | Host-defined normal state (tiers with no profile entry restore owned overrides). |

The base adapter changes **only** the resources a component declares in `Controlled`:

- actor tick
- component tick
- visibility
- collision

Collision is off by default, because a distant barrier may still need it. AI, physics and animation go through participants (`IDocActivationParticipant::OnActivationTierChanged`), since only the host knows how to change those in an owned, composable way.

## Policies

- **AlwaysActive, DistanceBased, Manual and Custom** (Custom calls `IDocActivationParticipant::GetCustomDesiredTier`).
- **VisibilityBased and InteractionBased** need provider bridges. Without one, they report a diagnostic and use `DefaultTier`.

Distance bands are in centimetres and each band has separate promote and demote thresholds (hysteresis). The defaults are illustrative only; measure your own content. `MinDwellSeconds` stops oscillation. Pins that raise a tier, and manual requests, bypass the dwell time.

**Sources** are every player's pawn or view (on a server that means every player, so one client can never demote shared simulation), plus any `AddRelevanceSource` components such as vehicles, cameras and cinematic targets. Adding a source or calling `RequestPriorityReevaluation` (use it on teleports) re-evaluates nearby objects first, using a spatial cell index.

**Pins** carry an owner, a reason, a minimum tier and an optional duration. They are released when their owner dies or when they expire, so nothing stays pinned forever.

## Transitions and budgets

Each transition follows this pipeline:

`Evaluate desired → queue → guard → prepare destination → validate → commit swap → release source`

- **Budgets:** `MaxEvaluationsPerTick` (round-robin, with prioritized entries first), `MaxTransitionsPerTick` and `EvaluationIntervalSeconds`.
- **Adapter failures:** when an adapter's prepare or commit fails, the prior representation is kept or rolled back, and the failure is reported.
- **Owned overrides:** the system records the original host value and the value it set. On restore it writes the original back **only if** the current value is still the one it set, so a change the host made in the meantime is never overwritten by a stale snapshot.
- **Unload vs destruction:** unregistering on EndPlay (stream unload or travel) drops the object's pins and records; it is never treated as destruction. Optimization transitions never imply persistent destruction either.

The debugger (`GetDebugInfo` and `GetStats`) reports:

- logical id
- current and desired tier
- policy
- nearest source
- pins
- representation
- queued transition
- transitions per second
- per-tier counts
- evaluation cost

## Tests

The tests are under `Doc.Activation.*`: DistanceAndManual, HysteresisAndDwell, SourcesAndPins, OwnedStateAndAdapters and BudgetsAndUnload. They use fixed source locations and a test adapter. ACT-05 performance measured under realistic load, ISM identity (ACT-06), pooling (ACT-07) and network authority (ACT-09) remain open.
