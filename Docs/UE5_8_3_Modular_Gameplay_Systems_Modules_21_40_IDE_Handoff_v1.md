# Unreal Engine 5.8.3 Modular Gameplay Systems Suite — Modules 21–40
## Highly Detailed IDE Implementation Handoff — Revision 1

**Document revision:** 1.0  
**Prepared:** September 27, 2026  
**Target engine:** Unreal Engine 5.8.3  
**Primary platform:** Windows x64  
**Implementation:** C++ runtime plugins with Blueprint-first public APIs  
**Configuration:** Data Assets, Developer Settings, Gameplay Tags, validated feature-owned schemas  
**Manager lifetime:** Unreal Subsystems with explicit world, owner, and session scope  
**Actor-level behavior:** Components and interfaces; no required project inheritance hierarchy  
**Input:** Enhanced Input through opt-in adapters; no forced mappings  
**Persistence:** Versioned feature records and optional storage bridges  
**Implementation status:** Specification only. This file supplies no plugin implementation, compiled binaries, authored Unreal binary assets, or passing native test/build evidence.

> **Instruction to the implementing IDE agent:** Read Sections 0–2, 23–25, and 34–37 before editing code. Start with Task 0 in Section 36. Audit the actual repository and engine installation. Complete one bounded vertical slice with failure handling, tests, and evidence before expanding it. Do not create twenty empty plugin folders and report the expansion as implemented.

### Source basis and relationship to the earlier handoffs

The twenty candidate descriptions in the preceding conversation are the source for module numbers, names, purposes, exclusions, and first-version boundaries. This document expands those descriptions into **recommended implementation contracts**. New algorithms, interfaces, data models, fixtures, performance workloads, milestones, and test requirements are design proposals introduced here, not previously implemented or independently validated capabilities.

Shared conventions follow the earlier files:

- `UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff_v2.md` — Core and modules 1–10.
- `UE5_8_3_Modular_Gameplay_Systems_Modules_11_20_IDE_Handoff_v2.md` — modules 11–20.

These companion documents describe intended architecture, not verified repository state. Their older scope exclusions apply to their own modules; the present document explicitly authorizes the twenty additional features listed here, without embedding them into earlier base plugins. No new marketplace audit or claim of market uniqueness is made by this handoff.

### Navigation

| Need | Read |
|---|---|
| Start implementation | Sections 0–2 and 36 |
| Locate a module | Section 1 directory; detailed specifications in Sections 3–22 |
| Understand shared transactions, clocks, and simulation coupling | Sections 2 and 23 |
| Follow implementation order | Section 24: proposed Phases 9–13 |
| Configure dependencies and optional bridges | Sections 25 and 33 |
| Define tests, budgets, and builds | Sections 28–32 |
| Continue work in an IDE | Section 34.3 |
| Decide whether a capability is complete | Section 37 |
| Trace scope and references | Appendices A–E |

---

# 0. Document Control and IDE Execution Contract

## 0.1 Requirement language and precedence

**Must** and **must not** are acceptance requirements for the named capability. **Recommended** is the default design, changeable through an architecture decision record. Examples are generic test fixtures, not hard-coded game content. Type names beginning `Doc` are proposed suite APIs unless an existing implementation confirms them. They are not native Unreal APIs.

Preserve repository instructions, licenses, user changes, functioning implementations, published APIs, and save compatibility. Audit before replacing code. Record a conflict between this document and existing behavior in `Docs/DECISIONS.md`; do not silently rename types or reinterpret saved data. A later explicit user instruction overrides this plan's recommendations, not engine constraints or the obligation to report honest verification.

This deliverable defines work; it does not authorize publishing releases, pushing commits, installing paid assets, changing global engine configuration, executing untrusted mods, or uploading diagnostic bundles. Implement only within the selected workspace and the authorization applicable to that implementation session.

## 0.2 Capability labels and release claims

| Label | Meaning | Required evidence |
|---|---|---|
| `SLICE` | Smallest end-to-end increment used to establish the architecture | Real behavior and tests; never advertised as the complete base |
| `BASE` | Independently usable plugin plus Core and essential native dependencies | All module base requirements, isolation tests, authoring, example, and package checks |
| `BRIDGE` | Separately installable native or cross-feature integration | Present/absent, failure, ownership, and teardown tests |
| `EXAMPLE` | Generic demonstration or reference adapter | Real generated/authored assets or executable host code, exercised in Unreal |
| `FUTURE` | Explicitly deferred extension | Recorded backlog; capability query says unsupported until implemented |

A `SLICE` is a subset of `BASE`, not a substitute. All twenty bases remain in the planned expansion. The broader ideas from the candidate descriptions are preserved either in base requirements or named extension boundaries, as recorded in Appendix A.

Use precise release statements: **Module Base Verified**, **Selected Bridge Profile Verified**, and **Modules 21–40 Base Expansion Verified**. List verified engine, platform, build configuration, input, rendering/audio, and networking modes. A data-only acoustic path solver is not a verified audible integration. A timing evaluator tested with a fake clock is not a verified rhythm challenge with working playback.

## 0.3 Exact-engine gate

Unreal Engine **5.8.3 is the requested target**, not a claim that this specification has been compiled against it. Resolve the engine from the existing `.uproject`, installed registrations, or explicit local configuration. Read `Engine/Build/Build.version`; record major/minor/patch, build identity/changelist, compiler, SDK, and target rules.

Use installed headers, plugin descriptors, and module rules to verify exact signatures, reflection support, build dependencies, and available backends. Compile small probes for Quartz, scene capture, UV lookup, media playback, replay, and Game Features before building substantial adapters. Online documentation supports architectural concepts, not every patch-specific implementation detail. Do not downgrade, change the project association, or enable unrelated plugins to make a build appear to pass.

Missing Unreal or an incompatible installation blocks native verification. Continue only bounded source/specification work that can be honestly checked, and record the exact unrun gate. Do not infer that existing modules are absent or complete merely because this document cannot inspect them.

## 0.4 Evidence and status

Implementation states: `Not Audited`, `Not Started`, `In Progress`, `Implemented / Unverified`, `Verified`, `Blocked`.

Test states: `Not Run`, `Passed`, `Failed`, `Skipped with Reason`. A missing device, absent engine, disabled bridge, or unsupported renderer is not a pass.

Evidence must include source revision and dirty-worktree fingerprint, engine/toolchain, enabled plugins, fixture content revision, command/arguments, exit code, discovered and executed tests, results, logs, and generated artifact paths. A successful command with zero tests is not successful verification. Compilation, PIE, cooked launch, Shipping launch, audible playback, and multiplayer behavior are distinct evidence categories.

## 0.5 Scope discipline

Do not add combat, character creation, movement, vehicle physics, commerce, crafting, online services, AI generation, or a replacement renderer as hidden prerequisites. Do not require all earlier twenty modules to be implemented before starting one of these bases. Audit and supply only the narrow shared contracts needed by the next slice.

No plugin may require a host to replace its Pawn, Character, GameMode, PlayerController, GameInstance, camera framework, Asset Manager, UI theme, or save framework. Host-owned interfaces and separate adapters are the integration mechanism.

---

# 1. Scope and Module Directory

This expansion adds exactly twenty feature plugins. The combined planned library has **one shared Core and forty feature bases**; bridges, tools, and examples are additional installation units, not additional numbered gameplay modules.

| Module | Plugin | Primary ownership | Not a replacement for |
|---|---|---|---|
| 21 | `DocPuzzleMechanisms` | Puzzle inputs, rule evaluation, attempts, completion | Interaction, quests, lockpicking |
| 22 | `DocPowerNetworks` | Simplified electrical supply, allocation, storage, breakers | Electrical engineering simulation or security |
| 23 | `DocFluidNetworks` | Finite liquid storage and constrained transport | Water rendering, swimming, CFD |
| 24 | `DocMechanicalNetworks` | Authored drive topology, ratios, coupling, stalls | Chaos physics, animation, vehicle drivetrain simulation |
| 25 | `DocMaterialReactions` | Authored material-state transitions and bounded propagation | Surface feedback, general combat/status effects |
| 26 | `DocServiceQueues` | Waiting order, offers, reservations, service capacity | NPC locomotion, schedules, payments |
| 27 | `DocAcousticSpaces` | Room connectivity and transmission policy | Adaptive soundtrack selection or acoustic wave simulation |
| 28 | `DocOpticalBeams` | Beam paths, channel transmission, receiver activation | Security tripwires or lighting renderer |
| 29 | `DocSurfacePainting` | Persistent paint/clean masks and coverage | Transient decals or universal mesh authoring |
| 30 | `DocEvidenceDeduction` | Evidence relationships, hypotheses, contradictions, conclusions | Codex storage, dialogue, LLM reasoning |
| 31 | `DocPhotography` | Capture records and authored subject evaluation | Camera framework or image recognition model |
| 32 | `DocBroadcastChannels` | Channel schedules, program position, interruptions, receivers | Adaptive Audio or codec implementation |
| 33 | `DocWorldTerminals` | Fictional devices, applications, virtual files, commands | General UI framework or real operating-system shell |
| 34 | `DocRhythmChallenges` | Chart interpretation, input judgment, scoring, calibration | Audio engine or generic combat timing |
| 35 | `DocGestureRecognition` | Local stroke normalization and template matching | Input remapping, abilities, machine-learning platform |
| 36 | `DocAssemblyMaintenance` | Part structure, procedures, assembly state, diagnostics | Crafting, building placement, generic health repair |
| 37 | `DocReplayGhosts` | Selected motion recording and isolated visual playback | Save restoration or universal world rewind |
| 38 | `DocRaceTiming` | Course validation, checkpoints, laps, splits, run records | Movement, navigation, vehicle physics |
| 39 | `DocModContent` | Validated data-pack catalogs and controlled registration | Arbitrary code loader or security sandbox |
| 40 | `DocPlaytestRecorder` | Bounded diagnostic capture and user-controlled evidence export | Crash reporter, telemetry service, guaranteed bug reproduction |

“Drop-in” means documented installation, content setup, and interface/component hookup without modifying plugin source. It does not mean any mesh, audio file, room, machine, or graph works without compatible authoring.

Each base needs a clean isolated host and a second structurally different consumer fixture. At least one relevant participant fixture must use an ordinary non-Character actor. Player-facing modules must prove that two local users do not share private sessions unintentionally.

---

# 2. Shared Architecture and Behavioral Contracts

## 2.1 Independent plugins and small Core

Each base depends only on `DocModularCoreRuntime` and native modules essential to its own advertised base behavior. Core imports no feature. Optional native integrations and all sibling integrations belong in bridges. A base must compile with siblings physically absent, not just disabled in the editor.

Unreal distinguishes module rules from plugin descriptors and discovers plugins through descriptor-bearing directories. Keep grouping folders descriptor-free and Runtime/Editor functionality separated. [R1]

A reflected `TSoftObjectPtr<UOtherFeatureDefinition>` still creates a dependency on that feature's C++ type. Use a bridge-owned configuration, feature-neutral ID, or narrow provider interface instead. Never hide dependency leakage behind `LoadModule` calls or dummy plugins in an isolation host.

Do not put twenty feature models into Core. Share only low-level concepts genuinely needed by multiple modules: results, world/owner context, persistent identity, transient handle primitives, and established control/clock/provider contracts. A reusable graph algorithm may become a private implementation utility after real reuse is demonstrated; it is not permission to force electrical, mechanical, narrative, and acoustic graphs into one gameplay model.

## 2.2 Public types and immutable definitions

Use `UDoc...`, `ADoc...`, `FDoc...`, and `IDoc...` for new public types after an API audit. Implement algorithms in C++; expose commands, side-effect-free queries, events, configuration, and extension interfaces through valid Unreal reflection. Verify an independent C++ consumer and Blueprint-only consumer; no private-header access or project-specific casts.

Definitions, Data Assets, templates, and class defaults remain immutable during play. Session progress, brush masks, evidence state, energy, reservations, recordings, and calibration belong in runtime records. Instanced actions require per-session ownership or immutable evaluation with external state. GC-safe references and async proxy lifetime are implementation requirements, not details to leave for final cleanup.

## 2.3 Scope, lifetime, and identity

Subsystems provide managed lifetimes; a subsystem's lifetime does not by itself define player ownership or replication. [R2]

| Scope | Intended use |
|---|---|
| World service | Networks, material state, queues, optical/acoustic topology, painting registration, broadcasts, terminal devices, assembly objects, race runs |
| LocalPlayer service | Photography sessions, rhythm/gesture input, terminal presentation, replay viewing, private interaction presentation |
| GameInstance coordinator | Owner-scoped evidence/records/catalogs and diagnostic export data that may span travel |
| Editor service/module | Validators, graph/fixture tools, inspectors, import tools, report viewers |

Reuse `FDocOwnerScope` where present: scope kind, stable subject identity, campaign/world namespace. A player array index, controller ID, transient network ID, or actor name is not durable profile identity. A client-supplied owner ID never grants access to that owner's state.

Persistent identity follows the companion contract:

```text
PersistentObjectId = (WorldNamespaceGuid, StableInstanceScope, LocalObjectGuid)
```

Repeated/nested placements need distinct instance scopes. Components, ports, graph nodes, parts, hypotheses, notes, gates, and channels need stable feature-owned IDs within their parent. Array reorder must not rewrite identity. Runtime operation handles are transient and must not be restored as live claims.

Every mutation context carries an epoch and state revision. Travel, restore, or world replacement invalidates old epochs. Reject stale asynchronous work and stale expected-revision commands. Avoid global live-object registries, `GWorld`, and implicit player zero.

## 2.4 Requests, cancellation, and terminal outcomes

Long-lived operations use opaque typed handles and applicable states:

```text
Pending → Running → Succeeded | Failed | Cancelled | TimedOut
```

A live request records exactly one terminal outcome; surviving listeners receive at most one terminal callback on the game thread. Destroyed listeners receive none. Cleanup must still release the request's owned resources.

Each command documents validation, duplicate-key semantics, expected revision, commit point, cancellation, timeout, and retryability. Before commit, cancellation leaves no committed mutation. After commit, return the committed result or TooLate; do not falsely report that the operation never happened. Release/unsubscribe are repeat-safe. Reentrant commands are queued until the current commit/notification boundary is complete.

Reuse `FDocSystemResult` and distinguish NoChange, InvalidInput, NotFound, NotReady, Unavailable, Unsupported, PermissionDenied, Conflict, Cancelled, TimedOut, and actual execution/storage errors. Include correlation ID, owner/context, diagnostic detail, and localized presentation text where appropriate.

## 2.5 Queries, events, and cross-feature effects

Queries and condition evaluation do not mutate state. Conditions distinguish Satisfied, Unsatisfied, and Unavailable/Error. Missing power, inventory, time, or knowledge providers are not implicit true conditions and not legitimate zero-valued measurements.

Commands mutate one owning feature through an explicit authority boundary. Events describe committed facts. External actions are separate intents with stable effect keys. Reuse the companion producer-outbox/consumer-receipt model where retriable effects are advertised:

```text
EffectKey = (OwnerScope, CampaignEpoch, ProducerInstanceId, TransitionOrdinal, ActionId)
```

A duplicate key with the same payload returns the earlier receipt; a duplicate key with different payload is a conflict. Persist receipts coherently with affected state. Arbitrary Blueprint actions and external services are not automatically transactional or exactly-once. Non-idempotent actions need an explicit non-retryable/reconciliation policy.

This matters for puzzle rewards, accepted deductions, photography objectives, rhythm results, terminal commands, repaired-part transfers, and race records. Restore/replay must not generate fresh rewards.

## 2.6 Time domains and reproducibility

Every feature declares its clock: simulation ticks, gameplay time, monotonic elapsed time, audio transport time, or explicitly opted-in wall time. Define pause, time dilation, time jumps, travel, and restore semantics. Do not derive rhythm judgment from delayed UI callbacks or reserve a queue slot using an unrelated day/night clock.

Simulation models use explicit fixed steps, bounded catch-up, deterministic ordering, and versioned parameters. Stable ordering and fixed time steps improve reproducibility but do not prove cross-platform bitwise determinism. State precisely whether tests guarantee exact integer results, bounded floating-point tolerance, or only qualitative behavior.

A budget overrun must be visible. Do not clamp away elapsed time and claim conservation/time accuracy. A network solver may retain backlog and expose stale simulation time; a challenge may mark a run timing-degraded; an acoustic path query may return a bounded approximation. Policies must be explicit and tested.

## 2.7 Common graph transaction pattern

Networks and puzzle graphs use feature-specific schemas with a common discipline:

```text
Validate proposed edit → Stage topology → Check IDs/limits/invariants
→ Commit topology revision → Invalidate affected caches
→ Evaluate coherent snapshot → Publish complete results
```

Commands received during evaluation apply at the next declared boundary. Workers operate on immutable snapshots tagged with epoch/topology revision; outdated results are discarded or recomputed. Do not publish half of a new graph beside half of an old graph.

Distinguish structural errors, unsupported topology, and unresolved streamed endpoints. Missing endpoints suspend or isolate the affected subgraph according to policy; they do not silently create power, fluid, service capacity, evidence, or beam energy. Avoid discovering all actors every frame.

## 2.8 Persistence and unloaded participants

Each persistable base supplies Capture, Validate/Migrate, Stage, Apply, ResolveReferences, and Finalize contracts with feature-owned schemas. A `DocSave` bridge or host backend supplies disk durability. Memory capture alone is not persistence to disk.

Save logical records and definition versions, not UObject addresses, scene/audio handles, GPU resources, timer handles, or active authority tokens. Include unloaded records; unregistration is not intentional destruction. Restore under a barrier, rebuild derived caches, and publish refreshed state rather than replaying historical effects.

Capture related feature records coherently. A paint mask plus stroke journal, battery state plus committed tick, assembly state plus inventory transaction receipt, or result record plus reward receipt must not come from incompatible snapshots. Retain and quarantine unknown optional records; reject unknown critical schemas without overwriting a valid save.

Unreal's async save API serializes on the game thread and performs platform storage asynchronously; moving a save call does not make arbitrary UObject serialization worker-safe. [R13]

## 2.9 Control, resource, and presentation ownership

Photography, terminals, rhythm, gestures, inspection, dialogue UI, sequences, and replay viewing must share the host's `IDocPlayerControlProvider` per local player. Acquire scoped leases for input, focus, camera, cursor, and pause; releasing one lease recomputes remaining ownership instead of restoring a stale snapshot over a newer owner.

Likewise use explicit ownership for streaming residency, audio parameters, material overrides, activation pins, render targets, part manipulation, and device exclusivity. A missing required provider produces an explicit capability error or a documented no-control-mutation mode.

No plugin may globally alter another owner's audio attenuation, shared material asset, collision, physics state, or input mappings and later assume an old snapshot is still authoritative.

## 2.10 Assets, rendering, audio, and threading

Declare asset types, discovery paths, bundles, and cook rules. Retain async loading handles for their required lifetime. Asset discovery, loading, and cooking are separate concerns in Unreal's Asset Manager. [R3]

Keep live UObject access and reflected gameplay callbacks on the game thread unless a verified native API explicitly permits otherwise. Workers may process immutable bytes, numerics, validated records, or other documented thread-safe data. Render/audio callbacks marshal results with request generations; they do not mutate arbitrary gameplay state directly.

Do not initialize graphics or audio in a dedicated-server/headless context. Distinguish logical base tests from actual rendered/audible tests. NullRHI cannot verify photographs, material masks, scene-capture fidelity, or GPU readback. A silent audio device cannot establish perceptual timing or speaker synchronization.

## 2.11 Security, privacy, and accessibility defaults

Validate external lengths, identifiers, paths, schemas, counts, and numeric values before allocation or mutation. Reject NaN/Infinity, arithmetic overflow, unsafe paths, unsupported encodings, and unregistered types. Data-pack schemas and terminal commands are allowlisted; no arbitrary object deserialization, console execution, operating-system commands, or automatic network access.

Diagnostics are local-first and opt-in; redact before retention, not only before export. Screenshots and user text may contain private material. Do not record passwords, authentication tokens, arbitrary keystrokes, clipboard content, microphone input, or private files by default.

Expose localized `FText`, scalable presentation data, non-color-only cues, reduced-motion/flash options, configurable timing assistance, and alternate gesture/drawing input routes. Assistance profiles must be recorded in scored results rather than misrepresented as identical competitive conditions.

## 2.12 Shared acceptance gates

| ID | Required observation |
|---|---|
| X21-01 | Every base builds and starts with Core and essential native dependencies only; sibling plugins physically absent |
| X21-02 | Public C++ and Blueprint-only consumers work without private headers or project casts |
| X21-03 | Two worlds, two owners, and concurrent sessions remain isolated |
| X21-04 | Cancellation, timeout, travel, owner loss, and late callbacks release only owned resources |
| X21-05 | Definitions remain immutable and state revisions/epochs reject stale mutations |
| X21-06 | Missing optional providers return documented failures without invalidating the base |
| X21-07 | Restore is versioned, idempotent, and suppresses historical rewards/effects |
| X21-08 | Claimed reproducibility is verified under declared ordering, time-step, and numeric tolerance |
| X21-09 | External input, arithmetic, record counts, and resource limits are validated before work |
| X21-10 | Cooked Win64 fixtures resolve required assets and configuration without editor-only dependencies |
| X21-11 | Control/audio/material claims coexist with earlier modules and release without clobbering |
| X21-12 | Evidence distinguishes logical, native, rendered, audible, packaged, and network verification |
---

# 3. Module 21 — DocPuzzleMechanisms

## 3.1 Purpose, ownership, and capability boundary

Provide reusable mechanical/logical puzzle rules: ordered inputs, simultaneous conditions, weighted thresholds, timed windows, symbol combinations, alternative solutions, partial progress, failures, and controlled resets. Interaction detects and authorizes operating a control; this plugin evaluates the arrangement of inputs. Quests and rewards remain external consumers.

**SLICE:** Three ordered controls, one attempt, correct/incorrect outcome, reset, and an independent Blueprint consumer.  
**BASE:** Ordered, simultaneous, weighted-threshold, symbol-combination, and timed-sequence evaluators; nested All/Any composition; attempts, progress, reset policies, versioned snapshots, and a generic example.  
**BRIDGE:** Interaction inputs, Events output, Save, Optical receiver inputs, Power conditions, objective/reward delivery.  
**FUTURE:** General visual scripting, arbitrary cyclic logic, procedural puzzle generation, solver-generated hints. These are not needed to author the base puzzle types.

## 3.2 Runtime types and lifetime

Use `UDocPuzzleSubsystem : UWorldSubsystem`, `UDocPuzzleComponent`, `UDocPuzzleInputComponent`, `UDocPuzzleDefinition : UPrimaryDataAsset`, immutable `UDocPuzzleRule` definitions, and `IDocPuzzleInputProvider`. State types include `FDocPuzzleInstanceId`, `FDocPuzzleAttempt`, `FDocPuzzleInputEvent`, `FDocPuzzleInputState`, `FDocPuzzleEvaluation`, and `FDocPuzzleSnapshot`.

A definition may create many independent instances. A puzzle instance has an explicit owner scope, stable object identity, definition revision, active attempt ID, state revision, and world epoch. World-shared puzzles and per-player copies are different instance policies, not one global progress counter.

## 3.3 Definitions and runtime data

| Record | Required fields |
|---|---|
| Definition | DefinitionId, schema/content version, display text, input IDs, rule graph, root rule ID, timeout clock, reset policy, completion policy, resource limits |
| Input definition | InputId, accepted value kind, semantic tags, edge/level semantics, valid range, repeat/debounce policy, required provider capability |
| Input event | SourceId, source sequence number, instance/attempt ID, accepted timestamp, input ID, typed value, authority context |
| Rule | Stable RuleId, kind, child/input IDs, threshold/order/window, missing-input policy, localized unmet reason |
| Attempt | AttemptId, progress per rule, last accepted source sequence, deadlines, active input contributors, terminal result, effect intents |

Use closed variants for Boolean, bounded numeric, symbol ID, and trigger inputs. A numeric value cannot silently coerce to a trigger. Debounce is declared per input and must not suppress intentionally repeated symbols required by a sequence.

## 3.4 Evaluation and timing contract

Order accepted inputs by authority-assigned sequence; use stable IDs for ties in pure fixtures. Client timestamps may be retained as evidence but do not retroactively reorder authoritative puzzle history. Duplicate `(SourceId, SourceSequence)` events cannot advance progress twice.

An ordered rule declares its mismatch policy: ResetProgress, FailAttempt, or IgnoreUnexpected. Repeated identical steps need distinct accepted trigger edges. A held button cannot satisfy a sequence multiple times through polling.

Simultaneous rules evaluate level state from **all active contributors**. One contributor leaving a weighted plate removes only its own weight. A simultaneous dwell rule succeeds only while every required condition remains satisfied for the declared continuous duration. Disconnecting an input makes it Unavailable and follows the authored policy; it must not preserve a false permanent pressed state.

Timed windows use half-open intervals `[start, deadline)`. Inputs accepted exactly at the deadline are late unless a definition explicitly selects another boundary policy. Evaluate an event against the deadline before applying it; frame order between timeout callback and input callback must not change the result. Pause, time jumps, and restore use the declared clock and remaining duration.

Composition is a validated acyclic graph. Bound graph depth and nodes per evaluation. Alternative solutions identify the winning rule; two alternatives completing in the same revision produce one puzzle completion, not duplicate rewards. Explanations expose unsatisfied rule IDs without necessarily revealing hidden solutions to unauthorized audiences.

## 3.5 State machine and public API

```text
Inactive → Ready → AttemptActive → Solved | Failed
                     ↓               ↓
                   Resetting ← authorized reset
```

Solved is terminal for a one-shot instance. Repeatable definitions create a new AttemptId on reset. A reset never erases an already committed external reward receipt; replay rewards require an explicit repeat policy and new effect identity.

| API contract | Behavior |
|---|---|
| `RegisterPuzzle` / `UnregisterPuzzle` | Bind/unbind a live participant without silently deleting retained state |
| `StartAttempt` | Validate owner, readiness, definition version, and reset/completion policy |
| `SubmitInput` | Validate source, attempt, sequence, type, range, clock boundary, and authority |
| `SetInputContributor` / `RemoveInputContributor` | Maintain source-owned level contributions; repeat-safe removal |
| `EvaluatePuzzle` / `GetProgress` | Side-effect-free view with rule evidence and state revision |
| `RequestReset` | Commit one reset revision, clear attempt-owned timers/input history as configured |
| `CapturePuzzle` / `StageRestore` | Versioned detached state; no replay of historical input callbacks |

Expose OnInputAccepted/Rejected, OnProgressChanged, OnAttemptFailed, OnPuzzleSolved, and OnResetCommitted. Notifications follow committed state. Nested listener commands cannot observe or mutate a partially reset attempt.

## 3.6 Persistence, authority, and integration

Persist solved state, attempt identity, versioned rule progress, remaining supported timers, durable contributors where meaningful, and effect receipts. Runtime pointer-backed contributors must re-register and revalidate; restore cannot invent a player still standing on a plate. The default ongoing physical-level puzzle policy resumes only after inputs are resolved.

Server-owned shared puzzles validate participant permission and input source. Private puzzle views do not receive hidden answer keys through replication merely to render progress. A standalone local instance uses the same validation path without transport.

## 3.7 Authoring and example

Provide a definition validator, input-link inspector, rule-progress debugger, and reset preview. The base editor may use structured Details panels; a custom graph editor is not a prerequisite. Validate missing input IDs, cycles, empty solution sets, duplicate rule IDs, impossible static ranges, and unsupported time policies.

The example includes an ordered panel, two weighted plates plus a held control, and a timed symbol sequence. Operate it with explicit API calls without DocInteraction installed, then demonstrate an optional interaction bridge separately.

## 3.8 Acceptance and baseline tests

| ID | Test name | Required result |
|---|---|---|
| PUZ-01 | `Doc.Puzzle.OrderedInputs` | Correct order succeeds; each mismatch policy behaves as authored |
| PUZ-02 | `Doc.Puzzle.DuplicateInput` | Replayed source sequence does not advance or reward twice |
| PUZ-03 | `Doc.Puzzle.ContributorOwnership` | Removing one plate contributor retains other active contributors |
| PUZ-04 | `Doc.Puzzle.SimultaneousDwell` | A broken condition resets continuous dwell; no early completion |
| PUZ-05 | `Doc.Puzzle.DeadlineBoundary` | Deadline-edge outcomes are independent of callback ordering |
| PUZ-06 | `Doc.Puzzle.AlternativeSolutions` | Two satisfied alternatives commit one terminal transition |
| PUZ-07 | `Doc.Puzzle.ResetEpoch` | Old-attempt inputs and delayed callbacks are rejected after reset |
| PUZ-08 | `Doc.Puzzle.RestoreNoEffects` | Restore preserves progress without replaying completion effects |
| PUZ-09 | `Doc.Puzzle.InvalidGraph` | Cycles, missing inputs, oversized graphs, and invalid ranges fail clearly |
| PUZ-10 | `Doc.Puzzle.IsolatedConsumers` | Headless C++/Blueprint use works without Interaction, Quest, or Save |

---

# 4. Module 22 — DocPowerNetworks

## 4.1 Purpose and scope

Model gameplay-level power availability through sources, batteries, connected circuits, switches, breakers, and consumers. Own finite energy accounting and allocation, not realistic voltage/current waveforms, wiring safety, security devices, or lighting rendering.

**SLICE:** One source, one switch, two prioritized loads, and a battery with measurable depletion.  
**BASE:** Connectivity islands, sources, finite batteries, on/off and scalable consumers, stable priority allocation, breaker trip/reset, disconnection, brownout hysteresis, and conserved per-step energy.  
**BRIDGE:** Powered machinery, broadcasts, terminals, optical emitters, optional Time/Save/Events.  
**FUTURE:** AC phases, impedance networks, Kirchhoff circuit solving, realistic motor startup, and general electrical simulation.

## 4.2 Types and data ownership

Use `UDocPowerNetworkSubsystem : UWorldSubsystem`, `UDocPowerNodeComponent`, `UDocPowerSourceComponent`, `UDocPowerConsumerComponent`, `UDocPowerStorageComponent`, `UDocPowerNetworkDefinition`, and feature-owned solver records. Suggested types: `FDocPowerPortId`, `FDocPowerEdge`, `FDocPowerIslandState`, `FDocPowerAllocation`, and `FDocEnergyLedger`.

A connection graph describes conductive connectivity, not current distribution. Stable node/port/edge IDs survive streaming and authoring reorder. Runtime data includes topology revision, step ordinal, available supply, requested/granted power, storage energy, breaker state, pending changes, and diagnostics.

## 4.3 Quantities and configuration

Use seconds, watts, and joules in the logical model; `energy = power × duration`. Keep integer fixed-unit quantities or double precision with explicit tolerance and overflow/range checks. Display units are a presentation conversion.

Source fields: maximum power, availability, priority, ramp policy if enabled, and whether energy is externally supplied. Battery fields: capacity joules, current energy, maximum charge/discharge power, charge/discharge efficiencies in `(0,1]`, and operating mode. Consumer fields: desired/minimum power, priority, stable tie-break ID, Binary or Scalable allocation, and recovery/dropout hysteresis.

A connected consumer is not necessarily powered. Expose Disconnected, Off, Supplied, Brownout, and Faulted states separately. A simple binary consumer receives its complete required allocation or zero; scalable behavior is opt-in.

## 4.4 Solver and conservation contract

At each fixed step, snapshot topology and source/load requests. Compute islands using closed enabled connections. Allocate finite source capacity to eligible loads using priority, then stable ID; never hash-map order. Document whether equal-priority fairness is fixed ordering or an explicit fair-share policy. Base uses fixed ordering, with starvation visible rather than disguised as fairness.

Discharge eligible batteries to cover remaining demand, constrained by initial stored energy, output efficiency, and maximum power. Charge batteries only from remaining external-source supply after committed load allocation. A battery must not charge and discharge in the same step; batteries must not charge each other by cycling energy through the island.

For discharge output `D` joules and efficiency `eta_d`, storage decreases by `D / eta_d`. For charge input `C` and efficiency `eta_c`, storage increases by `C × eta_c`. Validate these limits before allocation, not by clamping a negative battery after the fact.

The per-step ledger reconciles:

```text
ExternalEnergyAccepted + StoredEnergyBefore
= LoadEnergyDelivered + StoredEnergyAfter + ConversionLosses + ExplicitDiscard
```

Unused source capacity is not accepted/generated energy. Distinguish potential production from accepted energy. A disconnected node does not erase stored energy. Negative demand, invalid efficiency, zero/negative step duration, non-finite values, and numeric overflow fail validation.

## 4.5 Breakers, topology, and stability

Base breakers protect an island or an explicitly authored radial branch using a gameplay power/load threshold and configurable trip duration. They do not infer actual current on arbitrary loops. A branch-rated breaker must validate a unique protected downstream branch; unsupported meshed protection returns Unsupported rather than assigning invented currents.

Trip decisions use the declared overload metric from the coherent pre-allocation demand snapshot or an explicitly selected delivered-load policy. Record which metric is used. Commit trips at the next defined solver boundary and recompute affected topology; limit repeated changes per step. Reset requires permission and a cooldown/configured stability check. Brownout hysteresis prevents loads from rapidly toggling around thresholds.

The island solver runs only when relevant state changes or active energy/time evolution requires it. Never attach an unbounded per-consumer tick. On budget exhaustion retain unprocessed steps and expose simulation lag; offline elapsed-time catch-up requires its own policy and cannot create free battery charge.

## 4.6 API, lifecycle, and networking

Expose RegisterNode, ConnectPorts, DisconnectPorts, SetSwitchState, SetSourceAvailability, SetConsumerDemand, SetStorageMode, RequestBreakerReset, GetNodeSupply, QueryIsland, and CaptureNetworkState. Mutations validate expected topology/state revision; structural edits are atomic.

A `PowerSupplyChanged` delegate reports committed availability and granted watts, not an instruction to directly mutate a sibling component. Bind a provider/bridge for the consuming device. Devices declare their response to stale/unavailable supply.

On stream unload retain detached logical records when configured, or suspend/isolate their ports. Do not keep unloaded actors alive. Persistence includes batteries, authored switch states, breaker latches, and last committed step; rebuild islands on restore. Networking replicates authoritative snapshots/deltas and revisions, not client-solved battery changes. Visual meters may interpolate but cannot feed authority.

## 4.7 Authoring and example

Provide connection previews, island colors, per-node demand/allocation tables, breaker reasons, and an energy ledger inspector. A generic fixture shows a generator, battery, three prioritized consumers, and a branch that trips. Powering one consumer must work through a plain interface with no other feature installed.

## 4.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| PWR-01 | `Doc.Power.ConnectivityIslands` | Switch/disconnect edits update exactly the affected supply islands |
| PWR-02 | `Doc.Power.PriorityAllocation` | Allocation is stable under registration-order permutations |
| PWR-03 | `Doc.Power.BatteryConservation` | Step ledger balances and energy remains within valid bounds |
| PWR-04 | `Doc.Power.NoChargeDischargeLoop` | Battery cannot create energy by charging/discharging in one step |
| PWR-05 | `Doc.Power.BinaryAndScalableLoads` | Minimum supply and brownout policies match authored behavior |
| PWR-06 | `Doc.Power.BreakerTripReset` | Trip timing, protected branch, and reset rules are correct |
| PWR-07 | `Doc.Power.UnsupportedProtection` | Meshed branch-current assumptions are rejected explicitly |
| PWR-08 | `Doc.Power.TopologyDuringSolve` | Stale snapshot results cannot overwrite newer topology |
| PWR-09 | `Doc.Power.UnloadRestore` | Battery energy and breaker state survive unload/restore without extra ticks |
| PWR-10 | `Doc.Power.HitchAndBounds` | Catch-up, invalid numeric inputs, and extreme loads remain bounded and honest |

---

# 5. Module 23 — DocFluidNetworks

## 5.1 Purpose and scope

Move finite quantities of a single authored liquid through reservoirs, pipes, valves, pumps, and outlets. Support filling, draining, leaks, blocked links, flow limits, and simplified head/pressure eligibility. Rendering a water surface, buoyancy, swimming, farming behavior, and computational fluid dynamics are not base responsibilities.

**SLICE:** Two tanks, one valve, one pump, and a conserved transfer.  
**BASE:** Directed transfer graph, branching reservoirs, finite storage, valves, pumps, capped leaks/outlets, rate limits, explicit sources/sinks, coherent fixed-step evaluation, and versioned records.  
**BRIDGE:** Power-driven pump availability, materials/audio effects, optional Save and Events.  
**FUTURE:** Multiple liquids, mixing, temperature, compressibility, realistic pipe pressure, and zero-volume junction solvers with shared segment constraints.

## 5.2 Types and record model

Use `UDocFluidNetworkSubsystem : UWorldSubsystem`, `UDocFluidReservoirComponent`, `UDocFluidPortComponent`, `UDocFluidPumpComponent`, `UDocFluidValveComponent`, `UDocFluidNetworkDefinition`, `FDocFluidTransfer`, `FDocFluidReservoirState`, and `FDocFluidLedger`.

Every storage node has stable identity, capacity, current volume, liquid definition ID, normalized fill/head metadata, and revision. Every edge has stable endpoints, direction, maximum flow, valve opening, blockage state, pump requirements, and transfer policy. A pipe edge in the base does **not** store an untracked volume of liquid. In-pipe contents require explicit storage nodes and capacity accounting.

Use a documented volume unit, such as liters, and seconds. Single-liquid constant-density volume conservation is the base invariant; do not claim mass conservation for future variable-density mixtures without defining it.

## 5.3 Flow proposal and simultaneous application

The base uses a bounded, gameplay-rate model, not a physical pressure solution. A transfer candidate is limited by its edge's maximum rate, valve opening, source availability, destination capacity, and an authored head/pump eligibility function. Simplified head values must be named gameplay parameters, not mislabeled physical measurements.

Per step:

```text
Snapshot volumes and topology
→ Propose edge transfers
→ Constrain total outgoing volume per source
→ Constrain total incoming volume per destination
→ Commit all final transfers together
→ Publish ledger and changed reservoirs
```

Scale competing outgoing proposals proportionally to initial source volume; then scale competing incoming proposals to the destination's **initial free capacity**. The second scaling only reduces transfers, so source constraints remain valid. Use stable summation order and a documented rounding/residual distribution policy. Do not credit outgoing volume as new free capacity or incoming volume as new supply in the same step. This conservative rule introduces a declared one-step transport delay but avoids order-dependent duplication.

Base branches are connections among storage nodes. Do not introduce invisible zero-capacity junctions that accidentally lose flow or allow a shared pipe segment to exceed its rating. More complex hydraulic routing is an extension with its own solver.

Each accepted transfer appears once as a source decrement and once as a destination increment. Leaks/outlets transfer to explicit sink ledger entries; externally supplied liquid comes from explicit source ledger entries with rate limits.

```text
TotalVolumeAfter = TotalVolumeBefore + ExternalInflow - ExternalOutflow
```

## 5.4 Pumps, valves, failures, and topology

A pump has Enabled, Disabled, UnavailableSupply, Blocked, and Faulted outcomes as applicable. Its input can be supplied directly in the base; the Power bridge provides optional powered availability. A stopped pump prevents its authored transfer path, not every path in the network.

Valves validate opening in `[0,1]`; invalid user configuration fails rather than silently using an arbitrary value. Closed valves do not erase reservoir contents. A leak cannot drain below zero; an external source cannot overfill a tank. Closed loops are supported only as simultaneous bounded reservoir transfers, not as instantaneous zero-time circulation.

Commit graph changes between steps. Removing a storage node requires a declared transfer/quarantine/discard decision recorded in the ledger; it must not silently destroy liquid. Stream unload is not storage-node deletion. Unresolved endpoints suspend the affected edge, retain quantities, and surface diagnostics.

## 5.5 API and state lifecycle

Expose RegisterReservoir, RegisterConnection, SetValveOpening, SetPumpEnabled, SetExternalSupply, SetLeakRate, RequestDrain, QueryReservoir, QueryTransferRates, CaptureState, and StageRestore. A drain request returns the amount actually removed, remaining requested volume, reason, and committed revision. Requests for impossible volumes are not automatically partial unless the API explicitly permits partial fulfillment.

Queries return coherent committed snapshots plus simulation timestamp and staleness. Expensive catch-up uses a bounded step budget; large offline jumps are not integrated in one giant unstable step. A default suspended unloaded network does not secretly continue consuming liquid.

Persist capacities/definition versions as references plus runtime volumes, controls, and committed simulation time. Validate migrated capacities before restore. When a content update shrinks a tank below saved volume, quarantine or apply an explicit spill migration with a recorded sink; never silently clamp away the discrepancy.

Authoritative servers own shared liquid mutation. Replicate quantized volumes and revisioned control changes under a stated visual tolerance, not per-particle movement. A cosmetic water-height adapter must not write measured mesh height back as authoritative volume.

## 5.6 Authoring and example

Provide directed connection overlays, source/sink visualization, volume-ledger inspection, and validation for duplicate ports, invalid capacities, mixed liquid IDs, missing endpoints, and unsupported junction assumptions. The example drains one reservoir into two others, blocks a branch, enables a pump, and introduces a capped leak.

## 5.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| FLU-01 | `Doc.Fluid.SingleTransfer` | Source loss equals destination gain under the declared units |
| FLU-02 | `Doc.Fluid.CompetingOutflows` | Multiple edges cannot overdraft one reservoir |
| FLU-03 | `Doc.Fluid.CompetingInflows` | Multiple sources cannot overfill a destination |
| FLU-04 | `Doc.Fluid.OrderIndependence` | Registration permutations yield equal/tolerance-bounded results |
| FLU-05 | `Doc.Fluid.ValvePumpBlockage` | Each control affects only eligible transfers and retains stored volume |
| FLU-06 | `Doc.Fluid.ExplicitSourcesSinks` | Inflow, leaks, and drains reconcile in the volume ledger |
| FLU-07 | `Doc.Fluid.LoopAndHitch` | Cycles and bounded catch-up do not create liquid or exceed limits |
| FLU-08 | `Doc.Fluid.UnresolvedEndpoint` | Stream loss suspends transfers without deleting contents |
| FLU-09 | `Doc.Fluid.CapacityMigration` | Smaller restored capacity follows an explicit non-silent policy |
| FLU-10 | `Doc.Fluid.InvalidNumbers` | Negative/non-finite values, invalid types, and unsupported junctions fail safely |

---

# 6. Module 24 — DocMechanicalNetworks

## 6.1 Purpose and capability boundary

Transmit authored rotational drive through motors, shafts, gears, belts, clutches, and driven outputs. Calculate direction, ratio, simplified load, stalls, and engagement. The base is a **kinematic gameplay model**, not rigid-body simulation or a realistic drivetrain solver.

**SLICE:** Motor → gear pair → clutch → driven shaft, with a visible ratio and stall.  
**BASE:** Directed acyclic drive chains/trees, one active root driver per connected component, gear/belt ratios, engagement, load reflection, static stall policy, phase continuity, and snapshots.  
**BRIDGE:** Power supply, animation/transform presentation, assembly parts, optional Save/Events.  
**FUTURE:** Closed mechanical loops, multiple coupled drivers, inertia, elastic belts, true torque curves, and physics constraint coupling.

The initial candidate's slipping behavior remains a named extension: a deliberately authored speed-reduction/load-loss model may be added after base stall behavior; do not call it physical friction simulation.

## 6.2 Types and definitions

Use `UDocMechanicalNetworkSubsystem : UWorldSubsystem`, `UDocMechanicalNodeComponent`, `UDocDriveSourceComponent`, `UDocMechanicalLoadComponent`, `UDocClutchComponent`, `UDocMechanicalNetworkDefinition`, `FDocDriveEdge`, `FDocMechanicalNodeState`, and `FDocDriveSolveResult`.

Definitions contain stable ports/edges, signed speed ratios, efficiency, maximum operating speed, output axis and rest transform for adapters, source speed/capacity, load requirements, and clutch rules. Runtime records contain topology revision, engaged edges, requested/actual source speed, angular phase, fault/stall state, and reflected load evidence.

Use radians/second and Newton-meters where the simplified model exposes physical-style units; document which quantities are authored abstractions. Cosmetic rotations must convert from these values rather than use unrelated per-frame increments.

## 6.3 Topology and ratio evaluation

Validate a rooted directed acyclic graph with one driver for each active component. Multiple paths to the same driven node or cycles are unsupported in the base even if ratios appear compatible. Reject a topology edit before commit, with offending edge IDs and an actionable reason.

For an edge with signed ratio `r`:

```text
omega_child = r × omega_parent
```

For an external gear pair, a default authored ratio may be `-teeth_parent / teeth_child`; an open belt may use a positive radius ratio. Ratios must be finite and nonzero. Authoring helpers may derive them, but the evaluator uses the stored validated value and explicit direction.

Compute absolute child speed limits before publishing a result. Do not keep applying an invalid ratio and clamp only the final mesh rotation. Detached outputs follow HoldPhase or FreeVisualCoast policy; true momentum conservation is not claimed.

## 6.4 Load reflection and stalls

Reflect a resisting child load into the parent using the absolute speed ratio and efficiency:

```text
required_parent_torque = abs(r) × child_load_torque / efficiency
```

Sum branch requirements in stable order. This is a simplified steady-state demand model. At zero actual speed, do not divide power by zero to derive infinite torque. The source supplies authored torque capacity; base stall occurs when reflected demand exceeds it under the configured hysteresis.

Base policy stalls the active connected drive component. Branch-isolated failure requires an explicit clutch or an extension policy, not implicit removal of inconvenient loads. A stalled state produces zero actual angular advancement while retaining a nonzero requested speed and unmet-load diagnostic.

Opening a clutch commits a topology change; closing it validates the resulting topology and source count. Support an authored engagement delay or speed-difference threshold as base configuration only where the behavior is explicitly implemented. Do not instantly snap a detached shaft's saved phase to the driver's current phase.

## 6.5 Phase, updates, and APIs

Integrate angular phase over fixed simulation steps using double precision and a bounded/wrapped representation for rendering. Keep accumulated revolution information separately only when gameplay needs it. Cosmetic interpolation must not create extra logical revolutions or trigger output events twice after a hitch.

Expose RegisterNode, ConnectDrive, SetSourceSpeed, SetSourceCapacity, SetLoad, RequestClutchState, QueryNodeDrive, QueryReflectedLoads, CaptureState, and StageRestore. Requests carry topology revision; unsupported edits fail atomically. OnDriveChanged, OnStalled, OnRecovered, and OnClutchChanged follow committed state.

A driven output adapter owns only the transform/animation property it was authorized to control. It must not overwrite a host animation system or simulate physics and kinematic transforms on the same body without a deliberate adapter policy. On teardown release its claim and leave the current authoritative owner intact.

## 6.6 Persistence, streaming, and authority

Persist enabled controls, engagement, logical phase where needed, source/load settings, and model version. Derived speeds and reflected loads are recomputed. Restoring phase does not emit historical revolution triggers. World-origin rebasing does not alter local shaft phase.

Unloaded logical devices retain detached records or suspend the affected component. A missing load provider is not automatically zero load; use the authored FailClosed/Suspend policy. Shared networks are server-authoritative; clients interpolate visual rotations from authoritative timestamped states. Cross-machine bitwise physics determinism is not claimed.

## 6.7 Authoring and example

Provide graph validation, direction arrows, ratio labels, requested/actual speed, reflected load trees, and stall reasons. The fixture includes opposite-turning gears, a belt branch, a clutch, a removable load, and a stalled motor. All visuals use generic meshes; simulation works without the visual adapter.

## 6.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| MEC-01 | `Doc.Mechanical.RatioAndDirection` | Chain ratios and gear/belt signs produce expected speeds |
| MEC-02 | `Doc.Mechanical.ReflectedLoads` | Branch loads and efficiency produce the declared parent demand |
| MEC-03 | `Doc.Mechanical.StallRecovery` | Overload stalls and recovery hysteresis prevents oscillation |
| MEC-04 | `Doc.Mechanical.ZeroSpeed` | Zero speed does not divide by zero or generate invalid torque |
| MEC-05 | `Doc.Mechanical.ClutchIsolation` | Disengaging a branch isolates its load without phase corruption |
| MEC-06 | `Doc.Mechanical.InvalidTopology` | Cycles, reconvergent paths, and multiple drivers fail before mutation |
| MEC-07 | `Doc.Mechanical.PhaseContinuity` | Step partitioning/restore preserve declared angular tolerance |
| MEC-08 | `Doc.Mechanical.AdapterOwnership` | Presentation does not clobber unrelated transform/animation owners |
| MEC-09 | `Doc.Mechanical.UnloadRestore` | Missing nodes and restored controls follow the declared suspension policy |
| MEC-10 | `Doc.Mechanical.BoundsAndIsolation` | Extreme ratios/speeds fail safely; no Power or physics-plugin dependency |

---

# 7. Module 25 — DocMaterialReactions

## 7.1 Purpose and scope

Apply authored environmental reactions to material-bearing objects: burning, extinguishing, melting, and drying initially, with rule definitions supporting later corrosion or dissolution. Own the underlying material state, not cosmetic surface sounds, character ailments, general health/damage, or a universal chemistry simulator.

**SLICE:** One combustible object accepts ignition/exposure, burns down an authored fuel quantity, and can be extinguished.  
**BASE:** Burning/extinguishing, melting, drying, threshold/duration rules, competing-reaction resolution, bounded transfer/propagation, state queries, and versioned snapshots.  
**BRIDGE:** Weather exposure, Fluid application, Surface Feedback/VFX/audio, mesh/material presentation, Save/Events.  
**FUTURE:** Arbitrary chemical mixtures, physical heat diffusion, structural destruction, and a general damage framework.

## 7.2 Types, definitions, and states

Use `UDocMaterialReactionSubsystem : UWorldSubsystem`, `UDocReactiveMaterialComponent`, `UDocMaterialReactionDefinition`, `UDocMaterialProfile`, `IDocMaterialExposureProvider`, `FDocMaterialState`, `FDocExposureSample`, `FDocReactionInstance`, and `FDocReactionTransition`.

A profile defines material tags, supported channels, initial fuel/moisture/phase parameters, allowed reactions, and state limits. A reaction defines source/target conditions, exposure channel, activation/deactivation thresholds, dwell duration, state change rates, priority, exclusive group, propagation rules, and resulting material tags.

Runtime state separates material facts such as moisture, remaining fuel, phase fraction, and char amount from active reaction instances and visual effects. A sound stopping is not proof that burning stopped. An object can be wet and charred at once; avoid one giant enum that cannot represent valid combinations.

## 7.3 Exposure and evaluation

Expose typed channels such as HeatExposure, Wetting, Cooling, and DryingPotential. Declare whether values are physical units or normalized gameplay intensities; do not mix them in one untyped float. Inputs are source-owned samples with expiry, sequence, and revision. Removing one source removes its contribution, not every exposure on the object.

Combine channels using the profile's declared sum/max/weighted rule and cap valid inputs before evaluation. Snapshot inputs at a fixed boundary. Compute eligible reactions in stable priority/ID order and apply compatible deltas as one material-state update. Exclusive reactions in the same group cannot both consume the same fuel/moisture during a step.

A base drying reaction consumes existing moisture only. Burning consumes finite authored fuel, stops when depleted, and does not regenerate fuel on extinguish/reignite. Melting changes a bounded phase fraction; emitting a puddle or replacing the object is a separate approved effect. Do not claim conserved physical heat or mass where the authored model does not calculate it.

Threshold hysteresis and minimum duration prevent chatter. A wetting event arriving at a boundary has a defined order relative to ignition evaluation. Reaction rules cannot recursively spawn unlimited transitions within one callback.

## 7.4 Bounded propagation

Use explicit contacts or a budgeted spatial-neighbor provider. Propagation considers eligible registered objects, range/contact rules, transmissibility, generation/depth, and cooldown. It does not run GetAllActorsOfClass every frame.

A transfer carries stable source/target/step identity. Deduplicate repeated contacts and repeated propagation requests. Limit neighbors per source, active reactions, queued transfers, and work per step. A budget limit must defer or reject with diagnostics according to policy; it cannot repeatedly trigger a target merely because it was not processed earlier.

Authoring distinguishes cosmetic spread from stateful exposure transfer. Never let an effect actor ignite arbitrary nearby objects without passing through the validated reaction API. Unloaded participants are not live propagation neighbors; detached simulations require explicit logical contacts and a tested policy.

## 7.5 API and lifecycle

Expose RegisterReactiveObject, AddExposureSource, UpdateExposureSource, RemoveExposureSource, RequestReaction, SuppressReaction, QueryMaterialState, QueryActiveReactions, CaptureMaterialState, and StageRestore. Manual ignition still validates material eligibility, authority, and resource bounds.

OnMaterialStateChanged and OnReactionStarted/Ended include revision, cause, and committed transition ID. Resulting events/actions are issued after state commit; a failed visual spawn does not roll back consumed fuel. Retryable gameplay effects use receipts; cosmetic effect retries are bounded and separate.

Suppression is source-owned and expires/revokes independently. A temporary weather suppression cannot remove an explicit script's permanent inhibit. Component teardown clears live exposure handles; saved facts remain under the persistence policy.

## 7.6 Persistence, networking, and presentation

Persist material facts, reaction progress where restorable, model/content version, remaining durations, and effect receipts. Do not save active Niagara/audio handles. Restore material state first and rebuild effects from that state without replaying ignition rewards or consumption.

Server authority owns gameplay reactions. Replicate bounded state transitions or quantized values with declared tolerance. Clients may interpolate cosmetic wetness/fire intensity, but their particle collision does not authoritatively consume fuel or ignite targets.

Adapters apply per-object material instances or owned overrides, never edit a shared source material asset during play. A color/VFX change must have a text/state equivalent for debugging and accessibility.

## 7.7 Authoring and example

Provide profile validation, a channel/exposure inspector, active reaction graph, propagation counters, and reason codes for rejected reactions. The example uses combustible, meltable, and wettable generic objects with explicit heat/wetting controls. It must work with all external environmental and VFX plugins absent.

## 7.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| MAT-01 | `Doc.Material.IgnitionThreshold` | Threshold, dwell, and hysteresis prevent premature/chattering ignition |
| MAT-02 | `Doc.Material.FiniteFuel` | Burning cannot consume below zero or regenerate fuel on restart |
| MAT-03 | `Doc.Material.ExtinguishOrdering` | Wetting/ignition boundary order is reproducible and documented |
| MAT-04 | `Doc.Material.MeltAndDryBounds` | Phase and moisture remain within valid limits |
| MAT-05 | `Doc.Material.SourceOwnership` | Removing one exposure/suppression source leaves others intact |
| MAT-06 | `Doc.Material.CompetingReactions` | Exclusive reactions cannot double-consume the same resource |
| MAT-07 | `Doc.Material.PropagationBudget` | Contacts deduplicate and spread remains within explicit work limits |
| MAT-08 | `Doc.Material.UnloadedTarget` | Unloaded targets are not mutated through stale actor references |
| MAT-09 | `Doc.Material.RestoreNoIgnitionEffects` | Restored burning state rebuilds visuals without new gameplay effects |
| MAT-10 | `Doc.Material.AdapterAbsence` | Base runs without Weather, Fluid, Niagara, or Surface Feedback |
---

# 8. Module 26 — DocServiceQueues

## 8.1 Purpose and capability boundary

Manage waiting order, service stations, limited capacity, group reservations, batch admission, abandonment, expiry, and temporary closure. The plugin decides who is eligible to be served and which capacity is reserved; it does not walk actors, schedule their day, animate boarding, sell tickets, or operate the served machine.

**SLICE:** One queue, two stations, join/cancel, one offer, arrival acknowledgment, and service completion.  
**BASE:** FIFO and explicit priority policies, multiple stations, indivisible groups, admission offers, reservations, batch assembly, timeout/no-show handling, closures, and inspectable fairness.  
**BRIDGE:** NPC Schedules, Smart Objects, movement providers, interaction, admission entitlement checks, Save/Events.  
**FUTURE:** Crowd simulation, universal path planning, complex appointment scheduling, and commerce.

## 8.2 Types, lifetime, and data

Use `UDocServiceQueueSubsystem : UWorldSubsystem`, `UDocQueueStationComponent`, `UDocQueueParticipantComponent`, `UDocServiceQueueDefinition`, `FDocQueueTicket`, `FDocAdmissionOffer`, `FDocStationReservation`, and `FDocServiceBatch`.

A ticket owns QueueId, TicketId, participant/group IDs, party size, join ordinal, priority class, eligibility metadata, owner scope, state, deadline policy, and revision. Station records contain stable identity, capacity, supported service tags, operational state, current reservations, and service policy. Queue positions are logical authored slots/targets; physical positions come from an optional provider.

Keep ticket order separate from actual movement. A participant standing near the station has not accepted an offer or acquired capacity. An opaque lease distinguishes reserved capacity from an arbitrary actor reference.

## 8.3 Admission state machine

```text
Waiting → Offered → Reserved → Arriving → Ready → InService → Completed
   └──────────────→ Cancelled | Expired | Rejected
```

States may be combined only through an explicit simpler policy. An offer reserves capacity for a bounded period; acceptance confirms the participant's intent, arrival confirms position/eligibility, and service start consumes the admitted reservation. The service provider confirms completion. Returning a movement success result is not service completion.

Capacity accounting must satisfy:

```text
ReservedSeats + InServiceSeats <= StationCapacity
```

A group of size `n` is admitted atomically in the base. Never reserve some members and leave the others orphaned. An oversized group fails or stays waiting with an explicit reason; it must not block the queue forever without diagnostics.

## 8.4 Ordering, fairness, and batches

FIFO is join ordinal, not timestamp precision or actor registration order. Priority policy sorts by validated priority then join ordinal. Priority supplied by an untrusted participant is a request, not authority. Base supports StrictHeadOfLine and FirstFitWithBypassLimit. FirstFit must track bypass count/age so a large group is not invisibly starved.

A station may start on FullCapacity, MinimumBatchSize, or BatchWaitExpired after a configured minimum. Batch formation reserves every selected ticket in one commit. Two stations evaluating the same queue cannot reserve the same ticket. Station selection has stable ordering or an explicitly recorded balancing policy.

Offers, arrival, and service timeout use the queue's declared clock. A paused gameplay clock may freeze fictional service time; an orphan-cleanup watchdog may use monotonic time but cannot silently expire valid gameplay reservations under a different clock. Document both.

Closing a queue stops new admissions. Closing a station distinguishes DrainCurrentService, CancelPendingReservations, and EmergencyAbort. Emergency abort asks a host safety/evacuation provider; the queue plugin does not teleport actors or guarantee physical evacuation. Failure to evacuate is visible as a blocked station, not a free seat.

## 8.5 API and cleanup

Expose JoinQueue, LeaveQueue, QueryTicket, QueryQueueView, CreateAdmissionOffer, AcceptOffer, DeclineOffer, ConfirmArrival, StartService, CompleteService, SetStationOperationalState, and CancelStationReservations. Internal automatic policy execution uses the same validated commands as manual orchestration.

Duplicate join keys return the existing ticket when policy permits; a different party payload for the same key is a conflict. CompleteService and cancellation are receipt/idempotency-aware. A late arrival after expiry cannot resurrect an old reservation. Releasing one participant's token cannot free another ticket's seats.

Movement requests carry the ticket/offer generation. On cancellation, invoke only the matching movement cancellation and ignore stale results. Station destruction, stream unload, participant departure, and world teardown have explicit suspension/cancellation behavior. No reservation may retain a dead actor indefinitely.

## 8.6 Persistence and networking

Persist queue order and eligibility facts only when useful. On restore, create new transient leases and revalidate station/participant availability. Do not save a live Smart Object claim or assume a previously occupied seat is still occupied. InService restoration requires a cooperating service provider with compatible state; otherwise enter ReconciliationRequired rather than quietly charging/admitting twice.

Shared queues are server-authoritative. Replicate only each audience's queue view and relevant offers; participants cannot nominate another owner for removal. A client request to confirm arrival must be verified by the host's trusted arrival provider.

## 8.7 Authoring and example

Provide a ticket timeline, station capacity inspector, bypass/starvation warnings, deadline view, and authored waiting-position preview. The example services mixed groups at two stations, includes one no-show, and closes a station mid-offer. A headless fixture uses explicit arrival/completion commands without locomotion or NPC plugins.

## 8.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| QUE-01 | `Doc.Queue.FIFOAndPriority` | Ordering follows declared policy with stable ties |
| QUE-02 | `Doc.Queue.MultiStationReservation` | Concurrent station choices cannot double-reserve a ticket |
| QUE-03 | `Doc.Queue.GroupAtomicity` | Indivisible groups never partially consume station capacity |
| QUE-04 | `Doc.Queue.BypassAndStarvation` | First-fit policy enforces configured bypass/age limits |
| QUE-05 | `Doc.Queue.OfferExpiry` | Expired offers reject late accepts and release exactly their own capacity |
| QUE-06 | `Doc.Queue.ArrivalAndCompletion` | Movement arrival and actual service completion remain separate |
| QUE-07 | `Doc.Queue.BatchTimeout` | Partial batches start only under configured minimum/deadline rules |
| QUE-08 | `Doc.Queue.ClosureAndOwnerLoss` | Closure, destruction, and departure leave no orphan reservation |
| QUE-09 | `Doc.Queue.RestoreRevalidation` | Restored tickets get new leases and reconcile in-service state |
| QUE-10 | `Doc.Queue.IsolatedHeadless` | Full logical admission works without AI, Smart Objects, or UI |

---

# 9. Module 27 — DocAcousticSpaces

## 9.1 Purpose and scope

Model connected rooms, hallways, doors, and openings to produce bounded transmission policies for registered sounds. Adaptive Audio chooses what plays; this plugin determines how authored spatial connections influence perceived sound. It is not a wave simulator, universal sound replacement, or an automatic room-extraction system.

**SLICE:** Two authored spaces, one adjustable doorway, one emitter, one listener, and an audible open/closed change.  
**BASE:** Space membership, portal graph, strongest-path transmission, gain/filter output, smoothing, bounded queries, and a native single-listener reference emitter adapter.  
**BRIDGE:** DocRegions membership, Adaptive Audio/broadcast emitters, specialized spatializers, optional reverb routing.  
**FUTURE:** Diffraction, interference, multi-path wave summation, automatic geometry analysis, and independently mixed multi-listener propagation.

Unreal already exposes attenuation, filtering, occlusion, and reverb-related controls. Reuse those playback capabilities; the new feature is the authored connectivity model and scoped parameter policy. [R4]

## 9.2 Types and configuration

Use `UDocAcousticSpaceSubsystem : UWorldSubsystem`, `UDocAcousticSpaceComponent`, `UDocAcousticPortalComponent`, `UDocAcousticEmitterComponent`, `UDocAcousticProfile`, `FDocAcousticPathQuery`, `FDocAcousticPathResult`, and `IDocAcousticPlaybackAdapter`.

Space definitions contain stable SpaceId, authored bounds or membership provider, priority, interior profile, and optional exterior relation. Portals contain stable endpoints, openness, open/closed transmission values, filter profile, physical portal position, and enabled state. Emitters/listeners have explicit world context, location, membership, and registration generation.

Do not make DocRegions mandatory. The base supports authored box/volume membership and explicit SpaceId assignment. Overlap resolution uses priority then stable ID, with boundary hysteresis to avoid rapid toggling at a door.

## 9.3 Path calculation and approximation

For each enabled portal, derive a gain in `[0,1]` and nonnegative path cost. A default cost can use `-log(max(gain, epsilon))` plus a nonnegative distance penalty. A gain of zero is a blocked edge, not an infinite numeric value passed into an unsafe solver.

Use bounded Dijkstra or an equivalent validated shortest-path algorithm. The base selects the strongest admissible path, with stable tie-breaking and maximum hops/visited nodes. Do not add parallel path gains and pretend to model acoustic interference. Approximate filtering can use the most restrictive cutoff along the path; identify that as the authored model.

Separate source-listener distance attenuation from portal transmission. Applying both must follow a documented composition policy so native occlusion and room-path attenuation do not unintentionally attenuate the same wall twice. Same-space, exterior, disconnected, and unresolved-space outcomes each have explicit policies.

A disconnected path returns Blocked/NoPath and a configured floor/closed-wall policy. It must not suddenly play full-volume direct sound because no route was found. Budget-limited results carry approximation/stale metadata, not a false exact result.

## 9.4 Parameter ownership and smoothing

The reference adapter modifies only registered emitters and parameters it owns. It combines its transmission multiplier with the host's base gain instead of repeatedly multiplying last frame's already-adjusted value. Filter/volume smoothing is time-based, bounded, and reset appropriately after teleportation.

Do not mutate a shared attenuation asset to affect one source. Use per-emitter runtime configuration or a provider with explicit parameter claims. Removing the acoustic claim restores the current baseline/remaining owners, not a stale historical snapshot. An Adaptive Audio fade and portal attenuation must coexist without one erasing the other.

One Audio Component's global parameter cannot automatically represent different filters for two listeners. Base path queries are independently scoped for multiple listeners; the audible reference adapter claims **one selected listener per mix**. True separate listener mixes require a tested backend or explicit emitter-routing strategy and remain a distinct capability.

## 9.5 API and lifecycle

Expose RegisterSpace, RegisterPortal, SetPortalOpenness, RegisterEmitter, UpdateListener, QueryTransmission, AcquireAcousticClaim, ReleaseAcousticClaim, and GetDebugPath. Results contain path IDs, gain, cutoff/filter data, topology revision, listener/source timestamp, approximation flag, and reason.

Portal changes invalidate affected path caches. Moving sources/listeners invalidate membership/path queries at configured thresholds. Base queries never scan every Audio Component. Cap active emitter evaluations and stagger noncritical updates.

On space unload retain authored topology descriptors when configured, but remove stale collision/provider references. An unloaded door's saved openness may remain meaningful only under the authored identity/lifecycle policy. Runtime audio claims are recreated after restore; they are not persisted as handles.

## 9.6 Validation and example

Validate portal endpoints, duplicate space IDs, non-finite transmission, ambiguous membership, unsupported listener backend, and missing required audio. Provide space/portal overlays and a path inspector showing all composed gain stages. The fixture includes two alternative corridors and a door whose closure changes the chosen path.

The audible test must be supplemented by measurable parameter/capture evidence where available. A unit test of graph costs is not evidence that the sound pipeline applied the result.

## 9.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| ACO-01 | `Doc.Acoustics.SpaceMembership` | Overlapping spaces and boundary hysteresis resolve consistently |
| ACO-02 | `Doc.Acoustics.StrongestPath` | Chosen path follows declared nonnegative cost/tie rules |
| ACO-03 | `Doc.Acoustics.DoorTransition` | Openness changes transmission smoothly without stale cache reuse |
| ACO-04 | `Doc.Acoustics.DisconnectedPath` | No route follows explicit blocked policy, not full-volume fallback |
| ACO-05 | `Doc.Acoustics.ParameterComposition` | Gain/filter claims do not multiply cumulatively or clobber host fades |
| ACO-06 | `Doc.Acoustics.ListenerIsolation` | Per-listener query data remains distinct; unsupported mix mode is explicit |
| ACO-07 | `Doc.Acoustics.PathBudget` | Large graphs terminate within limits and flag approximate/stale results |
| ACO-08 | `Doc.Acoustics.UnloadTeardown` | Unload and emitter destruction release claims without stale references |
| ACO-09 | `Doc.Acoustics.NativeEmitterPlayback` | Cooked single-listener fixture audibly/measurably applies the path policy |
| ACO-10 | `Doc.Acoustics.IsolatedBase` | Space and audible base work without Regions or Adaptive Audio |

---

# 10. Module 28 — DocOpticalBeams

## 10.1 Purpose and scope

Route authored optical/energy beams through emitters, mirrors, filters, and receivers. The base calculates gameplay paths and receiver activation; it does not create physically correct illumination, generic damage, or security tripwires.

**SLICE:** One beam reflects from a mirror into a threshold receiver.  
**BASE:** Straight segments, planar reflection, channel/color filtering, transmission losses, bounded path solving, sustained receivers, loop detection, and generic beam presentation.  
**BRIDGE:** Puzzle inputs, Power availability, Events/Save, optional Niagara presentation.  
**FUTURE:** Splitters, refraction, lenses, volumetric scattering, coherent interference, and multi-wavelength physical optics. Splitters remain a named follow-on from the candidate description, not a silently removed idea.

## 10.2 Types and authored data

Use `UDocOpticalBeamSubsystem : UWorldSubsystem`, `UDocBeamEmitterComponent`, `UDocBeamSurfaceComponent`, `UDocBeamReceiverComponent`, `UDocOpticalProfile`, `FDocBeamSegment`, `FDocBeamChannelState`, and `FDocReceiverState`.

Emitter definitions contain stable ID, local origin/direction, finite range, channel vector or semantic channel ID, initial intensity, trace configuration, update policy, and segment budget. Surfaces define reflection/filter/transmission behavior and validated coefficients. Receivers define accepted channels, minimum intensity, required dwell, release hysteresis, and whether multiple emitters combine.

A visible RGB tint is not necessarily the full gameplay channel identity. Colorblind-readable receiver indicators need labels/shapes in the example.

## 10.3 Path solver

Normalize direction and reject zero vectors/non-finite values. For unit incoming direction `d` and unit surface normal `n`, reflection is:

```text
r = d - 2 × dot(d, n) × n
```

Choose a consistent normal-facing convention and validate mirrored/negative-scale authored surfaces. Offset the next segment origin by a configurable scale-aware epsilon to avoid immediate self-hit, while maintaining a total range ledger so offsets do not extend the beam indefinitely.

Trace only the declared collision channel and supported geometry. Plain opaque blocking geometry stops the beam; tagged beam surfaces apply their response. Do not assume visible glass is transmissive unless the gameplay profile says so. Ignore the emitter's own designated components without ignoring unrelated blocking parts.

Maintain maximum reflections, maximum total path length, minimum intensity, and a visited-state guard using surface/port identity plus quantized direction/channel as appropriate. A safety bound always terminates even when quantization misses a repeating cycle. Report TerminatedByBudget separately from HitReceiver or Blocked.

For a filter, output intensity per channel cannot exceed input intensity unless the definition explicitly represents an externally powered amplifier extension. Base coefficients are within `[0,1]`. Future splitters must budget the sum of branch outputs; do not duplicate full intensity into every branch.

## 10.4 Receiver aggregation and timing

Each receiver tracks contributions by stable emitter/path generation. Replacing a path removes old contributions and installs the new set atomically. Destroying or disabling one emitter removes only its contribution. Registration order cannot determine the combined result.

Aggregation modes are AnyEligible, SumIntensity, or AllRequiredChannels with explicit channel semantics. Duplicate hits from one path are deduplicated according to receiver policy; a mirror loop cannot satisfy a receiver multiple times by visiting repeatedly.

Receiver dwell uses the declared simulation/gameplay clock. Losing eligibility resets continuous dwell; release hysteresis may retain Activated state for a configured interval. Solve-path work may be budgeted, but a stale path must expose its age and use a maximum-staleness policy before granting gameplay activation.

## 10.5 API and state transitions

Expose RegisterEmitter/Surface/Receiver, SetEmitterEnabled, UpdateEmitterPose, SetSurfaceProfile, RequestPathRefresh, QueryPath, QueryReceiver, and CaptureReceiverState. OnReceiverActivated/Deactivated report committed contribution evidence and revision.

Moving mirrors queue path invalidation; do not resimulate every registered beam every frame when nothing moved. Async/batched traces carry world/topology/request generations. Late results from before a mirror edit or emitter destruction are discarded.

Beam visuals consume path segments and have no authority. Base uses a lightweight authored mesh/line representation with no mandatory Niagara dependency; debug drawing is not the only production presentation path. No actor/component should be spawned per segment every frame.

## 10.6 Persistence, authority, and authoring

Persist controls, receiver latched state where authored, and remaining supported timers, not stale trace hits. Recompute paths after restore and require fresh eligibility before continuing non-latched dwell. Server-owned receivers validate paths using their trusted world; client beam visuals do not decide puzzle completion.

Provide mirror-normal, range, intensity, path-budget, and receiver-contribution overlays. Validate invalid vectors, coefficients, collision settings, missing channels, and unreachable static authoring only where that can be established without pretending to solve all gameplay arrangements.

## 10.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| OPT-01 | `Doc.Optics.ReflectionMath` | Reflection direction matches normalized analytic fixtures |
| OPT-02 | `Doc.Optics.FilterLoss` | Filters cannot create intensity and channel rules are explicit |
| OPT-03 | `Doc.Optics.SelfHitAndRange` | Epsilon handling avoids self-hit without extending allowed range |
| OPT-04 | `Doc.Optics.LoopTermination` | Mirror loops stop at visited/budget limits without activation spam |
| OPT-05 | `Doc.Optics.ReceiverContributions` | Contributions replace/remove per emitter without double counting |
| OPT-06 | `Doc.Optics.SustainedActivation` | Dwell and release hysteresis obey declared clock boundaries |
| OPT-07 | `Doc.Optics.StaleTrace` | Old trace results cannot override newer mirror/emitter state |
| OPT-08 | `Doc.Optics.RestoreRecompute` | Restore recomputes paths before non-latched activation |
| OPT-09 | `Doc.Optics.CookedPresentation` | Generic beam segments render in the isolated cooked fixture |
| OPT-10 | `Doc.Optics.NoSiblingDependency` | Base requires no Puzzle, Power, Niagara, or security plugin |

---

# 11. Module 29 — DocSurfacePainting

## 11.1 Purpose and explicit asset compatibility

Maintain accumulated paint, dirt, cleaning, scraping, or reveal masks on authored surfaces. This is persistent surface state, not a transient decal effect. Arbitrary meshes are not automatically compatible.

**SLICE:** One prepared static mesh, one mask layer, paint/clean brushes, and reproducible coverage.  
**BASE:** Compatible static meshes with validated mapping, per-object masks, typed brush strokes, paint/clean/reveal operations, coverage queries, bounded history/checkpoints, persistence, and native material presentation.  
**BRIDGE:** Interaction/input, inventory tool consumption, objectives, Save/Events.  
**FUTURE:** Skeletal deformation, arbitrary runtime meshes, automatic seam repair, multi-layer blends, arbitrary world-space brush projection, and user-created texture importing.

## 11.2 Types and canonical state

Use `UDocSurfacePaintingSubsystem : UWorldSubsystem`, `UDocPaintableSurfaceComponent`, `UDocPaintSurfaceDefinition`, `UDocPaintBrushDefinition`, `FDocPaintStroke`, `FDocSurfaceMaskState`, and `FDocPaintCoverageResult`.

Definition fields: stable SurfaceId, mesh/version fingerprint, supported UV channel/mapping provider, material slot/parameter contract, mask dimensions, eligible-texel mask, initial state, layer semantics, and history/resource limits. A repeated mesh asset on two actors has two independent surface states.

Recommended base canonical state is a bounded CPU-side quantized mask plus ordered stroke/checkpoint metadata. The GPU texture is a presentation copy, not the only authoritative state. This avoids requiring synchronous GPU readback to decide cleaning completion. Declare the quantization, rasterizer version, and coverage tolerance.

## 11.3 Hit mapping and authoring constraints

Accept validated surface-local UV hits through `IDocSurfaceCoordinateProvider`. A reference collision-UV adapter may use Unreal's `FindCollisionUV`; its documented prerequisite is the Physics setting that supports UV data in hit results. Verify collision geometry, UV channel, cooked behavior, and local API before adopting it. Do not silently change the host's physics settings. [R5]

The base requires compatible non-overlapping UV regions for independently paintable areas, or explicit author acceptance that mirrored/overlapping UVs share paint. A missing UV hit returns Unsupported/InvalidMapping, never writes to `(0,0)` by default. UV seams, wrapping, and material sections have declared behavior. Do not draw across a seam merely because two consecutive screen hits are close.

Define brush radius in UV/texel units for the base. A world-meter radius needs a verified local surface mapping and distortion policy; it must not be represented by dividing by an arbitrary mesh bounds size.

## 11.4 Stroke model and rasterization

A stroke contains StrokeId, source/owner, SurfaceId, epoch/revision, operation, brush definition version, quantized points, pressure/strength, and sequence. Validate point count, spacing, bounds, strength, and duration before allocation. Bound resampling so sparse long drags do not create millions of samples.

For a normalized mask `m` and brush influence `a`, an illustrative operation contract is:

```text
Paint: m_new = max(m, a)
Clean: m_new = min(m, 1 - a)
```

The actual integer implementation must specify rounding and mask polarity. These monotonic operations are the base; alternative blend modes require separate tests. Painting followed by cleaning is order-dependent, so strokes must have one authoritative sequence. Concurrent strokes cannot rely on GPU submission order.

Update only dirty tiles/rectangles. Queue texture uploads with surface generation and mask revision. A late render upload cannot replace a newer mask. Avoid editing shared material/texture assets; each paintable instance owns its runtime material parameter claim and texture resource.

## 11.5 Coverage, undo, and persistence

Coverage is measured over the authored eligible-texel mask, using a declared threshold/weight. Base coverage is **eligible texture-mask coverage**, not automatically physical surface area. Physical-area weighting requires authored/baked weights. Return denominator, covered amount, threshold, revision, and precision so a zero-eligible-pixel mask cannot report success.

Gameplay completion uses canonical mask state at a committed revision. Render lag does not create duplicate completion events. Tool consumption/score rewards are external transactions keyed to accepted strokes or threshold transitions, not every preview dab.

A stroke journal may be checkpointed and compacted under fixed byte/count limits. Undo, when enabled, restores a prior checkpoint and replays the retained ordered strokes; it cannot undo another player's later stroke silently. Base may expose authorized local-session undo as an explicit profile; multiplayer collaborative undo is deferred.

Persist canonical mask/checkpoint, residual journal, mapping/version fingerprint, and revision. Validate dimensions and decompressed size before loading. A changed mesh/UV layout requires a migration or IncompatibleSurface state, not painting saved pixels onto a different object by guesswork. Failure to write a new checkpoint leaves the old valid state recoverable.

## 11.6 Networking, resource limits, and example

Server-authoritative shared painting validates stroke reach/permission and data limits; replicate accepted stroke sequences or bounded mask deltas. Client previews are provisional and reconcile on rejection. Local-only painting still applies the same input validation.

Set caps for mask resolution, active textures, pending uploads, stroke history, and persisted bytes. Texture creation may be deferred until visible; canonical state remains available while presentation is unloaded. Dedicated servers need no render texture.

The fixture includes a prepared object with an eligible region, a mirrored-UV incompatibility example, paint and clean modes, and a save/reload cycle. Editor tooling validates UV/material prerequisites and shows eligible coverage, dirty tiles, canonical/render revisions, and memory use.

## 11.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| PNT-01 | `Doc.Paint.MappingValidation` | Unsupported/missing/overlapping mappings follow explicit policies |
| PNT-02 | `Doc.Paint.StrokeRasterization` | Quantized reference strokes reproduce expected masks |
| PNT-03 | `Doc.Paint.OrderAndDeduplication` | Duplicate strokes do not reapply; authoritative order is preserved |
| PNT-04 | `Doc.Paint.CoverageDenominator` | Coverage uses eligible texels and handles empty masks safely |
| PNT-05 | `Doc.Paint.InstanceIsolation` | Two instances of one mesh do not share paint state |
| PNT-06 | `Doc.Paint.RenderRevision` | Late texture uploads cannot replace newer canonical state |
| PNT-07 | `Doc.Paint.CheckpointRestore` | Checkpoint plus journal restores identically within declared precision |
| PNT-08 | `Doc.Paint.MappingMigration` | Changed mesh/UV fingerprints reject or migrate without silent corruption |
| PNT-09 | `Doc.Paint.ResourceBounds` | Oversized masks/strokes/decompression requests are rejected before allocation |
| PNT-10 | `Doc.Paint.CookedMaterial` | A real cooked material displays paint/clean state; headless logic also works |

---

# 12. Module 30 — DocEvidenceDeduction

## 12.1 Purpose and scope

Represent observations, testimony, provenance, hypotheses, contradictions, and authored inference rules. Codex stores discovered information; this plugin evaluates relationships among it and records player conclusions. It is a fictional gameplay reasoning model, not a forensic truth engine or an LLM requirement.

**SLICE:** Three evidence records support or contradict two hypotheses, producing one validated conclusion.  
**BASE:** Owner-scoped evidence, provenance, explicit fact/claim distinction, hypothesis rules, contradictions, alternative conclusions, revisions/retractions, explanation data, and persistence.  
**BRIDGE:** Codex, Dialogue testimony, Photography evidence, objectives/unlocks, optional visual evidence board.  
**FUTURE:** Natural-language inference, probabilistic truth estimation, automatic story generation, and unrestricted rule scripting.

## 12.2 Types and record model

Use `UDocEvidenceSubsystem : UGameInstanceSubsystem` with owner/campaign partitioning, `UDocEvidenceDefinition`, `UDocHypothesisDefinition`, `UDocInferenceRuleDefinition`, `FDocEvidenceObservation`, `FDocEvidenceLink`, `FDocHypothesisState`, and `FDocDeductionResult`. World-bound providers register through explicit context; the GameInstance service must not retain actors across travel.

| Record | Required distinction |
|---|---|
| Evidence definition | Stable content identity and localized description, not discovery state |
| Observation | Who/what supplied it, source time and observation time, owner scope, revision, authenticity/availability metadata |
| Claim | A reported assertion that may conflict with other assertions; not automatically an established fact |
| Hypothesis | Stable ID, required/alternative rules, disqualifiers, visibility, and conclusion policy |
| Link | Player-authored or system-authored relationship with typed meaning and provenance |
| Conclusion record | Selected conclusion, supporting snapshot/revisions, validation result, effect receipts |

A player drawing a line on a board does not make the linked claim true. Keep player interpretation distinct from the authored evaluation result.

## 12.3 Rule evaluation and explanations

Base evaluators include AllRequiredEvidence, AnyEvidenceSet, Numeric/TimeComparison over typed authored fields, ClaimedRelationship, ContradictionPresent, and custom registered pure predicates. Treat missing/undiscovered evidence as Unknown/Unavailable according to audience policy, not false proof of absence.

Use at least Supported, Contradicted, Inconclusive, and Unavailable/Error as evaluation outcomes. “Confidence” values, when authored, are gameplay scores with an explanation, not calibrated probabilities. Do not claim an objective truth engine because a rule returned Supported.

Rules consume a coherent immutable evidence snapshot. Changes invalidate only dependent hypotheses through an index. Default inference dependencies are acyclic; reject cycles with a path diagnostic. A future fixed-point rule engine needs explicit termination semantics, not repeated evaluation until the game freezes.

Contradictions are typed and scoped: incompatible timestamps in the same event frame, mutually exclusive statements about one subject, or an authored negation. Distinguish event time, observation time, calendar/time zone metadata where relevant to authored content, and unknown precision. Do not compare raw display strings as timestamps.

## 12.4 Retraction, revision, and player decisions

Correcting testimony creates a new observation revision and retains provenance/history according to policy. Previously supported hypotheses become stale and are reevaluated. A conclusion records the snapshot on which it was accepted; new evidence may mark it Challenged rather than silently rewriting the player's history.

A hypothesis becoming eligible is not automatically a player decision. Separate EvaluateHypothesis from CommitConclusion. Commit validates owner, expected evidence revision, required confirmation policy, and repeatability. Effects are issued after the conclusion record commits and use durable receipt keys where retries are allowed.

Multiple valid conclusions may coexist. An authored mutually exclusive conclusion group enforces its own selection rule. Choosing one cannot secretly delete evidence supporting another unless a game-specific action explicitly does so through a consumer.

## 12.5 API and audience control

Expose RegisterEvidenceDefinition, AddObservation, ReviseObservation, RetractObservation, AddInterpretationLink, RemoveInterpretationLink, EvaluateHypothesis, QueryAvailableHypotheses, CommitConclusion, QueryExplanation, CaptureOwnerState, and StageRestore.

Queries apply audience/discovery restrictions **before returning evidence or explanations**. A hidden solution must not appear in the payload behind a disabled UI widget. A debug explanation view is development-only and separately authorized.

Operations carry stable request IDs and revisions. Duplicate observation provenance keys are handled by explicit deduplication policy; two independent witnesses are not merged merely because their text matches. Searching/filtering is localized presentation over stable IDs, not a reason to mutate the inference model on culture change.

## 12.6 Persistence, authority, and authoring

Persist observations, provenance, interpretation links, conclusion history, selected evidence revisions, and effect receipts. Rebuild dependency caches. Restore does not award a conclusion again. Unknown removed definitions become quarantined/orphaned records under a documented migration policy.

A shared investigation uses authoritative owner/campaign scope; personal notes may remain local/private. Networking must not replicate hidden testimony to unauthorized participants. A host decides whether conclusions are collaborative or per-player; the subsystem does not merge scopes automatically.

Provide a structured rule editor/validator and an explanation inspector. A visual corkboard is an optional presentation bridge, not a prerequisite for headless deduction. The example includes an unreliable statement, a contradictory timestamp, an alternative evidence set, and a revised conclusion.

## 12.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| EVD-01 | `Doc.Evidence.FactClaimSeparation` | Reported claims do not become established observations implicitly |
| EVD-02 | `Doc.Evidence.SupportAndContradiction` | Authored rules distinguish supported, contradicted, and inconclusive |
| EVD-03 | `Doc.Evidence.UnknownInputs` | Missing evidence cannot satisfy a rule by false absence |
| EVD-04 | `Doc.Evidence.AlternativeSets` | Alternative evidence sets and exclusive conclusions behave as configured |
| EVD-05 | `Doc.Evidence.RevisionInvalidation` | Revised/retracted observations invalidate dependent conclusions correctly |
| EVD-06 | `Doc.Evidence.StaleCommit` | A conclusion cannot commit against a stale evidence snapshot unnoticed |
| EVD-07 | `Doc.Evidence.PrivateExplanation` | Hidden evidence/answers never leak through ordinary query payloads |
| EVD-08 | `Doc.Evidence.RuleCycle` | Cyclic/oversized inference graphs fail before evaluation |
| EVD-09 | `Doc.Evidence.RestoreReceipts` | Restore preserves conclusion history without duplicate effects |
| EVD-10 | `Doc.Evidence.HeadlessIsolation` | Deduction runs without Codex, Dialogue, board UI, or AI model |
---

# 13. Module 31 — DocPhotography

## 13.1 Purpose and capability boundary

Capture an image plus a versioned record of the camera and eligible subjects, then evaluate authored framing, distance, angle, visibility, and moment conditions. This is gameplay photography, not a replacement camera system, universal screenshot tool, or image-recognition model.

**SLICE:** Capture one tagged subject, save an in-memory image/record, and evaluate a framing rule.  
**BASE:** On-demand scene capture, subject registration, geometric framing/approximate occlusion, versioned capture records, local gallery data, bounded image storage, and authored evaluation rules.  
**BRIDGE:** Evidence, Codex, objectives, UI, optional device/camera controls, Save metadata.  
**FUTURE:** Pixel segmentation, visual AI recognition, social sharing, cloud storage, unrestricted video recording, and automatic professional photo scoring.

## 13.2 Types and ownership

Use `UDocPhotographySubsystem : ULocalPlayerSubsystem`, a feature-internal world subject registry, `UDocPhotographableComponent`, `UDocPhotographyDefinition`, `UDocPhotoEvaluationProfile`, `FDocPhotoCaptureRequest`, `FDocPhotoRecord`, `FDocPhotoSubjectObservation`, and `IDocPhotoCameraProvider`.

The camera provider supplies an authorized view, projection, viewport/aspect information, and capabilities. It does not require the host to replace its camera class. Unreal's `USceneCaptureComponent2D` provides scene-to-render-target capture; exact capture/readback integration and synchronization must be verified locally. [R6]

One capture owns its render resources, loading handles, output bytes, and completion state. Multiple local players have separate sessions and storage ownership. A subject registry never returns hidden/private subject metadata to an unauthorized owner.

## 13.3 Capture record and frame coherence

Record PhotoId, OwnerScope, world/instance namespace, capture request ID, camera transform/projection, requested and captured frame/time identifiers, subject snapshot revision, evaluation profile/version, image dimensions/format/hash, storage status, and result details.

Each subject observation contains stable SubjectId, definition/version, authorized tags, transform/bounds or sample points, projected bounds, estimated visible fraction, distance, angle, and timestamp quality. A weak live actor is optional convenience, not saved identity.

Do not claim that metadata sampled when the shutter button was pressed exactly describes a scene rendered several frames later. Associate the capture with a defined snapshot/render boundary and record both timestamps. The base permits a documented geometric approximation within a configured time/pose tolerance. Scored captures exceeding that tolerance are rejected or marked EvaluationUnavailable, not silently scored using newer subjects against older pixels.

Freezing every actor is not a generic solution. A host may supply a controlled capture fixture or a synchronization provider; the plugin must not globally pause multiplayer to improve image consistency.

## 13.4 Evaluation model

Use the exact capture camera/projection/aspect for subject projection, not player-zero screen coordinates. Cull candidates through registered spatial queries and cap subjects/sample points. Handle behind-camera subjects, near-plane intersections, partial frame bounds, and zero-area/degenerate bounds explicitly.

Framing metrics may include fraction of projected authored bounds inside the image, distance bands, facing angle, and composition anchors. Bounds coverage is **not pixel-perfect object coverage**. Approximate occlusion traces sample authored points and report sampled visibility; a thin obstacle or translucent material follows the declared trace policy.

A profile defines required subjects, optional combinations, condition timestamps, minimum metrics, score weights, and failure reasons. Normalize weights and reject contradictory ranges. Separate “image captured” from “challenge criteria satisfied.” A valid photograph with no qualifying subject is still a successful capture and an unsuccessful evaluation, not an I/O failure.

## 13.5 Asynchronous pipeline and API

```text
Validate camera/owner/budgets → Snapshot capture context
→ Submit scene capture → Complete bounded readback
→ Encode immutable pixels → Stage image blob
→ Commit photo record/evaluation → Notify owner
```

Expose RequestPhoto, CancelCapture, QueryCapture, EvaluatePhotoRecord, ListPhotos, LoadPhotoImage, DeletePhoto, CaptureGalleryMetadata, and StageRestore. A capture handle reports distinct PendingRender, PendingReadback, Encoding, Committing, and terminal states where useful.

Do not synchronously stall every frame for readback. A bounded on-demand fallback may be explicitly profiled, but cannot be advertised as hitch-free. Thread-safe image encoding consumes copied pixels only. Late callbacks validate local-player/world/capture generation. Cancellation after committed storage does not claim the image never existed.

Persist the blob before committing a metadata reference that promises it is available, or use an explicit staged transaction. Failed writes leave no success-shaped record. Orphan cleanup is limited to generated storage roots and manifest-owned files; never delete arbitrary user images.

## 13.6 Storage, authority, and accessibility

Bound dimensions, in-flight captures, bytes, gallery count, and decoder output. Validate loaded image size and metadata schema. Store stable blob IDs under a controlled root, not arbitrary client paths. A missing blob marks the record ImageUnavailable and preserves meaningful metadata unless the user explicitly deletes it.

Local photography does not automatically produce authoritative quest proof. A shared reward bridge validates the camera/subject evidence against trusted game state under a stated anti-cheat/tolerance policy. A client-provided score or arbitrary ideal camera pose is not sufficient.

Provide non-flashing capture feedback, optional shutter sound, accessible failure text, and input alternatives. Do not capture desktop windows, microphone, or private overlays by default. A scene capture and a full viewport screenshot have different privacy and rendering characteristics; do not substitute silently.

## 13.7 Authoring and example

The fixture photographs three registered generic subjects, including a partly occluded one and a moving one. Show framing boxes and estimated visibility in a development view. Verify color space, exposure, aspect ratio, and rendered output against the chosen capture policy; do not promise exact parity with every host post-process stack.

## 13.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| PHO-01 | `Doc.Photo.CameraProjection` | Metrics use the capture projection/aspect, including off-screen/behind cases |
| PHO-02 | `Doc.Photo.OcclusionEstimate` | Sample-based visibility returns declared estimates, not false pixel accuracy |
| PHO-03 | `Doc.Photo.FrameCoherence` | Moving subjects and delayed capture enforce snapshot/time tolerance |
| PHO-04 | `Doc.Photo.CaptureVsEvaluation` | Valid image/no qualifying subject remains a successful capture |
| PHO-05 | `Doc.Photo.AsyncCancellation` | Cancellation and player/world loss release resources and ignore late callbacks |
| PHO-06 | `Doc.Photo.StorageCommit` | Write failure cannot produce a committed available-image record |
| PHO-07 | `Doc.Photo.GalleryBounds` | Oversized images, concurrency, and byte quotas are enforced |
| PHO-08 | `Doc.Photo.OwnerIsolation` | Galleries and private subject metadata do not leak between owners |
| PHO-09 | `Doc.Photo.RestoreMissingBlob` | Missing images are reported without erasing unrelated valid records |
| PHO-10 | `Doc.Photo.CookedRenderedCapture` | Actual cooked scene capture/readback/encoding works with graphics enabled |

---

# 14. Module 32 — DocBroadcastChannels

## 14.1 Purpose and scope

Represent in-world radio, television, announcements, and public-address programming as shared logical channels. Receivers tune to a channel's current position instead of restarting a playlist for every device. Adaptive Audio remains the listener-oriented soundtrack director.

**SLICE:** One scheduled audio channel, two receivers, late tuning, and one interrupting announcement.  
**BASE:** Bundled finite seekable audio programs, schedule resolution, per-channel transport, independent receiver volume/tuning, priority interruptions, explicit resume policy, and bounded audible playback.  
**BRIDGE:** Media Framework video/streams, Acoustic Spaces, Power, Time, UI, Save/Events.  
**FUTURE:** Broadcast networking services, station discovery, DRM, arbitrary web-page playback, and cross-machine sample-locked synchronization.

Media Framework supplies source/player/playlist concepts and asynchronous media opening. Its capabilities vary by backend; this plugin owns scheduling and receiver semantics, not codecs or guaranteed URL support. [R7]

## 14.2 Types and data

Use `UDocBroadcastSubsystem : UWorldSubsystem`, `UDocBroadcastReceiverComponent`, `UDocBroadcastChannelDefinition`, `UDocBroadcastProgramDefinition`, `FDocBroadcastTransport`, `FDocBroadcastScheduleEntry`, `FDocBroadcastInterruption`, and `IDocBroadcastPlaybackProvider`.

A channel contains stable ChannelId, localized name, schedule/version, epoch anchor, clock source, looping/gap policy, audience, and interruption arbitration. Programs declare finite duration, supported seek behavior, soft source ID, authored loudness/volume policy, and display metadata. Runtime records contain current program/cursor, schedule revision, active interruption stack, transport generation, and listeners/receivers.

The base restricts assets to a verified finite/seekable audio path. A generic sound with unknown duration, procedural behavior, or unsupported seeking fails capability validation. Do not infer reliable duration from a decorative metadata field when the backend cannot seek correctly.

## 14.3 Schedule and late-join semantics

Use half-open program intervals `[start, end)`. Resolve a channel cursor from its explicit anchor and clock. For a looping schedule, reduce elapsed time by the validated total duration and locate the entry using cumulative durations; reject zero-length schedules and arithmetic overflow.

A receiver that joins at 37 seconds into a program starts at that program offset, subject to the backend's declared seek precision. Tuning, muting, leaving range, or unloading the receiver does not reset channel time. A large time jump selects the current program directly; it does not replay every skipped announcement/event in sequence.

Distinguish fictional schedule time from audio playback time. A day/night clock accelerated by sixty times cannot automatically play normal-speed programs against every simulated minute without a policy. The Time bridge must choose ReanchorAtScheduleBoundary, FollowRealTransport, or another explicit mapping.

A logical shared cursor is not proof of sample-phase-identical speakers. Base playback declares a measured synchronization tolerance for local receivers. Tight phase coherence requires a tested shared-source/routing or scheduled playback implementation and is not assumed from simultaneous game-thread Play calls.

## 14.4 Interruptions and recovery

Interruption requests have owner handle, priority, queue/replace/reject policy, program reference, expiry, and resume rule. Base resume rules are PauseUnderlyingCursor and ContinueUnderlyingSchedule. The first returns to the paused program offset; the second returns to wherever the ordinary schedule has advanced.

Arbitrate using priority and request ordinal. Releasing one interruption cannot stop a newer owner's announcement. An expired queued announcement is discarded with a terminal result. Cancelled/replaced requests release their resources and cannot resume themselves on a delayed media callback.

NoProgram, Loading, Playing, Paused, Interrupted, UnavailableSource, and Faulted are distinguishable states. A failed source follows Skip, SilenceGap, or RetryBounded policy. Do not retry forever or silently restart the entire schedule.

## 14.5 API, backend, and lifecycle

Expose RegisterChannel, RegisterReceiver, TuneReceiver, SetReceiverVolume, MuteReceiver, QueryNowPlaying, PushInterruption, ReleaseInterruption, SetTransportPaused, SeekChannelAuthorized, and CaptureTransportState.

Playback providers declare CanSeek, KnownDuration, Audio/Video support, seek precision, concurrent streams, and readiness callbacks. A successful Open request is not playback readiness; asynchronous completion must be handled before play/seek where required. [R7]

Receivers own only their playback/volume claims. A television screen adapter may share a program decoder/texture where supported, but visual synchronization and per-device audio routing need explicit ownership. Virtualized or out-of-range receivers rejoin the current cursor rather than accumulating independent hidden playlists.

## 14.6 Persistence, networking, and content safety

Persist channel anchor, schedule version, selected controls, and explicitly durable interruption state. Transient owner leases are revalidated/recreated, not restored as live pointers. A missing old program requires a migration/gap policy. Loading a save must not rebroadcast historical completion events.

Server authority may replicate logical channel/schedule anchors and interruption revisions; each client decodes locally. Network time skew and seek precision define synchronization tolerance. No server audio device is required for logical channel authority.

External streams are opt-in bridge configuration. Allowlist schemes/origins, bound open/retry time, and require a genuinely supported stream/file endpoint. Do not promise arbitrary video-page URLs. Bundled media must be staged/cooked under the selected backend's documented content rules and checked in a packaged build.

## 14.7 Authoring and example

Provide a schedule timeline, receiver/cursor inspector, interruption queue, decoder capabilities, drift/seek diagnostics, and missing-media validator. Use original/appropriately licensed bundled sounds. The fixture demonstrates late joining, two interruption policies, source failure, and receiver unload/reload.

## 14.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| BRC-01 | `Doc.Broadcast.ScheduleBoundary` | Program boundaries, wrap, gaps, and long time jumps resolve correctly |
| BRC-02 | `Doc.Broadcast.LateReceiver` | New receiver joins the current offset rather than restarting |
| BRC-03 | `Doc.Broadcast.MuteIndependence` | Receiver mute/tune changes do not reset the channel |
| BRC-04 | `Doc.Broadcast.InterruptionResume` | Pause-underlying and continue-schedule policies restore correctly |
| BRC-05 | `Doc.Broadcast.OwnerAndExpiry` | Releasing/expiring an interruption affects only its own claim |
| BRC-06 | `Doc.Broadcast.AsyncOpenFailure` | Readiness/failure/cancel callbacks cannot start stale programs |
| BRC-07 | `Doc.Broadcast.SeekCapability` | Unknown duration or unsupported seeking fails explicitly |
| BRC-08 | `Doc.Broadcast.UnloadRestore` | Receivers and saved anchors rejoin without historical event replay |
| BRC-09 | `Doc.Broadcast.ClockMapping` | Accelerated time and transport time follow the selected bridge policy |
| BRC-10 | `Doc.Broadcast.CookedAudio` | Bundled playback and local synchronization tolerance are actually measured |

---

# 15. Module 33 — DocWorldTerminals

## 15.1 Purpose and capability boundary

Provide fictional computers, kiosks, consoles, and embedded device interfaces with persistent device state, virtual files, messages/logs, applications, and approved commands. UI manages focus/screens; this plugin owns the device and its data. It must not become an operating-system shell or surveillance framework.

**SLICE:** Open a device session, read a message/file, invoke one safe device-local command, and close cleanly.  
**BASE:** Device registry, sessions, virtual directories/files, messages/logs, application registry, typed command dispatch, permissions, persistent state, and a minimal generic front-end example.  
**BRIDGE:** CommonUI/GameFrameworkUI, Power, Knowledge, Dialogue, approved machinery/quest controls, Save/Events.  
**FUTURE:** Real web browsing, remote administration, arbitrary scripting, shell commands, package installation, and simulated full operating systems.

## 15.2 Types and scopes

Use `UDocWorldTerminalSubsystem : UWorldSubsystem`, `UDocTerminalComponent`, `UDocTerminalDefinition`, `UDocTerminalApplicationDefinition`, `FDocTerminalDeviceState`, `FDocTerminalSession`, `FDocVirtualFile`, and `IDocTerminalCommandHandler`. A feature-internal LocalPlayer facade handles presentation/session attachment without storing shared device state in a widget.

Device state and user state are distinct: one physical device may have shared logs/files and per-profile read markers. A session contains SessionId, DeviceId, OwnerScope, authorization context, current application, open-resource handles, generation, and control lease. Never use the currently focused widget as device authority.

## 15.3 Virtual filesystem and applications

Virtual files use stable FileId plus a canonical logical path, content type, immutable authored content or bounded runtime payload, permissions, version, and optional soft media reference. Normalize separators and define case behavior. Reject traversal outside the virtual root, excessive depth, invalid names, and duplicate canonical paths.

The virtual filesystem is a data model; it does not map arbitrary user-supplied paths to real disk paths. Rename changes path, not stable FileId. A removed file may leave a tombstone/history entry under policy. Listing/search applies permissions before returning file names or previews.

Base applications include Messages, File Browser, Log Viewer, and Device Control. Application definitions declare supported commands and required capabilities; applications do not directly reach sibling subsystem internals. An application can be unavailable when its provider is absent without preventing the terminal base from loading.

Authored display content uses localized `FText`; player-entered fictional notes remain bounded user data rather than being silently interpreted as commands or localization keys.

## 15.4 Typed commands and authority

A command has CommandId, registered verb/schema version, typed arguments, session/device/owner context, expected state revision, and idempotency key. A handler advertises permissions, synchronous/asynchronous behavior, cancellation/commit point, and whether its effects are retryable.

Validate session generation, device access, application permission, argument schema, and current capability at execution time. A stale enabled button is not authority. Return typed result/output models; never evaluate arbitrary function names, console strings, SQL, or operating-system commands from virtual file content.

Built-in Device Control mutates only safe terminal-owned properties. Controlling a power switch, door, or quest uses a separately installed bridge with the target's validated public API. External side-effect failure remains visible; it must not be disguised as a successful command line just because text was printed.

## 15.5 Session lifecycle and presentation

```text
Opening → Authorized → Active → Closing → Closed
              └──────────────→ Failed | Revoked
```

Acquire the shared player-control lease only after the required view/device capability is ready. Device exclusivity may be SharedRead, SingleWriter, or ExclusiveSession. Single-writer claims are source-owned and expire/revoke safely; two users cannot both become writer through a race.

Closing cancels session-owned pending work before commit where possible, unsubscribes UI, releases focus/input/camera claims, and leaves shared device state intact. Device power loss, stream unload, player removal, and travel revoke the appropriate session. Committed external work may finish with an internally recorded result after the UI closes; no callback should target a destroyed widget.

Presentation receives immutable view models and submits commands. The headless base must work without CommonUI/UMG. The supplied generic front end is example content, not a mandatory project theme.

## 15.6 Persistence, networking, and security

Persist device properties, runtime virtual files, message state, authorized per-user read markers, and command receipts where needed. Do not persist active control leases, open widgets, or privileged session tokens. Restore reauthorizes access and does not rerun command history.

Shared terminals use server-authoritative command validation. Client sessions receive only authorized files/outputs. Fictional login mechanics do not require or collect the player's real passwords. Diagnostic logging redacts command arguments marked sensitive before storage. Terminal text cannot cause host network/file access merely by containing a URL/path.

## 15.7 Authoring and example

Provide file/path validation, application/command capability inspection, device state revision view, session/lock monitor, and readable error output. The fixture opens the same device from two users, reads messages, modifies a device-local setting, rejects an unauthorized command, and loses power mid-session through an optional provider.

## 15.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| TRM-01 | `Doc.Terminal.VirtualPathSafety` | Canonical paths, duplicates, traversal, and depth limits are enforced |
| TRM-02 | `Doc.Terminal.PermissionFilteredViews` | Unauthorized filenames/content never enter ordinary view models |
| TRM-03 | `Doc.Terminal.TypedCommandValidation` | Unknown verbs/types, stale revisions, and revoked sessions fail safely |
| TRM-04 | `Doc.Terminal.SingleWriter` | Simultaneous sessions cannot acquire conflicting write ownership |
| TRM-05 | `Doc.Terminal.AsyncClose` | Closing UI cancels/releases session work without losing committed results |
| TRM-06 | `Doc.Terminal.PowerAndUnload` | Device loss revokes the correct session and cleans control leases |
| TRM-07 | `Doc.Terminal.CommandReceipt` | Repeated committed command keys do not duplicate external effects |
| TRM-08 | `Doc.Terminal.RestoreNoCommands` | Restore retains data without replaying history or privileged sessions |
| TRM-09 | `Doc.Terminal.NoHostExecution` | Virtual content cannot invoke arbitrary console, shell, file, or network access |
| TRM-10 | `Doc.Terminal.HeadlessAndExample` | Headless API and generic cooked front end work independently |

---

# 16. Module 34 — DocRhythmChallenges

## 16.1 Purpose and scope

Interpret authored musical charts, judge timed inputs, track hits/misses/holds/combos, and produce versioned results. This plugin owns challenge rules and timing calibration, not a DSP engine or automatic music transcription.

**SLICE:** One lane, constant-tempo tap notes, calibrated judgment, restart, and real audible backing playback.  
**BASE:** One-lane tap/hold charts, explicit chart/audio clock mapping, timing windows, combo/scoring, calibration, pause/resume/restart, assistance profiles, and native playback reference.  
**BRIDGE:** Quartz scheduling, Enhanced Input, UI, Broadcast practice source where appropriate, objectives/results storage.  
**FUTURE:** Multiple lanes, complex tempo maps, automatic chart generation, multiplayer competitive synchronization, and rhythm-combat frameworks.

Quartz supports sample-accurate audio scheduling across engine threads. It does not by itself measure every input device, display, operating-system, or output-device latency. Keep scheduling and calibrated input judgment separate. [R8]

## 16.2 Types and chart data

Use `UDocRhythmChallengeSubsystem : ULocalPlayerSubsystem`, `UDocRhythmChart : UPrimaryDataAsset`, `UDocRhythmJudgmentProfile`, `FDocRhythmSession`, `FDocRhythmNote`, `FDocRhythmInputSample`, `FDocRhythmResult`, and `IDocRhythmClockProvider`/`IDocRhythmPlaybackProvider`.

Each chart stores stable ChartId and content hash/version, finite duration, audio source/version, chart offset, constant tempo when beat conversion is used, lane ID, stable NoteIds, note type, start/end integer timestamps, and scoring profile. Base canonical note time is integer microseconds or another declared integer timebase; beat positions are authoring data converted with a documented rounding rule.

Reject overlapping same-lane holds or ambiguous simultaneous base notes unless the chosen profile explicitly defines them. A hold end must be after its start. Chart audio duration mismatch is a validation issue, not an automatic rescale of every note.

## 16.3 Clock mapping and calibration

Establish an audio transport epoch and a monotonic input timestamp mapping at session start. Input adapters report the earliest trustworthy timestamp they have plus a precision/source classification. Do not claim hardware timestamps when the API only supplies event-dispatch time.

Define calibration sign explicitly. For example, positive `InputOffsetUs` means observed input timestamps are late and are subtracted:

```text
JudgmentTimeUs = InputTimestampMappedToChartUs - InputOffsetUs
ErrorUs = JudgmentTimeUs - NoteTimeUs
```

Audio alignment and visual scroll compensation are distinct settings. Changing visual offset does not change score unless explicitly configured. Calibration uses repeated samples, outlier rejection, a robust estimate, and uncertainty; an unreliable session returns CalibrationInsufficient rather than inventing precision.

Do not advance the authoritative judgment clock by accumulating UI frame delta. Delayed Quartz/beat delegates are notifications, not exact timestamps of input. Audio device loss, seek, pause, and restart require a new transport generation or explicit reanchor.

## 16.4 Judgment and scoring

Sort notes by timestamp then stable NoteId. Each note has one terminal judgment; duplicate input events do not hit multiple notes. A tap selects the eligible pending note under a declared nearest/earliest rule. Window edges have explicit inclusive/exclusive semantics and asymmetric early/late windows where configured.

A hold requires valid start, continued active input, and release/end behavior defined by the profile. Releasing early, losing device focus, and switching input ownership have explicit break/pause policies. Do not generate arbitrary extra misses for every frame an input remains released.

Score arithmetic is integer/bounded. Record raw judgment counts, maximum possible score, combo history summary, chart/profile hash, calibration, assistance, and timing-quality flags. No zero-note chart division by zero. A result with assistance or degraded timing is not silently placed in the same record category as a strict run.

## 16.5 Lifecycle, API, and pause

```text
Loading → Ready → CountIn → Playing ↔ Paused → Completed
                                   └────────→ Aborted | TimingFault
```

Expose LoadChart, BeginChallenge, SubmitInput, PauseChallenge, ResumeChallenge, RestartChallenge, CancelChallenge, RunCalibration, QueryTimeline, and GetResult. Async load/playback callbacks carry session generation. Restart clears judgments and creates a new AttemptId; no old hold/timer callback can score the new run.

Pause must align chart, audio transport, and input eligibility. If the backend cannot pause/resume with the required precision, return Unsupported for that mode or mark the attempt non-comparable and restart from a safe checkpoint. Do not pretend UI-only pause froze the music.

Use the shared player-control provider and an opt-in input adapter. The base must include a real audible reference path; fake clocks support deterministic unit tests but do not satisfy playback acceptance. Quartz may improve scheduling through its bridge without becoming a dependency of every other suite module.

## 16.6 Persistence, accessibility, and example

Persist chart/result identity, settings/calibration per declared device/profile, and completed results. Mid-song save/resume is deferred unless a backend-specific transport restore is explicitly implemented; loading a result never reissues completion rewards.

Provide adjustable windows, hold alternatives, reduced motion/flash, visual/audio cues, and input remapping through the host. Device changes invalidate or revalidate calibration according to its measured scope. Do not label one measured offset universally correct for all devices.

The fixture contains taps, one hold, deliberate misses, a pause, and a restart. Measure scheduling/judgment under injected game-thread hitches and record where input timestamp precision limits recovery. A hitch cannot be “fixed” by retroactively inventing player input times.

## 16.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| RHY-01 | `Doc.Rhythm.NoteWindows` | Early/late boundary judgments match the declared integer rules |
| RHY-02 | `Doc.Rhythm.InputDeduplication` | One accepted input/note pair cannot score twice |
| RHY-03 | `Doc.Rhythm.HoldLifecycle` | Start, early release, end, and focus loss produce one valid hold result |
| RHY-04 | `Doc.Rhythm.CalibrationSign` | Known synthetic delays produce the correct signed correction |
| RHY-05 | `Doc.Rhythm.ClockAndHitch` | Judgment uses mapped timestamps and flags unavailable timing precision |
| RHY-06 | `Doc.Rhythm.PauseResume` | Audio/chart/input remain aligned or explicitly reject unsupported precision |
| RHY-07 | `Doc.Rhythm.RestartGeneration` | Old-session callbacks/holds cannot affect a restarted attempt |
| RHY-08 | `Doc.Rhythm.ResultIdentity` | Chart, profile, assistance, calibration, and timing quality accompany results |
| RHY-09 | `Doc.Rhythm.EmptyInvalidChart` | Invalid/empty/ambiguous charts and score overflow fail safely |
| RHY-10 | `Doc.Rhythm.CookedAudibleRun` | A real packaged tap/hold challenge is played and timing evidence recorded |

---

# 17. Module 35 — DocGestureRecognition

## 17.1 Purpose and scope

Recognize locally drawn single-stroke two-dimensional symbols against authored templates. Mouse, touch, or analog-stick adapters produce stroke points; the recognizer returns candidates or a deliberate NotRecognized result. It does not execute abilities, own UI mappings, or require machine learning.

**SLICE:** Three templates, one captured stroke, normalization, candidate scores, and rejection.  
**BASE:** Single-stroke 2D recognition, configurable scale/rotation/direction invariance, confidence-like similarity, rejection thresholds, bounded input, template authoring, and accessible alternative invocation.  
**BRIDGE:** Enhanced Input, terminal/puzzle commands, UI, optional result events.  
**FUTURE:** Multi-stroke handwriting, language OCR, 3D motion gestures, learned models, and automatic intent inference.

## 17.2 Types and data

Use `UDocGestureSubsystem : ULocalPlayerSubsystem`, `UDocGestureTemplateSet`, `UDocGestureTemplate`, `FDocGestureStroke`, `FDocGestureRecognitionRequest`, `FDocGestureCandidate`, and `FDocGestureRecognitionResult`.

A template defines stable GestureId, localized label, canonical points, resample count, invariance policy, direction policy, distance metric, acceptance threshold, runner-up margin, minimum path length, and valid aspect rules. Runtime strokes contain owner/session/generation, ordered points, input source, timestamps where available, coordinate frame, and sampling quality.

Templates are immutable. Normalized caches are keyed by template content hash and algorithm version; editing a template cannot silently change an in-flight request's interpretation.

## 17.3 Capture and coordinate contract

Input adapters convert from their device coordinate space to a declared local drawing plane. Document origin, axis direction, aspect correction, and DPI handling. A stroke drawn on a wide widget must not be distorted by normalizing screen X and Y against unrelated dimensions.

Mouse/touch capture starts and ends explicitly. Pointer leaving the surface follows Clamp, EndStroke, or Cancel policy. Analog-stick drawing needs a documented integration speed/dead-zone model and does not pretend its samples have the same precision as touch. Device changes during capture cancel or restart under policy.

Bound points, elapsed duration, total path length, and resampling work. Reject non-finite points and degenerate zero-length strokes. A repeated stationary point cannot create divide-by-zero during resampling.

## 17.4 Recognition algorithm

A proposed base pipeline is: remove near-duplicate points → reject insufficient path → resample by arc length to a fixed count → translate to a reference center → apply the selected scale policy → compare against equivalently processed templates.

Uniform scale normalization preserves aspect ratio. Nonuniform stretching is opt-in because it can turn distinct narrow/wide symbols into the same shape. Rotation invariance is per template/set: directional arrows normally preserve orientation, while a circle may not require it. Stroke reversal is considered only where direction invariance is enabled.

Use a deterministic point-distance metric with stable tie-breaking. Return a bounded similarity score derived from the metric, but label it **similarity**, not a calibrated probability that the user's intent was understood. Require both a minimum score and a sufficient margin over the runner-up; otherwise return Ambiguous/NotRecognized.

A nearest template always exists mathematically, but the system must be able to reject noise. Tests must include scribbles and near-matches, not only perfect template copies. Rotation-search range and step/iteration limits are explicit, preventing one malformed request from consuming unbounded CPU.

## 17.5 API, asynchronous work, and integration

Expose BeginStroke, AppendPoints, EndStroke, CancelStroke, RecognizeStroke, QueryCandidates, RegisterTemplateSet, and SelectAccessibleAlternative. Recognition may run synchronously for small bounded sets or on immutable copied data in a worker. Results carry template version and request generation; old results do not trigger actions after the player starts another gesture.

Recognition returns data. A separate authorized command/bridge decides whether to operate a puzzle or terminal. An accepted gesture cannot bypass current interaction permissions or consume an item just because the screen showed a matching icon.

Keep raw strokes transient by default. Persist template/customization/calibration settings only through an explicit owner-scoped storage policy. User gesture recordings used for debugging require opt-in and retention limits.

## 17.6 Accessibility, authoring, and example

Provide a button/menu alternative that submits the same semantic gesture intent with an explicit input-method label. The gameplay consumer decides whether an assisted input changes challenge classification; do not deny access merely because a player cannot draw reliably.

Editor tools preview normalized templates, show candidate distances/margins, warn about templates that collide under their invariance settings, and export a versioned test fixture. The example recognizes three symbols, rejects an ambiguous fourth, and offers a non-drawing input route without requiring CommonUI.

## 17.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| GES-01 | `Doc.Gesture.ResampleDegenerate` | Duplicate/zero-length/non-finite points fail or normalize safely |
| GES-02 | `Doc.Gesture.ScaleAndAspect` | Uniform/nonuniform policies preserve the intended symbol distinctions |
| GES-03 | `Doc.Gesture.RotationPolicy` | Directional templates do not accidentally accept rotated alternatives |
| GES-04 | `Doc.Gesture.StrokeDirection` | Reversal is accepted only for templates that allow it |
| GES-05 | `Doc.Gesture.RejectionMargin` | Noise and ambiguous near-matches return NotRecognized/Ambiguous |
| GES-06 | `Doc.Gesture.TemplateVersion` | Edited templates cannot change an in-flight result's recorded model |
| GES-07 | `Doc.Gesture.CancelGeneration` | Cancelled/old recognition cannot invoke a new session's consumer |
| GES-08 | `Doc.Gesture.InputBounds` | Point/duration/search limits prevent unbounded work |
| GES-09 | `Doc.Gesture.AccessibleAlternative` | Alternate semantic input reaches the same validated consumer path |
| GES-10 | `Doc.Gesture.LocalIsolation` | Two players/template sets remain isolated without AI or UI dependencies |
---

# 18. Module 36 — DocAssemblyMaintenance

## 18.1 Purpose and capability boundary

Represent multipart objects and meaningful assembly/disassembly procedures: covers, fasteners, replaceable parts, compatibility, diagnostics, step order, reassembly, and functional testing. This is not crafting recipes, building placement, skeletal equipment attachment, or restoring one generic health value.

**SLICE:** Diagnose one faulty module, remove an enclosure, replace the module, reassemble, and run a functional test.  
**BASE:** Stable part/slot structure, installed/detached states, fasteners, procedural prerequisites, exclusive manipulation, compatibility validation, fault observations, part replacement, and test results.  
**BRIDGE:** Inventory/tools, Interaction/Inspection, Power/Mechanical lockout state, Save/Events.  
**FUTURE:** Arbitrary CAD assemblies, physically simulated screw threads, procedural manufacturing, and universal repair of unknown assets.

## 18.2 Types and authored structure

Use `UDocAssemblySubsystem : UWorldSubsystem`, `UDocAssemblyComponent`, `UDocAssemblyDefinition`, `UDocAssemblyPartDefinition`, `UDocMaintenanceProcedure`, `FDocAssemblyState`, `FDocPartInstance`, `FDocAssemblyOperation`, and `IDocAssemblyResourceProvider`.

Definitions declare stable AssemblyId, PartDefinitionIds, SlotIds, compatibility tags/constraints, authored attachment transforms, removable relations, fastener IDs, prerequisite graph, supported diagnostic tests, and resource/safety capabilities. Runtime part instances have persistent PartInstanceId, definition version, condition/fault state, installed slot or detached location, and revision.

One part instance has exactly one logical location. An installed part cannot simultaneously exist in a detached registry or inventory container. A slot holds at most its declared capacity. Parent/child attachment relations are acyclic and validated independently of the procedure graph.

## 18.3 Procedure and diagnostic semantics

Operations include Inspect, Test, ReleaseFastener, SecureFastener, RemovePart, InstallPart, ReplacePart, and CompleteProcedure. They validate current state rather than trusting the UI's displayed next step. A cover cannot be removed before its required fasteners are released; an internal module cannot be replaced through an obstructing installed cover unless authored as accessible.

Tool requirements are capabilities/tags with a trusted provider, not hard-coded inventory classes. The base example uses a local tool capability provider; a production inventory bridge verifies ownership and consumption/durability separately.

Fault state, diagnostic observation, and player knowledge are distinct. A normal query reports available test results, not every hidden fault. A diagnostic test may be inconclusive or blocked by power/configuration. Replacing a part does not automatically mark the entire machine functional; the post-repair test evaluates the resulting assembly.

## 18.4 Transaction and exclusive manipulation

```text
Validate session/owner/revision → Acquire part/slot manipulation claims
→ Validate resources and prerequisites → Stage new assembly state
→ Commit logical operation → Apply/reconcile presentation/resources
→ Record receipt → Release claims
```

Base operations affecting only the feature's own logical parts commit atomically. A failed validation leaves all part locations and fastener states unchanged. Concurrent attempts to remove the same part or install into one slot are conflicts, not last-writer-wins.

Cross-plugin inventory transfer requires a cooperating transaction/reservation interface. Do not claim atomic cross-feature replacement by first removing an inventory item and then hoping installation succeeds. Use a shared local commit coordinator when available, or a durable intent/receipt/reconciliation model with an explicit in-doubt state. When the provider cannot meet the advertised guarantee, return Unsupported or use a clearly labeled non-atomic profile; do not silently fall back.

Visual mesh spawning/attachment occurs from committed logical state. A failed visual spawn cannot create a second logical part. Expose PresentationPending and retry boundedly; collision/interaction on an unresolved visual proxy must not allow duplicate collection.

## 18.5 Cancellation, control, and APIs

Expose BeginMaintenanceSession, QueryAssembly, QueryAvailableOperations, RequestOperation, CancelOperation, RunDiagnostic, QueryProcedureProgress, EndSession, CaptureAssemblyState, and StageRestore.

Long animations use operation handles. Cancel before logical commit leaves original assembly state; cancel afterward returns the committed outcome and stops only presentation the operation owns. Fastener progress may be transient until a declared commit threshold; do not save half a logical removal without a model for it.

Manipulation uses host input/camera/control leases where needed. Restrict a part while another user holds its manipulation lease. Machine-operational restrictions are gameplay state checks supplied by a provider; a missing required lockout/power capability fails explicitly rather than assuming safe/disabled state.

## 18.6 Persistence, streaming, and authority

Persist part identities, slot membership, detached part records, fastener states, fault/test observations, procedure revision, and resource receipts. Restore validates one-location-per-part and migrates changed slot/part definitions. Missing content is quarantined; do not delete a valuable part or duplicate it into an inventory to hide a mismatch.

Streaming out a detached visual does not destroy the part. Restore logical identity before creating visual proxies. Shared assembly operations are server-authoritative; clients cannot install arbitrary compatible-looking classes or nominate another player's part ID.

The default persistent procedure resumes at committed logical steps. An interrupted animation is restarted/reconciled visually, not treated as proof that its operation completed.

## 18.7 Authoring and example

Provide hierarchy/slot preview, prerequisite validation, fastener state inspector, part-location consistency checks, diagnostic-rule preview, and transaction/reconciliation logs. Use one generic machine with a cover, two fasteners, a replaceable module, and a post-repair test. Add an intentional incompatible replacement and concurrent-user conflict fixture.

## 18.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| ASM-01 | `Doc.Assembly.PrerequisiteOrder` | Covers/fasteners/parts enforce current structural prerequisites |
| ASM-02 | `Doc.Assembly.PartUniqueness` | A part occupies exactly one logical location across operations |
| ASM-03 | `Doc.Assembly.SlotCompatibility` | Invalid parts/slots fail without partial mutation |
| ASM-04 | `Doc.Assembly.ConcurrentManipulation` | Conflicting users cannot remove/install the same part simultaneously |
| ASM-05 | `Doc.Assembly.CancelCommitBoundary` | Cancellation before/after commit reports and preserves the correct state |
| ASM-06 | `Doc.Assembly.ResourceReconciliation` | Inventory bridge failures cannot silently lose/duplicate a part |
| ASM-07 | `Doc.Assembly.VisualFailure` | Failed presentation cannot create duplicate logical ownership |
| ASM-08 | `Doc.Assembly.DiagnosticTruth` | Observations remain separate from hidden faults; final test is required |
| ASM-09 | `Doc.Assembly.RestoreMigration` | Changed slots and detached parts restore or quarantine coherently |
| ASM-10 | `Doc.Assembly.IsolatedMachine` | Complete example works without Inventory, Interaction, or physics simulation |

---

# 19. Module 37 — DocReplayGhosts

## 19.1 Purpose and capability boundary

Record selected motion and presentation state over time and play it through isolated visual representations. Support time-trial ghosts, demonstrations, routes, annotations, scrubbing, and comparisons without mutating live gameplay.

**SLICE:** Record one actor's transform, play a collisionless surrogate, pause/seek, and verify no live-world effects.  
**BASE:** Selected transform/action-token tracks, bounded sampling, discontinuities, indexed playback, annotations, local recording format, visual-only ghost proxies, and resource-safe lifecycle.  
**BRIDGE:** RaceTiming, PlaytestRecorder references, native Unreal Replay, optional presentation/animation adapters.  
**FUTURE:** Full-world rewind, arbitrary actor cloning, deterministic input re-simulation, multiplayer rollback, and universal replay compatibility.

Unreal's native Replay system uses DemoNetDriver/streamers; its documentation warns that replay actors can invoke gameplay functions affecting shared state. The optional native bridge therefore needs explicit isolation tests rather than assuming playback is harmless. [R9]

## 19.2 Types and recording format

Use `UDocReplayGhostSubsystem : UWorldSubsystem` for world tracks/proxies, a LocalPlayer viewing facade, `UDocGhostProfile`, `UDocGhostTrackDefinition`, `FDocGhostRecordingHeader`, `FDocGhostFrame`, `FDocGhostTrackChunk`, and `FDocGhostPlaybackSession`.

Header fields: RecordingId, owner/scope, format and algorithm versions, source build/content hash, world/course namespace, coordinate-frame policy, sample clock, track definitions, duration, chunk index, and integrity metadata. Track definitions reference approved visual resources, never arbitrary spawnable gameplay classes.

Samples contain timestamp, stable track ID, transform/rotation, optional selected animation/state token, discontinuity flag, and optional bounded annotation/event data. Use explicit schema for every channel. Do not serialize arbitrary actor properties or execute recorded function names.

## 19.3 Sampling and coordinate semantics

Sampling uses a declared cadence and change thresholds. Record actual sample times; a dropped sample is a gap, not invented input. Bound track count, duration, buffer size, and outstanding writes. The base records observed motion, not enough information to prove a deterministic re-simulation.

Coordinate policy may be world-namespace coordinates with origin offsets or a stable local anchor. Rebase changes are captured explicitly. Runtime actor labels or moving parent pointers are not persistent anchors. Teleports, spawn/despawn, and attachment-frame changes create discontinuities; playback must not interpolate through them.

Linear position interpolation and normalized shortest-path quaternion interpolation are default presentation choices. Large gaps follow Hold, Hide, or Snap policy with a visible quality flag. Interpolation does not manufacture authoritative checkpoint crossings or collision events.

## 19.4 Playback isolation

Use dedicated visual surrogate actors/components with collision, gameplay interaction, AI, replication, and reward registration disabled by construction. Do not clone the host Pawn/Character and hope disabling Tick suppresses every side effect.

Animation adapters are presentation-only. Disable/filter root motion and gameplay-bearing animation notifies; playing a recorded montage must not fire live weapon, item, or quest actions. If an animation system cannot isolate those effects, use a simpler pose/transform proxy and report the missing capability.

The viewer owns camera/input leases only when explicitly requested. Playback can run beside live gameplay without pausing or replacing it. A local ghost's timeline events are sent through a replay-specific presentation channel, never the ordinary live DocEvents topic without a guard/bridge policy.

## 19.5 Seek, lifecycle, and API

Expose StartRecording, StopRecording, CancelRecording, AddAnnotation, OpenRecording, StartPlayback, PausePlayback, SeekPlayback, SetPlaybackRate, StopPlayback, and QueryTrackState. Recording and playback have separate handles and generations.

Build chunk indexes/checkpoints so seeking does not scan the entire recording or replay gameplay effects from time zero. Seeking reconstructs visual state at the target timestamp. One-shot visual annotations fire only under a declared playback policy; reverse/seek cannot duplicate a live-world action.

Async file reads and asset loads are generation-tagged. Closing a recording releases handles and invalidates pending chunk results. Cancel before finalized file commit leaves no advertised valid recording; finalized files receive an integrity-checked manifest.

## 19.6 Storage, migration, and networking

Local ghost recordings have their own bounded artifact format; they are not ordinary world saves. A Save bridge may store references/selection preferences, not embed unlimited recording bytes in every autosave. Validate chunk offsets, counts, duration, decompressed sizes, numeric transforms, and approved visual identifiers before playback.

Unsupported recording versions fail without modifying the original. Content changes may permit transform-only playback with a declared fallback visual, but must not silently claim full original animation compatibility.

Networked sharing is not base scope. Received recordings would be untrusted data; no class paths or events from a file may authorize arbitrary object creation or commands. Native Replay is a separate backend with its own recording/compatibility/isolation profile.

## 19.7 Authoring and example

Provide timeline/track view, discontinuity markers, resource statistics, selected-channel inspector, and an “effects suppressed” debug indicator. The fixture records a moving generic actor with a teleport, loops a visual-only playback, seeks backward, and runs beside an interactable collectible that must remain untouched.

## 19.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| GHO-01 | `Doc.Ghost.SampleTimeline` | Actual timestamps/cadence/gaps are recorded without fabricated samples |
| GHO-02 | `Doc.Ghost.Interpolation` | Position/rotation interpolation follows declared tolerance |
| GHO-03 | `Doc.Ghost.Discontinuity` | Teleports/rebases/frame changes do not interpolate through invalid space |
| GHO-04 | `Doc.Ghost.NoLiveEffects` | Ghost cannot collide, collect, damage, complete objectives, or mutate saves |
| GHO-05 | `Doc.Ghost.AnimationNotifyIsolation` | Presentation cannot dispatch gameplay notifies/root-motion effects |
| GHO-06 | `Doc.Ghost.IndexedSeek` | Seek reconstructs state without replaying ordinary gameplay events |
| GHO-07 | `Doc.Ghost.AsyncClose` | Late chunk/assets cannot revive stopped playback |
| GHO-08 | `Doc.Ghost.MalformedRecording` | Invalid offsets/counts/classes/numerics are rejected safely |
| GHO-09 | `Doc.Ghost.VersionFallback` | Unsupported data fails or uses an explicitly labeled visual fallback |
| GHO-10 | `Doc.Ghost.CookedConcurrentPlayback` | Cooked visual playback coexists with unchanged live gameplay |

---

# 20. Module 38 — DocRaceTiming

## 20.1 Purpose and scope

Validate and time checkpoint courses, laps, routes, and time trials independently of movement type. Support starts, directional gates, missed checkpoints, penalties, splits, personal bests, and result comparison. Do not implement movement, vehicle physics, route navigation, or online leaderboards.

**SLICE:** One participant, three directional gates, a timed finish, and a local result.  
**BASE:** Ordered checkpoint courses, countdown/running starts, laps, directional swept gate tests, false-start/missed-gate policies, splits, penalties, run validity, versioned local results, and best-run queries.  
**BRIDGE:** Map markers, Ghost recordings, UI, optional Save/Events/rewards.  
**FUTURE:** Complex branching race rules, full race-position estimation, online rankings, anti-cheat service, and vehicle-specific rules.

## 20.2 Types and definitions

Use `UDocRaceTimingSubsystem : UWorldSubsystem`, `UDocRaceParticipantComponent`, `UDocRaceGateComponent`, `UDocRaceCourseDefinition`, `FDocRaceRun`, `FDocRaceSplit`, `FDocRaceResult`, and `IDocRacePositionProvider`.

Course definitions contain stable CourseId, content/rules hash, world/instance namespace, ordered GateIds, each gate's plane/bounds/forward direction, start/finish policy, lap count, minimum route rules, penalty rules, and assistance/record categories. Gate array reorder changes course content revision, not stable gate identity.

Participants provide stable identity, trusted position samples, sample timestamps/quality, and discontinuity notifications. The rules do not assume CharacterMovement or a vehicle pawn.

## 20.3 Swept directional gate validation

Use signed distances from consecutive trusted participant positions to an authored gate plane. A forward crossing requires the declared sign transition, an intersection inside gate bounds, and an eligible current run state. Starting inside the volume is not automatically a crossing. Add spatial hysteresis to avoid jitter repeatedly crossing the plane.

For a linear segment with signed distances `d0` and `d1`, an illustrative crossing fraction is:

```text
alpha = d0 / (d0 - d1)
```

Validate denominator, range, sample times, and finite values before use. Interpolated crossing time is an estimate under a linear-motion assumption, not perfect subframe physics. Record tolerance/quality when samples are sparse or motion is curved.

Evaluate multiple eligible gate crossings in a long segment by increasing intersection fraction, then stable ID. Course authoring rejects ambiguous overlapping gates where ordering cannot be established under its rules. A hitch must not miss a gate solely because overlap events were absent.

Teleports, respawns, or world-frame discontinuities break the segment. They do not count as crossing every plane in between. A position provider must report trusted discontinuities; coarse speed/distance checks are additional sanity rules, not a complete anti-cheat guarantee.

## 20.4 Run state, clocks, and scoring

```text
Registered → Ready → Countdown → Running → Finished
                                └────────→ Invalid | Aborted
```

A running start may begin on a dedicated start-plane crossing. False-start behavior is RejectStart, Penalty, or Invalidate according to the course. Countdown display is presentation; the authoritative start boundary is the run clock's timestamp.

Use monotonic authoritative elapsed time, with an explicit pause policy. Competitive/shared runs do not pause merely because one local menu opened. Practice pause is a separate result category. Lap advancement requires the full ordered gate sequence; a finish gate cannot skip the course.

Keep elapsed time and penalties separate. Base penalties are bounded nonnegative durations/counts with stable IDs; duplicate penalty reports do not apply twice. Final time is elapsed plus penalties under overflow checks. Record splits against exact course/rules/assistance identity; do not compare incompatible course versions as personal bests.

## 20.5 API and persistence

Expose RegisterParticipant, BeginRun, SubmitPositionSample, NotifyDiscontinuity, ApplyPenalty, AbortRun, QueryRun, QuerySplits, FinalizeResult, QueryPersonalBest, CaptureResults, and StageRestore. A run handle includes owner/world/attempt generation. Duplicate finish requests return the existing committed result.

A result commits once, then optional reward/record bridges consume its effect key. A failed ghost save does not invalidate a legitimately finished race; mark the optional recording unavailable. A ghost is comparison presentation, not trusted evidence that the run was valid.

Base completed results can be captured through versioned storage contracts. Mid-run restoration defaults to Aborted/PracticeResume under explicit policy; it must not allow repeatedly reloading before a mistake and submitting an unmarked competitive result. Restore never regrants race completion rewards.

## 20.6 Authority, tooling, and example

Shared races use trusted server samples/validation and scoped client presentation. Client-supplied elapsed times are not authoritative. Network interpolation and latency handling are separately measured; do not claim cheating prevention from one Server RPC check.

Provide course/gate direction previews, crossing traces, expected-next-gate view, run validity reasons, split comparisons, and sample quality diagnostics. The fixture uses both an ordinary moving actor and a different host-controlled participant, includes a high-speed crossing, reverse crossing, teleport, missed gate, and valid lap.

## 20.7 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| RAC-01 | `Doc.Race.DirectionalGate` | Forward/reverse/inside-start cases follow declared crossing rules |
| RAC-02 | `Doc.Race.SweptHighSpeed` | A valid high-speed segment crossing is detected without overlap events |
| RAC-03 | `Doc.Race.MultiGateSegment` | Multiple crossings are ordered by segment fraction and validated course order |
| RAC-04 | `Doc.Race.TeleportDiscontinuity` | Teleport/respawn/rebase cannot grant intervening checkpoints |
| RAC-05 | `Doc.Race.StartAndPause` | Countdown, false start, and practice/competitive pause policies are distinct |
| RAC-06 | `Doc.Race.LapAndMissingGate` | Finish/lap cannot bypass required checkpoints |
| RAC-07 | `Doc.Race.PenaltyIdempotency` | Duplicate penalties/finish requests do not change committed results twice |
| RAC-08 | `Doc.Race.RecordCompatibility` | Personal best comparisons respect course/rules/assistance versions |
| RAC-09 | `Doc.Race.RestoreNoRewards` | Restore does not manufacture a valid competitive resume or repeat rewards |
| RAC-10 | `Doc.Race.MovementAgnostic` | Two different participant types work without vehicle/movement dependencies |

---

# 21. Module 39 — DocModContent

## 21.1 Purpose and trust boundary

Load user-authored **data-only** content packs into registered feature catalogs using controlled schemas and approved existing assets. Own manifests, versions, dependency resolution, registration, conflicts, and missing-content handling. The base is not a native-code loader, Blueprint-code sandbox, general plugin installer, or arbitrary package mounter.

**SLICE:** Validate two data packs, resolve their dependency order, register one approved definition type through a sample provider, and reject an unsafe pack.  
**BASE:** Directory-based data packs, versioned manifests, strict schemas, namespace IDs, dependency/conflict checks, catalog transactions, load-order explanation, resource quotas, pinning, and safe missing-content behavior.  
**BRIDGE:** Individually selected feature importers; optional trusted developer-built Game Features activation.  
**FUTURE:** Arbitrary scripts/native DLLs, untrusted cooked Blueprints, public mod hosting, automatic downloads, archive extraction, and a security sandbox.

Game Features supports modular activation of developer-built features. It is an optional trusted-package integration, not evidence that arbitrary user content is sandboxed. [R10]

## 21.2 Types and manifest

Use `UDocModContentSubsystem : UGameInstanceSubsystem` for a process/game catalog, `UDocModContentSettings`, `FDocModManifest`, `FDocModDependency`, `FDocModActivationPlan`, `FDocModCatalogRevision`, and `IDocModDefinitionProvider`.

Manifest minimum fields:

```json
{
  "manifest_version": 1,
  "pack_id": "example.author.training_pack",
  "pack_version": "1.0.0",
  "display_name": "Training Pack",
  "requires": [],
  "conflicts": [],
  "definitions": [
    {
      "id": "example.author.training_pack:sample_a",
      "schema": "doc.sample.definition",
      "schema_version": 1,
      "path": "definitions/sample_a.json"
    }
  ]
}
```

This JSON is an illustrative schema proposal, not an implemented loader format. Before publishing pack support, freeze the schema and version-range grammar in an ADR. Base may support exact/minimum/maximum version constraints rather than an incompletely implemented package-manager expression language.

## 21.3 File and data validation

Read only explicitly configured pack roots. Resolve canonical real paths, reject absolute/parent traversal and symlink/reparse escapes, enforce case-normalized collision rules on Windows, and cap total files, file size, JSON depth, string length, definition count, and aggregate bytes. Revalidate content identity between scan and activation to avoid loading different bytes from those validated.

The base reads approved directory content; archive extraction is deferred. Do not write an insecure unzip helper to make packaging convenient. Never load executable libraries, arbitrary `.uasset`/`.umap` content, operating-system commands, or unregistered reflected object types from a base pack.

Definition schemas are provided by installed trusted code. Assets are referenced by allowlisted registered IDs, not unrestricted disk/class paths. A provider validates every field and converts data into its feature's approved immutable definition model. No generic DeserializeAnything-to-UObject escape hatch is permitted.

## 21.4 Dependency resolution and activation

Namespace all content IDs by pack. Validate duplicate pack IDs, dependency versions, missing requirements, cycles, declared conflicts, and unsupported schema versions before modifying a catalog. Deterministic topological ordering uses stable pack ID tie-breaks; user order applies only where consistent with dependencies.

Base collisions fail rather than silently override another pack's definition. Explicit override support is a later policy requiring target permission, precedence, and compatibility rules. A load-order explanation lists why each pack precedes another and which definitions/capabilities would be registered.

Activation flow:

```text
Discover → Validate bytes/schemas → Resolve dependency plan
→ Stage all provider changes → Validate staged catalog
→ Commit one catalog revision → Publish change notification
```

A provider must support staged registration/rollback for atomic catalog activation. A partial failure cannot leave half the pack visible. If provider interfaces cannot meet that guarantee, restrict activation to a safe initialization point and fail the whole plan before live use; do not advertise hot reload.

## 21.5 Pinning, deactivation, and saved games

Consumers pin definition/catalog versions while sessions, saves, charts, or recordings depend on them. Base activation/deactivation occurs at startup or an explicit safe point with no conflicting live pins. Removing a pack with live consumers returns InUse, not a force unload that leaves dangling references.

Saved records store pack/definition identity and content version/hash. Loading a save with missing packs reports MissingContent and preserves dependent records. Do not strip missing items/evidence and overwrite the save automatically. A host may choose quarantine, read-only inspection, or a documented migration.

Checksums detect accidental changes; they do not authenticate authors or make content trustworthy. Digital signatures, when later added, need their own trust/key policy. The loader's constrained schema reduces exposure but is not a formal sandbox claim.

## 21.6 API, feature boundaries, and developer bridge

Expose DiscoverPacks, ValidatePack, BuildActivationPlan, QueryPlanDiagnostics, ActivatePlan, RequestDeactivate, QueryCatalog, AcquireDefinitionPin, ReleaseDefinitionPin, and GetSaveCompatibilityReport. Calls carry catalog revision and return exact affected pack/definition IDs.

Base uses a sample definition provider so it can be tested without installing any gameplay sibling. Real adapters such as `DocModContentPuzzles` or `DocModContentRhythm` depend on both parties. No base feature imports the mod loader just to consume its own ordinary Data Assets.

The optional Game Features bridge handles only trusted developer-built packages with verified platform/engine compatibility and explicit activation policy. It cannot grant untrusted packs permission to run native or Blueprint code under the base's “data-only” label.

## 21.7 Authoring and example

Provide a pack linter, schema templates, dependency/activation preview, content fingerprint manifest, conflict explanations, and clear error paths. The fixture includes valid dependency ordering, an unavailable schema provider, duplicate IDs, a traversal attempt, a cycle, and a missing pack referenced by saved data.

## 21.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| MOD-01 | `Doc.Mod.ManifestSchema` | Manifest/schema/version/range validation is strict and bounded |
| MOD-02 | `Doc.Mod.PathContainment` | Traversal, absolute paths, links/reparse escapes, and case collisions fail |
| MOD-03 | `Doc.Mod.DependencyOrder` | Deterministic ordering handles missing versions, cycles, and conflicts |
| MOD-04 | `Doc.Mod.NamespaceCollision` | Duplicate content cannot silently override another pack |
| MOD-05 | `Doc.Mod.TransactionalActivation` | Provider failure leaves the prior catalog wholly intact |
| MOD-06 | `Doc.Mod.ValidatedByteIdentity` | Changed bytes between validation and activation invalidate the plan |
| MOD-07 | `Doc.Mod.PinAndDeactivate` | In-use definitions prevent unsafe removal |
| MOD-08 | `Doc.Mod.MissingSaveContent` | Missing packs preserve/quarantine records without destructive save rewrite |
| MOD-09 | `Doc.Mod.NoExecutableContent` | Base packs cannot load arbitrary code/classes/assets or execute commands |
| MOD-10 | `Doc.Mod.IsolatedProvider` | Sample provider proves loader behavior with all gameplay siblings absent |

---

# 22. Module 40 — DocPlaytestRecorder

## 22.1 Purpose and capability boundary

Collect bounded, useful diagnostic context around a tester-marked issue and export a local evidence bundle. Record selected input intents, events, state snapshots, logs, screenshots, and build identity only through explicit opt-in providers. This is not automatic telemetry, a keylogger, a full crash reporter, or guaranteed bug reproduction.

**SLICE:** Opt-in event ring buffer, issue marker, bounded state snapshot, and a local manifest/export.  
**BASE:** Provider registry, bounded pre/post-marker buffers, redaction, state/event correlation, optional screenshot attachment, resource quotas, export validation, retention controls, and an optional viewer.  
**BRIDGE:** DocEvents and selected module state providers, Ghost recording references, engine-log capture, screenshot provider, issue-tracker upload only under separate authorization.  
**FUTURE:** Automatic reproduction, always-on remote analytics, voice/video recording, operating-system input capture, and unattended uploads.

Build the small recorder slice early in Phase 9 as optional development infrastructure. No gameplay base depends on it, and a disabled recorder must not break normal gameplay.

## 22.2 Types, lifetime, and provider boundary

Use `UDocPlaytestRecorderSubsystem : UGameInstanceSubsystem` for bounded detached evidence/export, feature-internal world registration contexts, `UDocPlaytestCaptureProfile`, `FDocPlaytestEvent`, `FDocIssueMarker`, `FDocDiagnosticSnapshot`, and `IDocPlaytestEvidenceProvider`.

A provider declares schema/version, owner/audience, sensitivity classes, snapshot cost/limits, supported worlds, and redaction rules. World callbacks capture approved values on the game thread; the recorder retains immutable records, not live actors after travel. Providers receive registration handles and unregister before destruction.

Record identity includes session/run ID, source/build/content identity, enabled plugin versions, engine/toolchain where available, capture profile, monotonic timestamp, world epoch, correlation/operation IDs, and optional simulation/audio clocks with explicit mapping metadata.

## 22.3 Ring buffer and marker semantics

Use byte limits as well as event count/time limits. A single giant payload cannot consume unlimited memory despite a low event count. Segment storage permits bounded eviction without copying the whole buffer every frame. Track dropped/evicted events, provider failures, and sampling gaps visibly.

An issue marker freezes a reference to a bounded pre-marker range and optionally collects a bounded post-marker range. Multiple markers share immutable segments or copy under an explicit quota. Pinning segments cannot bypass the global cap; additional markers may be rejected or stored without full context under a clearly reported policy.

A marker records tester note, category, local/world time, current scope, and capture status. It does not pause the game unless an authorized capture profile explicitly requests a control lease. Crash resilience is not promised by an in-memory ring buffer; optional journal flushing must be separately specified and measured.

## 22.4 Privacy and safe capture

Default input capture is semantic gameplay intent, not raw keystrokes. Exclude password/text-entry fields, authentication tokens, clipboard, microphone, network credentials, full user directories, and arbitrary memory dumps. Providers classify fields before retention; redact/drop sensitive values before they enter a ring buffer or worker queue.

Screenshots are optional and potentially sensitive. Capture only the game view through an approved provider, with visible profile policy and exclusion/redaction capabilities where supported. A screenshot may still contain player names or custom text; warn in the export preview and permit omission. No automatic upload occurs.

Do not collect an entire filesystem or unbounded engine log because it might help. Logs use a bounded allowlisted category/level filter and secret redaction. Binary attachments have allowlisted formats, sizes, and generated paths.

## 22.5 Export transaction and format

Suggested bundle layout:

```text
issue_<stable-id>/
  manifest.json
  issue.json
  events.jsonl
  snapshots/
  attachments/
  logs/
  README.md
```

The manifest identifies schema versions, source/content fingerprints, event time range, omitted/dropped data, files with sizes/hashes, redaction profile, and capture limitations. It must not claim the captured state is a coherent atomic world snapshot unless a provider actually supplies that guarantee.

Export snapshots immutable segments → writes a temporary bundle under a controlled root → validates file counts/hashes → commits the bundle → returns its path/status. Optional archive packaging requires safe generated relative paths. Cancellation/failure removes only the export's own staged files; existing completed bundles remain intact.

Checksums establish integrity, not reproducibility or authenticity. A viewer displays provenance and missing-provider warnings. Imported bundles are untrusted data: no auto-execution of commands, opening arbitrary local paths, or loading arbitrary gameplay classes.

## 22.6 API, overhead, and disabled state

Expose EnableCapture, DisableCapture, RegisterEvidenceProvider, RecordIntent/Event, AddIssueMarker, QueryMarkerStatus, PreviewExport, ExportIssue, CancelExport, DeleteOwnedBundle, and QueryRecorderStats.

Enablement is explicit per profile/session. Disabled mode must avoid constructing large payloads; providers can query a cheap capability flag before expensive capture. Bound per-frame snapshot cost, event serialization work, pending exports, and disk usage. Provider failure produces a recorded omission; it cannot crash gameplay or stop other providers.

Export and viewer work run outside authoritative gameplay mutation. Workers consume sanitized immutable data. Leaving a world flushes/detaches world references and records an epoch boundary. The recorder is not a universal service locator that imports all forty systems.

## 22.7 Authoring, distribution, and example

Provide capture-profile validation, live counters, marker timeline, preview of included/redacted fields, and a separate optional Editor viewer. Development/Test builds can include enabled capture profiles. Shipping inclusion and default enablement require explicit product policy; do not automatically enable diagnostics in every distributed build.

The fixture marks a simulated failed operation, exports its recent correlated events and one approved snapshot, injects disk failure, and verifies no secret field survives. Another fixture has the recorder physically absent while gameplay continues normally.

## 22.8 Acceptance and tests

| ID | Test name | Required result |
|---|---|---|
| DBG-01 | `Doc.Playtest.ByteBoundedBuffer` | Count/time/byte limits remain enforced under oversized payload pressure |
| DBG-02 | `Doc.Playtest.MarkerWindow` | Pre/post-marker windows and shared segment quotas are correct |
| DBG-03 | `Doc.Playtest.RedactBeforeRetention` | Sensitive fields never enter retained buffers or exports |
| DBG-04 | `Doc.Playtest.ProviderFailure` | Slow/failing providers produce bounded omissions without gameplay failure |
| DBG-05 | `Doc.Playtest.ExportTransaction` | Disk/cancel faults preserve old bundles and clean only owned staging |
| DBG-06 | `Doc.Playtest.ManifestIntegrity` | Files/hashes/time ranges/omissions match actual exported data |
| DBG-07 | `Doc.Playtest.WorldTeardown` | Travel removes live references and records context boundaries |
| DBG-08 | `Doc.Playtest.UntrustedViewerInput` | Malformed bundles cannot execute commands or escape allowed paths |
| DBG-09 | `Doc.Playtest.DisabledOverhead` | Disabled capture avoids expensive payload construction and allocations |
| DBG-10 | `Doc.Playtest.NoAutomaticUpload` | Export is local, user-controlled, and independent of gameplay plugins |
---

# 23. Cross-System Integration Rules and End-to-End Scenarios

## 23.1 One owner for every fact

A bridge translates contracts; it does not create a second owner for another feature's state. Power owns energy and granted supply. Fluid owns volumes. Mechanical owns drive state. Material Reactions owns material facts. Puzzle owns completion. Inventory owns container membership. Race owns valid results. Recording systems observe these facts and do not author them.

Choose a producer direction for each connection and document it in `Docs/DEPENDENCY_MATRIX.md`. Do not solve circular ownership by broadcasting events both ways until values agree. A bridge receives a committed snapshot and submits a new explicit command at the next appropriate boundary.

## 23.2 Coupled simulation without algebraic loops

A powered pump or driven generator can create circular relationships. The base integration profile uses **explicit one-step-lag coupling** unless a separately implemented coordinator provides a stronger joint solution:

```text
Power state at step k → pump/mechanical availability for step k+1
Fluid/mechanical measured demand at step k → requested power for step k+1
```

Document the shared clock mapping, lag, units, startup state, and unavailable-provider policy. Hysteresis can reduce oscillation but is not proof of stability. Cap retries/iterations; do not repeatedly solve an unbounded circular graph in one frame.

A richer jointly solved model requires a new ADR, shared snapshot/commit boundary, convergence criterion, maximum iterations, and fault behavior. The initial expansion does not promise it. Prevent a generator powered by its own output from becoming a perpetual-motion loop through bridge accounting; base source/sink roles and losses remain explicit.

## 23.3 Effects, result receipts, and recovery

A producer commits its internal transition and emits a keyed intent. The consumer validates and commits its own mutation with a receipt. Persist producer outbox, consumer receipt, and relevant gameplay state under the declared snapshot consistency model. Retry only where the consumer implements idempotency.

A solved puzzle, accepted conclusion, completed rhythm chart, repaired assembly, or finished race remains committed even if an optional reward delivery is temporarily unavailable. Expose Pending/FailedEffect separately. An operator can retry or reconcile the intent without rerunning the underlying gameplay event.

A custom Blueprint effect without a receipt cannot be silently retried on load. Record its non-retryable classification and a recovery instruction. Never claim cross-service exactly-once behavior solely because an event bus delivered once in a test.

## 23.4 Persistence and definition pinning across modules

Pin definition/content revisions for active sessions: puzzle attempts, rhythm charts, race courses, terminal applications, mod catalogs, and ghost recordings. A content update cannot reinterpret an active attempt halfway through execution. Safe-point migration or session restart is explicit.

Save-related bridges must coordinate ownership and capture boundaries. Example: installing a part from an inventory requires assembly location and inventory ownership to agree in the saved revision. A separate paint checkpoint must be durable before a save claims that checkpoint ID exists. A photo/ghost blob reference must indicate Available, Pending, Missing, or Incompatible truthfully.

Restoration never masquerades as new input, new discoveries, fresh checkpoint crossings, fresh ignition, or fresh completion. Rebuild derived views after state is applied; suppress consumer rewards until the restore barrier has completed.

## 23.5 Streaming and active-operation leases

Long-lived operations may request optional streaming/activation leases through providers. Examples include a maintained puzzle input, a station service in progress, an assembly transaction, or an imminent photograph. The requester releases only its lease.

No feature may assume streaming is installed. Without a residency provider it must handle target loss: suspend, abort before commit, or retain detached state according to its own contract. A stream unload is not intentional destruction, and an actor's EndPlay callback is not proof that its battery, liquid, part, or evidence record should be deleted.

## 23.6 Five required integration scenarios

| Scenario | Composition | Required failure exercise |
|---|---|---|
| Machine room | Power provider → pump availability → Fluid state → Puzzle condition | Power loss during transfer preserves volumes and puzzle state; provider restore does not duplicate transfer |
| Optical mechanism | Interaction adapter → mirror controls → Optical receiver → Puzzle input | Mirror unload/late trace cannot leave a phantom permanent receiver contribution |
| Investigation | Photography record → Evidence observation → conclusion → optional Codex/objective effect | Image write failure and repeated conclusion intent cannot grant duplicate progress |
| Timed activity | RaceTiming or Rhythm result → optional result store → Ghost reference | Recording failure does not corrupt a valid result; incompatible content/assistance is not compared as equal |
| Maintenance terminal | Terminal command → Assembly operation → optional inventory part transfer | Session closes during commit; final part ownership and receipt remain correct without unsafe callbacks |

These are generic fixture scenarios, not a game-specific integration mandate. Each base must already pass in isolation before these scenarios are used as proof of bridge behavior.

## 23.7 Cross-system acceptance requirements

| ID | Required integration observation |
|---|---|
| CROSS21-01 | Power-to-pump bridge uses declared units/clock lag and cannot create fluid on supply loss |
| CROSS21-02 | Mechanical demand/power coupling has bounded updates and detects unsupported feedback loops |
| CROSS21-03 | Optical-to-puzzle bridge replaces/removes source contributions atomically |
| CROSS21-04 | Reaction/painting presentation claims coexist without mutating shared source material assets |
| CROSS21-05 | Queue-to-movement bridge ignores late arrivals after reservation expiry |
| CROSS21-06 | Queue service completion remains distinct from animation/movement completion |
| CROSS21-07 | Broadcast and Acoustic Spaces compose volume/filter policies without double application |
| CROSS21-08 | Photography-to-evidence accepts only committed records with stated image/evaluation quality |
| CROSS21-09 | Conclusion/result rewards use receipts and survive retry without duplicate mutation |
| CROSS21-10 | Terminal command closure cannot cancel another owner's newer device/control claim |
| CROSS21-11 | Assembly/inventory save snapshots preserve one-location-per-part and recover in-doubt operations |
| CROSS21-12 | Race/Rhythm results retain course/chart/profile identity when connected to Ghost playback |
| CROSS21-13 | Ghost/native replay cannot send ordinary live reward, save, or objective effects |
| CROSS21-14 | Mod activation respects definition pins and fails without partial cross-provider catalogs |
| CROSS21-15 | Missing mod content is reported before destructive save migration or overwrite |
| CROSS21-16 | Playtest providers redact before retention and cannot become dependencies of observed bases |
| CROSS21-17 | Simultaneous terminal/photo/rhythm/replay/inspection sessions arbitrate shared player-control leases |
| CROSS21-18 | Travel/restore across integrated features invalidates old epochs and releases only owned resources |
| CROSS21-19 | A bridge's absence never changes the installation/build behavior of either base |
| CROSS21-20 | End-to-end cooked fixtures verify actual assets/backends, not only mocked event exchange |

---

# 24. Recommended Development Order — Phases 9–13

This order extends the earlier Phase 1–8 numbering. It is a new recommended plan, not a claim that earlier phases have been completed. It preserves the candidate recommendation to start with Puzzle, Service Queues, Power, Evidence, and Rhythm, with a small Playtest Recorder alongside them.

## 24.1 Milestone 9.0 — Compatibility and verification foundation

Audit repository instructions, actual implementations, shared Core contracts, engine/toolchain, source-control state, and test capabilities. Create isolated host staging and an evidence manifest before building a new feature. Adopt only the missing shared contracts required by the next slice; do not rebuild the whole suite to start.

**Exit:** Actual compatibility ledger, known source/engine identity, Core-only consumer check where available, first plugin test manifest, and a concrete native verification blocker when applicable. Documentation-only checks remain documentation-only evidence.

## 24.2 Phase 9 — First reusable gameplay batch

| Milestone | Work | Exit gate |
|---|---|---|
| 9.1 | `DocPuzzleMechanisms` SLICE then BASE | Ordered/simultaneous/timed behavior, reset/restore, Blueprint and C++ consumers; PUZ requirements |
| 9.D | `DocPlaytestRecorder` minimal development SLICE alongside 9.1 | Opt-in bounded events, marker, redaction, local export; explicitly not full DBG completion |
| 9.2 | `DocServiceQueues` | Multi-station/group admission with timeout/closure tests; QUE requirements |
| 9.3 | `DocPowerNetworks` | Allocation, batteries, breaker behavior, energy ledger; PWR requirements |
| 9.4 | `DocEvidenceDeduction` | Headless evidence/conclusion rules and receipt-aware effects; EVD requirements |
| 9.5 | `DocRhythmChallenges` | Fake-clock logic plus real audible single-lane challenge, calibration, pause/restart; RHY requirements |

Do not block gameplay implementation on a polished diagnostic viewer. The recorder slice is useful tooling, not a new dependency for every feature. Complete the full recorder base later.

## 24.3 Phase 10 — Spatial and surface mechanics

| Milestone | Work | Exit gate |
|---|---|---|
| 10.1 | `DocOpticalBeams` | Bounded reflection/filter paths and receiver ownership; OPT requirements |
| 10.2 | `DocAcousticSpaces` | Logical path solver plus measured single-listener emitter adapter; ACO requirements |
| 10.3 | `DocSurfacePainting` | Compatible mesh mapping, canonical masks, cooked material, persistence; PNT requirements |
| 10.4 | `DocMaterialReactions` | Finite state resources, competing reactions, bounded spread; MAT requirements |

Graphics/audio feasibility probes occur before extensive authoring tooling. A scene that cannot validate its mesh mapping or audio backend must not be hidden behind a completed logical API badge.

## 24.4 Phase 11 — Player activities and media

| Milestone | Work | Exit gate |
|---|---|---|
| 11.1 | `DocPhotography` | Actual capture pipeline, coherent metadata, storage failure tests; PHO requirements |
| 11.2 | `DocBroadcastChannels` | Bundled seekable audio, schedule/interruptions, late receivers; BRC requirements |
| 11.3 | `DocWorldTerminals` | Safe virtual filesystem, typed commands, sessions, reference UI; TRM requirements |
| 11.4 | `DocGestureRecognition` | Bounded recognition/rejection and accessible alternative; GES requirements |

Run the investigation and maintenance-terminal bridge probes only after the contributing bases pass independently. Do not create a mandatory combined narrative/media framework.

## 24.5 Phase 12 — Connected machinery and procedures

| Milestone | Work | Exit gate |
|---|---|---|
| 12.1 | `DocFluidNetworks` | Concurrent transfer conservation, controls, explicit source/sink ledger; FLU requirements |
| 12.2 | `DocMechanicalNetworks` | Ratios, reflected loads, stalls, rejected unsupported topology; MEC requirements |
| 12.3 | `DocAssemblyMaintenance` | Part uniqueness, prerequisites, transaction boundaries, diagnostics; ASM requirements |

First prove standalone numerical fixtures. Then add explicitly lagged Power/Fluid/Mechanical bridges and a limited inventory transfer adapter. Do not expand into physical simulation merely because a visual test looks unrealistic.

## 24.6 Phase 13 — Recording, results, extensibility, and release

| Milestone | Work | Exit gate |
|---|---|---|
| 13.1 | `DocReplayGhosts` | Local transform tracks, isolated proxies, seek/storage validation; GHO requirements |
| 13.2 | `DocRaceTiming` | Swept gates, laps, penalties, compatible results; RAC requirements |
| 13.3 | `DocModContent` | Strict data-only manifests, transactional providers, pins/missing content; MOD requirements |
| 13.4 | `DocPlaytestRecorder` full BASE | All providers/export/resource/privacy contracts; DBG requirements |
| 13.5 | Selected integration profile | Named bridge requirements and CROSS21 tests with cooked examples |
| 13.6 | Expansion release candidate | Isolated/second-host tests, Win64 Development/Shipping evidence, docs and compatibility manifest |

Do not call Phase 13.6 complete because all plugin descriptors exist. Every advertised base and selected bridge must meet Section 37. Deferred backends remain visibly deferred.

## 24.7 Per-module implementation ladder

For every module, use this sequence: data model and validation → pure evaluator/commands → real engine integration slice → Blueprint/C++ consumers → cancellation/teardown/restore → authoring/example → isolated cook/launch → selected bridges → performance/security regression → release evidence.

Native feasibility failures may change implementation details through an ADR, but cannot silently remove a required base capability. State the blocker and bounded alternative explicitly.

---

# 25. Dependency Matrix and Bridge Catalog

## 25.1 Native dependencies are candidates until compiled

Every row requires Core plus the smallest actual native dependency set. The following is an integration surface guide, **not copy-paste `.Build.cs` code**. Public reflected types, private implementation includes, and `.uplugin` dependencies must all be verified against installed 5.8.3.

| Base | Essential native surface | Optional participants, never base imports |
|---|---|---|
| Puzzle | Reflection, components, timing, Gameplay Tags | Interaction, Events, Optical, Power, Save, Quest |
| Power | Components, numerics, graph/state operations | Fluid, Mechanical, Time, Save, device plugins |
| Fluid | Components, numerics, bounded updates | Power, Materials, Save, Events |
| Mechanical | Components, transforms, numerics | Power, Chaos-specific adapters, animation, Assembly |
| Material Reactions | Components, spatial queries, timers | Weather, Fluid, Surface Feedback, Niagara, Save |
| Service Queues | Components, timers, registered providers | AI/navigation, Smart Objects, NPC Schedules, UI |
| Acoustic Spaces | Spatial queries and native audio component controls | Regions, Adaptive Audio, specialized spatializer |
| Optical Beams | Collision traces and generic mesh/material presentation | Puzzle, Power, Niagara, damage/security frameworks |
| Surface Painting | Static mesh/mapping integration, textures/materials; RenderCore/RHI only if needed | Interaction, Inventory, Quest, Save |
| Evidence | Reflected data, indexes, localized display data | Codex, Dialogue, Photography, Quest, AI |
| Photography | Scene capture, render resources, image encode/storage capability | Camera frameworks, UI, Evidence, Quest, Save |
| Broadcast | Verified finite/seekable native audio playback | MediaAssets/backends, Quartz, Power, Time, Acoustics |
| Terminals | Data/permissions/command model, session coordination | UMG/CommonUI example, Power, external device controllers |
| Rhythm | Clock/input data and native audio playback reference | Quartz, Enhanced Input adapter, UI, results/rewards |
| Gesture | Numerics and bounded point data | Enhanced Input, UI, Puzzle/Terminal commands, AI |
| Assembly | Components, transforms, logical part registry | Inventory, Interaction, Power, Mechanical, animation |
| Ghost | Components, approved visual proxies, bounded recording bytes | Native Replay/streamer, Race, UI, Playtest |
| Race | Components, geometry, trusted timestamped samples | Movement/vehicle plugins, Map, Ghost, UI, Save |
| Mod Content | File/JSON parsing, catalogs, hashes, safe path handling | Every feature importer; Game Features trusted bridge |
| Playtest | Bounded records, file/export support, hashing | Events, per-feature providers, image/log adapters, optional viewer |

A headless numerical evaluator may coexist with a native presentation path inside one base when that path is essential to its advertised behavior. Runtime graphics/audio initialization must still be gated by world/net mode. An optional engine plugin remains in a bridge even when a module is a frequent consumer.

## 25.2 Proposed first bridge set

| Bridge name | Participants | Single responsibility |
|---|---|---|
| `DocPuzzleInteraction` | Puzzle + Interaction | Translate authorized control use into source-owned puzzle inputs |
| `DocPuzzleOptical` | Puzzle + Optical | Map receiver contributions/state into puzzle inputs |
| `DocPowerFluid` | Power + Fluid | Map granted power into pump availability with declared clock lag |
| `DocPowerMechanical` | Power + Mechanical | Exchange demand/supply snapshots under bounded coupling policy |
| `DocMaterialWeather` | Material Reactions + Weather | Apply source-owned environmental exposure samples |
| `DocQueuesNPCSchedules` | Queues + NPC Schedules | Bind activity intent to queue membership without moving actors |
| `DocQueuesSmartObjects` | Queues + native Smart Objects | Translate reservations to validated object-use claims |
| `DocAcousticsBroadcast` | Acoustics + Broadcast | Apply portal transmission to registered receiver emitters |
| `DocPhotographyEvidence` | Photography + Evidence | Create observations from committed approved captures |
| `DocEvidenceKnowledge` | Evidence + KnowledgeCodex | Publish conclusion/discovery data with receipt-aware semantics |
| `DocTerminalAssembly` | Terminals + Assembly | Register authorized maintenance commands |
| `DocAssemblyInventory` | Assembly + InventoryItems | Coordinate one-location-per-part resource transfers |
| `DocRhythmQuartz` | Rhythm + native Quartz surface | Supply scheduled transport/clock alignment capability |
| `DocBroadcastMedia` | Broadcast + Media Framework/backend | Implement explicit video/stream source capabilities |
| `DocGhostNativeReplay` | Ghost + native Replay/streamer | Alternative recording backend with dedicated isolation policy |
| `DocRaceGhosts` | Race + Ghost | Associate compatible run/recording identities without moving authority |
| `DocModContentPuzzles` | Mod Content + Puzzle | Validate/import data-only puzzle definitions |
| `DocModContentRhythm` | Mod Content + Rhythm | Validate/import charts referencing approved audio assets |
| `DocModContentGameFeatures` | Mod Content + native Game Features | Activate trusted developer-built packages only |
| `DocPlaytestEvents` | Playtest + Events | Capture redacted selected event envelopes |

Names are proposed. Audit already published bridge names before creating duplicates. A selected bridge must be explicitly installed in the host's discoverable plugin tree. A repository `Bridges/` staging folder is not automatically an installed Unreal plugin directory.

## 25.3 Persistence, UI, and diagnostics bridges

Use one small feature-specific Save adapter for each actual persistence integration, rather than making a Save plugin import every feature. Examples: `DocSavePuzzles`, `DocSavePower`, `DocSaveEvidence`, `DocSaveAssembly`. The adapter translates feature-owned records; it does not reinterpret their gameplay meaning.

Likewise, UI bridges consume feature view models. A universal “all forty plugins UI” module is not a required dependency. Playtest providers register explicitly and may live in a selected bridge or host module; base features need no recorder import to emit their own ordinary delegates.

## 25.4 Bridge lifecycle checklist

Every bridge declares: required participant versions, provider registrations, owner/context mapping, units/clocks, commit/effect semantics, persistence relationship, absent-provider behavior, shutdown order, and tests. Unregister delegates/providers before dependent worlds or modules disappear. No bridge-owned timer may invoke an unloaded feature.

---

# 26. Editor Tooling and Generic Authoring

## 26.1 Shared entry point without a hard dependency hub

Extend the existing optional `Window → Doc Modular Systems` registration surface if it exists. Feature Editor modules register their own panels and unregister on shutdown. The Core editor must not import all twenty features to populate its menu. Missing panels show Not Installed when a dashboard supports that status.

Do not create empty Editor modules merely to satisfy a folder diagram. Base validation may begin with native asset validation, structured Details panels, and debug snapshots. Dedicated graph/timeline editors follow only where they materially improve authoring and are part of the selected profile.

## 26.2 Minimum tools by feature group

| Group | Minimum useful editor/debug surface |
|---|---|
| Puzzle and Evidence | Stable rule IDs, broken links, cycle path, evaluation reasons, snapshot revision |
| Power/Fluid/Mechanical | Ports/edges, units, topology revisions, ledger/load traces, unsupported-model warnings |
| Materials/Painting | Eligible channels/surfaces, current state, propagation/mask budgets, mapping/version checks |
| Queues | Tickets, capacity claims, deadlines, bypass counts, service state |
| Acoustics/Optics | Spaces/portals or beam segments, path reasons, budget/staleness, contribution ownership |
| Photography/Broadcast | Asset/backend readiness, frame/cursor identity, image/seek quality, resource use |
| Terminals/Gestures/Rhythm | Session/command or candidate/judgment diagnostics, permission/timing/invariance settings |
| Assembly | Part locations, prerequisite graph, resource receipts, visual reconciliation |
| Ghost/Race | Track/gate timeline, discontinuities, run validity, content compatibility |
| Mods/Playtest | Activation/export preview, schemas, unsafe inputs, omitted data, privacy and quotas |

## 26.3 Asset authoring rules

Create `.uasset` and `.umap` files only through valid Unreal authoring/generation workflows, then open/cook them. Never create text placeholders with binary extensions. Generated fixtures must be deterministic enough to recreate, identify their creator/version, and avoid external paid content.

Use generic primitives, original sample text, and properly licensed/generated media. Asset licenses/provenance belong in fixture documentation. Runtime plugin source must not contain a specific game's map names, story, machine paths, or proprietary asset references.

Editor repair actions need previews and undo/transaction support where appropriate. Duplicate IDs may be detected automatically, but changing an already persistent identity requires an explicit migration/repair decision. Do not regenerate all IDs on every construction script run.

---

# 27. Per-Plugin Documentation Requirements

Each implemented plugin must include the following documents with actual setup paths, tested behavior, and known limitations—not empty heading placeholders:

```text
README.md
SETUP.md
API_OVERVIEW.md
BLUEPRINT_USAGE.md
CPP_USAGE.md
DATA_MODEL.md
INTEGRATION.md
VALIDATION.md
TESTING.md
PERFORMANCE.md
DEBUGGING.md
CHANGELOG.md
```

Add `PERSISTENCE.md`, `NETWORKING.md`, `SECURITY_PRIVACY.md`, `ASSET_COMPATIBILITY.md`, or `EDITOR_TOOLS.md` where relevant. A Not Applicable entry must explain why; a feature with external files cannot omit security considerations simply because it is local-first.

## 27.1 Required README contents

State purpose, non-goals, capability profile, supported engine/platform evidence, dependencies, installation, minimum content/configuration, first working example, public API entry points, failure behavior, test commands, and known deferred features. Include what still needs a host provider.

“Blueprint-friendly” requires a documented node flow that has actually been executed. “Drop-in” requires a clean-host setup without plugin source edits. “Network-ready” is not an acceptable replacement for a list of tested net modes and limitations.

## 27.2 API documentation contract

For every public operation document context/owner, inputs and units, validation, authority, expected revision, duplicate behavior, output/result states, callback thread, timeout, cancellation/commit point, and lifecycle cleanup. Mark queries pure only when they truly have no side effects.

Document data versioning separately from runtime revisioning. List every persisted stable ID and every transient handle that must not be saved. Provide an example missing-provider result and a teardown example, not only the successful path.

## 27.3 Root documentation

Maintain `Docs/DEVELOPMENT_STATUS.md`, `Docs/DECISIONS.md`, `Docs/ENGINE_COMPATIBILITY.md`, `Docs/DEPENDENCY_MATRIX.md`, `Docs/REQUIREMENTS_TRACEABILITY_21_40.md`, `Docs/TEST_MATRIX.md`, `Docs/PERFORMANCE_BASELINE.md`, and `Docs/RELEASE_CHECKLIST.md`. Keep historical evidence in run directories; status links to it rather than copying unsupported claims forward.

---

# 28. Data Validation and Failure Injection

## 28.1 Validation layers

Validate at authoring time, at load/activation, and at the mutation boundary. Editor validation does not replace runtime checks for external data, network requests, restored saves, or dynamic graph edits. Runtime validation should return actionable typed failures without crashing a packaged game.

Common checks include stable-ID uniqueness, supported schema/version, references, finite numerics, range/overflow, cycle constraints, owner scope, maximum counts/depth, valid clocks, required capabilities, and cooking/mapping requirements. Feature-specific invariants remain owned by their features.

## 28.2 Fault matrix

| Fault | Required response |
|---|---|
| Missing optional provider | Feature remains installed; affected operation returns Unavailable/Unsupported |
| Missing required definition/asset | Reject or use a clearly configured fallback; no silent synchronous load workaround |
| Corrupt/oversized save, mod, ghost, photo, or diagnostic data | Reject before unsafe allocation/deserialization; preserve original valid data |
| World/owner destroyed during async work | Invalidate generation, cancel/finish internally, suppress unsafe callbacks |
| Duplicate request/effect | Same payload returns prior outcome where defined; conflicting payload rejects |
| Stale graph/catalog/state revision | Reject or recompute; never overwrite newer state with old worker result |
| File write/rename/full-disk failure | No false committed artifact; retain old valid state and report exact failure |
| Audio/graphics device unavailable | Logical tests may continue; actual playback/capture capability fails honestly |
| Budget exhaustion | Defer/reject/approximate under documented policy and record quality/staleness |
| Missing content after update | Quarantine/migrate under policy; no automatic destructive progress reset |

## 28.3 Generative and property-style tests

Use seeded generated fixtures for graph registration permutations, competing transfer capacities, extreme numeric ranges, malformed records, repeated cancellation, and interleaved owner actions. Record seeds and reduce failures to reproducible small fixtures.

Important properties: power/volume ledgers balance; part ownership is unique; reservations never exceed capacity; one note/input is judged once; one result commits once; one owner cannot release another owner's lease; a ghost cannot mutate live state; rejected mod activation leaves the old catalog unchanged.

Property tests supplement hand-authored scenarios and real engine fixtures. A pure Python or C++ arithmetic model does not prove the reflected Unreal integration obeys the same rules unless the tested production implementation is actually used.
---

# 29. Performance Requirements and Proposed Workloads

## 29.1 Measurement contract

The values below are **proposed reproducible test workloads, not measured performance claims**. Record CPU, GPU, RAM, OS, engine build, resolution, graphics/audio backend, enabled plugins, test seed, and build configuration. A useful optional graphics reference is an RTX 3060 12 GB-class machine; this is a proposed reference, not evidence of a benchmark or a required consumer GPU.

Measure game-thread time, worker time, GPU time where relevant, p50/p95/p99 latency, peak memory, allocations, queued work, drops/degradation, and teardown residue. Use Unreal Insights or equivalent native evidence where available. Separate warm steady-state from cold asset loading and first-use resource creation.

Before performance becomes a release gate, capture a baseline on the agreed reference machine and record approved budgets in `Docs/PERFORMANCE_BASELINE.md`. Do not invent a universal frame-rate guarantee for all forty plugins active simultaneously. Regression thresholds apply only to equivalent source/content/hardware/settings workloads.

## 29.2 Workload table

| Module | Normal verification workload | Stress/fault workload | Key measured result |
|---|---|---|---|
| Puzzle | 100 instances, 16 inputs each, burst of 1,000 accepted inputs | Oversized/cyclic graphs and repeated resets | Evaluation latency, bounded queues, no duplicated completions |
| Power | 1,000 nodes, 2,000 connectivity edges, 100 active storage nodes | Large topology edits and prolonged catch-up | Solver time, ledger error, topology rebuild cost |
| Fluid | 500 reservoirs, 1,000 directed links, 20 logical steps/second | Competing inflows/outflows, full tanks, cycles | Volume error, per-step time, backlog visibility |
| Mechanical | 100 independent drive trees of up to 20 nodes | Extreme ratios, load changes, invalid reconvergence | Solve time, phase error, clean rejection |
| Material Reactions | 1,000 registered objects, 100 actively reacting | Dense contact propagation and exhausted fuel | Neighbor/query cost, queue bounds, active-state count |
| Service Queues | 1,000 tickets across 20 stations | Group no-shows, closure bursts, competing offers | Reservation latency, fairness, capacity invariants |
| Acoustic Spaces | 100 spaces, 200 portals, 64 audible candidates | Rapid door changes and disconnected graphs | Path cache cost, update staleness, audible parameter stability |
| Optical Beams | 64 emitters, maximum 16 reflected segments each | Mirror loops and rapidly moving surfaces | Trace/solve cost, segment cap, receiver freshness |
| Surface Painting | 32 surfaces at 256×256 canonical mask resolution, four active brushes | Resolution/history/upload cap pressure | CPU/GPU memory, dirty upload time, canonical/render lag |
| Evidence | 2,000 observations and 500 dependent hypotheses | Retraction bursts, deep invalid rules | Incremental reevaluation time, explanation size |
| Photography | 1920×1080 captures, at most two in flight, 100 candidate subjects | Rapid requests, moving subjects, failed writes | Capture/readback/encode latency, peak memory, frame coherence |
| Broadcast | Eight channels, 32 receivers, bounded interruption queues | Missing sources, retunes, clock jumps | Active voices/decoders, cursor drift, readiness latency |
| Terminals | 20 devices, 5,000 total virtual files, two active users | Permission-filtered search and provider failure | Query latency, session cleanup, bounded output |
| Rhythm | Ten-minute single-lane chart, 5,000 authored notes | Injected frame hitches, device loss, restart spam | Judgment error/quality, allocations, callback freshness |
| Gesture | 100 templates, 64 normalized points each | Maximum input length and ambiguous noise | Recognition latency, rejection rate on fixed fixtures |
| Assembly | 100 assemblies, 30 parts each, two concurrent manipulation sessions | Conflicting replace operations and visual failures | Transaction latency, uniqueness, resource reconciliation |
| Ghost | Ten tracks at 30 observed samples/second for ten minutes | Random seeking, gaps, truncated chunks | File size, seek latency, memory, no live effects |
| Race | 32 participants, 100 gates, 60 trusted samples/second | High-speed multi-gate segments and teleports | Crossing cost, timing quality, validity correctness |
| Mod Content | 100 packs, 10,000 total definitions within fixed byte quotas | Cycles, malformed JSON, changed bytes, missing providers | Validation/activation latency and bounded memory |
| Playtest | 1,000 small events/second, 60-second target window, 32 MiB byte cap | Oversized events, pinned markers, simultaneous exports | Drop accounting, overhead, redaction, strict cap enforcement |

These loads are independent fixtures; they do not imply that their simultaneous sum is a supported game configuration. Stress success means bounded documented behavior, not necessarily maintaining the normal workload's quality or throughput.

## 29.3 Resource-specific rules

No full-world scans each frame. Register participants and maintain indexes. Batch topology changes, texture uploads, traces, and state notifications. Static objects should not tick merely to confirm they remain static.

Render-target and mask memory accounting must use actual format, dimensions, mip levels, staging/readback copies, and concurrent requests. A CPU mask, GPU texture, and readback buffer are separate allocations. Audio budgets count voices/decoders and pending source loads, not just receiver components.

File-backed systems need byte quotas, temporary-file quotas, and bounded parallel writes. A bounded ring buffer with unlimited pinned export segments is not bounded. Metrics must expose backpressure, omitted data, and failed allocations.

Cold loads may be asynchronous but still have memory and latency costs. Do not label a feature “zero hitch” because work was dispatched to a worker while the game thread later waits for it.

---

# 30. Networking and Authority Profiles

## 30.1 Base architecture versus verified networking

Every mutation API must be authority-aware, but real multiplayer support is a **separate selected and tested profile**. No default assumption that these subsystems replicate themselves. Use actual actor/component transport owned appropriately for RPC routing, or a host transport adapter. Unreal documents component replication through a configured replicated owner; verify both actor and component setup. [R15]

Do not expose a Server RPC on an ordinary subsystem and call the implementation networked. The transport resolves the authenticated owner context; it does not trust a client-provided profile ID, transform, score, result, file path, or object identity without validation.

## 30.2 State classification

| State | Recommended authority/presentation |
|---|---|
| Shared puzzle inputs/completion | Server authoritative; clients receive permitted progress, not hidden answers |
| Power/fluid/mechanical/material facts | Server authoritative logical state; clients interpolate allowed visuals |
| Queue tickets/capacity | Server authoritative; owner-specific offer data and filtered public queue views |
| Acoustic transmission | Usually local presentation from trusted world portal state |
| Optical receiver activation | Server authoritative; client beam rendering is cosmetic |
| Shared painting | Server validates strokes; client previews reconcile; local-only mode is explicit |
| Evidence/conclusions | Owner/campaign authority chosen by host; private facts filtered before replication |
| Photography | Usually local media; shared rewards require trusted evaluation policy |
| Broadcast | Authoritative logical schedule anchor; local playback under measured clock/seek tolerance |
| Terminal commands/device state | Server authority for shared devices; local focus/read presentation |
| Rhythm/gesture | Local input processing; competitive result transport is deferred unless explicitly implemented |
| Assembly/part transfers | Server authority and coherent resource transactions |
| Ghost viewing | Local visual playback; imported recordings remain untrusted data |
| Race runs/results | Server/trusted position/timer authority for shared records |
| Mod catalogs | Agreed trusted catalog identity for networked content; distribution not supplied by base |
| Playtest evidence | Local opt-in diagnostics; never automatically replicated/uploaded |

For each actual field, mark Authoritative, Replicated, Local, Persistent, and/or Transient as applicable. “Multiplayer aware” is not a field classification.

## 30.3 Required selected-network tests

A selected profile must test dedicated server plus at least two clients, unauthorized owner requests, late join, disconnect/reconnect policy, duplicate/out-of-order messages, stale world epochs, rate limits, and snapshot/delta reconciliation. Include network latency/loss where it matters.

Gameplay-critical facts cannot depend on a client's audible timing, GPU image, particle collision, ghost motion, or hidden UI flag. A dedicated server must not create render targets/audio playback to calculate a logical battery or queue state.

Large data such as paintings, photographs, ghosts, and mod packs needs explicit chunking/quotas/protocol design before networking is advertised. Do not send unbounded byte arrays through one convenient RPC.

---

# 31. Test Strategy, Fixtures, and Traceability

## 31.1 Test layers

Use pure algorithm/unit tests, reflected API/Blueprint tests, world lifecycle tests, functional map tests, cooked runtime checks, actual rendered/audible checks, and selected multiplayer tests. Unreal's automation facilities support different testing scopes; choose the layer that exercises the capability being claimed. [R11]

Mock providers are useful for deterministic failures and isolation. They cannot replace every real engine backend test. A fake audio clock, fake image, fake network transport, or mocked disk success does not verify its real counterpart.

## 31.2 Required installation combinations

At minimum stage: Core only; Core plus each of the twenty bases individually; each selected bridge with its real participants; the same bases with the bridge physically absent; and the selected combined profile. That is **twenty independent base-host checks plus Core**, not a single full-suite build with optional plugins disabled.

Use disposable generated hosts/output directories. Do not delete plugins from the user's source project to perform isolation. A host manifest records exactly which plugin trees were copied and their hashes. A stub sibling plugin cannot satisfy an absence check.

A second structurally different host demonstrates portability without editing runtime library source. Relevant differences include ordinary actor versus Character participant, no custom GameInstance versus an existing one, alternate UI/input presentation, and independent owner scopes. Do not claim portability from two copies of the same sample map with different names.

## 31.3 Fixture conventions

Use test names specified in each module's acceptance table. The catalog contains ten baseline tests per module; these are minimum grouped requirements, not a ceiling on test cases. Add named subcases and broader failure tests as implementation expands. Do not rename baseline tests merely to make an old report look current.

Recommended fixture assets live in separately installed example/test content. Numeric fixtures use checked-in text/struct inputs with schema versions. Old-save/recording fixtures remain immutable so migrations are tested against genuine older formats. Corrupt copies belong in temporary test directories.

Each test sets up its own state, cleans its own resources, and restores any altered engine/host setting. Run order cannot determine success. Tests that require graphics/audio clearly declare those requirements and skip with a concrete reason when unavailable; they do not pass by omitting the assertion.

## 31.4 Traceability record

For every requirement ID maintain:

```text
RequirementId
CapabilityProfile
ImplementationPaths
PublicEntryPoints
TestNamesAndSubcases
FixtureIdsAndVersions
VerificationLevel
SourceAndContentFingerprint
EvidenceRunId
Status
KnownLimitations
```

Normative prose within a requirement's scope must map to actual assertions; a test name alone is not coverage. Split a broad baseline requirement into explicit subcases where necessary. Trace every selected bridge and every promised asset/backend path. An optional feature remains Not Selected or Deferred until deliberately included, never silently Passed.

## 31.5 Result validation

Parse the automation report, not only the process exit code. Confirm expected tests were discovered and executed, none failed, required assertions ran, and the report belongs to the current source/fixture/engine. A crash, timeout, incomplete report, or empty test list is failed/blocked verification.

Keep packaged smoke checks separate from editor automation. A build-only operation cannot prove a UI opened, a beam rendered, a photograph was encoded, or a channel played at the intended offset. Store screenshots/audio/timing traces only when relevant and with their capture limitations.

---

# 32. Build, Test, Cook, and Packaging Templates

## 32.1 Targets and native verification requirements

Each selected base/profile must build `Development Editor Win64`, build/cook/launch `Development Win64`, and build/cook/launch `Shipping Win64`. Development automation need not be compiled into Shipping; Shipping still needs a real smoke scenario proving initialization, content availability, and required input/render/audio behavior. A networked profile adds its verified server/client targets.

Validate Runtime modules contain no Editor-only dependency; required assets/configuration are staged; soft references resolve after cook; optional examples are not accidentally mandatory; and product/sample licenses are included. Unreal's packaging pipeline separates build, cook, stage, package, deploy, and run, with BuildCookRun as an automation entry point. [R14]

## 32.2 Parameterized PowerShell preflight template

The following is a **template to adapt and verify in the actual workspace**, not a script supplied or executed by this document. Use actual target names from `.Target.cs` and actual tool paths. Do not copy example target names into an unrelated project.

```powershell
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [Parameter(Mandatory = $true)][string]$EditorTarget,
    [Parameter(Mandatory = $true)][string]$GameTarget,
    [Parameter(Mandatory = $true)][string]$EvidenceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
if ([IO.Path]::GetExtension($ProjectFile) -ne '.uproject') {
    throw 'ProjectFile must identify an existing .uproject.'
}
$VersionPath = Join-Path $EngineRoot 'Engine/Build/Build.version'
$Version = Get-Content -LiteralPath $VersionPath -Raw | ConvertFrom-Json
if ($Version.MajorVersion -ne 5 -or
    $Version.MinorVersion -ne 8 -or
    $Version.PatchVersion -ne 3) {
    throw 'Requested target is Unreal Engine 5.8.3; record the mismatch.'
}
$Build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$UAT = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
foreach ($Tool in @($Build, $UAT, $EditorCmd)) {
    if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) {
        throw "Missing required tool: $Tool"
    }
}
$RunId = (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '_' +
    [Guid]::NewGuid().ToString('N').Substring(0, 8)
$RunDir = Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) $RunId
New-Item -ItemType Directory -Path $RunDir -ErrorAction Stop | Out-Null
$Version | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $RunDir 'engine.json') -Encoding UTF8
```

A production wrapper also records exact source/worktree/configuration fingerprints, selected plugins, OS/toolchain, and content. It enforces a configurable timeout, captures stdout/stderr, and cleans only the process tree it created. Do not use global `taskkill` against every Unreal process or delete arbitrary Intermediate/Saved folders.

## 32.3 Build templates

```powershell
& $Build $EditorTarget Win64 Development "-Project=$ProjectFile" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }

& $Build $GameTarget Win64 Development "-Project=$ProjectFile" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Game build failed: $LASTEXITCODE" }
```

Capture commands and logs in the production wrapper. Avoid Live Coding as the sole release build evidence. Shared-Core public API changes require rebuilding affected independent consumers, not just the currently open editor target.

## 32.4 Automation template

Epic documents automation selection through ExecCmds and report export paths. Validate the exact installed command/exit behavior before standardizing a wrapper; it must wait for test completion rather than terminate early. [R12]

```powershell
$TestPrefix = 'Doc.Puzzle'
$ReportDir = Join-Path $RunDir 'automation'
$LogPath = Join-Path $RunDir 'automation.log'
$Arguments = @(
    $ProjectFile,
    '-unattended',
    '-nop4',
    '-nosplash',
    "-ExecCmds=Automation RunTest $TestPrefix",
    '-TestExit=Automation Test Queue Empty',
    "-ReportExportPath=$ReportDir",
    "-abslog=$LogPath"
)
# Add -NullRHI / -nosound ONLY for explicitly logical tests that support them.
& $EditorCmd @Arguments
$ExitCode = $LASTEXITCODE
if ($ExitCode -ne 0) { throw "Automation process failed: $ExitCode" }
# Required next step: parse the actual report and compare discovered/executed
# tests against the selected requirement manifest. Exit code zero is not enough.
```

`-TestExit` behavior and automation command aliases are local verification items, not a guarantee from this template. A wrapper that cannot reliably detect queue completion must be fixed before it is used as release evidence. Do not append an immediate Quit command that can bypass pending async tests in the selected environment.

## 32.5 Cook/package template

```powershell
$ArchiveDir = Join-Path $RunDir 'package-development'
& $UAT BuildCookRun `
    "-project=$ProjectFile" `
    "-target=$GameTarget" `
    -noP4 -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -package -archive `
    "-archivedirectory=$ArchiveDir" -utf8output
if ($LASTEXITCODE -ne 0) { throw "Packaging failed: $LASTEXITCODE" }
```

Use the host's actual map/cook list and packaging policy; add IoStore or other flags only when supported/configured for that host. Repeat for Shipping with a separate archive/evidence path. Do not infer packaged launch success from UAT finishing. Discover the produced executable and launch the selected real smoke fixture with a bounded timeout and explicit result signal.

Standalone plugin distribution may use a verified BuildPlugin workflow, but Core/sibling bridge dependencies need a real discoverable host arrangement. A successful plugin package alone does not replace a consumer project's cooked validation.

## 32.6 Packaged smoke evidence

Each smoke fixture must enter/operate/exit the feature and emit a machine-readable completion record with required assertions. Record actual process exit, logs, build identity, and artifacts. Rendering/audio fixtures require the appropriate device/backend and cannot substitute headless success.

The smoke record itself is not sufficient if it is unconditional. Inspect its assertions: e.g., image blob exists and decodes, receiver joins at measured cursor tolerance, beam reaches the intended receiver, ghost cannot affect a collectible, or mod activation leaves the expected catalog. Preserve failure logs instead of overwriting them on retry.

---

# 33. Repository and Distribution Structure

```text
Repository/
├─ Plugins/
│  └─ DocModular/                    # Grouping directory; no .uplugin here
│     ├─ DocModularCore/
│     ├─ <existing modules 1–20>/
│     ├─ DocPuzzleMechanisms/
│     ├─ DocPowerNetworks/
│     ├─ DocFluidNetworks/
│     ├─ DocMechanicalNetworks/
│     ├─ DocMaterialReactions/
│     ├─ DocServiceQueues/
│     ├─ DocAcousticSpaces/
│     ├─ DocOpticalBeams/
│     ├─ DocSurfacePainting/
│     ├─ DocEvidenceDeduction/
│     ├─ DocPhotography/
│     ├─ DocBroadcastChannels/
│     ├─ DocWorldTerminals/
│     ├─ DocRhythmChallenges/
│     ├─ DocGestureRecognition/
│     ├─ DocAssemblyMaintenance/
│     ├─ DocReplayGhosts/
│     ├─ DocRaceTiming/
│     ├─ DocModContent/
│     └─ DocPlaytestRecorder/
├─ Bridges/                         # Source staging; install selected bridges in host Plugins/
├─ Samples/                         # Generic reference hosts/content
├─ Tests/
│  ├─ Fixtures/
│  ├─ Compatibility/
│  └─ Manifests/
├─ Scripts/                         # Actual tested wrappers, not assumed to exist
├─ Docs/
└─ Artifacts/                       # Generated local output; ignore as appropriate
```

Per-plugin structure:

```text
DocFeature/
  DocFeature.uplugin
  Source/
    DocFeatureRuntime/
      DocFeatureRuntime.Build.cs
      Public/
      Private/
        Tests/                      # Appropriate development-only tests
    DocFeatureEditor/               # Only when real editor functionality exists
  Config/
  Content/                          # Only genuinely required base assets
  Resources/
  Docs/
```

Examples/tests must not become mandatory asset dependencies of a production base. Distribute manifests identifying source, binaries by engine/platform/configuration, required Core version, selected bridges, runtime content, optional examples, licenses, and checksums. Never include user-specific absolute paths, credentials, caches, or arbitrary diagnostic exports in a release ZIP.

---

# 34. Development Status and IDE Continuation

## 34.1 Repository state is authoritative

Maintain `Docs/DEVELOPMENT_STATUS.md` after each meaningful work slice. This specification does not establish which modules currently exist. Begin with Not Audited/Unknown until actual source inspection resolves it. Preserve historical evidence but mark it stale when source, engine, configuration, content, or providers change.

Record blockers precisely. “Unreal missing” is different from “compiler missing,” “selected test not discovered,” “GPU unavailable,” or “API does not support required seeking.” Do not hide a failed foundation gate by starting unrelated plugins.

## 34.2 Status template

```markdown
# Development Status — Modular Systems 21–40

## Workspace identity
- Updated: <timestamp and timezone>
- Branch / commit: <actual values>
- Worktree: <clean or relevant changed files/fingerprint>
- Engine: <version/build identity and local configuration reference>
- Toolchain / OS: <actual compiler, SDK, OS>
- Enabled profile: <base, selected bridges, graphics/audio/network modes>
- Current milestone: <one bounded milestone>

## Verified this slice
<Requirement IDs, implementation paths, executed tests, evidence run IDs>

## Implemented but not verified
<Exact code/asset work and the missing verification level>

## Evidence
| Run | Command/profile | Discovered | Executed | Passed | Failed | Skipped | Exit | Logs |
|---|---|---:|---:|---:|---:|---:|---:|---|
| <actual> | <actual> | <actual> | <actual> | <actual> | <actual> | <actual> | <actual> | <path> |

## Blocked / not run
<Concrete cause, affected requirement IDs, smallest next verification action>

## Compatibility changes
<Public API, Core contract, save/schema/content changes and ADR links>

## Known limitations
<Unsupported backends/topologies/asset types and non-verified claims>

## Next bounded task
<Objective, files/contracts, required fixture/tests, exit/stop condition>
```

The template's placeholders are documentation examples. Do not leave them in a status report that claims completed implementation. Unknown values stay explicitly Unknown/Not Run rather than becoming guessed counts.

## 34.3 Copy-ready IDE continuation prompt

```text
Use UE5_8_3_Modular_Gameplay_Systems_Modules_21_40_IDE_Handoff_v1.md
as the implementation specification for modules 21–40 of the project-agnostic
Unreal Engine 5.8.3 modular gameplay suite.

First read repository instructions, Docs/DEVELOPMENT_STATUS.md, the actual
source, the companion Revision 2 Core contracts, and this handoff's Sections
0–2, 23–25, and 34–37. Audit branch/worktree and engine/build/toolchain identity.
Preserve existing working code, user changes, licenses, APIs, and saved data.

Do not assume earlier modules are complete or absent. Resolve only the minimal
shared compatibility gap needed for the next task. Do not create a second Core.
Keep each base independent of siblings; put cross-feature and optional native
integrations in explicit bridges with present/absent tests.

Choose the smallest incomplete milestone in Section 24. Implement one real
vertical slice including validation, public Blueprint/C++ access, ownership,
commit/cancellation semantics, teardown, and tests. A slice is not base completion.
Use installed 5.8.3 headers and successful builds for exact API/module decisions.
Do not invent Unreal APIs, placeholder binary assets, test results, or compatibility.

Run the relevant logical, native, functional, cooked, rendered/audible, and selected
network checks that the environment supports. Parse test reports and verify actual
expected test discovery/execution. Record exact commands, exit codes, fixture and
source fingerprints, logs, and artifact paths. Missing/skipped tests are not passes.

When native validation is blocked, make bounded useful progress and record the
precise missing verification. Do not downgrade the engine, hide a failure, expand
scope into a replacement game framework, or repeatedly ask for information already
available in the workspace.

Update development status, traceability, decisions, and the next bounded task.
Do not publish, push, install paid content, upload diagnostics, or alter global
settings without the authorization applicable to this implementation session.
```

---

# 35. Initial Module Status Matrix

This table is an audit template, not a statement that no code exists. Replace Not Audited only with facts established from the actual workspace.

| Module | Source audit | Base runtime | Blueprint/C++ | Editor/example | Tests | Cooked Win64 | Selected bridges |
|---|---|---|---|---|---|---|---|
| DocPuzzleMechanisms | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocPowerNetworks | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocFluidNetworks | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocMechanicalNetworks | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocMaterialReactions | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocServiceQueues | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocAcousticSpaces | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocOpticalBeams | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocSurfacePainting | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocEvidenceDeduction | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocPhotography | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocBroadcastChannels | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocWorldTerminals | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocRhythmChallenges | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocGestureRecognition | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocAssemblyMaintenance | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocReplayGhosts | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocRaceTiming | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocModContent | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |
| DocPlaytestRecorder | Not Audited | Unknown | Unknown | Unknown | Not Run | Not Run | Not Selected |

---

# 36. Immediate IDE Tasks

## Task 0 — Audit before changing code

Read actual repository instructions and status. Identify existing modules, Core public contracts, tests, target files, engine identity, compiler/SDK, fixture assets, and dirty user changes. Create a compatibility ledger using Appendix D. Record exact blockers and do not assume every plugin needs to be scaffolded.

**Exit:** A source-grounded next task and evidence plan, not a fabricated completion percentage.

## Task 1 — Establish the first isolated host

Stage a disposable host containing the real Core and `DocPuzzleMechanisms` only. No sibling stubs. Confirm plugin/module descriptors, native tags, public API export, and supported world filters. Use the actual engine toolchain to build when available.

**Exit:** Core-only and Core-plus-Puzzle consumer builds, or a precise blocked native gate with bounded source work identified.

## Task 2 — Implement puzzle IDs, definitions, and input validation

Add stable definition/rule/input/instance/attempt IDs, immutable definitions, typed inputs, request sequence deduplication, and expected-revision checks. Implement one ordered rule using the production evaluator, not a test-only replacement.

**Exit:** `Doc.Puzzle.OrderedInputs`, `Doc.Puzzle.DuplicateInput`, and invalid input subcases execute against production code.

## Task 3 — Complete reset, timed input, and simultaneous contributors

Add rule composition, contributor-owned level state, deadline comparison, reset epochs, and one terminal completion. Test callback reentrancy and late old-attempt events. Keep reward effects outside the puzzle's internal state commit.

**Exit:** Relevant PUZ requirements and cancellation/teardown tests pass with actual discovered/executed counts.

## Task 4 — Add real public consumers and example assets

Create a Blueprint-only puzzle component/example and an independent C++ consumer. Author/generate valid Unreal assets and a small map with generic controls. Operate through direct APIs without DocInteraction installed.

**Exit:** Both consumers work; example assets open and cook; no source-specific external content path.

## Task 5 — Implement puzzle snapshots and restore

Capture versioned detached state, apply under a restore barrier, rebuild live input contributors, and suppress historical completion effects. Test a save/content mismatch and repeated restore. A storage backend is optional; do not claim disk durability from an in-memory snapshot test.

**Exit:** `Doc.Puzzle.RestoreNoEffects` and shared restore/identity gates have evidence.

## Task 6 — Add the optional recorder development slice

Implement a minimal opt-in recorder/provider surface with bounded event bytes, issue marker, redaction before retention, and a local export manifest. It must be removable without changing the Puzzle base. Do not spend this slice building a polished viewer or every module provider.

**Exit:** Selected early DBG assertions pass; full recorder status remains In Progress until Phase 13.4.

## Task 7 — Run cooked verification and update status

Build/run the isolated Win64 example, inspect actual automation reports, and record graphics/input behavior where applicable. Update status, traceability, compatibility, and known limitations. Preserve failed run logs.

**Exit:** The next bounded step is either a concrete failed gate fix or Phase 9.2 Service Queues. Do not advance based solely on scaffolding or documentation lint.

---

# 37. Completion Definition and Release Gates

A module is complete only for a named capability profile. The full base expansion requires all twenty base profiles; selected bridges and future features have separate status.

| ID | Release requirement |
|---|---|
| REL21-01 | Exact requested engine/build target is recorded; actual public Runtime code compiles and independent C++/Blueprint consumers work |
| REL21-02 | Core-plus-base isolation and second-host portability pass with siblings physically absent and no plugin source edits |
| REL21-03 | All module base acceptance requirements and applicable shared invariants have fresh behavior evidence, not test-name placeholders |
| REL21-04 | Definitions, ownership, identity, clocks, revisions, numeric/input limits, and async cleanup are implemented and documented |
| REL21-05 | Persistable records are versioned; restore/migration/fault tests preserve state and prevent duplicate effects or destructive silent recovery |
| REL21-06 | Required base examples/assets exist, validate, and load in cooked Win64; Runtime code has no Editor-only dependency |
| REL21-07 | Development and Shipping packages build/cook/launch; selected rendered/audible/input capabilities have actual device/backend evidence |
| REL21-08 | Selected bridges meet lifecycle/absence/failure tests and applicable CROSS21 requirements; unselected work is not reported as complete |
| REL21-09 | Advertised networking profiles have real authority/transport/late-join/failure evidence; local-only behavior is labeled accurately |
| REL21-10 | Performance workloads, limits, degradation policies, and approved regression budgets have measured evidence on identified hardware |
| REL21-11 | Security/privacy/accessibility requirements, licenses, setup/API/testing docs, and compatibility manifests are complete for the selected profile |
| REL21-12 | Release artifacts match recorded source/content fingerprints; known limitations and deferred capabilities are listed without inflated claims |

No test result can establish guarantees beyond its scope. A profile may be ready while another bridge is blocked, but the release statement must name that boundary. A crash-free demonstration is not proof of conservation, ownership, privacy, or authoritative correctness.

---

# 38. Final Design Principle

Build twenty useful, independently testable plugins that compose through stable contracts—not twenty partial frameworks that require the other nineteen to function.

Own one coherent problem per plugin. Make supported approximations explicit. Provide a real first-use example, a reliable failure path, and evidence for each advertised capability. Keep advanced simulation, unrestricted modding, full-world replay, and perfect media synchronization out of base-completion claims until separately implemented and verified.

The goal is reusable engine infrastructure that can move between unrelated projects with documented setup and minimal source changes—not project-specific logic extracted into folders with plugin descriptors.
---

# Appendix A. Candidate-to-Specification Scope Ledger

The preceding twenty-candidate response supplies the module selection and first-version direction. This ledger makes expansions and deferrals visible. It does not claim that any candidate's capability has already been built.

| Module | Preserved candidate intent | First base boundary and explicitly later work |
|---|---|---|
| 21 — Puzzle Mechanisms | Ordered/simultaneous/timed rules, weighted controls, symbols, alternatives, progress/reset | Validated acyclic rule composition; procedural generation/general scripting remain later |
| 22 — Power Networks | Generators, batteries, circuits, priorities, overload/disconnection/brownout | Gameplay energy allocation and explicit breaker policy; no full electrical engineering solver |
| 23 — Fluid Networks | Tanks, pipes, valves, pumps, finite transfer, leaks, flow limits | Single liquid with storage-node transfers; mixtures/real pressure/zero-volume routing later |
| 24 — Mechanical Networks | Motors, shafts, gears, belts, clutches, direction/speed, loads | Rooted acyclic kinematics and stalls first; slipping extension, closed loops, and physics coupling later |
| 25 — Material Reactions | Burning, extinguishing, melting, drying, later corrosion/dissolution | Authored state changes and bounded propagation; no universal chemistry/damage model |
| 26 — Service Queues | Multiple stations, groups, admission, no-shows, expiry, closures | Logical capacity/arrival/service contracts; movement and physical evacuation are providers |
| 27 — Acoustic Spaces | Connected rooms/doors influence audible sound transmission | Authored strongest-path approximation plus one-listener adapter; wave/multi-path physics later |
| 28 — Optical Beams | Emitters, mirrors, filters, channels, receivers, sustained activation | Reflection/filtering first; splitters/refraction/complex optics remain explicit follow-ons |
| 29 — Surface Painting | Paint/clean/scrape/reveal persistent surfaces and coverage | Compatible static meshes and one canonical mask layer; arbitrary meshes/multiple layers later |
| 30 — Evidence Deduction | Observations, testimony, provenance, hypotheses, contradictions, revised conclusions | Authored typed rules and explanation data; board UI optional, LLM not required |
| 31 — Photography | Image plus subject identity, framing, distance, angle, approximate visibility | Actual on-demand capture and geometric metrics; pixel recognition/AI scoring later |
| 32 — Broadcast Channels | Shared channel position, schedules, tuning, interruptions, radio/TV/PA uses | Bundled seekable audio first; video/network streams via explicitly tested backends |
| 33 — World Terminals | Fictional devices, files, messages/logs, applications, approved commands | Safe virtual data and typed commands; no real OS shell/network execution |
| 34 — Rhythm Challenges | Authored charts, taps/holds, windows, combos, calibration, pause/restart | Real one-lane audible challenge; multiple lanes/tempo-map expansion later |
| 35 — Gesture Recognition | Local drawn patterns with configurable invariance and rejection | Single-stroke 2D templates and accessible alternative; handwriting/3D/ML later |
| 36 — Assembly Maintenance | Multipart objects, fasteners, compatibility, diagnostics, replacement/testing | Authored part/procedure structure; no arbitrary CAD/crafting/physical screw simulation |
| 37 — Replay Ghosts | Motion recording, visual ghosts, annotations, scrubbing, comparison | Selected visual tracks with strict isolation; native Replay bridge and world rewind separate |
| 38 — Race Timing | Starts, directional checkpoints, laps, splits, penalties, local records | Movement-agnostic ordered courses; complex branching/online leaderboards later |
| 39 — Mod Content | Manifests, schemas, versions, dependencies, IDs, conflicts, missing packs | Directory-based data-only packs and trusted providers; executable/untrusted cooked mods excluded |
| 40 — Playtest Recorder | Local issue markers, bounded context, state/log/screenshot evidence, redaction | Opt-in local capture/export; no automatic uploads, keylogging, or reproduction guarantee |

### Engineering detail added by this handoff

The candidate descriptions did not specify exact C++ types, request states, data schemas, formulas, tolerance policies, authority transports, test names, performance loads, build commands, or storage transactions. Those are recommended design contracts introduced here. Adopt or modify them through recorded decisions and actual implementation evidence.

The first-batch recommendation is preserved in Phase 9. Later phase grouping is new planning, not an existing development history. The minimum base can be implemented before optional bridges, but must include the actual visible/audible behavior promised by its base profile where relevant.

---

# Appendix B. Official Engine References and Verification Notes

These references were consulted on **September 27, 2026** for specific engine integration concepts. They do not prove that the proposed plugins compile, meet performance budgets, support every renderer/audio backend, or implement the APIs proposed here. Installed 5.8.3 headers and actual builds remain authoritative for exact usage.

The specification's algorithms, ownership models, test workloads, and scope decisions are design recommendations, not excerpts from Epic documentation. Older examples within a current documentation page must not be copied as current compiler/toolchain guidance without local verification.

| Reference | Official source | Supported integration concept |
|---|---|---|
| R1 | [Epic — Plugins in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine) | Descriptor discovery, plugin organization, and module separation |
| R2 | [Epic — Programming Subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine) | Managed subsystem lifetime and extension points |
| R3 | [Epic — Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine) | Primary asset identity, discovery, streamable handles, bundles, and cook organization |
| R4 | [Epic — Sound Attenuation](https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-attenuation-in-unreal-engine) | Native attenuation/filtering/occlusion/reverb-related playback controls |
| R5 | [Epic API — UGameplayStatics::FindCollisionUV](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UGameplayStatics/FindCollisionUV) | Collision UV lookup and its required UV-hit-data setting |
| R6 | [Epic API — USceneCaptureComponent2D](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D) | Scene capture to a render target; capture/view configuration surface |
| R7 | [Epic — Media Framework Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/media-framework-overview-for-unreal-engine) | Sources, players, playlists, media textures/audio, and asynchronous open readiness |
| R8 | [Epic — Quartz Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-quartz-in-unreal-engine) | Sample-accurate scheduled audio and cross-thread clock concepts |
| R9 | [Epic — DemoNetDriver and Streamers](https://dev.epicgames.com/documentation/en-us/unreal-engine/demonetdriver-and-streamers-in-unreal-engine) | Native replay backends and the risk of replay actors affecting shared live state |
| R10 | [Epic — Game Features and Modular Gameplay](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-features-and-modular-gameplay-in-unreal-engine) | Developer-authored feature activation; not an untrusted-code sandbox guarantee |
| R11 | [Epic — Automation Test Framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine) | Native automation testing concepts and fixture scopes |
| R12 | [Epic — Run Automation Tests](https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine) | Test selection through command-line execution and report export |
| R13 | [Epic API — AsyncSaveGameToSlot](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UGameplayStatics/AsyncSaveGameToSlot) | Game-thread serialization, asynchronous storage, game-thread completion |
| R14 | [Epic — Build Operations: Cook, Package, Deploy, and Run](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-operations-cooking-packaging-deploying-and-running-projects-in-unreal-engine) | UAT/BuildCookRun and distinct packaging stages |
| R15 | [Epic — Actor Component Replication](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicating-actor-components-in-unreal-engine) | Replicated owner/component setup and component RPC capabilities |

### Local verification ledger to complete during implementation

| Integration | What must be verified locally before claiming support |
|---|---|
| Core/public reflection | Exact exported types, includes, UHT-compatible signatures, engine/native module ownership |
| Audio timing | Available playback/seek APIs, callback timing, device latency limits, Quartz bridge capabilities |
| Painting | Collision UV setting, supported collision/mesh path, material parameter, texture upload/cook behavior |
| Photography | Capture scheduling, camera/projection parity, readback synchronization, encoder, image color space |
| Broadcast media | Actual codec/backend, seek precision, source readiness, cooked file staging, failure behavior |
| Native replay | Selected streamer, recording compatibility, actor lifecycle, and live-state isolation |
| Game Features | Trusted-package descriptor/activation lifecycle and platform compatibility |
| Automation | Actual command aliases, queue-exit behavior, report format, timeout and failure detection |
| Packaging | Actual targets/maps/platform flags, installed SDK/toolchain, Development/Shipping launch |

---

# Appendix C. Requirement and Test Coverage Index

Each module has ten baseline acceptance requirements with named automation/functional test contracts. Shared, cross-system, and release requirements supplement them. These are planned checks, not test results.

| Module | Section | Requirement prefix/range | Baseline test namespace |
|---|---:|---|---|
| 21 — Puzzle Mechanisms | 3 | PUZ-01 through PUZ-10 | `Doc.Puzzle.*` |
| 22 — Power Networks | 4 | PWR-01 through PWR-10 | `Doc.Power.*` |
| 23 — Fluid Networks | 5 | FLU-01 through FLU-10 | `Doc.Fluid.*` |
| 24 — Mechanical Networks | 6 | MEC-01 through MEC-10 | `Doc.Mechanical.*` |
| 25 — Material Reactions | 7 | MAT-01 through MAT-10 | `Doc.Material.*` |
| 26 — Service Queues | 8 | QUE-01 through QUE-10 | `Doc.Queue.*` |
| 27 — Acoustic Spaces | 9 | ACO-01 through ACO-10 | `Doc.Acoustics.*` |
| 28 — Optical Beams | 10 | OPT-01 through OPT-10 | `Doc.Optics.*` |
| 29 — Surface Painting | 11 | PNT-01 through PNT-10 | `Doc.Paint.*` |
| 30 — Evidence Deduction | 12 | EVD-01 through EVD-10 | `Doc.Evidence.*` |
| 31 — Photography | 13 | PHO-01 through PHO-10 | `Doc.Photo.*` |
| 32 — Broadcast Channels | 14 | BRC-01 through BRC-10 | `Doc.Broadcast.*` |
| 33 — World Terminals | 15 | TRM-01 through TRM-10 | `Doc.Terminal.*` |
| 34 — Rhythm Challenges | 16 | RHY-01 through RHY-10 | `Doc.Rhythm.*` |
| 35 — Gesture Recognition | 17 | GES-01 through GES-10 | `Doc.Gesture.*` |
| 36 — Assembly Maintenance | 18 | ASM-01 through ASM-10 | `Doc.Assembly.*` |
| 37 — Replay Ghosts | 19 | GHO-01 through GHO-10 | `Doc.Ghost.*` |
| 38 — Race Timing | 20 | RAC-01 through RAC-10 | `Doc.Race.*` |
| 39 — Mod Content | 21 | MOD-01 through MOD-10 | `Doc.Mod.*` |
| 40 — Playtest Recorder | 22 | DBG-01 through DBG-10 | `Doc.Playtest.*` |

Additional groups: `X21-01` through `X21-12` for shared architecture; `CROSS21-01` through `CROSS21-20` for selected integrations; `REL21-01` through `REL21-12` for release. **Total: 244 numbered requirement entries, including 200 module-specific baseline test contracts.**

A baseline test may require multiple assertions, platforms, or fixture variants. The number 200 is not a claim that only 200 executable tests will be needed, nor that any have been authored or run. Requirement traceability must cover the detailed normative contracts, not merely report that a matching test name exists.

---

# Appendix D. Compatibility Ledger with Modules 1–20

## D.1 Shared contracts to reuse or reconcile

| Contract from companion revisions | Required use here | Adoption rule |
|---|---|---|
| `FDocSystemResult` | Typed outcomes, reasons, operation IDs, diagnostics | Reuse actual fields; adapt terminology without creating a competing result framework |
| `FDocGameplayContext` | Explicit world, instigator/target, authority context | Extend narrowly only after checking existing consumers |
| `FDocOwnerScope` | Player/shared-world/session partition | Verify current definition; do not assume one GameInstance equals one user |
| `FDocPersistentObjectId` | Stable world/instance/object identity | Preserve existing serialized format and duplication semantics |
| `FDocWorldObjectReference` | Detached identity plus optional live resolution | No persistent raw actor ownership or forced world loading |
| Transient handles/epochs | Cancel/release and stale-callback rejection | Reuse primitives, add feature-specific typed wrappers |
| `IDocPersistentIdentity` | Components/providers expose durable identity | Avoid a second GUID component that disagrees with the existing one |
| Tag/state provider contracts | Conditions and source-owned mutable claims | Missing provider is Unavailable; do not treat tags as a universal state store |
| `IDocPlayerControlProvider` | Shared input/focus/camera/pause ownership | One host implementation per local player for all participating features |
| Versioned capture/restore | Feature records, restore barriers, migrations | Base remains storage-independent; adapters preserve receipt consistency |
| Producer outbox/consumer receipts | Retriable rewards and cross-feature effects | Implement only for cooperating consumers; no universal exactly-once claim |
| Editor panel registration | Feature-owned tools in shared menu | Core editor does not import every feature |

Clock samples, graph transaction helpers, and resource-claim helpers are proposed reuse candidates, not established Core APIs. Introduce a shared type only when multiple actual consumers need the same semantics. Otherwise keep the helper private to its feature.

## D.2 Existing responsibility boundaries preserved

Interaction owns the operation request, not puzzle truth. Events owns delivery, not permanent gameplay state. Regions owns spatial membership, not sound propagation. Streaming owns residency, not service completion. Time owns its clock, not every feature's deadline. Adaptive Audio owns soundtrack requests, not radio schedules. Sequences owns playback sessions, not mechanical state.

Inspection owns viewing sessions, not photography proof or deduction. Save stores records, not their gameplay interpretation. World Activation controls approved representation costs, not deletion of batteries/parts. Map Navigation displays navigation data, not race validity. Weather supplies environmental state, not physical fluid conservation. Surface Feedback chooses responses, not material reactions.

Dialogue supplies conversations, not evidence truth. Quest Objectives owns objectives, not every minigame rule. NPC Schedules selects activities, not queue capacity or movement. KnowledgeCodex records knowledge, not inference. InventoryItems owns item/container transactions, not assembly structure. UnlocksProgression owns grants/gates, not run scoring. GameFrameworkUI owns presentation flow, not authoritative device commands.

## D.3 Breaking changes and migration policy

Do not rename an already shipped plugin/type/asset because this proposal uses a cleaner name. Record mappings and use supported redirects/migrations where needed. Changing persistent ID composition, interpretation of a timer, result schema, or effect-key semantics is a compatibility change requiring fixtures.

A new module cannot force earlier bases to depend on it. A bridge may introduce a new required contract version for its selected profile, but the existing base must continue to build alone against its supported Core version. Keep compatibility requirements explicit in descriptors/docs and test them in clean hosts.

---

# Appendix E. Minimum Demonstration and Content Manifest

Each example must contain a successful path, an intentional rejected/failure path, and teardown/re-entry. Generic examples are part of verification; a polished game-specific scene is not required.

| Module | Minimum authored/constructed fixture | Required negative case |
|---|---|---|
| Puzzle | Ordered panel, weighted simultaneous controls, timed symbol rule | Wrong order, reset with a late input, missing contributor |
| Power | Source, battery, three consumers, switch, protected branch | Overload, disconnection, unsupported meshed branch protection |
| Fluid | Three finite reservoirs, branch valves, pump, leak/sink | Competing inflow to full tank and endpoint unload |
| Mechanical | Gear/belt tree, clutch, source, resisting load | Stall, multiple driver, invalid cycle/reconvergence |
| Material Reactions | Combustible, wettable, meltable generic objects | Exhausted fuel, competing reaction, propagation cap |
| Service Queues | Mixed-size groups and two capacity-limited stations | No-show, closure during offer, oversized group |
| Acoustic Spaces | Connected rooms, two corridors, adjustable door, emitter | Disconnected route and unsupported listener mix |
| Optical Beams | Emitter, mirror, filter, receiver, generic segment mesh | Loop, blocked path, stale trace after mirror move |
| Surface Painting | Prepared UV mesh, eligible mask, runtime material, two brushes | Unsupported UV mapping and stale texture upload |
| Evidence | Observation/testimony set, two hypotheses, contradiction | Retraction, hidden fact, stale conclusion commit |
| Photography | Three registered subjects, camera provider, image output | Occluded/moving subject, no qualifying subject, failed write |
| Broadcast | Finite bundled programs, two receivers, interruption | Unseekable source, late open callback, receiver unload |
| Terminals | Messages, files, logs, device-local control application | Unauthorized command, conflicting writer, session revoke |
| Rhythm | Original/licensed short audio, tap/hold chart, calibration view | Early release, restart, device timing fault |
| Gesture | Three templates, drawing plane, alternate input | Scribble rejection, ambiguous template, cancelled request |
| Assembly | Cover, two fasteners, replaceable module, diagnostic test | Incompatible part, concurrent removal, resource reconciliation |
| Ghost | Recorded moving proxy, teleport, annotation, seek view | Corrupt recording, late load, attempted live interaction |
| Race | Directional start/checkpoints/finish, two participant types | Reverse crossing, missing gate, teleport, incompatible record |
| Mod Content | Valid sample provider and two data-only packs | Traversal, cycles, missing schema, changed bytes, pinned removal |
| Playtest | Opt-in profile, issue marker, approved snapshot, local export | Secret field, oversized event, disk failure, disabled mode |

A content manifest records asset IDs/paths, source/license, generator or authoring instructions, schema/content versions, cook rules, fixture usage, and verification status. Do not assume a valid editor thumbnail proves the asset ships. No proprietary third-party asset is mandatory for the isolated base fixtures.

---

**End of specification.** Implementation, native builds, authored binary assets, performance measurements, and runtime/network test results remain tasks for the implementing workspace and are not supplied by this Markdown document.
