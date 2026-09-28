# DocAcousticSpaces

Authored acoustic spaces, a portal graph, strongest-path transmission, and a claimed single-listener parameter pipeline (Modules 21–40 handoff, Section 9; Module 27). This plugin is not a wave simulator, and it does not extract rooms from geometry automatically.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocAcousticSpaceSubsystem` (tickable World subsystem) |
| Components | `UDocAcousticSpaceComponent`, `UDocAcousticPortalComponent`, `UDocAcousticEmitterComponent` (all self-register) |
| Adapters | `IDocAcousticPlaybackAdapter`, `UDocAcousticReferencePlaybackAdapter` (records applied values) |
| Depends on | DocModularCore; engine modules only. No DocRegions, no DocAdaptiveAudio. |
| Not included | Audio-component backend adapter, reverb routing, independent per-listener mixes, debug overlay UI |

## Topology

| Element | What it holds | Validation |
|---|---|---|
| Space | Stable `SpaceId`, bounds and priority, from a component or registered programmatically | A duplicate id gets `Conflict`, and invalid bounds are refused |
| Portal | Two endpoint spaces; `Openness`; open/closed transmission and cutoff; a position; enabled state | Refused: self-loops, non-finite values, `Min > Max`, a duplicate id |

- **Disabled portals:** a disabled portal, or one whose effective gain is zero, is a *blocked edge*. It is left out of the solver entirely.
- **`ValidateTopology`:** reports portals that reference unknown spaces, and equal-priority overlaps (ambiguous membership).

**Membership:**

- The highest-priority containing space wins; ties go to the lexical id.
- Emitters and listeners queried by id keep a tracked membership with `MembershipHysteresis`, so walking through a doorway does not toggle.
- `ExplicitSpaceId` on an emitter skips location-based resolution.

## Strongest path

Dijkstra over (space, hop-count) states, which is exact under `MaxHops`.

- **Edge cost:** `-ln(gain) + DistanceCostPerUnit · segment`. The cost is always nonnegative.
- **Ties:** broken by fewer hops, then lexical space id, then lexical portal id.
- **Gain:** the product along the chosen path. Parallel routes are **not** summed; there is no fake interference.
- **Cutoff:** the most restrictive portal cutoff on the path. This is an authored approximation.
- **Debug information:** results list each portal's gain and cutoff stage, the route distance through portal positions, the topology revision, timestamps, and a reason code. `GetDebugPath(Emitter, Listener)` returns the last result for that pair.

| Outcome | Status / Reason | Gain |
|---|---|---|
| Same space | `SameSpace` | 1 |
| Route found | `Connected` | product of portal gains |
| No route | `Blocked` / `NoRoute` | `DisconnectedFloorGain` |
| Route longer than `MaxHops` | `Blocked` / `HopLimit` | floor |
| Outside every space | `Unresolved` | floor, or mapped to `ExteriorSpaceId` with `TreatAsExterior` |
| Visited budget hit | `BudgetExceeded`, `Failed` result | floor |
| Visited budget hit, approximation allowed | `Connected` / `BudgetApproximate`, `bIsApproximation` | best tentative route |

No outcome ever falls back to full-volume direct sound.

**Caching:**

- Results are cached per (emitter space, listener space, limits) for the current topology revision.
- Any portal or space change, including a portal component's `SetOpenness` or `SetPortalEnabled`, bumps the revision and invalidates the cache.
- Cached blocked results always use the current floor settings.
- Nothing is cached when distance contributes to cost.

## Composition

- `TransmissionGain` is **portal transmission only**. Distance attenuation stays with the emitter's native attenuation settings.
- `TotalDistance` is reported for adapters that prefer route length. An adapter must use one distance model, never both, so the same wall is not attenuated twice.
- The emitter keeps the host-owned `BaseGain` and `BaseCutoffHz` separate from the acoustic-owned `TransmissionMultiplier` and `RestrictiveCutoffHz`.
- The target is always recomputed as `BaseGain × TransmissionMultiplier`, so repeated updates never compound, and a host fade applied mid-smoothing is honored.

## Claims and playback

- Only emitters holding an acoustic claim are ever written to the adapter. There is one claim per emitter; a repeat request returns the same claim as `NoChange`.
- `AdvanceSmoothing(dt)`, or `Tick` when `bAutoUpdate` is on, does three things:
  1. Re-evaluates up to `MaxEmitterEvaluationsPerUpdate` claimed emitters against the **one mix listener**, staggered round-robin.
  2. Smooths each claimed emitter over time (exponential approach). The first evaluation, a teleport beyond `TeleportDistance`, and a listener jump all snap instead.
  3. Applies the result through the adapter.
- Releasing a claim, unregistering an emitter or destroying it removes the acoustic influence. The adapter then resets to the emitter's *current* baseline, not a stored snapshot and not a fixed 1.0.
- Per-listener queries are independent. The audible mix follows one listener (`SetMixListener`). `RequestIndependentListenerMixes` returns `Unsupported`.

## Streaming

- A space or portal component with `bRetainDescriptorOnUnload` leaves a component-free descriptor behind when it unloads, so routing keeps working. On reload, a portal takes the retained openness.
- Components without that flag are removed from the topology.
- Runtime claims are never persisted as handles; they are re-acquired after load.

## Tests

`Doc.Acoustics.*`, 10 tests: ACO-01 to ACO-10.

ACO-09 is logic-level evidence only: the reference adapter records the applied parameters. It is not audible or cooked-capture evidence, which remains a manual release gate.
