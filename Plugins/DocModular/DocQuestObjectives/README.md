# DocQuestObjectives

DocQuestObjectives provides standalone objectives and multi-stage quests. Progress is event-driven, and quests support prerequisites, optional and parallel work, tracking, failure rules, repetition and receipted rewards (Modules 11–20 handoff, Section 7; Module 15).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Service | `UDocObjectiveSubsystem` (GameInstance). Holds records for every owner scope; records survive world travel |
| World facade | `UDocObjectiveWorldFacade` (world). Attaches and detaches worlds (a new world generation each time) and drives timers |
| Data | `UDocQuestDefinition` and `UDocObjectiveAsset` (primary data assets) |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | DocEvents transport bridge; Regions, Interaction, Inventory, Knowledge, Dialogue, Unlock and combat bridges; map-marker and UI adapters; DocSave bridge; network transport and client views (OBJ-11 network profile); debugger UI |

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| `UObjectiveSubsystem` | `UDocObjectiveSubsystem` |
| QuestDefinition | `UDocQuestDefinition` |
| ObjectiveDefinition | `FDocObjectiveDefinition` (standalone: `UDocObjectiveAsset`) |
| ObjectiveCondition | `FDocQuestCondition` |
| ObjectiveReward | `FDocQuestAction`, delivered through `FDocQuestRewardIntent` |
| QuestRuntimeState | `FDocQuestRuntimeState` |
| ObjectiveRuntimeState | `FDocObjectiveRuntimeState` |

## Owners and identity

**Owner scopes.** Records are keyed by (owner scope, QuestId). The owner scope carries the campaign namespace.

- **Owner policy.** Each quest's `OwnerPolicy` (PerPlayer, SharedWorld or Party) must match the scope's kind. Shared and private records never merge, and nothing defaults to a single global record.
- **Instances.** Every activation, including a repeat, gets a new `QuestInstanceId` and repeat ordinal. Each objective activation gets a new `ObjectiveInstanceId`.
- **Isolation between runs.** Two runs never share progress, timers, dedup windows or effect keys.
- **Standalone objectives** (`RegisterObjectiveAsset` / `ActivateObjective`) run as a synthesized single-stage record with id `Objective.<ObjectiveId>`.

## Stages

Activation and completion are separate settings:

| Setting | Values |
|---|---|
| Activation | `Parallel` activates every eligible objective; `Ordered` activates the next eligible one when none is active |
| Completion | `AllRequired` or `AnyRequired` |

**Other stage rules:**

- **Optional work** never blocks completion. When the stage ends, open optional objectives are cancelled or failed, according to `OptionalClose`.
- **Empty stages.** A stage with no required objectives is a validation error unless it is marked `bPassThrough`.
- **Stage order.** `NextStage` defaults to array order, and `bCompletesQuest` ends the quest early.

**Legacy presets.** `ApplyLegacyStageMode` maps the original values:

| Preset | Maps to |
|---|---|
| AllRequired | Parallel activation + AllRequired completion |
| AnyRequired | Parallel activation + AnyRequired completion |
| Ordered | Ordered activation + AllRequired completion |
| Parallel | Parallel activation only; the completion rule must be chosen explicitly |

**Failure** comes from failure rules (observation filters, optionally limited to one stage) or from a failed required objective (`OnRequiredObjectiveFailed`). The possible outcomes are FailStage (branch or fail), FailQuest, RemainActive and BranchToStage.

- **Precedence.** When completion and failure happen together, `FailurePrecedence` decides, and exactly one terminal transition is recorded.
- **Terminal states.** Terminal states are never reset or cancelled; only a repeat creates a new instance.

## Observations and progress

**Observations.** `SubmitObservation` accepts only registered trusted sources, on the authority. An arbitrary client broadcast is not proof that an objective happened. Each observation carries:

- source id and `EventId`
- owner scope and world generation
- event, target and sender tags, plus payload tags
- amount
- optional source epoch and sequence

**Filters.** Tags match hierarchically unless the exact flag is set. `RequiredPayloadTags` must all be present.

**Evaluators.** EventCount, ReachRegion, Interact, Collect, Discover and Activate count observations when they are Cumulative.

- **CurrentState** objectives query an `IDocQuestStateProvider` instead. That is the difference between "collect three, ever" and "currently own three"; restoring a snapshot never creates collect events.
- **Wait** uses the quest's timer clock.
- **CustomCondition** uses an `IDocQuestConditionProvider`.
- **Missing providers** are reported as unavailable and never treated as satisfied.

**Active-only counting.** Only active objectives count. Historical observations count only when `ReplayPolicy` accepts them.

- **Default.** An event is processed against the objectives active when its dispatch started, so it cannot also satisfy a stage it activates.
- **Cascade.** `BoundedCascade` with `MaxCascadeDepth` is the authored, bounded alternative.

**Dedup** is per quest instance:

- **With a source sequence:** a high-water mark per (source, epoch) plus a 64-entry out-of-order window. Anything older than the window is rejected, never assumed new.
- **Otherwise:** a bounded window of recent `EventId`s (`EventIdWindow`). Dedup is not claimed indefinitely.

**Counters** use checked 64-bit arithmetic and clamp at the target, so they never overflow.

**Processing order.**

1. Commit objective changes.
2. Evaluate the stage, then the quest.
3. Stage effect intents.
4. Publish ordered change events.
5. Deliver the intents.

Handler calls made during a commit run after it.

**Subscriptions.** Active listeners are indexed by owner and tag, and completed work is unsubscribed. Only active timers are touched, so inactive quests are never polled.

## Timers

Each quest declares a `TimerClock`. Wait and time-limit objectives store remaining seconds, which are saved.

**The world facade** advances the WorldGameplay clock (which pauses and dilates with the world) and the RealTime clock. It advances Simulation too unless a time bridge drives it (`bDriveSimulationFromWorldTime`).

**Time jumps.** An `AdvanceClock` call is one coherent step, and skipped activities are not replayed.

**Offline progress** is opt-in per objective (`bAllowOfflineProgress` together with `ApplyOfflineElapsed`).

## Rewards and effects

Rewards, objective completion and failure actions, and stage completion actions all become intents. Each intent has a Core effect key: owner, campaign namespace, quest instance, transition ordinal, action id.

**Delivery results:**

| Consumer returns | Intent becomes |
|---|---|
| Success or NoChange | Delivered |
| InvalidInput, InvalidConfiguration, Unsupported or PermissionDenied | Failed (permanent, still visible) |
| Anything else, such as a full inventory | Pending (retryable) |

**Separate facts.** Quest Completed and `bRewardDeliveryPending` are separate facts.

- **Retry.** `RetryRewardDelivery` resends pending intents with their original keys, so a consumer that already applied an effect returns its original result.
- **Claim flow.** `ManualClaim` holds rewards until that call.
- **Rewards are never lost or repeated.** A completed quest is never reverted, and a repeat is refused while rewards are still pending.

## Tracking

Tracking is per viewer scope and refers to the progress owner, so two local players can track a shared quest differently.

- **API:** `SetTrackedQuest`, `SetTrackedObjective`, `GetTrackedQuest` and `GetTrackedObjectives` (manual picks plus AutoWhenActive objectives of the tracked quest).
- **No effect on progress.** Untracking never cancels anything, and tracking never changes progress. Terminal quests drop out of tracking.

## Persistence

`CaptureState` and `RestoreState` cover:

- lifecycle and counts
- stage and completed stages
- instance ids and repeat ordinal
- timers
- timestamps
- dedup state
- intents and their results
- optionally, tracking

**Restore flow:**

1. Validate the envelope and owners.
2. Migrate: apply `StageRedirects` and `ObjectiveRedirects`. A removed optional objective is quarantined. A missing critical stage, required objective or definition quarantines the record, keeps its state, and preserves it in future saves; it never completes silently.
3. Stage the records and apply them under a restore barrier, with automatic activation suppressed.
4. Rebuild the indexes.
5. Send one `OnStateRefreshed`.

Rewards and effects are never replayed.

## Tests

The tests are under `Doc.Quest.*`:

- Activation
- EventCount
- SequentialStage
- ParallelObjectives
- OptionalObjective
- Failure
- SaveRestore
- CurrentVersusCumulative
- RewardRetry
- Tracking
- WorldAndAuthority (local part)
- Wait
- Validation

OBJ-11's real network profile and OBJ-12's bridge coexistence still need their hosts.
