# DocStreaming

Logical request layer over native Unreal level streaming (Rev 2 handoff, Section 8). It never replaces the engine's streaming, loader, HLOD or renderer.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Base backend | `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr` level instances |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags |
| Not included | World Partition bridge (streaming sources, Runtime Data Layers — STR-07), Save/Regions/Sequences bridges, Packed Level Actor handling (packed content stays governed by its owning level) |

## Concepts

| Term | Meaning |
|---|---|
| `UDocStreamingChunkDefinition` | What to load: level, default transform, dependencies, priority, keep-loaded, readiness (Loaded or Visible), timeout, load/unload distances (cm) |
| `FDocChunkInstanceKey` | Which placement: chunk id + stable instance scope. Repeated placements use different scopes; the same key with a different transform or definition is a Conflict |
| Request handle | Which caller owns the lease. Every `RequestChunk` is an independent lease |

- Dependency graphs are validated (null entries, cycles, bridge-only backends) **before** anything loads, then acquired in dependency order as independent internal leases; a later failure unwinds only that request's leases.
- Release never unloads content another lease, dependency or keep-loaded policy needs; the result reports `bReleasedButResident`.
- Desired state (leases), observed native state (polled `ELevelStreamingState`) and request outcome are separate. Native streaming gives no measurable per-level progress, so `bProgressKnown` is always false.
- Cancelling a pending request ends it as Cancelled; its late native completion changes nothing.
- Owner-bound leases are released when the owner is destroyed; world teardown cancels everything.
- World-state groups: owner-scoped variant requests (highest priority, then most recent). Release recomputes the effective variant; switching acquires the new variant before releasing the old one and is **not atomic** — wait for `IsWorldStateTransitioning() == false`.
- `UDocStreamingSourceComponent` requests chunks by distance with load/unload hysteresis.
- Blueprint async **Request Chunk And Wait** (OnReady / OnFailed; the lease stays held on success).

## Tests

`Doc.Streaming.*`: ReferenceCounting, DependencyLoad, DependencyCycle, CancelStaleCompletion, RepeatedInstances, WorldState. These disable the native backend and force observed states; real level loading (STR-06) needs a host with level assets and a cooked build.
