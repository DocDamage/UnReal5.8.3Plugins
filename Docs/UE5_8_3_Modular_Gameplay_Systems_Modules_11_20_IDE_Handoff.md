# Unreal Engine 5.8.3 Modular Gameplay Systems Suite — Modules 11–20
## IDE Implementation Handoff — Revision 2

**Document revision:** 2.0  
**Prepared:** September 27, 2026  
**Target engine:** Unreal Engine 5.8.3  
**Primary platform:** Windows x64  
**Implementation:** C++ runtime plugins with Blueprint-first public APIs  
**Configuration:** Data Assets, Developer Settings, Gameplay Tags  
**Manager lifetime:** Unreal Subsystems, with explicit world and player ownership  
**Actor-level behavior:** Actor Components and interfaces  
**Input:** Enhanced Input through explicit adapters; no forced project mappings  
**Optional presentation:** CommonUI and other separately installed presentation adapters  
**Companion specification:** `UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff_v2.md`  
**Implementation status:** Specification only. This revision does not supply plugin code, a compiled Unreal project, authored binary assets, or successful runtime/packaging evidence.

> **For the implementing IDE agent:** Read Sections 0–2, 13–15, and 24–27 before changing code. Begin with Task 0 in Section 26. Audit the actual workspace and implement one verified vertical slice at a time. Do not interpret a specification, a plugin skeleton, or a passing documentation check as working gameplay.

### What this revision preserves

The supplied Modules 11–20 handoff is the source of truth for the ten module names, intended capabilities, Unreal target, Windows platform, project-agnostic requirement, original 28-section organization, and Phase 5–8 ordering. Those sections are retained. Section 0 and Appendices A–D add execution rules, change tracking, reference notes, and coverage information.

New ownership models, algorithms, failure policies, test scenarios, and capability gates are **recommended engineering contracts introduced by this revision**. They are not claims that the source already specified these details. The companion Revision 2 informs shared conventions; it is not evidence that the foundation exists in the repository. Explicit clarifications and their scope effects appear in Appendix A.

### Navigation

- **Start implementation:** Sections 0–2 and 26.
- **Module contracts:** Sections 3–12, corresponding to modules 11–20.
- **Integration, dependencies, and sequencing:** Sections 13–15.
- **Tooling, validation, performance, networking, and packaging:** Sections 16–23.
- **Status, IDE continuation prompt, and completion:** Sections 24–28.
- **Changes, official references, coverage, and compatibility ledger:** Appendices A–D.

---

# 0. Document Control and IDE Execution Contract

## 0.1 Meaning of requirements

**Must** and **must not** establish acceptance requirements for the capability under development. **Recommended** establishes a default that may be changed through an architecture decision record. Examples are configurable fixtures, not hard-coded gameplay assumptions. API names describe proposed contracts unless verified in the repository or explicitly identified as native Unreal APIs.

Preserve repository instructions, licenses, user work, established public APIs, existing save compatibility, and functioning implementations. Record deviations in `Docs/DECISIONS.md`. Do not use this document as authority to rename published types, rewrite the foundation, delete content, change engine associations, or overwrite existing configuration without evaluating compatibility.

## 0.2 Relationship to modules 1–10

Reuse the companion revision's `FDocSystemResult`, persistent identity model, transient handle semantics, provider boundaries, player-control leases, and save restoration principles where they actually exist. Missing shared contracts are a **bounded foundation compatibility task**, not permission to fabricate a second Core or copy sibling internals.

The first handoff excludes inventory, quests, weather, and framework UI **from its own ten-module scope**. This expansion explicitly authorizes those features in modules 11–20. They still must not be embedded inside Core or older base plugins. Neither document authorizes a game-specific combat, crafting, economy, procedural-generation, character, or movement framework.

## 0.3 Capability profiles

| Label | Meaning | Verification obligation |
|---|---|---|
| `BASE` | Core plus one feature and essential native engine dependencies | Independently useful, headless where appropriate, tested with siblings physically absent |
| `BRIDGE` | Separately installable cross-feature or optional native integration | Tests for integration present, integration absent, unavailable provider, and teardown |
| `EXAMPLE` | Generic demonstration content or UI | Real assets generated/authored through Unreal and exercised; no runtime dependency from base |
| `FUTURE` | Explicitly deferred extension | Visible backlog and unsupported capability response; never counted as implemented |

All ten bases remain required. Original optional integrations remain explicitly tracked; moving an operation behind a bridge changes installation boundaries, not its behavior or its acceptance obligation when advertised. The named baseline integrations in Section 14 must not disappear behind an unrestricted “future” label.

Use separate release statements: **Expansion Base Complete**, **Selected Integration Profile Complete**, and **Full Declared Profile Complete**. Always list exactly which bridges, platforms, presentation paths, and networking modes were verified. A base-complete inventory library is not automatically a tested multiplayer inventory product. A headless UI state manager is not a completed CommonUI front end.

## 0.4 Exact-engine and workspace gate

Unreal Engine **5.8.3** is the requested target, not a compatibility claim. Resolve the actual engine from the existing project association or explicit local configuration. Read `Engine/Build/Build.version`; record version, changelist/build identity, compiler, SDK, target rules, branch, commit, and relevant uncommitted changes. Inspect installed headers, native plugin descriptors, and build rules before choosing version-sensitive APIs.

General official documentation supports architectural guidance, not every 5.8.3 signature or setting. Compile small integration probes. Do not assume a fixed installation path, silently downgrade, rewrite the engine association, enable optional plugins globally, or invent APIs. Missing/different Unreal means native verification is blocked; report that precisely and continue only bounded work that can be honestly completed.

## 0.5 Evidence vocabulary and work boundaries

Use `Not Started`, `In Progress`, `Implemented / Unverified`, `Verified`, and `Blocked`. Tests separately use `Not Run`, `Passed`, `Failed`, or `Skipped with Reason`. Save evidence by source revision, engine, configuration, content revision, enabled plugins, command, exit code, discovered/executed test names, and logs.

“Compiled” does not mean “works in PIE”; PIE does not mean “cooked”; cooked does not mean “Shipping tested”; an authority check does not mean “network tested.” Do not mark a skipped test passed or report a full-suite result from one selected prefix. Use the actual repo as project state, not the document's status template.

Do not publish, push, install paid assets, modify global settings, or remove user files without applicable authorization. Generated files and scratch hosts belong in identified working/output directories. Changes to portable library source must never embed a user's machine-specific paths.

---

# 1. Scope

## 1.1 Foundation and expansion

The assumed architectural foundation contains Core plus the first ten features; its actual implementation must be audited:

```text
DocModularCore
DocEvents             DocInteraction        DocRegions
DocStreaming          DocTime               DocAdaptiveAudio
DocSequences          DocInspection         DocSave
DocWorldActivation
```

This expansion adds exactly these ten systems:

| Module | Plugin | Owns | Explicitly does not own |
|---|---|---|---|
| 11 | `DocMapNavigation` | Spatial map data, markers, discovery, projections, compass output | Pathfinding, movement, teleportation, mandatory map widgets |
| 12 | `DocWeather` | Weather state, profiles, transitions, scheduling, regional samples | A sky renderer, fluid simulation, mandatory weather assets |
| 13 | `DocSurfaceFeedback` | Surface/event resolution and bounded response dispatch | Combat damage, physics simulation, character movement |
| 14 | `DocDialogue` | Branching sessions, choices, conditions, participant binding | Mandatory NPC hierarchy, cinematics, LLM generation, fixed dialogue UI |
| 15 | `DocQuestObjectives` | Standalone objectives, quest stages, event-driven progress | Combat, inventory storage, map presentation, rewards implemented in other systems |
| 16 | `DocNPCSchedules` | Activity selection, overrides, expected locations, unloaded records | Locomotion, path following, an AI brain, unrestricted offline economic simulation |
| 17 | `DocKnowledgeCodex` | Knowledge discovery, staged reveal, relationships, read state, search | Mandatory media player, investigation mechanics, authoring story content |
| 18 | `DocInventoryItems` | Definitions, instances, containers, validated transactions | Crafting, combat, commerce accounting, skeletal equipment attachment |
| 19 | `DocUnlocksProgression` | Entitlements, prerequisites, temporary grants, progression gates | A mandatory skill tree, economy, attribute system, paid entitlements |
| 20 | `DocGameFrameworkUI` | Screen state, settings contracts, prompts, notifications, input presentation | A fixed visual theme, mandatory HUD/game framework, authority over gameplay |

There are **21 base plugins across the combined suite: one Core and twenty features**. Bridges and examples are additional packages, not new mandatory game systems.

## 1.2 Portability requirements

Do not hard-code project names, GameModes, player/NPC classes, maps, narrative content, inventory layouts, dialogue layouts, sky packages, cameras, save-slot UI, animation frameworks, or external asset paths. Do not require replacing a host's GameInstance, PlayerController, Asset Manager, viewport client, or save framework simply to use a base plugin.

Drop-in means documented installation, configuration, asset registration, component/interface attachment, and optional UI/input hookup without editing plugin source. It does not mean unknown assets, surface tables, calendars, or control schemes configure themselves.

Each base must operate in a clean isolated host and in a second structurally different host. Per-player systems must support two independently scoped users. World services must survive travel or shut down according to their declared lifetime, without retaining the previous world.

---

# 2. Global Architectural Rules

## 2.1 Plugin independence and dependency direction

Every base may depend on `DocModularCoreRuntime` and only native modules essential to its own base behavior. No base imports another feature's runtime, editor, public reflected type, asset class, or bridge. Core depends on no feature. A bridge may depend on both participants; neither participant depends on the bridge.

Unreal separates plugin descriptors from C++ module rules. Verify both. Keep grouping directories free of `.uplugin` descriptors and never nest one plugin inside another plugin's directory. Unreal plugin discovery and Runtime/Editor separation are documented by Epic. [R1]

A `TSoftObjectPtr<UOtherFeatureDefinition>` still references the optional feature's C++ type. Soft references reduce asset-loading coupling; they do **not** erase compilation or reflection dependencies. Use a bridge-owned definition, narrow interface, or validated feature-neutral identifier instead. No dummy sibling plugins may be installed to make an isolation test pass.

`DocEvents`, `DocRegions`, `DocTime`, and `DocSave` remain optional integrations. “Recommended” in the original dependency matrix describes preferred runtime integration, never a mandatory `.Build.cs` relationship.

## 2.2 Public APIs and immutable configuration

Keep core algorithms in C++. Expose meaningful BlueprintCallable operations, genuinely side-effect-free BlueprintPure queries, delegates, components, Data Assets, and interface extension points. Compile both C++ consumer code and a Blueprint-only consumer. Unreal reflection declarations, interface execution, GC ownership, module exports, and async proxy lifetimes must be real, not pseudocode.

Prefer `UDoc...`, `ADoc...`, `FDoc...`, and `IDoc...` for **new** public types, consistent with the companion revision. The original names below are semantic mappings, not authorization to break an existing API. Maintain redirects/migration notes if already-shipped names change.

Definitions, graph nodes, conditions, actions, and class defaults are immutable at runtime. Mutable execution data belongs to a session, owner-scoped record, transaction, or deliberately instantiated object with valid ownership. Sharing one mutable action UObject between simultaneous conversations or reward operations is forbidden.

## 2.3 Lifetime and owner scope

Subsystems offer managed lifetime and extension points; lifetime is not automatically data ownership or networking. [R2]

| Service | Recommended lifetime | State partition |
|---|---|---|
| Map views and discovery facade | LocalPlayer | Explicit owner scope plus world/map identity |
| Weather and surface feedback | World | World generation; position where applicable |
| Dialogue sessions | World | Session, participants, authorized audience |
| Objectives, knowledge, unlock records | GameInstance | Owner scope + campaign/world scope; no stale actor ownership |
| Item definition registry/coordinator | GameInstance | Definition catalog and detached records; world-bound mutation contexts |
| Inventory containers | Component or owned logical record | Stable container identity and authority |
| NPC scheduling | World | Persistent NPC identity; optional detached state restored by a provider |
| Screen/notification state | LocalPlayer | Local player and viewport generation |
| Machine-wide settings application | Explicit shared service/provider | One machine/viewport setting authority, not one competing writer per player |

Introduce or reuse a narrow `FDocOwnerScope` value contract: scope kind, stable subject ID, and campaign/world namespace as needed. Scope kinds can include PlayerProfile, SharedWorld, Party, and Session. Do not assume a network account service, player index zero, controller ID, or array slot is a durable player identity. Anonymous local profiles require saved local IDs. Online authentication, when present, maps into the scope through a host adapter.

A GameInstance subsystem can coordinate multiple owner scopes. It does not imply “one quest/inventory/codex state shared by all players.” Subject authorization is resolved from trusted host context, not from an arbitrary client-supplied owner ID. Shared-world and private-player records must never merge implicitly.

Filter supported worlds/net modes, isolate simultaneous PIE worlds, clear registrations on travel, and avoid `GWorld` or static registries of live objects. LocalPlayer services must rebind world-specific data after travel and release it on player removal. Dedicated servers do not create widgets, play surface audio, or load weather VFX.

## 2.4 Identity, revisions, and time domains

Use different types for semantic Gameplay Tags, definition IDs, stable object IDs, owner scopes, and transient operation handles. Reuse the companion persistent identity:

```text
PersistentObjectId = (WorldNamespaceGuid, StableInstanceScope, LocalObjectGuid)
```

Repeated/nested level placements must have distinct stable instance scopes. Do not save actor pointers, actor labels, array indices, transient NetGUIDs, widget pointers, runtime Smart Object claims, or handle generations as persistent identity.

Mutable records expose a monotonically increasing revision inside an identified epoch. Restoring a save or replacing a world changes the epoch so a stale request cannot match a numerically reused revision. Definition/schema versions are separate from state revisions. Stable graph-node/choice/objective IDs survive array reorder; content deletions require redirects or an explicit orphan policy.

Every timer declares its clock: simulation, world gameplay, monotonic real-time, or explicitly opted-in offline wall-clock. Pause, time dilation, time jumps, travel, and save/load behavior are part of the contract. Never use simulation time for the safety countdown that must revert a paused game's display settings. Duration-based offline progress is not silently enabled by reading the OS clock.

## 2.5 Handles, cancellation, asynchronous work, and leases

Long-lived operations use typed handles with operation ID, owner/context generation, and queryable state. Define applicable states from Pending, Running, Succeeded, Failed, Cancelled, and TimedOut. Exactly one terminal outcome is recorded. Live listeners receive at most one terminal callback on the game thread; destroyed owners receive no unsafe callback, but internal cleanup still completes.

Every request documents duplicate-key behavior, cancellation, timeout, reentrancy, and commit point. Cancel before commit means no committed mutation; cancel after commit must not pretend to undo it. Return the committed result or a clearly defined too-late outcome. Reject stale callbacks after owner loss, graph advance, transaction completion, or world replacement.

Use owner-scoped leases for input, focus, pause, cameras, streaming residency, presentation overrides, and reservations. Release only the caller's lease. UI, Dialogue presentation, Inspection, and Sequences must use the **same `IDocPlayerControlProvider` per local player** rather than independent snapshot-and-restore stacks. Provider implementation stays outside Core's feature logic.

Async loading retains ownership handles, has clear fallbacks, and never synchronously loads substantial media to conceal a miss. Workers operate on immutable copied data; live UObject access, reflected callbacks, and gameplay mutations stay on the game thread unless a particular native API explicitly supports otherwise.

## 2.6 Query, command, and effect boundaries

Queries are side-effect-free. Conditions return `Satisfied`, `Unsatisfied`, or `Unavailable/Error` with evidence, revision, and reason. Missing inventory/time/quest providers must not become “condition satisfied” or be confused with a legitimate zero value. Authoring validation catches required missing capabilities; a running session follows its declared fail/pause/disable policy.

Commands validate owner, epoch, authority, schema, expected revision, and bounds before mutation. Events describe committed changes. Observer reentrancy cannot expose partially updated multi-container state. Queue nested mutations until the current commit completes.

Gameplay tag presence is not a mutable tag store. Grant/remove actions require a declared mutable provider, source-owned grant semantics when claims overlap, and authorization. Never remove another system's tag merely because one dialogue action ended.

## 2.7 Idempotent actions and cross-feature rewards

State mutation and external side effects are different operations. Define an effect key such as:

```text
EffectKey = (OwnerScope, CampaignEpoch, ProducerInstanceId, TransitionOrdinal, ActionId)
```

A bridge delivers a committed intent with this key; a cooperating consumer stores an idempotency receipt with its mutation. A repeated key with the same payload returns its original result. The same key with a different payload fails. A generation/revision alone is not a durable receipt identity.

Use a producer outbox and consumer receipts for retriable reward delivery where durability is claimed. Persist both coherently with affected state. This is a suite design requirement, **not a built-in Unreal guarantee**. Do not claim distributed exactly-once execution for arbitrary Blueprint actions, external services, or unrelated storage backends. Non-idempotent custom actions are classified non-retryable and require an explicit recovery policy.

For a completion reward that fails, preserve completed objective state and expose a pending/failed reward receipt. Do not silently revert a completed quest or duplicate an already granted item. Section 13 defines recovery tests and scope limits.

## 2.8 Persistence without mandatory DocSave

Each persistable base exposes versioned capture/stage/apply/finalize contracts with feature-owned payloads. A separately installed `DocSave` bridge handles durable storage. Without it, the base still works in memory and can use a host persistence provider; it must not advertise disk durability by default.

Restore flow is: validate envelope and owner → migrate → stage detached records → apply state under a restore barrier → resolve optional live references → rebuild derived caches → publish a state-refreshed notification. Restoration must not replay item grants, quest completion actions, dialogue events, codex discovery toasts, or autosave triggers as new gameplay.

Capture dependent records under a shared snapshot revision or explicitly documented stronger/weaker consistency model. Unloaded records remain included. Unknown optional records are preserved or quarantined by policy; unknown critical data fails visibly. Never “repair” incompatible saves by clearing progress and overwriting the original.

## 2.9 Content, localization, packaging, and diagnostics

Each feature declares Primary Asset types, scan/cook rules, asset bundles where appropriate, and missing-definition handling without requiring a replacement Asset Manager. Epic documents Primary Asset IDs and cooking rules separately; both must be addressed. [R3], [R4]

Use `FText` for author-authored display content and stable text identities through the localization pipeline. A culture change invalidates culture-dependent search/sort caches, not persistent record IDs. Shipping cultures and internationalization resources must be explicitly packaged. [R5]

Expose `FDocSystemResult`-compatible outcomes: Success, NoChange, InvalidInput, NotFound, NotReady, Unsupported, Unavailable, PermissionDenied, Conflict, Cancelled, TimedOut, and execution/storage failures as applicable. Include technical diagnostics, localized presentation text, correlation ID, owner scope, and relevant revision. Missing user assets do not justify shipping crashes.

## 2.10 Shared acceptance gates

| ID | Required observation |
|---|---|
| EXP-01 | Core plus each base compiles and starts with every sibling/bridge physically absent |
| EXP-02 | Two owner scopes and two PIE worlds do not leak state or callbacks into each other |
| EXP-03 | Teardown, travel, cancellation, timeout, and late callback release all owned resources |
| EXP-04 | Blueprint-only and independent C++ consumers exercise public APIs without private headers |
| EXP-05 | Definitions remain unchanged across concurrent sessions and restore |
| EXP-06 | Missing providers produce documented unavailable/unsupported outcomes, not false success |
| EXP-07 | Stable identities, revisions, and epochs reject stale/cross-owner commands |
| EXP-08 | Restore is idempotent; no replayed rewards, effects, or destructive orphan cleanup |
| EXP-09 | Content and localization load from an actual cooked Win64 build |
| EXP-10 | Optional adapters have explicit registration/unregistration and present/absent tests |
| EXP-11 | Shared player-control leases coexist with modules 1–10 without stale restoration |
| EXP-12 | Work logs distinguish source, native build, functional, network, and packaging evidence |

---

# 3. Module 11 — DocMapNavigation

## 3.1 Purpose and runtime types

Provide world maps, minimaps, compasses, waypoints, actor/objective/location markers, discovery, fog of war, multiple floors, and off-screen indicator data. The base owns spatial data and math, not widget trees, scene capture rendering, navigation meshes, route solving, or fast-travel execution.

Preserve the original semantic types: `UMapNavigationSubsystem`, `UMapDefinition`, `UMapMarkerDefinition`, `UMapMarkerComponent`, `UMapDiscoveryComponent`, `FMapMarkerHandle`, `FMapMarkerState`, `FMapLayerDefinition`, and `FMapCoordinateTransform`. New implementations should use the `Doc` prefix after an API audit.

Use a LocalPlayer subsystem for player view state. Add a **feature-internal world marker registry**, not a sibling plugin dependency, for actor registration and shared detached marker descriptors. Components register once per world; local players subscribe to permission-filtered views. A headless server may publish shared marker records through a replication adapter without constructing a LocalPlayer subsystem.

## 3.2 Definition and identity contracts

`MapDefinition` contains MapId/MapTag, localized DisplayName, WorldNamespace, projection configuration, origin, authored basis/rotation, positive extents, units, soft texture/material references, supported layers, floors, zoom limits, bounds policy, and discovery settings. A map tag is semantic categorization; a stable MapId distinguishes definitions.

Marker definitions contain semantic tag, localized label template, icon reference, priority, filter tags, discovery requirement, audience policy, and tracking/lifetime defaults. Runtime state contains stable MarkerId, map/world/instance identity, revision, source ID, detached transform, optional weak actor, optional logical region/floor, visibility facts, and origin metadata. Per-player tracked/discovered/filter state is separate from the shared record.

Distinguish marker lifetime policies:

| Policy | On source unregister/unload |
|---|---|
| ActorLifetime | Remove marker and notify views |
| PersistentLocation | Retain authored/saved location and metadata |
| LastKnownDynamic | Retain last authorized location, mark stale, expose observation time |
| ProviderManaged | Ask the provider to update/remove; no invented current position |

A marker retained after an NPC unload is not automatically a live tracking feed. Privacy/audience rules apply before replication and before the view receives the descriptor; hiding a widget is not access control.

## 3.3 Coordinate conversion with a defined inverse

Implement `WorldToMap`, `MapToWorld`, `WorldToNormalized`, `NormalizedToWorld`, `MapToWidget`, and `WidgetToMap` with typed results and explicit projection metadata. Base supports an authored planar orthographic mapping. Custom/non-planar projections use an extension provider; do not claim arbitrary 3D projections are reversible.

For an orthonormal authored map basis `U`, `V`, `N`, origin `O`, and positive lengths `Lu`, `Lv`, define an example mapping:

```text
relative = world - O
u = dot(relative, U) / Lu
v = dot(relative, V) / Lv
height = dot(relative, N)
world = O + u * Lu * U + v * Lv * V + height * N
```

Store origin/basis conventions so this example is not confused with a center-origin texture transform. A 2D coordinate loses height: `MapToWorld` therefore requires a floor plane, caller-supplied height, or explicit ray/placement provider. It returns a point on that declared surface, not an invented ground hit. The round-trip guarantee applies to supported coordinates with height/floor preserved and a declared tolerance.

Keep unclamped mathematical conversion separate from display clipping. Return Inside/Outside and optional clamped display coordinates; never silently clamp and then claim inversion. Reject zero extent, degenerate basis, NaN/Infinity, invalid zoom, and unsupported projection. Handle large coordinates with double-precision calculations before final widget conversion; profile rather than promising a universal precision range.

Map pixel space, normalized map space, local widget coordinates, viewport pixels, safe-area bounds, DPI scaling, zoom, pan, and display rotation are distinct transformations. The UI adapter supplies geometry/DPI. A base query must not need UMG. A world-origin rebase uses an explicit frame/offset conversion; persistent markers must not shift twice or mutate discovery when the engine frame changes.

## 3.4 Marker API, filtering, and bounded updates

Preserve `RegisterMarker`, `UnregisterMarker`, `UpdateMarker`, `SetMarkerVisibility`, `SetMarkerTracked`, `SetWaypoint`, `ClearWaypoint`, `GetMarkersByTag`, `GetVisibleMarkers`, and `GetNearestMarker`. Add request owner/context, scoped handles, change delegates, and query options. Return immutable snapshots or bounded pages, not references to mutable internal arrays.

Unregister releases that source's registration; it does not necessarily erase a persistent marker. Provide separate authorized `RemovePersistentMarker`. Duplicate stable IDs from different sources fail validation instead of last-writer-wins replacement. Same-source updates use expected revision or an explicitly documented upsert operation.

Apply scope/audience → map/world/layer → floor → discovery → tag filters → distance/priority → presentation budget. Specify exact/hierarchical tag matching. Break equal-priority ties by stable ID; never rely on hash-map iteration. Nearest-marker queries declare 2D/3D distance, floor policy, and eligibility filters.

Static markers have no update tick. Dynamic sources use movement/rotation thresholds and configurable cadence. Registration, movement, and filter changes invalidate indexed data. Budget projections and large query results; do not project every world marker every frame or call GetAllActorsOfClass to rediscover them.

## 3.5 Discovery and fog of war

Preserve `DiscoverLocation`, `UndiscoverLocation`, `IsLocationDiscovered`, and `GetDiscoveredLocations`, scoped to owner/map/world. RegionBased discovery stores explicit region/location IDs without requiring `DocRegions`; a region bridge supplies observations. RadiusDiscovery uses bounded persistent coverage in map space with documented resolution and error tolerance.

Recommended radius backend: a chunked bit grid whose cells are revealed by a documented center/coverage rule, with cell size and schema stored in the save. The runtime must bound memory by allocated chunks. Do not store an ever-growing unbounded list of every position sampled. CPU discovery is authoritative for queries; a GPU texture mask is a replaceable visualization, not the only saved truth.

`None`, `GridBased`, `TextureMask`, and `Custom` remain supported configuration/extension concepts. RegionBased and RadiusDiscovery are mandatory base implementations. Other backends require their own capability tests and explicit backlog status. A grid used internally for radius coverage does not by itself mean every advertised grid-editing feature exists.

Discovery is not current line-of-sight visibility. UndiscoverLocation changes the location flag; erasing overlapping radius coverage requires an explicit area reset/provenance policy and must not erase unrelated contributors. Gameplay-triggered discovery is idempotent; restoration refreshes state without duplicate discovery rewards. Fog schema changes require conversion/reset confirmation, never silent loss.

## 3.6 Compass, minimap, floors, and off-screen data

Compass output includes marker handle, signed bearing, distance, forward/right direction, priority, and eligibility. Compute bearing from an explicit heading provider and authored north; do not assume +X or +Y is north. Test normalization at ±180 degrees, same-position markers, arbitrary north rotation, and a missing/invalid heading. Widget screen direction belongs to a supplied view/projection adapter.

Preserve NorthUp, PlayerUp, and CustomRotation. Define whether marker icons rotate independently of the map background. For off-screen indicators, distinguish behind-camera from outside-viewport, project relative to the correct local player's viewport, and clamp to its safe rectangle. Reject invalid/zero-sized view geometry without division by zero.

Floor definitions contain stable FloorId, localized name, map transform, texture, altitude band, priority, and optional region/provider key. Z-band resolution is the baseline; use half-open bands and an explicit top-boundary rule. Overlap requires authored priority, and hysteresis prevents floor flicker on stairs. Support explicit/provider floor override for stacked/overlapping interiors where Z alone is ambiguous. No candidate returns Unknown/Exterior by policy, not a randomly selected floor.

## 3.7 Integrations, persistence, tools, and acceptance

`DocMapNavigationWorldPartition` handles native streamed-source integration. Persistent descriptors already survive ordinary unregister in the base. Optional region, quest, save, and CommonUI bridges consume the public contracts. FastTravel markers do not authorize teleportation; projects supply their own validated travel operation.

Persist discoveries, coverage chunks, custom markers, optional waypoint, filters, and visited regions with owner/map/schema IDs. Do not persist transient handles, actor pointers, or projected widget coordinates. Content updates rebuild derived indexes and quarantine unknown marker definitions.

Editor tools retain Map Definition Editor, Map Bounds Preview, Marker Debugger, Coordinate Conversion Debugger, and Discovery Debugger. Begin with usable Details customization/preview and a functional debugger; a specialized asset editor is a separately visible authoring milestone.

| ID | Acceptance / required test evidence |
|---|---|
| MAP-01 | `Doc.Map.CoordinateConversion`: rotated basis round-trips with declared height; invalid/degenerate transforms fail |
| MAP-02 | `Doc.Map.MarkerRegistration` and `MarkerUnregister`: duplicate IDs, ownership, repeated removal, and lifetime policies |
| MAP-03 | `Doc.Map.MarkerFiltering`: hierarchy, priority ties, audience, layer, distance, and bounded result order |
| MAP-04 | `Doc.Map.MultiFloorResolution`: boundaries, overlaps, hysteresis, unknown floor, explicit override |
| MAP-05 | `Doc.Map.Discovery`: independent owners, region/radius behavior, overlapping reveals, scoped undiscovery |
| MAP-06 | `Doc.Map.PersistentMarkerWithoutActor`: unload/reload preserves eligible descriptors without stale actor access |
| MAP-07 | `Doc.Map.CompassWrap`: arbitrary north, ±180 wrap, same-position target, invalid heading |
| MAP-08 | `Doc.Map.WidgetTransform`: zoom/pan/DPI/rotation and per-player viewport inverse through a supplied adapter |
| MAP-09 | `Doc.Map.WorldOriginAndInstances`: rebase and repeated level placement do not move/collide logical markers |
| MAP-10 | `Doc.Map.SaveRestore`: versioned discovery/custom markers restore without replayed events |
| MAP-11 | `Doc.Map.OffscreenAndWaypoint`: behind-camera/edge data, owner-local waypoints, invalid geometry |
| MAP-12 | Base works without UI, Events, Regions, Save, or World Partition; indexed stress query meets recorded budget |

---

# 4. Module 12 — DocWeather

## 4.1 Purpose and runtime types

Provide renderer-independent environmental state with manual control, weighted scheduling, duration-driven progression, profile transitions, regional overrides, and optional time/render/audio integration. Use a World subsystem, immutable WeatherProfile and WeatherTransitionDefinition assets, WeatherRegionOverrideComponent, and state/target/transition structs. Preserve the original type names semantically and apply Doc-prefixed names only through the compatibility policy.

The weather subsystem computes state. It never searches the entire world for a particular sky Blueprint, implicitly owns every light, or hard-references a third-party weather package.

## 4.2 State schema and units

Preserve CloudCoverage, CloudDensity, Precipitation, PrecipitationType, FogDensity, FogHeight, WindSpeed, WindDirection, Temperature, Humidity, Wetness, SnowAmount, LightningProbability, ThunderProbability, Visibility, and AmbientLightMultiplier. Add explicit units and valid ranges rather than assuming every field is normalized.

| Field family | Recommended contract |
|---|---|
| Coverage, humidity, wetness, snow amount | Unitless fraction in [0,1] |
| Precipitation | Intensity in [0,1] plus semantic phase/type; renderer maps to its own units |
| Cloud/fog density | Dimensionless authored control with declared bounds, not a promise of physical density |
| Wind | Meters/second plus normalized direction or vector; zero wind has an explicit direction fallback |
| Temperature | Degrees Celsius in authored bounds |
| Fog height / visibility | Meters relative to an explicit reference; positive visibility with defined unlimited sentinel |
| Ambient light multiplier | Nonnegative bounded multiplier; renderer adaptation is separate |
| Lightning / thunder | Defined stochastic rates or chance per explicit interval; never chance-per-frame |

For the original `LightningProbability`/`ThunderProbability` fields, either store a probability and its interval or migrate to named rate values. Define whether thunder accompanies an emitted lightning event or can be an independent authored event. Do not treat the two as unrelated random effects by accident. Event scheduling should use a separate RNG stream from profile selection.

Weather tags retain Clear, Cloudy, Fog, Rain/Light/Heavy, Storm, Thunderstorm, Snow/Light/Heavy, Wind/High, and Custom. Mixed weather uses a declared dominant tag plus blend data/context tags. A midpoint in a rain-to-snow transition must not choose its categorical type through undefined numeric interpolation.

## 4.3 Profiles, schedules, and random reproducibility

Profiles preserve tag, localized name, duration range, weather values, target wetness/snow, lightning/visibility settings, audio-profile tag, and gameplay tags. Validate nonnegative weights, positive duration limits where required, at least one selectable entry, and compatible state schema. All-zero weights produce InvalidConfiguration, not index zero.

Mandatory base modes are Manual, WeightedRandom, and TimeDuration. Manual changes do not start a hidden random scheduler. Each scheduling command declares whether it replaces, pauses, or queues behind an existing override. Weighted selection uses a stable candidate order and a dedicated saved RNG state/draw position. Seed alone is insufficient to restore a partially consumed random sequence.

TimeDriven and RegionDriven use provider/bridge inputs. Scripted is an explicit command sequence/provider capability. None requires a sibling plugin to compile. The clock provider declares pause, dilation, forward jumps, backward jumps, and maximum catch-up work. For a large time jump, use a documented bounded fast-forward/catch-up policy; do not replay millions of skipped precipitation/lightning events or claim the same random history when draws were skipped.

Determinism is limited to a fixed algorithm/version, definition set, candidate ordering, clock inputs, and serialized RNG state. Floating-point rendering output across devices is not promised bit-identical.

## 4.4 Transition state machine

Store transition ID/revision, source **evaluated snapshot**, target definition/version, start timestamp, duration, curve policy, elapsed/remaining time, and completion status. Support Linear, CurveAsset, Step, and a Custom provider. Zero duration applies immediately once. Invalid duration or missing required curve fails cleanly.

When replacing a transition, evaluate its current state and use that snapshot as the new source. Do not restart from the old source profile and visibly jump. Cancel/reject/queue policy is explicit; completing/cancelling a transition emits exactly one terminal result. Protect against callbacks or time events from superseded transitions.

Clamp or reject curve overshoot by a per-field policy. Interpolate cyclic directions using normalized vector/shortest-angle behavior with a defined antipodal fallback. Do not linearly interpolate 359 degrees to 1 degree through 180. Derived wetness and snow accumulation have their own bounded response/decay law; a profile change does not necessarily make wet ground instantly dry.

A proposed continuous relaxation rule is `x(t+dt)=target+(x(t)-target)*exp(-k*dt)` with nonnegative `k` and a declared time domain. This is a recommended game-state model, not a physically accurate hydrology or snow simulation. Recompute targets from regional samples without writing local exposure into the global state.

## 4.5 Spatial weather and renderer ownership

Provide `GetGlobalWeatherState` and `SampleWeatherAtLocation(Context, Location)`. Overrides carry stable region/volume identity, priority, weight, blend distance, active window, source owner, and explicitly overridden fields. The base supports explicit/provider spatial influence; `DocRegions` supplies its optional bridge.

Resolve priority and blending deterministically: choose a documented priority band, combine continuous fields with normalized weights/base remainder, and select categorical fields by priority/weight/stable ID. Incompatible overrides are validation failures. Removing a source recomputes from remaining sources; it does not restore a captured old global profile.

Interior suppression usually reduces local rain/snow exposure while outdoor weather continues. Two local players, one inside and one outside, require different precipitation samples. A world-global material parameter collection cannot encode two different local-player values at once; per-player effects/local masks need a suitable adapter. Each renderer documents whether it can represent global state, spatial samples, or per-view state.

Optional adapters retain SkyAtmosphere, VolumetricClouds, Niagara, Materials, and Audio. They bind explicitly assigned components/providers. They expose supported parameters, units, quality/cost limits, and ownership leases. Disabling an adapter releases its own controls without overwriting a newer renderer owner. SnowAmount state does not imply arbitrary surfaces automatically receive snow shading.

## 4.6 Events, networking, and persistence

Preserve OnWeatherStarted, OnWeatherEnding, OnWeatherChanged, OnTransitionStarted, OnTransitionCompleted, and OnLightning. Document whether a changed event means a selected profile, evaluated state revision, or significant sampled delta; avoid broadcasting every parameter every frame. Add cancelled/failed transition outcomes and bounded event identifiers.

Server authority requires a real replicated actor/component transport, not RPCs on the subsystem. Actor/component replication has owning-actor requirements. [R6] Replicate profile IDs, source snapshot or reconstruction data, target, start time, duration/curve identifier, clock epoch, seed/state as required, and state revision. A lone transition-progress float is not sufficient for late join after an interrupted blend.

Clients reconstruct/interpolate presentation against a synchronized clock and reconcile newer snapshots. They do not independently decide gameplay lightning strikes from render ticks. Authoritative lightning/damage intents, when configured, are separated from cosmetic flashes/thunder and sent through a validated consumer adapter. Cosmetic one-shots are not replayed to late joiners unless explicitly requested.

Persist global state, transition source/target, elapsed/remaining duration, clock basis, RNG state/draw position, scheduler mode, current hold duration, and durable regional overrides. Transient component-owner overrides expire on owner loss unless a persistent source explicitly restores them.

## 4.7 Tools and acceptance

Provide Weather Profile Preview, Weather Transition Graph/view, Weather Debugger, and State Inspector with clock, RNG cursor, source/target, sampled location, winning overrides, adapter capability, and recent errors. A transition graph viewer is not a second executable source of truth.

| ID | Acceptance / required test evidence |
|---|---|
| WEA-01 | `Doc.Weather.ProfileApply`: fields/units/tags validate; invalid values do not partially apply |
| WEA-02 | `Doc.Weather.Transition`: linear/curve/step interpolation, wind wrap, categorical policy |
| WEA-03 | `Doc.Weather.TransitionCompletion`: zero duration, interruption, cancellation, one terminal result |
| WEA-04 | `Doc.Weather.RandomSelection`: stable order, zero/negative weight rejection, restored RNG continuation |
| WEA-05 | `Doc.Weather.RegionalOverride`: deterministic overlap/removal and distinct inside/outside samples |
| WEA-06 | `Doc.Weather.SaveRestore`: interrupted transition, RNG, wetness, duration, and source identity restore |
| WEA-07 | `Doc.Weather.ClockDiscontinuity`: pause/dilation/large jump/backward policy with bounded work |
| WEA-08 | `Doc.Weather.RateIndependence`: comparable event rates/state across supported update cadences |
| WEA-09 | `Doc.Weather.LateJoin`: real server/client reconstruct ongoing interrupted transition and reject stale revision |
| WEA-10 | `Doc.Weather.AdapterLifecycle`: missing targets/assets and owner removal leave other renderer owners intact |
| WEA-11 | `Doc.Weather.DedicatedServer`: state runs without renderer, audio, Niagara, or local players |
| WEA-12 | Manual/weighted/duration modes work without Time/Regions/Save; recorded transition/sampling workload stays bounded |

---

# 5. Module 13 — DocSurfaceFeedback

## 5.1 Purpose, types, and ownership

Resolve surface/context/event requests into audio, visual, tactile, and optional gameplay responses. Support footsteps, landings, jumps, slides, impacts, projectile/melee contacts, vehicle/drag/break events, and custom events without requiring movement, weapon, projectile, or combat classes.

Preserve the semantic types `USurfaceFeedbackSubsystem`, `USurfaceFeedbackComponent`, `USurfaceResponseProfile`, `FSurfaceFeedbackRequest`, and `FSurfaceFeedbackResult`. Use a World subsystem for resolution/dispatch and attachable components for request production. Separate pure `ResolveFeedback` from side-effecting `SubmitFeedback` and response-handle control.

## 5.2 Physical surface mapping and request data

Use Physical Materials/Physical Surface types and optional Surface.* tags. Physical surface labels are project-configured, and different collision paths can return different material sources; Epic documents the static-mesh and physics-asset cases. [R7] Therefore never assume `SurfaceType1` means Wood or replace the host's surface table.

An explicit mapping asset resolves physical-material identity first, configured Physical Surface mapping next, then an optional host surface-tag provider, followed by a documented default. Alternate precedence is configurable and visible in diagnostics. Unmapped/null material must be distinguishable from a configured Default surface. Preserve tags for Wood, Metal, Dirt, Grass, Water, Mud, Concrete, Glass, Sand, Snow, and Custom.

Requests retain EventTag, Location, Normal, Velocity, Magnitude, Instigator, HitComponent, PhysicalMaterial, and ContextTags. Add world/owner generation, event/correlation ID, source/limb/channel identifier, optional hit snapshot, timestamp/clock, and cosmetic-versus-authoritative intent. Queued requests use weak live references or copied stable identity; do not keep whole actors alive merely to play a delayed sound.

Define units: world geometry in the declared engine coordinate units, velocity with documented conversion, and magnitude semantics per event (normalized strength, impulse, or speed). Reject non-finite values and invalid normals or apply an explicit safe normal fallback. Trace producers must request returned physical materials and define channel/simple/complex settings. They do not select complex collision indiscriminately. Tests include simple collision, complex collision, skeletal physics assets, landscape/material mapping where supported, and no hit.

## 5.3 Deterministic rule resolution

A response rule has stable RuleId, event match policy, surface match policy, required/blocked context tags, magnitude range, explicit priority, response definitions, and optional cooldown/budget group. Recommended ranking: priority → event specificity → surface specificity → number of satisfied required context constraints → stable RuleId. Exact versus ancestor tag matching is explicit; equally ranked ambiguous rules generate authoring warnings.

Preserve the fallback chain: specific event+surface+context → event+surface → event-only → default. A missing optional effect asset does not silently cause an unrelated rule to execute; use that response's configured fallback or return a partial/unavailable result. Results expose selected rule, resolved material/surface, match path, rejected candidates summary, and dispatched/skipped responses.

Variation selection uses request-local or source-scoped RNG with explicit no-immediate-repeat policy. A footstep sequence is not coupled to weather's random stream. Cache keys include profile/version, event/surface, relevant context, and magnitude bucket only if quantization is documented; invalidation occurs on profile/mapping changes.

## 5.4 Response dispatch and budgets

Retain Sound, MetaSound, Niagara, Decal, Camera Shake, Haptic Feedback, Gameplay Event, Gameplay Tag, Spawned Actor, and Custom Action as response families. Audio/MetaSound control and Niagara are optional bridges; native decal/camera/haptic/custom executors are separately registered capabilities. The core resolver can return a complete descriptor without loading or playing anything.

Cosmetic response failure must not suppress an authoritative gameplay effect or vice versa. Gameplay tag/event responses use explicit providers/bridges and trusted authority, never a client footstep notification as proof of damage. Spawned actors use allowlisted definitions and explicit lifetime/pool participation; the library does not spawn arbitrary classes from untrusted request data.

Budget by world and local view: maximum active voices/effects/decals, distance cull, source cooldown, event frequency, queue length, and stale-request time-to-live. Admission yields Executed, BudgetSuppressed, Unsupported, AssetUnavailable, or Cancelled, not an undifferentiated success. Cheap successful resolution and successful playback are separate outcomes.

Pooling is opt-in for eligible objects. Reset transforms, parameters, delegates, timers, attachments, and ownership on reuse. A pooled callback checks generation; release cannot stop an effect now owned by a later request. Continuous Slide/Drag/Vehicle responses return handles with update/stop semantics and stop on source teardown.

## 5.5 Producers and networking

Support AnimNotify, Gameplay Event, Movement Distance, and Manual Trigger producers. AnimNotifies remain immutable and read the executing mesh/component context. Different limbs/sockets are data-driven; biped names are examples only. Notifies and distance triggers must not both double-fire for the same configured source.

Distance producers accumulate eligible grounded travel, emit bounded steps at stride thresholds, and reset or handle phase on teleport, airborne motion, pose changes, and source activation. Do not emit hundreds of steps after a hitch or teleport. Landing fires once per validated transition. No default per-frame trace for every registered actor.

Cosmetics may run locally/predictively. A network bridge uses event IDs to avoid hearing both predicted and replicated copies, applies relevance/rate limits, and never replicates entire assets. Confirmed gameplay effects have a separate server-authorized path. Cosmetic feedback is normally transient and not persisted; persistent decals/marks require a separately specified consumer, not an accidental save side effect.

## 5.6 Debugging and acceptance

Retain last surface, profile, event, fallback path, magnitude, and spawned response inspection. Add material origin, trace settings, clock age, budget reason, live/pool counts, and request ID. Debug histories are bounded and disabled by default in Shipping.

| ID | Acceptance / required test evidence |
|---|---|
| SFC-01 | `Doc.Surface.PhysicalMaterialResolution`: explicit mapping works without changing host surface indices |
| SFC-02 | `Doc.Surface.ExactMatch`: event/surface/context priority is deterministic |
| SFC-03 | `Doc.Surface.Fallback`: each fallback stage is observable and correct |
| SFC-04 | `Doc.Surface.ContextModifier`: required/blocked tags, magnitude boundaries, hierarchy, ties |
| SFC-05 | `Doc.Surface.NoMatch`: null material, no hit, missing/default profile, invalid normal return declared results |
| SFC-06 | `Doc.Surface.FootstepProducers`: notify/manual/distance inputs, teleport and airborne handling, no duplicates |
| SFC-07 | `Doc.Surface.ImpactAndContinuous`: impact differs from footstep; slide/drag stop cleanly |
| SFC-08 | `Doc.Surface.BudgetAndPooling`: cap, TTL, cooldown, pool reset, generation-safe completion |
| SFC-09 | `Doc.Surface.AsyncOwnerLoss`: unload/cancel prevents late asset callback from spawning orphan feedback |
| SFC-10 | `Doc.Surface.NetworkDedupe`: predicted/confirmed cosmetic reconciliation; forged gameplay effect rejected |
| SFC-11 | `Doc.Surface.DedicatedServer`: no audio/VFX/haptic/viewport creation; authoritative resolver still usable |
| SFC-12 | Base builds without Niagara, Audio, Events, weapons, or movement framework; stress lookup/dispatch profiled separately |

---

# 6. Module 14 — DocDialogue

## 6.1 Purpose and runtime types

Implement branching, condition-driven conversations that run headlessly and bind arbitrary logical participants. Preserve `UDialogueSubsystem`, `UDialogueGraph`, `UDialogueParticipantComponent`, `IDialogueParticipant`, `UDialogueCondition`, `UDialogueAction`, `FDialogueSession`, `FDialogueNodeHandle`, and `FDialogueChoice` as semantic contracts.

The World subsystem coordinates sessions; a separate session object/plain execution state owns mutable variables, visited nodes, choices, timers, outstanding effects, and participant leases. A graph asset does not become a running session. No dependency on a particular NPC class, camera, animation framework, Sequence system, or widget library.

## 6.2 Authoring schema and validation

Retain Line, Choice, Branch, Condition, Event, Jump, Delay, End, and Custom nodes. Graph fields include stable GraphId, content/schema version, start node, node table keyed by stable NodeId, participant roles, typed variable declarations/defaults, and explicit execution policy. ChoiceId and ActionId are stable IDs, never array offsets.

Line nodes preserve Speaker, FText Text, soft VoiceAsset, SubtitleTiming, AnimationTag, ExpressionTag, CameraTag, GameplayTags, AutoAdvance, and Duration. Add skippability, missing-voice policy, and explicit completion condition. Choice nodes preserve localized ChoiceText, Destination, Conditions, HiddenConditions, DisabledConditions, FailureReason, and Actions.

Conditions remain pure and support GameplayTagPresent/Missing, EventState, NumericComparison, ParticipantTag, CustomBlueprint, and InterfaceQuery through explicit providers. QuestState, InventoryItem, KnowledgeEntry, UnlockState, Region, and Time are bridge conditions. BroadcastEvent and tag mutations also require the relevant provider; their name does not create a dependency on Events or GAS.

Base actions retain broadcast/tag/custom semantics; StartSequence, GrantKnowledge, AdvanceObjective, and UnlockContent are bridge implementations. Conditions are not arbitrary code strings, and custom data does not authorize arbitrary class loading. Versioned typed variables define numeric coercion and missing-value behavior.

Validation catches missing starts, duplicate IDs, broken links, invalid variable types, missing participant roles, and invalid action/condition configuration. Unreachable nodes generate warnings unless intentional. Cycles are legal for authored conversations, but unconditional zero-wait cycles must be flagged and bounded at runtime.

## 6.3 Participants, session scope, and arbitration

Participant bindings use a logical role ID plus a stable participant identity and optional weak actor. `IDialogueParticipant` supplies ParticipantId, localized DisplayName, soft Portrait/VoiceProfile, current actor if present, and tags. Two instances using an NPC role label must not be mistaken for the same physical participant.

Each session declares initiator, owner scope, audience, authority mode, and participants. A role can be nonexclusive (narrator) or exclusive (an NPC engaged in one conversation); a participant policy decides. Acquire all required reservations in stable order or roll back the attempted acquisition. No half-reserved conversation may block a participant indefinitely.

Participant death/unload, range loss, travel, owner removal, and takeover use declared Cancel, PauseWithTimeout, ContinueWithoutActor, or RebindByIdentity policies. Rebinding only succeeds in the correct world/instance scope. Never force-load a level merely to resolve a portrait or continue a narrator line.

## 6.4 Execution and public API

Preserve Start, Advance, SelectChoice, Pause, Resume, Cancel, and End. Add GetSessionSnapshot, GetAvailableChoices, variable queries/authorized writes, and terminal delegates. Operations include session handle and expected node/session revision. Return typed failure for stale selection, hidden choice, disabled choice, unauthorized actor, or missing capability.

Recommended state flow:

```text
PendingBindings → Running → WaitingForAdvance / WaitingForChoice / WaitingForDelay / WaitingForAction
                         → Paused
                         → Completed / Cancelled / Failed / TimedOut
```

A presentation animation revealing the rest of a line is not automatically Advance. The UI adapter may first complete text reveal, then submit Advance on a later input. AutoAdvance and manual Advance competing in one frame must transition once. A line shown and a line completed are separate events with stable ordinals.

Bound immediate graph evaluation by a node-step budget per dispatch and a total runaway guard. Yield safely when budget is reached; fail a detected infinite non-yielding loop with an actionable graph/node diagnostic. Delay nodes use declared clocks, not permanent ticking of every dialogue session.

## 6.5 Choice freshness and action commitment

Compute VisibleEnabled, VisibleDisabled, and Hidden states for an explicit snapshot revision. SelectChoice re-evaluates authoritative conditions and validates the ChoiceId still belongs to the active node. A client cannot submit a choice index from an old list, choose a hidden branch, or rely on a cached inventory count.

Inventory spending/grants require a bridge with reservation or transaction semantics; a check-then-remove pair is insufficient under concurrent choices. Resolve required action success before committing the transition when it affects the branch's validity. Cosmetic actions can run after transition according to policy.

Action states include Pending, Committed, Failed, and Compensated where a compensation contract actually exists. Use effect keys/receipts for retriable state changes. Cancellation after an item grant does not silently take it back unless the action advertises a validated compensation. Non-idempotent custom callbacks are not automatically retried after restore.

## 6.6 Voice, localization, UI, and persistence

All authored display text uses FText and packaged localization targets. [R5] Line timing can use fixed duration, a presentation acknowledgement, or available voice duration with defined fallback; missing voice must not deadlock a headless session. Subtitles remain usable without audio. No synthesis, lip-sync generation, or generative dialogue is implied.

A presentation adapter observes session snapshots and requests leased camera/input/animation changes. It never decides authoritative quest/item/unlock outcomes. CommonUI and Sequences remain optional. Audience-specific lines/choices are filtered at the transport, not just in the widget. Local-only, owner-authoritative, and shared-authoritative conversations are separately declared capabilities; no implied voting system.

Persist visited NodeIds, choice history, one-time branches, typed variables, definition version, committed action receipts, and optionally a safe resumable checkpoint. In-flight playback/camera locks are transient. Saving during an action either waits for a safe boundary or records a recoverable intent/receipt consistently. Loading a removed node uses a migration/restart/error policy; never silently jump to the first node and replay grants.

Editor scope retains a Dialogue Graph Editor, node validation, broken-link detection, participant browser, condition preview, search, and reference navigation. Initial Data Asset authoring is a usable interim path, not completion of the eventual graph editor. Editor graph layout is not required in runtime execution data.

## 6.7 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| DIA-01 | `Doc.Dialogue.LineAdvance`: headless/manual/auto advancement has one transition per input/clock race |
| DIA-02 | `Doc.Dialogue.ChoiceBranch`: stable ChoiceIds, branch destinations, revision checks |
| DIA-03 | `Doc.Dialogue.HiddenChoice` and `DisabledChoice`: direct submission cannot bypass visibility/conditions |
| DIA-04 | `Doc.Dialogue.Condition`: typed variables, unavailable provider, pure evaluation, participant tags |
| DIA-05 | `Doc.Dialogue.Jump`: valid loop works; immediate infinite loop is bounded/reported |
| DIA-06 | `Doc.Dialogue.End`: normal end/cancel/failure/pause timeout release reservations and leases |
| DIA-07 | `Doc.Dialogue.ConcurrentParticipants`: exclusive conflict, shared narrator, all-or-none reservation |
| DIA-08 | `Doc.Dialogue.ActionReceipt`: failed action, stale choice, duplicate delivery, post-commit cancellation |
| DIA-09 | `Doc.Dialogue.SaveRestore`: history/checkpoint/content migration without replayed actions |
| DIA-10 | `Doc.Dialogue.VoiceAndLocalization`: missing voice fallback, subtitle timing, cooked second culture |
| DIA-11 | `Doc.Dialogue.AuthorityAndAudience`: real transport validates participant/choice scope; local mode remains independent |
| DIA-12 | Base works without NPC framework/UI/Sequences/Events; UI+Inspection+Sequence lease coexistence verified for selected bridges |

---

# 7. Module 15 — DocQuestObjectives

## 7.1 Purpose, types, and owner model

Support independent objectives and multi-stage quests/missions with event-driven progress, prerequisites, optional/parallel work, tracking, failure, repetition, and rewards. Preserve `UObjectiveSubsystem : UGameInstanceSubsystem`, QuestDefinition, ObjectiveDefinition, ObjectiveCondition, ObjectiveReward, QuestRuntimeState, and ObjectiveRuntimeState semantically.

Partition records by explicit owner scope and campaign/world epoch. A world-bound subscription facade attaches/detaches as worlds change; the GameInstance service retains detached records, not event-bus pointers or actors from a previous world. Shared quests and per-player quests are authored policies. Neither defaults to every player progressing a single global record.

Runtime identity includes QuestDefinitionId, QuestInstanceId, repeat ordinal, StageId, and ObjectiveInstanceId. Standalone objectives have their own instance/owner keys. Two repeat runs must not share progress, timers, event-dedup windows, or reward receipt keys.

## 7.2 Definitions and stage semantics

Preserve objective fields: ObjectiveId, DisplayName, Description, Hidden, Optional, TrackingMode, TargetCount, EventTag, Conditions, CompletionActions, FailureActions, and optional map-marker association. A map-marker association is a feature-neutral logical key or bridge-owned configuration, not a typed dependency on MapNavigation.

Quest definitions retain QuestId, Title, Description, Category, Stages, Prerequisites, Rewards, FailureRules, Repeatable, Trackable, and AutoActivate. Add stable content version, owner/sharing policy, progress evaluation mode, transition priority, replay policy, timer clock, and reward-delivery policy.

The original AllRequired, AnyRequired, Ordered, and Parallel values mix completion and activation concepts. This revision makes them explicit:

| Dimension | Contract |
|---|---|
| Activation | Parallel activates eligible objectives together; Ordered activates the next eligible objective |
| Completion | AllRequired needs every required objective; AnyRequired needs one eligible required objective |
| Optional work | Does not block completion; its grace/close policy is authored |
| Failure | FailStage, FailQuest, RemainActive, or authored branch; precedence over simultaneous completion is explicit |

Retain original presets through documented mappings rather than dropping them. Recommended AllRequired/AnyRequired presets use Parallel activation; Ordered uses AllRequired completion unless configured otherwise; Parallel requires an explicit completion rule. Reject accidental empty-required sets instead of completing a stage immediately unless explicitly authored as an empty/pass-through stage.

Objective lifecycle remains Inactive, Active, Completed, Failed, Cancelled. Hidden is a separate visibility dimension; it is not mutually exclusive with Active. Quest states remain Inactive, Active, Completed, Failed, Cancelled. Define legal transitions, including whether a custom reversible condition can regress before completion; terminal states never reset accidentally.

## 7.3 Evaluators, event identity, and progress semantics

Preserve EventCount, ReachRegion, Interact, Collect, Discover, Wait, Activate, and CustomCondition. These are **base evaluator concepts with provider inputs**, not hard sibling dependencies. A clean host can feed typed progress observations manually. DocEvents is the preferred optional transport; Regions, Interaction, Inventory, Knowledge, Dialogue, Unlock, and combat/Kill bridges translate committed facts.

Every observation includes trusted source identity, EventId, owner/campaign/world context, semantic tag, typed payload, and applicable source revision/sequence. Filter EventTag, TargetTag, SenderTag, payload conditions, and required count with documented tag matching. An arbitrary client broadcast is not proof that an objective occurred.

Distinguish cumulative events from current-state predicates. “Collect three ever” uses receipt/event count; “currently own three” queries inventory state through a provider. Restoring a current inventory snapshot must not create three new collect events. ReachRegion/Discover can be instantaneous events or durable state predicates; define which the asset uses.

Count only while active unless an explicit historical/replay mode is configured. A retained DocEvents payload is not an event history. Recommended default: an event is processed against the active-objective snapshot at dispatch start and cannot also count for the next stage activated by that same event. An authored cascade mode must be bounded and tested.

Dedupe by event identity within a declared replay window or use a source epoch+sequence high-water mark with bounded out-of-order tracking. Do not claim indefinite deduplication after receipts expire. Retain durable terminal/reward receipts for the required lifetime. Counters use checked integers and clamp/reject overflow; target counts are positive unless a specific evaluator defines zero semantics.

## 7.4 State mutation, timers, rewards, and tracking

Process matching observations on the authority context. Commit objective changes → evaluate stage → evaluate quest → stage reward intents → publish ordered change events. Observers see a complete committed revision; callbacks do not recursively modify the same transition. Simultaneous success/failure follows authored precedence and records one terminal transition.

Wait objectives use explicit simulation/real-time policy, store deadlines or remaining duration with epoch, and define pause/time-jump behavior. Offline elapsed time is opt-in. No one-timer-per-frame polling across every inactive quest. Index active subscriptions by event tag/filter where practical and unsubscribe completed work.

Base rewards preserve broadcast/tag/custom semantics through providers; item/knowledge/unlock/sequence rewards are bridges. Quest Completed and RewardDeliveryPending are separate facts. An inventory capacity failure must produce a retryable pending reward or configured claim flow, not disappear or rerun the entire quest. Successful rewards have idempotency receipts.

Preserve SetTrackedQuest, SetTrackedObjective, GetTrackedQuest, and GetTrackedObjectives. Tracking preferences are usually local per player even when progress is shared. A player may untrack without cancelling the quest. Marker/UI adapters subscribe to tracking snapshots; quest logic never manipulates map widgets.

Expose ActivateQuest/Objective, Cancel, authorized progress input, GetSnapshot, EvaluatePrerequisites, and RetryRewardDelivery with typed results. Automatic activation needs bounded dependency evaluation and is suppressed during restore until records are coherent.

## 7.5 Persistence, tools, and acceptance

Persist quest/objective lifecycle, counts, stage, repeat ordinal, instance IDs, time policy, completion timestamps, required dedup state, reward intents/receipts, and optionally local tracked state. Migrations explicitly map removed/reordered stages/objectives. A missing optional objective can be quarantined; a missing critical stage cannot silently complete the quest.

The debugger retains quest, current stage, objective, progress, subscribed event, last matching event, and completion result. Add owner scope, repeat ID, ignored-event reason, unavailable provider, reward ledger, active timer domain, and transition cause. Do not expose another player's private narrative data through an unrestricted runtime debugger.

| ID | Acceptance / required test evidence |
|---|---|
| OBJ-01 | `Doc.Quest.Activation`: prerequisites, standalone objective, owner scope, repeated quest instances |
| OBJ-02 | `Doc.Quest.EventCount`: exact/hierarchical filters, dedup, overflow, active-only semantics |
| OBJ-03 | `Doc.Quest.SequentialStage`: one event does not accidentally satisfy both old and new stage |
| OBJ-04 | `Doc.Quest.ParallelObjectives`: activation and completion policies combine deterministically |
| OBJ-05 | `Doc.Quest.OptionalObjective`: optional failure/closure does not block required work |
| OBJ-06 | `Doc.Quest.Failure`: simultaneous completion/failure, cancellation, terminal-state protection |
| OBJ-07 | `Doc.Quest.SaveRestore`: progress, clocks, repeat identity, migrations, no replayed rewards |
| OBJ-08 | `Doc.Quest.CurrentVersusCumulative`: collect count differs correctly from current possession |
| OBJ-09 | `Doc.Quest.RewardRetry`: full destination, duplicate delivery, partial external failure, receipt recovery |
| OBJ-10 | `Doc.Quest.Tracking`: two local players can track differently without changing shared progress |
| OBJ-11 | `Doc.Quest.WorldAndAuthority`: travel rebind, forged observations, old-world events, real network profile |
| OBJ-12 | Base operates without Events/Inventory/Dialogue/Map; active-index workload avoids global polling |

---

# 8. Module 16 — DocNPCSchedules

## 8.1 Purpose and runtime types

Answer what an NPC should do, where it should be, and when, without replacing locomotion or AI. Preserve NPCScheduleSubsystem, NPCScheduleComponent, NPCScheduleDefinition, NPCActivityDefinition, NPCScheduleState, and NPCActivityRequest as semantic types. The World subsystem owns registered logical NPC records and an indexed schedule queue; components bind loaded representations.

Separate **desired activity**, **requested activity**, and **observed execution state**. A schedule selecting Work does not prove the NPC arrived or started working. The AI/activity executor reports Accepted, Travelling, Started, Completed, Failed, Cancelled, or Unavailable through a narrow interface with request/generation IDs.

## 8.2 Time, entries, and selection

Retain StartTime, EndTime, ActivityTag, TargetRegion, TargetLocation, TargetSmartObjectTag, Priority, Conditions, and FallbackActivity. Add stable EntryId, calendar/time-provider identity, day/week/season filters, recurrence, arrival policy, tolerance, and deadline. Target references are logical IDs or provider-owned locations, not persistent actor pointers.

Support Daily, Weekly, Seasonal, SpecialEvent, TemporaryOverride, EmergencyOverride, and ManualOverride sources. Daily/weekly evaluation uses a calendar-aware time provider; do not assume 24-hour days or seven-day weeks when the host calendar differs. DocTime is preferred through a bridge. A documented standalone/manual time provider must exercise base behavior without DocTime.

Use half-open intervals `[Start, End)` and an explicit cross-midnight rule. Equal endpoints mean invalid/empty unless explicitly marked AllDay. Prior-day spanning entries must be considered at midnight. Tie-break by override class → explicit priority → authored stable order/EntryId. Hash iteration and actor spawn order must not determine the activity.

Preserve example priority values Normal=10, Weather=20, WorldState=30, Alert=40, Quest=60, Scripted=80, Cinematic=100 as editable defaults, not global laws. Conditions return unavailable distinctly. Missing required condition data uses a declared fallback rather than selecting a potentially unsafe activity.

## 8.3 Override ownership and activity lifecycle

Preserve PushScheduleOverride, PopScheduleOverride, ClearOverrides, GetCurrentActivity, and GetExpectedLocation. Push returns an owner-scoped handle with priority, expiry, resume policy, and epoch. Pop releases only that handle. ClearOverrides defaults to the caller's own claims; a privileged administrative clear-all is separate.

Overrides can preempt, wait, or reject based on executor interruptibility. Cancelling a request releases its own target claims and ignores late completion. When a higher-priority override ends, recompute the schedule at **current time**; do not resume an outdated activity snapshot by default. If authored ResumeInterrupted is used, verify its deadline and target still exist.

Activity request fields include persistent NPCId, ActivityTag, target descriptor, urgency, start/expiry, request revision, interruption policy, and fallback. Executors own locomotion/animation/BT/StateTree behavior. Failed paths, missing targets, crowded slots, and actor destruction yield a reason, bounded retry/backoff, and fallback; they do not busy-loop every frame or teleport automatically.

An expected location is an estimate with confidence/source/status. Arrival deadlines may trigger reselection, not a false arrival report. Base scheduling does not assume navmesh reachability, a specific pawn size, or a walking animation.

## 8.4 Smart Objects, StateTree, Mass, and streamed actors

Smart Objects represent activities with a reservation system in Epic's model. [R8] The SmartObjects bridge must own claim/acquire/use/release lifecycle, claim invalidation, contention, timeouts, and actor unload. Search and reservation are separate: finding a suitable object does not reserve it. Validate access before use and release on every terminal path.

An unloaded NPC normally releases live Smart Object claims. Persisting a logical intent is allowed; saving a transient claim handle and pretending the native reservation still exists after reload is forbidden. Shared allocation/reservation services require their own explicit durable contract.

The StateTree bridge exposes CurrentActivity, Target, Urgency, Expiry, and request status; it does not make StateTree required. A simple mock/manual executor proves the base first. The Mass bridge maps entity records to the same stable NPC identities, processes bounded batches, and handles representation swaps without duplicating schedule records. It is a separately tested integration, not a claim that adding one fragment creates a complete crowd simulation.

Optional Regions, Weather, Events, WorldActivation, and streaming bridges translate context/pins. Schedule ownership must stay independent of actor representation. Actor spawn/unload/rebind is authorized by the host's representation provider; the base does not directly spawn every offline NPC.

## 8.5 Offline evaluation and persistence

Retain lightweight NPCId, current schedule, expected region/activity, last evaluation time, and important state. Evaluate to the current time at activation using indexed schedule boundaries or a bounded direct interval lookup. Do not simulate each unloaded NPC every second or replay all skipped animation/activity events.

Forward time jumps compute the current desired state and one coherent change result. Backward jumps recompute desired state but do not undo/regrant irreversible gameplay effects. Any offline production, trading, combat, consumption, or reward simulation is outside this module unless a separately authorized executor provides bounded, idempotent rules.

Reactivation produces a proposed location/activity. The representation provider validates collision, streaming readiness, navigation, and visibility-safe placement. Never teleport a currently visible actor because its offline estimate changed. Unresolved positions remain pending with a fallback; “expected” is not “safe to spawn.”

Persist definition/version, logical NPC identity, current/expected activity, last evaluated time/epoch, eligible durable overrides, and persistent location state. Do not persist AI task pointers, path handles, live claims, or transient timer IDs. Restore requests fresh executor/target binding and suppresses duplicate start rewards.

Debugger scope retains NPCId, activity, schedule, override stack, target region/location, next activity, and transition time; add desired-versus-observed state, claim owner, retry count, next evaluation, and offline policy.

## 8.6 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| SCH-01 | `Doc.Schedule.TimeSelection`: interval boundaries, midnight spans, recurrence, custom calendar |
| SCH-02 | `Doc.Schedule.PriorityOverride`: ties, conditions unavailable, editable source priorities |
| SCH-03 | `Doc.Schedule.OverridePop`: ownership, expiry, recompute-current behavior, noninterruptible executor |
| SCH-04 | `Doc.Schedule.TimeJump`: large forward/backward jumps are bounded and do not replay effects |
| SCH-05 | `Doc.Schedule.OfflineSimulation`: unloaded records advance without per-NPC frequent timers |
| SCH-06 | `Doc.Schedule.SaveRestore`: durable overrides/identity restore without stale claims/tasks |
| SCH-07 | `Doc.Schedule.ExecutorLifecycle`: desired/requested/actual states, late callback, failure/backoff |
| SCH-08 | `Doc.Schedule.TargetUnavailable`: missing region, blocked path, no slot, configured fallback |
| SCH-09 | `Doc.Schedule.SmartObjectContention`: simultaneous claims, invalidation, unload, release on all outcomes |
| SCH-10 | `Doc.Schedule.RepresentationSwap`: actor/Mass or activation swap keeps one logical NPC record |
| SCH-11 | `Doc.Schedule.Authority`: shared schedule decisions originate from trusted authority; dedicated server works |
| SCH-12 | Base works with manual clock/executor without Time/StateTree/BT/Mass/Smart Objects; load-scale profile recorded |

---

# 9. Module 17 — DocKnowledgeCodex

## 9.1 Purpose, types, and scope

Store persistent discoveries, tutorials, evidence, lore, bestiary entries, documents, and progressive knowledge without a mandatory UI. Preserve KnowledgeSubsystem, KnowledgeEntry, KnowledgeCategory, and KnowledgeRuntimeState. The GameInstance service partitions state by explicit player/shared owner scope and campaign/world identity.

Preserve Knowledge.* categories Tutorial, Location, Character, Creature, Item, History, Clue, Document, Mechanic, and Custom. Category membership and relationship meaning are data, not a fixed story taxonomy.

## 9.2 Entry model and state dimensions

Definitions retain EntryId, Category, Title, Summary, Body, Images, Audio, Video, Tags, RelatedEntries, SortOrder, HiddenUntilDiscovered, CanUpdate, and VersionedSections. Use FText for authored visible text, stable EntryId/SectionId, content revision, and soft approved media references. Category/relationship graphs validate missing IDs and cycles according to their semantics.

The original Unknown, Discovered, Updated, Read, Completed, and Hidden labels describe overlapping facts, not one reliable enum. Represent discovery/reveal stage, visibility, content revision, acknowledged/read revision, and completion separately; provide derived display labels for compatibility.

Store DiscoveryTime, LastUpdateTime, ReadTime, UpdateCount, revealed sections/stage, current visible content revision, and last read revision. `Updated` means new visible content exists beyond what was read. MarkRead carries the **displayed revision**; it cannot mark a just-arrived unseen update read because a widget closed late.

Preserve Stage0 Unknown, Stage1 Name, Stage2 Summary, Stage3 Full as an editable example. Projects may define other staged sections. By default reveal is monotonic; deliberate conceal/revoke commands are separate authorized operations with a defined unread/completion policy. Setting Hidden must not erase discovery history.

## 9.3 API and idempotent change behavior

Preserve DiscoverEntry, UpdateEntry, MarkRead, SetEntryState, IsDiscovered, GetEntryState, GetEntriesByCategory, and GetEntriesByTag. Add owner context, effect key for grants, expected revision where applicable, GetVisibleEntry, GetRelatedVisibleEntries, and paginated SearchEntries.

Repeated discovery of the same stage returns NoChange and does not increment UpdateCount or replay OnEntryDiscovered. UpdateCount tracks committed visible updates under a declared policy, not UI refreshes, asset loads, or save restores. SetEntryState is an administrative compatibility operation validated against legal state transitions; ordinary gameplay uses explicit discover/reveal/read commands.

OnEntryDiscovered, OnEntryUpdated, and OnEntryRead include owner, stable entry ID, old/new revision, cause, and visibility-safe summary. Restoration uses state-refreshed notifications, not discovery toasts. Progression conditions query discovered facts, not whether a user happened to open a widget.

## 9.4 Relationships and spoiler-safe search

Support RelatedCharacters, RelatedLocations, RelatedItems, RelatedEvents, and RelatedDocuments as typed relationships using stable IDs. Directed relationships are not automatically bidirectional. Define display sorting and dangling-reference behavior. Cycles may be valid relationships; protect graph traversal with visited sets and depth/result limits.

Search retains title, tag, category, body, and relationship filtering. Results must be based only on currently visible/revealed content for the requesting owner. Hidden titles, unrevealed body text, media names, result counts, autocomplete, and relationship links must not expose undiscovered content through ordinary player APIs.

Build culture-aware search/sort indexes from visible text snapshots, versioned by catalog, culture, and reveal revision. Background indexing operates on copied plain text; apply only if generations still match. A culture change or revoked visibility invalidates relevant cached results. Large result sets are paginated and bounded; changing filters cancels old requests and rejects stale completions.

This prevents unintended disclosure through the library's runtime API. It is **not a promise to hide cooked client assets from determined extraction**. Sensitive server-only narrative data requires appropriate transport/content architecture outside UI filtering.

## 9.5 Media, discovery sources, persistence, and tooling

Inspection, Dialogue, Quest, Region, Item, Event, and Unlock discovery sources remain bridges. A source emits a committed discovery intent; Codex records knowledge; notification/UI bridges decide presentation. No dependency cycle is created by a quest reward discovering knowledge that an unlock later observes.

Images/audio/video are optional soft references. A media adapter defines supported asset types, pause/stop ownership, transcript/caption handling, missing asset fallback, and approved loading. The base can expose descriptors without Media Framework or a playback widget. External URLs never execute arbitrary commands or scripts; supported streaming protocols/content sources require an explicitly configured provider.

Persist discovered/reveal state, read revision, current stage, update count, timestamps, and content/schema IDs. Definition updates preserve progress and reconcile section versions; removed entries use a tombstone/redirect/quarantine policy. Never infer completion just because a definition disappeared.

Retain Knowledge Browser, Category Browser, Missing Reference Validator, Related Entry Graph Preview, and Search Tool. Runtime debugging shows requesting owner/reveal revision so cross-player leaks are detectable. Editor authoring tools may inspect the full catalog; player APIs cannot silently become editor-debug APIs in Shipping.

## 9.6 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| KNO-01 | `Doc.Knowledge.Discover`: duplicate discovery is NoChange with one discovery notification |
| KNO-02 | `Doc.Knowledge.Update`: visible revision/update count and CanUpdate constraints |
| KNO-03 | `Doc.Knowledge.MultiStage`: configured reveals, monotonic default, explicit conceal policy |
| KNO-04 | `Doc.Knowledge.Relationships`: directed/cyclic/dangling references with bounded traversal |
| KNO-05 | `Doc.Knowledge.MarkRead`: stale displayed revision does not acknowledge an unseen update |
| KNO-06 | `Doc.Knowledge.SaveRestore`: timestamps/read stages/removed definitions migrate without toasts/rewards |
| KNO-07 | `Doc.Knowledge.SearchPrivacy`: hidden text/counts/relationships never appear in ordinary scoped queries |
| KNO-08 | `Doc.Knowledge.CultureChange`: search/sort cache invalidates; cooked localized text remains valid |
| KNO-09 | `Doc.Knowledge.AsyncSearch`: result paging, cancellation, stale generations, owner removal |
| KNO-10 | `Doc.Knowledge.MediaFallback`: missing media/captions return usable text/state without playback dependency |
| KNO-11 | `Doc.Knowledge.OwnerScope`: two profiles and shared discovery/read policies remain distinct |
| KNO-12 | Base works without UI/Inspection/Dialogue/Save; large catalog query/index budgets measured |

---

# 10. Module 18 — DocInventoryItems

## 10.1 Purpose, types, and ownership

Provide generic item definitions, runtime instances, containers, equipment state, world pickup handoff, and authoritative transactions. Preserve ItemSubsystem, InventoryComponent, ItemDefinition, ItemInstance, ItemInstanceId, InventorySlot, InventoryTransaction, and InventoryTransactionResult semantically.

The GameInstance item service owns definition lookup and detached container/instance coordination keyed by explicit scope. Actor components expose loaded containers, but container membership must not disappear merely because an actor unloads. World-bound transport/interaction code cannot keep old worlds alive through the GameInstance registry.

Every item instance belongs to exactly one logical container/world-item record or an explicit in-transit/quarantine record. “OwnerId” is derived/validated against membership and authority, not a second independent truth. Equipment is logical membership/slot assignment, not a skeletal attach operation.

## 10.2 Definition versus instance and stack identity

Definitions retain ItemId, localized DisplayName/Description, soft Icon/WorldMesh, ItemTags, MaxStackSize, Weight, BaseProperties, UseActions, EquipTags, Category, and RarityTag. Add schema/content version and explicit stack-equivalence policy. Definitions are immutable and loaded asynchronously with a stable ID catalog.

Instances retain InstanceGuid, Definition, Quantity, Durability, RuntimeTags, CustomData, OwnerId, and CreationTime. Use an authoritative stable instance ID, definition ID/version, checked integer quantity, bounded typed instance data, state revision, and validated container identity. Never store arbitrary unversioned UObject graphs as durable CustomData.

Stacks merge only when the configured stack key agrees: definition plus all relevant durability, ownership/binding, runtime tags, and custom-property values. Similar display names are not stack compatibility. A merge preserves the chosen surviving ID and tombstones the consumed ID. A split creates one new ID and decrements the original exactly once. Define invalidation behavior for handles targeting merged/removed instances.

Recommended weight/capacity accounting uses checked integer base units or documented fixed precision. Reject negative values and arithmetic overflow. Do not compare borderline capacity using inconsistent float rounding between client and authority. Mixed-durability stacks require an explicit aggregate model or are forbidden; do not silently destroy per-item data by averaging.

## 10.3 Containers, slots, and equipment

Retain List, Slot, Equipment, Storage, Vendor, Loot, and Custom container modes. Vendor/Loot are container semantics/tags; this does not implement currency, price negotiation, random loot generation, crafting, or commerce accounting.

Container rules preserve AllowedTags, BlockedTags, MaxSlots, MaxWeight, Stacking, SlotTypes, and UniqueItems. Add stable ContainerId, owner/access policy, container revision, reservation state, enabled/read-only state, and supported operation set. SlotId is stable authored/runtime identity, not a persistent array index. Distinguish an unrestricted list from capacity-limited slots.

Equipment preserves Head, Body, Hand, Primary, Secondary, and Custom logical tags. Multi-slot occupancy, mutually exclusive tags, and uniqueness constraints are evaluated atomically. Equip/Unequip do not attach meshes or grant abilities in the base; bridges execute those effects after committed state and can reconcile on restore.

Nested containers are an explicit extension. A base may reject placing a container inside another container. If supported, detect cycles, define recursive weight/capacity, and enforce depth limits. Do not accidentally enable nesting by accepting arbitrary custom data.

## 10.4 Transaction protocol and concurrency

All mutations use transactions: Add, Remove, Transfer, Split, Merge, Swap, Consume, Move, Equip, and Unequip. Provide PreviewTransaction, SubmitTransaction, GetTransactionResult, and explicit supported cancellation/reservation APIs. Previews are estimates at a stated revision, never promises of a later commit.

A transaction carries stable RequestId/idempotency key, trusted actor/owner context, operation, source/destination IDs, instance/slot IDs, requested quantity, expected container/item revisions, and explicit AllOrNothing/AllowPartial policy. Validate capacity, stack rules, ownership, slot rules, tags, weight, custom conditions, authority, access distance when required by a bridge, and availability of definitions.

Recommended commit protocol:

```text
Resolve trusted context and definitions
→ validate request key/payload and revisions
→ acquire short-lived mutation guards in stable ContainerId order
→ build proposed source/destination snapshots
→ validate every affected invariant
→ commit both/all records and increment revisions
→ record transaction receipt
→ release guards
→ publish immutable deltas and post-commit effects
```

Mutations execute in one authority context, normally the game thread. Logical guards prevent reentrant callbacks from observing half-transfers; they are not an excuse to access UObjects from arbitrary threads. Do not hold guards while awaiting asset loads, player input, or external callbacks. Async preflight reloads/revalidates before commit. Nested observer requests queue until the commit finishes.

Two simultaneous requests for the last item cannot both succeed. A capacity failure on destination cannot leave source decremented. A swap validates both directions together. Same-source/destination operations are explicitly NoChange or a valid slot move, never duplicated inventory.

## 10.5 Results, retries, and conservation

Results preserve Success, FailureReason, MovedQuantity, RemainingQuantity, Source, and Destination. Add outcome, committed revision set, affected instance IDs, immutable deltas, receipt ID, and post-commit effect status. State whether “Moved” means fulfilled quantity for a non-transfer operation. A result reporting Success with nonzero RemainingQuantity requires AllowPartial and a clear PartialSuccess outcome.

AllOrNothing is the recommended default. AllowPartial computes the maximum valid quantity deterministically and reports the remainder. Add means an authorized grant; a client cannot conjure a definition/quantity into inventory simply by calling Add. Source-less removal is an authorized sink with a recorded reason.

Conservation rules: transfer/move/swap preserve total quantity; split/merge preserve quantity and instance data under the stack policy; grants/consumption identify explicit sources/sinks. Test every failure boundary. Negative, zero where invalid, oversized quantities, stale revisions, unknown definitions, duplicate membership, and mismatched request payloads must fail without mutation.

Retried RequestId with identical payload returns the original receipt, not a second transfer. Receipt retention and replay window are documented. Durable reward receipts must be saved consistently with mutated containers; a volatile cache alone cannot protect a grant across crash/load. Irrecoverable receipt/payload mismatch is Conflict, not a new operation.

## 10.6 Item actions and world pickup/drop

Preserve Use, Equip, Drop, Inspect, Consume, and Custom actions as registered handlers. Condition checks are pure. Async Use has an explicit reservation/commit model: validate/reserve → execute declared preparatory work → commit consumption/effect intent → deliver idempotent effect. Arbitrary external side effects cannot be universally rolled back; declare them non-atomic or require a cooperating provider.

`UWorldItemComponent` is an optional component inside the inventory feature that binds a logical item record to a world actor. Interaction bridges request pickup; the base does not require DocInteraction. Competing pickups share a logical item reservation/expected revision. Destroy/hide the world representation only after inventory commit succeeds. Streaming unload is not pickup or intentional destruction.

Drop flow validates an approved representation and safe spawn provider before committing inventory removal, or uses an explicit recoverable escrow record. Spawn failure must return/retain the item under a tested policy. Saved world-item state and container state share one snapshot revision to avoid both copies existing after load. Animation success is not proof of successful pickup/drop.

## 10.7 Networking, persistence, and tools

Inventory transactions are server-authoritative in the network capability. Use a real owned request endpoint and replicated component/state transport; validate caller access to every addressed container. Replicate bounded deltas and stable definition IDs, not all static definitions or private contents to everyone. If a Fast Array or another native delta mechanism is chosen, verify its exact installed API and exercise add/change/remove/late-join behavior.

Prediction is optional. A drag preview does not mutate authority. Rejected transactions reconcile presentation to server revisions. Delta gaps/out-of-order epochs trigger a resnapshot; client sorting never changes authoritative membership. A client request must not choose another user's owner scope or trigger arbitrary actions by class path.

Persist instance IDs, definition versions, quantities, durability, typed custom data, membership, equipment, container rules version where needed, committed receipts, and unresolved intents under a coherent snapshot. Unknown definitions are quarantined/preserved; do not delete items silently. Restore containers/instances first, resolve memberships second, validate uniqueness/conservation, then notify views. Re-equipping restored state must not duplicate ability/tag grants.

Retain Item Definition Browser, Duplicate Item ID Validator, Inventory Debugger, and Transaction Monitor. Include source/destination revisions, validation failures, conservation counters, receipts, reservations, and quarantined records. Tests must not modify a user's real inventory/save slots.

## 10.8 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| INV-01 | `Doc.Inventory.Add` and `Remove`: authorized sources/sinks, invalid quantities, exact failure results |
| INV-02 | `Doc.Inventory.Transfer`: simultaneous last-item requests and failed destination preserve conservation |
| INV-03 | `Doc.Inventory.Split` and `Merge`: IDs, compatible stack keys, durability/custom-data retention |
| INV-04 | `Doc.Inventory.Capacity`: slot/weight/overflow boundaries and explicit partial-success behavior |
| INV-05 | `Doc.Inventory.TagRules`: allowed/blocked/unique/equipment multi-slot constraints |
| INV-06 | `Doc.Inventory.SaveRestore`: identities, membership, equipment, missing definition quarantine, receipts |
| INV-07 | `Doc.Inventory.SwapAndReentrancy`: atomic two-way validation and queued observer mutations |
| INV-08 | `Doc.Inventory.RetryReceipt`: repeated key, changed payload, stale revisions, restart-safe reward retry |
| INV-09 | `Doc.Inventory.PickupRace`: two claimants, unload, commit failure, no duplicate world/container item |
| INV-10 | `Doc.Inventory.DropAndUseFailure`: spawn/action failure, reservation expiry, post-commit effect recovery |
| INV-11 | `Doc.Inventory.NetworkPrivacy`: unauthorized container, spoofed grant, delta gap, rejection, late join |
| INV-12 | Base works without UI/GAS/crafting/combat/Save; random transaction sequences preserve all invariants under load |

---

# 11. Module 19 — DocUnlocksProgression

## 11.1 Purpose, types, and state ownership

Provide generic persistent/temporary entitlements, prerequisite graphs, progress thresholds, and gates. Preserve UnlockSubsystem, UnlockDefinition, UnlockCondition, UnlockAction, and UnlockRuntimeState as semantic types. A GameInstance service partitions records by owner and campaign/world scope; gates for other systems are queried through explicit interfaces.

Retain Unlock.* categories Ability, Area, Feature, Item, Mode, Travel, Interaction, and Custom. Unlocking a logical Area does not load a level; unlocking an Ability does not implement its combat behavior. Consumer adapters own those actions.

## 11.2 Definitions and effective-state model

Definitions retain UnlockId, DisplayName, Description, Category, Prerequisites, Conditions, AutoEvaluate, Permanent, HiddenUntilAvailable, UnlockActions, and LockActions. Add stable version, evaluation policy, threshold/progress input schema, temporary-grant rules, explicit revocation policy, and action retry behavior.

The original Locked, Unlocked, TemporarilyUnlocked, Disabled, and Hidden labels are derived views of independent facts:

```text
PermanentEntitlement
TemporaryGrants[GrantId, SourceOwner, Expiry/Condition/Session/RegionPolicy]
AdministrativeDisabled
VisibilityPolicy
ProgressValues
EffectiveAvailable
```

Recommended availability is “not administratively disabled and either a permanent entitlement or a currently valid temporary grant,” with prerequisite/condition checks applied according to the definition's policy. **Unlock eligibility** and **ongoing availability** are distinct. A permanent earned entitlement normally stays earned when a prerequisite later becomes unavailable; a live-gated ability may be temporarily unusable without deleting its earned entitlement.

Hidden affects presentation, not entitlement. Disabled can suppress availability without erasing a permanent grant. Removing one temporary grant does not revoke another grant or a permanent entitlement. Derived labels must report the underlying reason without impossible combinations of mutually exclusive booleans.

## 11.3 Graphs, conditions, and evaluation

Support single prerequisites, A→B→C chains, AND, OR, nested expressions, and custom conditions through bounded typed data. Validate duplicate IDs, missing prerequisites, self-edges, cycles, and invalid expressions before runtime activation. Detect cycles across loaded content sets/catalog updates, not only within one asset. A missing prerequisite is Unavailable, not satisfied.

Conditions preserve GameplayTag, NumericThreshold, WorldEvent, Time, and CustomBlueprint. QuestCompleted, KnowledgeDiscovered, ItemOwned, RegionVisited, and DialogueState are bridges. Conditions query explicit owner-scoped providers; they do not import sibling definitions into the base.

Manual, EventDriven, Batch, and Auto remain available policies. Build reverse dependency indexes and enqueue dirty nodes on relevant input changes. Evaluate a deterministic topological batch against coherent input revisions. Publish committed changes after the batch; a callback cannot recursively unlock the same graph indefinitely. Repeated no-change inputs must not replay actions.

For numeric progress, distinguish SetValue, AddDelta, and observed current-state input. Check overflow and reject non-finite values. Latching thresholds versus reversible availability must be authored; rapidly oscillating inputs may use explicit hysteresis/cooldown, not hidden arbitrary delays. Evaluate only affected nodes within a work budget; a large batch can yield with visible Pending status.

## 11.4 Public API and temporary grants

Provide EvaluateUnlock, EvaluateDirty, GetUnlockSnapshot, IsUnlocked/IsAvailable, GetBlockingReasons, SetProgress, GrantPermanent, AcquireTemporaryGrant, ReleaseTemporaryGrant, and authorized SetDisabled/SetVisibility operations. Queries are pure. Mutations require trusted context, revision/epoch, and receipt keys where effects are retriable.

Retain DurationBased, ConditionBased, SessionBased, RegionBased, and Custom temporary sources. Duration specifies clock and persistence policy. Session grants expire with their session; region grants are recomputed when membership changes; condition grants fail closed or enter a declared unavailable state when their provider disappears. Never persist raw region/owner pointers or transient handles.

Default duration leases use a runtime clock and do not age while the application is closed unless explicitly configured for trusted offline elapsed time. Simulation-time leases follow configured pause/jump semantics. Expiry processing is indexed/budgeted; no full unlock scan every frame. Backward time changes do not resurrect a grant already permanently expired without a declared rewind model.

Revocation is explicit. A permanent grant is removed only by an authorized revoke/reset/migration action with a documented audit reason. Releasing a temporary source, loading a save, or closing a UI cannot delete it.

## 11.5 Effects, persistence, and authority

Base actions retain BroadcastEvent, GrantGameplayTag, RemoveGameplayTag, and Custom through providers. Tag claims carry source ownership so multiple entitlements granting the same tag do not cancel one another. Inventory/quest/knowledge/gameplay consequences are bridge actions with receipt semantics.

Only real effective-state transitions trigger unlock/lock actions. Retried evaluation, profile load, graph rebuild, and UI refresh do not repeat them. If a post-commit consumer fails, retain the entitlement and track pending/failed delivery; do not toggle the entitlement repeatedly as a retry mechanism. A reset that intentionally reverses effects must use declared compensation, not assume every custom action is reversible.

Persist permanent entitlements, progress values, source/claim metadata, timestamps, eligible temporary-grant descriptors, schema/content version, and required action receipts. Dependency cache state is derived and rebuilt after restore; persisted cache data is only an optimization validated against catalog/version. No transient administrative session state is accidentally made permanent.

For network profiles, authoritative gates remain server-owned. UI preview eligibility is advisory; the action it unlocks must validate current effective availability on authority. Private entitlements/reasons are replicated only to authorized audiences. A client-hidden lock icon is not access control.

Retain the debugger's UnlockId, state, prerequisites, condition results, last evaluation, and source. Add pending dependency revisions, permanent-versus-temporary facts, expiry clock, administrative suppression, and action receipts.

## 11.6 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| UNL-01 | `Doc.Unlock.Basic`: owner-specific permanent entitlement and derived visible/effective states |
| UNL-02 | `Doc.Unlock.Prerequisite`: missing/unsatisfied/unavailable prerequisites are distinct |
| UNL-03 | `Doc.Unlock.AndCondition` and `OrCondition`: nested conditions and deterministic evaluation |
| UNL-04 | `Doc.Unlock.CycleDetection`: self, indirect, and cross-asset cycles report specific paths |
| UNL-05 | `Doc.Unlock.Temporary`: overlapping source grants expire independently without overwriting permanent state |
| UNL-06 | `Doc.Unlock.SaveRestore`: progress/eligible leases/receipts migrate; caches rebuild without actions |
| UNL-07 | `Doc.Unlock.DisabledAndHidden`: presentation/administrative flags never erase entitlement |
| UNL-08 | `Doc.Unlock.DirtyPropagation`: only affected graph work, stable order, bounded reentrancy |
| UNL-09 | `Doc.Unlock.ClockPolicy`: pause/jump/session/region/provider-loss expiry behavior |
| UNL-10 | `Doc.Unlock.ActionReceipt`: duplicate events, consumer failure, overlapping tag grants, authorized revocation |
| UNL-11 | `Doc.Unlock.AuthoritativeGate`: forged/stale client gate use is rejected and private state filtered |
| UNL-12 | Base builds without quests/inventory/knowledge/UI/Events; graph stress and meaningful blocking diagnostics verified |

---

# 12. Module 20 — DocGameFrameworkUI

## 12.1 Purpose and package boundaries

Provide reusable screen management, settings, notifications, prompts, modals, loading state, input-device presentation, and accessibility hooks. This is a framework of concepts and behavior, not a fixed visual theme.

Retain `DocGameFrameworkUI` and `DocGameFrameworkUICommonUI`. The base contains screen/notification/request state, typed settings models, abstract presenter/renderer contracts, and LocalPlayer services. The CommonUI bridge supplies actual activatable widgets, input routing, action bars, back handling, navigation, and focus behavior. An optional lightweight UMG presenter/reference example can prove a non-CommonUI path without changing base ownership.

Preserve UIManagerSubsystem, NotificationSubsystem, UIScreenDefinition, NotificationDefinition, UIScreenRequest, and NotificationRequest semantically. The base must not contain reflected `UCommonActivatableWidget` properties. A strictly presentation-neutral base also moves typed WidgetClass references into bridge-owned screen presentation assets keyed by ScreenId. This explicitly clarifies the original WidgetClass field rather than pretending a soft class pointer makes CommonUI optional.

## 12.2 Layers, screens, and lifecycle

Preserve UI.Layer.Game, HUD, Menu, Modal, Popup, Loading, and Debug. Each layer defines stack/exclusivity policy, relative priority, blocking behavior, and capacity. Layers are semantic IDs; one arbitrary integer z-order is not an input-ownership contract.

Screen definitions retain ScreenTag, layer, input mode, pause policy, cursor policy, stack policy, transition profile, required/blocked tags, and a presentation key. Add stable ScreenId, duplicate policy, payload schema, owner/local-player scope, focus policy, restoration policy, and capability requirements. Widgets remain soft-loaded through their chosen presenter.

Preserve PushScreen, PopScreen, ReplaceScreen, ClearLayer, GetActiveScreen, and IsScreenActive. Add handles, expected revision, structured result, CancelScreenRequest, and state-change delegates. Recommended states:

```text
Requested → LoadingPresentation → Activating → Active ↔ Suspended
                                         → Deactivating → Closed
                                         → Failed / Cancelled
```

Handle cancellation before widget load completes. A stale callback cannot resurrect a closed menu or create a widget for a removed player. Duplicate policies are explicit: Reject, FocusExisting, ReplaceExisting, or AllowDistinctInstances. Distinct instances have distinct request/instance IDs even when their ScreenTag matches.

ReplaceScreen should stage/validate the replacement before closing the old screen unless an explicitly authored destructive transition is chosen. A failed asset load should leave a usable old screen or fallback, not a permanently blocked empty layer. Reentrant push/pop callbacks queue until the current stack mutation completes.

## 12.3 Focus, input, pause, and shared control leases

Each local player owns an independent screen stack, navigation focus history, and input-device state. Use the shared `IDocPlayerControlProvider` to arbitrate input capture, cursor visibility, camera claims, and pause requests with Inspection, Dialogue presentation, and Sequences.

The highest eligible blocking claim controls routing. Releasing one modal must not restore a stale gameplay input mode over another still-open modal or sequence. Focus restoration validates the saved target still exists/is focusable; otherwise choose an explicit fallback within the surviving screen. Clearing one layer never clears another owner's stack or a different player's root.

Preserve GlobalPause, UIOnlyPause, and MultiplayerNonPause. Global pause requires an authorized world-level policy and shared pause leases. UIOnlyPause means local UI/input behavior, not halting authoritative simulation. Multiplayer menus generally do not pause the server. Two split-screen players cannot independently toggle a single global pause flag without arbitration.

CommonUI supplies a specialized input-routing model and viewport integration requirements. The bridge must follow the installed version's setup rather than layering conflicting manual input-mode writes over it. [R9] Base use does not require replacing the host viewport client; enabling a bridge with viewport requirements must expose an explicit integration/compatibility step and a tested composition path.

Enhanced Input actions/contexts provide runtime input mapping concepts. [R10] Adapters install only their owned temporary contexts, handle shared-context ownership deliberately, and never call a global clear-all-mappings operation to close one screen.

## 12.4 Devices, glyphs, remapping, and action presentation

Preserve KeyboardMouse, XboxController, PlayStationController, GenericGamepad, Touch, and Custom presentation categories. Actual hardware may be reported only generically; do not infer a controller brand from an input key alone. Use verified platform/device metadata when available, configurable presentation override, and a generic/text fallback.

Retain OnInputDeviceChanged and GetCurrentInputDevice. Debounce analog noise and insignificant mouse movement; deliberate input switches device presentation. Device disconnect/focus loss has an explicit recovery policy. Events are scoped per local player, not a process-global last-device variable.

Resolve a logical Input Action or action ID using the player's **effective current mapping/profile/context**. Glyphs support alternate bindings, key chords, hold/toggle presentation, unknown keys, and missing art. Rebinding invalidates cached glyphs. Never display only the default key when the player remapped it.

Input settings preserve mouse/controller sensitivity, InvertX/Y, dead zones, remapping, and hold/toggle preferences. Remapping declares conflicts, reserved navigation keys, reset/apply/cancel, and keyboard/gamepad scope. A player must retain a working confirmation/back path during remap. Exact Enhanced Input user-settings APIs and CommonUI integration behavior require local 5.8.3 validation; no blanket maturity/production-support claim is made here.

## 12.5 Typed settings registry and ownership

Preserve Video, Audio, Input, Gameplay, Accessibility, and Language categories. Each setting describes stable ID, FText label/help, type, current/default/pending value, valid range/options, capability availability, dependency rules, storage scope, preview/apply/restart policy, and provider.

Machine/display settings use one shared settings authority and revision. Per-player sensitivity/remaps and preferences use explicit profile scopes. Two local-player settings screens edit snapshots; conflicting apply requests return Conflict or merge only independent fields under a documented rule. They must not become competing writers to the same display mode.

Video retains Resolution, WindowMode, VSync, FrameLimit, Scalability, AntiAliasing, UpscalingMode, ResolutionScale, ShadowQuality, TextureQuality, EffectsQuality, PostProcessQuality, and ViewDistance. `UGameUserSettings` provides applicable native settings plus confirmation/reversion functions; its `ApplySettings` also saves settings, so it must not be treated as a harmless preview API. [R11]

Hardware/renderer-specific options such as upscaling are capability-provided. Missing native/third-party support removes or disables the option with a reason. Do not promise vendor-specific features or enable arbitrary CVars by user-supplied string. Preserve unsupported saved preferences without blindly applying invalid values.

Audio preserves Master, Music, SFX, Dialogue, Ambience, UI, and Custom channels via Sound Class/Submix/provider adapters. Settings values do not prove every host sound is routed through the selected bus; setup includes a diagnostic sample and declared routing coverage. Avoid global audio changes when editing one player's UI preview unless shared behavior is explicit.

## 12.6 Apply, preview, confirmation, and recovery

Settings editing uses a transaction: read snapshot/revision → edit pending values → validate capabilities/conflicts → apply preview or durable changes through the correct provider → confirm or revert → persist confirmed values. Cancel restores only values owned by that edit transaction and does not overwrite newer unrelated updates.

Display mode/resolution preview requires a **monotonic real-time confirmation deadline**, an obvious keep/revert prompt, and a last-known-good checkpoint. The deadline still works while gameplay is paused or time is dilated. Losing the screen/owner or reaching timeout triggers the tested revert path. Persist unconfirmed-preview metadata so abnormal termination can recover to safe confirmed settings on next launch.

Use the installed `ConfirmVideoMode`/`RevertVideoMode` behavior appropriately; explicitly verify whether reapplication is needed in the selected flow. [R11] A mode setter returning without error is not proof the monitor applied it. Check actual reported state and preserve a safe fallback. Reversion failure becomes a visible recovery state, not a success message.

Providers with side effects declare transactional limits and restart requirements. A setting unsupported mid-edit (device removed, renderer changed) must be revalidated. Do not write an entire old settings file over newly changed unrelated values.

## 12.7 Accessibility, language, and localization

Retain SubtitleSettings, TextScale, HighContrast, ColorFilters, ReduceMotion, CameraShakeStrength, HoldToToggleAlternatives, and InputAssistance. Expose capability, persisted preference, and actual consumer acknowledgement. A toggle in a settings list is not proof the game applies its effect.

Reference presentation must demonstrate keyboard-only and gamepad-only navigation, visible focus, scalable/readable text, sufficient layout tolerance for long localized text, safe areas, reduced motion, and subtitle presentation without audio. Screen-reader integration, platform certification, and universal accessibility compliance are not implied unless explicitly implemented and tested.

Language changes update visible FText and layout/glyph/search caches through declared notifications. Preserve valid defaults and fallback culture. Layout tests include a long-text/pseudo-localized case and an RTL-capable fixture where the selected presenter declares support. Shipping locale resources must actually be cooked. [R5]

## 12.8 Notifications and confirmation dialogs

Notification fields retain NotificationTag, Title, Message, Icon, Priority, Duration, Category, MergeKey, Sound, and Action. Add owner scope, stable request ID, payload version, enqueue time/clock, expiry, and action-handler key. Categories retain Info, Success, Warning, Error, Discovery, Objective, Unlock, and Custom.

Queue policies remain Stack, Replace, Merge, PriorityInterrupt, Persistent, and Timed. Define bounded visible/queued counts, stable priority/FIFO tie-breaks, merge-key scope, duplicate identity, expiry while hidden, and overflow behavior. Merging two item-grant notifications does not repeat the actual grant. High-priority notifications do not starve lower-priority messages indefinitely without an explicit policy.

A paused game's notification duration uses the declared real-time/UI clock by default, not a frozen gameplay timer. Critical failures have an explicit persistent/dismissible policy. Sound and action execution use owned providers; action tokens resolve approved handlers and revalidate current context rather than invoking serialized arbitrary callbacks.

Dialogs preserve Title, Body, ConfirmText, CancelText, ThirdAction, and callbacks. Return one result: Confirmed, Cancelled, ThirdAction, OwnerDestroyed, or failure as applicable. Double click, back, timeout, parent removal, and travel cannot invoke multiple terminal callbacks. No default affirmative action on close for a destructive prompt.

## 12.9 Loading, travel, persistence, and tools

Preserve ShowLoading, HideLoading, SetLoadingProgress, and SetLoadingStatus but back them with owner-scoped loading leases. One operation finishing cannot hide another's loading display. Progress is Indeterminate unless the producer can provide a meaningful denominator; never manufacture a smooth percentage from elapsed time. Cancellation/error is visible and distinct from completion.

DocStreaming remains independent of UI. A bridge observes operation state and owns the loading request. In-game overlay support is distinct from pre-LocalPlayer startup or blocking map-load presentation. The latter needs a separately validated native loading-screen adapter; a LocalPlayer subsystem alone does not guarantee visible rendering during a blocked game-thread load.

Save preferences through their defined settings/profile storage, not transient widgets/stacks. Optional screen-restoration descriptors contain stable ScreenId and validated plain payload only. Modals, callbacks, focus pointers, notification timers, unconfirmed destructive actions, and old-world references are not blindly restored after travel.

Retain debugger views for active layers, stack, focus, input device, input mode, notification queue, and current settings. Add active leases, pending loads, settings transaction/revision, remaining confirmation time, presentation capability, and recent stale-callback rejection.

## 12.10 Acceptance

| ID | Acceptance / required test evidence |
|---|---|
| UI-01 | `Doc.UI.PushPop`: layers, duplicate policy, replace failure, reentrant changes, async cancellation |
| UI-02 | `Doc.UI.ModalBlocking`: shared control claims, valid focus restoration, one modal cannot release another |
| UI-03 | `Doc.UI.InputDeviceSwitch`: hot swap, analog-noise debounce, unknown-device fallback, per-player scope |
| UI-04 | `Doc.UI.NotificationQueue`: merge/replace/priority/TTL/overflow and no duplicate gameplay action |
| UI-05 | `Doc.UI.SettingsApply`: typed validation, machine/profile separation, concurrent edit conflict, actual consumer effect |
| UI-06 | `Doc.UI.SettingsRevert`: paused real-time timeout, screen loss, failed mode apply, restart recovery |
| UI-07 | `Doc.UI.GlyphAndRemap`: effective rebound/chord/alternate input presentation and working navigation escape |
| UI-08 | `Doc.UI.DialogExactlyOnce`: double input, back, parent teardown, third action, one terminal result |
| UI-09 | `Doc.UI.LoadingLeases`: overlapping loads, unknown progress, failure, travel, no premature hide |
| UI-10 | `Doc.UI.AccessibilityAndCulture`: keyboard/gamepad navigation, focus/text scale/reduced motion and cooked culture fixture |
| UI-11 | `Doc.UI.SplitScreenAndControlInterop`: separate roots/notifications; shared pause/display authority; Inspection/Sequence coexistence |
| UI-12 | Base works without CommonUI/UMG presenter/Streaming; selected CommonUI bridge works in packaged Win64 and cleans up on player removal |

---

# 13. Cross-System Integration Rules

## 13.1 One owner for each decision

A bridge translates a producer's committed state or a validated request into a consumer's public API. It does not reach into private arrays, modify Data Assets, bypass authority, or replicate state through a second competing transport.

| Integration | Producer owns | Bridge owns | Consumer owns |
|---|---|---|---|
| Quest → Map | Tracked objective and target descriptors | Per-player marker handles and updates | Marker registry, discovery filtering, projections |
| Dialogue → Knowledge | Authorized dialogue action intent | Scoped request translation and receipt correlation | Reveal/read/content state |
| NPC Schedule ← Time | Clock/calendar state | Clock-domain conversion and discontinuity notification | Desired activity selection |
| Weather → Audio | Evaluated environmental state and lightning events | Audio profile mapping and request leases | Playback, limits, channel arbitration |
| Inventory → Unlock | Committed container changes and authorized ownership query | Affected-condition invalidation | Unlock evaluation and grant state |
| Quest → Inventory | Reward intent with stable effect key | Delivery/retry coordination | Validated inventory transaction and receipt |
| Dialogue ↔ Sequence | Conversation session and sequence playback session, respectively | Waiting/cancellation policy and shared control claims | Each participant's own lifecycle |
| Streaming → UI | Loading state and readiness/error information | Owner-scoped loading presentation request | Local screen/notification presentation |
| Inspection → Knowledge | Validated discovery result | Entry/reveal mapping | Persistent knowledge record |
| Save ↔ Feature | Snapshot/restore operation and storage envelope | Feature record registration and lifecycle coordination | Feature payload validation and restoration |

Merely enabling two plugins is not an integration. Each integration requires an installed bridge or a documented host adapter, configuration, and a verified behavior test.

## 13.2 Change envelopes and delivery contracts

Cross-system notifications must carry sufficient context to reject wrong-owner and stale work: producer identity, owner scope, world/session generation where relevant, event or transition ID, producer revision, semantic tag, and a validated payload. Carry persistent IDs rather than assuming the referenced Actor remains loaded.

The exact envelope belongs to the producer or integration contract. Do not force `DocEvents` types into Core or into every feature's public API. A DocEvents bridge may translate the feature's native delegate into a bus message.

State changes are published after their owning mutation commits. Consumers must not infer durable success from a speculative UI response, a predicted cosmetic event, or an unfinished transaction. An integration documents whether it consumes a snapshot, a delta, a one-shot occurrence, or a durable action intent; these are not interchangeable.

Subscribe with explicit owner/world context and unregister on adapter shutdown. On binding to an already-running producer, request its current snapshot and revision, then apply newer changes without a gap or double count. A retained last-event value is not a replayable history.

## 13.3 Cross-system rewards and recovery

Use the effect key, consumer receipt, and pending-action contracts in Section 2. A recommended local save-compatible sequence is:

```text
Producer commits progress plus pending reward intent
→ bridge requests consumer operation with EffectKey
→ consumer validates and commits mutation plus receipt
→ producer records acknowledged delivery
→ coordinated snapshot preserves progress, receipt, and delivery state
```

A crash may happen between any two steps. A retry must return the consumer's existing receipt instead of applying the reward again. This requires compatible persistence and retention on both sides; simply naming a request “idempotent” does not create crash safety.

The local base suite does not promise a distributed transaction across independent files, remote services, or unrelated backends. An adapter that cannot provide durable idempotency must advertise that limitation and define a recoverable pending/manual-resolution state. Arbitrary Blueprint callbacks cannot be assumed reversible.

A save/load barrier suppresses normal gameplay side effects while records are staged and references resolve. After restoration, consumers rebuild derived views and producers resume only genuinely pending intents. Restore notifications are explicitly distinguished from new gameplay occurrences.

## 13.4 Shared local-player control

`DocGameFrameworkUI`, `DocInspection`, `DocSequences`, and any dialogue presentation adapter must use the same local player's control-provider instance when they manipulate input, camera, cursor, focus, or pause. Reuse the companion handoff's `IDocPlayerControlProvider` contract rather than inventing four incompatible lock systems.

The provider resolves concurrent claims. A release affects only its claim; remaining claims are reevaluated against current state. Never restore a cached pre-dialogue camera or input mode over an active inspection or newer modal. A world-level pause request additionally uses the host's shared-world arbitration policy; it is not a private per-player boolean.

## 13.5 Integration failure behavior

A missing optional provider returns a specific unavailable/unsupported result. A required operation cannot silently become a successful no-op. Pure presentation integrations may use an explicitly configured fallback; gameplay spending, rewards, quest completion, and entitlement checks fail closed or remain pending according to their documented contract.

A bridge must not resurrect a cancelled session when an asset or target finally appears. Source cancellation, player removal, world travel, and adapter unloading release bridge-owned handles. Persistent pending rewards belong to durable producer state, not to a transient bridge object's lifetime.

Cross-module condition/action loops require validation and a runtime work budget. A quest that unlocks content which activates that same quest must not cause unbounded synchronous recursion. Use stable transition IDs, change detection, a bounded dispatch queue, and an actionable cycle diagnostic.

---

# 14. Recommended Development Order

Preserve the original Phase 5–8 order. Implement one working vertical slice at a time; each base is exercised without sibling plugins before integration work can hide missing contracts.

## 14.1 Milestone 5.0 — Foundation compatibility audit

Audit the actual repository and the companion Revision 2 contracts. Verify Core result types, persistent identity, request generations, tag-provider semantics, control leases, and public API exports. Identify existing implementations instead of assuming the first ten systems are complete.

Create an expansion traceability file and clean host fixtures. Resolve the engine/toolchain gate. Implement only the minimal missing shared contracts needed by the next module; do not start building all twenty systems to clear an unrelated dependency.

**Exit:** Core-only consumer build when available; proposed compatibility changes recorded; exact blocker/evidence state; isolated SurfaceFeedback fixture ready. Existing working source and assets remain intact.

## 14.2 Phase 5 — Environmental / Navigation

| Milestone | Base vertical slice | Exit gate |
|---|---|---|
| 5.1 — SurfaceFeedback | Request → physical-surface mapping → deterministic response resolution → inspectable result; manual and notify/contact producers | SFC-01–SFC-12 base-applicable paths, including fallback and bounded failure; optional sinks reported separately |
| 5.2 — MapNavigation | Registered/detached markers → per-player filtering → coordinate/compass output → region/radius discovery | MAP-01–MAP-12 base-applicable paths, including unload identity and floor boundaries |
| 5.3 — Weather | Profiles → deterministic scheduling → interruptible transitions → sampled state | WEA-01–WEA-12 base-applicable paths, including seed-state restore and dedicated-server-safe core |

Use in-plugin fake providers and diagnostic consumers for isolated tests. A fake audio sink proves dispatch and ownership, not audible playback. A numerical weather preview proves state, not sky rendering.

## 14.3 Phase 6 — NPC / Narrative

| Milestone | Base vertical slice | Exit gate |
|---|---|---|
| 6.1 — NPCSchedules | Clock provider → schedule/override selection → desired activity → executor acknowledgment | SCH-01–SCH-12 base-applicable paths; time jumps and inactive records without per-second replay |
| 6.2 — Dialogue | Headless graph execution, participant binding, conditions, choice revalidation, bounded async actions | DIA-01–DIA-12 base-applicable paths; concurrent session and restore fixtures |
| 6.3 — QuestObjectives | Standalone objective, stages, owner-scoped progress, event adapters, pending reward contract | OBJ-01–OBJ-12 base-applicable paths; duplicate occurrence and reward-retry fixtures |
| 6.4 — KnowledgeCodex | Progressive reveal, read/content revisions, relationships, scoped search | KNO-01–KNO-12 base-applicable paths; spoiler filtering and migration fixtures |

Quest rewards and inventory conditions initially use contract fakes, not premature inventory implementations hidden inside a quest plugin. Real consumer bridges are verified when their owning features are available.

**Narrative authoring follow-through:** the original eventual Dialogue Graph Editor remains a named subsequent authoring milestone, not a requirement to delay headless runtime development. Implement graph editing, validation, search, reference navigation, and undo/redo only after the serialized graph contract is stable. A plain Details panel is not completion of that eventual graph editor.

## 14.4 Phase 7 — Progression / Item Systems

| Milestone | Base vertical slice | Exit gate |
|---|---|---|
| 7.1 — UnlocksProgression | Prerequisite graph, owner-scoped permanent/temporary grants, event-driven reevaluation | UNL-01–UNL-12 base-applicable paths; overlapping grants and bounded feedback loop |
| 7.2 — InventoryItems | Item instances/containers, atomic transactions, receipts, equipment policy, world-item lifecycle | INV-01–INV-12 base-applicable paths; conservation, concurrency, failed drop and coherent restore |

After 7.2, replace condition/reward fakes in the integration host with real adapters. Verify the full quest → reward → inventory → unlock chain under retries and restoration. A test double remaining in the production integration path must be reported as incomplete.

## 14.5 Phase 8 — Framework UI

Build UI last as originally requested, using proven headless state and operation contracts. This does not prohibit earlier minimal diagnostic consumers or unit-test presenters.

| Milestone | Deliverable | Exit gate |
|---|---|---|
| 8.1 — UI base | Screen/layer state, notifications, typed settings operations, control leases | UI base-applicable requirements without UMG/CommonUI |
| 8.2 — CommonUI presenter | Usable generic screens, focus/navigation, input integration, modal and loading presentation | UI-01–UI-12 presenter-applicable paths in PIE and cooked Win64 |
| 8.3 — Settings safety | Actual settings consumers, video preview/confirm/revert, remapping, accessibility | Paused-time and restart-recovery tests; no decorative nonfunctional settings |
| 8.4 — Consumer presentation | Selected map/dialogue/codex/inventory/unlock views through their public APIs | Each named bridge has an isolated absence test and a real presentation fixture |

## 14.6 Integration profiles and truthful release claims

These are recommended **named verification profiles**, not additional mandatory dependencies:

| Profile | Minimum verified integrations | What it does not establish |
|---|---|---|
| `ExpansionBase` | Core + each of ten bases, all mandatory base behavior, isolated consumers | Any renderer, CommonUI, storage backend, network transport, or optional AI integration |
| `PersistenceAndProgression` | Real DocSave bridges for persistent features; quest/inventory/unlock receipts and coherent restore | Remote-backend atomicity or unrestricted arbitrary Blueprint rollback |
| `WorldAndEnvironment` | DocTime/Regions where selected; one real weather renderer/material adapter; surface audio and Niagara sinks; named streamed-marker path | Every sky package, every surface/render feature, or all World Partition modes |
| `NarrativeAndNavigation` | Quest-to-map and dialogue-to-knowledge bridges; selected inspection/sequence adapters | A full narrative game or completed custom graph editor |
| `FrameworkPresentation` | CommonUI framework presenter and explicitly listed consumer views | Every platform/controller glyph pack, vendor UI, or certified accessibility compliance |
| `NetworkedExpansion` | Actual authorized transports for selected authoritative features; dedicated server and late join | Networking of an unlisted feature or backend persistence |
| `AdvancedAIAdapters` | Individually verified StateTree, Smart Objects, or Mass bridges | Support for all three merely because one works |

The original optional bridge list and extension modes remain tracked in Section 15 and Appendix C. A base release may precede those integrations, but it must label unsupported capabilities and must not claim that the whole original capability set is finished. Original required base behavior is not silently reclassified as optional.

---

# 15. Dependency Matrix

## 15.1 Base dependencies

`Required` below means a shipped compile/load dependency. `Adapter` means an optional integration with a separately installed implementation, not a reference to that plugin from the base. “Recommended” in the original meant desirable integration; it did not override individual enablement.

| Base plugin | Core | Events | Regions | Time | Save | Presentation | World Partition |
|---|---|---|---|---|---|---|---|
| DocMapNavigation | Required | Adapter | Adapter | — | Adapter | Adapter | Adapter |
| DocWeather | Required | Adapter | Adapter | Adapter | Adapter | Renderer adapters | — |
| DocSurfaceFeedback | Required | Adapter | — | — | No transient-FX save | Audio/VFX adapters | — |
| DocDialogue | Required | Adapter | Adapter | Adapter | Adapter | Adapter | Actor lifecycle via provider |
| DocQuestObjectives | Required | Recommended adapter | Adapter | Adapter | Adapter | Adapter | Target resolution via provider |
| DocNPCSchedules | Required | Adapter | Adapter | Recommended adapter | Adapter | No mandatory presenter | Lifecycle adapter |
| DocKnowledgeCodex | Required | Adapter | Adapter | — | Recommended adapter | Adapter | — |
| DocInventoryItems | Required | Adapter | — | — | Recommended adapter | Adapter | World-item lifecycle provider |
| DocUnlocksProgression | Required | Recommended adapter | Adapter | Adapter | Recommended adapter | Adapter | — |
| DocGameFrameworkUI | Required | Adapter | — | — | Preference adapter where needed | CommonUI/other presenter | — |

Essential native dependencies are feature-specific. Core/Engine/GameplayTags and reflected object facilities are normal building blocks; Optional Niagara, CommonUI, GAS, Mass, StateTree, Smart Objects, and audio-specialized modules must not leak into unrelated bases. Even an engine-integrated feature can need a separate architectural adapter without necessarily being a separately downloadable engine plugin. Verify module ownership locally.

## 15.2 Preserve the original named bridges

All names here describe intended suite packages, not a claim that code is already installed. A bridge's `.uplugin` and `.Build.cs` declare its actual participants.

| Bridge | Participants / responsibility |
|---|---|
| DocMapNavigationCommonUI | MapNavigation + CommonUI; map/compass presentation |
| DocMapNavigationWorldPartition | MapNavigation + verified native World Partition surface; stable authored/detached marker integration |
| DocWeatherSkyAtmosphere | Weather + verified atmosphere integration; owned renderer binding |
| DocWeatherVolumetricClouds | Weather + verified cloud integration; cloud parameter mapping |
| DocWeatherNiagara | Weather + Niagara; bounded precipitation/lightning effects |
| DocWeatherMaterials | Weather + native material integration; explicitly owned parameter bindings |
| DocWeatherAudio | Weather + AdaptiveAudio, or a clearly documented native-audio adapter contract; select one declared backend rather than hidden runtime imports |
| DocSurfaceFeedbackNiagara | SurfaceFeedback + Niagara; response dispatch and pooling lifecycle |
| DocSurfaceFeedbackAudio | SurfaceFeedback + native audio; optional AdaptiveAudio integration is an additional declared participant |
| DocDialogueCommonUI | Dialogue + CommonUI; transcript/choice presentation |
| DocDialogueSequences | Dialogue + Sequences; sequence action/wait/cancel bridge |
| DocQuestObjectivesMapBridge | QuestObjectives + MapNavigation; per-player tracked markers |
| DocQuestObjectivesKnowledgeBridge | QuestObjectives + KnowledgeCodex; conditions/reveal actions |
| DocNPCSchedulesStateTree | NPCSchedules + StateTree; desired activity adapter, not a new locomotion system |
| DocNPCSchedulesMass | NPCSchedules + selected Mass modules; bounded entity/record integration |
| DocNPCSchedulesSmartObjects | NPCSchedules + Smart Objects; real reservation/execution lifecycle |
| DocKnowledgeCodexCommonUI | KnowledgeCodex + CommonUI; scoped search and reveal-safe presentation |
| DocInventoryItemsCommonUI | InventoryItems + CommonUI; transaction-driven views without UI-owned items |
| DocInventoryItemsGAS | InventoryItems + GameplayAbilities; explicit use/equip effects and authority |
| DocUnlocksProgressionCommonUI | UnlocksProgression + CommonUI; availability and entitlement presentation |
| DocGameFrameworkUICommonUI | GameFrameworkUI + CommonUI/CommonInput and verified Enhanced Input integration; screen/focus/settings presenter |

A consumer-specific CommonUI bridge is not automatically dependent on `DocGameFrameworkUI`. If it uses that framework's layers and notification APIs, declare the third participant or supply a separately named framework integration. Avoid pulling every consumer bridge into one monolithic UI package.

## 15.3 Additional bridges needed by the contracts

Create only bridges used by an actual milestone. Recommended naming patterns are:

```text
DocWeatherTime                 Weather + Time
DocWeatherRegions              Weather + Regions
DocNPCSchedulesTime            NPCSchedules + Time
DocNPCSchedulesRegions         NPCSchedules + Regions
DocQuestObjectivesEvents       QuestObjectives + Events
DocUnlocksProgressionEvents    UnlocksProgression + Events
DocDialogueKnowledge           Dialogue + KnowledgeCodex
DocQuestObjectivesInventory    QuestObjectives + InventoryItems
DocUnlocksProgressionInventory UnlocksProgression + InventoryItems
DocInspectionKnowledge        Inspection + KnowledgeCodex
DocStreamingFrameworkUI       Streaming + GameFrameworkUI
DocSaveMapNavigation           Save + MapNavigation
DocSaveWeather                 Save + Weather
DocSaveDialogue                Save + Dialogue
DocSaveQuestObjectives         Save + QuestObjectives
DocSaveNPCSchedules            Save + NPCSchedules
DocSaveKnowledgeCodex          Save + KnowledgeCodex
DocSaveInventoryItems          Save + InventoryItems
DocSaveUnlocksProgression      Save + UnlocksProgression
```

The names are recommendations; preserve existing published names through a compatibility record. Shared bridge helpers must remain small and must not import all their potential consumers. Save bridges register feature-owned encoded records with DocSave; they do not move gameplay state ownership into the storage subsystem.

`DocGameFrameworkUIEnhancedInput`, a generic UMG presenter, media presentation, or a startup loading-screen adapter may be separately packaged when an actual implementation needs them. They remain explicit extensions, not hidden base requirements.

## 15.4 Enforce the graph

Audit public headers, reflected properties, serialized asset dependencies, native module imports, plugin descriptors, config defaults, and cooked asset manifests. Any of these can introduce coupling.

Run clean builds with sibling directories physically absent from the consumer workspace. An editor in which every plugin was once loaded can conceal missing dependencies. Do not use `__has_include`, forced plugin loading, or dynamic casts to hide an undeclared production dependency.

---

# 16. Editor Menu Expansion

Extend the optional shared menu:

```text
Window → Doc Modular Systems
  Map Debugger
  Weather Debugger
  Surface Feedback Debugger
  Dialogue Graph Browser
  Quest / Objective Monitor
  NPC Schedule Monitor
  Knowledge Browser
  Inventory Monitor
  Unlock Inspector
  UI Stack Debugger
```

Feature Editor modules register their panels through the existing Core editor registration contract. Core Editor never imports every feature to construct the menu. Registration and unregistration are safe across module startup/shutdown and repeated PIE sessions. Missing features appear as absent/not installed, not as broken panel loads.

Every runtime monitor exposes selected world, net mode, owner scope, generation, latest relevant revision, and a bounded recent-error view. A GameInstance monitor distinguishes its profile partitions; a LocalPlayer monitor requires an actual selected local player. Do not default all tools to the first PIE world or player zero.

Keep authoring and runtime inspection separate. Edit immutable definitions through normal editor transactions and asset dirtying; mutate runtime state only through explicitly labeled debug commands that use the public authorization/validation path. A validator or preview must not execute real rewards, update user saves, or grant inventory.

Retain the original module-specific tools: map bounds/coordinate/discovery previews; weather profile and transition inspection; surface fallback tracing; dialogue links/participants/search; quest subscriptions and counts; schedule overrides; codex categories/references; inventory transactions; unlock dependencies; UI layers/focus/settings. Add tooling incrementally with working data sources rather than placeholder tabs.

Custom editors and graph views live in Editor modules. Runtime serialized assets cannot depend on editor graph nodes or toolkit classes. Editor graph representation must compile/export to the runtime graph contract and preserve stable authored node/action IDs through edit, undo, duplication, and cook.

---

# 17. Documentation Requirements

Each implemented plugin includes the original documentation set:

```text
README.md                 Purpose, installation, capability status, limitations
SETUP.md                  Exact dependencies and minimum working configuration
API_OVERVIEW.md           Public operations, queries, delegates, and ownership
BLUEPRINT_USAGE.md        Runnable Blueprint examples and failure branches
CPP_USAGE.md              Compiling consumer example using public headers
INTEGRATION.md            Providers, bridges, absence behavior, setup ownership
DEBUGGING.md              Logs, validation errors, monitors, recovery steps
CHANGELOG.md              Public API/content/schema changes and migrations
```

Where relevant, include `DATA_MODEL.md`, `NETWORKING.md`, `SAVE_INTEGRATION.md`, and `EDITOR_TOOLS.md`, as originally requested. Add `CAPABILITIES.md` and `TEST_EVIDENCE.md` or equivalent clearly discoverable sections rather than scattering support claims across chat logs.

The documentation must answer: what is authoritative, who owns the record, which clock applies, what survives travel/save, what gets replicated to whom, what happens on retry/cancel/unload, and which operation constitutes the commit point. Each public asynchronous API specifies its terminal results, timeout behavior, delegate thread, and handle validity.

Provide a minimum setup without sibling plugins and a separate integrated setup. Document primary asset registration/cook rules, tag configuration, settings ownership, required provider bindings, and example content. Any host configuration change is explicit and reversible. No README may imply that copying source also creates finished binary assets.

Feature-specific documentation includes projection units and floor rules; weather units/random state; surface matching precedence; dialogue choice/action semantics; quest stage policies; NPC execution boundaries; codex reveal/search rules; inventory conservation/stacking; unlock entitlement/grant semantics; and UI settings/lease policies.

Repository-level documents should include `ENGINE_COMPATIBILITY.md`, `DEPENDENCY_MATRIX.md`, `REQUIREMENTS_TRACEABILITY.md`, `TEST_MATRIX.md`, `PERFORMANCE_BASELINE.md`, `DECISIONS.md`, `RELEASE_CHECKLIST.md`, and the development status file. Link requirement IDs to real implementation and evidence paths; a checklist without evidence is planning, not verification.

---

# 18. Data Validation

Use Unreal asset validation extension points for editor/CI checks and pure runtime validation for incoming data. Epic documents asset-level and validator-based paths; command-line validation requires verifying that the intended rule implementations actually run. [R12]

A diagnostic includes severity, owning plugin, asset/definition ID, field or node path, violated rule, and an actionable resolution. Validation is read-only. Optional repair commands are separate, preview their changes, and participate in editor undo where applicable.

| Module | Required validation coverage |
|---|---|
| MapNavigation | Finite/nondegenerate bounds and projection basis; valid scale; stable map/layer/floor/marker IDs; declared overlapping-floor policy; discovery limits; supported projection; required assets and cook registration |
| Weather | Missing/duplicate tags or profile IDs; normalized/unit ranges; finite values; invalid/zero scheduling durations; negative/nonfinite weights; no selectable candidates; transition curves/durations; configured clocks; ambiguous override ties; renderer capability mismatch |
| SurfaceFeedback | Physical-surface mapping; required event/surface tags; duplicate or ambiguous matching rules; illegal magnitudes; unsupported response payloads; missing mandatory sinks; unsafe lifetime/count/pool limits; context specificity and fallback coverage |
| Dialogue | One valid entry; unique graph/node/choice/action IDs; broken targets; participant-role declarations; unreachable nodes; unbounded automatic strongly connected paths; valid delays; condition/action schema; missing localization keys or required media |
| QuestObjectives | Unique definitions/stages/objectives; missing stages/targets; legal activation/completion policies; prerequisite cycles; valid counts and clocks; hidden versus terminal-state consistency; repeat-instance policy; required reward/condition providers |
| NPCSchedules | Stable NPC/schedule/entry IDs; calendar validity; overnight/all-day rules; overlap priority; missing fallback/target; invalid executor requirements; override source/expiry policy; Smart Object tag schema only when its bridge is selected |
| KnowledgeCodex | Stable entry/section IDs; valid reveal stages and ordering; duplicate categories; broken/cyclic category hierarchy; missing related entries; safe unknown-entry policy; localization/media availability; search-visible-field configuration |
| InventoryItems | Unique item/instance/container/slot IDs; valid stack and quantity limits; finite/nonnegative weights; compatible custom-data schemas; multi-slot equipment rules; duplicate membership; cycles if nesting is enabled; authorized action types; orphan-definition policy |
| UnlocksProgression | Missing/duplicate unlock IDs; broken prerequisites; cycles; explicit AND/OR grouping; valid permanent/live-gate rules; nonnegative expiry; declared clock; grant-source ownership; conditions/actions and indexed dependency declarations |
| GameFrameworkUI | Unique screens/layers/settings keys; valid priorities and stack policies; presenter bindings; required input routes; settings type/range/scope; confirmation fallback; missing localized text; malformed notification actions; capacity/TTL limits |

Graph validation distinguishes an intentional dialogue loop containing a wait/choice from an invalid prerequisite cycle. Validation must not classify every cycle in every feature identically.

An optional adapter's absence can be a warning for unused presentation capability. An asset that requires that capability cannot pass a selected integration-profile gate. Errors block the affected operation or release profile rather than crashing at an unrelated future use.

Validate persisted/network input before allocation or execution: bounded counts and lengths, supported schema, trusted owner scope, numeric overflow, allowed classes/assets, and valid revision. Editor validation is not a security boundary for runtime requests.

---

# 19. Performance Requirements

## 19.1 Structural requirements

Retain the original no-unnecessary-tick approach. Marker movement is throttled and spatially filtered; weather updates are configurable; surface dispatch is indexed and pooled where appropriate; dialogue/codex have no idle tick; quest/unlock reevaluation is dependency-driven; inactive NPC state advances by time selection, not per-second simulation; inventory changes use affected containers; UI updates changed views rather than rebuilding every widget tree.

A timer per record is not a performance guarantee. Prefer one budgeted scheduler or indexed next-boundary queue where it reduces overhead. Separate producer event collection, pure computation, and bounded game-thread application. No unbounded deferred queue, retained event history, search cache, receipt cache, media handle collection, or notification backlog.

## 19.2 Proposed test workloads, not benchmark claims

These are starting fixtures for measuring behavior on the selected reference machine. They are not promises that a particular CPU/GPU already meets a frame-time target. Adjust documented workload tiers after measurement without deleting correctness tests.

| Module | Initial load fixture | Measure / assert |
|---|---|---|
| MapNavigation | 10,000 registered markers, 200 moving, 250 visible candidates, two local players | Candidate query/projection time, movement invalidations, bounded view result count, discovery-memory growth |
| Weather | 32 overlapping override records, 128 sample locations, interrupted transitions over a long simulated interval | State evaluation/update cost, event counts, scheduling work under time jumps; renderer cost separately |
| SurfaceFeedback | 1,000 requests/second plus a one-frame burst of 500; deliberately low sink capacity | Resolution cost, queue depth, overflow policy, active FX cap, eventual pool/handle cleanup |
| Dialogue | 100 headless sessions, 1,000-node assets, long chains and intentional loops | Work per advance, loop-cap behavior, asset load deduplication, no idle-session scanning |
| QuestObjectives | 1,000 active objective records, 10,000 unrelated definitions, 500 occurrences/second | Candidate subscription fan-out, duplicate rejection, stage transitions, no reevaluation of unrelated definitions |
| NPCSchedules | 10,000 logical records, 200 realized NPCs; forward/backward day/week jumps | Selection cost, bounded dirty processing, next-boundary maintenance, no replay proportional to elapsed seconds |
| KnowledgeCodex | 10,000 entries with staged localized text and related-entry edges | Index build memory, query latency, culture/reveal invalidation, stale async result rejection |
| InventoryItems | 100 containers with 100 occupied entries each; 500 transactions/second | Affected-container validation time, receipt retention, conservation and revision conflicts, serialized snapshot size |
| UnlocksProgression | 10,000 nodes and 30,000 dependency edges; sparse changed inputs | Affected-node visits, propagation queue high-water mark, deterministic bounded completion |
| GameFrameworkUI | Two local players, repeated open/close cycles, 1,000-notification burst | View creation/reuse, queue cap, focus correctness, pending-load cleanup, settings timeout under pause |

Always retain small correctness fixtures; a stress workload must not become the only way to run unit tests. Negative workloads—missing definitions, unavailable providers, malformed graphs, denied transactions—also need bounded cost.

## 19.3 Measurement protocol and budget decisions

Record CPU, GPU, RAM, OS, driver, engine/build identity, target/configuration, resolution, feature flags, content revision, and warm/cold cache state. Use a repeatable seed and workload configuration. After a defined warmup, capture multiple steady-state runs and a teardown run; record sample counts and variability.

Report p50/p95/p99 and maximum measured subsystem cost where meaningful, allocations/memory growth, loaded asset count, peak queue size, active effect/widget/session counts, and network bytes where applicable. Trace with the selected engine profiling tools and retain the trace path. Do not subtract unrelated frame work and call the result a measured subsystem cost without a valid method.

Establish approved budgets in `PERFORMANCE_BASELINE.md` after a real reference run. Regression thresholds must consider test noise and workload changes. An engine/toolchain upgrade invalidates affected baselines until rerun. A NullRHI test cannot establish rendering cost, and an unattended test with no audio device cannot establish audible timing/quality.

## 19.4 Backpressure and graceful degradation

Performance pressure may reduce cosmetic FX, marker projection frequency, notification presentation, or renderer update rate according to policy. It must not drop authoritative inventory mutations, silently discard accepted rewards, expose hidden codex content, or change unlock truth.

If a durable operation cannot be accepted within capacity, reject it before commit with a retryable result or durably queue it under a documented bounded policy. Do not report success and then lose it to a cosmetic overflow mechanism.

---

# 20. Networking Requirements

## 20.1 Classify state across independent dimensions

The original labels Authoritative, Replicated, Local, Persistent, and Transient are retained as classification concepts, but they are **not mutually exclusive enum states**. A quest record can be authoritative, replicated to its owner, and persistent simultaneously.

For each field or coherent record, document:

```text
Authority: server / trusted local host / local presentation
Audience: owner / party / relevant observers / everyone / none
Replication: snapshot / revisioned delta / transient cosmetic event / not replicated
Persistence: durable / checkpoint / session-only / transient
Owner scope and world generation
Reconciliation and late-join behavior
```

Subsystems coordinate state; they are not assumed to be automatic RPC/replication endpoints. Use actual replicated Actors/components and authorized owning connections for transport. Native actor-component replication and ownership requirements must be configured and tested. [R6], [R14]

No manually placed global manager Actor is required by the base architecture. An opt-in network adapter may attach to a suitable host-owned replicated Actor or create its explicitly owned transport Actor. Transport lifetime, travel, and relevance remain visible design decisions.

## 20.2 Feature authority and privacy

| Feature | Authoritative/shared state | Usually local state | Transport concern |
|---|---|---|---|
| Map | Shared markers and gameplay-relevant discovery where selected | Filters, local waypoint, zoom, cosmetic compass | Filter audience before transmission; unloaded targets resolve by stable identity |
| Weather | Global/regional profile selection, source/target snapshots, timeline and seed state | Render interpolation and camera-local effects | Late join receives enough state to evaluate the active transition |
| Surface | Any gameplay effect, damage-like intent, or tag mutation | Footstep audio/VFX/haptic presentation | Dedupe predicted cosmetic contact; never turn a client impact report into authorized gameplay |
| Dialogue | World-state-changing choices/actions and shared session decisions | Transcript scroll, subtitle preferences, private presentation | Revalidate selected choice and participant permission; correct audience for private conversations |
| Quest | Owner/party/world progress, repeat identity, rewards | Tracking and HUD selection where configured | Client cannot submit an arbitrary completion count as fact |
| NPC | Shared desired activity, overrides, logical location policy | Presentation/interpolation | One authoritative schedule/executor chain, not competing AI on every client |
| Codex | Gameplay-relevant discovery/reveal | Purely cosmetic read state where allowed | Send only permitted progress; local cooked content is not a cryptographic secrecy boundary |
| Inventory | Transactions, item ownership, quantities, container/equipment revisions | Selection, sorting, drag preview | Atomic source/destination operation, private container audience, retry receipts |
| Unlock | Permanent entitlement, temporary grants, disabled policy | Presentation selection | Revalidate source conditions and owner; temporary lease expiry uses authoritative time |
| UI | None by default; settings adapter controls the local machine/profile | Screens, notifications, input device, focus | UI invokes gameplay APIs; a clicked button does not grant authority |

A server maps a connection to an authorized owner scope. It must not accept a client-supplied profile ID, arbitrary Actor pointer, class path, or inventory owner field as permission. Rate/size limits, malformed request checks, revision checks, and permission errors precede mutation.

## 20.3 Reconciliation and transport contracts

Replicate definition IDs and bounded runtime deltas rather than complete static Data Assets. A selected delta mechanism needs sequence/revision validation, full resync, removal handling, and late-join tests. Native fast-array facilities may be suitable for inventory/records; verify their installed API and behavior rather than assuming a serializer automatically solves transaction atomicity or privacy.

Cosmetic prediction is optional. Keep speculative state visibly separate until confirmation, and reconcile rejection without creating duplicate durable effects. Reliable transport alone does not provide application-level idempotency across reconnects or persistence across server crashes.

GameInstance lifetime is local to a process/session architecture, not a substitute for authoritative ownership. Hosted and dedicated-server fixtures must test multiple owner partitions. Shared progress requires an explicit party/world scope; client-specific filters or read state must not overwrite another player's preferences.

## 20.4 Network verification

For every feature advertised as networked, test an actual server and at least two clients, a joining client after state changes, disconnect/reconnect, rejected ownership, duplicate requests, stale revisions, delayed/dropped cosmetic messages, and travel/teardown. Use the chosen engine's supported network emulation or a deterministic adapter harness; record which mechanism was used.

Dedicated-server runs must avoid UI/media/render initialization while still executing authoritative state and tests. A compile-only server target or a local authority check is not proof of multiplayer behavior. Unsupported networking remains explicitly `Not Implemented` or `Not Verified` by feature.

---

# 21. Test Combinations

## 21.1 Mandatory isolation matrix

Create clean consumer hosts with Core plus **each base individually**, not merely representative pairs:

```text
Core only
Core + DocMapNavigation
Core + DocWeather
Core + DocSurfaceFeedback
Core + DocDialogue
Core + DocQuestObjectives
Core + DocNPCSchedules
Core + DocKnowledgeCodex
Core + DocInventoryItems
Core + DocUnlocksProgression
Core + DocGameFrameworkUI
```

Sibling directories and optional content must be physically absent from isolated distributions. Test both a C++ consumer of public headers and a Blueprint consumer of public operations. No consumer may include a feature's Private headers or require a project-specific inheritance hierarchy.

Use two structurally different generic hosts for portability: an actor/component-oriented host with no mandatory Character hierarchy and a separate player-oriented host. At least one integration host supports two local players; at least one networking profile uses a dedicated server when that capability is claimed.

## 21.2 Preserve and strengthen the original combination tests

Retain Core-only, Surface-only, Core+Events+Map, Core+Time+Weather, Core+Events+Dialogue, Core+Events+Quest, Core+Time+NPCSchedules, Core+Save+Inventory, Core+Save+Unlocks, and Full Suite cases. Where behavior depends on integration, install the corresponding bridge explicitly. Also test the same producer/consumer pair without the bridge to confirm that no hidden integration activates.

Add save fixtures for map discovery, weather transitions, dialogue history/checkpoints, quest rewards, NPC overrides, codex reveal/read revisions, item memberships, and unlock grants. UI preferences have their own storage policy; transient screen handles are not generic save records.

Use Unreal automation for suitable logic/functional tests and real host execution for presentation/input/network evidence. Test flags, fixtures, discovery, and cleanup must be configured correctly for their target. [R13]

## 21.3 Cross-system acceptance

| ID | Required behavior |
|---|---|
| CROSS-01 | Quest tracking creates only the correct local player's map markers; untracking, target unload, bridge removal, and player removal release the correct handles |
| CROSS-02 | Dialogue/inspection knowledge grant retries produce one reveal transition; stale UI read acknowledgments cannot mark newly revealed sections read |
| CROSS-03 | Quest reward → inventory → unlock survives duplicate delivery and a staged restore without losing items or applying the reward twice |
| CROSS-04 | Weather, Time, and NPC schedule integrations handle forward/backward discontinuities without replaying every missed second or invalidating permanent state |
| CROSS-05 | UI, Inspection, Sequences, and Dialogue share control claims; overlapping close/cancel/travel operations do not restore another owner's stale input/camera state |
| CROSS-06 | Save during inventory transfer, pending reward, or temporary grant uses a documented coherent revision; restoration does not emit ordinary gameplay completion events |
| CROSS-07 | Base+bridge-absent, provider unavailable, two PIE worlds, two profiles, and stale callbacks cannot leak state or cause a successful gameplay no-op |
| CROSS-08 | Selected networked integration passes authorization, duplicate request, late join, owner privacy, and dedicated-server lifecycle fixtures |

Every integration row is gated by its named profile; it does not become a base compile dependency. Report unselected profiles as not tested, not passed.

## 21.4 Failure injection and reproducibility

Inject failures before/after transaction commit, before/after receipt acknowledgment, during asset loading, while a target unloads, during save capture/storage, and at UI confirmation timeout. A failure injected after commit must not be reported as a rollback that never occurred.

Retain old schema fixtures and deterministic content IDs. Test duplicate authored identities, missing definitions, empty weighted weather choices, invalid coordinate bases, runaway dialogue loops, unlock cycles, full containers, unavailable presenters, and late callbacks after teardown. Run repeated create/destroy cycles and inspect remaining registrations, resources, and memory.

Discover the actual test set from source/build output. An empty test selection, absent test module, skipped integration, or process exit without a report is not a passing suite. Preserve original baseline test names in Appendix C and map additional requirement IDs to concrete new tests.

---

# 22. Packaging Validation

## 22.1 Required targets and content checks

For each implemented milestone, validate `Development Editor Win64`, `Development Win64`, and `Shipping Win64` to the extent supported by the selected host/toolchain. Shipping must build/cook/launch successfully; development-only automation need not be compiled into Shipping. Networked profiles additionally need their documented server target.

Verify no Runtime-to-Editor dependency, no sibling assets in isolated packages, no project-specific class/path in library runtime, no broken soft references, no missing configuration/tag registration, and no required test-only asset accidentally stripped from its selected fixture. Distribution manifests distinguish production assets from optional sample/test content.

A `.uasset` or `.umap` must be authored/generated through valid Unreal tooling and opened/cooked successfully. Never create a text placeholder with a binary asset extension. Generated C++ compilation alone does not satisfy the example-content requirement.

## 22.2 Build and command templates

These are **templates for the implementing IDE**, not commands executed while preparing this handoff. Verify installed 5.8.3 tools/flags and actual target names first. Use a per-run output directory outside shipping content and a wrapper with bounded process timeout, process-tree cleanup, captured stdout/stderr, and a manifest of source/engine/configuration.

Example PowerShell setup and Editor build:

```powershell
$ErrorActionPreference = 'Stop'
foreach ($name in @('UE_ROOT', 'DOC_HOST_PROJECT', 'DOC_EDITOR_TARGET')) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
        throw "Set $name from the audited workspace; do not guess it."
    }
}
$EngineRoot = (Resolve-Path -LiteralPath $env:UE_ROOT).Path
$Project = (Resolve-Path -LiteralPath $env:DOC_HOST_PROJECT).Path
$VersionFile = Join-Path $EngineRoot 'Engine/Build/Build.version'
$Build = Get-Content -LiteralPath $VersionFile -Raw | ConvertFrom-Json
if ($Build.MajorVersion -ne 5 -or $Build.MinorVersion -ne 8 -or $Build.PatchVersion -ne 3) {
    throw 'Target mismatch: this handoff requires Unreal Engine 5.8.3.'
}
$BuildBat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$RunUAT = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
foreach ($tool in @($BuildBat, $EditorCmd, $RunUAT)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing tool: $tool" }
}
& $BuildBat $env:DOC_EDITOR_TARGET Win64 Development "-Project=$Project" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }
$EvidenceRoot = Join-Path (Split-Path -Parent $Project) 'Saved/DocModularEvidence'
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$RunId = (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [Guid]::NewGuid().ToString('N')
$RunRoot = Join-Path $EvidenceRoot $RunId
$ReportDir = Join-Path $RunRoot 'Automation'
$ArchiveDir = Join-Path $RunRoot 'DevelopmentArchive'
$ShippingArchiveDir = Join-Path $RunRoot 'ShippingArchive'
New-Item -ItemType Directory -Path $ReportDir,$ArchiveDir,$ShippingArchiveDir | Out-Null
```

Engine metadata/version equality alone does not verify headers, installed optional modules, compiler, SDK, or compatibility. Record those independently.

Native asset validation entry point:

```powershell
& $EditorCmd $Project '-run=DataValidation' '-unattended' '-nop4'
if ($LASTEXITCODE -ne 0) { throw "Validation process failed: $LASTEXITCODE" }
# The wrapper must also check that expected assets and validators ran,
# and that the validation report contains no blocking failures.
```

The documented commandlet does not guarantee discovery/execution of every custom rule language without setup. Register and exercise required validators explicitly. [R12]

An automation invocation can use the documented command family and report export mechanism below; the exact command is selected after checking the installed runner. [R16]

```powershell
# $ReportDir must be a new, validated directory for this run.
# Start with the implemented module, not a prefix containing only absent tests.
& $EditorCmd $Project '-unattended' '-nop4' `
    '-ExecCmds=Automation RunTest Doc.Surface;Quit' `
    "-ReportExportPath=$ReportDir"
if ($LASTEXITCODE -ne 0) { throw "Automation process failed: $LASTEXITCODE" }
# Parse this run's report and require a nonzero expected test set,
# all selected tests terminal, and no failures or unexplained skips.
```

Do not add `-NullRHI` to a test requiring rendering, focus/input interaction, graphics settings, or visual assets. Separate headless logic tests from rendered functional tests. Verify that the chosen run-and-exit behavior waits for completion; absence of the expected final report is a failed/incomplete run, even when the process returned zero.

Cooking/staging template for a verified host target:

```powershell
# $ArchiveDir is a new, validated per-run output directory.
# The host must have its intended maps, plugin content, and cook rules configured.
& $RunUAT BuildCookRun "-project=$Project" '-noP4' '-platform=Win64' `
    '-clientconfig=Development' '-build' '-cook' '-stage' '-pak' '-archive' `
    "-archivedirectory=$ArchiveDir" '-unattended'
if ($LASTEXITCODE -ne 0) { throw "Cook/stage failed: $LASTEXITCODE" }
```

Repeat with `-clientconfig=Shipping` and `-archivedirectory=$ShippingArchiveDir`. For multi-target hosts, use the actual selected target options supported by the installed UAT. Do not infer successful packaging solely from an old executable at an expected path. A new run must identify its produced artifacts and then launch the packaged fixture.

## 22.3 Verification evidence

Retain exact command arguments, process exit, start/end timestamps, engine identity, source commit plus dirty-diff identity, content/configuration hashes, toolchain, discovered/executed test counts, selected/skipped capabilities, and report/log/artifact paths. A timeout is a failed or incomplete run, not a pass.

Use evidence labels `Passed`, `Failed`, `Not Run`, or `Skipped with Reason`. A missing engine permits useful specification/source work but leaves native gates `Not Run` or `Blocked`. Do not substitute static grep checks or generated stub tests for Unreal behavior evidence.

---

# 23. Repository Structure

Preserve the existing repository rather than replacing it with a new skeleton. The following is the intended organization, not a declaration that these folders already exist:

```text
Repository/
├─ Plugins/
│  ├─ DocModular/                      # Grouping directory: no .uplugin here
│  │  ├─ DocModularCore/
│  │  ├─ DocEvents/
│  │  ├─ DocInteraction/
│  │  ├─ DocRegions/
│  │  ├─ DocStreaming/
│  │  ├─ DocTime/
│  │  ├─ DocAdaptiveAudio/
│  │  ├─ DocSequences/
│  │  ├─ DocInspection/
│  │  ├─ DocSave/
│  │  ├─ DocWorldActivation/
│  │  ├─ DocMapNavigation/
│  │  ├─ DocWeather/
│  │  ├─ DocSurfaceFeedback/
│  │  ├─ DocDialogue/
│  │  ├─ DocQuestObjectives/
│  │  ├─ DocNPCSchedules/
│  │  ├─ DocKnowledgeCodex/
│  │  ├─ DocInventoryItems/
│  │  ├─ DocUnlocksProgression/
│  │  └─ DocGameFrameworkUI/
│  └─ DocModularBridges/               # Grouping directory: no .uplugin here
│     └─ <only implemented bridge plugins>/
├─ Samples/
│  ├─ <isolated consumer hosts>/
│  └─ <integration host and generic sample content>/
├─ Scripts/
│  └─ <build, validation, test, evidence, and packaging wrappers>/
└─ Docs/
   ├─ DEVELOPMENT_STATUS.md
   ├─ DECISIONS.md
   ├─ ENGINE_COMPATIBILITY.md
   ├─ REQUIREMENTS_TRACEABILITY.md
   ├─ DEPENDENCY_MATRIX.md
   ├─ TEST_MATRIX.md
   ├─ PERFORMANCE_BASELINE.md
   └─ RELEASE_CHECKLIST.md
```

One plugin owns its descriptor, runtime source, optional genuine Editor module, content/default configuration where required, and documentation. Example content stays separately identifiable; a base is not allowed to depend on its demonstration widgets or sample map.

Epic's plugin discovery stops recursively searching beneath a discovered plugin. Keep grouping directories descriptor-free and do not nest a plugin inside another plugin. A bridge stored in a repository staging directory must still be installed into the selected host's discoverable plugin tree. [R1]

Use a distinct distribution staging directory for each isolation build. Never remove sibling plugins from the user's active workspace merely to conduct an absence test. Copy the authorized source set into an isolated host, or use an equivalent safe staging mechanism. Exclude caches, local credentials, private project assets, and unrelated binaries from distributions.

---

# 24. Development Status File

## 24.1 Authoritative status and freshness

Maintain `Docs/DEVELOPMENT_STATUS.md` as an index to actual repository work and evidence. The repository and its fresh verification results are authoritative; this handoff is not proof of what has already been implemented.

Existing source was not audited to prepare this specification. Initial status therefore starts `Not Audited`/`Unknown`, not an invented list of completed systems or an assumption that everything is absent. Once audited, use the execution states in Section 0.

Record the smallest meaningful completed slice and next task. Historical test results remain identifiable but become stale when relevant source, configuration, content, engine, or toolchain changes. Never copy an old “all tests pass” line forward without checking its applicability.

## 24.2 Status template

```markdown
# Development Status — Modular Systems Expansion

## Workspace and verification identity
- Updated: <timestamp and timezone>
- Branch / commit: <actual values>
- Relevant worktree changes: <precise paths or clean>
- Engine: <major.minor.patch, changelist/build identity>
- Toolchain / SDK: <detected values>
- Current milestone: <one milestone from Section 14>
- Selected capability profile: <base and specifically selected bridges>
- Source/config/content evidence identity: <manifest path>

## Foundation compatibility
| Contract | Existing implementation | Compatibility decision | Evidence / blocker |
|---|---|---|---|
| Results / handles | <path or absent> | <reuse / additive change / blocked> | <evidence> |
| Persistent identity | <path or absent> | <decision> | <evidence> |
| Owner scope | <path or absent> | <decision> | <evidence> |
| Player control leases | <path or absent> | <decision> | <evidence> |
| Save restore barrier / receipts | <path or absent> | <decision> | <evidence> |

## Verified work
<Requirement IDs, implementation paths, and fresh evidence references>

## Implemented but unverified
<Exact behavior written, missing verification, and reason>

## Test/build results
| Run | Revision | Engine/target | Expected/executed tests | Outcome | Evidence |
|---|---|---|---|---|---|
| <id> | <source identity> | <actual> | <counts or Not Run> | <actual> | <path> |

## Optional integrations
| Bridge/profile | Implemented | Verified | Missing capability / blocker |
|---|---|---|---|
| <name> | <state> | <state> | <specific cause> |

## Known issues and risks
<Reproduction, severity, affected requirement, mitigation>

## Recent architecture decisions
<Decision ID, compatibility/schema impact, reason>

## Next bounded task
- Objective: <one concrete outcome>
- Implementation surfaces: <files/contracts to inspect or change>
- Exit evidence: <named tests/builds/fixtures>
- Stop condition: <exact blocker or completion condition>
```

The matrix values are placeholders, not evidence. Documentation work may be verified independently, but it cannot mark native runtime or packaging as verified.

## 24.3 Copy-ready IDE continuation prompt

```text
Use UE5_8_3_Modular_Gameplay_Systems_Modules_11_20_IDE_Handoff_v2.md
as the expansion specification. It defines modules 11–20, not a request to
rewrite the first ten systems or build a particular game.

Read repository instructions, current source, Docs/DEVELOPMENT_STATUS.md,
this document's Sections 0–2 and 13–15 and 24–27, and the companion Revision 2
contracts where they are present. Audit the branch/worktree and preserve user
changes. Verify the actual Unreal 5.8.3 installation, headers and toolchain.
Do not silently downgrade the engine or claim builds that were not executed.

Start at Task 0 in Section 26 unless its audit has fresh evidence. Select the
smallest incomplete milestone in the original Phase 5–8 order. Implement one
working vertical slice with public C++/Blueprint access, explicit ownership,
failure handling, cleanup, validation, and relevant tests.

Every base plugin depends only on Core and its necessary native engine modules.
Optional feature and engine integrations belong in declared bridges. Reuse
shared persistent identities and control leases. Keep player/profile/world
scope explicit; do not use player zero or mutable Data Assets as global state.

For durable actions, implement the actual commit, idempotency, receipt and
restore contract. Do not replace these with success-shaped placeholders.
For UI and settings, validate real focus/input/setting effects in a rendered
host; a headless unit test does not establish presentation behavior.

Use installed engine declarations as the authority for exact API signatures.
Create binary assets only through valid Unreal tooling. Run the relevant
isolation, automation, packaging, and integration gates when available.
Record real commands, exit codes, expected/executed test counts, source/engine
identity and report paths. Missing prerequisites are Blocked or Not Run.

Update Docs/DEVELOPMENT_STATUS.md and requirement traceability after the work
slice. Report what changed, what was actually verified, outstanding failures,
and the exact next bounded task. Do not push, publish, delete user content,
install marketplace assets or change global settings without authorization.
```

---

# 25. Status Matrix Template

Use separate matrices for implementation and verification rather than one misleading “complete” checkbox. These initial values mean **the repository has not been assessed by this document**.

## 25.1 Implementation matrix

| Module | Runtime / public API | Blueprint example | Editor tools | Docs / schemas | Next audit |
|---|---|---|---|---|---|
| DocMapNavigation | Not Audited | Not Audited | Not Audited | Not Audited | Projection/owner scope |
| DocWeather | Not Audited | Not Audited | Not Audited | Not Audited | Scheduling/transition state |
| DocSurfaceFeedback | Not Audited | Not Audited | Not Audited | Not Audited | Surface resolution slice |
| DocDialogue | Not Audited | Not Audited | Not Audited | Not Audited | Graph/session contracts |
| DocQuestObjectives | Not Audited | Not Audited | Not Audited | Not Audited | Stage/reward semantics |
| DocNPCSchedules | Not Audited | Not Audited | Not Audited | Not Audited | Clock/executor boundaries |
| DocKnowledgeCodex | Not Audited | Not Audited | Not Audited | Not Audited | Reveal/read/search state |
| DocInventoryItems | Not Audited | Not Audited | Not Audited | Not Audited | Atomicity/membership |
| DocUnlocksProgression | Not Audited | Not Audited | Not Audited | Not Audited | Entitlements/temporary grants |
| DocGameFrameworkUI | Not Audited | Not Audited | Not Audited | Not Audited | Control/settings ownership |

## 25.2 Verification matrix

For each module or bridge, maintain a row containing:

```text
Capability name
Source/config/content identity
Core-only isolation build
Public C++ and Blueprint consumer
Automation expected/executed/passed/failed/skipped counts
Cooked Development launch
Cooked Shipping launch
Persistence migration/recovery fixture, if selected
Network/dedicated-server fixture, if selected
Presentation/input fixture, if selected
Performance workload and trace
Evidence paths
Open blocker
```

All verification starts `Not Run` until actual current evidence exists. A selected integration's requirements cannot be covered by a base-only test. Include the original eventual editor tools and optional backends in a separate backlog so a base completion report does not erase them.

---

# 26. Immediate IDE Tasks

## Task 0 — Audit before creating code

Read existing repository instructions and implementation. Resolve actual engine/build identity and required compiler/SDK. Compare shared Core contracts to Appendix D. Preserve any already functioning expansion modules; do not overwrite them with this document's suggested class names.

Create or update the development status and traceability documents. Record whether the current task can run native verification. A missing engine does not authorize invented Unreal results; proceed only with bounded work whose verification limitations are explicit.

## Task 1 — Create or finish DocSurfaceFeedback

Use `Plugins/DocModular/DocSurfaceFeedback`. Add a real Runtime module and only the Editor functionality needed for asset validation/debugging. Establish a clean host containing Core and this plugin, with siblings physically absent from the staged host.

Compile a minimal public C++ consumer before growing the feature. Confirm Blueprint-exposed structs/functions through UHT and an actual Blueprint asset when tooling is available. Do not report an empty plugin skeleton as a finished system.

## Task 2 — Implement the surface data model

Implement the original concepts `FSurfaceFeedbackRequest`, `FSurfaceFeedbackResult`, `USurfaceResponseProfile`, and `USurfaceFeedbackSubsystem`, using audited existing names or the recommended Doc-prefixed equivalents.

Include explicit context, correlation/owner generation, copied contact data, surface/event/context tags, magnitude validation, resolved-rule identity, selected response descriptors, and separate resolution/dispatch outcomes. Keep immutable profile configuration separate from transient request and sink state.

## Task 3 — Implement Physical Surface resolution

Implement the declared request/material/surface/tag precedence and deterministic rule ordering. Mapping is configurable; do not rewrite host `DefaultEngine.ini` surface labels or assume a physical surface number means a specific material.

Test the same rule set in different insertion orders. Cover missing physical material, explicit overrides, ancestor tags, equally specific rules, no match, and invalid request values. Return a useful fallback path in diagnostics.

## Task 4 — Prove the footstep path

Build a generic fixture producing `SurfaceEvent.Footstep` through a manual request and one real supported contact producer. Confirm resolution for at least two configured physical surfaces. The headless fixture uses a recording sink, not a fake claim that sound/VFX played.

Verify repeated contact IDs, missing contact, teleport/distance discontinuity, owner destruction, and independent simultaneous participants. Add an actual audio or Niagara sink only in its selected bridge milestone.

## Task 5 — Prove the impact path

Use `SurfaceEvent.Impact` against the same surfaces and confirm the configured different response. Validate context/magnitude modification and bounded dispatch. No projectile, weapon, or combat class is introduced into the base.

## Task 6 — Run the baseline and additional automation

Preserve the original `Doc.Surface.PhysicalMaterialResolution`, `ExactMatch`, `Fallback`, and `ContextModifier` tests, plus the original `NoMatch` case. Add the SFC requirement fixtures for deterministic ties, contact deduplication, sink absence, backpressure, authority separation, and teardown.

Record discovered and executed tests rather than only a shell exit code. Where native tests are blocked, mark them Not Run and state the missing prerequisite. Do not substitute a handwritten assertion over the Markdown for execution of the C++ behavior.

## Task 7 — Package, document, and update status

Finish the isolated consumer example, required docs, asset validation, current target builds, and cooked smoke fixture. Update `Docs/DEVELOPMENT_STATUS.md` and traceability with exact evidence and known limitations before beginning MapNavigation.

The next base is DocMapNavigation, preserving the original order. Unavailable optional renderer/audio integrations do not require a hidden dependency in the base; their remaining work stays visible in the integration backlog.

---

# 27. Completion Definition

A module is complete only for a specifically named capability profile. These release gates preserve the original completion requirements and add evidence, scope, and failure-path checks.

| ID | Release gate |
|---|---|
| REL-01 | Correct Unreal Engine 5.8.3 target verified; public Runtime module compiles; no Editor-only imports; both C++ and Blueprint consumers work |
| REL-02 | Base installs with Core and essential native dependencies only; sibling/optional plugin absence builds pass; two structurally different hosts demonstrate portability without plugin source edits |
| REL-03 | Data Assets and incoming requests validate; no mutable shared-definition state; explicit owner scope, identities, time domains, and cleanup semantics are implemented |
| REL-04 | All mandatory base acceptance IDs have fresh behavior evidence; selected bridge IDs and CROSS requirements have their own evidence; missing/skipped work is not represented as passing |
| REL-05 | Original examples and required authoring/documentation exist; additional eventual/optional editor tools and backends remain tracked until actually implemented and verified |
| REL-06 | Clean Development and Shipping Win64 packages build/cook/launch; runtime resolves required cooked assets/config; no project-specific runtime classes, map names, or external sample dependencies |
| REL-07 | Selected persistence/network/presentation capabilities pass their migration, recovery, authority, privacy, input/focus, and resource-lifetime gates; no claim based only on architecture diagrams |
| REL-08 | Performance workloads and teardown have measured evidence; source/engine/configuration identity is recorded; known issues, supported capabilities, and next tasks are accurate and reviewable |

“All ten expansion bases complete” requires every base row, not a subset or ten empty descriptors. “Full original expansion complete” additionally requires the originally requested integrations and eventual tools advertised by that claim. A statement of “network ready” must identify whether it means authority-aware APIs or verified network operation; those are different outcomes.

This document itself satisfies none of the native build, gameplay, packaging, or performance gates. Its requirement IDs define work and evidence to produce.

---

# 28. Final Design Principle

Modules 11–20 extend the reusable library with navigation, environment, narrative, NPC activity, knowledge, items, progression, and UI infrastructure. Their integration must not turn twenty reusable systems into one inseparable game framework.

Communicate through Gameplay Tags, interfaces, subsystem APIs, stable IDs, immutable Data Assets, soft references, revisioned state, and optional bridge plugins. DocEvents remains a useful optional transport, not a compulsory global dependency.

Avoid project-specific casts/references, deep mandatory inheritance trees, singleton-world assumptions, shared mutable definition objects, automatic reward replay, and mandatory UI/AI/World Partition/Mass/GAS/CommonUI integration in unrelated bases.

The success criterion is practical portability: a consuming project can adopt a selected feature through documented configuration and public adapters, understand its ownership and failure behavior, and verify it without rewriting the plugin. Clear limits and tested contracts matter more than a long list of unimplemented integrations.

---

# Appendix A. Revision Change Record

## A.1 Provenance and intent

**Source specification:** `UE5_8_3_Modular_Gameplay_Systems_Modules_11_20_IDE_Handoff(1).md` supplied in this conversation. It defines the ten expansion modules, their original features, Unreal Engine 5.8.3 / Win64 target, independent Core-based architecture, Phase 5–8 order, and 28 numbered sections.

**Compatibility reference:** `UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff_v2.md`, the preceding revision for modules 1–10. Its persistent identity, request/lease, runtime/editor separation, and evidence contracts are reused where applicable. Its unverified project/release statements are not treated as evidence of the current repository or engine installation.

**New engineering recommendations:** the precise state machines, ownership/clock/transaction contracts, capability profiles, failure policies, workload fixtures, and numbered acceptance gates in this revision are proposed implementation requirements. They are not claims that those details were already in the original source or have been approved through a repository design review.

## A.2 Explicit changes and clarifications

| Original ambiguity or gap | Revision 2 decision | Scope / compatibility impact |
|---|---|---|
| Each base independent, but some proposed fields reference other plugin classes | Move typed integration configuration to bridges; keep generic semantic keys/providers in bases | Original integration remains; published asset schemas require explicit migration rather than silent field deletion |
| GameInstance subsystems could be read as one shared progress record | Partition by explicit trusted owner scope; separate local tracking/read preferences | Supports multiple profiles/players without changing which feature owns progress |
| Map conversion called reversible without a lost-height policy | Planar conversion retains height or uses an explicit floor/surface; clipping is not invertible | Clarifies supported math rather than claiming arbitrary 3D recovery from two numbers |
| Marker survives Actor unload, but ownership/destruction unclear | Persistent detached records with explicit lifetime, audience, rebind, and instance identity | No unintentional stale NPC position advertised as live tracking |
| Weather seed/progress did not fully define interrupted restore | Persist RNG state/cursor and evaluated transition source/timing; declared time-jump behavior | Avoids restart-based divergence; deterministic guarantees remain scoped to verified inputs/algorithm |
| Surface “most specific” and FX pooling were underspecified | Deterministic rule ordering, copied contact context, bounded sink dispatch and reset | Audio/Niagara stay optional; no base combat or weapon requirement |
| Dialogue conditions/choices/actions lacked concurrency semantics | Stable IDs, fresh choice validation, participant reservations, bounded graph execution and effect receipts | No arbitrary client-selected choice or shared mutable action object |
| Quest stage modes mixed activation and completion behavior | Separate those axes while retaining original modes as documented presets | Existing assets need explicit preset mapping; no automatic stage reinterpretation |
| Completed quest implied reward always delivered | Commit progress plus durable pending intent; consumer receipt-based retry | Honest pending-reward state; no unsupported distributed exact-once claim |
| NPC expected activity could be confused with executed behavior | Separate selection, request, acknowledgment, and executor ownership | Does not add locomotion, navmesh, AI framework, or offline economic simulation |
| Codex states conflated reveal, updates, and read status | Track reveal/content/read revisions independently; filter search and relationships | Avoids stale acknowledgments and spoiler leakage through query results |
| Inventory transaction listed operations without a commit definition | Atomic in-memory affected-container commit, conservation, receipts, rollback-before-commit and drop/pickup recovery | External side effects require an explicit recoverable contract, not assumed rollback |
| Temporary unlock could conflict with permanent state | Independent durable entitlement and source-owned temporary grants | Expiring a temporary grant cannot erase a permanent unlock |
| UI core called independent while ScreenDefinition referenced a widget class | Presenter-neutral base; typed soft widget class in presentation binding | Original UI capability remains in a real presenter; serialized API changes are audited |
| Settings API did not define safe preview/rollback | Machine-global writer, scoped preferences, real-time preview deadline, confirm/revert/restart recovery | A configuration switch is not claimed to work until its consumer effect is tested |
| Network classifications appeared mutually exclusive | Record authority, audience, transport, persistence, and lifetime as separate dimensions | Subsystems coordinate; actual network transports are separately implemented and verified |
| Example pairs could hide mandatory sibling imports | Test all ten Core+base combinations with siblings physically absent | Optional bridges receive additional tests; no source-workspace deletions required |
| Completion was a general checklist | Add 148 requirement IDs, capability-specific release gates, real evidence/status rules | Documentation, architecture, compilation, runtime, and cooked verification remain distinct |

## A.3 Features retained as explicit extensions

GridBased/TextureMask/Custom discovery, non-planar map projections, custom floor providers, advanced weather scheduling/adapters, StateTree/Mass/Smart Objects, the eventual Dialogue Graph Editor, optional media presentation, and selected consumer UI views remain visible work. The base minimums are the ones identified in their sections; an extension is neither silently deleted nor advertised merely because its enum/tag exists.

This revision does not authorize new combat, crafting, commerce, character creation, pathfinding, terrain generation, online account service, or entire game implementations. Existing Item Vendor/Equipment/Loot modes describe container policies, not full separate game systems. A waypoint is not a fast-travel execution system.

---

# Appendix B. Official Engine References and Verification Limits

Public references below were consulted on **September 27, 2026**. They support engine-specific integration facts, not this suite's build compatibility or the proposed algorithms. General/versioned documentation may not describe every detail of a particular installation; inspect the actual Unreal Engine 5.8.3 headers, descriptors, tool help, and native behavior before implementation.

No exact engine patch availability date is required by this handoff. **5.8.3 is the requested target**, and only an audited local installation/build can satisfy the native compatibility gate.

| Ref | Official Epic source | Supported integration point |
|---|---|---|
| R1 | [Plugins in Unreal Engine][R1] | Plugin descriptors/discovery, dependency and module separation concepts |
| R2 | [Programming Subsystems][R2] | Engine-managed subsystem lifetimes and extension points |
| R3 | [Asset Management][R3] | Primary asset discovery, identifiers, loading, bundles and management |
| R4 | [Cooking Content and Creating Chunks][R4] | Cook organization and explicit asset inclusion considerations |
| R5 | [Localization Overview][R5] | Localizable text/content workflow; culture support requires content preparation |
| R6 | [Actor Component Replication][R6] | Components replicate through correctly configured owning Actors |
| R7 | [Physical Materials User Guide][R7] | Physical Material/surface configuration and collision-query considerations |
| R8 | [Smart Objects][R8] | Activity interactions use reservation-oriented facilities |
| R9 | [CommonUI Input Technical Guide][R9] | CommonUI input routing and viewport/action-router integration |
| R10 | [Enhanced Input][R10] | Actions, mapping contexts, runtime input integration |
| R11 | [UGameUserSettings API][R11] | Apply/confirm/revert settings operations; ApplySettings also persists settings |
| R12 | [Data Validation][R12] | Validation extension points and commandlet; actual rule execution must be checked |
| R13 | [Automation Test Framework][R13] | Automation categories, integration and testing practices |
| R14 | [Programming Subsystems and Replication][R14] | Subsystem coordination versus actual replication endpoints |
| R15 | [Saving and Loading Your Game][R15] | Native save facilities as integration background, not proof of feature-level atomicity |
| R16 | [Run Automation Tests][R16] | Command-line automation invocation and exported reports |

The additional durable reward, transaction, projection, scope, ownership, and workload requirements are this revision's design decisions. For persistence implementation, inspect both the feature contracts and the companion DocSave specification; native save facilities alone do not establish cross-feature crash consistency. [R15]

[R1]: https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine
[R2]: https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine
[R3]: https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine
[R4]: https://dev.epicgames.com/documentation/unreal-engine/cooking-content-and-creating-chunks-in-unreal-engine
[R5]: https://dev.epicgames.com/documentation/en-us/unreal-engine/localization-overview-for-unreal-engine
[R6]: https://dev.epicgames.com/documentation/unreal-engine/replicating-actor-components-in-unreal-engine
[R7]: https://dev.epicgames.com/documentation/en-us/unreal-engine/physical-materials-user-guide-for-unreal-engine
[R8]: https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine
[R9]: https://dev.epicgames.com/documentation/en-us/unreal-engine/commonui-input-technical-guide-for-unreal-engine
[R10]: https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine
[R11]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UGameUserSettings
[R12]: https://dev.epicgames.com/documentation/en-us/unreal-engine/data-validation-in-unreal-engine
[R13]: https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine
[R14]: https://dev.epicgames.com/community/learning/knowledge-base/moP1/unreal-engine-programming-subsystems-and-replication
[R15]: https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-and-loading-your-game-in-unreal-engine
[R16]: https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine

---

# Appendix C. Requirements and Original Coverage

## C.1 Numbered requirement index

The document defines **148 unique acceptance/release requirements**. A requirement may need several fixtures or apply to different capability profiles; this number is not a count of implemented or passing tests.

| Area | Section | Requirement range | Count |
|---|---|---|---|
| Shared expansion contracts | 2 | EXP-01–EXP-12 | 12 |
| MapNavigation | 3 | MAP-01–MAP-12 | 12 |
| Weather | 4 | WEA-01–WEA-12 | 12 |
| SurfaceFeedback | 5 | SFC-01–SFC-12 | 12 |
| Dialogue | 6 | DIA-01–DIA-12 | 12 |
| QuestObjectives | 7 | OBJ-01–OBJ-12 | 12 |
| NPCSchedules | 8 | SCH-01–SCH-12 | 12 |
| KnowledgeCodex | 9 | KNO-01–KNO-12 | 12 |
| InventoryItems | 10 | INV-01–INV-12 | 12 |
| UnlocksProgression | 11 | UNL-01–UNL-12 | 12 |
| GameFrameworkUI | 12 | UI-01–UI-12 | 12 |
| Cross-system integration | 21 | CROSS-01–CROSS-08 | 8 |
| Release evidence | 27 | REL-01–REL-08 | 8 |
| **Total** | | | **148** |

For every requirement, maintain the following repository traceability fields:

```text
Requirement ID | Capability profile | Implementation path | Public API
Fixture/test name | Current evidence path | Source/engine identity
Outcome | Known limitation | Next action
```

A single passing test can cover multiple requirements only when its assertions actually exercise them. Conversely, one broad requirement must not be marked complete from a narrow happy-path assertion.

## C.2 Original feature coverage

| Original feature group | Where preserved / clarified |
|---|---|
| Map world/minimap/compass/waypoints, markers, discovery, fog modes, floors, detached data | Section 3; unsupported extension modes remain explicit rather than assumed implemented |
| Weather state/profile/transition/scheduling, regional/time inputs, renderer/material/audio outputs, saves/network | Section 4; bridge catalog and named verification profiles in 14–15 |
| Surface physical materials, event/context tags, footsteps/impacts, all response categories, pooling/debug | Section 5; optional sink implementations receive separate evidence |
| Dialogue nodes, participants, localized lines/choices, conditions/actions, sessions, voice/history, eventual editor | Section 6; authoring milestone in 14; stable graph IDs and safe action semantics added |
| Objectives independent of quests, stages/types, event filters, rewards/tracking/persistence | Section 7; activation/completion mode ambiguity resolved explicitly |
| Daily/weekly/seasonal/special/temporary/emergency/manual schedules, overrides, offline state, AI bridges | Section 8; desired activity remains separate from locomotion/execution |
| Codex entries/categories/media/relationships, staged reveal, read/update/notifications/search/persistence | Section 9; search and relationship results follow reveal scope |
| Item definitions/instances, all listed container and transaction modes, equipment/actions/pickups/network/save | Section 10; vendor mode does not silently add pricing/currency/commerce |
| Unlock tags, prerequisite AND/OR graph, conditions/actions, evaluation modes, temporary/permanent state | Section 11; temporary source grants do not overwrite earned entitlements |
| UI layers/stack, screens, devices/glyphs, all settings categories, accessibility, notifications/modals/loading/pause | Section 12; CommonUI presentation and global display ownership kept explicit |
| Original cross-feature examples and Phase 5–8 development order | Sections 13–14 |
| Original named optional bridges | Section 15, with additional missing dependency boundaries made explicit |
| Menu/docs/validation/performance/network/tests/packaging/repository/status/tasks/completion/principle | Original numbered Sections 16–28 retained and expanded |

## C.3 Original baseline test-name registry

The following **65 original baseline test names** are preserved as intended verification coverage. Their presence here does not claim that implementations or test registrations already exist. Module acceptance tables add more cases; exact test naming changes in an existing repository require a traceability mapping.

### Map

```text
Doc.Map.CoordinateConversion
Doc.Map.Discovery
Doc.Map.MarkerFiltering
Doc.Map.MarkerRegistration
Doc.Map.MarkerUnregister
Doc.Map.MultiFloorResolution
Doc.Map.PersistentMarkerWithoutActor
```

### Weather

```text
Doc.Weather.ProfileApply
Doc.Weather.RandomSelection
Doc.Weather.RegionalOverride
Doc.Weather.SaveRestore
Doc.Weather.Transition
Doc.Weather.TransitionCompletion
```

### Surface

```text
Doc.Surface.ContextModifier
Doc.Surface.ExactMatch
Doc.Surface.Fallback
Doc.Surface.NoMatch
Doc.Surface.PhysicalMaterialResolution
```

### Dialogue

```text
Doc.Dialogue.ChoiceBranch
Doc.Dialogue.Condition
Doc.Dialogue.DisabledChoice
Doc.Dialogue.End
Doc.Dialogue.HiddenChoice
Doc.Dialogue.Jump
Doc.Dialogue.LineAdvance
```

### Quest

```text
Doc.Quest.Activation
Doc.Quest.EventCount
Doc.Quest.Failure
Doc.Quest.OptionalObjective
Doc.Quest.ParallelObjectives
Doc.Quest.SaveRestore
Doc.Quest.SequentialStage
```

### Schedule

```text
Doc.Schedule.OfflineSimulation
Doc.Schedule.OverridePop
Doc.Schedule.PriorityOverride
Doc.Schedule.SaveRestore
Doc.Schedule.TimeJump
Doc.Schedule.TimeSelection
```

### Knowledge

```text
Doc.Knowledge.Discover
Doc.Knowledge.MarkRead
Doc.Knowledge.MultiStage
Doc.Knowledge.Relationships
Doc.Knowledge.SaveRestore
Doc.Knowledge.Update
```

### Inventory

```text
Doc.Inventory.Add
Doc.Inventory.Capacity
Doc.Inventory.Merge
Doc.Inventory.Remove
Doc.Inventory.SaveRestore
Doc.Inventory.Split
Doc.Inventory.TagRules
Doc.Inventory.Transfer
```

### Unlock

```text
Doc.Unlock.AndCondition
Doc.Unlock.Basic
Doc.Unlock.CycleDetection
Doc.Unlock.OrCondition
Doc.Unlock.Prerequisite
Doc.Unlock.SaveRestore
Doc.Unlock.Temporary
```

### UI

```text
Doc.UI.InputDeviceSwitch
Doc.UI.ModalBlocking
Doc.UI.NotificationQueue
Doc.UI.PushPop
Doc.UI.SettingsApply
Doc.UI.SettingsRevert
```

## C.4 Deferred and optional capability ledger

For every feature labeled BRIDGE or FUTURE, track the original name, selected profile, implementing package/provider, expected acceptance criteria, current status, and any missing native capability. Important examples are the full Dialogue Graph Editor, additional fog/projection backends, weather render integrations, Smart Objects/StateTree/Mass adapters, GAS item effects, consumer CommonUI views, and optional media/startup loading presentation.

Do not register a tag, enum value, empty factory, or menu item and treat the corresponding capability as delivered. Unsupported capability selection produces an actionable result and stays visible in the ledger.

---

# Appendix D. Foundation and Native API Compatibility Ledger

## D.1 Reuse before adding new Core contracts

The preceding handoff proposed the contracts below. Inspect their actual implementation first; document absent/incompatible contracts rather than declaring them available merely because a specification named them.

| Contract | Reuse requirement |
|---|---|
| FDocSystemResult | Preserve outcome/error/correlation semantics; add feature-specific error tags without a second result hierarchy |
| FDocGameplayContext | Explicit world/instigator/target context; extend through a compatible owner-scope contract if absent |
| FDocPersistentObjectId | Preserve world namespace + stable instance scope + local object GUID, including repeated/nested instances |
| FDocWorldObjectReference | Stable identity plus optional weak live resolution, never serialized pointer identity |
| Transient handle primitives | Request identity/generation and repeated-release behavior; feature handles remain typed |
| IDocPersistentIdentity | Independent of DocSave; feature actors/components expose existing logical identity |
| IDocGameplayTagProvider | Read-only semantic query; mutations require a separately authorized mutable provider |
| IDocWorldStateProvider | Narrow query capability, not a compulsory universal global store |
| IDocPlayerControlProvider | One shared provider per local player; caller-owned claims for overlapping UI/inspection/sequence/dialogue usage |
| Feature snapshot / DocSave adapter | Feature-owned schema and staged restore; coordinated barrier and pending-effect handling, not save-system knowledge of gameplay semantics |

`FDocOwnerScope`, pure condition outcomes, receipt/effect-key schemas, and narrow provider contracts added by this expansion are proposals, not claims that the previous Core already exposes those exact declarations. Add only truly shared primitives to Core. Keep quest, item, dialogue, weather, and UI algorithms in their respective plugins.

## D.2 Installed-engine verification ledger

Before a version-sensitive integration, fill a record containing:

```text
Integration / capability
Actual engine Build.version and build identity
Installed header and include path
Owning native module and optional plugin descriptor
Required public/private module dependencies
Reflected type and Blueprint/UHT compatibility
Minimal compile probe
Runtime fixture and target
Packaging/cook configuration
Known platform/version limitation
Evidence paths
```

Prioritize Physical Material query behavior; Asset Manager/cook registration; CommonUI viewport/input integration; Enhanced Input user settings/remapping; UGameUserSettings apply/confirm/revert; Smart Object reservation handles; StateTree/Mass lifecycle; Niagara pooling; native replication/delta serialization; and automation/validation tool invocation.

The types in this document are descriptive contracts unless they are explicitly identified native APIs. Do not invent exact C++ signatures from prose or copy an older engine example without compiling against the target. A successful minimal probe narrows risk but does not replace the feature acceptance tests.

## D.3 Compatibility decision and stop rules

An additive implementation can proceed when existing ownership, schema, and public API remain compatible. A breaking change needs a recorded decision, affected consumers/assets, migration/redirect plan, rollback path, and tests of both old and new fixtures.

Stop the affected native verification when its actual engine/toolchain/module is unavailable. Stop an unsafe schema migration before overwriting saved data. Continue only independent bounded work that does not conceal the blocker. Never change the target engine, destroy unrelated content, regenerate published identities, or rewrite functioning first-suite systems merely to make the expansion appear complete.
