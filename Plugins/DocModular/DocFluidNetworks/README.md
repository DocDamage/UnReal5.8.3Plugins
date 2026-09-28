# DocFluidNetworks

Gameplay liquid networks: reservoirs, pipes, valves, pumps, leaks and external supply, with conserved single-liquid volume (Modules 21–40 handoff, Section 5; Module 23). Units are litres and seconds. This is a bounded gameplay-rate model, not a pressure solver.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocFluidNetworkSubsystem` (World) |
| Components | `UDocFluidReservoirComponent`, `UDocFluidValveComponent`, `UDocFluidPumpComponent` |
| Depends on | DocModularCore; engine modules only |
| Not included | Power bridge for pumps, mixing, temperature, pressure, zero-volume junctions, network replication |

## Step

Each fixed step does the following:

1. Snapshots volumes and free capacity.
2. Proposes a transfer for each eligible edge.
3. Scales competing outflows to the source's initial volume.
4. Scales competing inflows to the destination's initial free capacity.
5. Commits all transfers together, with a declared one-step transport delay.
6. Applies external supply and leaks.

Order is always stable: by id, never by registration or hash order.

`TotalAfter = TotalBefore + ExternalInflow - ExternalOutflow - Leaks - Drains - Discarded - NetQuarantined` (`FDocFluidLedger::VerifyConservation`).

## Time

`StepSimulation` runs whole steps of `FixedStepDeltaTime`, up to `MaxSimulationStepsPerCatchUp` per call. Time that doesn't fill a step, or goes over the budget, is kept as lag (`GetSimulationLagSeconds`); `DiscardSimulationLag` is the explicit offline policy. A long gap never becomes one long step. Invalid deltas do nothing.

## Controls and diagnostics

- **Controls.** Valves must be in [0,1]. `SetPumpEnabled` and `SetEdgeBlocked` each affect only their own edge. Stored volume is never erased by a control.
- **`GetEdgeStatus`** explains why an edge does or doesn't flow:

  | Status | Reason |
  |---|---|
  | Enabled | — |
  | Disabled | pump off |
  | Blocked | — |
  | UnavailableSupply | `UnresolvedEndpoint`, `SuspendedEndpoint`, `SourceEmpty` |
  | Faulted | `FluidMismatch`, `UnknownEdge` |

- **`QueryTransferRates`** returns the last step's transfers, with litres per second.

## Validation

Refused rather than clamped or overwritten:

- duplicate reservoir or edge ids
- a missing liquid type
- non-finite, negative or over-capacity values
- self-loops
- edges between different liquids, including a reservoir registration that would create one

## Lifecycle and persistence

- **Removal.** `UnregisterReservoir` records the contents as an explicit discard.
- **Stream unload.** When a component ends play, its reservoir is suspended: contents are kept, and no transfers, supply or leaks touch it. A reloaded component resumes it without overwriting the retained volume. An edge to a missing reservoir stays suspended until that reservoir registers.
- **`MigrateCapacity`.** Shrinking a tank below its contents either spills the excess to the external-outflow sink or quarantines it (`QuarantinedVolume`). Both are recorded. `ReleaseQuarantine` returns quarantined liquid as space allows.
- **`CaptureState` / `StageRestore`** save volumes, quarantine, controls, rates, simulated time and lag.
  - Restore validates everything first.
  - For a tank that is currently registered, the authored capacity wins, and saved volume above it is quarantined.
  - Restore starts a new accounting period and replays nothing.

## Tests

`Doc.Fluid.*`: SingleTransfer (FLU-01), CompetingOutflows (FLU-02), CompetingInflows (FLU-03), OrderIndependence (FLU-04), ValvePumpBlockage (FLU-05), ExplicitSourcesSinks (FLU-06), LoopAndHitch (FLU-07), UnresolvedEndpoint (FLU-08), CapacityMigration (FLU-09), InvalidNumbers (FLU-10).
