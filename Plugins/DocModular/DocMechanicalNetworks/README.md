# DocMechanicalNetworks

Gameplay drive trains: shafts, gears and belts with signed ratios, drive sources with torque capacity, clutches, and reflected loads (Modules 21–40 handoff; Module 22). The model is a rooted tree solved each step. It does not simulate rigid-body dynamics.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocMechanicalNetworkSubsystem` (World) |
| Components | `UDocMechanicalNodeComponent`, `UDocDriveSourceComponent`, `UDocClutchComponent` |
| Depends on | DocModularCore; engine modules only |
| Not included | Presentation adapter (mesh rotation driver), load component, power bridge, network replication |

## Topology

Edits are validated before any state changes. A rejected edit leaves the network untouched and records the reason in `GetLastTopologyError()`. `ConnectDrive`, `DisconnectDrive` and `SetClutchState` accept an optional `ExpectedRevision`; a stale revision is refused.

The following are rejected:

- a node with two parents
- a cycle
- a drive source attached anywhere but a tree root
- more than one driver on the same root

Children are solved in stable id order. `GetTopologyRevision()` increments on each committed edit.

## Solve

- Speed propagates from the root through signed edge ratios. A negative ratio reverses direction.
- Loads are reflected back through each ratio to the source (`QueryReflectedLoads`).
- **Stall:** a source whose demand exceeds its capacity latches Stalled. It recovers only once demand falls below capacity by the hysteresis margin. `OnDriveStalled` and `OnDriveRecovered` fire once per transition.
- **Overspeed:** a node driven above its `MaxOperatingSpeed` is Overloaded, and its actual speed is zero. `OffendingEdgeIds` names the edges involved.
- **Missing or suspended child:** handled according to `MissingLoadPolicy`.
  - `FailClosed` (the default) stalls the branch with the reason MissingEndpoint.
  - `Suspend` marks the branch Suspended.
- **Clutch:** engagement is validated like any topology edit. Disengaging decouples the subtree. `OnClutchChanged` fires after the commit.

All events are broadcast only after state has been committed.

## Time and visuals

`StepSimulation` wraps each node's phase angle and accumulates `AccumulatedRevolutions`. For gameplay, use revolutions, not phase.

`FreeVisualCoast` is visual-only: after drive is lost, the visible phase keeps turning and decays over `VisualCoastSeconds`, while the gameplay speed is already zero.

## Streaming and save

- A node component on an unloaded stream level suspends its node, keeping its data, rather than deleting it. Reloading resumes the node.
- `CaptureState` records phases, revolutions, loads, clutch engagement and source speeds, capacities and enabled flags.
- `StageRestore` validates the whole snapshot before applying anything, and replays no events.

## Tests

`Doc.Mechanical.*`, 10 tests: MEC-01 to MEC-10.
