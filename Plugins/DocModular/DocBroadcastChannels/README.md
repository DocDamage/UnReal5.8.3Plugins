# DocBroadcastChannels

Module 32 of the DocModular suite (Phase 11: Player Activities and Media).

Shared in-world radio/TV/PA channels. Receivers tune into a channel's current position instead of restarting a playlist per device. Depends only on DocModularCore and engine modules.

## Model
- `UDocBroadcastChannelDefinition`: ordered, non-overlapping half-open slots `[start, start + duration)`, optional trailing gap, looping flag, `ScheduleVersion`, source-failure policy (`Skip`, `SilenceGap`, `RetryBounded` with `MaxOpenRetries`) and clock policy. `ValidateChannel` rejects empty schedules, zero-length or overlapping slots, slots longer than their program, and non-finite/oversized totals.
- `UDocBroadcastProgramDefinition`: finite duration, `bKnownDuration`, `bCanSeek`, soft media reference. Scheduled programs must be seekable with a known duration; otherwise registration fails with `Unsupported` (BRC-07).
- Schedule resolution uses doubles with loop index + local offset, so long-running channels and large time jumps resolve directly without replaying skipped programs.

## Transport
- The channel clock is advanced explicitly by `AdvanceTime` (real transport seconds). The schedule offset is `Clock - Anchor - Held`.
- Receivers own only tuning, volume and mute. Tuning, muting, unloading or reloading a receiver never changes channel time (BRC-02/03/08).
- `SetTransportPaused` and `SeekChannelAuthorized` are the only authorized transport changes. Seeking needs a seekable source and is refused while interrupted.

## Playback provider and async open
`UDocBroadcastPlaybackProvider` is the backend seam (the spec's `IDocBroadcastPlaybackProvider`): capabilities (seek, known duration, audio/video, seek precision, audible) plus ticketed async `OpenProgram` / `CancelOpen`. A content change cancels the pending ticket; `NotifyOpenResult` ignores stale tickets, so late callbacks cannot start replaced, cancelled or released programs (BRC-06). States: NoProgram, Loading, Playing, Paused, Interrupted, UnavailableSource, Faulted.

The default `UDocBroadcastSyntheticProvider` is logic-only and produces no sound. No audible Media Framework/audio provider ships yet.

## Interruptions
Requests carry an owner handle, priority, arbitration (`Queue`, `Replace`, `Reject`), resume policy, program, duration and expiry.
- Highest priority plays; equal priority is FIFO by request ordinal.
- Only the owner can release (`PermissionDenied` otherwise). Releasing one claim never stops another owner's.
- Queued requests past their expiry end as `Expired` without playing. Every terminal state (Completed, Released, Expired, Replaced, Rejected, SourceFailed, Cancelled) is queryable from a bounded history.
- `PauseUnderlyingCursor` holds the schedule while the announcement plays; `ContinueUnderlyingSchedule` lets it run on (BRC-04).

## Clock mapping
`FollowRealTransport` ignores the fictional clock scale. `ReanchorAtScheduleBoundary` plays programs at normal speed, and at each program boundary jumps to the entry the fictional clock (`SetScheduleClockScale`) indicates, starting it from the top (BRC-09).

## Persistence
`CaptureTransportState` stores schedule version, anchor, clock, held and fictional time, scale and pause. `StageRestoreTransportState` restores a registered channel without emitting program or interruption events. Live interruption leases are not persisted and are cancelled quietly on restore. A schedule version mismatch is migrated by resolving the saved offset against the current schedule and is reported in the diagnostic.

## Tests (Doc.Broadcast.*)
| ID | Test | Covers |
|----|------|--------|
| BRC-01 | ScheduleBoundary | half-open slots, gap, wrap, far offsets, validation, one event per long jump |
| BRC-02 | LateReceiver | late and pre-tuned receivers join the live cursor without reopening |
| BRC-03 | MuteIndependence | mute/volume/tune/unload leave the channel untouched |
| BRC-04 | InterruptionResume | pause-underlying vs continue-schedule |
| BRC-05 | OwnerAndExpiry | owner-only release, expiry, reject/replace |
| BRC-06 | AsyncOpenFailure | stale tickets ignored, Retry/Silence/Skip policies, failing announcement |
| BRC-07 | SeekCapability | unknown duration / no seek fail explicitly; authorized seek |
| BRC-08 | UnloadRestore | receiver reload, restore without event replay, migration, corrupt saves |
| BRC-09 | ClockMapping | FollowRealTransport vs ReanchorAtScheduleBoundary |
| BRC-10 | CookedAudio | reports Unsupported; no measurement is claimed |

## Known limits
- No audible provider, so BRC-10 (cooked bundled playback and measured local sync tolerance) is a manual gate and is not verified.
- Receivers do not create audio components; bounded audible playback belongs to a future audio provider.
- Networking, streams and video are bridge/future work.
