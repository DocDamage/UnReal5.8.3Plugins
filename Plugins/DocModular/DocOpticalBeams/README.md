# DocOpticalBeams

Authored gameplay beams through emitters, mirrors, filters and receivers (Modules 21–40 handoff, Section 10; Module 28). The plugin computes gameplay paths and receiver activation. It does not produce physically correct lighting, damage or security tripwires.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocOpticalBeamSubsystem` (World subsystem + tickable) |
| Components | `UDocBeamEmitterComponent`, `UDocBeamSurfaceComponent`, `UDocBeamReceiverComponent`, `UDocBeamPresentationComponent` (all self-register on component registration) |
| Assets | `UDocOpticalProfile` |
| Depends on | DocModularCore; engine modules only. No Puzzle, Power, Niagara or security plugin. |
| Not included | Splitters, refraction, lenses, scattering (Prism is an explicit `Unsupported` profile), network replication, debug overlays |

## Solver

Each path:

- Starts from the emitter's world origin and normalized direction. A zero or non-finite direction produces a `Disabled` path.
- Traces only the emitter's `TraceChannel`, plus analytic one-sided planes registered for tests and tools.
- Ignores the owning actor only on the first segment, so the beam can leave its housing.

Reflection is `r = d − 2(d·n)n` with normalized inputs. The result is independent of the normal's sign.

Surface normals:

- Surfaces use their authored `SurfaceNormalOverride`, falling back to the hit normal.
- An owner with an odd number of negative scale axes flips the authored normal.
- A one-sided surface (`bTwoSided = false`) blocks a beam that arrives from behind.

After each surface response the next origin is offset by `TraceEpsilon`. **The offset is charged to the range ledger**, so `TotalLength` never exceeds `MaxRange` and offsets cannot extend the beam.

**Termination reasons:**

| Reason | Cause |
|---|---|
| `HitReceiver` | The beam reached an enabled receiver. |
| `HitBlocker` | Plain geometry, a surface without a valid profile, the back of a one-sided surface, or a Prism. |
| `FilteredOut` | A filter rejected the beam's channel. |
| `OutOfRange` | The range ledger ran out. |
| `IntensityDepleted` | Intensity fell below `MinIntensity`. |
| `LoopDetected` | Visited guard: same surface, quantized direction and channel. |
| `TerminatedByBudget` | `MaxReflections` or `MaxSegments` reached. `MaxSegments` guarantees termination even when the visited guard misses a cycle. |
| `Disabled` | The emitter is disabled or its direction is invalid. |

**Coefficients:**

- All coefficients are in [0,1], and they are clamped at runtime as well.
- A profile that fails validation acts as a blocker, so no surface can ever amplify a beam.

**Filter channel rule:**

- An empty `FilterChannels` passes every channel. Otherwise only matching tags pass.
- `ShiftedOutputChannel` relabels the transmitted beam.
- Channel tags are the gameplay identity. The presentation exposes them per segment for labels and shapes, so color alone is never the only cue.

## Invalidation, generations and staleness

- **What queues an emitter:** `SetEmitterEnabled`, `UpdateEmitterPose` and `NotifyEmitterMoved` each bump that emitter's generation and queue it. Surface registration, `NotifySurfaceMoved` and `SetSurfaceProfile` (validated) bump the topology revision and queue every emitter.
- **Re-solving:** `Tick`, or `AdvanceSimulation(dt)`, re-solves up to `MaxSolvesPerTick` queued emitters in round-robin id order. Beams whose inputs did not change are not re-solved.
- **Batched or async traces:** use `CreatePathRequest → TracePath → CommitPath`. A commit whose emitter generation or topology revision changed returns `Conflict` and is discarded. A commit for a destroyed emitter returns `NotFound`.
- **Staleness:** a queued path older than `MaxPathStalenessSeconds` has its receiver contribution **held**. The contribution stays visible as evidence but cannot grant eligibility until the path is re-solved. `GetPathAgeSeconds` and `IsPathPending` expose this.

## Receivers

- One contribution per emitter, stamped with the path generation and solve time.
- A commit swaps that emitter's contribution atomically: it is installed at the terminal receiver and removed everywhere else. Re-solving never double counts.
- Disabling or destroying an emitter removes only its contribution.
- Aggregation iterates in emitter-id order, so registration order never matters:

| Mode | Eligible when |
|---|---|
| `AnyEligible` | Any accepted contribution reaches `MinRequiredIntensity`. |
| `SumIntensity` | The sum of accepted contributions reaches `MinRequiredIntensity`. |
| `AllRequiredChannels` | Every listed channel has a contribution at or above `MinRequiredIntensity`. |

- Dwell runs on the subsystem's simulation clock and must be continuous. Any loss of eligibility resets it.
- `ReleaseHysteresisSeconds` holds activation after the beam is lost.
- `bLatchOnActivate` keeps a receiver activated until `ResetLatch`.
- Activation events carry the committed contribution evidence and revision.

## Persistence

- `CaptureReceiverState` saves only latched activation. Trace hits and non-latched progress are never saved.
- `RestoreReceiverState` restores that latch without emitting events, clears the stale contributions and queues every path.
- Non-latched receivers must earn activation again with a fresh, full dwell.

## Presentation

`UDocBeamPresentationComponent` has no authority. It rebuilds pooled segment transforms and channels only when its emitter's committed path changes: never per frame, and never a component per segment. With `BeamMesh` set, it keeps one owned instanced-mesh component with one instance per segment. There is no Niagara dependency.

## Tests

`Doc.Optics.*`, 10 tests: OPT-01 to OPT-10.

OPT-09 is logic-level presentation evidence. Visual confirmation in a cooked build remains a manual gate.
