# Unreal Engine 5.8.3 Modular Gameplay Systems Suite
## IDE Implementation Handoff / Development Plan — Revision 2

**Document revision:** 2.0  
**Prepared:** September 26, 2026  
**Target engine:** Unreal Engine 5.8.3  
**Primary platform:** Windows x64  
**Implementation:** C++ runtime plugins with Blueprint-first public APIs  
**Configuration:** Data Assets and Developer Settings  
**Identifiers / state:** Gameplay Tags, explicit state models, and stable persistent identities  
**Manager lifetime:** Unreal Subsystems  
**Actor-level features:** Actor Components and interfaces  
**Input:** Enhanced Input without mandatory project mappings  
**Persistence:** Stable GUID-based identities, versioned records, and save interfaces  
**Deliverable defined here:** A reusable, project-agnostic gameplay plugin library—not a game and not a monolithic framework.  
**Implementation status:** Specification only. No plugin source, Unreal build, runtime test, or packaging result is supplied by this document.

> **Instruction to the implementing IDE agent:** Read Sections 0–4, 15–19, and 27–29 before editing code. Start with Task 0 in Section 28. Implement one bounded, verified milestone at a time. Do not scaffold ten empty plugins and report the suite as implemented.

### What this revision preserves and changes

The supplied handoff remains the basis for the ten systems, their names, project-agnostic scope, Windows target, Unreal version, and four-phase development order. The original 30 numbered sections are retained and strengthened.

This revision adds **recommended implementation contracts**: concrete dependency boundaries, ownership rules, asynchronous state machines, persistent identity semantics, failure handling, capability gates, test evidence, and IDE execution instructions. Those additions are engineering decisions proposed by this revision, not claims that the original document already specified them or that an existing implementation has passed them.

**Explicit clarifications to the original:** optional features have their own acceptance gates; retained events are not automatically disk persistence; subsystems are not treated as automatic replication endpoints; Packed Level Actors are not treated as a universal independent streaming backend; save serialization is not assumed to be worker-thread safe; integer time alone is not a proof of determinism. Section A records these changes and their scope impact. Official engine references are identified separately in Section B.

---

## Navigation

- **Begin implementation:** Sections 0, 4, 15, and 28.
- **Architecture and dependencies:** Sections 2–3.
- **Ten system specifications:** Sections 5–14.
- **Milestones and test gates:** Sections 15–19.
- **Tooling, quality, and release:** Sections 20–27.
- **Continuation prompt and status template:** Section 29.
- **Change record, references, and requirement coverage:** Sections A–C.

---

# 0. Document Control and IDE Execution Contract

## 0.1 Scope and precedence

Treat **must** and **must not** as acceptance requirements for the capability being implemented. Treat **recommended** as the default design choice; a different choice requires a short architecture decision record explaining the trade-off. Examples illustrate data and are not hard-coded product behavior.

Preserve existing repository instructions, licenses, user changes, and working implementations. Audit existing code before renaming classes or creating replacement frameworks. Where this revision supplies a new contract, record its adoption in `Docs/DECISIONS.md`; do not silently rewrite an already published API or save format.

No additional game systems are authorized. Inventory, quests, combat, weather simulation, crowds, character creation, ride logic, procedural world generation, and UI frameworks remain consumers or optional adapters—not features to implement inside this suite.

## 0.2 Capability labels

| Label | Meaning | Completion rule |
|---|---|---|
| `BASE` | Independently usable feature plugin plus Core and its essential native engine dependencies | Must pass isolated build, behavior, and packaging gates |
| `BRIDGE` | Separately installed integration with another feature or optional engine system | Must pass its own tests and the base-with-bridge-absent test |
| `EXAMPLE` | Generic sample content or demonstration UI | Must be generated or authored in Unreal and exercised; not required by runtime libraries |
| `FUTURE` | Explicitly deferred extension | Must remain visible in the backlog; not advertised as implemented |

All ten **base systems** remain in scope. Optional integrations from the original remain in scope as separately tracked work. Moving a feature to a bridge does not mark it complete and does not remove its acceptance criteria.

Use three distinct release claims:

- **Base Suite Complete:** Core and ten base systems meet Section 27.
- **Original Capability Profile Complete:** additionally verifies Runtime Data Layers, the required Level Instance path, Quartz quantization, and MetaSound playback, as originally requested.
- **Extended Integrations Complete:** individually identifies tested Smart Objects, GAS, CommonUI, Mass, ISM/HISM, pooling, and other bridges. Never imply that every integration is supported merely because the bases work.

## 0.3 Exact-engine gate

Unreal Engine 5.8.3 is the requested target. Epic published its 5.8.3 hotfix announcement on September 22, 2026. That establishes the release, not compatibility of code that has not been built. [R1]

Before using an engine API:

1. Resolve the engine from the existing `.uproject` association, an explicit local configuration, or installed engine registrations. Do not assume a fixed drive path.
2. Read `Engine/Build/Build.version`; record major, minor, patch, changelist, and build identity where available.
3. Inspect the installed headers, module rules, and relevant plugin descriptors. Record exact includes and native module dependencies for version-sensitive integrations.
4. Compile a minimal vertical slice before committing to a large abstraction around an API.
5. If the installation is missing or differs from 5.8.3, report the mismatch. Do not silently downgrade, change the engine association, or claim native verification.

Online 5.8 documentation is architectural guidance. The installed **5.8.3 headers and successful builds** are the authority for exact signatures, UHT support, and module availability.

## 0.4 Evidence vocabulary

Use only these implementation states:

`Not Started → In Progress → Implemented / Unverified → Verified`

`Blocked` is a separate state with a concrete cause and next action. A test can be `Not Run`, `Passed`, `Failed`, or `Skipped with Reason`. Missing Unreal, an unavailable toolchain, a missing asset, or a disabled bridge is never a passing test.

Record commands, exit codes, discovered tests, executed tests, failures, log paths, source revision, engine identity, and artifact paths. Documentation checks do not prove runtime correctness. Compilation does not prove cooked content availability. PIE success does not prove packaged execution.

## 0.5 Safe execution boundaries

Work inside the selected workspace. Do not overwrite user assets, delete unrelated files, install marketplace content, change global engine settings, publish releases, or push commits without the applicable authorization. Local generated build artifacts may be recreated only within known output directories.

Make forward progress without inventing missing facts. When native validation is unavailable, produce bounded source/documentation work and an exact blocked verification step; do not expand into unrelated systems to conceal the blocker.

---

# 1. High-Level Goal

Build ten reusable Unreal Engine 5.8.3 gameplay systems:

| System | Plugin | Owns | Explicitly does not own |
|---|---|---|---|
| Universal Interaction | `DocInteraction` | Candidate detection, interaction requests, validation, execution sessions | Inventory, dialogue, commerce, combat, project HUD |
| Gameplay Event Manager | `DocEvents` | Local typed event delivery, scopes, bounded retained values | Automatic networking, arbitrary persistent game state |
| Region / Zone Manager | `DocRegions` | Logical region definitions and tracked spatial membership | Audio, loading, weather, population behavior |
| Level Instance / Streaming Manager | `DocStreaming` | Request ownership and orchestration over native streaming | A replacement streaming engine or renderer |
| Time + Day/Night | `DocTime` | Simulation clock, calendars, periods, schedule queries | Sky actors, weather, NPC routines |
| Adaptive Music + Ambient Audio | `DocAdaptiveAudio` | Audio requests, channel arbitration, layers, transitions | DSP implementation or mandatory game-state dependencies |
| Sequence / Set-Piece Manager | `DocSequences` | Sequence playback sessions, bindings, arbitration, cleanup | Mandatory character, camera, AI, or mission framework |
| Inspection / Environmental Storytelling | `DocInspection` | Per-player inspection sessions, content models, discoveries | Mandatory HUD/CommonUI, automatic world rewards |
| Persistent World Save | `DocSave` | Versioned persistence, identity resolution, restoration | Interpretation of inventory, damage, or quest payloads |
| World Activation / Proximity Optimization | `DocWorldActivation` | Runtime relevance tiers and owned representation transitions | World Partition replacement or unrestricted actor virtualization |

The suite contains **eleven foundational plugins: one shared Core plus ten feature plugins**. Optional bridges and examples are additional packages, not extra mandatory gameplay systems.

A consuming project must not be required to replace its Pawn, Character, GameMode, PlayerController, GameInstance, Asset Manager, camera system, save UI, inventory, or quest framework. Interfaces and opt-in components provide integration points.

“Drop-in” means documented setup with no modifications to plugin source: copy or install the chosen plugin and Core, enable essential dependencies, configure assets, attach components or implement interfaces, and bind the project's own input/UI where relevant. It does not mean that arbitrary assets configure themselves without authoring.

**Portability gate:** demonstrate each base plugin in two clean, structurally different consumer hosts. At least one host uses a non-Character actor as an interaction participant. Neither host may contain original-project classes, map names, or external asset paths in library runtime code.

---

# 2. Plugin Layout

## 2.1 Repository structure

```text
Repository/
├─ Plugins/
│  └─ DocModular/                 # Grouping directory only: no .uplugin here
│     ├─ DocModularCore/
│     ├─ DocEvents/
│     ├─ DocInteraction/
│     ├─ DocRegions/
│     ├─ DocStreaming/
│     ├─ DocTime/
│     ├─ DocAdaptiveAudio/
│     ├─ DocSequences/
│     ├─ DocInspection/
│     ├─ DocSave/
│     └─ DocWorldActivation/
├─ Bridges/                       # Source/distribution staging, when implemented
├─ Samples/                       # Generic host projects and separate example plugins
├─ Scripts/                       # Validation/build/test wrappers
└─ Docs/
   ├─ DEVELOPMENT_STATUS.md
   ├─ DECISIONS.md
   ├─ REQUIREMENTS_TRACEABILITY.md
   ├─ DEPENDENCY_MATRIX.md
   ├─ ENGINE_COMPATIBILITY.md
   ├─ TEST_MATRIX.md
   ├─ PERFORMANCE_BASELINE.md
   └─ RELEASE_CHECKLIST.md
```

A bridge used by a host must be installed into that host's discoverable `Plugins` tree; a repository staging directory is not assumed to be automatically discovered.

Epic's plugin discovery scans subdirectories but stops descending after finding a plugin. Keep the grouping directory descriptor-free; do not place one plugin inside another plugin's directory. Runtime and Editor modules must be declared separately. [R2]

Per-plugin layout:

```text
DocFeature/
├─ DocFeature.uplugin
├─ Source/
│  ├─ DocFeatureRuntime/
│  │  ├─ DocFeatureRuntime.Build.cs
│  │  ├─ Public/
│  │  └─ Private/
│  │     └─ Tests/                # Development-only automation where appropriate
│  └─ DocFeatureEditor/          # Only when actual editor functionality exists
│     ├─ DocFeatureEditor.Build.cs
│     ├─ Public/
│     └─ Private/
├─ Config/                       # Minimal defaults / tag configuration as needed
├─ Content/                      # Only assets truly required by this plugin
├─ Resources/
└─ Docs/
```

Do not create empty Editor modules for every plugin just to match the diagram. Keep generic demonstration assets in separate example content so they cannot accidentally become runtime dependencies.

## 2.2 Dependency contract

Every feature runtime module may depend on `DocModularCoreRuntime` and **only the native engine modules necessary for its own base behavior**. It must not depend on another feature plugin. Core must not depend on any feature.

Candidate native dependencies below require verification against installed 5.8.3 headers; this is not a copy-paste `.Build.cs` declaration:

| Plugin | Essential native integration surface | Forbidden sibling dependencies in the base |
|---|---|---|
| Core | Core, CoreUObject, Engine, GameplayTags; DeveloperSettings where used | All ten features |
| Events | Reflected structs, delegates, timers; verified `FInstancedStruct` ownership | Regions, Save, Interaction |
| Interaction | Traces, components, Enhanced Input for supplied input adapter | Events, Sequences, Inspection, GAS |
| Regions | Collision/geometry queries, components, optional volume authoring | Events, Streaming, Audio |
| Streaming | Engine-native level streaming | Regions, Save, Sequences; World Partition bridge types |
| Time | Clock/update integration and configuration | Audio, Save, Events |
| Adaptive Audio | Engine audio playback via `USoundBase` | Regions, Time, Events; optional audio plugin types |
| Sequences | LevelSequence and MovieScene runtime integration | Streaming, Events, Interaction |
| Inspection | Local-player sessions, Enhanced Input, runtime preview rendering | Interaction, CommonUI, optional media integration |
| Save | Versioned data and storage integration | Streaming, Regions, Activation |
| Activation | Registration, bounded relevance evaluation, component state control | Mass, ISM bridge, Save, pooling utility |

Module dependencies and plugin descriptor dependencies are different checks: declaring one does not replace the other. Public headers determine public dependencies; implementation-only dependencies remain private. Verify reflected property types as well as C++ includes.

`FInstancedStruct` module/header ownership is version-sensitive. The current API reference lists the type under CoreUObject; verify the installed declaration instead of blindly adding an older StructUtils dependency. [R3]

## 2.3 Optional bridge plugins

Preserve the original optional integrations:

| Bridge / utility | Required participants | Contract |
|---|---|---|
| `DocInteractionSmartObjects` | Interaction + native Smart Objects integration | Reservation and use adapter, not a replacement interaction core |
| `DocInteractionGAS` | Interaction + GameplayAbilities | Ability/attribute/tag adapter with authority rules |
| `DocRegionsWorldPartition` | Regions + native World Partition integration | Stream-aware registration and authored identity support |
| `DocStreamingWorldPartition` | Streaming + native World Partition integration | Streaming Sources and Runtime Data Layer backends |
| `DocAdaptiveAudioMetaSound` | Adaptive Audio + verified MetaSound modules | Typed MetaSound controls; generic playback remains `USoundBase`-based |
| `DocInspectionCommonUI` | Inspection + CommonUI | UI adapter without changing inspection state ownership |
| `DocSaveWorldPartition` | Save + native World Partition integration | Streamed participant registration and restoration hooks |
| `DocWorldActivationMass` | Activation + native Mass integration | Entity/actor transitions after a separate feasibility gate |
| `DocWorldActivationISM` | Activation + native instancing integration | Logical-object-to-instance mapping and reversible transitions |
| `DocObjectPool` | Core + essential native engine modules | Explicitly poolable actor lifecycle utility |

**Added boundary clarifications:** use `DocAdaptiveAudioQuartz`, `DocAdaptiveAudioModulation`, and `DocInspectionMedia` for optional Quartz scheduling, Audio Modulation, and media playback. Their exact module requirements must be verified locally. These names describe integrations to implement, not available products.

Use small producer/consumer bridges for actual cross-feature behavior, for example `DocInteractionEvents`, `DocInteractionSequences`, `DocInteractionInspection`, `DocRegionsAudio`, `DocTimeAudio`, `DocSequencesStreaming`, or `DocSaveTime`. Record each real bridge in the dependency matrix. Do not prebuild every possible pair.

A bridge may depend on both participants. Neither participant may include the bridge. A base plugin must compile with the other plugin **physically absent**, not merely disabled.

Never put a reflected property typed as another optional plugin's class into a base Data Asset. Use an extension interface, a validated generic metadata field, or bridge-owned configuration. A soft pointer does not remove a compile-time dependency on its C++ type.

---

# 3. Shared Architectural Rules

## 3.1 C++ foundation, Blueprint-first API

Implement infrastructure in C++; expose meaningful operations through `BlueprintCallable`, queries through genuinely side-effect-free `BlueprintPure` nodes, events through assignable delegates, and extensions through `BlueprintNativeEvent` / `BlueprintImplementableEvent` where appropriate.

Use valid Unreal reflected types, `UINTERFACE`/`IInterface` pairs, proper module API exports, and UHT-compatible signatures. Provide the generated interface-call path for Blueprint implementations. Descriptive signatures in this document are contracts, not guaranteed compilable declarations.

Prefer `UDoc...`, `ADoc...`, `FDoc...`, and `IDoc...` names for newly authored public types to reduce collisions. The original unprefixed names were proposed names; audit existing consumers before adopting this normalization.

Each async Blueprint action must have a deliberate lifetime, explicit cancellation, and cleanup after completion. A garbage-collected proxy must not cause a silent, permanently pending operation.

## 3.2 Tags, stable IDs, and bounded enums

Use Gameplay Tags for extensible meanings: `Interaction.*`, `Event.*`, `Region.*`, `State.*`, `Audio.*`, `Sequence.*`, `World.*`, `Time.*`, `Save.*`, `Activation.*`, and `Inspection.*`.

Small enums are appropriate for closed algorithmic states such as Pending/Succeeded/Failed/Cancelled or Exact/IncludeChildren matching. Do not represent a state machine as unrelated booleans.

Distinguish four identities: a semantic tag, a definition/asset ID, a persistent world-object ID, and a transient request handle. A tag is not automatically unique per object or per instance.

Provide documented exact-versus-hierarchical matching and redirect/migration policy. Register native plugin tags deterministically; projects may add their own tags without source edits. Do not rename public tags casually after content or saves ship.

## 3.3 Interfaces over project-specific casting

Use interfaces for interaction, persistence, world state, sequence participants, tag state, and external camera/input control. Do not cast to a particular game's actor hierarchy.

A tag-grant action requires an explicit mutable tag provider; Gameplay Tags alone do not imply an actor has mutable state storage. Missing providers return an actionable capability error. A generic sample provider may be supplied without forcing GAS.

## 3.4 Separate definitions from runtime state

Data Assets and instanced configuration objects are immutable during gameplay. Do not store cooldowns, active targets, discovered focus points, event listeners, or per-player progress on shared assets or class default objects.

For an authored condition/action UObject, choose either immutable evaluation with external session state or a deliberate per-session instance with a valid Outer and GC ownership. Never reuse one mutable action instance across concurrent sessions.

Components register/unregister actor functionality. Subsystems own registries and session coordination. Providers implement replaceable mechanisms; bridges implement cross-plugin policies.

## 3.5 Subsystem lifetimes and world isolation

Subsystems provide engine-managed lifetime and Blueprint-accessible extension points. Use that lifecycle rather than requiring a placed manager actor. [R4]

| Lifetime | Intended ownership |
|---|---|
| `UWorldSubsystem` | Events, Interaction, Regions, Streaming, Time, Audio director, Sequences, Activation |
| `ULocalPlayerSubsystem` | Inspection and local presentation/session state |
| `UGameInstanceSubsystem` | Save-slot coordination and persistence data spanning map travel |
| Editor module/service | Panels, validators, asset authoring, diagnostics registration |

Filter supported world types and net modes. Gameplay world services must not accidentally start in asset previews, class defaults, or unrelated editor worlds. Dedicated servers must not initialize audio playback or inspection rendering.

Avoid `GWorld`, static world registries, and implicit player index zero. Receive or resolve an explicit world/local-player context. Clear world references on travel, PIE shutdown, and subsystem deinitialization. A GameInstance subsystem may retain plain save records across travel, not old actors.

## 3.6 Request handles, ownership, and cancellation

Every long-lived request returns a typed handle containing a unique operation ID plus enough owner/world generation information to reject stale use. Each API documents whether duplicate requests create independent leases or are idempotent for an explicit caller key.

Standard state model:

```text
Created → Pending → Running → Succeeded
                  └────────→ Failed
                  └────────→ Cancelled
                  └────────→ TimedOut
```

Use only applicable states for each operation. Exactly one terminal result is recorded. Live subscribers receive one terminal notification on the game thread; after world/owner destruction, suppress unsafe callbacks while still releasing resources and recording cancellation internally.

Cancellation releases **that request's** assets, subscriptions, locks, and leases. It must not cancel another request's shared load or stop another owner's audio. Late native callbacks check generation and request state before changing anything.

Timeouts are configurable and classified by operation. Release and unsubscribe operations are safe to repeat. Invalid or cross-world handles do not affect valid requests.

## 3.7 Typed results and diagnostics

`FDocSystemResult` must expose a closed outcome, extensible error tag, localized user-facing text where appropriate, technical diagnostic text, and correlation/operation ID. Distinguish `Unsupported`, `NotReady`, `NotFound`, `InvalidConfiguration`, `PermissionDenied`, `Cancelled`, `TimedOut`, and actual execution failure.

A recoverable error does not crash a packaged game. `check` is for violated internal invariants, not missing user-authored content. Avoid success-shaped default values when configuration is invalid.

## 3.8 Asynchronous content and cooking

Use soft references for substantial content. Resolve through the engine's asset loading facilities, retain required loading handles for the correct lifetime, and release them when ownership ends. The Asset Manager supports discovery, loading, bundles, and cook organization; a complete integration must address all of those concerns rather than just pointer type. [R5]

Each plugin documents primary asset types, scan paths, bundles where used, duplicate-definition handling, and cooking rules. Do not require replacing the project's Asset Manager class. Do not silently modify project config.

A packaged test must resolve every demonstration asset from a cooked build. Never assume a valid editor path is proof that an asset will ship. Missing optional assets produce a clear fallback or failure, not a blocking synchronous load.

## 3.9 Multiplayer-aware without false replication claims

A subsystem coordinates state; the design must supply an actual replication transport when a feature advertises networking. Use a replicated actor/component owned appropriately for its traffic, not a Server RPC declared on an ordinary subsystem.

Epic documents actor-component replication through the owning replicated actor; both actor and component must be configured appropriately. [R6]

Base behavior must have an authority-aware execution path. Real client/server support is a separate verified capability with RPC authorization, late-join behavior, state reconciliation, and dedicated-server tests. “A validation method exists” is not multiplayer completion.

## 3.10 Resource leases and restoration

Use owner-scoped leases for streaming residency, temporary audio priority, input/camera control, activation pins, and other overlapping requests.

Define a Core-level `IDocPlayerControlProvider` contract for acquiring and releasing local-player control claims. The implementation belongs to the consuming project's adapter or an explicitly packaged shared adapter—not to a hard-coded Character subclass. Examples must include a usable reference adapter.

Inspection and Sequences must use the **same provider per local player** when both manipulate control. Releasing one claim recomputes remaining ownership; it must not restore a stale snapshot over a newer owner's camera, input, or pause choice. With no provider, features that require control locks fail explicitly or run in an explicitly configured no-control-mutation mode.

## 3.11 Threading and bounded work

Keep live UObject access and reflected gameplay callbacks on the game thread. Workers may process immutable, explicitly thread-safe data. A struct is not automatically safe because it is called a payload; it may contain UObject references or serializers that touch reflection state.

No full-world actor scan every frame. Prefer registration, change notifications, indexed queries, and budgeted updates. Timers may be appropriate but are not automatically cheaper or safer than one bounded subsystem update. Measure actual work.

## 3.12 Validation, accessibility, and graceful absence

Every plugin supplies actionable validation, named logs, inspectable state, and automated tests. Presentation must expose localized `FText`, normalized progress, and readable failure reasons without imposing a widget library.

Missing optional bridges must be distinguishable from invalid base configuration. A bridge's absence cannot prevent the base plugin from loading. Editor dashboards may show `Not Installed`; runtime APIs return `Unsupported` only for that missing capability.

---

# 4. Shared Core Plugin — DocModularCore

## 4.1 Purpose and size boundary

Provide only shared low-level contracts used by multiple systems. Core is not an event manager, save manager, interaction framework, or universal service locator.

Core may include common tag roots, result/error types, transient-handle primitives, stable identity structs, narrow interfaces, cancellation/lifetime helpers, and logging conventions. Put feature-specific payloads, calendars, definitions, and state machines in their owning plugins.

Do not place `FDocGameplayEventPayload` in Core merely because it appeared in the original suggestion. The event envelope belongs in `DocEvents`; a truly shared context/identity type may remain in Core. Moving a type that already shipped requires compatibility handling.

## 4.2 Required public contracts

| Contract | Minimum meaning |
|---|---|
| `FDocSystemResult` | Outcome, error tag, diagnostic, user-facing text, operation ID |
| `FDocGameplayContext` | Explicit world/instigator/target context; ownership and authority semantics |
| `FDocPersistentObjectId` | World namespace, stable instance scope, local object GUID |
| `FDocWorldObjectReference` | Persistent ID and optional weak runtime resolution; no pointer identity |
| Transient handle primitives | Opaque request ID plus generation, validity and equality semantics |
| `IDocPersistentIdentity` | Supplies persistent identity without requiring DocSave |
| `IDocGameplayTagProvider` | Read-only tags; separate explicit interface for authorized mutation |
| `IDocWorldStateProvider` | Narrow state query contract, not a mandatory state store |
| `IDocPlayerControlProvider` | Per-local-player lease contract for requested control capabilities |

Keep durable IDs distinct from transient handles. A serialized transient handle is invalid in a new session.

## 4.3 Persistent identity foundation

Define identity before other systems begin inventing their own GUID schemes:

```text
PersistentObjectId = (WorldNamespaceGuid, StableInstanceScope, LocalObjectGuid)
```

The instance scope must distinguish repeated placements of the same authored level/template, including nested instances. Use a stable saved hierarchy or a documented collision-resistant composition with collision validation; do not infer identity from transforms, actor labels, cell indices, or load order.

Authored local GUIDs are serialized with authored instances. Editor duplication creates a new authored identity where appropriate; PIE duplication preserves authored identity inside its isolated world context. Runtime-spawned objects receive authoritative IDs once and restore those IDs from saves.

## 4.4 Core editor and debug surface

An optional `DocModularCoreEditor` module may own the shared menu and a registration contract for feature-provided panels. Feature editor modules register/unregister their own panels; the Core editor must not import all feature modules to draw a dashboard.

Expose an optional `Project Settings → Plugins → Doc Modular` page for genuinely shared settings. Feature settings remain owned by features. Do not turn one global settings object into a hard dependency on all optional classes.

## 4.5 Acceptance

`CORE-01`: Core builds alone, with no feature plugin installed.  
`CORE-02`: Native tags register consistently in Editor and packaged targets.  
`CORE-03`: Handles reject stale/cross-world operations and tolerate repeated release.  
`CORE-04`: Persistent IDs distinguish repeated level instances and survive serialization.  
`CORE-05`: Two PIE worlds cannot read each other's registries or contexts.  
`CORE-06`: Public contracts compile from a separate consumer module; no private-header reliance.  
`CORE-07`: Editor menu registration works with zero, one, or multiple feature editors and unloads safely.

# 5. System 1 — DocInteraction

## 5.1 Goal and runtime types

Provide universal interactions between arbitrary actors without knowing whether the target is a door, item, machine, NPC, seat, vehicle, terminal, or custom object.

Required types: `UDocInteractionSubsystem`, `UDocInteractableComponent`, `UDocInteractorComponent`, `UDocInteractionTargetProvider`, `UDocInteractionCondition`, `UDocInteractionAction`, `UDocInteractionProfile`, and reflected interactable/interactor interfaces. Add explicit request, candidate, definition, session, and result structs.

The world subsystem coordinates sessions; interactor components own detection configuration; interactables supply immutable definitions. Do not make the subsystem own one global focused actor.

## 5.2 Definitions and detection

Each interaction definition contains a stable definition ID, semantic tag, localized display/prompt text, optional soft icon, input hint, execution mode, duration/repeat configuration, cancellation policy, concurrency policy, conditions, and ordered action definitions.

Preserve the initial tags: Use, Activate, Deactivate, Toggle, Open, Close, Pickup, Drop, Push, Pull, Rotate, Read, Inspect, Enter, Exit, Sit, Talk, Purchase, Hold, Repair, Unlock, and Custom under `Interaction.*`. A tag describes intent; `Interaction.Purchase` does not implement an economy.

Support LineTrace, SphereTrace, CapsuleTrace, Overlap, CursorTrace, Proximity, and ExplicitTarget providers. Build LineTrace and Overlap first, but do not mark the complete provider set delivered until the others are tested. Detection cadence, query channel/profile, range, ignored actors, and line-of-sight requirements are configurable.

Candidate selection must be deterministic: explicit eligibility, configured priority, score/distance, then stable tie-breaker. Deduplicate candidates returned by multiple providers. Use focus hysteresis where configured to prevent prompt flicker; expose the reason for rejection.

Detection is separate from execution. AI or scripted actors may submit explicit targets without a camera or input component.

## 5.3 Conditions and actions

`EvaluateInteraction(Context)` returns allowed/denied, failure tag, technical reason, and optional localized prompt. Built-ins cover required/blocked gameplay tags, distance, facing angle, cooldown, one-time use, authority, required object/interface, and custom Blueprint conditions.

Built-in actions cover authorized tag changes, interface invocation, owned activation/enabled-state changes, spawning/destruction, attach/detach, sound playback, animation invocation through an adapter, and custom Blueprint actions. Define spawn ownership and collision-failure results explicitly.

“Broadcast Event” and “Start Sequence” actions are implemented by `DocInteractionEvents` and `DocInteractionSequences`, respectively. Their absence cannot prevent basic interaction. Inspection launching follows the same bridge rule.

Use a mutable-state provider for tag changes and reversible control leases for temporary overrides. Never remove tags, disable components, or restore settings that belong to another owner.

## 5.4 Session and concurrency contract

```text
Request → Validate → Reserve → Execute / Hold → Commit → Completed
                 ↘ Rejected      ↘ Cancelled / Failed → Cleanup
```

Support Instant, HoldToComplete, Continuous, and Repeated modes. Hold progress uses a declared clock domain; default authoritative gameplay holds pause with gameplay time. UI progress may interpolate but never authorizes completion.

Revalidate critical conditions immediately before commit. For holds, also validate target existence, allowed distance, and reservation at a configurable cadence. Define continuous/repeated intervals, maximum duration where needed, cancellation, and what counts as completion.

Concurrency policies: exclusive target/definition, one session per interactor, or explicitly permitted shared use. An exclusive one-time action cannot be completed twice by simultaneous requesters.

Actions are **not automatically transactional**. Classify actions as reversible or irreversible; execute ordered actions with fail-fast results. Report partial execution honestly. Optional compensating actions must be explicit and tested; do not claim that destroying/spawning arbitrary actors can always be rolled back.

## 5.5 Public API and UI/input contract

Expose QueryCandidates, GetAvailableInteractions, RequestInteraction, CancelInteraction, GetSessionState, and session progress/completion/failure events. Queries do not start interactions.

UI state includes prompt, display name, icon, input hint, progress, availability, and failure reason. A generic example uses the project's binding of `IA_Interact`, next/previous selection, and cancel. Never install or clear global input mappings implicitly.

Blueprint quick flow:

```text
Attach Interactor → Configure Provider → Bind Existing Input
→ Request Selected Interaction → Observe Handle Events → Render Optional Prompt
```

## 5.6 Networking

Use an opt-in request component on an appropriately client-owned replicated actor; do not send a Server RPC through an unowned world target. The server resolves an allowed target, validates range/line of sight, definition, state, cooldown, ownership, reservation, and rate limits, then executes.

Use request IDs to reject duplicate completion. Never accept a client-provided condition pass, hold-complete timestamp, arbitrary UObject class, or arbitrary event payload as authority. Replicate resulting gameplay state through the responsible owner; cosmetics may be client-local.

## 5.7 Acceptance and debug evidence

`INT-01`: Blueprint-only target and non-Character interactor work without Events or UI.  
`INT-02`: Every promised provider works; overlapping providers produce one candidate.  
`INT-03`: Conditions return actionable reasons; a condition changed during hold blocks commit.  
`INT-04`: All four execution modes have success, cancellation, and target-destroyed tests.  
`INT-05`: Competing requests cannot double-commit an exclusive/one-time interaction.  
`INT-06`: Shared action assets do not leak state between sessions or players.  
`INT-07`: Partial action failure reports executed actions and performs only declared compensation.  
`INT-08`: RPC ownership, out-of-range rejection, duplicate requests, and late state updates are verified when networking is enabled.  
`INT-09`: Bridge actions are discoverable only when their bridges are installed.

Debugger: focused actor, candidates, provider, scores, selected definition, passed/failed conditions, progress, reservation owner, authority, request ID, and last result.

---

# 6. System 2 — DocEvents

## 6.1 Goal and runtime types

Provide a typed, Gameplay Tag-based event bus for unrelated systems in the **same explicit world context**. `UDocGameplayEventSubsystem : UWorldSubsystem` owns subscriptions and bounded retained values. Public types include `FDocGameplayEvent`, scope keys, subscription handles, and delayed-event handles.

“Global” means world-global by default—not every PIE instance, every server/client process, or every map across travel. Cross-world forwarding must be an explicit integration, never a static singleton.

## 6.2 Envelope, scope, and matching

The event envelope contains EventTag, scope, monotonic sequence number, timestamp with clock-domain metadata, optional weak sender/target references, optional persistent IDs, location, contextual tags, and a typed `FInstancedStruct` payload.

Retain simple FloatValue, IntValue, and NameValue compatibility/convenience fields only with a documented schema. Do not allow the same semantic field to disagree between the envelope and typed payload; choose one canonical representation per event type.

Scopes support World/Global, Actor, Component, Region, Team, and Custom. Region/team scopes use value identifiers and do not import Region or team-system classes. Scope matching is exact unless the subscription explicitly asks for broader behavior.

Each subscription chooses exact-tag or include-descendants matching. A parent subscription to `Event.World` may receive child events, but delivery to a single subscription happens once even when several matching tag conditions overlap.

Examples remain `Event.Actor.Spawned`, `Event.Actor.Destroyed`, `Event.Player.Interaction.Started`, `Event.Player.Interaction.Completed`, `Event.World.Region.Entered`, `Event.World.Region.Exited`, `Event.Time.HourChanged`, and sequence lifecycle events. Producers do not require Events unless a bridge is installed.

## 6.3 Delivery and mutation semantics

Delivery runs on the game thread in a documented stable order: configured listener priority followed by registration order. Take a safe dispatch snapshot; listeners may unsubscribe themselves or others during delivery. New listeners become eligible for subsequent dispatch, not retroactively halfway through the current event.

Nested broadcasts are queued and drained after the current event. Apply a configurable maximum event count/depth per drain, with diagnostics and a defined overflow policy; avoid infinite recursive event storms.

Owner-bound subscriptions use weak ownership and are cleaned on owner teardown. A one-shot wait unregisters after a match, timeout, cancellation, or world shutdown. Delayed events are cancellable and declare whether delay uses gameplay time or unscaled time.

## 6.4 Retained values are not automatic persistence

Selected tags may retain the latest payload per **exact tag + scope key**, with a revision. Provide explicit UpdateRetainedValue, GetLatestPayload, ClearRetainedValue, and optional replay-on-subscribe behavior.

Bound retained entry count, payload size, and optional time-to-live. `State.Power.On` or `State.Alert.High` may be retained, but this is runtime state only. Disk persistence requires a Save bridge and a supported schema; networking requires a separate authorized transport.

Retained UObject references must not keep streamed worlds alive. Use weak runtime references or persistent IDs; validate allowed payload contents. Unsupported payload types fail clearly rather than silently dropping fields.

`HasEventOccurred` means occurrence within the configured, bounded observation/history policy. Return Unknown/NotTracked when history was never retained or was evicted; do not claim knowledge of all past events from a bounded ring buffer.

## 6.5 Public API

Expose BroadcastEvent, BroadcastNextFrame, BroadcastAfterDelay, CancelScheduledEvent, Subscribe, Unsubscribe, HasEventOccurred, GetLastPayload/GetLatestPayload with explicit retention semantics, and Blueprint Listen/Wait async nodes.

No generic client-to-server broadcast endpoint. A bridge that transports a small allowlisted event schema must authorize it, impose size/rate limits, and distinguish notifications from authoritative state.

## 6.6 Acceptance and debugging

`EVT-01`: C++ and Blueprint broadcast/subscription work with exact and hierarchical tags.  
`EVT-02`: Scope isolation works across actor, custom scope, and two PIE worlds.  
`EVT-03`: Self-unsubscribe, unsubscribe-other, and subscribe-during-dispatch are deterministic.  
`EVT-04`: Reentrant broadcasts preserve order and terminate under the configured drain limit.  
`EVT-05`: Owner destruction and world teardown leave no active callbacks or retained strong world references.  
`EVT-06`: Delayed and one-shot events have cancellation, timeout, and pause-clock tests.  
`EVT-07`: Retained revision, replay, eviction, and unknown-history behavior are tested.  
`EVT-08`: Typed payload mismatch is rejected with a schema/type diagnostic.

The event monitor uses a bounded ring buffer, filters by tag/scope, and shows timestamp, sender/target identity, payload type, listener count, operation correlation, and dropped/evicted counts. Debug capture is opt-in and must not log private payload content by default.

---

# 7. System 3 — DocRegions

## 7.1 Goal and runtime types

Represent logical spatial regions and expose membership. Regions answer **where**, not **what should happen there**.

Use `UDocRegionSubsystem`, `ADocRegionVolume`, `UDocRegionComponent`, `UDocRegionDefinition`, a shape-provider interface, and explicit tracked-observer registration. Box, Sphere, Capsule, and Volume are base providers; Spline Corridor, Polygon, and arbitrary custom geometry remain separately tracked extensions as in the original plan.

A region component associates metadata and a shape provider with an arbitrary actor. It does not assume every `UActorComponent` has its own transform or collision shape. Supply a generic box-region example that does not require custom brush authoring.

## 7.2 Definitions and instance identity

A definition contains RegionTag, localized DisplayName, Priority, RegionTraits, optional explicit parent relationship, and generic/soft metadata for environment, audio, population, streaming, and custom consumers.

A definition tag may be shared by several placed region instances. Each runtime instance has a stable RegionInstanceId scoped to its world instance. Queries distinguish definition tag from instance identity.

Hierarchical tags such as `Region.World.ZoneA.Building.RoomA` express semantic organization, not proof of physical containment. Validate explicit hierarchy cycles. Track geometrically present regions separately from optionally inferred logical ancestors.

Metadata must not hard-reference optional consumer classes. Typed audio/streaming metadata belongs to its bridge or a validated generic extension record.

## 7.3 Membership and overlap policy

Track explicitly registered observers, not every actor in the world. Define each observer's membership test: reference point by default, optional bounds-overlap policy. Document boundary inclusion and tolerance.

Deduplicate primitive/component overlap notifications into one actor-region membership. One primitive exiting must not produce an actor exit while another qualifying primitive still overlaps. Handle teleport, spawn inside a volume, collision disabled, destroyed observers, and moving region shapes.

Deterministic primary selection: higher explicit priority, then configured logical depth/specificity rule, then stable RegionInstanceId. “Specificity” must be defined by the policy; do not rely on random registration order or tag-container iteration.

Recompute the affected membership set before emitting notifications. Emit exits, entries, and one final primary-region-changed event in documented order. Provide a reason such as Movement, Teleport, RegionUnloaded, or ObserverRemoved.

## 7.4 Queries and streaming semantics

Expose GetRegionAtLocation, GetRegionsAtLocation, GetRegionsForActor, GetPrimaryRegionForActor, IsActorInRegion, GetActorsInRegion, and FindRegionsByTag. Keep the original singular FindRegionByTag as an explicitly unique query or report ambiguity.

`GetActorsInRegion` returns tracked, loaded observers—not an omniscient list of unloaded entities. Location queries operate on registered geometry unless an optional metadata backend supplies additional coverage. Report unavailable coverage rather than guessing that an unloaded region is absent from the authored world.

Use a spatial index or equivalent bounded candidate selection. Update it when shapes move or the world origin changes. Do not cache raw references to unloaded region actors.

Definition metadata may survive unload; live membership cannot claim an unavailable volume is still being geometrically evaluated. Re-entry after reload uses stable instance identity and avoids stale duplicates.

## 7.5 Events, integrations, and acceptance

Expose OnRegionEntered, OnRegionExited, and OnPrimaryRegionChanged as native/Blueprint delegates. `DocRegionsEvents` mirrors them into the event bus; other bridges consume these delegates directly without making Events mandatory.

`REG-01`: Nested and overlapping shapes produce stable primary selection regardless of registration order.  
`REG-02`: Two primitives from one observer do not double-enter or exit early.  
`REG-03`: Spawn-inside, teleport, moving volume, and observer destruction resolve correctly.  
`REG-04`: Unload/reload produces reasoned transitions without dangling references.  
`REG-05`: Tag queries handle multiple instances explicitly.  
`REG-06`: Parent-cycle and ambiguous-definition validation fails with object/asset context.  
`REG-07`: Base Regions builds without Events, Audio, Streaming, or World Partition bridges.

Debug view: bounds, tag, instance ID, parent relationship, priority, memberships, primary-region rationale, candidate count, and geometry/metadata availability.

---

# 8. System 4 — DocStreaming

## 8.1 Goal and backend boundaries

Provide a logical request layer over native Unreal streaming. Never replace the engine's world streaming, package loader, HLOD, or renderer.

Use `UDocWorldStreamingSubsystem`, `UDocStreamingChunkDefinition`, `UDocStreamingSourceComponent`, a backend interface, chunk-instance keys, request leases, and async Blueprint actions.

**Base backend:** native streaming levels/dynamic level instances supported by verified 5.8.3 runtime APIs. **Bridge backends:** World Partition Streaming Sources and Runtime Data Layers. Resolve exact API and module ownership at the engine gate.

Packed Level Actors remain supported **as content governed by their native owning world/level**, not as an invented universal `LoadPackedLevelActor` streaming mechanism. Epic distinguishes rendering-oriented packed content from Level Instance runtime streaming modes; some embedded Level Instances do not remain standalone runtime actors. [R7]

## 8.2 Chunk definition and identity

A definition contains ChunkTag, backend kind, soft level/map reference or bridge-owned configuration, dependencies, priority, load/unload/preload thresholds, keep-loaded policy, world-state tags, and timeout/readiness policy.

Distinguish:

```text
ChunkDefinitionId       = What content/configuration is requested?
ChunkInstanceKey        = Which stable placement/world instance?
StreamingRequestHandle  = Which caller owns this lease?
```

A repeated level may have several transforms/instance scopes. A single tag cannot accidentally collapse them into one runtime instance. Reject incompatible requests for the same instance key.

All spatial thresholds use explicitly labeled Unreal world units in serialized configuration. Editor UI may provide a meters conversion, but never silently mix meters and centimeters.

## 8.3 Request ownership and dependency algorithm

RequestChunk returns a lease handle, not a success boolean. ReleaseChunk releases that handle. Keep a caller/owner table so abandoned requests can be cleaned automatically.

Before requesting native resources, validate the entire dependency graph, missing definitions, and cycles. Acquire dependencies in topological order. Shared dependencies are held by independent leases. On failure, unwind only leases acquired for that request.

Duplicate release is harmless. Releasing one requester cannot unload content still needed by another requester, dependency, keep-loaded policy, or external engine owner. Report **Released but Still Resident** separately from native content actually unloaded.

Priority affects request arbitration; it cannot override another owner's valid residency lease. Cancellation during loading must not turn a later engine callback into a successful cancelled request.

## 8.4 Observable state and readiness

```text
Unrequested → Requested → Loading → Loaded → Activating → Ready
                                          ↘ Failed / TimedOut
Ready → ReleaseRequested → Unloading → Unrequested
```

Keep **desired state**, **observed backend state**, and **request outcome** distinct. A request may fail while shared content remains resident for another owner.

Define readiness per backend: loaded is not always visible/activated, and visible is not a universal guarantee of navigation/collision/gameplay readiness. Supply optional readiness probes for consumer prerequisites. Only claim the guarantee the chosen probe actually validates.

A progress result contains phase, completed/total measurable units where available, and a `bProgressKnown` flag. Unknown native progress must not become a fabricated percentage.

## 8.5 World Partition and world-state requests

The World Partition bridge supports source positions/shapes and Runtime Data Layer state orchestration. Runtime Data Layers have distinct requested/actual states and are associated with world instances; their activation and spatial streaming must be reasoned about together. [R8]

Do not assume activating a Data Layer alone loads all spatial cells needed by a distant sequence or teleport. Acquire the needed streaming-source residency and layer state, then wait for the backend's verified readiness condition for the requested region/content.

Use owner-scoped world-state leases for normal/damaged/destroyed or other mutually exclusive variants. Define conflict groups and effective-state arbitration. Releasing one request recomputes the desired state instead of blindly deactivating a layer required by another owner or parent.

Do not promise that every layered world-state change is atomic. Expose Transitioning/Failed and configured fallback behavior; consumers must wait for Ready before depending on the new state.

## 8.6 Public API and acceptance

Expose RequestChunk, ReleaseChunk, RequestWorldState, ReleaseWorldState, GetChunkStatus, WaitUntilChunkReady, and cancellation. Keep ActivateWorldState/DeactivateWorldState convenience calls only if they preserve explicit caller ownership.

`STR-01`: Two requesters share content; releasing either alone does not unload it.  
`STR-02`: Missing dependency/cycle fails before partial activation; acquired leases unwind on later failure.  
`STR-03`: Cancel/re-request during load ignores stale completion callbacks.  
`STR-04`: Repeated level instances remain distinct and restore stable instance identity.  
`STR-05`: Loaded, visible/activated, ready, and released-but-resident are distinguishable.  
`STR-06`: Base level-instance path loads and unloads in a cooked Win64 host.  
`STR-07 [BRIDGE]`: A real Runtime Data Layer path and distant streaming-source readiness path pass in a cooked World Partition fixture.  
`STR-08`: World travel and requester destruction release owned resources without dangling handles.

Debugger: definition/instance ID, backend, desired/observed states, request owners, dependency leases, active layers/sources, measurable progress, readiness probe, and last failure.

# 9. System 5 — DocTime

## 9.1 Goal and runtime types

Provide deterministic simulated time and configurable calendars independently of rendering. Use `UDocWorldTimeSubsystem`, `UDocTimeConfiguration`, calendar/period definitions, schedule entries, and explicit clock snapshots.

The subsystem does not search for or control sky lights, directional lights, weather actors, or NPC schedules. Those consumers use outputs or bridges.

## 9.2 Representation and advancement

Retain `int64 TotalSimulatedMilliseconds` as the public canonical simulation time. Also maintain the remainder required by the chosen fixed-point/rational advancement algorithm.

Do not implement time as `Total += int64(DeltaSeconds * Scale * 1000)` each frame: discarding the fraction at every update creates avoidable loss. Quantize input duration through a documented adapter and carry the remainder. Record the precision of time scale, overflow checks, and rounding rule.

Provide a pure advancement function driven by integer elapsed units and explicit scale. Tests compare different partitions of the **same integer input duration** against an integer/rational reference. This proves the defined arithmetic, not universal bitwise determinism across operating systems or independently sampled wall clocks.

Reject negative scale by default; reverse-time support is not implied. Large jumps use explicit SetTime/AdvanceTime operations with defined event policy.

## 9.3 Calendar and periods

Configure SecondsPerMinute, MinutesPerHour, HoursPerDay, DaysPerWeek, MonthsPerYear, and month lengths. Validate positive values, consistent month tables, and safe arithmetic. Supply an explicit epoch and minimum/maximum supported dates.

Derive year, month, day, hour, minute, second, day-of-week, and normalized day time. Provide a simple uniform custom calendar first; variable month lengths belong to the completed configurable-calendar capability. Leap/astronomical rules require an explicit provider and are not silently assumed.

Periods include Dawn, Day, Dusk, Night, and custom names, plus boundaries such as Sunrise, Noon, Sunset, and Midnight. Define wraparound periods, overlap priority, and empty/invalid intervals. These are configurable gameplay periods, not automatically real-world astronomy.

## 9.4 Boundary and schedule policy

Expose MinuteChanged, HourChanged, DayChanged, period entered/exited, and explicit TimeJumped/TimeRestored events.

For forward advancement, use a documented half-open boundary convention such as `(oldTime, newTime]`. A boundary is emitted once per forward crossing within a traversal epoch. Seeking backward or restoring a save does not automatically replay all historical gameplay effects.

Choose per operation: FireCrossedBoundaries, CoalesceWithCount, or SuppressAndNotifyJump. A multi-year skip must not synchronously enqueue millions of per-minute events. Report skipped/coalesced counts and provide schedule-range queries for consumers that need reconciliation.

Schedules contain stable entry IDs, tags, recurrence rules, and configured times. Expose next-entry and range queries. The Time plugin does not open a shop or lock a door itself.

## 9.5 Control, celestial output, and networking

Expose SetTime, AdvanceTime, SkipToTime, SkipToNextPeriod, PauseTime, ResumeTime, SetTimeScale, GetCalendarTime, GetPeriod, CaptureClockState, and RestoreClockState. Multiple pause owners use tokens; one owner cannot resume time still paused by another.

A saved clock snapshot includes canonical time, remainder, scale, calendar/config identity, and algorithm/schema version. Transient pause owners and runtime subscription handles are not blindly restored.

Optional celestial output may provide NormalizedSolarTime, SunElevation, SunAzimuth, MoonPhase, and NormalizedDay through a replaceable provider. A stylized day curve is acceptable when labeled. Physically meaningful solar/lunar output requires the corresponding geographic/date/model inputs; do not advertise it from normalized time alone.

A network adapter replicates authoritative clock samples, scale, pause state, and revision through a valid actor/component. Clients interpolate presentation and snap/smooth under a documented correction policy; they do not independently trigger authoritative schedule effects.

## 9.6 Acceptance

`TIM-01`: Integer advancement matches its reference across different input partitions, scales, and long durations.  
`TIM-02`: Remainders survive save/restore and do not disappear on scale changes.  
`TIM-03`: Calendar rollover, variable month lengths, custom calendars, invalid values, and overflow are covered.  
`TIM-04`: Boundaries fire once per defined crossing; backward seek, restore, and large skips follow explicit policy.  
`TIM-05`: Nested pause tokens and time-scale changes behave independently.  
`TIM-06`: Blueprint queries and schedule-range queries match native results.  
`TIM-07 [NETWORK]`: Late join and authoritative clock correction are tested without duplicate gameplay triggers.

---

# 10. System 6 — DocAdaptiveAudio

## 10.1 Goal and runtime types

Provide a reusable director for music, ambience, environmental layers, random one-shots, stingers, and state-driven transitions. Use `UDocAdaptiveAudioSubsystem`, `UDocAudioStateProfile`, `UDocAudioLayerDefinition`, request handles, and a native playback adapter.

Keep the world subsystem as the logical director. Track listener/presentation policy explicitly. The default split-screen policy is one shared output mix with a configured primary-listener/combined-state rule; do not promise independent per-player music outputs on one audio device without an implemented output backend.

Dedicated servers may track replicated logical audio cues where needed but must not allocate playback components or audio clocks unnecessarily.

## 10.2 Base playback and optional integrations

Base playback accepts `USoundBase` assets, including compatible Sound Waves and Sound Cues. MetaSound assets may be supplied through their compatible sound interface where supported; direct MetaSound-specific controls require the bridge. Verify actual playback in a cooked test rather than inferring support from inheritance alone.

Sound Classes, Sound Mixes, and Submixes remain compatible routing targets. Audio Modulation and typed MetaSound controls are isolated behind their bridges. No plugin replaces Unreal's DSP.

Quartz is a separate scheduling integration. Epic describes it as an audio scheduling system that can place playback relative to an audio clock instead of relying on game-thread timing alone. Use that mechanism for verified musical timing; do not label a game-thread timer “sample accurate.” [R9]

## 10.3 Profiles, channels, and state inputs

Channels include `Audio.Music`, `Audio.Ambience`, `Audio.Environment`, `Audio.Tension`, `Audio.Combat`, `Audio.Stinger`, `Audio.UI`, `Audio.DialogueSupport`, and `Audio.Custom`.

Profiles contain a stable tag/ID, priority, layered soft sound references, random one-shots, fade/crossfade curves and durations, loop rules, quantization preference, conditions, and voice/concurrency limits. Layer definitions may describe base, percussion, melody, tension, combat, or custom layers.

Inputs are generic gameplay/world tags, region tags, time periods, threat/combat/weather state, and custom context. Adapters supply them; the base imports none of those gameplay systems.

Define blend rules per channel: Exclusive, Layered, or AdditiveOneShot as appropriate. A high-priority music request must not automatically silence dialogue or unrelated UI audio.

## 10.4 Arbitration and transition lifecycle

RequestAudioState returns an owner-scoped lease. Resolve priority, explicit tie-breaker, and channel policy deterministically. Releasing an override recomputes currently valid requests; it does not restore a removed or expired request merely because it was once underneath.

```text
Requested → AssetPreloading → Scheduled → Playing / Fading → Released
                         ↘ Failed / Cancelled / TimedOut
```

Superseded asynchronous loads and scheduled playback must not resurrect old profiles. Define behavior when a requested sound fails: retain previous valid music, use a configured fallback, or become silent with an explicit result.

For aligned stems, validate tempo, meter, loop length, start offset, and compatible quantization. Bar/phrase timing needs metadata. Preserve or deliberately restart phase under a documented resume policy; do not promise phase continuity after a source is stopped or evicted.

Immediate/faded transitions work without Quartz. Beat, Bar, Phrase, and Custom musical boundaries require the scheduling capability and return a clear fallback/result when unavailable.

## 10.5 Emitters and control ownership

Bound active ambient components, random one-shot rate, pending transitions, and retained audio assets. Use deterministic seeded selection for reproducible tests without claiming identical audio rendering across machines.

Pool components only with full parameter/delegate/attachment reset. Respect engine concurrency and attenuation settings. Release only owned mix/control overrides; do not restore a global Sound Mix snapshot over another system's changes.

Expose current profiles/layers, effective requests, pending transitions, voice counts, and requested versus actual timing. Provide a usable muted/no-audio-device diagnostic path for automated hosts.

## 10.6 Acceptance

`AUD-01`: Independent channels, fades, and priority restoration work with no Regions, Time, or Events plugins.  
`AUD-02`: Superseded loads, released owners, and failed assets do not start stale audio.  
`AUD-03`: Layer enable/disable, looping, bounded one-shots, and voice limits are verified.  
`AUD-04`: Sound Wave/Sound Cue and compatible MetaSound playback pass in the declared capability profile.  
`AUD-05 [BRIDGE]`: Quartz transitions meet the recorded scheduling tolerance in an actual audio-enabled run.  
`AUD-06`: Device unavailable, world teardown, and repeated profile changes leak no owned resources.  
`AUD-07`: Split-screen behavior follows the documented shared-output policy.

---

# 11. System 7 — DocSequences

## 11.1 Goal and runtime types

Centralize logical Level Sequence playback and set-piece orchestration without requiring manually located sequence actors. Use `UDocSequenceSubsystem`, `UDocSequenceDefinition`, playback session handles, binding providers, and reflected participant interfaces.

A sequence definition contains SequenceTag, soft Level Sequence reference, priority, queue/interrupt policy, CanSkip, CanReplay, Interruptible, restore-state policy, requested camera/input/AI control capabilities, participant binding roles, prerequisites, and completion/failure notifications.

Required world chunks are bridge-owned prerequisites, not a typed reference to DocStreaming in the base definition. The base supports generic prerequisite providers and functions without Streaming.

## 11.2 Session state and API

```text
Requested → Queued / Loading → ResolvingBindings → Preparing → Playing
                                                            ↔ Paused
Playing → Completed / Skipped / Interrupted / Failed → Restoring → Finished
```

Expose PlaySequence, PauseSequence, ResumeSequence, StopSequence, SkipSequence, RestartSequence, JumpToMarker, IsSequencePlaying, GetSessionState, and terminal results. A session ID is distinct from the logical sequence tag; replay creates a new session.

Arbitration supports Reject, Queue, Interrupt, and ReplaceLowerPriority. Bound queue length, define equal-priority order, remove dead owners, and make queued cancellation observable. Evaluate replay restrictions against explicit persistent state only when a provider supplies it.

## 11.3 Bindings, markers, and gameplay effects

Bind participants through role tags/interfaces or explicit supplied objects—not global actor-label searches. Validate required versus optional bindings. Handle participant unload/destruction before and during playback using Fail, WaitWithTimeout, or explicitly allowed MissingOptional policies.

Separate named timeline markers used for navigation from configured event callbacks used for gameplay. `JumpToMarker` does not by itself imply execution of every crossed gameplay effect. Verify the appropriate 5.8.3 MovieScene/LevelSequence mechanism and record seek/scrub behavior.

Provide marker/event tags such as DoorOpen, Explosion, and GameplayResume through an explicit mapping. Classify them as presentation-only or authoritative. Irreversible gameplay effects use idempotent effect IDs keyed to sequence session and effect identity; skip, replay, seek, and late join must not double-apply them.

A Completion Event or Failure Event always exists as a delegate/result; mirroring through DocEvents requires the Events bridge.

## 11.4 Skip and restoration policies

Skipping may FinishAtEndState, CancelAndRestore, or use a definition-provided reconciliation policy. Decide whether required gameplay milestones are committed, omitted, or reconciled; do not silently rely on playback position changes.

Acquire camera/input/AI control through declared participant/control providers. Track only owned claims and release them on every terminal path, including failed asset load, binding failure, cancellation, disconnect, and world teardown.

Do not assume a captured camera target is still alive. If another control owner has taken priority, release the sequence claim and let the provider compute the effective state. Never globally unpause all AI or re-enable all input.

## 11.5 Streaming and networking

`DocSequencesStreaming` acquires chunk leases, waits for readiness, holds them for the required lifetime, and releases them during cleanup. It uses the same cancellation generation as the sequence request.

For networked sequences, distinguish server-authoritative gameplay from each client's presentation. Replicate a session ID, state/revision, authoritative start time, and binding identifiers as appropriate. Define late-join seeking and skip voting/authorization. Do not replicate every frame or grant world mutation because a client skipped a cinematic.

## 11.6 Acceptance

`SEQ-01`: Playback by tag, soft loading, binding roles, and all public controls work.  
`SEQ-02`: Queue/interrupt/reject behavior is deterministic and cancellation cleans queued entries.  
`SEQ-03`: Missing participant, missing asset, and timeout produce one terminal result.  
`SEQ-04`: Skip/seek/replay follows explicit effect policy without duplicate irreversible effects.  
`SEQ-05`: Camera/input/AI claims release cleanly and do not override a newer owner.  
`SEQ-06 [BRIDGE]`: Required streaming residency is held through playback and released on every failure path.  
`SEQ-07 [NETWORK]`: Late join and authorized skip reconcile presentation and gameplay state.

---

# 12. System 8 — DocInspection

## 12.1 Goal and runtime types

Provide per-player inspection of 3D objects, documents, images, books, audio logs, video, world details, and custom content. Use `UDocInspectionSubsystem : ULocalPlayerSubsystem`, `UDocInspectableComponent`, `UDocInspectionDefinition`, session handles, and presentation-provider interfaces.

Preserve tags under `Inspection.Object3D`, Document, Image, Book, Audio, Video, WorldDetail, and Custom. Mesh/image/document sessions are base behavior; media-specific transport/decoding is supplied by `DocInspectionMedia`. Video remains a tracked original capability, not a forgotten placeholder.

## 12.2 Definitions and content models

Definition fields include stable InspectionId, localized title/description/text, content type, optional soft mesh/texture/audio/video assets or provider configuration, camera constraints, rotation/pan/zoom limits, page data, focus points, and semantic tags.

Keep per-player discoveries and session state outside shared definitions. Local-player identity is not a persistent account identity; projects supply a stable player/profile key when discoveries are saved.

Text supports localization, readable scaling, multi-page order, scrolling, page turns, and optional high-contrast presentation metadata. Do not encode document content only as an unreadable texture when accessible text is available.

## 12.3 World versus preview mode

WorldInspection observes the live object in place. PreviewInspection renders a presentation copy without moving, attaching, disabling, or rotating the authoritative world actor.

Use an actually runtime-safe rendering path. Do not depend on UnrealEd or editor-only advanced preview-scene utilities in packaged code. A runtime preview actor/scene-capture implementation must isolate inspected content, own its render target/resources, and throttle or stop capture when unchanged or closed.

Provide bounded render-target size, update cadence, lighting setup, mesh loading policy, and skeletal/animated preview limitations. A preview copy must not execute arbitrary gameplay BeginPlay behavior from a live actor class; prefer explicit presentation assets/providers.

## 12.4 Session, input, and discovery

```text
Opening → Loading → Active → Closing → Closed
                    ↘ Failed / Cancelled → Cleanup
```

Default to one inspection session per local player, with configurable replace/reject behavior. A missing local player is an explicit error, not a fallback to index zero.

Use temporary Enhanced Input contexts through ownership-aware integration. Enhanced Input supports dynamically applied contexts; preserve the project's existing mapping setup. [R10]

Support Rotate, Pan, Zoom, Next, Previous, Accept, Back, and ActivateFocusPoint. Add only owned contexts/bindings, remove only what was actually acquired, and never call a global clear-all to close inspection. Contexts already installed by another owner must remain installed.

Camera/input claims use the shared per-player control provider. World pause is opt-in and coordinated; closing one player's inspection must not resume another player's pause or disrupt a sequence.

Focus points have stable IDs, reveal text, dependency/unlock rules, and idempotent discovery events. Tag grants and world rewards use an authorized provider or bridge. Merely inspecting on a client cannot grant arbitrary authoritative state.

## 12.5 Audio, video, and UI

Audio logs support play/pause/resume, seeking when the selected source supports it, subtitles, transcript, and optional continue-after-closing. Continued audio transfers to a deliberately longer-lived owner; it must not outlive a destroyed session accidentally.

Media integration reports loading/buffering/unsupported-codec/seek failure. Do not claim all formats can scrub. External URLs are opt-in, validated against configured schemes/hosts, and not automatically opened from arbitrary metadata. Close media resources during teardown.

The runtime exposes a complete view model and presentation events. UMG examples and the CommonUI bridge demonstrate them, but neither becomes a mandatory runtime framework. Documents/images/books and real audio/video playback must be verified in the appropriate example/bridge profile.

## 12.6 Acceptance

`INS-01`: World and preview 3D inspection work without changing the live object's transform/state.  
`INS-02`: Document, image, and multi-page book content render through a generic example front end.  
`INS-03`: Two local players have independent sessions, selection, discoveries, and input ownership.  
`INS-04`: Opening failure, closing during load, travel, and player removal release all resources.  
`INS-05`: Focus-point unlocks/discovery are idempotent and separate from shared asset state.  
`INS-06 [BRIDGE]`: Audio/video playback, supported seeking, transcripts, and continue-after-close ownership are tested.  
`INS-07`: Base builds without CommonUI, Media bridge, Interaction, or Events.  
`INS-08`: Coexistence with a sequence does not clobber camera/input control.

---

# 13. System 9 — DocSave

## 13.1 Goal and runtime types

Provide robust versioned world/player persistence across map travel, streamed content, dynamic objects, and schema changes. Use `UDocSaveGameSubsystem : UGameInstanceSubsystem`, `UDocSaveableComponent`, save-participant interfaces, a storage backend, migration registry, and world-scoped registration contexts.

The GameInstance service owns slot operations and plain records. It must not keep old worlds alive. Serialization knows record types and versions, not the gameplay meaning of inventory, damage, animation, or quest state.

## 13.2 Stable identity and authored content

Use the Core composite persistent identity. Never use actor names, labels, array indices, pointers, spawn order, transient NetGUIDs, or HISM indices as saved identity.

Generate authored identities through explicit editor-safe creation/duplication paths and serialize them. Do not regenerate them on every construction-script run, BeginPlay, reload, or PIE copy. Duplicate-ID validation is mandatory; runtime conflicts fail visibly rather than assigning a fresh ID and hiding corrupted continuity.

Repeated and nested level instances require stable instance scopes. Runtime-generated instance scopes must themselves be persisted by the world generator/owner. Define a policy for authored content removed or renamed between game versions.

Each independently saved component needs a stable component key or GUID within its actor. A user-friendly component name alone is not a durable key across renames unless a migration/redirect preserves it.

## 13.3 Durable record format

Define a versioned envelope, not a raw memory dump:

```text
SaveHeader:
  Magic, FormatVersion, SuiteVersion, ProducerGameVersion
  WorldNamespace, SaveRevision, Timestamp, CompressionMode
  PayloadByteCount, UncompressedByteCount, IntegrityAlgorithm, IntegrityValue

ObjectRecord:
  PersistentObjectId, RecordSchemaId, RecordSchemaVersion
  AuthoredOrRuntimeSpawned, AllowedClassId / SoftClassPath
  Transform, OwningInstanceOrChunkKey, LifecycleState
  ComponentRecords[StableComponentId, TypeId, Version, EncodedPayload]
```

Runtime capture may use `FInstancedStruct`; durable storage requires an explicit serializer and schema/type registry. Do not assume a reflected struct with arbitrary object fields is portable or safe to deserialize later. Save object references as resolvable persistent IDs or approved asset identifiers.

Persist destroyed tombstones, enabled state, transforms, tags, and arbitrary participant payloads where requested. Gameplay implementations own valid restore semantics for animation and other transient mechanisms.

## 13.4 Unload is not destruction

Track lifecycle transitions explicitly: Registered, StreamedOut, Virtualized, IntentionallyDestroyed, and WorldTeardown. **EndPlay alone is not proof of intentional destruction.** Activation/pooling/streaming must never generate a destroyed tombstone simply because an actor instance disappeared.

Provide an authoritative MarkPersistentlyDestroyed operation and a documented participant lifecycle notification. Ordinary unregistration removes a live reference while retaining the latest record. Capture dirty state before participant resources become invalid; late teardown callbacks may be too late.

Maintain records for already-unloaded content. Saving the currently loaded actors alone is insufficient. A dirty-state journal or equivalent retained-record store combines loaded snapshots, unloaded records, and tombstones.

## 13.5 Consistent capture and asynchronous storage

Pipeline:

```text
Validate Request → Establish Snapshot Revision → Capture / Encode on Game Thread
→ Freeze Immutable Bytes → Worker Compression / Integrity / I/O
→ Commit Under Slot Serialization → Publish Result on Game Thread
```

A bounded snapshot must have a declared consistency model. Begin with a short coordinated capture barrier for cooperative participants. If later spreading capture over frames, use a journal/revision strategy or document weaker consistency; do not call an arbitrary multi-frame sweep an atomic snapshot.

Epic's `AsyncSaveGameToSlot` documentation states that serialization occurs on the game thread, platform-specific writing on a worker, and completion on the game thread. Asynchronous storage does not automatically remove capture/serialization hitches. [R11]

Use a small-snapshot `USaveGame` backend where suitable, or an explicit byte-envelope backend when stronger record/migration/integrity control is required. Do not invent a second incompatible save path without documenting which backend meets each guarantee. Worker code never traverses arbitrary live UObjects or reflected payload graphs.

Serialize writes per slot/profile. Coalesce compatible autosave requests, but give every caller a completion result tied to the actual committed revision. Prevent an older save from overwriting a newer revision. Define cancellation before and after the commit point; a committed save cannot honestly report that no data was written.

## 13.6 Load and restoration lifecycle

```text
Read → Validate Bounds/Integrity → Decode/Migrate → Resolve World/Schema
→ Stage Records → Register/Spawn Participants → Apply State
→ Resolve Cross-References → Release Restore Barrier → Ready
```

Use asynchronous approved-class/asset loading. Runtime-spawned actors receive their saved identity through deferred spawn/setup before normal participation; avoid duplicate spawns when the same logical object is already registered.

Actors with tombstones stay suppressed under the documented participant initialization contract. Apply state at a verified safe lifecycle point and keep cooperative participants inactive until restore completes. Do not promise that a component can retroactively prevent arbitrary host actor BeginPlay side effects.

Resolve cross-object references in a second phase. Unloaded references remain unresolved handles until registration; they must not force-load an entire world. Unknown optional payloads may be preserved/skipped according to policy; missing critical payloads fail restoration visibly.

Restoring must be idempotent for a given save revision. Prevent restore-generated events from triggering autosave loops or regranting rewards.

## 13.7 Migration, integrity, and slot safety

Support Manual, Auto, Checkpoint, Quick, Profile, and World slot categories without enforcing a slot UI or hard-coded slot name. Distinguish persistent player/profile identity from machine-local player index.

Version the outer format and each payload schema. Apply deterministic migration chains; keep old fixture saves. Reject unsupported future versions without modifying the original file. Never “recover” by silently starting a new game and overwriting the failed save.

A reliable local-file backend writes a temporary file, verifies it, and commits through the backend's supported replacement strategy while retaining a last-known-good backup. Advertise atomic replacement only for a backend where the guarantee is implemented and fault-tested. Checksums detect corruption; they do not prove authenticity or prevent cheating.

Validate lengths/counts before allocation, cap decompression output, sanitize slot identifiers, and reject path traversal. Resolve only registered/allowlisted classes and payload types; a save file must not authorize arbitrary class loading or arbitrary file access.

## 13.8 Acceptance

`SAV-01`: Authored identity survives editor save/reopen, PIE, and stream reload; duplicate detection catches conflicts.  
`SAV-02`: Repeated level instances and component identities remain distinct across restore.  
`SAV-03`: Dynamic spawn, destroyed tombstone, unloaded record, and virtualized object restore correctly.  
`SAV-04`: References resolve in two phases without loading unrelated content or duplicating actors.  
`SAV-05`: Old fixtures migrate; future/unknown critical schemas fail without file mutation.  
`SAV-06`: Concurrent autosave/manual requests commit monotonic revisions and notify all callers correctly.  
`SAV-07`: Fault injection at write/replace boundaries preserves a recoverable valid save.  
`SAV-08`: Truncation, oversized counts, decompression limits, forbidden classes, and unsafe slot paths are rejected.  
`SAV-09`: Travel during save/load does not use stale UObjects or apply data to the wrong world.  
`SAV-10`: Restore is idempotent and does not trigger gameplay rewards/autosave recursion.

---

# 14. System 10 — DocWorldActivation

## 14.1 Goal and runtime types

Provide gameplay-level LOD for **loaded logical objects**. Streaming asks whether content should be loaded; Activation asks how expensive its loaded gameplay representation should be.

Use `UDocWorldActivationSubsystem`, `UDocWorldActivationComponent`, `UDocActivationPolicy`, `UDocActivationProfile`, registered logical-object records, relevance-source providers, and representation adapters.

The base supplies safe loaded-actor state/tick policies. Actor removal, ISM/HISM representation, Mass entities, and pooled replacement require an explicit adapter with a complete lifecycle/state contract.

## 14.2 Tiers and capabilities

Preserve semantic tier tags:

| Tier | Intended behavior | Requirement |
|---|---|---|
| `Activation.Dormant` | Minimal loaded-actor activity, or no actor plus retained record | No-actor form requires an explicit virtualization provider |
| `Activation.Representation` | Cheap non-gameplay representation | ISM/HISM or other adapter required |
| `Activation.Lightweight` | Reduced update rate and limited optional simulation | Base can support on eligible actors |
| `Activation.Active` | Normal gameplay participation | Base |
| `Activation.Full` | Highest configured animation/AI/physics update capability | Only capabilities the participant declares safe to control |

Configure an explicit rank/order; tag lexical order is not a tier ordering. A missing representation adapter returns Unsupported or chooses an explicitly configured safe tier, never silently deletes an actor.

Avoid universal disabling of collision, replication, AI, or physics. A distant barrier may still need collision; an occupied object may still need full simulation. Components/providers declare the exact resources the Activation system may control.

## 14.3 Policies and budgeted relevance

Support AlwaysActive, DistanceBased, Manual, and Custom in the base. VisibilityBased and InteractionBased require applicable relevance/pin providers. RegionBased and EventBased are bridges, not sibling dependencies.

Distance bands are data-driven, explicitly labeled in centimeters internally, and include separate promote/demote thresholds. The original meter values were illustrative, not production defaults. Select defaults from a generic measured fixture rather than embedding kilometer-scale assumptions.

Use hysteresis, minimum dwell time, bounded evaluation cadence, and per-frame transition budgets. Maintain spatial/indexed registration instead of scanning every actor per frame. Teleports and newly added relevance sources may request prioritized reevaluation.

Evaluate all relevant sources: players, vehicles, cameras, cinematic targets, and explicit interest/pins. For authoritative simulation, combine all relevant players and server requirements; a single client's screen visibility cannot authorize removing server collision or simulation.

## 14.4 Pins, state, and transitions

Interactions, inspection, sequences, occupancy, and other consumers may hold activation pins through providers/bridges. Pins have owners, reasons, minimum tiers, and cancellation rules; expired owners cannot pin forever.

```text
Evaluate Desired Tier → Acquire Transition Guard → Capture State
→ Prepare Destination → Validate Destination → Commit Swap → Release Source
```

Keep the prior usable representation until the destination is ready. On failure, preserve or restore it and report the result. A transition must never leave two authoritative actors or no usable representation for a logical object.

State preservation includes transform, tags, enabled state, and custom payloads such as damage, as supplied by participants. Runtime cache is not disk persistence; a Save bridge handles durable state. Optimization transitions never imply intentional persistent destruction.

Do not directly modify a global actor state then restore a stale snapshot. Track owned overrides and reconcile with host changes; participant adapters may be necessary for AI/physics state that lacks composable ownership.

## 14.5 ISM/HISM, pooling, and Mass

`DocWorldActivationISM` maps persistent/logical object IDs to runtime instance handles and storage locations. Raw instance indices are mutable implementation details. Update reverse maps after removal/reordering and verify the exact 5.8.3 instance API used.

Promotion creates/acquires an actor, assigns logical identity, restores state, then removes/hides the instance at commit. Demotion captures state, creates the instance, then retires the actor. Prevent duplicate rendering/collision during the handover and handle failed spawn/instance creation.

`DocObjectPool` accepts only explicitly poolable classes. Require OnAcquire/OnRelease reset contracts for owner, instigator, attachments, transform, tags, delegates, timers, latent work, audio/effects, collision, and custom state. BeginPlay is not a reusable reset hook. If the actor cannot prove clean reuse, spawn/destroy it normally. Replicated actor pooling is a separate capability and disabled until lifecycle/network tests pass.

`DocWorldActivationMass` remains a later integration: prove state/identity transfer, entity ownership, and network behavior before implementing far-entity/near-actor switching. Base activation never requires Mass.

## 14.6 Acceptance

`ACT-01`: Distance/manual policies and distinct Lightweight/Active behavior work with no optional bridges.  
`ACT-02`: Hysteresis/dwell rules prevent rapid oscillation near boundaries.  
`ACT-03`: Multiple relevance sources and owned pins choose a safe effective tier.  
`ACT-04`: State survives repeated transitions; failed promotion leaves a usable prior representation.  
`ACT-05`: Transition/evaluation budgets are obeyed and measured under load.  
`ACT-06 [BRIDGE]`: ISM/HISM removal/reordering preserves object identity and hit-to-object resolution.  
`ACT-07 [UTILITY]`: Pool reuse leaks no prior state and rejects unsupported actors.  
`ACT-08`: Stream unload/world teardown is distinct from gameplay destruction.  
`ACT-09 [NETWORK]`: Server authority and late relevance changes preserve required simulation.

Debugger: logical ID, current/desired tier, policy, distance/source, pins, representation, queued transition, evaluation cost, transitions/second, active actor/instance counts, and pool usage.

# 15. Recommended Development Order

## 15.1 Delivery method

Keep the original order: Core → Events → Interaction → Regions → Time → Streaming → Save → Audio → Sequences → Inspection → Activation → optional optimization bridges.

Use a vertical slice per system: public contract, minimal real behavior, failure path, Blueprint example, automation, isolated host build, and status update. Expand the slice to the complete declared base capability before calling that milestone complete.

Do not substitute placeholder methods, empty delegates, fake progress, comments describing future behavior, or always-success implementations for the requested feature. Future capabilities must return Unsupported and remain on the backlog; they must not masquerade as working implementations.

## 15.2 Phase 1 — Core Foundation

| Milestone | Required implementation | Exit evidence |
|---|---|---|
| M0 — Workspace/engine audit | Inspect repository, engine, toolchain, existing plugins, instructions, and user changes | `ENGINE_COMPATIBILITY.md`, initial status and dependency matrix |
| M1.1 — DocModularCore | Descriptor/module, shared contracts, identity, tags, narrow settings/logging, first test harness | Core-only build; CORE tests; no sibling dependencies |
| M1.2 — DocEvents | Dispatch, scopes, typed payloads, retention, delays/cancellation, Blueprint nodes, monitor | EVT tests; C++/Blueprint fixture; first cooked Core+Events smoke |
| M1.3 — DocInteraction | Providers, definitions, conditions/actions, sessions, all four modes, input/view model, authority boundary | INT tests; generic target/interactor examples; isolated build |
| M1.4 — DocRegions | Shapes, observer registration, queries, hierarchy/priority, streamed lifecycle handling, debug | REG tests; nested-region fixture; phase integration report |

The Phase 1 integration example may enable `DocInteractionEvents` or `DocRegionsEvents`, but their base-only tests must also pass. Deliver the first real example before investing in polished multi-panel editor tooling.

**Phase 1 exit:** first four foundational plugins work independently; a cooked demonstration resolves its assets; shutdown/world isolation tests pass; status names the next unverified step.

---

# 16. Phase 2 — World Infrastructure

| Milestone | Required implementation | Exit evidence |
|---|---|---|
| M2.1 — DocTime | Integer/remainder clock, calendar, boundary policy, schedules, snapshot interface | TIM tests and long-duration arithmetic fixture |
| M2.2 — DocStreaming | Leases, dependency graph, native level-instance backend, readiness/cancellation | STR base tests and cooked repeated-instance fixture |
| M2.2b — World Partition bridge | Streaming Sources, Runtime Data Layers, state arbitration, effective readiness | STR bridge tests in real cooked World Partition map |
| M2.3 — DocSave | Stable registration, records, capture, migration, storage/recovery, restoration | SAV tests, old-schema fixtures, fault injection, stream/save/restore round trip |

Prove Core's identity scheme against repeated level instances before committing the durable save schema. A lightweight earlier identity fixture is required; this is not authorization to implement Save before the foundations.

**Phase 2 integration scenario:** mutate an object, unload its region/level, save while it is absent, travel/restart, restore, reload, and verify its state exactly once. Repeat with an intentionally destroyed actor and a runtime-spawned actor. The loaded-only snapshot shortcut must fail this test.

---

# 17. Phase 3 — Presentation / Narrative Systems

| Milestone | Required implementation | Exit evidence |
|---|---|---|
| M3.1 — DocAdaptiveAudio | Channels, profiles, layers, fades, request ownership, emitter limits | AUD base tests and audio-enabled playback evidence |
| M3.1b — Audio bridges | Quartz scheduling and compatible MetaSound playback/controls as scoped | Timing report and original-capability-profile checks |
| M3.2 — DocSequences | Logical playback, queue/priority, bindings, markers/effect policy, restoration | SEQ tests; skip/failure/teardown fixture |
| M3.3 — DocInspection | Local-player sessions, runtime preview, documents/images/books, focus points, input ownership | INS base tests and two-local-player fixture |
| M3.3b — Inspection integrations | Media playback and optional front ends; interaction/sequence coexistence | Audio/video fixture, supported-seek evidence, control-ownership tests |

**Phase 3 integration scenario:** enter a tracked region, request audio, begin a sequence with residency prerequisites, open/close an allowed inspection session, interrupt/fail an operation, and verify every surviving owner's control/audio/streaming claims remain correct.

Run presentation tests with a real rendering/audio path. NullRHI or no-audio runs may test logic, not final visual/audio output.

---

# 18. Phase 4 — Optimization

| Milestone | Required implementation | Exit evidence |
|---|---|---|
| M4.1 — DocWorldActivation | Registered objects, distance/manual policies, tiers, budgets, pins, state preservation | ACT base tests and reproducible performance baseline |
| M4.2 — DocObjectPool | Explicit poolable lifecycle, eligibility checks, bounded capacity, clean reuse | Pool reset/leak/reuse tests; compare against ordinary spawn/despawn |
| M4.3 — ISM/HISM bridge | Instance identity map and reversible actor/instance transitions | Reordering/failure/reload tests; measured handover cost |
| M4.4 — Mass bridge | Feasibility first, then an explicitly scoped entity/actor adapter | Separate authority, identity, state-transfer, and performance report |

Do not add pooling simply because it is listed. Implement it as an optional utility and enable it for a workload only when measured benefit and correct reset behavior are demonstrated. The same applies to Mass transitions.

**Phase 4 exit:** reduced cost is measured against an equivalent workload; state and gameplay behavior remain correct; missing bridges leave the base operational.

**Release hardening after Phase 4:** execute Section 27 across both clean consumer hosts, the standalone plugin matrix, and declared bridge profiles. This is a verification phase, not an additional gameplay feature.

---

# 19. Automation Tests

## 19.1 Required test layers

Use Unreal Automation/Functional Testing as appropriate; do not simulate engine behavior entirely with mocks and call the feature verified. Epic's framework covers API, feature, content, and screenshot-oriented testing; tests should not assume editor state or leave test files behind. [R12]

| Layer | Purpose | Examples |
|---|---|---|
| Static validation | Descriptor/dependency/schema/documentation checks | No forbidden sibling includes; tag/asset/ID validation |
| Pure/native logic | Deterministic algorithms | Priority ordering, calendar arithmetic, graph cycles, migration |
| Engine automation | UObject/world lifetime and reflection | Owner destruction, Blueprint interface dispatch, async cancellation |
| Functional maps | Actual engine integrations | Traces, streamed instances, Data Layers, runtime preview |
| Network sessions | Verified opt-in networking | Owned RPCs, duplicate requests, authority, late join |
| Cooked/package tests | Distribution reality | Soft asset loading, UI/media assets, no Editor modules |
| Fault/stress/performance | Robustness under adverse conditions | Interrupted writes, event storms, transition churn, bounded memory |

Every requirement ID in Sections 4–14 maps to one or more concrete test names in `REQUIREMENTS_TRACEABILITY.md`. Keep planned tests distinct from authored/discovered/executed tests. Do not freeze a fake test count into the report.

## 19.2 Baseline names retained from the original

The original test families remain useful starting points. Add edge-case tests rather than replacing these with an unrelated naming scheme.

| Family | Baseline test names |
|---|---|
| Core | `Doc.Core.TagRegistration` |
| Events | `Doc.Events.Broadcast`, `.HierarchicalSubscription`, `.Unsubscribe`, `.PersistentPayload` |
| Interaction | `Doc.Interaction.BasicInteraction`, `.ConditionFailure`, `.HoldInteraction`, `.ServerValidation` |
| Regions | `Doc.Regions.EnterExit`, `.NestedPriority`, `.PrimaryRegion` |
| Streaming | `Doc.Streaming.ReferenceCounting`, `.DependencyLoad`, `.DependencyCycle` |
| Time | `Doc.Time.Advancement`, `.CalendarRollover`, `.EventBoundary`, `.TimeScale` |
| Audio | `Doc.Audio.PriorityStack`, `.ProfileTransition` |
| Sequences | `Doc.Sequence.Priority`, `.Queue`, `.SkipRestore` |
| Inspection | `Doc.Inspection.SessionOpenClose`, `.FocusPointDiscovery` |
| Save | `Doc.Save.PersistentGuid`, `.RuntimeSpawn`, `.DestroyedActor`, `.SchemaMigration` |
| Activation | `Doc.Activation.DistanceTransition`, `.StateRestore` |

Leading-dot entries in this table inherit the family prefix. `PersistentPayload` tests runtime retention; disk persistence is a separately named integration test. `ServerValidation` must exercise a real request boundary before the network capability is declared verified.

## 19.3 Cross-system regression gates

Required suite tests:

- **Isolation:** Core alone; then Core plus exactly one feature with all other feature directories absent. Repeat enabling/disabling each supported bridge in its valid host.
- **Lifetime:** two PIE worlds, map travel, local-player removal, owner destruction, cancellation at each async stage, and teardown with pending native callbacks.
- **Ownership:** overlapping audio requests, streaming leases, pause/control claims, and activation pins; releasing one owner preserves the others.
- **Persistence:** repeated level instances, streamed-out dirty state, destroyed tombstones, runtime spawns, cross-object references, and migration fixtures.
- **Distribution:** clean extraction/install, missing optional assets, cooked soft references, generated Blueprint assets, and native dependency availability.
- **Abuse/limits:** invalid handles, untrusted payloads, oversized save records, event storms, repeated RPC requests, and bounded queue/retention behavior.

A shared implementation bug must not be hidden by tests that calculate their expected value with the same function being tested. Use independent reference calculations and explicit golden fixtures where appropriate.

## 19.4 Test runner requirements

Implement wrappers such as `Validate-Workspace.ps1`, `Build-Host.ps1`, `Run-Automation.ps1`, `Package-Host.ps1`, and `Verify-PluginIsolation.ps1`. These are **required future repository deliverables**, not scripts included with this specification.

Wrappers accept engine/project paths, resolve real target names, preserve logs, set an explicit timeout, terminate only their owned process tree on timeout, and propagate nonzero exit codes. They must fail when expected tests are not discovered or no tests execute.

Each run receives a unique output directory. Reports include the command, engine/build identity, source commit or source hash, configuration, plugin manifest, start/end times, test counts, and log/report hashes where useful. Source changes invalidate a previous “current build passed” claim.

## 19.5 Command templates, not verification evidence

After resolving and validating `$EngineRoot`, `$ProjectFile`, the actual `$EditorTarget`, and output paths, a host build may use this pattern:

```powershell
# Template: verify paths, target, toolchain and supported flags locally first.
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" `
    $EditorTarget Win64 Development "-Project=$ProjectFile" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }
```

Automation invocation pattern:

```powershell
# Template: use an isolated host and validated output directory.
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    $ProjectFile -unattended -nop4 -nosplash `
    "-ExecCmds=Automation RunTests Doc." `
    "-TestExit=Automation Test Queue Empty" `
    "-ReportExportPath=$ReportDir" "-abslog=$LogFile"
if ($LASTEXITCODE -ne 0) { throw "Automation process failed: $LASTEXITCODE" }
# Also parse the report: a zero process exit alone does not prove tests ran/passed.
```

Use dedicated logic-only selections when running headless; do not add NullRHI/no-audio flags to the full presentation suite and still claim visual/audio validation. Discover and verify exact 5.8.3 automation/package flags locally.

---

# 20. Editor Tooling

Create `Window → Doc Modular Systems` through the Core Editor registration surface. Feature editors supply System Overview, Gameplay Event Monitor, Interaction Debugger, Region Debugger, Streaming Monitor, Time Debugger, Audio Debugger, Sequence Monitor, Save Inspector, and Activation Monitor.

Provide explicit world/session selection; do not silently inspect the wrong PIE world. Panels show Available, Not Installed, Disabled, Not Running, and Error separately. They can inspect authored data outside PIE and live runtime state during a compatible session; do not imply live gameplay exists when no world is running.

Data validation covers duplicate definition tags, persistent IDs, component keys, missing cooked assets, invalid durations/distances, region hierarchy cycles, streaming graph cycles, unsupported backend types, invalid audio metadata, sequence bindings, inspection focus dependencies, and missing save migrations.

Bulk identity repair, asset renaming, and configuration edits require a preview/confirmation or an explicitly authorized command. Provide undo/transactions where applicable and a repair report. Do not silently regenerate persistent identities to make validation green.

Start with logs, debug drawing, and a functional inspector. Polished editor panels must not delay core behavioral verification. Strip or disable high-volume diagnostics in Shipping; maintain a small actionable error surface without exposing sensitive saved data.

---

# 21. Documentation Requirements

Each implemented plugin provides:

```text
README.md
SETUP.md
API_OVERVIEW.md
BLUEPRINT_USAGE.md
CPP_USAGE.md
DEBUGGING.md
CHANGELOG.md
```

The README states purpose, exact supported engine/build profile, essential dependencies, optional bridges, installation, verified quick start, limitations, and test commands. Examples must reference real existing types/assets, not planned names presented as available.

`SETUP.md` must explain required project config, tag registration, asset discovery/cooking, component attachment, input binding, authority requirements, cleanup, and how to uninstall the plugin without leaving dangling references.

For each public async API, document request owner, world/local-player scope, clock, thread, states, cancellation, timeout, terminal result, resource ownership, and behavior during travel. For each Data Asset, document required fields, defaults, validation, and a minimal valid example.

Root documentation also includes:

| File | Required content |
|---|---|
| `DEPENDENCY_MATRIX.md` | Runtime/editor/native/plugin dependencies, bridge profiles, isolation evidence |
| `ENGINE_COMPATIBILITY.md` | Actual engine identity, toolchain, headers/modules verified, unresolved differences |
| `REQUIREMENTS_TRACEABILITY.md` | Requirement → source file → tests → last evidence → state |
| `TEST_MATRIX.md` | Hosts, configurations, scopes, exclusions, executed commands/reports |
| `DECISIONS.md` | Short design decisions and deliberate deviations from this handoff |
| `PERFORMANCE_BASELINE.md` | Fixture, hardware, settings, baseline and measured results |
| `RELEASE_CHECKLIST.md` | Distribution contents and release gates |
| `DEVELOPMENT_STATUS.md` | Current factual state and next bounded task |

Examples use primitive/procedural or appropriately licensed redistributable assets. List attribution/license obligations. A paid asset in one developer's library must not become an undeclared dependency of the suite.

---

# 22. Coding Standards

Follow Unreal naming/reflection conventions and the repository's existing formatter/style. Use complete includes, module API exports, generated-header placement, and independently compilable public headers. Run at least one clean non-unity build to catch accidental transitive includes and unity-hidden symbol problems.

Use `TObjectPtr` for reflected owned UObject references, `TWeakObjectPtr` for non-owning runtime references, and soft object/class references for deferred assets where appropriate. Raw non-owning access may be valid locally; do not use raw pointers as undocumented ownership or persistence.

Every object/session explains its owner and teardown path. Unregister delegates, input bindings, timers, tick/update hooks, provider registrations, loading handles, audio components, preview targets, and owned leases exactly once. Avoid capturing raw `this` into deferred callbacks without a verified lifetime guard.

Prefer small focused files/classes over god subsystems. No arbitrary line-count limit is imposed; split by responsibility and testability. Avoid unnecessary generalized frameworks before two real consumers justify shared code.

Use meaningful `UE_LOG` categories per feature, concise result tags, `ensure` for recoverable invariant diagnostics where appropriate, and `check` only for internal programmer errors. No normal authoring error should intentionally crash Shipping.

Do not modify shared definitions at runtime. Do not use CDOs as per-session state. Do not hide incomplete code behind always-success results or catch-all exception/error suppression.

---

# 23. Performance Requirements

## 23.1 Non-negotiable behavior

No repeated full-world `GetAllActorsOfClass` searches in routine runtime updates. No hidden high-frequency Actor/Component Tick on every registered object. No blocking load of substantial assets during ordinary interaction, inspection, sequence, or audio transitions.

Bound event history, retained payloads, listener queues, async work, streaming requests, audio voices, preview captures, save snapshots, and activation transitions. Cache lookups with explicit invalidation; stale caches are not an optimization.

A centralized tick/update is acceptable when it has clear work limits and a valid engine update mechanism. A plain manager declaration does not prove it receives ticks. Prefer timers/delegates when they better match the workload, not as a blanket replacement for measured scheduling.

## 23.2 Initial benchmark fixtures

The following are **test workload targets, not measured performance claims**:

| Fixture | Initial workload | Metrics |
|---|---|---|
| Events | 1,000 listeners; sustained 1,000 events/second; bounded burst | Dispatch time, allocations, queue depth, dropped/deferred count |
| Regions | 256 simple regions and 64 tracked observers | Query/update cost, candidate counts, transition latency |
| Activation | 10,000 registered lightweight records and 4 relevance sources | Evaluation cost, transition cost/backlog, active representations |
| Save | 10,000 small records plus 100 deliberately large payloads | Capture hitch, encoding cost, peak memory, storage latency |
| Streaming | 8 requesters sharing overlapping chunk dependencies | Lease correctness, request latency, residency, failure cleanup |
| Inspection/audio | Repeated open/close and rapid profile supersession | Resource counts, peak memory, stale callbacks, audible/visual defects |

Start smaller to debug correctness, then run these reproducibly. Store payload sizes, geometry complexity, asset sizes, sample duration, warm/cold conditions, CPU/GPU/RAM, resolution, RHI, and engine configuration.

Choose measured budgets per target host before release. Record median, p95, worst spike, memory trend, and workload limits. Do not invent a universal “under 1 ms” guarantee for arbitrary hardware/content. Use Unreal Insights once native systems are functional and retain the trace paths or summarized evidence.

Optimization is accepted only when equivalent gameplay correctness remains intact and the measured change improves the stated workload without unbounded memory or latency regressions.

---

# 24. Threading Rules

All UObject/Actor/component state capture, reflected interface calls, Blueprint delegates, native registration, and gameplay state mutation run on the game thread unless a specific engine API documents a different safe contract.

Workers receive immutable value data or already encoded bytes with no hidden live-object dependency. Validate `FInstancedStruct` contents and serializer behavior; the type name is not a blanket thread-safety guarantee.

On completion, marshal results to the correct world/session on the game thread and check owner generation. Cancelling work does not assume its native task immediately disappears. Late completion must be harmless.

Implement bounded queues and explicit shutdown behavior. Do not block the game thread waiting for worker work that itself needs the game thread. Avoid arbitrary detached threads; use appropriate engine task/storage facilities with owned lifetimes.

For Time and other periodic managers, explicitly implement the supported update/ticker/subsystem mechanism. Inheriting `UWorldSubsystem` alone is not an implementation of a per-frame update loop.

---

# 25. Networking Rules

Document each capability as Standalone, Authority-Aware, or Network-Verified. These are different claims.

| Concern | Required behavior |
|---|---|
| Interaction | Owned request transport, target/definition validation, rate limiting, deduplication |
| Events | Local delivery by default; no arbitrary RPC event bus |
| Regions | Local queries may differ by loaded geometry; authoritative membership policy is explicit |
| Streaming | Server gameplay/world-state authority and client residency are distinct |
| Time | Authoritative samples with revision; client interpolation without duplicate world effects |
| Audio | Usually local presentation; replicate only approved logical cues/state when needed |
| Sequences | Authoritative effects separated from playback; late join and skip authorization |
| Inspection | Per-local-player presentation; discoveries/rewards use an authorized path |
| Save | Server-authorized world mutation; clients do not supply trusted save records/classes |
| Activation | All relevant players influence authoritative simulation; visibility is not authority |

Use a real replicated actor/component for state/RPC transport. Do not require a particular project class; offer attachable integration and validate ownership. Each enabled network feature needs loss/latency or at least delayed-delivery tests, duplicate request tests, disconnect cleanup, and late-join state reconstruction.

Declare how Iris/legacy replication or other optional transports are supported only after verification. Do not claim compatibility with every replication setup from a single listen-server PIE run. Dedicated-server source builds are tested where the actual engine/toolchain supports them; unavailable server targets are marked Blocked, not passed.

---

# 26. Packaging Requirements

Required primary build configurations: **Development Editor Win64, Development Win64, and Shipping Win64**.

A native plugin distribution must state whether it is source-only or precompiled, its exact engine/build/toolchain target, essential native dependencies, and whether a Blueprint-only consumer needs a matching precompiled package. “Blueprint-first API” does not mean C++ source requires no compiler.

Validate:

1. Core alone, each Core+feature host, all bases, and each declared bridge profile.
2. Runtime modules contain no Editor module references or editor-only reflected types/assets.
3. Soft-reference assets, tags, config, localization, and media files actually appear and resolve in the cooked package.
4. A clean extracted installation with no developer machine paths, cached binaries, or original host content.
5. Repeated launch, map travel, shutdown, and the relevant packaged functional scenario.

When packaging an individual plugin with a shared Core dependency, the build host must have that dependency explicitly installed/resolvable. Do not assume a standalone plugin packaging command automatically finds sibling repository plugins. Document the verified packaging strategy rather than guessing command-line flags.

Generate `.uasset`/`.umap` content using Unreal's real editor/tooling. Never write text into a file with one of those extensions or claim a planned example map exists. If Unreal asset creation cannot run, provide a generator/authoring procedure and mark the asset and its tests unverified.

Distribute only necessary source/content/docs/licenses and approved binaries. Exclude Intermediate, Saved, DerivedDataCache, private credentials, personal paths, and unrelated project content. Produce a file manifest and archive checksum. Verify extraction and an installation smoke before marking the package usable.

---

# 27. Completion Definition

## 27.1 Base Suite Complete

The suite reaches this claim only when:

- Core and all ten base plugins compile on the recorded Unreal Engine 5.8.3 installation and pass the required Win64 configurations.
- Each feature works with only Core and its essential native dependencies; unrelated plugin directories can be removed without breaking it.
- Public C++ and Blueprint APIs, valid defaults/examples, data validation, and complete setup documentation exist for every base system.
- All applicable BASE requirement IDs map to executed passing tests and real evidence; Blocked/Not Run items are not counted as passed.
- Persistent identity, async cancellation, teardown/world isolation, ownership restoration, and cooked content tests pass.
- Two clean consumer hosts prove portability without replacing project classes or hard-coding maps, assets, cameras, inventory, or UI.
- Measured performance/resource limits are recorded; the distribution installs and runs from a clean extraction.

An example map, a compiling stub, or a count of generated files cannot satisfy a system's functional completion gate.

## 27.2 Original Capability Profile Complete

Additionally verify the original requested native/integration capabilities that are intentionally isolated from bases: Runtime Data Layers and World Partition source readiness, a genuine native Level Instance path, Quartz quantization, compatible MetaSound playback, and the original inspection media/audio/video capabilities.

Preserve all original base feature families—additional interaction providers and modes, configurable calendars, scoped/retained/delayed events, region queries, sequence controls, and persistent dynamic/streamed actor state. A minimal first slice is not a replacement for these.

## 27.3 Optional integration claims

List every bridge separately with supported features, engine/plugin version, limitations, tests, and packaging evidence. ISM/HISM, Mass, Smart Objects, GAS, CommonUI, Audio Modulation, and pooling are optional; unimplemented ones remain explicit backlog items.

A base release may ship before all optional integrations, but its README must not advertise unverified bridges or describe the entire extended suite as complete.

---

# 28. First IDE Tasks

## Task 0 — Inspect before changing anything

Read repository instructions and this document. Inspect the branch, worktree, `.uproject`, existing plugin code, docs, engine association, installed 5.8.3 build, and toolchain. Preserve user changes. Search for reusable existing contracts before creating duplicates.

Produce initial `DEVELOPMENT_STATUS.md`, `ENGINE_COMPATIBILITY.md`, and `DEPENDENCY_MATRIX.md`. Identify the smallest unimplemented milestone. If this is a fresh workspace, the next implementation is M1.1—not every system.

## Task 1 — Establish the Core build slice

Create `Plugins/DocModular/DocModularCore` with a real descriptor and Runtime module. Add an Editor module only for actual needed authoring/tooling. Provide the minimal host, actual test harness, and README.

Implement initial results, context, handle, identity, tag, and narrow-interface contracts. Compile immediately on 5.8.3. If native compilation is blocked, report the missing dependency and stop claiming verified milestones; do not hide the failure under more generated systems.

## Task 2 — Lock down shared ownership and identity

Add CORE tests, cross-world/generation checks, authored/runtime identity rules, and a repeated-instance identity fixture. Record the adopted naming/module decisions. Establish the development-status and test evidence pipeline.

## Task 3 — Implement Events as the first working feature

Create DocEvents and complete a real C++/Blueprint publish/subscribe slice. Then add hierarchy, scopes, mutation-safe dispatch, retention, delays, cancellation, and corresponding tests. Prove it without other features installed.

## Task 4 — Add a usable Event Monitor and first cooked test

Show bounded event history with world/tag/scope filters and resource counts. Generate real example assets and run the Core+Events cooked host. Do not spend this task on a full polished suite dashboard.

## Task 5 — Implement Interaction

Start with generic interactor/interactable components, a LineTrace provider, immutable definitions, conditions/actions, Instant and Hold modes, and a Blueprint view model. Expand to Overlap and the remaining promised providers/modes before the full Interaction milestone is marked complete.

## Task 6 — Prove the interaction authority boundary

Add the attachable owned network request transport where this capability is in the current scope; verify server rejection, reservation, deduplication, and condition revalidation. Keep standalone behavior intact. A network design without an executed server/client test remains Authority-Aware, not Network-Verified.

## Task 7 — Implement Regions

Create definitions, region instances/shapes, explicit observer tracking, actor/location queries, deterministic priority, lifecycle reasons, validation, and debug drawing. Test nested and duplicated instances plus component-overlap deduplication.

## Task 8 — Close Phase 1 with evidence

Run the appropriate automation, isolated builds, packaged fixture, and cross-world shutdown tests. Update exact pass/fail/blocked state, current milestone, known issues, and the next bounded task. Do not advance to Phase 2 while silently carrying a failed foundation gate.

---

# 29. IDE Handoff Rule

## 29.1 Repository state is authoritative

Always update `Docs/DEVELOPMENT_STATUS.md` after a meaningful work slice. Do not depend on chat memory, an old test summary, or this specification as proof of current repository state.

Use this template, replacing every placeholder with facts or `Not Run`/`Unknown`:

```markdown
# Development Status

## Workspace and verification identity
- Updated: <timestamp with timezone>
- Branch / commit: <actual branch and revision>
- Worktree: <clean or precise relevant changes>
- Engine: <actual version, changelist/build identity, local path reference>
- Toolchain: <detected compiler/SDK>
- Current milestone: <one milestone>
- Capability profile: <Base / Original / named bridges>

## Completed and verified
<Requirement IDs, implementation paths, test/build evidence>

## Implemented but unverified
<What exists; exactly what has not been executed>

## Current verification
| Check | Result | Command/report | Source revision |
|---|---|---|---|
| Editor build | Not Run | ... | ... |
| Runtime Development build | Not Run | ... | ... |
| Shipping cook/package | Not Run | ... | ... |
| Automation discovered/executed/passed/failed | Not Run | ... | ... |
| Isolated dependency host | Not Run | ... | ... |
| Functional/network/presentation test | Not Run | ... | ... |

## Known issues and blockers
<Reproduction, first relevant error, affected requirement, next action>

## Artifacts
<Actual path, capability profile, manifest/checksum, validation state>

## Exact next task
<Bounded objective, files/contracts involved, exit tests, stop condition>
```

Do not replace the status file with a marketing summary. Preserve historical results in a separate change/run log and mark them stale when the relevant source, engine, configuration, or content changes.

## 29.2 Copy-ready continuation instruction

```text
Use this handoff as the technical specification for the project-agnostic
Unreal Engine 5.8.3 modular gameplay suite.

First read repository instructions, Docs/DEVELOPMENT_STATUS.md, the current
source, and this handoff's Sections 0–4 and 27–29. Inspect branch/worktree and
verify the installed engine/build/toolchain. Preserve existing user work.

Choose the smallest incomplete milestone from the specified order. Implement
one real vertical slice, including failure handling, public Blueprint/C++
access, cleanup, and its tests. Keep every base plugin independent of siblings;
put cross-feature and optional engine integrations in explicit bridges.

Use the installed 5.8.3 headers as the authority for exact API signatures and
module ownership. Do not invent APIs, placeholder binary assets, test results,
progress percentages, or completed functionality. Do not downgrade the engine.

Build and run the relevant tests when the environment allows it. Record actual
commands, exit codes, discovered/executed test counts, reports, and source
revision. Mark unavailable verification Blocked/Not Run, not Passed. Do not
claim multiplayer, cooked content, or performance verification from a compile.

Update DEVELOPMENT_STATUS.md, requirement traceability, dependency notes, and
any affected setup/API documentation. End with what changed, what was verified,
what remains blocked, and the exact next task. Do not expand into unrelated
systems, rewrite working project architecture, or publish/push without the
applicable authorization.
```

---

# 30. Final Implementation Principle

The deliverable is not ten systems that happen to work in one sample game. It is a reusable engine-infrastructure library with precise contracts and independently proven behavior.

Each system must work independently, integrate through explicit interfaces/tags/bridges, remain data-driven and Blueprint-friendly, be extensible from C++, support streamed-world lifetimes where relevant, and provide a realistic path to verified multiplayer without pretending that future support already exists.

**Prefer a working, tested, documented vertical slice with an honest status over a broad collection of compiling shells. Preserve the complete feature scope in the backlog until every promised capability has evidence.**

# A. Revision Change Record

These changes are deliberate recommendations in Revision 2. They are not retroactive claims about the contents or implementation status of the original handoff.

| Original ambiguity or risk | Revision 2 decision | Where |
|---|---|---|
| “Only Core dependency” coexisted with actions/fields importing other systems | Bases may use essential native modules; sibling functionality uses explicit bridges and absence tests | 2–3, 5–14 |
| One grouping tree could be mistaken for nested plugins | Descriptor-free grouping directory; no plugin inside a discovered plugin | 2.1 |
| Generic proposed class names could collide in reusable libraries | Recommended `Doc`-prefixed public types; existing APIs require an audit before renaming | 3.1 |
| Feature event payload proposed for Core | Keep feature payloads in their owning plugin; Core stays small | 4.1 |
| Multiplayer-aware architecture did not define transport/ownership | Separate Authority-Aware and Network-Verified; use real owned replicated transports | 3.9, 25 |
| “Global” events and persistent/latest payloads had unclear lifetime | World-scoped bus; bounded retained runtime values, explicit disk/network integrations | 6 |
| Async operations lacked cancellation/late-callback contracts | Typed owner-scoped handles, generation checks, terminal results, timeouts, cleanup | 3.6 and each system |
| Interaction actions could partially fail without defined outcome | Reservation/revalidation; explicit irreversible effects and compensation limits | 5 |
| Region tags, placed instances, and geometry availability overlapped | Separate definition/instance identity, explicit observer membership, reasoned unload behavior | 7 |
| Packed Level Actors appeared alongside native load backends | Treat packed content under its actual owning streaming mechanism; verify runtime instance mode | 8 |
| Integer time was treated as sufficient to prevent drift | Preserve conversion remainder; test exact input arithmetic; define jumps and boundary traversal | 9 |
| Optional Quartz/MetaSound support appeared in unconditional acceptance | Separate base playback, compatible MetaSound playback, typed controls, and Quartz bridge gates | 10, 27 |
| Sequence skips/markers/restoration lacked side-effect semantics | Explicit seek/skip policy, effect identity, control leases, and readiness ownership | 11 |
| Inspection preview/input cleanup could disturb live gameplay | Runtime-safe presentation copies, per-player ownership, shared control-provider contract | 12 |
| Authored GUID alone did not distinguish repeated level instances | Composite persistent identity with stable instance scope and component keys | 4, 13 |
| Save record/threading/versioning details were insufficient | Encoded versioned records, game-thread capture, worker-safe bytes, migrations, recovery tests | 13 |
| Activation's “No Actor” state could lose identity or create false destruction | Explicit virtualization adapters, preserved records, pins, atomic-like guarded handover | 14 |
| HISM instance index/pooling assumptions were too broad | Indirection/remapping and opt-in reset contract; networking separately gated | 14 |
| Compile/test/example completion could be asserted without evidence | Requirement IDs, clean-host/cooked tests, explicit evidence vocabulary and status template | 19, 26–29 |

**Scope clarification:** optional native integrations are isolated, not discarded. “Base complete” and “original capability profile complete” are separate release statements. Spline/polygon regions and Mass remain deferred/optional as originally framed. Other desired extensions remain listed until implemented and verified.

---

# B. External Verification Notes and Official References

These references support engine-specific clarifications, not the entire proposed design. The ownership models, test workloads, result types, and milestone contracts are recommendations in this revision. No source cited here proves that this suite compiles or runs.

Public references were checked on **September 26, 2026**. Where a general documentation page contains an older example, use it for the concept only; verify current headers and supported flags in the installed 5.8.3 build.

| ID | Official source | What it supports |
|---|---|---|
| R1 | [Epic: 5.8.3 Hotfix Released](https://forums.unrealengine.com/t/5-8-3-hotfix-released/2833315) | Availability of the requested hotfix; announcement dated September 22, 2026 |
| R2 | [Epic: Plugins in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine) | Plugin discovery, descriptors, module separation, dependency concepts |
| R3 | [Epic API: FInstancedStruct](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/FInstancedStruct) | Current published type/header location; local verification still required |
| R4 | [Epic: Programming Subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine) | Engine-managed subsystem lifetime and Blueprint-accessible extension points |
| R5 | [Epic: Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine) | Primary assets, discovery, streamable handles, bundles, cook organization |
| R6 | [Epic: Actor Component Replication](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicating-actor-components-in-unreal-engine) | Replicated actor/component requirements and component RPC support |
| R7 | [Epic: Level Instancing](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-instancing-in-unreal-engine) | Level Instances versus packed content and embedded/streaming runtime modes |
| R8 | [Epic: World Partition — Data Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine) | Data Layer assets/instances and Loaded versus Activated runtime behavior |
| R9 | [Epic: Overview of Quartz](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-quartz-in-unreal-engine) | Audio-clock scheduling rather than ordinary game-thread timer timing |
| R10 | [Epic: Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) | Runtime input actions and mapping-context integration |
| R11 | [Epic API: AsyncSaveGameToSlot](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UGameplayStatics/AsyncSaveGameToSlot) | Game-thread serialization, worker storage, game-thread completion |
| R12 | [Epic: Automation Test Framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine) | Test types, plugin tests, independent fixtures and cleanup practices |

Additional context: [Epic: Saving and Loading Your Game](https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-and-loading-your-game-in-unreal-engine) describes the native `USaveGame` workflow and storage APIs. The stronger migration, integrity, journaling, and transactional-ownership guarantees in this handoff must be implemented and tested separately.

---

# C. Source Scope Coverage and Traceability Seed

## C.1 Original system coverage

| Original system | Preserved feature families | Explicit separation / caution |
|---|---|---|
| Interaction | All seven providers; tag intents; conditions/actions; Instant/Hold/Continuous/Repeated; prompts; Enhanced Input; authority/debug | Event/sequence/inspection actions via bridges; inventory/economy remain external |
| Events | Broadcast/subscription; hierarchical tags; typed payload; global/actor/component/region/team/world/custom scopes; retained state; delays; Blueprint wait/listen; monitor | Global is world-scoped; retained history is bounded; disk/network persistence is separate |
| Regions | Definition metadata; nested/hierarchical regions; overlap priority; box/sphere/capsule/volume; actor/location queries; enter/exit/primary events; streamed lifecycle | Spline/polygon/custom extensions remain tracked; geometry coverage is explicit |
| Streaming | Logical chunks; map/level references; dependencies; multi-requester residency; priority/preload; keep-loaded; variants; sources; async status/debug | Data Layers/World Partition through bridge; packed content follows native ownership |
| Time | Integer simulated time; custom calendar; scale/pause; day periods/boundaries; schedules; skip/set/advance; celestial outputs; authority; save/restore | Remainder/jump semantics added; astronomy requires an explicit model/provider |
| Adaptive Audio | Channels; soft sound profiles; layered music; ambience/one-shots; fades/crossfades; priorities; routing; emitter limits; musical transitions | Quartz, typed MetaSound controls, and modulation have explicit integration gates |
| Sequences | Logical playback; soft assets; priority/queue; pause/resume/stop/skip/restart/markers; participant interfaces; input/camera/AI requests; optional streaming | Gameplay effect/seek/skip semantics are explicit; controls are provider-owned |
| Inspection | 3D/world/preview; documents/images/books; audio/video; focus points; local-player sessions; temporary input; optional UI | Runtime-safe preview; optional media/CommonUI integrations; no implicit authoritative rewards |
| Save | Stable authored/runtime identity; component data; tombstones; streamed/dynamic objects; slots; versioning/migrations; async storage; integrity/backup/recovery; debounce | Composite IDs and explicit capture/restore/storage contracts replace underspecified persistence |
| Activation | Tier tags; distance/manual/custom and other policies; state preservation; nearby relevance; optional actor/ISM/HISM/Mass representations; pooling; debug | Pins/hysteresis/budgets added; representation/pooling require eligible adapters |

## C.2 First traceability entries to create in the repository

Do not mark these tests authored or passed merely because they appear in this document.

```text
Requirement ID | Capability | Implementation | Test name(s) | Evidence | State
CORE-01        | BASE       | Not created    | Not authored | None     | Not Started
EVT-01         | BASE       | Not created    | Not authored | None     | Not Started
INT-01         | BASE       | Not created    | Not authored | None     | Not Started
REG-01         | BASE       | Not created    | Not authored | None     | Not Started
STR-01         | BASE       | Not created    | Not authored | None     | Not Started
TIM-01         | BASE       | Not created    | Not authored | None     | Not Started
AUD-01         | BASE       | Not created    | Not authored | None     | Not Started
SEQ-01         | BASE       | Not created    | Not authored | None     | Not Started
INS-01         | BASE       | Not created    | Not authored | None     | Not Started
SAV-01         | BASE       | Not created    | Not authored | None     | Not Started
ACT-01         | BASE       | Not created    | Not authored | None     | Not Started
```

For an existing implementation, replace the seed with facts from the repository audit. Expand to **every** requirement ID in this handoff, classify bridge/network-only items accurately, and add the cross-system gates in Section 19.

## C.3 Source document record

**Input file:** `UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff(1).md`  
**Input size:** 39,089 bytes  
**Input SHA-256:** `0b84afafa1639479f51e92fc1106d871b9a0dd0f46cc0ece7a93ff85c86699b8`  
**Revision method:** Preserve the original ten-system scope and 30-section organization; consolidate repetitive lists and add explicitly identified implementation contracts, verification gates, and traceability.  
**Software verification performed by this document:** None. Source implementation and engine execution are future IDE work.
