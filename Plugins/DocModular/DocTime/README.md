# DocTime

Deterministic simulated time and configurable calendars (Rev 2 handoff, Section 9). Drives no sky, weather or NPC directly.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Network clock adapter (TIM-07), Save bridge, Audio/Weather/Schedule bridges, physically based celestial provider |

## Model

- Canonical time: `int64` total simulated milliseconds plus a carried sub-millisecond remainder.
- Scale is fixed-point (`ScaleNumerator / 1,000,000`, max 100,000x). Negative scale is rejected.
- `FDocTimeMath::Advance` is exact integer arithmetic: any partition of the same integer microsecond input gives the same result. Frame deltas are quantized to microseconds with the fraction carried (the documented input adapter). This proves the defined arithmetic, not bitwise determinism across independently sampled clocks.
- Calendar: seconds/minute, minutes/hour, hours/day, days/week, per-month lengths, epoch year/weekday, max year (overflow-checked). No leap rules.
- Periods: start/end minute (end exclusive, wraps midnight; start == end is empty), priority for overlaps.
- Schedules: Once, Daily, Weekly, Monthly, Yearly with next-occurrence and range queries. Time only reports *when*.
- Boundaries use the half-open interval (old, new]. Per-frame advancement fires each crossed minute/hour/day up to a cap, then coalesces. Explicit jumps take a policy: FireCrossedBoundaries, CoalesceWithCount, SuppressAndNotifyJump. Backward seeks and restores never replay boundaries.
- Pause uses owner tokens; time runs only when every token is released.
- `CaptureClockState` / `RestoreClockState`: time, remainder, scale, calendar id, schema and algorithm versions. Pause tokens are not restored.
- Celestial output is **stylized** (sin-curve sun between configured sunrise/sunset, linear moon phase) and says so (`bStylized`).

## Setup

Create a `UDocTimeConfiguration` asset and set it in Project Settings → Plugins → Doc Time, or call `SetConfiguration`. Without one, a 24-hour, 12×30-day calendar at 60x starting 08:00 is used.

## Tests

`Doc.Time.*`: Advancement, CalendarRollover, EventBoundary, TimeScale, SaveRestore, Schedules.
