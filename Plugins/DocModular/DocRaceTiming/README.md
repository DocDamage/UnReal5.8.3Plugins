# DocRaceTiming

Validates and times checkpoint courses, laps and time trials, independently of how the participant moves (Modules 21–40 handoff, Section 20; Module 38).

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocRaceTimingSubsystem` (World) |
| Components | `UDocRaceGateComponent`, `UDocRaceParticipantComponent` |
| Data | `UDocRaceCourseDefinition` (ordered gates, laps, start mode, false-start and pause policies, penalty limit) |
| Depends on | DocModularCore; engine modules only |
| Not included | Map/Ghost/UI/Save bridges, online rankings, anti-cheat, `IDocRacePositionProvider` |

## Run lifecycle

`BeginRun` creates a run in **Ready**.

- **Running start** (`bRunningStart = true`, the default): the run becomes **Running** at the first forward crossing of gate 0, and the clock starts at that interpolated crossing time.
- **Standing start** (`bRunningStart = false`): `StartCountdown(RunId, GoTimestamp)` moves the run to **Countdown**. The clock starts at the go timestamp. Crossing gate 0 before go is a false start, handled by the course's `FalseStartPolicy`:
  - `RejectStart`: the crossing is not counted; the participant must cross again after go.
  - `Penalty`: the crossing counts and one `FalseStart` penalty is recorded.
  - `Invalidate`: the run ends **Invalid**.

A run ends **Finished**, **Invalid** (missed gate or false start, with `StatusReason`) or **Aborted**.

## Clock

- Every time is on one clock: the sample timestamps. Timestamps must strictly increase; older, duplicate or non-finite samples are refused. A timestamp of 0 means `FPlatformTime::Seconds()`, so don't mix explicit and default timestamps in one run.
- Split and finish times use the linearly interpolated crossing time, not the sample time.
- **Pause.** `CompetitiveContinuous` courses refuse `PauseRun`; the clock keeps running. `PracticeAllowPause` courses exclude the paused time, and the result is marked `bPracticeResult` and kept as a separate personal-best category.

## Gates

- A crossing needs a strictly-behind to strictly-in-front transition inside the gate bounds. Starting on or past the plane is not a crossing.
- Several crossings in one segment are processed by increasing fraction, then gate id.
- Re-crossing a gate already passed (jitter) is ignored. Crossing a later gate before the expected one invalidates the run (`MissedGate`). Each lap needs the full gate sequence.
- `NotifyDiscontinuity` (or the participant component's version) breaks the segment, so a teleport grants nothing.
- `BeginRun` refuses courses with a missing or duplicate gate id, or with non-finite gate geometry.

## Results and restore

- `FinalizeResult` commits once, only for a Finished, Invalid or Aborted run; later calls return the committed result. A finished run commits automatically.
- **Penalties** have stable ids and are applied once. Negative, non-finite or over-limit penalties are refused, and so is any penalty after the result is committed.
- **Personal bests** are keyed by course id, course version, assistance category and practice flag.
- **Restore.** `CaptureResults` / `CaptureActiveRuns` / `StageRestore` handle persistence. Restore deduplicates results by run id and rebuilds personal bests without broadcasting. Mid-run state comes back **Aborted** (`RestoredMidRun`) and earns no reward.

## Tests

`Doc.Race.*`: DirectionalGate (RAC-01), SweptHighSpeed (RAC-02), MultiGateSegment (RAC-03), TeleportDiscontinuity (RAC-04), StartAndPause (RAC-05), LapAndMissingGate (RAC-06), PenaltyIdempotency (RAC-07), RecordCompatibility (RAC-08), RestoreNoRewards (RAC-09), MovementAgnostic (RAC-10).
