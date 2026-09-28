# DocNPCSchedules

DocNPCSchedules works out what an NPC should be doing, where, and when. It does not replace locomotion or AI (Modules 11–20 handoff, Section 8; milestone 6.1).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Records | `UDocNPCScheduleSubsystem` (world): logical NPC records keyed by persistent `NPCId`, plus a boundary-indexed evaluation queue |
| Representation | `UDocNPCScheduleComponent` binds a loaded actor to its record and is the executor's API surface |
| Data | `UDocNPCScheduleDefinition` (entries), `UDocNPCActivityDefinition` (interruptibility, retries, fallback, arrival deadline) |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Time bridge (DocTime clock), SmartObjects bridge (claims, SCH-09), StateTree and Mass bridges, Regions/Weather/Events/Activation context bridges, replication (SCH-11 client side), debugger UI |

## Time and selection

- **Clock.** The clock is an `IDocScheduleClock` with an explicit calendar (day length, days per week, days per season, seasons per year). It never assumes 24-hour days or 7-day weeks. `FDocManualScheduleClock` is the standalone clock.
- **Intervals.** Entries are half-open `[Start, End)` in seconds of the day.
  - `Start > End` crosses midnight. The span belongs to the day it starts on, so yesterday's span still applies after midnight.
  - `Start == End` is empty unless the entry is `bAllDay`.
  - Entries whose times fall outside the calendar's day length are never active.
- **Filters.** Entries can be restricted by day of week, season, or `SpecialDay` for special events.
- **Ranking.** Candidates are ordered by override class (Daily < Weekly < Seasonal < SpecialEvent < Temporary < Emergency < Manual), then explicit priority, then authored order (or newest push for overrides), then `EntryId`. Hash iteration order and spawn order never decide the result.
- **Priorities.** The example defaults (Normal 10 … Cinematic 100) are editable in settings.
- **Conditions.** Entry conditions return Satisfied, Unsatisfied or Unavailable through an `IDocScheduleConditionProvider`.
  - When the data is unavailable, including when no provider is installed, the entry's `FallbackActivity` is used. The system never guesses.
  - An entry with no fallback is skipped.

## Desired, requested and observed state

- **Desired.** What the schedule selects.
- **Requested.** The request sent to the executor: a request id, a revision, the target (logical ids and locations only, never actor pointers), interruptibility, and a restored flag.
- **Observed.** The status the executor reports back: Accepted, Travelling, Started, Completed, Failed, Cancelled or Unavailable.

Failure handling:

- **Stale callbacks.** A report carrying an old request id is ignored.
- **Retries.** Failures retry with bounded exponential backoff, then switch to the fallback activity. If the fallback also fails, nothing more happens until the next boundary, so there is no busy loop.
- **Arrival deadline.** A missed deadline triggers reselection or the fallback, never a false "arrived" report.
- **Expected location.** This is an estimate with a confidence level: Unknown, RegionOnly, ScheduledTarget or Observed. "Expected" does not mean safe to spawn; the representation provider validates placement.

## Overrides

`PushScheduleOverride` returns an owner-scoped handle and takes these options:

- class and priority
- expiry
- resume policy (the default is to recompute at the current time)
- preemption: Preempt, WaitForInterruptible or RejectIfBusy against non-interruptible activities
- whether the override is durable

The other operations:

- `PopScheduleOverride` releases only the handle it is given, then recomputes.
- `ClearOverrides` removes only the caller's own overrides.
- `ClearAllOverridesAdmin` is a separate, privileged call.
- Expired overrides, and those whose owner is gone, are dropped at the next evaluation.

## Offline, time jumps, representation

- **Unloaded NPCs.** They are evaluated only at indexed boundaries: entry start and end, override expiry, retry time and arrival deadline. Nothing ticks per second or per NPC.
- **Forward jumps.** One coherent re-evaluation. Skipped activities are not replayed.
- **Backward jumps.** Desired state is recomputed, but irreversible effects are neither undone nor granted again.
- **Binding.** `BindRepresentation` issues a fresh request, and swapping representations cancels the old executor.
- **Unbinding.** On unload, `UnbindRepresentation` cancels the current request, and the executor releases its own claims. The logical record stays.

## Persistence

`CaptureState` and `RestoreState` cover the NPC id, schedule id and version, desired activity, expected location, last evaluation time and **durable** overrides. They never store request ids, live claims or tasks.

On restore:

- Records are validated before anything is applied.
- Old override handles become stale.
- Restored durable overrides have no owner; they end on expiry or through the admin clear.
- Executors receive `bRestored` requests, which tells them not to grant start rewards a second time.

Mutations require authority, and the whole system works on a dedicated server.

## Tests

The tests are under `Doc.Schedule.*`: TimeSelection (including a custom calendar), PriorityOverride, OverridePop, TimeJump, OfflineSimulation (500 unloaded NPCs), SaveRestore, ExecutorLifecycle, TargetUnavailable and RepresentationSwap.

These still need their bridges and hosts:

- Smart Object contention (SCH-09)
- Mass or activation swaps (SCH-10 beyond components)
- client authority (SCH-11)
