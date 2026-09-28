# DocSequences

DocSequences handles logical Level Sequence playback and set-piece orchestration without asking you to place sequence actors by hand (Rev 2 handoff, Section 11).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Director | `UDocSequenceSubsystem` (world) |
| Assets | `UDocSequenceDefinition`: tag, soft Level Sequence, priority, arbitration, skip/replay/interrupt policy, restore state, requested control, roles, effects, prerequisites |
| Playback | `FDocLevelSequenceBackend`: one `ALevelSequenceActor` per session. Bindings are set with Sequencer **binding tags** |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, LevelSequence, MovieScene |
| Not included | Streaming bridge (SEQ-06 with real chunk leases), Events mirror bridge, network replication of sessions (SEQ-07) |

## Sessions

A session goes through these states:

`Requested → Queued / Loading → ResolvingBindings → Preparing → Playing ↔ Paused → Completed / Skipped / Interrupted / Failed → Restoring → Finished`

- **Session ids.** A session id is not the same thing as the sequence tag. `RestartSequence` and every replay create a new session.
- **Controls.** `PlaySequence`, `Pause`, `Resume`, `Stop`, `Skip`, `Restart`, `JumpToMarker`, `IsSequencePlaying`, `GetSessionState` and `GetSessionInfo`. Info on finished sessions is kept for a bounded history.
- **Arbitration.** Only one session is active at a time. Each definition picks one of these policies:
  - `Reject`
  - `Queue`, which is bounded and ordered by priority, then first come first served
  - `Interrupt`, which only interrupts a session marked `bInterruptible`
  - `ReplaceLowerPriority`

  If a request can't interrupt, it is queued instead.
- **Queued requests.** If the owner of a queued request dies, the request is removed. Cancelling a queued request produces an observable Interrupted/Cancelled result.
- **Terminal results.** Every session ends exactly once, and the finish delegates report the terminal state and the typed result. Examples:
  - a missing asset gives NotFound
  - a load or participant timeout gives TimedOut
  - a missing prerequisite provider gives Unsupported

## Bindings

Roles are `Sequence.Role.*` tags mapped to binding tags. They are resolved in this order:

1. Actors you pass explicitly in `FDocSequencePlayParams`.
2. Registered participants (`UDocSequenceParticipantComponent`, or anything implementing `IDocSequenceParticipant`), in registration order.

There are never actor-label searches. What happens when a participant is missing, before or during playback, depends on the role's policy:

- **`Fail`:** the session fails.
- **`WaitWithTimeout`:** playback pauses, then rebinds when the participant comes back.
- **`MissingOptional`:** playback continues without it.

## Effects, skip, seek, replay

Gameplay effects are an explicit mapping on the definition. Each effect is triggered one of two ways:

- **By time:** it fires when forward playback crosses its time.
- **By event tag:** it fires when a director blueprint or event track calls `NotifySequenceEvent(Sequence.Event.*)`.

Effect rules:

- **Presentation vs Authoritative.** `Authoritative` effects run only with authority. A client's playback or skip never mutates world state.
- **Irreversible effects.** Their keys go into a receipt ledger as `FDocEffectKey`s scoped to the world, sequence and effect. Replay, skip, seek and late join can therefore never apply them twice. Use `GetEffectReceipts` / `RestoreEffectReceipts` to persist the ledger through a save bridge.
- **Seeking.** `JumpToMarker` uses Sequencer's Jump update, so no crossed events run. The subsystem also skips crossed effects unless an effect opts in with `bCommitWhenSeekedPast`.
- **Skip policies:**
  - `FinishAtEndState`: commits the effects marked `bCommitOnSkip`, then evaluates the end state.
  - `CancelAndRestore`: commits nothing and restores state.
  - `Reconcile`: calls `OnReconcileSkip` to choose which effects to commit.
- **Replay restriction.** `bCanReplay = false` checks the completions recorded in this world and, if one is set, the history provider.

## Control and restoration

- **Camera and input.** `Control.Camera` and `Control.Input.*` are claimed per local player through Core's `UDocPlayerControlSubsystem`.
- **AI.** `Sequence.Control.AI` is handed to bound participants through `SetDocSequenceAIControlled`. The component counts nested claims.
- **Release.** On every terminal path, including world teardown, the session releases only its own claims, AI hand-backs, prerequisites and playback. It never re-enables all input or unpauses all AI.
- **Missing control provider.** If a player has no control provider, the session plays anyway and reports it. Set `bRequireControlProvider` to make that a failure instead.
- **Prerequisites.** These are generic tags resolved by registered `IDocSequencePrerequisiteProvider`s, which is how the Streaming bridge plugs in. They share the session's cancellation generation, so a readiness callback that arrives late is ignored.

## Tests

The tests are under `Doc.Sequences.*`: PlaybackControls, Arbitration, FailuresAndWaiting, EffectsSkipReplay and ControlAndPrerequisites.

A scripted playback backend stands in for Level Sequence players. The following still need real assets in a cooked host:

- Sequencer evaluation
- binding-tag override behaviour
- `Jump` event suppression
