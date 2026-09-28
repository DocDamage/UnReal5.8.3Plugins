# DocPuzzleMechanisms

Reusable mechanical and logical puzzle rules: ordered inputs, simultaneous conditions, weighted thresholds, timed windows, symbol combinations, alternative solutions and resets (Modules 21–40 handoff, Section 3).

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocPuzzleSubsystem` (tickable World subsystem) |
| Authoring | `UDocPuzzleDefinition` (PrimaryDataAsset), `UDocPuzzleRule` subclasses |
| Components | `UDocPuzzleComponent` (puzzle host), `UDocPuzzleInputComponent` (input adapter) |
| Dependencies | `DocModularCoreRuntime`; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Procedural solver, visual scripting graphs, physical simulation, Blueprint example (PUZ-10 Blueprint half) |

## Rules

**Composition.** `UDocPuzzleRule_Composite` supports `All` and `Any`. For `Any`, the first satisfied child in authored order is the winning rule. Two alternatives completing in the same revision produce one solve.

**Rule types:**

- `UDocPuzzleRule_OrderedInputs`: an ordered sequence with a mismatch policy (`ResetProgress`, `FailAttempt` or `IgnoreUnexpected`). A level input re-sent with its current value is not a new edge, so a held button can't satisfy two steps.
- `UDocPuzzleRule_Simultaneous`: level inputs from all active contributors, with an optional continuous dwell (`MinDwellSeconds`). The dwell starts and breaks at commit time.
- `UDocPuzzleRule_WeightedThreshold`: the summed weight of all contributors. One contributor leaving removes only its own weight.
- `UDocPuzzleRule_SymbolCombination`: matches discrete symbol inputs. Unmet reasons give a count, never the solution.
- `UDocPuzzleRule_TimedSequence`: a half-open window `[start, deadline)`. An input exactly at the deadline is late, and the window closing fails the attempt.

## Definitions

`ValidateDefinition` walks the rule graph and reports:

- cycles
- depth over `MaxGraphDepth` (16)
- more than `MaxGraphNodes` (256) rules
- duplicate RuleIds (rule state is keyed by RuleId)
- when inputs are declared: rules that reference undeclared inputs
- invalid input ranges, debounce or timeout

`RegisterPuzzle` refuses a definition that fails validation. Evaluation also has a recursion guard.

## Inputs

`SubmitInput` requires the active `AttemptId`. An event with a missing or stale attempt id is rejected. `UDocPuzzleComponent` fills in the attempt id for you.

**Time.**

- Acceptance time is the subsystem's authority clock (world time, or `SetClockOverride`). A caller-supplied `AcceptedTimestamp` is ignored.
- Each input is checked against the attempt timeout and the window deadlines before it is applied.

**Declared inputs.** When the definition declares inputs, the subsystem enforces declaration, value kind, scalar range and `DebounceSeconds`. Duplicate `(SourceId, SourceSequence)` pairs are rejected.

**Contributors.** `SetInputContributor` and `RemoveInputContributor` maintain source-owned level state. Removal is repeat-safe.

## State, commits and events

`Inactive → Ready → AttemptActive → Solved | Failed`, and `RequestReset` returns to Ready with a new reset epoch.

- **Commit points.** Inputs, contributor changes, `StartAttempt` and the subsystem tick (`ProcessTimers`) are commit points. Every transition to Solved or Failed publishes its event; a puzzle never solves silently.
- **Views.** `EvaluatePuzzle` is a read-only view. It never changes state or publishes events.
- **Retained state.** `UnregisterPuzzle` retains the state and drops live contributors. Registering the same instance with the same definition restores it. A different definition gets `Conflict` until you call `DiscardRetainedState`.
- **Snapshots** use schema 2 (schema 1 is still read). `CapturePuzzle` / `StageRestore` keep:
  - rule state
  - remaining timer durations, re-anchored to the restoring clock
  - submitted level inputs

  Contributor levels are not captured: live sources re-register them. Restore publishes nothing.

## Tests

`Doc.Puzzle.*`: OrderedInputs (PUZ-01), DuplicateInput (PUZ-02), ContributorOwnership (PUZ-03), SimultaneousDwell (PUZ-04), DeadlineBoundary (PUZ-05), AlternativeSolutions (PUZ-06), ResetEpoch (PUZ-07), RestoreNoEffects (PUZ-08), InvalidGraph (PUZ-09), IsolatedConsumers (PUZ-10, C++ half).
