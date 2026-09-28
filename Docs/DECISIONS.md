# Decisions

Short architecture decision records. Each notes deviations from, or choices within, the handoff (`Docs/UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff.md`).

## D-001 — Repository root is the development host project
**Date:** 2026-09-26
**Decision:** `DocModularDev.uproject` lives at the repository root so `Plugins/DocModular/*` is discovered without copying. It has one empty primary game module (`DocModularDev`) so UBT has real targets to build, and that module doubles as the CORE-06 external consumer.
**Consequence:** The two clean *portability* hosts required by Section 1 are separate projects under `Samples/` and are still to be created. The root host does not count as one.

## D-002 — Adopt `Doc`-prefixed public type names
**Date:** 2026-09-26
**Decision:** New workspace, nothing to audit, so the handoff's recommended `UDoc/ADoc/FDoc/IDoc` names are adopted as-is.

## D-003 — Core tag roots: `Doc.Error.*` and `Doc.Control.*`
**Date:** 2026-09-26
**Decision:** The handoff does not name an error-tag root. Core registers `Doc.Error.*` (default tag per result outcome, plus handle/identity errors) and `Doc.Control.*` (capabilities for `IDocPlayerControlProvider`). Feature roots (`Interaction.*`, `Event.*` …) are registered only by their features.
**Consequence:** These tag names are public API from now on.

## D-004 — `FDocSystemResult` defaults to `Unset`, which is a failure
**Date:** 2026-09-26
**Decision:** Section 3.7 forbids success-shaped defaults. The closed outcome enum has an explicit `Unset` first value; `IsSuccess()` is true only for `Succeeded`.

## D-005 — Handles: process-unique operation ID + table epoch + scope key
**Date:** 2026-09-26
**Decision:** `FDocRequestHandle` = (OperationId, Epoch). Operation IDs and epochs come from process-wide atomic counters and are never reused. `TDocHandleTable<T>` (native, game-thread) binds each entry to a scope `FObjectKey` (normally the owning `UWorld`) and returns Invalid / Active / Stale / WrongScope. Both handle fields are `UPROPERTY(Transient)`, so persistent serialization drops them (a restored handle is unset).
**Rejected:** storing a weak world pointer inside the handle (leaks world identity into Blueprint values, and a recycled object address could alias).

## D-006 — Instance-scope composition v1 = truncated SHA-1
**Date:** 2026-09-26
**Decision:** `ComposeInstanceScope(Parent, Placement)` = first 16 bytes of SHA-1("DocModular.InstanceScope.v1" ‖ Parent ‖ Placement), GUIDs as little-endian uint32s; zero result is remapped (D=1) because zero is the root scope. The algorithm is part of the save format and is pinned by golden values in `Doc.Core.Identity.RepeatedInstances`. Any change requires a new version string and a migration.
**Why SHA-1:** available in Core, deterministic across platforms, collision resistance is ample for identity (not security). Duplicate detection is still required at registration time (DocSave / validators).

## D-007 — `IDocGameplayTagProvider` in addition to the engine's `IGameplayTagAssetInterface`
**Date:** 2026-09-26
**Decision:** The engine interface cannot be implemented in Blueprint. Core defines a Blueprint-implementable read interface and `UDocCoreBlueprintLibrary::GetOwnedTagsFromObject()`, which checks the Doc interface first and then falls back to the engine interface, so existing C++/GAS actors need no changes. Mutation is a separate interface (`IDocMutableGameplayTagProvider`) with owner-attributed deltas.

## D-008 — No Core settings object and no Core editor module yet
**Date:** 2026-09-26
**Decision:** Section 4.4 says settings only for genuinely shared values; none exist yet, so no `UDeveloperSettings` subclass and no DeveloperSettings dependency. The editor module is deferred until there is real editor functionality (Section 2.1: no empty Editor modules). CORE-07 stays Not Started.

## D-009 — Control-claim arbitration helper deferred
**Date:** 2026-09-26
**Decision:** Core ships only the `IDocPlayerControlProvider` contract and `FDocControlClaimRequest`. A shared arbitration helper (priority, then recency, per capability) will be added when Inspection or Sequences is implemented, with tests against both consumers, rather than speculatively now.

## D-010 — `EDocTriState` uses `Unknown / No / Yes`
**Date:** 2026-09-26
**Decision:** Avoids `True`/`False` enumerators, which collide with X11 macros on Linux builds.

## D-011 — Build order across both handoffs
**Date:** 2026-09-27
**Decision:** One sequence: M1.1 verification → Rev 2 modules 1–10 (Core → Events → Interaction → Regions → Time → Streaming → Save → Audio → Sequences → Inspection → Activation) → expansion Milestone 5.0 re-check → Phase 5 (SurfaceFeedback → MapNavigation → Weather) → Phase 6 (NPCSchedules → Dialogue → QuestObjectives → KnowledgeCodex) → Phase 7 (UnlocksProgression → InventoryItems) → Phase 8 (GameFrameworkUI). Expansion bases don't need modules 1–10 to compile, but building them later lets their bridges (Save, Time, Events, Sequences, Inspection) be tested against real implementations instead of fakes, and keeps one milestone open at a time.
**Consequence:** No expansion plugin code until the foundation reaches Activation, unless the developer reprioritises. The Milestone 5.0 audit was done now as documentation (`Docs/FOUNDATION_COMPATIBILITY.md`) and must be re-run when Phase 5 starts.

## D-012 — Extend `EDocResultOutcome` for the expansion (accepted)
**Date:** 2026-09-27
**Proposal:** Append `NoChange`, `InvalidInput`, `Unavailable`, `Conflict`, with default tags `Doc.Error.NoChange` (if NoChange stays a success, no tag), `Doc.Error.InvalidInput`, `Doc.Error.Unavailable`, `Doc.Error.Conflict`, and add a `Doc.Error.Storage` tag. Open question: does `IsSuccess()` include `NoChange`? (Proposed: yes, plus `IsChanged()`.)
**Status:** Accepted by the developer 2026-09-27 and applied before Core's first compile. `NoChange` counts as success: `IsSuccess()` is true for `Succeeded` and `NoChange`; new `IsChanged()` is true only for `Succeeded`. Values appended after `Failed` (10–13) and pinned with `static_assert`s in `DocSystemResult.cpp`. Tags added: `Doc.Error.InvalidInput`, `Doc.Error.Unavailable`, `Doc.Error.Conflict`, `Doc.Error.Storage` (storage failures use Outcome `Failed` with this tag). `NoChange` has no error tag.

## D-013 — Bridge plugins live in `Plugins/DocModularBridges/`
**Date:** 2026-09-27
**Decision:** Follow expansion §23. The folder is a descriptor-free grouping directory that the dev host discovers. The root `Bridges/` folder (from Rev 2 §2.1) is used only for distribution staging, if at all. Only implemented bridges are created.

## D-014 — Add `Doc.Control.Cursor` and `Doc.Control.Focus` when first needed
**Date:** 2026-09-27
**Decision:** The expansion's UI and dialogue presentation claim cursor and focus through the shared `IDocPlayerControlProvider`. The tags are added to `DocCoreTags` (additive) in the milestone that first uses them (Inspection in Rev 2, or GameFrameworkUI), not speculatively.

## D-015 — Shared expansion primitives go into Core with their first consumer
**Date:** 2026-09-27
**Decision:** `FDocOwnerScope` (first consumer: MapNavigation), pure condition outcome with reason and revision (first: Dialogue/NPCSchedules), durable record revision + epoch (first persistable base), and the effect-key value type (first: QuestObjectives/InventoryItems) are shared by several features, so Core is their home. Each is added only in the milestone that first uses it, with tests. Algorithms (quest, item, dialogue, weather, UI) never enter Core.

## D-016 — Build every base plugin before the first compile (developer request)
**Date:** 2026-09-27
**Decision:** The developer asked for all plugins to be written now and verified later. This overrides D-011's "one open milestone at a time" for source authoring only. All twenty feature bases were written in the D-011 order; none is compiled. Verification still follows D-011: Core first, then each feature in order, fixing compile errors before behaviour.
**Consequence:** Compile errors may exist across many plugins at once. `Scripts/Verify-Suite.ps1` runs builds plus every `Doc.*` suite and `-Only <filter>` re-runs one area.

## D-017 — Per-plugin documentation lives in each plugin's `README.md`
**Date:** 2026-09-27
**Decision:** Each feature plugin has one `README.md` (status, dependencies, not-included list, handoff-to-Doc name mapping, behaviour, tests). Core keeps its `Docs/` folder (API, Blueprint, C++, debugging, setup, changelog) because every feature builds on it.

## D-018 — Packaged reference control provider in Core (resolves D-009)
**Date:** 2026-09-27
**Decision:** `FDocControlClaimArbiter`, `UDocReferencePlayerControlProvider` and the per-local-player `UDocPlayerControlSubsystem` are in Core. Inspection, Sequences, Dialogue presentation and Framework UI share one provider per local player. Without a provider, acquires return Unavailable and features run in a declared no-control-mutation mode.

## D-019 — DocSave: runtime spawns detected with `!IsNetStartupActor()`
**Date:** 2026-09-27
**Decision:** An actor with a saveable component that is not a net-startup actor is treated as a runtime spawn and gets a runtime identity exactly once. Level-placed actors keep their authored GUID.

## D-020 — DocSave: deterministic world namespace from the map package
**Date:** 2026-09-27
**Decision:** `WorldNamespace` is a deterministic GUID derived from the map package name, with the PIE prefix stripped. Renaming a map needs a migration, or `WorldNamespaceOverride`.

## D-021 — DocSave: local backend does not claim atomic replace
**Date:** 2026-09-27
**Decision:** The local backend writes a temp file, verifies it, keeps the previous file as `.bak`, then renames. It is not advertised as atomic until a rename-based commit is fault-tested on each platform.

## D-022 — EnhancedInput is an essential dependency of Interaction and Inspection
**Date:** 2026-09-27
**Decision:** Both install owned Input Mapping Contexts for their own input and never clear other contexts. EnhancedInput is an engine plugin, so it is listed in their descriptors and enabled in the host `.uproject`. No other base depends on it. Framework UI takes device samples and default bindings from an adapter instead (InputCore only).

## D-023 — NPCSchedules: restored durable overrides are ownerless
**Date:** 2026-09-27
**Decision:** Owner objects do not survive a save, so a durable override restored from a save is ownerless. It ends by expiry or an explicit administrative clear, never because an owner pointer is missing.

## D-024 — SurfaceFeedback does not pool
**Date:** 2026-09-27
**Decision:** Components are created and destroyed normally. Pooling with full reset and generation checks belongs in the DocObjectPool utility (ACT-07 backlog), not in a feature base.

## D-025 — World facades for GameInstance-owned state
**Date:** 2026-09-27
**Decision:** Quests, unlocks and similar state live in a GameInstance subsystem so they survive travel. A small `UTickableWorldSubsystem` facade attaches the authority world on `OnWorldBeginPlay`, detaches it in `Deinitialize`, and drives WorldGameplay/RealTime clocks. Each attach is a new world generation, so callbacks from an old world are rejected.

## D-026 — No `Busy` outcome
**Date:** 2026-09-27
**Decision:** The expansion's "busy"/"in use" cases map to existing outcomes: `Conflict` for concurrent modification, reservations and stale revisions, and `NotReady` for work queued behind a running commit. `EDocResultOutcome` stays append-only (D-012).

## D-027 — Doc-prefixed names everywhere; handoff names mapped in each README
**Date:** 2026-09-27
**Decision:** D-002 applies to every plugin. Each README has a table mapping the handoff's semantic names (ItemSubsystem, UIManagerSubsystem...) to the Doc-prefixed types.
**Consequence:** A name collision between two plugins is a UHT error once both load in one host. `FDocScheduleEntry` existed in DocTime and DocNPCSchedules, so the NPCSchedules struct was renamed `FDocNPCScheduleEntry`.

## D-028 — Inventory receipts cover successes only; retired ids are session state
**Date:** 2026-09-27
**Decision:**
- **Receipts.** Only successful transactions are receipted. A failed request can be retried with the same key, and a receipted key with a changed payload is a `Conflict`.
- **Persistence.** Receipts are saved with the containers.
- **Retired ids.** Tombstones for merged or consumed instance ids help with diagnostics during a session. They are not saved.

## D-029 — Framework UI base is presentation-neutral; settings authority rules
**Date:** 2026-09-27
**Decision:**
- **No widget types.** The base has no UMG or CommonUI types. Screens carry a `PresentationKey` that a presenter (the CommonUI bridge, a UMG reference presenter or a test double) resolves. Headless operation changes logical state only.
- **Previews.** Video previews call `ApplyResolutionSettings(false)`, never `ApplySettings`, which also saves.
- **Confirm.** Confirmation calls `ConfirmVideoMode`, and `SaveSettings` runs only after confirmation.
- **Revert.** Revert re-applies the last-known-good values explicitly instead of relying on `RevertVideoMode`, then verifies the applied `GSystemResolution`.
- **Recovery.** The recovery marker is persisted before a preview is applied.
- **Global pause** is a lease in `UDocUIPauseSubsystem`, refused in networked worlds unless listen-server pause is enabled.

## D-030 — Module 21 DocPuzzleMechanisms: Closed input variant, composite rules, and reset epoch discipline
**Date:** 2026-09-27
**Decision:**
- **Closed input variant.** `FDocPuzzleInputValue` supports `Trigger`, `Boolean`, `Scalar`, and `DiscreteSymbol` without silent type coercion.
- **Rule hierarchy.** Authored acyclic rules (`UDocPuzzleRule`) support ordered sequence matching (with explicit `ResetProgress`, `FailAttempt`, or `IgnoreUnexpected` mismatch policies), simultaneous dwell (`MinDwellSeconds`), weighted threshold aggregation across contributors, symbol combination, and half-open timed sequence windows (`[start, deadline)`).
- **Composite rules.** `UDocPuzzleRule_Composite` supports `All` (AND) and `Any` (OR / alternative solutions). Alternative paths complete in one terminal state transition.
- **Reset epoch & attempt invalidation.** Resetting increments an instance `ResetEpoch` and invalidates the active attempt ID, ensuring delayed asynchronous or replayed events cannot corrupt the new attempt.
- **Dual delegates.** Subsystem and host component expose dynamic multicast delegates for Blueprint consumption and native multicast delegates for low-overhead C++ test and game-thread listeners.

## D-031 — Module 26 DocServiceQueues: Indivisible group atomicity, starvation-bounded first-fit, and capacity accounting
**Date:** 2026-09-27
**Decision:**
- **Capacity accounting.** Invariants strictly enforce `ReservedSeats + InServiceSeats <= StationCapacity`.
- **Indivisible groups.** Groups of size `n` are evaluated, offered, and admitted atomically; partial party admissions are strictly rejected.
- **Starvation-bounded first fit.** `FirstFitWithBypassLimit` increments `BypassCount` on skipped tickets. Once a head-of-line ticket reaches `MaxBypassCount`, further bypasses are blocked to prevent large group starvation.
- **Decoupled movement vs service.** Arrival confirmation (`ConfirmArrival`) transitions ticket state to `Ready` without consuming in-service capacity; `StartService` explicitly consumes reservation into in-service seats.
- **Graceful station closure.** Supports `DrainCurrentService`, `CancelPendingReservations` (reverting tickets cleanly to waiting without losing ordinal precedence), and `EmergencyAbort`.

## D-032 — Module 22 DocPowerNetworks: Invariant energy conservation, deterministic allocation, and breaker protection
**Date:** 2026-09-27
**Decision:**
- **Energy Conservation Ledger.** Every fixed simulation step reconciles `ExternalEnergyAccepted + StoredEnergyBefore == LoadEnergyDelivered + StoredEnergyAfter + ConversionLosses + ExplicitDiscard`. Potential generation is not accepted energy; only energy delivered to loads or stored into batteries is accepted.
- **Deterministic Priority Allocation.** Consumers are sorted by priority (descending), then stable `TieBreakId` (ascending), then NodeId (lexicographically). Allocation is invariant under registration order permutations.
- **No Charge/Discharge Loops.** A battery is forbidden from charging and discharging within the same step. Surplus external power is required to charge storage; batteries cannot cycle energy between each other to invent free energy.
- **Binary and Scalable Modes.** Binary consumers require full requested power or drop out to Brownout/Off. Scalable consumers accept partial power between MinimumPowerWatts and DesiredPowerWatts.
- **Breaker Protection & Meshed Rejection.** Breakers monitor coherent pre-allocation demand on protected radial branches or islands. Overloaded breakers trip, decouple the circuit, and require a cooldown period before reset is accepted. Meshed branch protection assumptions are explicitly rejected with `EDocResultOutcome::Unsupported` rather than assigning invented loop currents.
- **Topology Revision Guard.** Snapshots record `TopologyRevision`. Restoring an outdated snapshot against a modified topology is rejected with `EDocResultOutcome::Conflict`.

## D-033 — Module 30 DocEvidenceDeduction: Fact vs claim epistemology, revision invalidation, and audience masking
**Date:** 2026-09-27
**Decision:**
- **Fact vs Claim Distinction.** Observations distinguish `Status == Claim` (unverified witness testimony / rumor) from `Status == EstablishedFact`. Only established facts satisfy required evidence in inference rules.
- **Alternative Evidence Sets & Disqualifiers.** Hypotheses support multiple alternative sets of required evidence (OR composition among sets, AND within a set). Disqualifying facts trigger immediate `Contradicted` status even if supporting sets are present. Missing evidence yields `Inconclusive` with explicit missing IDs, preventing false satisfaction by absence.
- **Revision Invalidation & Challenged State.** Retracting or revising an observation automatically reevaluates dependent hypotheses and marks committed conclusions as `bIsChallenged = true`, broadcasting `OnConclusionChallenged`.
- **Stale Commit Protection.** `CommitConclusion` checks expected evidence revision against active `EvidenceRevision`, rejecting stale commits with `EDocResultOutcome::Conflict`. Mutually exclusive conclusion groups enforce single-decision exclusivity.
- **Acyclic Dependency Validation.** Inference graphs check for cycles prior to rule evaluation, failing explicitly with `EDocResultOutcome::InvalidConfiguration` and path diagnostics if a cycle is detected.
- **Audience Protection.** Hidden hypotheses and secret answer keys are masked from ordinary queries (`QueryExplanation`) unless explicitly authorized with `bIncludeSecretSolutions = true`, returning `EDocResultOutcome::PermissionDenied` and redacted summaries.

## D-034: DocRhythmChallenges Architecture and Deterministic Judgment Engine (Milestone 9.5)
- **Separation of Judgment Rules and DSP/Transport.** The plugin owns challenge rules, timing windows, combo/scoring logic, hold note lifecycles, and input calibration. It abstracts audio transport and clock providers via `IDocRhythmClockProvider` and `IDocRhythmPlaybackProvider`, allowing headless deterministic test execution with mock clocks and playback providers as well as seamless binding to engine audio engines (Quartz / SoundSubsystem).
- **Signed Calibration and Offset Math.** Timing evaluation follows `JudgmentTimeUs = InputTimestampMappedToChartUs - InputOffsetUs` and `ErrorUs = JudgmentTimeUs - NoteTimeUs`. Positive `InputOffsetUs` represents late player/device arrival and is subtracted from mapped time. Robust median estimation with spread filtering provides calibration estimation while returning `EDocResultOutcome::NotReady` for erratic samples.
- **Strict Boundary and Deduplication Semantics.** Windows (`Perfect`, `Great`, `Good`, `Miss`) use integer microseconds with inclusive bounds (`|ErrorUs| <= WindowHalfUs`). Unjudged notes are hit sequentially; one accepted input sample cannot score multiple notes, and judged notes are sealed against duplicate scoring.
- **Hold Note Lifecycle & Policy.** Holds require valid initial tap timing and continuous held input. Release before minimum hold percentage (`MinHoldPercent`, default 80%) yields an explicit early release `Miss`. Holds held through the entire note duration automatically complete. Device focus loss immediately breaks active holds as a single Miss without spamming repeated miss events.
- **Attempt and Generation Scoping.** Restarting a challenge increments both `AttemptId` and `SessionGeneration`, instantly isolating prior callbacks, holds, and state, resetting judgments, combo, and score.
## D-035: DocOpticalBeams Architecture and Deterministic Beam Ledger (Milestone 10.1)
- **Deterministic Segment Ledger.** Beam propagation paths are computed synchronously into an immutable `FDocBeamPath` ledger of segments (`FDocBeamSegment`). Each segment records start point, hit point, direction, travel length, incident/exit intensity, and hit metadata.
- **Budget Envelopes & Hard Limits.** Beam tracing enforces strict budget parameters: maximum reflections/bounces (`MaxReflections`), maximum trace distance (`MaxRange`), and minimum intensity cutoff (`MinIntensity`).
- **Surface Interaction Pipeline.** Surfaces implement physical and gameplay optical profiles (`UDocOpticalProfile`) specifying surface type (`Mirror`, `Filter`, `Splitter`, `Absorber`, `Refractor`), reflectivity, transmittance, absorption, and wavelength/channel filter tags. Refraction follows Snell's Law with critical angle total internal reflection fallback.
- **Self-Hit Avoidance.** Reflected and transmitted rays advance forward along their new direction by a calibrated `TraceEpsilon` to prevent numerical self-intersection with the originating collision boundary. Travelled distance accounts for epsilon offsets.
- **Receiver Aggregation & Dwell Lifecycle.** Receivers (`UDocBeamReceiverComponent`) accumulate incoming emitter contributions with configurable aggregation modes (`AnyEligible`, `SumIntensity`, `AllRequiredChannels`). Sustained activation requires unbroken dwell duration (`RequiredDwellTimeSeconds`), while loss of illumination transitions to a hysteresis cooldown window (`ReleaseHysteresisSeconds`) before deactivation.
- **Cycle and Loop Detection.** Infinite reflective loops between opposing parallel mirrors are detected via spatial state hashing (position and orientation quantization) or budget exhaustion, terminating cleanly with `EDocBeamTerminationReason::LoopDetected` without hanging execution.
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types, requiring zero dependencies on `DocPuzzleMechanisms`, `DocPowerNetworks`, or Niagara/VFX subsystems.

## D-036: DocAcousticSpaces Architecture, Portal Graph, and Parameter Composition (Milestone 10.2)
- **Bounded Nonnegative Dijkstra Solver.** Portal edges compute nonnegative logarithmic gain penalties `-log(max(gain, 1e-4))` plus distance tie-breaking. A portal with zero gain or disabled state is treated as a blocked edge. Paths are resolved using bounded Dijkstra minimizing total path cost, selecting the single strongest admissible acoustic route with stable tie-breaking on `PortalId`.
- **Filtering Composition Policy.** Path transmission gain equals the product of all traversed portal gains. Cutoff frequency approximates the most restrictive (minimum) low-pass filter along the path.
- **Explicit Disconnected Fallback Policy.** Unreachable spaces or missing routes evaluate to `EDocAcousticPathStatus::Blocked` and apply configured floor gain (`DisconnectedFloorGain`, default 0.0) and wall cutoff (`DisconnectedCutoffHz`, default 200 Hz), preventing abrupt full-volume unattenuated fallbacks.
- **Non-Cumulative Parameter Composition.** Acoustic parameter claims combine portal transmission multipliers directly with authored/host `BaseGain` (`TargetEffectiveGain = BaseGain * TransmissionMultiplier`). This prevents exponential attenuation loops when callers update host parameters during game events or audio fades.
- **Continuous Parameter Smoothing.** Emitters interpolate effective gain and cutoff frequencies smoothly using frame time and `SmoothingInterpSpeed`, eliminating audio zipper artifacts during portal state transitions while supporting explicit teleport snapping (`ResetSmoothing`).
- **Single-Listener Reference Mix Adapter.** While path queries are independently scoped for arbitrary listeners, the reference playback adapter claims a single designated active listener per mix, cleanly isolating multiple listeners and avoiding undefined shared attenuation asset mutations.
- **Boundary Hysteresis & Overlap Priority.** Spatial membership resolves overlapping boundaries via space `Priority` then stable ID. Boundary hysteresis allows listeners and emitters to remain in their previous space across transition thresholds to prevent spatial jitter at room boundaries.
- **No Sibling Dependencies.** Operates completely standalone against `DocModularCoreRuntime` without coupling to `DocRegions` or `DocAdaptiveAudio`.

## D-037: DocSurfacePainting Architecture, UV Rasterization, and Non-Destructive Material Safety (Milestone 10.3)
- **Dual Representation Model.** Surfaces maintain both a high-fidelity discrete 2D rasterized mask grid (`FDocPaintGrid`) for direct coverage queries / texture blits and a canonical stroke log (`FDocPaintStroke`) for replay, undo/redo, and persistence compaction.
- **Exact Half-Texel Centering.** UV coordinates evaluate texel centers at `((X + 0.5) / Width, (Y + 0.5) / Height)`, ensuring exact symmetric radial falloff calculation and zero-distance stroke alignment.
- **Monotonic Coverage & Mode Invariants.** `Paint` adds coverage clamped to `[0.0, 1.0]`; `Erase` subtracts coverage clamped to `[0.0, 1.0]`. Total coverage is computed deterministically via exact texel summation, avoiding floating-point drift.
- **Analytical Brush Falloff.** Radial falloff evaluates normalized distance `d = dist / radius`, applying `Linear`, `Smoothstep`, or `Hard` curves scaled by brush opacity.
- **Non-Destructive Material Override.** Dynamic Material Instances are created per-component to receive render target updates. Source material assets in project storage are never modified during play or editor preview.
- **Undo / Redo & Checkpoint Compaction.** Stroke histories support undo/redo stacks. Checkpoint captures serialize raw/RLE grid buffers, allowing old stroke logs to be compacted without losing current surface coverage.
- **No Sibling Dependencies.** Runs entirely on `DocModularCoreRuntime` and engine types, requiring zero coupling to `DocInteraction`, `DocInventoryItems`, or physics/render plugins.

## D-038: DocMaterialReactions Architecture, State-Presentation Isolation, and Bounded Propagation (Milestone 10.4)
- **State vs Presentation Separation.** `FDocMaterialState` models authoritative gameplay facts (moisture, remaining fuel, phase fraction, char amount, revision) independently of cosmetic audio/particle systems. Restoring material states cleanly rebuilds visual representations without re-triggering gameplay ignition rewards or consumption events.
- **Dwell, Threshold, and Hysteresis.** Ignition requires sustained channel exposure meeting or exceeding `ActivationThreshold` for `DwellDuration` seconds. Deactivation hysteresis enforces distinct `DeactivationThreshold < ActivationThreshold` levels to eliminate edge chatter.
- **Self-Sustaining vs Continuous Exposure.** Reactions specify `bSelfSustaining`. Combustible reactions (burning) continue consuming fuel once ignited until depleted or quenched, whereas exposure reactions (melting, drying) deactivate when external intensity falls below deactivation thresholds.
- **Exclusive Reaction Grouping & Priority Resolution.** Competing reactions within the same `ExclusiveGroup` are resolved deterministically by authored `Priority`, strictly preventing double consumption of finite fuel or moisture during a single simulation step.
- **Bounded Spatial Propagation & Deduplication.** Subsystem propagation enforces strict budget limits (`MaxNeighborsPerSource`, `MaxWorkPerStep`). Evaluated contacts are deduplicated per step using symmetric pair hashing, preventing quadratic explosion.
- **Lifecycle & Weak Pointer Safety.** Components register on `OnRegister` and unregister on `OnUnregister`. Stale or unloaded actors are pruned safely without invalid pointer mutations.
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and essential engine types, requiring zero dependencies on `DocWeather`, `DocFluidNetworks`, `DocSurfaceFeedback`, or Niagara plugins.

## D-039: DocPhotography Architecture, Camera Projection, and Subject Evaluation (Milestone 11.1)
- **Separation of Capture and Challenge Evaluation.** A photo capture commits image bytes and metadata records reliably regardless of whether the composition satisfies any specific challenge or quest profile (`PHO-04`). Failed challenge evaluation produces an unsuccessful score/reason without corrupting or aborting image storage.
- **Rigorous Camera Projection & Frustum Metrics.** Subject centers and relative sample points are projected to screen UV space using the exact capture FOV, aspect ratio, camera position, and rotation (`PHO-01`). Objects behind the camera or outside `[0.0, 1.0]` UV bounds are explicitly flagged.
- **Sampled Occlusion & Bounded Geometry.** Visibility is evaluated by tracing sample points against world collision, producing an empirical visible fraction rather than ungrounded claims of pixel-accurate segmentation (`PHO-02`).
- **Owner Scope Isolation & Subject Privacy.** Captures record `FDocOwnerScope`. Private subjects marked `bIsPrivate` belonging to other owner scopes are filtered out during metadata gathering, preventing information leakage across local players (`PHO-08`).
- **Bounded Gallery Storage & Failure Safety.** Galleries enforce strict photo count and byte budgets with deterministic eviction of the oldest records (`PHO-07`). Disk failure simulation cleanly aborts record commitment without phantom records (`PHO-06`). Missing disk blobs mark records `ImageUnavailable` without corrupting metadata (`PHO-09`).
- **No Sibling Dependencies.** Completely isolated to `DocModularCoreRuntime` and engine types, requiring zero dependencies on `DocSave`, `DocEvidenceDeduction`, or `DocKnowledgeCodex`.

## D-040: DocBroadcastChannels Architecture, Shared Channel Transports, and Interruption Policies (Milestone 11.2)
- **Authoritative Shared Channel Transports.** Channels maintain a single continuous virtual transport cursor derived from an explicit anchor epoch and clock source (`BRC-01`). Rather than spinning up separate playlists per listener, receivers tune into the live channel offset (`BRC-02`). Receiver tuning and muting operations remain completely decoupled from transport playback (`BRC-03`).
- **Dual Interruption Resume Policies.** `PauseUnderlyingCursor` pauses the underlying schedule progression during higher-priority interruptions, resuming from the paused offset once the alert ends (`BRC-04`). `ContinueUnderlyingSchedule` lets the underlying schedule advance in real time while an announcement plays, re-synchronizing with the advanced timeline upon completion (`BRC-04`).
- **Priority and Ordinal Arbitration.** Interruption requests declare priority (`Emergency > High > Normal > Low`) with stable FIFO ordinal tie-breaking. Releasing or expiring an announcement cleanly pops only its own reservation without disturbing remaining active alerts (`BRC-05`).
- **Failure Transparency & Capability Verification.** Unsupported seeking or unknown/procedural program durations fail explicitly before mutation (`BRC-07`). Failed source loads transition to `UnavailableSource` without replaying stale programs (`BRC-06`).
- **Time Dilation & Save/Restore Isolation.** Accelerated virtual clocks map to transport schedules via configurable dilation (`BRC-09`). Channel state captures record virtual progress without replaying historical transition triggers on restore (`BRC-08`).
- **No Sibling Dependencies.** Operates purely on `DocModularCoreRuntime` and engine types without coupling to `DocAdaptiveAudio`, `DocPowerNetworks`, `DocTime`, or `DocSave`.

## D-041: DocWorldTerminals Architecture, Session Arbitration, and State-Presentation Isolation (Milestone 11.3)
- **Authoritative User Sessions vs Presentation Viewers.** Terminals separate the single active interactive user session (`ActiveSessionUser`) from passive world observers (`PassivePresentationViewers`). A player attempting to interact with an occupied terminal is cleanly queued or rejected under `CollisionPolicy` (`RejectNewRequest`, `QueueIfAvailable`, `EvictLowerPriority`) without corrupting or hijacking the active user's input stream (`TRM-01`).
- **Headless State vs Presentation Isolation.** `FDocTerminalState` encapsulates the complete authoritative simulation state (screen text buffer, active screen mode, security lock level, cursor position, revision). Headless servers and simulated world actors advance terminal logic and execute commands without allocating Slate or UMG widgets (`TRM-03`).
- **Explicit Transition Validation & Reentrancy Guards.** Command dispatch validates terminal operational state, power requirements, and user privilege before execution. Staged state changes commit atomically, and invalid commands return structured error receipts without causing partial state corruption (`TRM-02`, `TRM-04`).
- **Remote Terminal Bridge & Network Latency Simulation.** Remote terminal links support configurable round-trip latency, bandwidth constraints, and packet loss simulation, cleanly reporting `CommandTimeout` or `LinkSevered` without blocking calling threads or dropping terminal session state (`TRM-05`).
- **Peripheral Device Security & Capability Tokens.** External peripherals (keycards, biometric scanners, datalinks) attach via explicit capability tokens. Removal of an authorizing peripheral immediately revokes elevated privilege without retaining cached session credentials (`TRM-06`).
- **Crash/Reboot Cycles & Power Interruption Recovery.** Terminals model reboot intervals and cold restart sequences. Unexpected power failure resets volatile memory to authored defaults while preserving non-volatile diagnostic logs (`TRM-07`).
- **No Sibling Dependencies.** Operates purely on `DocModularCoreRuntime` and engine types, requiring zero coupling to `DocInteraction`, `DocPowerNetworks`, `DocSave`, or `DocBroadcastChannels`.

## D-042: DocGestureRecognition Architecture, Resampling Normalization, and Accessible Alternatives (Milestone 11.4)
- **Deterministic Arc-Length Resampling & Centroid Alignment.** Input strokes filter consecutive duplicate points and non-finite coordinates, resample evenly along cumulative arc length into a fixed canonical count (N=32), and translate to origin via arithmetic centroid (`GES-01`).
- **Explicit Scale Policy & Aspect Ratio Preservation.** Normalization policies are authored per template (`UniformPreserveAspect` vs `NonUniformFitBox`). Uniform scaling scales by maximum dimension `max(W, H)`, strictly preserving thin versus wide aspect distinctions, while non-uniform stretching is explicitly opt-in (`GES-02`).
- **Authored Rotation & Direction Invariance Controls.** Templates configure `bRotationInvariant` (circles permit orientation invariance, whereas directional arrows enforce absolute orientation, `GES-03`) and `bDirectionInvariant` (stroke reversal evaluated only when explicitly enabled, `GES-04`).
- **Similarity Scoring & Runner-Up Margin Rejection.** Average Euclidean point-distance maps to similarity in `[0, 1]`. Recognized matches must satisfy both `MinSimilarityThreshold` and `MinRunnerUpMargin` over the second-place candidate, safely rejecting ambiguous near-matches as `Ambiguous` and scribbles as `NotRecognized` (`GES-05`).
- **Immutable Template Sets & Generation Safety.** Recognition results carry the template set version and request generation. Invalidation of sessions or version updates do not alter past results or allow stale/cancelled strokes to trigger consumer actions (`GES-06`, `GES-07`).
- **Input Bounds & Resource Defense.** Subsystem enforces strict bounds (`MaxPointsPerStroke`, `MaxStrokeDurationSeconds`, `MinStrokePathLength`), rejecting malformed or bloated inputs before algorithmic evaluation (`GES-08`).
- **Accessible Alternative Semantic Invocation.** Players unable to draw reliably can submit semantic gesture intents directly through `SelectAccessibleAlternative`, returning a fully qualified recognized result tagged `EDocGestureInputSource::AccessibleAlternative` through the exact same validated consumer path (`GES-09`).
- **Local Player Isolation.** Multiple local players operate with independent stroke sessions, generations, and template sets without cross-talk or UI/AI dependencies (`GES-10`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocInteraction`, `DocSave`, `DocEvents`, or UI libraries.

## D-043: DocFluidNetworks Architecture, Volume Conservation, and Proportional Flow Scaling (Milestone 12.1)
- **Single-Liquid Volume Conservation Invariant.** Fluid transfers operate in documented volumetric units (Liters) where each committed transfer appears identically as a source decrement and destination increment (`FLU-01`). Pipe edges are directed conduits without implicit storage; fluid resides strictly in finite-capacity storage nodes.
- **Proportional Two-Phase Flow Constraint.** Competing outgoing flows are scaled proportionally to initial source volume, and competing incoming flows are scaled proportionally to initial free destination capacity (`FLU-02`, `FLU-03`). The second constraint only reduces transfer volume, strictly preserving source constraints.
- **Conservative One-Step Transport Delay.** Outgoing volume is not credited as new destination capacity, nor is incoming volume credited as new source supply within the same simulation step (`FLU-01`, `FLU-07`). This eliminates unphysical infinite instantaneous circulation loops while maintaining numerical stability.
- **Explicit Inflow, Leak, and Drain Accounting.** External supplies, drain requests, and leaks are committed to an authoritative ledger (`FDocFluidLedger`) satisfying `TotalVolumeAfter == TotalVolumeBefore + ExternalInflow - ExternalOutflow - Leaks - Drains` (`FLU-06`).
- **Valve Aperture & Pump Availability.** Flow rate evaluates `MaxFlowRate * ValveOpening * dt`, gated by pump status (`Enabled`, `Disabled`, `Blocked`). Closed valves or disabled pumps retain stored reservoir contents without leakage or phantom flow (`FLU-05`).
- **Unresolved Endpoint Isolation.** Stream unloads or missing downstream components cleanly suspend the affected edge without deleting or clamping reservoir volume (`FLU-08`).
- **Explicit Capacity Migration Policy.** Reduced reservoir capacities from asset/definition changes enforce declared policies (`Quarantine` vs `SpillToSink`), recording any spilled volume explicitly in the sink ledger rather than silently clamping (`FLU-09`).
- **Strict Input Sanitization & Error Handling.** Negative volumes, non-finite values, and invalid configurations fail safely without corrupting network state (`FLU-10`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocPowerNetworks`, `DocSave`, or `DocMaterialReactions`.

## D-044: DocMechanicalNetworks Architecture, Drive Train Trees, and Reflected Load Propagation (Milestone 12.2)
- **Directed Acyclic Drive Train Topology.** Mechanical networks model rotational kinetic energy transmission as directed acyclic trees originating at drive sources (motors, engines, cranks). Multi-parent drive convergences and cyclic loops are rejected during topology validation, returning explicit configuration errors before edge insertion (`MEC-01`, `MEC-02`).
- **Deterministic Angular Velocity & Direction Propagation.** Angular speed propagates from root driver through downstream gears, shafts, and pulleys using signed gear ratios (`Omega_Child = Omega_Parent * GearRatio`). Negative ratios model rotational direction reversal without special cases (`MEC-03`).
- **Recursive Reflected Load & Efficiency Losses.** Load torques propagate in reverse from driven leaves back to the driving source as `Load_Parent = Load_Child * abs(GearRatio) / Efficiency`. Transmission inefficiencies compound proportionally up the drive chain (`MEC-04`).
- **Motor Torque-Speed Curve & Hysteresis Stall Recovery.** Prime movers compute speed based on power curves and maximum torque limits. When total reflected load exceeds available drive torque, the branch transitions to `Stalled` (`MEC-05`). Resuming rotation requires the reflected load to drop below `DriveTorque * (1.0 - StallReleaseMargin)` to prevent rapid limit-cycle chattering (`MEC-05`).
- **Zero-Speed Static Torque Stability.** At zero speed, motor torque is capped at authored `MaxTorque` rather than attempting a `P / Omega` division, ensuring numerical stability during startup and stall transitions (`MEC-06`).
- **Clutch Decoupling & Kinetic Freewheeling.** Disengaging a clutch or disconnecting an edge separates downstream elements into isolated kinetic bodies whose speeds decay via friction damping (`Omega(t) = Omega_0 * exp(-Damping * t)`). Re-engaging synchronizes rotational speeds while enforcing shock torque limits (`MEC-07`).
- **Structural Torque Limits & Mechanical Failure.** Operating beyond component-rated torque thresholds triggers mechanical failure (`Damaged` / `Broken`), decoupling downstream drive shafts and logging diagnostics (`MEC-08`).
- **Authoritative Snapshot Capture & Rehydration.** Complete rotational state (angular velocities, clutch engagements, reflected loads, wear/damage) serializes cleanly for save/load and streaming recovery (`MEC-09`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocPowerNetworks`, `DocFluidNetworks`, `DocSave`, or Chaos physics (`MEC-10`).

## D-045: DocAssemblyMaintenance Architecture, Structural Prerequisites, and Two-Phase Transactions (Milestone 12.3)
- **Structural Prerequisites & Obstructing Hierarchy.** Removal and installation operations validate physical assembly constraints rather than relying on UI workflow assumptions. Slots require all defined fasteners to be in `Released` state and all obstructing slots to be empty before parts can be removed or accessed (`ASM-01`).
- **Strict Logical Part Uniqueness Invariant.** Each part instance holds a unique `PartInstanceId` and exists in exactly one logical location: installed in an authored slot, detached in world storage, or quarantined (`ASM-02`). Parts cannot be duplicated or concurrently installed across multiple locations.
- **Atomic Staged Transactions & Commit Boundaries.** Operations follow a two-phase commit: `StageOperation` validates prerequisites and acquires resource reservations, while `CommitOperation` applies the logical mutations (`ASM-05`). Cancellation before commit cleanly rolls back staged changes without modifying part locations or fastener tightness; cancellation after commit preserves the committed logical outcome.
- **Resource & Inventory Reconciliation.** External inventory interactions use `IDocAssemblyResourceProvider` with explicit `ReservePart`, `CommitPartTransfer`, and `RollbackPartTransfer` steps (`ASM-06`). Provider transfer failures trigger immediate transaction abort and rollback without leaking or duplicating parts.
- **Presentation Failure Decoupling.** Visual mesh spawning failures set `bPresentationPending` on the part instance without creating duplicate logical representations or dropping ownership (`ASM-07`).
- **Diagnostic Truth vs Hidden Faults.** Part defects are modeled as hidden fault tags unrevealed until targeted diagnostic tests evaluate the containing slot (`ASM-08`). Diagnostic execution checks power prerequisites (`BlockedByPower`), and overall assembly functional certification requires running post-repair tests rather than inferring success from part replacement.
- **Streaming & Restore Quarantine Migration.** Deserializing corrupted states or obsolete slot assignments migrates mismatched parts into `QuarantinedParts` without silent deletion or inventory duplication (`ASM-09`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocInventoryItems`, `DocInteraction`, `DocSave`, or physics engines (`ASM-10`).

## D-046: DocReplayGhosts Architecture, Presentation Isolation, and Discontinuity Invariants (Milestone 13.1)
- **Isolated Visual Surrogates by Construction.** Ghosts instantiate dedicated `ADocGhostSurrogateActor` instances rather than cloned player pawns or characters (`GHO-04`). All collision, live interaction, damage capability, root motion, and replication are disabled by construction, ensuring ghost playback can never collect pickups, trigger overlaps, damage entities, or mutate live world state (`GHO-04`, `GHO-05`).
- **Bounded Motion Sampling without Fabricated Points.** Motion is captured at declared cadences with actual timestamps (`GHO-01`). Dropped frames or sample pauses remain explicit temporal gaps rather than synthesized or interpolated historical inputs.
- **Deterministic Translation Lerp and Shortest-Path Quaternion Slerp.** Sample playback evaluates continuous transforms via linear position interpolation and shortest-path quaternion spherical linear interpolation (`FQuat::Slerp`) (`GHO-02`).
- **Discontinuity Boundary Invariant.** Teleports, respawns, world rebases, and attachment transitions are explicitly tagged with `bIsDiscontinuity = true` (`GHO-03`). The evaluation pipeline clamps to the pre-teleport pose prior to the boundary timestamp and snaps instantaneously at the boundary, strictly forbidding artificial interpolation through invalid world space.
- **Indexed Chunk Seeking.** Recordings partition samples into indexed chunks (`FDocGhostTrackChunk`) with precomputed duration intervals (`GHO-06`). Scrubbing and seeking evaluates the target timestamp directly without sequential simulation or replaying historical gameplay events.
- **Generation-Protected Lifecycle Defense.** Playback sessions increment their active generation on termination or closure, invalidating any pending asynchronous chunk streams or asset loads and preventing stopped playback from reviving (`GHO-07`).
- **Strict Format Validation and Visual Fallbacks.** Malformed sample buffers, negative timestamps, and non-finite/NaN coordinates are rejected at validation boundaries (`GHO-08`). Unsupported future recording formats cleanly fall back to basic transform playback when explicitly authorized, or fail safely without data corruption (`GHO-09`).
- **Concurrent Playback Coexistence.** Ghost playback executes concurrently beside live world gameplay without performance degradation or interference with ongoing game mechanics (`GHO-10`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocRaceTiming`, `DocSave`, `DocEvents`, or native Unreal Replay subsystems.

## D-047: DocRaceTiming Architecture, Continuous Gate Crossings, and Multi-Segment Splitting (Milestone 13.2)
- **Continuous Directed Plane Sweeping.** Checkpoint gate triggers evaluate continuous actor movement segments between consecutive ticks `[P0, P1]` against the gate's plane `(Center, Normal)`. Crossing is validated by sign transition `dot(P0 - Center, Normal) <= 0` and `dot(P1 - Center, Normal) > 0`, projecting the intersection point into the gate's local width/height aperture (`RAC-01`, `RAC-02`).
- **Exact Chronological Fractional Traversal.** Segment crossings compute fractional time `Alpha = -d0 / (d1 - d0)`. When multiple checkpoints or rapid gates are traversed in a single simulation frame, crossings are sorted chronologically by `Alpha` ascending, preventing high-speed frame tunneling or out-of-order checkpoint commits (`RAC-02`, `RAC-03`).
- **Authoritative Course Progression & Lap Invariants.** Courses configure sequential checkpoints and finish lines. Progression rejects out-of-order gates or backwards crossings (`RAC-04`). Reaching the final checkpoint advances the lap counter, records authoritative split and lap times, and resets sequential checkpoint tracking for multi-lap races (`RAC-03`).
- **Missed Gate Penalty & Disqualification Rules.** Leaving the track or bypassing a required checkpoint gate enforces authored policy (`TimePenalty`, `EnforceRespawn`, `Disqualification`). Accumulated penalties are recorded in the active run ledger without corrupting raw split timings (`RAC-06`).
- **False Start Detection & Staged Session States.** Sessions enforce distinct lifecycle phases (`Staged`, `CountingDown`, `Active`, `Finished`, `Disqualified`). Movement beyond the start threshold during countdown triggers false start penalties or run invalidation (`RAC-05`).
- **Sector Splits & Delta Comparison.** Courses partition into timed sectors. Current splits compare against personal best (`PB`) and session leader splits, exposing relative delta values (`SplitDeltaSeconds`) for HUD telemetry without UI coupling (`RAC-07`).
- **Multiple Concurrent Participants.** The subsystem tracks concurrent participants (human players and AI racers) through isolated participant records (`FDocRaceParticipantSession`), supporting synchronized starts and independent split timing (`RAC-08`).
- **No Sibling Dependencies.** Operates purely against `DocModularCoreRuntime` and engine types without coupling to `DocReplayGhosts`, `DocSave`, `DocEvents`, `DocMapNavigation`, or UI systems.

## D-048: DocModContent Architecture, Transactional Activation, and Strict Data Boundary (Milestone 13.3)
- **Strict Data-Only Content Boundary.** Mod packs are bounded collections of text/JSON definitions and assets strictly constrained to authored schemas (`doc.sample.definition`, etc.). Executable libraries (`.dll`, `.so`), script files (`.bat`, `.sh`, `.ps1`), and binary assets (`.uasset`, `.umap`) are rejected by default during path and manifest safety validation (`MOD-01`, `MOD-09`).
- **Path Containment & Directory Sandboxing.** Mod file accesses validate canonical collapsed paths against configured pack root directories. Parent traversals (`..`), absolute Windows drive paths, leading slashes, and path escapes outside the pack root are blocked prior to I/O operations (`MOD-02`).
- **Deterministic Dependency Graph & Topological Ordering.** Packs declare versioned dependencies and conflict lists. Dependencies are resolved using Kahn's topological sort with deterministic alphabetical tie-breaks. Missing required dependencies, incompatible semantic version constraints, cyclic dependencies, and declared pack conflicts fail plan building with explicit diagnostics (`MOD-03`).
- **Namespace Collision Defense.** All authored definition IDs require pack prefix namespacing (`pack_id:definition_id`). Global namespace collisions across distinct mod packs fail activation plan building, preventing silent definition hijacking or unintentional overrides (`MOD-04`).
- **Two-Phase Staged Catalog Transactions.** Mod pack activation executes an atomic two-phase commit: all definitions are staged across registered providers (`StageDefinition`). If any provider fails staging or validation, all staged changes are rolled back (`RollbackStagedDefinitions`), leaving the prior catalog revision and active definitions completely intact (`MOD-05`).
- **Validated Byte Identity / Hash Verification.** Content byte hashes computed at scan/validation time are re-verified prior to activation. Any modification or tampering of definition files between plan building and activation fails activation and invalidates the plan (`MOD-06`).
- **Reference-Counted Definition Pinning.** Active gameplay sessions, records, and save contexts can acquire definition pins (`AcquireDefinitionPin`). Pack deactivation requests are rejected with `InUse` while active pins remain, preventing dangling references and mid-game asset disappearance (`MOD-07`).
- **Missing Save Content Quarantine.** Loading save files that reference missing or uninstalled mod packs quarantines the affected records (`QuarantineMissingContent`) with raw JSON payloads preserved, avoiding destructive save rewrites or data loss (`MOD-08`).
- **Isolated Provider Architecture.** The subsystem operates with zero sibling dependencies, tested and verified end-to-end using the self-contained `UDocModSampleDefinitionProvider` (`MOD-10`).

## D-049: DocPlaytestRecorder Architecture, Bounded Ring Buffer, Redaction, and Local Evidence Bundles (Milestone 13.4)
- **Strictly Bounded In-Memory Ring Buffer.** Diagnostics are collected in memory under configurable count, retention window, and byte budgets (`MaxBufferSizeBytes`, `MaxEventCount`, `MaxRetentionWindowSeconds`). Memory pressure drops the oldest events first while tracking `EvictedEventCount`, preventing unbounded memory growth or runaway allocations during high-frequency gameplay events (`DBG-01`).
- **Temporal Marker Windows & Event Correlation.** Issue markers capture configured pre- and post-marker intervals around tester observations (`PreWindowSeconds`, `PostWindowSeconds`). Segments are extracted into self-contained chronological `.jsonl` streams without replaying or altering live simulation state (`DBG-02`).
- **Privacy-Aware Redaction Prior to Retention.** Sensitive authentication credentials, passwords, private tokens, and secret keys are detected and replaced with `[REDACTED]` before entering ring buffer memory or serialized exports (`DBG-03`). Raw unredacted secrets never touch memory buffers or disk bundles.
- **Fault-Tolerant Provider Omission Ledger.** Contextual snapshots are supplied via `IDocPlaytestEvidenceProvider`. Provider crashes, timeouts, or exceptions are caught and recorded as structured omission records (`FDocDiagnosticOmission`) in the bundle manifest rather than crashing or disrupting active gameplay (`DBG-04`).
- **Transactional Staged Bundle Export.** Export transactions write files into a dedicated staging folder (`.staging_<MarkerId>`). Hardware write failures, disk capacity issues, or user cancellations roll back the staging area cleanly, guaranteeing that existing completed bundles remain untorn and intact (`DBG-05`).
- **Cryptographic Manifest & Archive Integrity.** All bundle contents (event streams, provider snapshots, marker metadata) are hashed with MD5/SHA-256 and cataloged with exact file sizes and time ranges in `manifest.json`, providing verifiable provenance and tamper detection (`DBG-06`).
- **World Teardown & Context Boundary Isolation.** Level travel and world unload boundaries notify active providers and commit explicit boundary events (`Engine.WorldBoundary`) to the event stream, preventing dangling actor references or stale world context leakage across level transitions (`DBG-07`).
- **Untrusted Bundle Protection.** External bundle paths and preview inputs are validated against strict canonical directory sandboxing, rejecting relative path traversals (`..`), drive escapes, and leading slashes before inspection (`DBG-08`).
- **Zero-Overhead Disabled Mode.** When capture is disabled (`bCaptureEnabled = false`), event submission evaluates an inlined boolean check and returns immediately without memory allocations, JSON serialization, or string formatting (`DBG-09`).
- **Local User-Controlled Architecture.** Bundles are created strictly on local disk at user request without automatic network transmission, remote telemetry services, or coupling to sibling gameplay plugins (`DBG-10`).

## D-050: Stable Source Fingerprints for Run Evidence

- **Decision:** `Get-DocSourceHash` fingerprints implementation, project configuration, and specification inputs while excluding generated output and mutable run/status documents (`DECISIONS.md`, `DEVELOPMENT_STATUS.md`, `HANDOFF.md`, the traceability files, `RELEASE_CHECKLIST.md`, and `TEST_MATRIX.md`).
- **Reason:** Verification summaries and traceability records are updated after builds/tests complete. Hashing those records would change the reported source fingerprint without changing the project inputs under test.
- **Consequence:** The recorded SHA-256 identifies the code/config/specification inputs to a run. Logs, generated output, and post-run status records do not alter it.

## D-051: Physical-Absence Isolation Hosts

- **Decision:** `Verify-PluginIsolation.ps1` generates a fresh temporary host for Core alone and for Core plus one selected feature. Each host contains only its allowed DocModular plugin directories; declared engine plugin dependencies remain engine-provided. The generated consumer module includes each public header in a separate translation unit and builds non-unity.
- **Reason:** A full-suite editor build can conceal missing dependencies while sibling plugins are available. Physical staging tests the base dependency boundary and external header usability directly.
- **Consequence:** A passing isolation build establishes compile/header independence for that source snapshot. It does not establish runtime startup, packaging, PIE behavior, or portability to a second consumer project.

## D-052: Exclude UAT Build Scratch from Source Fingerprints

- **Decision:** Ignore the repository-root `/Build/` directory in Git and exclude generated `Build` directories from `Get-DocSourceHash`.
- **Reason:** `RunUAT BuildCookRun` creates file-open-order logs under `Build/Windows`. They are generated scratch, not project inputs; including them makes a successful package run appear to have a different source tree after cooking.
- **Consequence:** Packaging and manual-gate records keep a stable source fingerprint before and after UAT runs. Packaged artifacts remain under `Scripts/Output/<run-id>/Archive/`.

## D-053: Read-only Workspace Preflight

- **Decision:** `Validate-Workspace.ps1` performs read-only checks of the exact engine identity and tools, host project and plugin descriptors, the Core-only feature dependency boundary, Android File Server defaults, and required verification wrappers. It writes a `summary.json` under `Scripts/Output/`.
- **Reason:** Catch repository and environment setup errors before expensive builds while keeping static checks distinct from build, automation, asset, runtime, and package evidence.
- **Consequence:** A passing preflight reports workspace readiness only. It cannot mark plugin behavior, runtime startup, authored assets, cooking, networking, presentation, or performance as verified.

## D-054: Isolated Runtime Startup Evidence

- **Decision:** `Verify-IsolatedStartup.ps1` launches each already-built physical-absence host in headless game mode and requires the expected Runtime modules to load, an engine world to reach play, clean shutdown, and exit code zero. Before launch, it verifies that staged plugin files match the current repository sources.
- **Reason:** Compilation and public-header isolation do not prove that plugin modules load in a game world. A bounded startup check closes that evidence gap without claiming feature behavior.
- **Consequence:** A pass proves basic startup and teardown with the documented plugin set only. PIE behavior, feature functionality, cooked assets, bridge-present behavior, performance, and consumer-host portability require separate evidence.



