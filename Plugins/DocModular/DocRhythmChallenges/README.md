# DocRhythmChallenges

Module 34 of the DocModular suite. Authored one-lane tap/hold charts, calibrated judgment, combo/scoring, and versioned results. Depends only on DocModularCore and engine modules.

## Charts and profiles
- `UDocRhythmChart`: integer-microsecond note times, stable NoteIds, `CountInUs`, optional `AudioDurationUs`. Validation rejects empty charts, duplicate ids, bad lanes, holds that do not end after they start or run past the chart, taps with a duration, same-lane simultaneous notes or notes inside a hold, audio-length mismatches (never rescaled) and charts over 100,000 notes. `BeatToMicroseconds` converts at constant tempo, rounding half away from zero.
- `ComputeContentHash` hashes all timing content. The authored `ContentHash` field is kept as a declared version label.
- `UDocRhythmJudgmentProfile`: inclusive window edges (`|error| <= window`), optional asymmetric early/late windows, bounded non-decreasing points (Miss <= Good <= Great <= Perfect <= 1,000,000), hold threshold, focus-loss policy, drift and hitch thresholds, and an assistance flag. `ComputeProfileHash` identifies it in results.

## Time
Chart time = monotonic clock − attempt epoch − recorded pause intervals. It is never built from frame deltas. `IDocRhythmClockProvider` supplies the monotonic clock (default: platform high-resolution time).
- Inputs are judged at their own mapped timestamps, so a hitch between input and processing does not change the judgment. Missing timestamps use processing time and are flagged `LowPrecisionInput`, as is `DispatchEstimate`/`Unknown`.
- Inputs stamped before the attempt started, during a pause, or in the future are refused. If the timeline has already missed a note before a late input arrives, that note stays missed; input times are never invented.
- Timeline gaps above the hitch threshold flag `ClockHitchDetected`.

Calibration sign: positive `InputOffsetUs` means inputs are late and is subtracted.
```
JudgmentTimeUs = InputTimestampMappedToChartUs - InputOffsetUs
ErrorUs        = JudgmentTimeUs - NoteTimeUs
```
`RunCalibration` needs 8+ samples, rejects outliers by MAD, reports the median and a scaled-MAD uncertainty, and returns `CalibrationInsufficient` rather than inventing precision. The calibration is scoped to a device and invalidated when the device changes. Audio and visual offsets never affect score. Calibration cannot change during an attempt.

## Judgment
- Press rule: the earliest pending note in the lane whose error is a hit, otherwise the earliest pending note inside its miss window. Inputs beyond every window select nothing.
- Each note gets exactly one terminal judgment. Repeated input event ids or identical (lane, type, timestamp) events are ignored.
- Holds: the start is judged on press. Release at or after `start + round(duration × MinHoldPercent)`, or holding to the end, completes with the start judgment. An earlier release, or focus loss under `BreakHold`, is one Miss. Stray releases never create misses.
- Integer score, bounded by profile and chart limits.

## Lifecycle
`LoadChart → Ready → (CountIn) → Playing ↔ Paused → Completed`, or `Aborted` / `TimingFault`.
- Restart creates a new AttemptId, session generation and transport generation. Inputs tagged with the old attempt, or stamped before the restart, are refused.
- Pause is only allowed when the playback provider reports precise pause/resume; otherwise it returns `Unsupported` and play continues.
- Each update checks the playback provider. Audio loss raises `TimingFault` and flags the attempt `NonComparable`. Drift beyond `MaxAudioDriftUs` re-anchors the chart to the audio in a new transport generation, flagged `AudioReanchored`.

## Results
Results carry the chart id, computed hash and declared version, the profile id and hash, raw counts, max and final combo, maximum score, accuracy, the calibration used, timing-quality flags, the transport generation count, the AttemptId and `bAudibleReference`. Results go into separate categories: `Strict`, `Assisted` and `Degraded` (hitch, low precision, calibration uncertainty, re-anchor or non-comparable).

## Playback
`UDocRhythmAudioComponentPlayback` is the native audible reference. It plays the chart's `USoundBase` path through a 2D audio component. It reports no precise position and claims no precise pause. Quartz scheduling belongs in a bridge.

## Tests (Doc.Rhythm.*)
| ID | Test | Covers |
|----|------|--------|
| RHY-01 | NoteWindows | inclusive edges, asymmetric windows, in-play boundaries |
| RHY-02 | InputDeduplication | event-id/timestamp dedupe, selection rule, stale attempt, future stamps |
| RHY-03 | HoldLifecycle | early release, threshold release, held to end, focus loss, one result each |
| RHY-04 | CalibrationSign | robust signed offset, insufficient data, device scope, visual offset |
| RHY-05 | ClockAndHitch | mapped timestamps across a hitch, low-precision flag, unrecoverable late input |
| RHY-06 | PauseResume | pause-aligned mapping, pause-time inputs refused, drift re-anchor, unsupported pause, audio loss |
| RHY-07 | RestartGeneration | new attempt/generation, old callbacks refused, clean state |
| RHY-08 | ResultIdentity | hashes, profile, calibration, categories, single completion |
| RHY-09 | EmptyInvalidChart | invalid charts and profiles, bounded score |
| RHY-10 | CookedAudibleRun | native audio path fails explicitly without a world/sound; the audible run is a manual gate |

## Known limits
- RHY-10 (a packaged audible run with timing evidence) is a manual gate and is not verified by automation.
- The input adapter and player-control integration live in the host project or a bridge.
- Mid-song save/resume is not implemented.
