# DocReplayGhosts

Module 37 of the DocModular suite. Records the observed motion of selected tracks and plays it back through isolated visual surrogates, for time-trial ghosts, demonstrations and comparisons. Depends only on DocModularCore and engine modules.

## Recording
- `StartRecording(Name, Profile, Tracks)` declares the tracks; each track names an approved visual id, never a class.
- `RecordSampleAt(Recording, Track, Time, Transform, Token, bDiscontinuity)` records at the actual observed time. `RecordSample` uses world time since the start. Samples are refused if their time is non-finite or not increasing, or if they fall beyond the maximum duration or sample buffer. Samples faster than the cadence, or unchanged within the thresholds, are skipped (NoChange).
- Dropped time is a gap, never filled in with invented samples.
- `AddAnnotation` adds bounded text annotations.
- `StopRecording` finalizes: it builds per-track time-window chunks (the only sample storage and the seek index), sets the duration from the last real sample, and computes a payload CRC. An empty recording is discarded. `CancelRecording` leaves nothing to advertise.

## Evaluation
- Each track is evaluated on its own through its chunk index, using a binary search (no scan from zero).
- Position interpolates linearly; rotation uses a normalized shortest-path slerp.
- Playback never interpolates into a discontinuity (teleport, rebase or frame change).
- Intervals longer than the gap threshold follow Hold, Hide or Snap, and are flagged `bInGap`.

## Playback and isolation
- `StartPlayback` validates the recording, resolves every visual through the subsystem catalog (`RegisterGhostVisual`; "Default" is built in), then spawns one transient surrogate per track.
- Surrogates have collision, overlaps, ticking, damage and replication disabled. `EnforceIsolation` re-applies this to any component an adapter attaches (skeletal meshes get animation paused, so no notifies and no root motion).
- `QueryAnimationCapability` reports Unsupported: no notify-isolating animation adapter ships.
- Pause, Resume (continues from the paused time, and only from Paused), Seek (reconstructs state through the index and fires nothing), SetPlaybackRate (±8) and Stop are supported. Close destroys the surrogates and invalidates the generation.
- Annotations and action tokens go only to `OnPresentationEvent` (the replay channel), and only during forward playback.
- Async opens (`RequestOpenRecording` → `CompleteOpenRequests`) can be cancelled; a late completion starts nothing. `NotifyVisualLoaded` refuses stale generations.
- At most 8 concurrent sessions.

## File format and compatibility
`FDocGhostRecordingIO` writes and reads a bounded binary format: magic, version, header, track table, chunks, annotations. Every read is length-checked, counts are capped, strings are bounded, trailing data is rejected, and the result passes the same validation as in-memory recordings, including numeric checks and the CRC. Unsupported versions return Unsupported without touching the input. A missing visual either plays as a labelled `TransformOnlyFallback` (when the recording allows it) or is refused.

## Tests (Doc.Ghost.*)
| ID | Test | Covers |
|----|------|--------|
| GHO-01 | SampleTimeline | real timestamps, gaps not filled, cadence/threshold skipping, cancel, empty |
| GHO-02 | Interpolation | linear/slerp, shortest path, independent tracks and surrogates |
| GHO-03 | Discontinuity | no interpolation through teleports; Hold/Hide/Snap gaps |
| GHO-04 | NoLiveEffects | no collision/overlap/damage/replication; collectible untouched |
| GHO-05 | AnimationNotifyIsolation | adapter components isolated; tokens only on the replay channel |
| GHO-06 | IndexedSeek | logarithmic chunk lookup; seeks fire nothing; forward crossing policy |
| GHO-07 | AsyncClose | cancelled/late opens, resume semantics, closed sessions stay closed |
| GHO-08 | MalformedRecording | every truncation, corruption, bad magic, numerics, class paths |
| GHO-09 | VersionFallback | unsupported versions untouched; labelled fallback; strict refusal |
| GHO-10 | CookedConcurrentPlayback | concurrent sessions beside live gameplay; session bound |

## Known limits
- GHO-10's cooked/packaged visual playback is a manual gate.
- There is no animation adapter, native Replay bridge or LocalPlayer viewing facade.
- File I/O to disk is left to the host (byte arrays in and out).
