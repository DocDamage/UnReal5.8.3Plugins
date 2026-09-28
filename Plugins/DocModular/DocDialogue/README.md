# DocDialogue

DocDialogue runs branching, condition-driven conversations. It works headless and binds arbitrary logical participants (Modules 11–20 handoff, Section 6; Module 14).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Sessions | `UDocDialogueSubsystem` (world). Each running session is private mutable state inside the subsystem; a graph asset is never a running session |
| Data | `UDocDialogueGraph` (primary data asset): nodes keyed by stable `NodeId`, roles, typed variables, execution policy, node redirects |
| Participants | `IDocDialogueParticipant`, `UDocDialogueParticipantComponent` |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Dialogue Graph Editor and participant browser (data-asset authoring is the interim path), network transport and audience filtering (DIA-11 client side), CommonUI and presentation adapters, and the Quest, Inventory, Knowledge, Unlock, Region, Time, Sequence and Events bridges |

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| `UDialogueSubsystem` | `UDocDialogueSubsystem` |
| `UDialogueGraph` | `UDocDialogueGraph` |
| `UDialogueParticipantComponent` / `IDialogueParticipant` | `UDocDialogueParticipantComponent` / `IDocDialogueParticipant` |
| `UDialogueCondition` / `UDialogueAction` | `FDocDialogueCondition` / `FDocDialogueAction` (plain data) |
| `FDialogueSession` | `FDocDialogueSessionSnapshot` |
| `FDialogueNodeHandle` | `FDocDialogueNodeRef` |
| `FDialogueChoice` | `FDocDialogueChoice` |

## Authoring

**Nodes.** The node types are Line, Choice, Branch, Condition, Event, Jump, Delay, End and Custom.

- **Branch** tries its cases in order and takes the first satisfied one; otherwise it goes to `Next`.
- **Condition** is a gate: it goes to `Next` when satisfied and to `FailDestination` otherwise.
- **Destinations.** A `None` destination ends the conversation normally.

**Lines** keep the handoff's fields:

- Speaker role, `FText` text, a soft voice asset, subtitle timing
- Animation, expression and camera tags, plus gameplay tags
- Auto-advance and duration

They add:

- skippability
- a timing source: fixed, voice, or presentation acknowledgement
- a missing-voice policy

**Choices** are:

- identified by a stable `ChoiceId` that is unique in the graph
- given a destination and actions
- optionally marked `bOnceOnly`

Each choice has three condition lists:

| List | If unsatisfied |
|---|---|
| `Conditions` | Hidden |
| `HiddenConditions` | Hidden |
| `DisabledConditions` | VisibleDisabled, with `FailureReason` |

**Conditions** are data, never code strings:

- tag present or missing, on a role or on the context tags plus the initiator
- participant tag
- numeric comparison of a typed variable
- node visited or choice taken, from memory
- EventState, CustomProvider and InterfaceQuery, which go through providers registered by id

A missing provider or missing data gives **Unavailable**, which is never treated as false:

- **Branch or Condition node:** it follows `UnavailableDestination`, or the session fails if that is unset.
- **Choice:** the choice is disabled or hidden, according to the graph's `UnavailableChoicePolicy`.

**Variables** are declared with a type (Bool, Int, Float or Name) and a default, and can be marked persistent.

- **Coercion:** Bool is 0 or 1; Float to Int rounds half away from zero; Name is never numeric.
- **Undeclared names** are rejected, never created.
- **Saved values** that no longer fit the declared type fall back to the default, with a warning.

**Validation** (`FindProblems`, and `IsDataValid` in the editor) reports these as errors:

- missing start node
- duplicate node, choice, action or role ids
- broken links, including redirect targets
- undeclared or wrongly typed variables
- missing participant roles
- malformed conditions and actions
- unconditional zero-wait cycles made only of Jump and Event nodes

Unreachable nodes are warnings, unless the node is marked `bIntentionallyUnreachable`. `StartDialogue` refuses a graph that has errors.

## Participants and reservations

Each binding is a role id plus a stable `ParticipantId`, with an optional participant object. The object must be in the same world and must implement `IDocDialogueParticipant`.

- **Logical participants.** A participant with no object, such as a narrator, is allowed.
- **Identity.** Two NPCs playing the same role are different participants, and one participant cannot fill two roles in one session.

**Reservations:**

- **Exclusive roles** reserve their participant unless the participant allows concurrent dialogue (`bAllowConcurrentConversations`). A narrator can therefore be shared.
- **All or none.** Reservations are taken in a stable order (by `ParticipantId`), and all of them are rolled back if any one conflicts, so no conversation is ever half-reserved.
- **Release.** Every terminal state releases all reservations.

**Loss.** Loss comes from the component's `EndPlay` or from `NotifyParticipantLost`, which covers range, travel and takeover. Each role applies its loss policy:

| Policy | Effect |
|---|---|
| `Cancel` | The session is cancelled |
| `PauseWithTimeout` | The session pauses and ends as TimedOut if not resumed in time |
| `ContinueWithoutActor` | The session continues; conditions on that participant report Unavailable |
| `RebindByIdentity` | The session pauses until a component with the same id registers in the same world, or times out |

The system never loads a level or asset to resolve a portrait or continue a line.

## Execution

**States.** A session moves from PendingBindings to Running, and from there into one of these:

- a waiting state: WaitingForAdvance, WaitingForChoice, WaitingForDelay or WaitingForAction
- Paused
- a terminal state: Completed, Cancelled, Failed or TimedOut

**API.** Every command returns a typed `FDocSystemResult`:

- `StartDialogue`, `Advance`, `SelectChoice`, `Pause`, `Resume`, `Cancel`, `End`
- `AcknowledgeLinePresented`, `CompletePendingAction`
- `GetSessionSnapshot`, `GetAvailableChoices`, `GetVariable`, `SetVariable`

Commands take the session handle and, where a stale view could act, the expected session revision. Typed failures cover stale revision, a choice that is not on the active node, hidden, disabled, unauthorized, wrong state, not skippable, and a missing provider.

**Lines:**

- **Events.** "Shown" and "completed" are separate events that share a stable line ordinal.
- **Races.** When a manual Advance and the auto-advance timer compete, only one transition happens; the late input is rejected as stale.
- **Presentation.** A text-reveal acknowledgement is not an Advance. Acknowledged lines have a safety timeout, so a headless session never deadlocks.

**Budgets.**

- Each dispatch runs at most `MaxStepsPerDispatch` immediate steps, then yields to the next tick.
- More than `MaxNonYieldingSteps` consecutive steps without waiting fails the session with `Doc.Error.Dialogue.RunawayLoop` and a graph/node diagnostic.

**Clocks and ticking.** Delay nodes use their declared clock domain. The subsystem checks only sessions that have deadlines or pending continuations; it does not tick every session.

**Events.** Events are queued and delivered after bookkeeping, so handlers can call back into the subsystem.

## Choice freshness and action commitment

**Choice lists** are stamped with the session revision. `SelectChoice` then:

1. checks that revision
2. checks that the `ChoiceId` belongs to the active node
3. re-evaluates every condition authoritatively

A client cannot submit an old list, a hidden branch, or a cached count.

**Before-transition actions** run in authored order before the transition commits.

- **Variables.** Variable actions are staged and committed together with the transition.
- **External actions.** Event broadcast, tag grant or remove through `IDocMutableGameplayTagProvider`, and custom actions each get a Core effect key: owner, campaign epoch, session id, transition ordinal, action id.
- **Receipts.** Each committed external action is recorded in the owner's receipt ledger.

**Failures and retries:**

- **Required action fails.** A failed required action rejects the transition, the session returns to the choice, and staged variables are discarded.
- **Retry.** Retrying the same transition reuses the same effect keys, so actions that already committed are skipped as `Duplicate` and never applied twice.
- **Irreversible effects.** Actions that commit before a later action fails remain committed. Bridges should order irreversible effects last, or use reservation or transaction semantics.

**Asynchronous actions.** A provider can return `Pending`, and the session waits in WaitingForAction until `CompletePendingAction`.

**After-transition actions** are cosmetic. Their failures are logged only.

**Cancellation** never silently undoes effects. It compensates an action only if the action asks for it (`bCompensateOnCancel`) and its provider advertises `SupportsCompensation`.

## Voice, localization, presentation

All display text is `FText`.

**Line timing** comes from one of three sources:

- **Fixed.** The authored duration, or a text-length estimate.
- **Voice.** The voice duration from a resolver. The default resolver reads only already-loaded sounds and never loads.
- **Presentation acknowledgement.** The line waits for `AcknowledgeLinePresented`, then holds its duration.

**Fallbacks.**

- A missing voice falls back to the authored duration or the text estimate, according to the line's policy.
- Subtitle minimum display time always applies.
- No synthesis, lip-sync or generative dialogue is included.

**Presentation adapters** observe snapshots and events and request their own leases. They never decide outcomes.

**Authority modes** are separate declared capabilities; no voting system is implied:

| Mode | Who can drive it |
|---|---|
| `LocalOnly` | The initiator or audience; works on clients |
| `OwnerAuthoritative` | The initiator only; requires authority |
| `SharedAuthoritative` | The initiator or any audience member; requires authority |

A null requester means trusted host code. Audience filtering at the transport belongs to the network bridge.

## Persistence

Memory is kept per (GraphId, owner scope) and records:

- visited nodes
- choice history
- once-only choices taken
- persistent variables
- receipts
- the campaign epoch
- the graph version
- an optional checkpoint

**Checkpoints** are written only at waiting boundaries.

- **In-flight transitions.** If a transition's actions are still in flight, the checkpoint records it as an intent: the source node, the `ChoiceId`, and the action in flight.
- **Resuming.** `ResumeFromCheckpoint` reuses the saved session id and transition ordinal, so committed actions are not replayed.
- **Ambiguous in-flight actions.** A non-idempotent action that was in flight is never retried automatically. The resume fails with `UnresolvedAction` until the host calls `ResolveCheckpointAction`.
- **Removed nodes.** If the checkpoint's node has been removed, `NodeRedirects` is tried first. If there is no redirect, the result is `NotFound`, unless the host explicitly passes `RestartFromStart`. The system never silently jumps to the first node.
- **What is not saved.** Playback, camera and input locks are transient and never saved.

**Save and restore.**

- `CaptureState` skips session-scoped owners.
- `RestoreState` validates everything before applying, is refused while sessions are active, and makes every old handle stale.

## Tests

The tests are under `Doc.Dialogue.*`:

- LineAdvance
- ChoiceBranch
- HiddenChoice
- DisabledChoice
- Condition
- Jump
- End
- ConcurrentParticipants
- ActionReceipt
- SaveRestore
- VoiceAndLocalization
- AuthorityAndAudience
- Validation

These still need a packaged build, a transport or bridges:

- cooked second culture (DIA-10)
- real network transport and audience scope (DIA-11)
- UI, Inspection and Sequence lease coexistence (DIA-12)
