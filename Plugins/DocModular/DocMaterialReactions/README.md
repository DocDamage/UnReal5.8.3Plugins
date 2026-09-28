# DocMaterialReactions

Authored environmental reactions on material-bearing objects: burning, extinguishing, melting, drying and wetting (Modules 21–40 handoff, Section 7; Module 25). The plugin owns material facts. It does not own VFX, audio, character ailments or damage. It is not a chemistry or heat-diffusion simulator, and it does not claim conserved heat or mass.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocMaterialReactionSubsystem` (tickable World subsystem, fixed step) |
| Component | `UDocReactiveMaterialComponent` |
| Assets | `UDocMaterialProfile`, `UDocMaterialReactionDefinition` |
| Bridge hook | `IDocMaterialExposureProvider` (not polled by the base; adapters push samples) |
| Depends on | DocModularCore; engine modules only |
| Not included | Weather/Fluid/VFX/audio adapters, material-instance presentation adapter, network replication, profile inspector UI |

## Facts, not one enum

`FDocMaterialState` holds independent quantities: Moisture [0,1], Fuel [0,MaxFuel], PhaseFraction [0,1] and CharAmount [0,1], plus derived tags. An object can be wet and charred at once.

Reaction instances (`FDocReactionInstance`) are separate from the facts, and presentation is separate from both. State tags are always rebuilt as the profile's base tags plus the `ResultingTags` of each Active reaction.

## Exposure

Samples are owned by their source (`SourceId`) and carry the following:

- channel, intensity and weight
- expiry on the simulation clock
- sequence and revision
- propagation generation

Removing one source removes only that source's contribution. A sample whose `Sequence` is lower than the stored one for that source is refused as `StaleSequence`.

Each channel declares its units (`NormalizedIntensity` or `Physical` with a unit label), its combination rule and its cap (`FDocExposureChannelSpec`). Channels without a spec use the profile defaults.

| Rule | Combination |
|---|---|
| Sum | total of all samples |
| Max | largest sample |
| Weighted | weighted mean: Σ(I·w) / Σw |

Each sample and the combined value are clamped to the cap before evaluation.

## Step order

Each step runs in this order. It is deterministic and does not depend on authoring or arrival order.

1. Expire exposure samples and suppressions on the simulation clock.
2. Snapshot every channel value. Changes made by event listeners apply next step.
3. **Pass 0 — wetting-class reactions** (`MoistureGainRate > 0`): evaluate them, resolve their groups, and apply their moisture gain.
4. **Pass 1 — everything else**, evaluated against the *post-wetting* facts, in this order:
   - Priority, highest first; ties broken by ReactionId.
   - Blocks: suppressed, fuel or moisture depleted, `Moisture >= InhibitAtMoisture`.
   - Hysteresis: an active reaction ends only below `DeactivationThreshold`, unless it is `bSelfSustaining`.
   - Dwell: continuous seconds at or above `ActivationThreshold`. The dwell resets whenever exposure dips below the threshold.
   - Exclusive groups: the first reaction in order claims the group. The others become `Outcompeted` and consume nothing.
   - Per-object active limit (`MaxActiveReactionsPerObject`): reactions that were already active keep their slots.
   - Deltas are applied together, bounded to valid ranges. Consumption uses only what exists.
   - A reaction whose resource ran out ends as `Depleted`.
5. Commit the new state once, with one revision bump. Then broadcast Ended, Started and StateChanged events.

**Boundary rule (MAT-03):** when heat and water arrive in the same step, wetting applies first. A reaction inhibited by the resulting moisture never starts. A burning reaction crossing the inhibit level ends as `Inhibited`, with the cause `MoistureInhibit`, and consumes no fuel that step.

Fuel is finite. Extinguishing or reigniting never regenerates it; only `InitializeFromProfile`, an explicit authoring reset, does.

## Events and safety

- Transitions carry TransitionId, ReceiptId, cause, new state, the committed material revision, and the activation's totals (fuel, moisture, phase).
- Events fire only after commit, so a listener failing cannot roll back consumption.
- Listeners cannot re-enter `StepSimulation` (refused with `RecursionLimit`). Nested mutations are limited to `MaxNestedTransitionDepth`.
- Gameplay mutation requires authority on the owning actor.
- Refusals set `GetLastRejectReason()`. Subsystem calls return `FDocSystemResult`.

## Suppression

Suppression is source-owned per reaction; a duration of 0 or less means permanent until that source revokes it. A temporary weather suppression expiring cannot lift a script's permanent inhibit. Suppressing an Active reaction ends it (`Suppressed`).

## Propagation

Propagation is a stateful exposure transfer through the validated component API. It is never an effect actor igniting things directly.

- **Candidates:**
  - explicit logical contacts first (`AddContact`, symmetric and deduplicated)
  - then registered objects within the radius, ordered by distance, then id
  - there is no actor iteration
- **Idempotent transfers:**
  - each source/reaction pair owns one sample id per target (`Prop:<source>:<reaction>`), refreshed each step, so repeated contacts never stack
  - repeated requests within a step are deduplicated
  - `PropagationCooldownSeconds` rate-limits refreshes
- **Limits:**
  - `MaxNeighborsPerSource`, `MaxQueuedTransfersPerStep` and `MaxWorkPerStep` (candidate checks)
  - a source that does not get its full turn is *deferred* and processed first next step, so it is not starved or re-triggered
  - `MaxPropagationGeneration` stops chains
- **Diagnostics:** `GetLastPropagationStats()`.
- **Unloaded objects:**
  - unregistering removes the object's contacts and withdraws the exposure it was providing to others
  - an unloaded object is `NotFound` through the id API, and its stale component refuses exposure (`NotRegistered`)

## Time

When `bAutoStep` is on, `Tick` runs steps of `FixedStepSeconds`, at most `MaxStepsPerTick` per frame. Excess time is dropped and reported by `GetDroppedSimulationSeconds()`. Tests and custom drivers call `StepSimulation(Delta)` directly.

## Persistence

`CaptureMaterialState` produces an `FDocMaterialSnapshot`, which includes:

- schema version, and the profile's `ContentVersion`
- the material facts
- live reaction instances, with progress, TransitionId and ReceiptId
- suppressions, with remaining time

Exposure samples are live source handles and are not saved.

`StageRestore` validates the whole snapshot first and refuses any of the following:

- a schema or content version mismatch
- facts out of range
- unknown or duplicate reactions
- two active reactions in one exclusive group
- an active reaction without its resource

It then restores facts and reactions and emits a single `OnMaterialStateChanged` with the cause `Restored`, for presentation to rebuild. It emits no reaction starts, makes no consumption, and issues no new receipts.

## Networking

Reactions are server-authoritative. Replication is **not implemented** in this revision. Clients may interpolate cosmetic values, but must never consume fuel or ignite anything.

## Tests

`Doc.Material.*`, 10 tests: MAT-01 to MAT-10.
