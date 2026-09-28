# DocPowerNetworks

Gameplay-level power: sources, batteries, connected circuits, switches, breakers and consumers, with finite energy accounting (Modules 21–40 handoff, Section 4; Module 22). It does not model voltage, current waveforms or wiring safety.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocPowerNetworkSubsystem` (World) |
| Components | `UDocPowerNodeComponent` and its Source, Consumer and Storage subclasses |
| Data | `UDocPowerNetworkDefinition` |
| Depends on | DocModularCore; engine modules only |
| Not included | Machinery/terminal/optical bridges, Save/Events bridges, network replication, editor previews |

## Graph

- **Nodes and edges** have stable ids.
  - A duplicate node id or edge id is a `Conflict`.
  - An edge must name ports that exist on its nodes.
- **Validation.** Invalid values are rejected, never clamped: non-finite or negative power, availability outside [0,1], efficiencies outside (0,1], energy above capacity.
- **Stale edits.** Structural edits accept an `ExpectedTopologyRevision`; a stale one is refused with `Conflict`.
- **Islands** are built from enabled nodes over closed edges. Open switches and tripped breakers do not conduct.

## Solver

A step first validates. Then, in each island:

1. Consumers are ordered by priority, then tie-break id, then NodeId (never registration or hash order). Each is granted power against the island's total supply: external sources plus what each battery can deliver, limited by its power rating and its stored energy × discharge efficiency.
   - **Binary** consumers get all of their demand or nothing.
   - **Scalable** consumers get their demand, or what's left if that is at least their minimum, or nothing.
   - **Recovery hysteresis.** A load that browned out needs `(1 + RecoveryDropoutHysteresis) ×` its threshold to come back. A running load stays on at the threshold.
2. Grants are paid from external supply first, then from batteries in stable order.
3. Only external supply left after the loads charges batteries. A battery that discharged in a step never charges in the same step.

The per-step ledger balances: `ExternalAccepted + StoredBefore = LoadDelivered + StoredAfter + ConversionLosses + ExplicitDiscard`. Unused source capacity is not counted as accepted energy.

## Stepping and time

`StepSimulation(Delta)` runs sub-steps of at most `MaxStepSeconds` (default 1 s), up to `MaxCatchUpSteps` (default 10) per call; set both with `SetStepLimits`.

- **Lag.** Time beyond that budget is kept as lag (`GetSimulationLagSeconds`) and processed by later calls. `DiscardSimulationLag` is the explicit offline policy.
- **Refused steps.** A refused step (non-finite or non-positive delta, unsupported protection) changes nothing.

## Breakers

- **Trip rule.** A breaker trips when the pre-allocation demand of its protected branch (or of its whole island when no branch is given) stays above the threshold for `TripDurationSeconds`. A dip below the threshold restarts the timer.
- **Reset** is refused until the cooldown has run.
- **Radial branches only.** A protected branch must be reachable from the rest of the network only through its breaker. The following return `Unsupported` and change nothing:
  - declared meshed protection
  - a detected mesh
  - a later edit that would create one

## Lifecycle and persistence

- **Stream unload.** When a component is streamed out (`RemovedFromWorld` / `LevelTransition`), `bRetainOnUnload` suspends its record: energy, edges and latches are kept, the node leaves every island, and the actor is not kept alive. A reloaded component with the same NodeId re-binds without overwriting the retained energy.
- **Destruction** unregisters the node.
- **Snapshots.** `CaptureNetworkState` / `RestoreNetworkState` cover:
  - battery energy
  - switch states
  - breaker latches, cooldowns and overload timers
  - the last committed step
  - pending lag

  Restore validates everything before applying anything, and never runs extra steps.

## Tests

`Doc.Power.*`: ConnectivityIslands (PWR-01), PriorityAllocation (PWR-02), BatteryConservation (PWR-03), NoChargeDischargeLoop (PWR-04), BinaryAndScalableLoads (PWR-05), BreakerTripReset (PWR-06), UnsupportedProtection (PWR-07), TopologyDuringSolve (PWR-08), UnloadRestore (PWR-09), HitchAndBounds (PWR-10).
